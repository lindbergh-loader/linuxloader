// Marvel Contest of Champions card binder.
//
// The cabinet reads the QR code of each hero card held up to its cameras;
// here a player can pick the cards from a binder drawn over the game
// instead. On the scan screen the stick pushed down opens the player's
// binder; the cards picked make the hand (a team: three), and Start hands
// them to the scan screen (mcocPlugins.c) one after another, as if each was
// held up to the camera.
//
// It is drawn with Dear ImGui on the game's GL thread, just before the frame
// is presented (unityVideo.c), in the look of the Minecraft Dungeons binder
// (g7McdBinder.cpp): granite with the game showing through, corners cut at
// 45 degrees, the Cozette pixel font at whole multiples of its size, the red
// ochre accent on the frame of focus and teal on what is chosen.
//
// The card art (PNGs named by card code) is read from dump/binder_art in the
// game's directory, or MCOC_CARD_ART; a card without art gets a drawn tile.
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

#include "mcocBinder.h"

#define binderLog(...) fprintf(stderr, "unity: mcoc: " __VA_ARGS__)

// The Cozette font (MIT, src/fonts), in the library.
#define FONT_STR2(x) #x
#define FONT_STR(x) FONT_STR2(x)
__asm__(".section .rodata\n"
        ".balign 16\n"
        "mcocCozetteTtf:\n"
        ".incbin \"" FONT_STR(OVERLAY_FONT_PATH) "\"\n"
        "mcocCozetteTtfEnd:\n"
        ".previous\n");
extern "C" const unsigned char mcocCozetteTtf[], mcocCozetteTtfEnd[];
static const float COZETTE_SIZE = 13.0f;

// ---------------------------------------------------------------------------
// The cards: the game's table of codes (CameraManager.SetupBarcodeMap), the
// Series 2 cards first in the order of their fronts.
struct Card
{
    const char *code, *hero, *name, *detail;
    int series;
};
static const Card cards[] =
#include "mcocCards.inc"
    ;
enum { CARD_COUNT = sizeof cards / sizeof cards[0] };
enum { TABS = 2 };
static const char *const tabNames[TABS] = {"Series 2", "Other codes"};
static int tabCards[TABS][CARD_COUNT];
static int tabCount[TABS];

// ---------------------------------------------------------------------------
// What the main thread and the GL thread share.
static pthread_mutex_t shared = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<int> owner{-1};      // the player it is open for
static std::atomic<int> onScanScreen{0};
static int openFor = -1;                // asked by the last open, for the GL thread
struct Press
{
    int button, down;
};
static Press presses[64];
static int pressCount;
static int hand[MCOC_PLAYERS][MCOC_HAND_MAX];
static int handCount[MCOC_PLAYERS];

extern "C" void mcocBinderOpen(int player)
{
    if (player < 0 || player >= MCOC_PLAYERS)
        return;
    pthread_mutex_lock(&shared);
    openFor = player;
    pressCount = 0;
    owner = player;
    pthread_mutex_unlock(&shared);
}

extern "C" void mcocBinderClose(void)
{
    owner = -1;
}

extern "C" int mcocBinderOwner(void)
{
    return owner;
}

extern "C" void mcocBinderScanScreen(int shown)
{
    onScanScreen = shown;
}

extern "C" void mcocBinderButton(int button, int down)
{
    pthread_mutex_lock(&shared);
    if (pressCount < (int)(sizeof presses / sizeof presses[0]))
        presses[pressCount++] = {button, down};
    pthread_mutex_unlock(&shared);
}

// ---------------------------------------------------------------------------
// The card art, decoded by a thread of its own and made into textures on the
// GL thread.
static char artDir[512];
static bool artChecked, artFound;
enum { ART_NONE, ART_ASKED, ART_READY, ART_MISSING };
struct Art
{
    GLuint tex;
    int state;
};
static Art arts[CARD_COUNT];
static pthread_mutex_t artLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t artWake = PTHREAD_COND_INITIALIZER;
static int artAsks[CARD_COUNT], artAskCount; // the last asked first
struct Decoded
{
    int card;
    unsigned char *pixels;
    int w, h;
};
static Decoded decoded[8];
static int decodedCount;

static void *artThread(void *)
{
    for (;;)
    {
        pthread_mutex_lock(&artLock);
        while (!artAskCount || decodedCount == (int)(sizeof decoded / sizeof decoded[0]))
            pthread_cond_wait(&artWake, &artLock);
        int card = artAsks[--artAskCount];
        pthread_mutex_unlock(&artLock);

        char path[600];
        int w = 0, h = 0, n;
        snprintf(path, sizeof path, "%s/%s.png", artDir, cards[card].code);
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        pthread_mutex_lock(&artLock);
        decoded[decodedCount++] = {card, px, w, h};
        pthread_mutex_unlock(&artLock);
    }
    return NULL;
}

static void findArt(void)
{
    const char *env = getenv("MCOC_CARD_ART");
    artChecked = true;
    // The game's directory is the run tree's (linuxloader64 links its entries).
    snprintf(artDir, sizeof artDir, "%s", env && *env ? env : "dump/binder_art");
    for (int i = 0; i < CARD_COUNT && !artFound; i++)
    {
        char probe[600];
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

static void askArt(int card)
{
    Art &a = arts[card];
    if (!artFound || a.state != ART_NONE)
        return;
    a.state = ART_ASKED;
    pthread_mutex_lock(&artLock);
    if (artAskCount < (int)(sizeof artAsks / sizeof artAsks[0]))
        artAsks[artAskCount++] = card;
    pthread_cond_signal(&artWake);
    pthread_mutex_unlock(&artLock);
}

static GLuint makeTexture(const unsigned char *px, int w, int h)
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
    bool mipmaps = glad_glGenerateMipmap != NULL;
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

// The decoded pictures made textures, a few each picture (kept: 120 cards of
// 373x526 are some 90 MB).
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
        Art &a = arts[d.card];
        if (d.pixels)
        {
            a.tex = makeTexture(d.pixels, d.w, d.h);
            a.state = ART_READY;
            stbi_image_free(d.pixels);
        }
        else
            a.state = ART_MISSING;
    }
}

// ---------------------------------------------------------------------------
// The look (Odin's Runasteinn palette, as the Minecraft binder's).
namespace look
{
static const ImU32 FRAME = IM_COL32(0x2A, 0x2A, 0x28, 0xE6);
static const ImU32 INK = IM_COL32(0xEE, 0xEA, 0xE2, 0xFF);
static const ImU32 DIM = IM_COL32(0xB0, 0xAB, 0xA0, 0xFF);
static const ImU32 FAINT = IM_COL32(0x83, 0x7F, 0x75, 0xFF);
static const ImU32 LINE = IM_COL32(0xEE, 0xEA, 0xE2, 0x29);
static const ImU32 OUTLINE = IM_COL32(0xEE, 0xEA, 0xE2, 0x38);
static const ImU32 HOVER = IM_COL32(0xEE, 0xEA, 0xE2, 0x10);
static const ImU32 ACCENT = IM_COL32(0xE0, 0x66, 0x4A, 0xFF);
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
static float S = 1;     // the scale: a whole number
static float FONT = 13; // the font's size now, in pixels

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

// A card's full name: "Hulk (Ragnarok)".
static const char *fullName(const Card &c, char *buf, size_t size)
{
    if (!*c.detail)
        return c.name;
    snprintf(buf, size, "%s (%s)", c.name, c.detail);
    return buf;
}

// ---------------------------------------------------------------------------
// The binder on screen.
struct View
{
    int player = 0, tab = 0;
    int focus[TABS] = {};
    float scroll[TABS] = {}, scrollShown[TABS] = {};
    bool held[MCOC_IN_COUNT] = {};
    double heldSince[MCOC_IN_COUNT] = {}, repeatedAt[MCOC_IN_COUNT] = {};
    int cols = 6;
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

static void closeBinder(void)
{
    owner = -1;
}

static int handIndex(int player, int card)
{
    for (int i = 0; i < handCount[player]; i++)
        if (hand[player][i] == card)
            return i;
    return -1;
}

static void toggleCard(int card)
{
    pthread_mutex_lock(&shared);
    int p = v.player, i = handIndex(p, card);
    if (i >= 0)
    {
        memmove(&hand[p][i], &hand[p][i + 1], sizeof(int) * (handCount[p] - i - 1));
        handCount[p]--;
    }
    else if (handCount[p] < MCOC_HAND_MAX)
        hand[p][handCount[p]++] = card;
    else
        say("A team is 3 champions");
    pthread_mutex_unlock(&shared);
}

static void scanHand(void)
{
    char codes[MCOC_HAND_MAX][9];
    int n;
    pthread_mutex_lock(&shared);
    for (n = 0; n < handCount[v.player]; n++)
    {
        memcpy(codes[n], cards[hand[v.player][n]].code, 8);
        codes[n][8] = 0;
    }
    pthread_mutex_unlock(&shared);
    if (!n)
    {
        say("The hand is empty: Light attack picks a card");
        return;
    }
    mcocPresent(v.player, codes, n);
    binderLog("binder: P%d scans %d card(s)\n", v.player + 1, n);
    closeBinder();
}

static void clearHand(void)
{
    pthread_mutex_lock(&shared);
    handCount[v.player] = 0;
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
            f--;
        else if (dx > 0 && f + 1 < n)
            f++;
    }
    if (dy)
    {
        int to = f + dy * v.cols;
        if (to >= 0 && to < n)
            f = to;
        else if (dy > 0 && f / v.cols < (n - 1) / v.cols)
            f = n - 1;
    }
}

static void act(int b)
{
    switch (b)
    {
    case MCOC_IN_UP: moveFocus(0, -1); break;
    case MCOC_IN_DOWN: moveFocus(0, 1); break;
    case MCOC_IN_LEFT: moveFocus(-1, 0); break;
    case MCOC_IN_RIGHT: moveFocus(1, 0); break;
    case MCOC_IN_PICK:
        if (tabCount[v.tab])
            toggleCard(tabCards[v.tab][v.focus[v.tab]]);
        break;
    case MCOC_IN_BACK: closeBinder(); break;
    case MCOC_IN_CLEAR: clearHand(); break;
    case MCOC_IN_TAB: v.tab = (v.tab + 1) % TABS; break;
    case MCOC_IN_SCAN: scanHand(); break;
    }
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
        v.player = openFor;
        openFor = -1;
        memset(v.held, 0, sizeof v.held);
    }
    pthread_mutex_unlock(&shared);
    double t = now();
    for (int i = 0; i < n && owner >= 0; i++)
    {
        int b = got[i].button;
        if (b < 0 || b >= MCOC_IN_COUNT)
            continue;
        v.held[b] = got[i].down;
        if (got[i].down)
        {
            v.heldSince[b] = v.repeatedAt[b] = t;
            act(b);
        }
    }
    // a direction held moves again and again
    for (int b = MCOC_IN_UP; b <= MCOC_IN_RIGHT && owner >= 0; b++)
        if (v.held[b] && t - v.heldSince[b] > 0.35 && t - v.repeatedAt[b] > 0.085)
        {
            v.repeatedAt[b] = t;
            act(b);
        }
}

static void cardPicture(int card, ImVec2 a, ImVec2 b)
{
    askArt(card);
    if (arts[card].state == ART_READY)
    {
        dl->AddImage((ImTextureID)(uintptr_t)arts[card].tex, a, b);
        return;
    }
    // no picture (yet): a tile with the name
    const Card &c = cards[card];
    char buf[96];
    frame(a, b, px(4), look::TILE, look::LINE);
    float wrap = b.x - a.x - px(10);
    const char *name = fullName(c, buf, sizeof buf);
    ImVec2 ns = measure(name, 0, wrap);
    dl->PushClipRect(a, b, true);
    write({a.x + px(5) + (wrap - ns.x) / 2, (a.y + b.y - ns.y) / 2}, look::DIM, name, 0, wrap);
    dl->PopClipRect();
}

static const float CARD_ASPECT = 1016.0f / 721.0f;

static void drawGrid(ImVec2 a, ImVec2 b)
{
    int n = tabCount[v.tab];
    float gap = px(8), minTile = px(84);
    float w = b.x - a.x - px(10);
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
        cardPicture(card, ta, tb);
        int inHand = handIndex(v.player, card);
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
        char full[96];
        const char *name = fitted(fullName(cards[card], full, sizeof full), tw, buf, sizeof buf);
        ImVec2 ns = measure(name);
        write({ta.x + (tw - ns.x) / 2, tb.y + px(3)}, i == f ? look::INK : inHand >= 0 ? look::ACTION : look::DIM,
              name);
    }
    dl->PopClipRect();

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
}

static void drawSide(ImVec2 a, ImVec2 b)
{
    char buf[128], full[96];
    int n = tabCount[v.tab];
    float y = a.y, w = b.x - a.x;
    // the card under the frame, large
    if (n)
    {
        int card = tabCards[v.tab][v.focus[v.tab] < n ? v.focus[v.tab] : 0];
        const Card &c = cards[card];
        float ph = fminf((b.y - a.y) * 0.5f, w * CARD_ASPECT), pw = floorf(ph / CARD_ASPECT);
        ImVec2 pa = {a.x + floorf((w - pw) / 2), y};
        cardPicture(card, pa, {pa.x + pw, pa.y + ph});
        y += ph + px(8);
        write({a.x, y}, look::INK, fitted(fullName(c, full, sizeof full), w, buf, sizeof buf));
        y += FONT + px(3);
        if (c.series)
            snprintf(buf, sizeof buf, "Series %d  %s", c.series, c.code);
        else
            snprintf(buf, sizeof buf, "%s  %s", c.hero, c.code);
        write({a.x, y}, look::FAINT, fitted(buf, w, full, sizeof full));
        y += FONT + px(10);
    }

    // the hand
    dl->AddRectFilled({a.x, y}, {b.x, y + px(1)}, look::LINE);
    y += px(7);
    snprintf(buf, sizeof buf, "TEAM P%d", v.player + 1);
    spaced({a.x, y}, look::TITLE, buf);
    pthread_mutex_lock(&shared);
    int count = handCount[v.player], mine[MCOC_HAND_MAX];
    memcpy(mine, hand[v.player], sizeof mine);
    pthread_mutex_unlock(&shared);
    snprintf(buf, sizeof buf, "%d/%d", count, MCOC_HAND_MAX);
    write({b.x - measure(buf).x, y}, look::FAINT, buf);
    y += FONT + px(6);
    float rowH = FONT + px(5), buttonsH = FONT + px(10);
    int removeAt = -1;
    for (int i = 0; i < count; i++)
    {
        const Card &c = cards[mine[i]];
        ImVec2 ra = {a.x, y}, rb = {b.x, y + rowH};
        bool over = hovered(ra, rb);
        if (over)
            frame(ra, rb, px(2), look::HOVER, 0);
        snprintf(buf, sizeof buf, "%d", i + 1);
        write({a.x + px(3), y + px(2)}, look::ACTION, buf);
        char nb[96];
        write({a.x + px(16), y + px(2)}, look::INK, fitted(fullName(c, full, sizeof full), w - px(30), nb, sizeof nb));
        write({b.x - px(10), y + px(2)}, over ? look::DANGER : look::FAINT, "x");
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            removeAt = i;
        y += rowH;
    }
    if (!count)
        write({a.x, y + px(2)}, look::FAINT, "Light attack or a click picks a card", 0, w);
    if (removeAt >= 0)
        toggleCard(mine[removeAt]);

    float by = b.y - 2 * buttonsH - px(6);
    snprintf(buf, sizeof buf, "Scan P%d", v.player + 1);
    if (button({a.x, by}, {b.x, by + buttonsH}, buf, true, count > 0))
        scanHand();
    by += buttonsH + px(6);
    float half = floorf((w - px(6)) / 2);
    if (button({a.x, by}, {a.x + half, by + buttonsH}, "Clear", false, count > 0))
        clearHand();
    if (button({b.x - half, by}, {b.x, by + buttonsH}, "Close", false))
        closeBinder();
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

    // the head: the title, the player, whether the game takes the cards now
    spaced({x0, y + px(2)}, look::TITLE, "CARD BINDER");
    float tx = x0 + spacedWidth("CARD BINDER") + px(10);
    snprintf(buf, sizeof buf, "Player %d", v.player + 1);
    write({tx, y + px(2)}, look::ACTION, buf);
    float segH = FONT + px(8);
    int ready = mcocScanning(v.player), waiting = mcocWaiting(v.player);
    if (waiting)
        snprintf(buf, sizeof buf, "%d card(s) wait to be scanned", waiting);
    else
        snprintf(buf, sizeof buf, "%s", ready ? "The game takes cards now" : "Cards wait for the player to join");
    float stW = measure(buf).x, dot = px(6), stX = x1 - stW;
    dl->AddRectFilled({stX - dot - px(6), y + (segH - dot) / 2}, {stX - px(6), y + (segH + dot) / 2},
                      ready ? look::ACTION : look::ACCENT);
    write({stX, y + (segH - FONT) / 2}, ready ? look::DIM : look::FAINT, buf);
    y += segH + px(10);

    // the tabs
    float tabH = FONT + px(10), tabX = x0;
    for (int k = 0; k < TABS; k++)
    {
        snprintf(buf, sizeof buf, "%s", tabNames[k]);
        char count[8];
        snprintf(count, sizeof count, "%d", tabCount[k]);
        float tw = measure(buf).x + measure(count).x + px(30);
        ImVec2 ta = {tabX, y}, tb = {tabX + tw, y + tabH};
        bool on = k == v.tab, over = hovered(ta, tb);
        frame(ta, tb, px(4), on ? alpha(look::ACTION, 0.16f) : over ? look::HOVER : 0, on ? look::ACTION : look::LINE);
        write({ta.x + px(10), y + (tabH - FONT) / 2}, on ? look::INK : look::DIM, buf);
        write({tb.x - px(10) - measure(count).x, y + (tabH - FONT) / 2}, on ? look::ACTION : look::FAINT, count);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            v.tab = k;
        tabX += tw + px(4);
    }
    y += tabH + px(10);

    // the foot: the buttons, and a word when there is one
    float footH = FONT + px(4), footY = b.y - pad - footH;
    dl->AddRectFilled({a.x + px(1), footY - px(8)}, {b.x - px(1), footY - px(7)}, look::LINE);
    float kx = x0;
    kx += keyHint({kx, footY}, "Stick", "move");
    kx += keyHint({kx, footY}, "Light", "pick");
    kx += keyHint({kx, footY}, "Heavy", "clear");
    kx += keyHint({kx, footY}, "Special", "tab");
    kx += keyHint({kx, footY}, "Start", "scan");
    kx += keyHint({kx, footY}, "Medium", "close");
    if (*v.note && now() < v.noteUntil)
        write({x1 - measure(v.note).x, footY + px(2)}, look::ACCENT, v.note);

    float sideW = floorf(fminf(fmaxf((x1 - x0) * 0.27f, px(200)), px(380)));
    float bodyBottom = footY - px(16);
    drawGrid({x0 + px(4), y + px(4)}, {x1 - sideW - px(16), bodyBottom});
    dl->AddRectFilled({x1 - sideW - px(8), y}, {x1 - sideW - px(7), bodyBottom}, look::LINE);
    drawSide({x1 - sideW, y + px(4)}, {x1, bodyBottom});
}

// On the scan screen, the binder closed: a word at each player's side.
static void drawHint(int W, int H)
{
    const char *t = "Stick down: card binder";
    ImVec2 s = measure(t);
    float y = H - s.y - px(6);
    for (int p = 0; p < MCOC_PLAYERS; p++)
    {
        float x = p == 0 ? px(8) : W - s.x - px(8);
        dl->AddRectFilled({x - px(4), y - px(2)}, {x + s.x + px(4), y + s.y + px(2)}, alpha(look::FRAME, 0.7f));
        write({x, y}, look::DIM, t);
    }
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
    const char *s = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int major = 1, minor = 10;
    if (s)
        sscanf(s, "%d.%d", &major, &minor);
    int n = major * 100 + minor;
    return n >= 150 ? "#version 150" : n >= 130 ? "#version 130" : "#version 120";
}

static bool setUp(void)
{
    for (int i = 0; i < CARD_COUNT; i++)
    {
        int t = cards[i].series ? 0 : 1;
        tabCards[t][tabCount[t]++] = i;
    }
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
    font = io.Fonts->AddFontFromMemoryTTF((void *)mcocCozetteTtf, (int)(mcocCozetteTtfEnd - mcocCozetteTtf),
                                          COZETTE_SIZE, &cfg);
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

extern "C" void mcocBinderDraw(void *display, unsigned long drawable, int W, int H)
{
    bool open = owner >= 0;
    if ((!open && !onScanScreen) || failed || W <= 0 || H <= 0)
        return;
    if (!ready)
    {
        if (!setUp())
        {
            failed = true;
            closeBinder();
            return;
        }
        ready = true;
    }
    if (!artChecked)
        findArt();
    takeArt();

    ImGuiIO &io = ImGui::GetIO();
    double t = now();
    io.DeltaTime = (float)(t - lastTime > 0 ? (t - lastTime < 0.25 ? t - lastTime : 0.25) : 1.0 / 60);
    lastTime = t;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    if (queryPointer && display && open)
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
    S = (float)(int)(H / 600.0f + 0.5f);
    if (S < 1)
        S = 1;
    FONT = COZETTE_SIZE * S;

    takePresses();
    open = owner >= 0;

    GLint oldFb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(font, FONT);
    dl = ImGui::GetBackgroundDrawList();
    if (open)
        drawBinder(W, H);
    else if (onScanScreen)
        drawHint(W, H);
    ImGui::PopFont();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)oldFb);
}
