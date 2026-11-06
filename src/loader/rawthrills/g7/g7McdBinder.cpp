// Minecraft Dungeons Arcade card binder.
//
// The cabinet scans paper cards with a webcam; here the player picks them
// from a binder drawn over the game instead.  F3..F6 (or Select+Start on a
// pad) open it; the tabs sort the cards by kind, the cards picked make the
// hand, and Start sends the hand to the station's scan slot (g7Minecraft.c),
// one card after another, as if each was held to the camera.
//
// It is drawn with Dear ImGui on the game's GL thread, just before the frame
// is presented (g7OverlayDraw), in the look of the Odin emulator's menus:
// granite with the game showing through, corners cut at 45 degrees, strokes
// of one pixel, the Cozette pixel font at whole multiples of its size, the
// red ochre accent on the frame of focus and teal on what is chosen.
//
// The card art (373x526 PNGs named by card code, as Ultra Loader's
// binder_art) is read from dump/binder_art beside the dongle answers, or
// MCD_CARD_ART; a card without art gets a drawn tile.
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <atomic>

#include <glad/gl.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "g7McdBinder.h"

extern "C" const char *g7DumpDir(void);

#define binderLog(...) fprintf(stderr, "g7_rt: minecraft: " __VA_ARGS__)

// The Cozette font (MIT, src/fonts), in the library.
#define FONT_STR2(x) #x
#define FONT_STR(x) FONT_STR2(x)
__asm__(".section .rodata\n"
        ".balign 16\n"
        "mcdCozetteTtf:\n"
        ".incbin \"" FONT_STR(OVERLAY_FONT_PATH) "\"\n"
        "mcdCozetteTtfEnd:\n"
        ".previous\n");
extern "C" const unsigned char mcdCozetteTtf[], mcdCozetteTtfEnd[];
static const float COZETTE_SIZE = 13.0f;

// ---------------------------------------------------------------------------
// The cards.
enum { MCD_HERO, MCD_MELEE, MCD_RANGED, MCD_ARMOR, MCD_SKIN, MCD_PET, MCD_KINDS };
struct Card
{
    const char *code;
    int kind;
    const char *name, *detail;
};
static const Card cards[] =
#include "g7McdCards.inc"
    ;
enum { CARD_COUNT = sizeof cards / sizeof cards[0] };
static const char *const kindTabs[MCD_KINDS] = {"Heroes", "Melee", "Ranged", "Armor", "Skins", "Pets"};
static const char *const kindOne[MCD_KINDS] = {"Hero", "Melee", "Ranged", "Armor", "Skin", "Pet"};
static const char *const kindGlyph[MCD_KINDS] = {"H", "M", "R", "A", "S", "P"};
static int tabCards[MCD_KINDS][CARD_COUNT];
static int tabCount[MCD_KINDS];

static int cardNamed(const char *code)
{
    for (int i = 0; i < CARD_COUNT; i++)
        if (!strncmp(cards[i].code, code, 8))
            return i;
    return -1;
}

// ---------------------------------------------------------------------------
// What the input thread and the GL thread share: whether it is open, the
// buttons pressed since the last picture, and the hands.
static pthread_mutex_t shared = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<int> isOpen{0};
static int openFor = -1; // the station asked for by the last open
struct Press
{
    int button, down, pad;
};
static Press presses[64];
static int pressCount;
static int hand[MCD_STATIONS][MCD_HAND_MAX];
static int handCount[MCD_STATIONS];

extern "C" void mcdBinderOpen(int station)
{
    pthread_mutex_lock(&shared);
    openFor = station;
    pressCount = 0;
    isOpen = 1;
    pthread_mutex_unlock(&shared);
}

extern "C" void mcdBinderClose(void)
{
    isOpen = 0;
}

extern "C" int mcdBinderIsOpen(void)
{
    return isOpen;
}

extern "C" void mcdBinderButton(int button, int down, int fromPad)
{
    pthread_mutex_lock(&shared);
    if (pressCount < (int)(sizeof presses / sizeof presses[0]))
        presses[pressCount++] = {button, down, fromPad};
    pthread_mutex_unlock(&shared);
}

extern "C" void mcdBinderSetHand(int station, const char *codes)
{
    if (station < 0 || station >= MCD_STATIONS || !codes)
        return;
    pthread_mutex_lock(&shared);
    handCount[station] = 0;
    while (*codes && handCount[station] < MCD_HAND_MAX)
    {
        size_t n = strcspn(codes, ", ");
        int c = n == 8 ? cardNamed(codes) : -1;
        if (c >= 0)
            hand[station][handCount[station]++] = c;
        else if (n)
            binderLog("'%.*s' is not a card code, skipped\n", (int)n, codes);
        codes += n;
        codes += strspn(codes, ", ");
    }
    pthread_mutex_unlock(&shared);
}

extern "C" int mcdBinderHand(int station, char out[][9], int max)
{
    int n = 0;
    pthread_mutex_lock(&shared);
    for (; n < handCount[station] && n < max; n++)
    {
        memcpy(out[n], cards[hand[station][n]].code, 8);
        out[n][8] = 0;
    }
    pthread_mutex_unlock(&shared);
    return n;
}

// ---------------------------------------------------------------------------
// The card art, decoded by a thread of its own and made into textures on the
// GL thread: a half-size picture of each card for the tiles, the whole one for
// the few last looked at closely.
static char artDir[512];
static bool artChecked, artFound;
enum { ART_NONE, ART_ASKED, ART_READY, ART_MISSING };
struct Art
{
    GLuint tex;
    int state;
    unsigned lastUsed;
};
static Art thumbs[CARD_COUNT], wholes[CARD_COUNT];
static pthread_mutex_t artLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t artWake = PTHREAD_COND_INITIALIZER;
static int artAsks[2 * CARD_COUNT], artAskCount; // card * 2 + whole; the last asked first
struct Decoded
{
    int ask;
    unsigned char *pixels;
    int w, h;
};
static Decoded decoded[8];
static int decodedCount;
static const int WHOLES_KEPT = 6;

static void *artThread(void *)
{
    for (;;)
    {
        pthread_mutex_lock(&artLock);
        while (!artAskCount || decodedCount == (int)(sizeof decoded / sizeof decoded[0]))
            pthread_cond_wait(&artWake, &artLock);
        int ask = artAsks[--artAskCount];
        pthread_mutex_unlock(&artLock);

        char path[600];
        int w = 0, h = 0, n;
        snprintf(path, sizeof path, "%s/%s.png", artDir, cards[ask / 2].code);
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        if (px && !(ask & 1) && w >= 2 && h >= 2)
        {
            // the tile's picture: half the size, each pixel four averaged
            int hw = w / 2, hh = h / 2;
            unsigned char *half = (unsigned char *)malloc((size_t)hw * hh * 4);
            if (half)
            {
                for (int y = 0; y < hh; y++)
                    for (int x = 0; x < hw; x++)
                        for (int c = 0; c < 4; c++)
                        {
                            const unsigned char *s = px + ((size_t)(2 * y) * w + 2 * x) * 4 + c;
                            half[((size_t)y * hw + x) * 4 + c] =
                                (unsigned char)((s[0] + s[4] + s[(size_t)w * 4] + s[(size_t)w * 4 + 4] + 2) / 4);
                        }
                stbi_image_free(px);
                px = half;
                w = hw;
                h = hh;
            }
        }
        pthread_mutex_lock(&artLock);
        decoded[decodedCount++] = {ask, px, w, h};
        pthread_mutex_unlock(&artLock);
    }
    return NULL;
}

static void findArt(void)
{
    const char *env = getenv("MCD_CARD_ART");
    char probe[600];
    artChecked = true;
    if (env && *env)
        snprintf(artDir, sizeof artDir, "%s", env);
    else
        snprintf(artDir, sizeof artDir, "%s/binder_art", g7DumpDir() && *g7DumpDir() ? g7DumpDir() : "dump");
    snprintf(probe, sizeof probe, "%s/%s.png", artDir, cards[0].code);
    for (int i = 0; i < CARD_COUNT && !artFound; i++)
    {
        snprintf(probe, sizeof probe, "%s/%s.png", artDir, cards[i].code);
        artFound = access(probe, R_OK) == 0;
    }
    if (artFound)
    {
        pthread_t t;
        pthread_create(&t, NULL, artThread, NULL);
        pthread_detach(t);
        binderLog("card binder art from %s\n", artDir);
    }
    else
        binderLog("card binder: no card art in %s (PNGs named by card code), drawn tiles\n", artDir);
}

// Ask for that picture (the last asked is decoded first).
static void askArt(int card, bool whole)
{
    Art &a = whole ? wholes[card] : thumbs[card];
    if (!artFound || a.state != ART_NONE)
        return;
    a.state = ART_ASKED;
    pthread_mutex_lock(&artLock);
    if (artAskCount < (int)(sizeof artAsks / sizeof artAsks[0]))
        artAsks[artAskCount++] = card * 2 + (whole ? 1 : 0);
    pthread_cond_signal(&artWake);
    pthread_mutex_unlock(&artLock);
}

static GLuint makeTexture(const unsigned char *px, int w, int h, bool mipmaps)
{
    GLint oldTex = 0, oldAlign = 4, oldRow = 0, oldBuffer = 0;
    GLuint t = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlign);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &oldRow);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &oldBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    mipmaps = mipmaps && glad_glGenerateMipmap;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (mipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, (GLuint)oldTex);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)oldBuffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlign);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, oldRow);
    return t;
}

static unsigned frameNumber;

// The decoded pictures made textures, a few each picture; the whole pictures
// not looked at for longest let go.
static void takeArt(void)
{
    for (int i = 0; i < 4; i++)
    {
        Decoded d;
        pthread_mutex_lock(&artLock);
        if (!decodedCount)
        {
            pthread_mutex_unlock(&artLock);
            break;
        }
        d = decoded[0];
        memmove(decoded, decoded + 1, sizeof decoded[0] * --decodedCount);
        pthread_cond_signal(&artWake);
        pthread_mutex_unlock(&artLock);
        Art &a = d.ask & 1 ? wholes[d.ask / 2] : thumbs[d.ask / 2];
        if (d.pixels)
        {
            a.tex = makeTexture(d.pixels, d.w, d.h, !(d.ask & 1));
            a.state = ART_READY;
            a.lastUsed = frameNumber;
            free(d.pixels);
        }
        else
            a.state = ART_MISSING;
    }
    int kept = 0, oldest = -1;
    for (int c = 0; c < CARD_COUNT; c++)
        if (wholes[c].state == ART_READY)
        {
            kept++;
            if (oldest < 0 || wholes[c].lastUsed < wholes[oldest].lastUsed)
                oldest = c;
        }
    if (kept > WHOLES_KEPT && oldest >= 0)
    {
        glDeleteTextures(1, &wholes[oldest].tex);
        wholes[oldest] = {};
    }
}

// ---------------------------------------------------------------------------
// The look: Odin's Rúnasteinn palette (granite and the red ochre of painted
// runestones).
namespace look
{
static const ImU32 SCREEN = IM_COL32(0x2A, 0x2A, 0x28, 0xFF);
static const ImU32 FRAME = IM_COL32(0x2A, 0x2A, 0x28, 0xE6);
static const ImU32 INK = IM_COL32(0xEE, 0xEA, 0xE2, 0xFF);
static const ImU32 DIM = IM_COL32(0xB0, 0xAB, 0xA0, 0xFF);
static const ImU32 FAINT = IM_COL32(0x83, 0x7F, 0x75, 0xFF);
static const ImU32 LINE = IM_COL32(0xEE, 0xEA, 0xE2, 0x29);
static const ImU32 OUTLINE = IM_COL32(0xEE, 0xEA, 0xE2, 0x38);
static const ImU32 HOVER = IM_COL32(0xEE, 0xEA, 0xE2, 0x10);
static const ImU32 ACCENT = IM_COL32(0xE0, 0x66, 0x4A, 0xFF);
static const ImU32 ON_ACCENT = IM_COL32(0x16, 0x06, 0x04, 0xFF);
static const ImU32 ACTION = IM_COL32(0x6A, 0xA9, 0xB0, 0xFF);
static const ImU32 ON_ACTION = IM_COL32(0x2A, 0x2A, 0x28, 0xFF);
static const ImU32 DANGER = IM_COL32(0xEF, 0x5A, 0x50, 0xFF);
static const ImU32 TITLE = IM_COL32(0xBD, 0xB5, 0xD0, 0xFF);
static const ImU32 TILE = IM_COL32(0x1D, 0x1D, 0x1B, 0xFF);
static const ImU32 SHADE = IM_COL32(0x00, 0x00, 0x00, 0x80);
} // namespace look

static ImU32 alpha(ImU32 c, float share)
{
    unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFF;
    return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)a * share) << IM_COL32_A_SHIFT);
}

static ImDrawList *dl;
static ImFont *font;
static float S = 1;      // the scale: a whole number
static float FONT = 13;  // the font's size now, in pixels

static float px(float points)
{
    return floorf(points * S + 0.5f);
}

static void octagon(ImVec2 a, ImVec2 b, float cut, ImVec2 *p)
{
    p[0] = {a.x + cut, a.y};
    p[1] = {b.x - cut, a.y};
    p[2] = {b.x, a.y + cut};
    p[3] = {b.x, b.y - cut};
    p[4] = {b.x - cut, b.y};
    p[5] = {a.x + cut, b.y};
    p[6] = {a.x, b.y - cut};
    p[7] = {a.x, a.y + cut};
}

// A box with its corners cut, filled and stroked.
static void frame(ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 border, float strokes = 1)
{
    ImVec2 p[8];
    octagon(a, b, cut, p);
    if (fill)
        dl->AddConvexPolyFilled(p, 8, fill);
    if (border)
    {
        // the stroke inside the box, on whole pixels
        float t = px(strokes), h = t / 2;
        octagon({a.x + h, a.y + h}, {b.x - h, b.y - h}, cut > h ? cut - h * 0.4f : cut, p);
        dl->AddPolyline(p, 8, border, t, ImDrawFlags_Closed);
    }
}

static ImVec2 measure(const char *t, float size = 0, float wrap = 0)
{
    return font->CalcTextSizeA(size ? size : FONT, FLT_MAX, wrap, t);
}

static void write(ImVec2 at, ImU32 col, const char *t, float size = 0, float wrap = 0)
{
    dl->AddText(font, size ? size : FONT, {floorf(at.x), floorf(at.y)}, col, t, NULL, wrap);
}

// A title in capitals, its letters spread.
static float spacedWidth(const char *t)
{
    float w = 0;
    for (const char *c = t; *c; c++)
    {
        char one[2] = {*c, 0};
        w += measure(one).x + px(1.5f);
    }
    return w;
}
static void spaced(ImVec2 at, ImU32 col, const char *t)
{
    for (const char *c = t; *c; c++)
    {
        char one[2] = {*c, 0};
        write(at, col, one);
        at.x += measure(one).x + px(1.5f);
    }
}

// The text cut to that width, with dots.
static const char *fitted(const char *t, float width, char *buf, size_t size)
{
    if (measure(t).x <= width)
        return t;
    size_t n = strlen(t);
    if (n >= size)
        n = size - 1;
    while (n > 0)
    {
        snprintf(buf, size, "%.*s...", (int)n, t);
        if (measure(buf).x <= width)
            return buf;
        n--;
    }
    return "";
}

static bool hovered(ImVec2 a, ImVec2 b)
{
    return ImGui::IsMouseHoveringRect(a, b, false);
}
static bool clicked(ImVec2 a, ImVec2 b)
{
    return hovered(a, b) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

// A key cap and what it does; the width taken.
static float keyHint(ImVec2 at, const char *key, const char *what)
{
    ImVec2 ks = measure(key);
    float padX = px(4), h = FONT + px(4);
    frame(at, {at.x + ks.x + 2 * padX, at.y + h}, px(2), 0, look::OUTLINE);
    write({at.x + padX, at.y + px(2)}, look::DIM, key);
    float x = at.x + ks.x + 2 * padX + px(5);
    write({x, at.y + px(2)}, look::FAINT, what);
    return x + measure(what).x + px(14) - at.x;
}

// A button: action tone when it is the one to press.
static bool button(ImVec2 a, ImVec2 b, const char *label, bool action, bool enabled = true)
{
    bool over = enabled && hovered(a, b);
    ImU32 fill = !enabled ? 0 : action ? (over ? look::ACTION : alpha(look::ACTION, 0.85f)) : over ? look::HOVER : 0;
    ImU32 ink = !enabled ? look::FAINT : action ? look::ON_ACTION : look::INK;
    frame(a, b, px(3), fill, enabled ? (action ? look::ACTION : look::OUTLINE) : look::LINE);
    ImVec2 s = measure(label);
    write({(a.x + b.x - s.x) / 2, (a.y + b.y - s.y) / 2}, ink, label);
    return enabled && clicked(a, b);
}

// ---------------------------------------------------------------------------
// The binder on screen.
struct View
{
    int station = 0, tab = 0;
    int focus[MCD_KINDS] = {};
    float scroll[MCD_KINDS] = {}, scrollShown[MCD_KINDS] = {};
    bool padLast = false;
    bool held[MCD_IN_COUNT] = {};
    double heldSince[MCD_IN_COUNT] = {}, repeatedAt[MCD_IN_COUNT] = {};
    int cols = 4;
    char note[120] = "";
    double noteUntil = 0;
    bool dragging = false;
};
static View v;

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void say(const char *what)
{
    snprintf(v.note, sizeof v.note, "%s", what);
    v.noteUntil = now() + 3.0;
}

static int handIndex(int station, int card)
{
    for (int i = 0; i < handCount[station]; i++)
        if (hand[station][i] == card)
            return i;
    return -1;
}

// The card in or out of the station's hand.
static void toggleCard(int card)
{
    pthread_mutex_lock(&shared);
    int st = v.station, i = handIndex(st, card);
    if (i >= 0)
    {
        memmove(&hand[st][i], &hand[st][i + 1], sizeof(int) * (handCount[st] - i - 1));
        handCount[st]--;
    }
    else if (handCount[st] < MCD_HAND_MAX)
        hand[st][handCount[st]++] = card;
    else
    {
        char t[80];
        snprintf(t, sizeof t, "A hand holds %d cards", MCD_HAND_MAX);
        say(t);
    }
    pthread_mutex_unlock(&shared);
}

static void scanHand(void)
{
    char codes[MCD_HAND_MAX][9];
    int n = mcdBinderHand(v.station, codes, MCD_HAND_MAX);
    if (!n)
    {
        say(v.padLast ? "The hand is empty: A picks a card" : "The hand is empty: Space picks a card");
        return;
    }
    mcdPresent(v.station, codes, n);
    binderLog("binder: P%d scans %d card(s)\n", v.station + 1, n);
    isOpen = 0;
}

static void clearHand(void)
{
    pthread_mutex_lock(&shared);
    handCount[v.station] = 0;
    pthread_mutex_unlock(&shared);
}

static void moveFocus(int dx, int dy)
{
    int n = tabCount[v.tab], &f = v.focus[v.tab];
    if (!n)
        return;
    if (dx)
    {
        int col = f % v.cols;
        if ((dx < 0 && col > 0) || (dx > 0 && col < v.cols - 1 && f + 1 < n))
            f += dx;
        else if (dx < 0 && col == 0 && f > 0)
            f--; // the end of the row before
        else if (dx > 0 && f + 1 < n)
            f++;
    }
    if (dy)
    {
        int to = f + dy * v.cols;
        if (to >= 0 && to < n)
            f = to;
        else if (dy > 0 && f / v.cols < (n - 1) / v.cols)
            f = n - 1; // the last, shorter row
    }
}

static void act(int b)
{
    switch (b)
    {
    case MCD_IN_UP: moveFocus(0, -1); break;
    case MCD_IN_DOWN: moveFocus(0, 1); break;
    case MCD_IN_LEFT: moveFocus(-1, 0); break;
    case MCD_IN_RIGHT: moveFocus(1, 0); break;
    case MCD_IN_PICK:
        if (tabCount[v.tab])
            toggleCard(tabCards[v.tab][v.focus[v.tab]]);
        break;
    case MCD_IN_BACK: isOpen = 0; break;
    case MCD_IN_PLAYER: v.station = (v.station + 1) % MCD_STATIONS; break;
    case MCD_IN_TAB_PREV: v.tab = (v.tab + MCD_KINDS - 1) % MCD_KINDS; break;
    case MCD_IN_TAB_NEXT: v.tab = (v.tab + 1) % MCD_KINDS; break;
    case MCD_IN_SCAN: scanHand(); break;
    case MCD_IN_CLEAR: clearHand(); break;
    case MCD_IN_P1: case MCD_IN_P2: case MCD_IN_P3: case MCD_IN_P4: v.station = b - MCD_IN_P1; break;
    }
}

static bool repeats(int b)
{
    return b <= MCD_IN_RIGHT;
}

static void takePresses(void)
{
    Press got[64];
    int n;
    pthread_mutex_lock(&shared);
    n = pressCount;
    memcpy(got, presses, sizeof got[0] * n);
    pressCount = 0;
    if (openFor >= 0)
    {
        v.station = openFor;
        openFor = -1;
        memset(v.held, 0, sizeof v.held);
    }
    pthread_mutex_unlock(&shared);
    double t = now();
    for (int i = 0; i < n && isOpen; i++)
    {
        int b = got[i].button;
        if (b < 0 || b >= MCD_IN_COUNT)
            continue;
        v.padLast = got[i].pad;
        v.held[b] = got[i].down;
        if (got[i].down)
        {
            v.heldSince[b] = v.repeatedAt[b] = t;
            act(b);
        }
    }
    // a direction held moves again and again
    for (int b = 0; b < MCD_IN_COUNT && isOpen; b++)
        if (v.held[b] && repeats(b) && t - v.heldSince[b] > 0.35 && t - v.repeatedAt[b] > 0.085)
        {
            v.repeatedAt[b] = t;
            act(b);
        }
}

// A card's picture, or its drawn tile, in that box.
static void cardPicture(int card, ImVec2 a, ImVec2 b, bool whole)
{
    Art *art = NULL;
    if (whole)
    {
        askArt(card, true);
        if (wholes[card].state == ART_READY)
            art = &wholes[card];
    }
    if (!art)
    {
        askArt(card, false);
        if (thumbs[card].state == ART_READY)
            art = &thumbs[card];
    }
    if (art)
    {
        art->lastUsed = frameNumber;
        dl->AddImage((ImTextureID)(uintptr_t)art->tex, a, b);
        return;
    }
    // no picture (yet): the tile, the kind's letter large, the name
    const Card &c = cards[card];
    frame(a, b, px(4), look::TILE, look::LINE);
    float big = FONT * (b.x - a.x > px(150) ? 4 : 3);
    ImVec2 g = measure(kindGlyph[c.kind], big);
    write({(a.x + b.x - g.x) / 2, a.y + (b.y - a.y) * 0.22f}, alpha(look::TITLE, 0.35f), kindGlyph[c.kind], big);
    float wrap = b.x - a.x - px(10);
    ImVec2 ns = measure(c.name, 0, wrap);
    float ty = fminf(a.y + (b.y - a.y) * 0.62f, b.y - ns.y - px(6));
    dl->PushClipRect(a, b, true);
    write({a.x + px(5) + (wrap - ns.x) / 2, ty}, look::DIM, c.name, 0, wrap);
    dl->PopClipRect();
    if (artChecked && !artFound)
        return;
    if (thumbs[card].state == ART_ASKED)
    {
        ImVec2 ls = measure("...");
        write({(a.x + b.x - ls.x) / 2, b.y - FONT - px(6)}, look::FAINT, "...");
    }
}

static const float CARD_ASPECT = 526.0f / 373.0f;

static void drawGrid(ImVec2 a, ImVec2 b)
{
    int n = tabCount[v.tab];
    float gap = px(8), minTile = px(84);
    float w = b.x - a.x - px(10); // room for the scroll bar
    v.cols = (int)((w + gap) / (minTile + gap));
    if (v.cols < 2)
        v.cols = 2;
    float tw = floorf((w - gap * (v.cols - 1)) / v.cols), th = floorf(tw * CARD_ASPECT);
    float rowH = th + FONT + px(6) + gap;
    int rows = (n + v.cols - 1) / v.cols;
    float total = rows * rowH - gap, viewH = b.y - a.y, maxScroll = total > viewH ? total - viewH : 0;
    int &f = v.focus[v.tab];
    if (f >= n)
        f = n ? n - 1 : 0;

    // the frame of focus kept in view, unless the bar is being dragged
    float &sc = v.scroll[v.tab], &shown = v.scrollShown[v.tab];
    if (!v.dragging)
    {
        float top = (f / v.cols) * rowH, bottom = top + rowH - gap;
        if (top < sc)
            sc = top;
        if (bottom > sc + viewH)
            sc = bottom - viewH;
    }
    sc = sc < 0 ? 0 : sc > maxScroll ? maxScroll : sc;
    shown += (sc - shown) * fminf(1.0f, ImGui::GetIO().DeltaTime * 16);
    if (fabsf(sc - shown) < 0.5f)
        shown = sc;

    dl->PushClipRect({a.x - px(4), a.y - px(4)}, {b.x + px(4), b.y + px(4)}, true);
    char buf[96];
    for (int i = 0; i < n; i++)
    {
        int r = i / v.cols, c = i % v.cols, card = tabCards[v.tab][i];
        ImVec2 ta = {a.x + c * (tw + gap), a.y + r * rowH - floorf(shown)};
        ImVec2 tb = {ta.x + tw, ta.y + th};
        if (tb.y + FONT + px(6) < a.y - px(4) || ta.y > b.y + px(4))
            continue;
        bool over = hovered(ta, {tb.x, tb.y + FONT + px(6)}) && hovered(a, b);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            f = i;
            toggleCard(card);
        }
        cardPicture(card, ta, tb, false);
        int inHand = handIndex(v.station, card);
        if (over)
            frame(ta, tb, px(4), look::HOVER, 0);
        if (inHand >= 0)
        {
            frame(ta, tb, px(4), alpha(look::ACTION, 0.18f), look::ACTION, 2);
            char num[4];
            snprintf(num, sizeof num, "%d", inHand + 1);
            float bs = FONT + px(6);
            ImVec2 ba = {tb.x - bs - px(4), ta.y + px(4)};
            frame(ba, {ba.x + bs, ba.y + bs}, px(3), look::ACTION, 0);
            ImVec2 ns = measure(num);
            write({ba.x + (bs - ns.x) / 2, ba.y + (bs - ns.y) / 2}, look::ON_ACTION, num);
        }
        if (i == f)
            frame({ta.x - px(3), ta.y - px(3)}, {tb.x + px(3), tb.y + px(3)}, px(5), 0, look::ACCENT, 2);
        const char *name = fitted(cards[card].name, tw, buf, sizeof buf);
        ImVec2 ns = measure(name);
        write({ta.x + (tw - ns.x) / 2, tb.y + px(3)}, i == f ? look::INK : inHand >= 0 ? look::ACTION : look::DIM,
              name);
    }
    dl->PopClipRect();

    // the scroll bar
    if (maxScroll > 0)
    {
        float x = b.x - px(3), barTop = a.y, barH = viewH;
        float thumbH = fmaxf(px(24), barH * viewH / total), thumbY = barTop + (barH - thumbH) * (shown / maxScroll);
        dl->AddRectFilled({x - px(1), barTop}, {x + px(1), barTop + barH}, look::LINE);
        ImVec2 ka = {x - px(3), thumbY}, kb = {x + px(3), thumbY + thumbH};
        bool overBar = hovered({x - px(8), barTop}, {x + px(8), barTop + barH});
        if (overBar && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            v.dragging = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            v.dragging = false;
        if (v.dragging)
        {
            float to = (ImGui::GetIO().MousePos.y - barTop - thumbH / 2) / (barH - thumbH);
            sc = shown = (to < 0 ? 0 : to > 1 ? 1 : to) * maxScroll;
        }
        frame(ka, kb, px(2), v.dragging || overBar ? look::DIM : look::FAINT, 0);
    }
    if (!n)
        write(a, look::FAINT, "No card of this kind");
}

static void drawSide(ImVec2 a, ImVec2 b)
{
    char buf[128];
    int n = tabCount[v.tab];
    float y = a.y, w = b.x - a.x;
    // the card under the frame, large
    if (n)
    {
        int card = tabCards[v.tab][v.focus[v.tab] < n ? v.focus[v.tab] : 0];
        const Card &c = cards[card];
        float ph = fminf((b.y - a.y) * 0.42f, w * CARD_ASPECT), pw = floorf(ph / CARD_ASPECT);
        ImVec2 pa = {a.x + floorf((w - pw) / 2), y};
        cardPicture(card, pa, {pa.x + pw, pa.y + ph}, true);
        y += ph + px(8);
        write({a.x, y}, look::INK, fitted(c.name, w, buf, sizeof buf));
        y += FONT + px(3);
        // the series is the code's first letter; from the fifth on, cards of
        // the game's table never (or not all) handed out
        int series = c.code[0] - 'a' + 1;
        snprintf(buf, sizeof buf, "%s  Series %d  %s", kindOne[c.kind], series, c.code);
        write({a.x, y}, look::FAINT, buf);
        if (series >= 5)
        {
            y += FONT + px(3);
            write({a.x, y}, look::ACCENT, "Not released on the cabinets", 0, w);
        }
        y += FONT + px(3);
        if (*c.detail)
        {
            write({a.x, y}, look::DIM, c.detail, 0, w);
            y += measure(c.detail, 0, w).y;
        }
        y += px(10);
    }

    // the hand
    dl->AddRectFilled({a.x, y}, {b.x, y + px(1)}, look::LINE);
    y += px(7);
    snprintf(buf, sizeof buf, "HAND P%d", v.station + 1);
    spaced({a.x, y}, look::TITLE, buf);
    pthread_mutex_lock(&shared);
    int count = handCount[v.station], mine[MCD_HAND_MAX];
    memcpy(mine, hand[v.station], sizeof mine);
    pthread_mutex_unlock(&shared);
    snprintf(buf, sizeof buf, "%d/%d", count, MCD_HAND_MAX);
    write({b.x - measure(buf).x, y}, look::FAINT, buf);
    y += FONT + px(6);
    float rowH = FONT + px(5), buttonsH = FONT + px(10);
    float listBottom = b.y - 2 * buttonsH - px(8);
    int removeAt = -1;
    for (int i = 0; i < count; i++)
    {
        if (y + 2 * rowH > listBottom && i + 1 < count)
        {
            snprintf(buf, sizeof buf, "+%d more", count - i);
            write({a.x + px(16), y + px(2)}, look::FAINT, buf);
            break;
        }
        const Card &c = cards[mine[i]];
        ImVec2 ra = {a.x, y}, rb = {b.x, y + rowH};
        bool over = hovered(ra, rb);
        if (over)
            frame(ra, rb, px(2), look::HOVER, 0);
        snprintf(buf, sizeof buf, "%d", i + 1);
        write({a.x + px(3), y + px(2)}, look::ACTION, buf);
        const char *kind = kindOne[c.kind];
        float kw = measure(kind).x;
        char nb[96];
        write({a.x + px(16), y + px(2)}, look::INK, fitted(c.name, w - px(16) - kw - px(26), nb, sizeof nb));
        write({b.x - kw - px(18), y + px(2)}, look::FAINT, kind);
        write({b.x - px(10), y + px(2)}, over ? look::DANGER : look::FAINT, "x");
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            removeAt = i;
        y += rowH;
    }
    if (!count)
        write({a.x, y + px(2)}, look::FAINT, v.padLast ? "A picks a card" : "Space or a click picks a card", 0, w);
    if (removeAt >= 0)
        toggleCard(mine[removeAt]);

    // what to do with it
    float by = b.y - 2 * buttonsH - px(6);
    snprintf(buf, sizeof buf, "Scan P%d", v.station + 1);
    if (button({a.x, by}, {b.x, by + buttonsH}, buf, true, count > 0))
        scanHand();
    by += buttonsH + px(6);
    float half = floorf((w - px(6)) / 2);
    if (button({a.x, by}, {a.x + half, by + buttonsH}, "Clear", false, count > 0))
        clearHand();
    if (button({b.x - half, by}, {b.x, by + buttonsH}, "Close", false))
        isOpen = 0;
}

static void drawBinder(int W, int H)
{
    char buf[128];
    dl->AddRectFilled({0, 0}, {(float)W, (float)H}, look::SHADE);
    float m = fmaxf(px(16), floorf(fminf(W, H) * 0.04f));
    ImVec2 a = {m, m}, b = {W - m, H - m};
    if (b.x - a.x > px(1500))
    {
        float cut = ((b.x - a.x) - px(1500)) / 2;
        a.x += cut;
        b.x -= cut;
    }
    frame(a, b, px(6), look::FRAME, look::OUTLINE);
    float pad = px(14), x0 = a.x + pad, x1 = b.x - pad, y = a.y + pad;

    // the head: the title, the stations, whether the game takes cards now
    spaced({x0, y + px(2)}, look::TITLE, "CARD BINDER");
    float tx = x0 + spacedWidth("CARD BINDER") + px(10);
    write({tx, y + px(2)}, look::FAINT, "Minecraft Dungeons Arcade");
    float segW = measure("P4").x + px(16), segH = FONT + px(8), sx = x1 - 4 * segW - 3 * px(4);
    for (int p = 0; p < MCD_STATIONS; p++)
    {
        ImVec2 sa = {sx + p * (segW + px(4)), y}, sb = {sa.x + segW, y + segH};
        bool on = p == v.station, over = hovered(sa, sb);
        frame(sa, sb, px(3), on ? look::ACTION : over ? look::HOVER : 0, on ? look::ACTION : look::OUTLINE);
        snprintf(buf, sizeof buf, "P%d", p + 1);
        ImVec2 s = measure(buf);
        write({sa.x + (segW - s.x) / 2, sa.y + (segH - s.y) / 2}, on ? look::ON_ACTION : look::DIM, buf);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            v.station = p;
    }
    int ready = mcdScanScreen(), waiting = mcdWaiting(v.station);
    if (waiting)
        snprintf(buf, sizeof buf, "%d card(s) wait for a scan screen", waiting);
    else
        snprintf(buf, sizeof buf, "%s", ready ? "The game takes cards now" : "Cards wait for a scan screen");
    float stW = measure(buf).x;
    float dot = px(6), stX = sx - px(18) - stW;
    dl->AddRectFilled({stX - dot - px(6), y + (segH - dot) / 2}, {stX - px(6), y + (segH + dot) / 2},
                      ready ? look::ACTION : look::ACCENT);
    write({stX, y + (segH - FONT) / 2}, ready ? look::DIM : look::FAINT, buf);
    y += segH + px(10);

    // the tabs, LB and RB at their ends
    float tabH = FONT + px(10), lbW = measure(v.padLast ? "LB" : "Q").x + px(8);
    float tabW[MCD_KINDS];
    for (int k = 0; k < MCD_KINDS; k++)
    {
        snprintf(buf, sizeof buf, "%s %d", kindTabs[k], tabCount[k]);
        tabW[k] = measure(buf).x + px(20);
    }
    float tabX = x0 + lbW + px(8);
    frame({x0, y + px(3)}, {x0 + lbW, y + tabH - px(3)}, px(2), 0, look::OUTLINE);
    write({x0 + px(4), y + (tabH - FONT) / 2}, look::DIM, v.padLast ? "LB" : "Q");
    for (int k = 0; k < MCD_KINDS; k++)
    {
        ImVec2 ta = {tabX, y}, tb = {tabX + tabW[k], y + tabH};
        bool on = k == v.tab, over = hovered(ta, tb);
        frame(ta, tb, px(4), on ? alpha(look::ACTION, 0.16f) : over ? look::HOVER : 0,
              on ? look::ACTION : look::LINE);
        snprintf(buf, sizeof buf, "%s", kindTabs[k]);
        write({ta.x + px(10), y + (tabH - FONT) / 2}, on ? look::INK : look::DIM, buf);
        snprintf(buf, sizeof buf, "%d", tabCount[k]);
        write({tb.x - px(10) - measure(buf).x, y + (tabH - FONT) / 2}, on ? look::ACTION : look::FAINT, buf);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            v.tab = k;
        tabX += tabW[k] + px(4);
    }
    frame({tabX + px(4), y + px(3)}, {tabX + px(4) + lbW, y + tabH - px(3)}, px(2), 0, look::OUTLINE);
    write({tabX + px(8), y + (tabH - FONT) / 2}, look::DIM, v.padLast ? "RB" : "E");
    y += tabH + px(10);

    // the foot: the keys, and a word when there is one
    float footH = FONT + px(4), footY = b.y - pad - footH;
    dl->AddRectFilled({a.x + px(1), footY - px(8)}, {b.x - px(1), footY - px(7)}, look::LINE);
    float kx = x0;
    if (v.padLast)
    {
        kx += keyHint({kx, footY}, "D-pad", "move");
        kx += keyHint({kx, footY}, "A", "pick");
        kx += keyHint({kx, footY}, "LB RB", "kind");
        kx += keyHint({kx, footY}, "Y", "player");
        kx += keyHint({kx, footY}, "Select", "clear");
        kx += keyHint({kx, footY}, "Start", "scan");
        kx += keyHint({kx, footY}, "B", "close");
    }
    else
    {
        kx += keyHint({kx, footY}, "Arrows", "move");
        kx += keyHint({kx, footY}, "Space", "pick");
        kx += keyHint({kx, footY}, "Q E", "kind");
        kx += keyHint({kx, footY}, "1-4", "player");
        kx += keyHint({kx, footY}, "Tab", "clear");
        kx += keyHint({kx, footY}, "Enter", "scan");
        kx += keyHint({kx, footY}, "Bksp", "close");
    }
    if (*v.note && now() < v.noteUntil)
    {
        float nw = measure(v.note).x;
        write({x1 - nw, footY + px(2)}, look::ACCENT, v.note);
    }

    // the body: the cards, and at the right the one under the frame and the hand
    float sideW = floorf(fminf(fmaxf((x1 - x0) * 0.27f, px(200)), px(380)));
    float bodyBottom = footY - px(16);
    drawGrid({x0 + px(4), y + px(4)}, {x1 - sideW - px(16), bodyBottom});
    dl->AddRectFilled({x1 - sideW - px(8), y}, {x1 - sideW - px(7), bodyBottom}, look::LINE);
    drawSide({x1 - sideW, y + px(4)}, {x1, bodyBottom});
}

// ---------------------------------------------------------------------------
// Each picture, on the GL thread.
typedef int (*QueryPointerFn)(void *, unsigned long, unsigned long *, unsigned long *, int *, int *, int *, int *,
                              unsigned *);
static QueryPointerFn queryPointer;
static bool ready, failed;
static double lastTime;

static const char *glslVersion(void)
{
    const char *v = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int major = 1, minor = 10;
    if (v)
        sscanf(v, "%d.%d", &major, &minor);
    int n = major * 100 + minor;
    return n >= 150 ? "#version 150" : n >= 130 ? "#version 130" : "#version 120";
}

static bool setUp(void)
{
    for (int i = 0; i < CARD_COUNT; i++)
        tabCards[cards[i].kind][tabCount[cards[i].kind]++] = i;
    void *x11 = dlopen("libX11.so.6", RTLD_NOW);
    queryPointer = x11 ? (QueryPointerFn)dlsym(x11, "XQueryPointer") : NULL;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.PixelSnapH = true;
    cfg.OversampleH = cfg.OversampleV = 1;
    font = io.Fonts->AddFontFromMemoryTTF((void *)mcdCozetteTtf, (int)(mcdCozetteTtfEnd - mcdCozetteTtf), COZETTE_SIZE,
                                          &cfg);
    if (!font)
        font = io.Fonts->AddFontDefault();
    const char *glsl = glslVersion();
    if (!ImGui_ImplOpenGL3_Init(glsl))
    {
        binderLog("card binder: the GL renderer would not start (%s), no binder\n", glsl);
        return false;
    }
    binderLog("card binder ready (GL %s, %s)\n", (const char *)glGetString(GL_VERSION), glsl);
    lastTime = now();
    return true;
}

extern "C" void mcdBinderDraw(void *display, unsigned long drawable, int W, int H)
{
    if (!isOpen || failed)
        return;
    if (!ready)
    {
        if (!setUp())
        {
            failed = true;
            isOpen = 0;
            return;
        }
        ready = true;
    }
    if (!artChecked)
        findArt();
    frameNumber++;
    takeArt();

    ImGuiIO &io = ImGui::GetIO();
    double t = now();
    io.DeltaTime = (float)(t - lastTime > 0 ? (t - lastTime < 0.25 ? t - lastTime : 0.25) : 1.0 / 60);
    lastTime = t;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    if (queryPointer && display)
    {
        unsigned long root, child;
        int rx, ry, wx, wy;
        unsigned mask = 0;
        if (queryPointer(display, drawable, &root, &child, &rx, &ry, &wx, &wy, &mask))
        {
            io.AddMousePosEvent((float)wx, (float)wy);
            io.AddMouseButtonEvent(0, (mask & (1u << 8)) != 0);
        }
    }
    // the scale: a whole number, the font crisp
    S = (float)(int)(H / 600.0f + 0.5f);
    if (S < 1)
        S = 1;
    FONT = COZETTE_SIZE * S;

    takePresses();

    GLint oldFb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // the game draws with sRGB encoding on: the palette is already sRGB
    GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(font, FONT);
    dl = ImGui::GetBackgroundDrawList();
    if (isOpen)
        drawBinder(W, H);
    ImGui::PopFont();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)oldFb);
}
