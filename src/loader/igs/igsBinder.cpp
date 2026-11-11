// Hero of Robots card binder.
//
// The cabinet reads the QR code of each card held to its scanner (the
// robot, its power card and its skill card, on the insert card screen);
// here the player picks them from a binder drawn over the game instead.
// Tab (or LB) opens it; the cards picked make the hand (up to three), and
// Return (or Start) hands them to the scanner one after another
// (igsCard.c), as if each was held to it.
//
// The cards are the game's own: its table (media/localization/Card/
// QRCardRes.csv: each card's print number, name and the number its QR
// stands for) and its pictures (media/localization/ui/common/card/<print
// number>_I1.dds). The QR text of a card is made from its number the way
// the board's firmware reads it back (its command 0x0e, 0x10005214): 16
// digits of 3 bits each (the 9th digit of 17 skipped), the 48 bits
// shuffled into 6 bytes (a CRC byte first), three bytes mixed, 32 bits
// shuffled into the number. The firmware's tables are read from the board.
//
// Each card the dispenser gives out is one picked at random among the
// game's newest series, shown a while and kept in the binder's "Dispensed"
// tab (for the session).
//
// It is drawn in the look of the Raw Thrills binders (mcocBinder.cpp):
// granite with the game showing through, corners cut at 45 degrees, the
// Cozette pixel font at whole multiples of its size.
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <vector>

#include <glad/gl.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "igsBinder.h"

// The library links without the C runtime's start files (-nostdlib): its
// C++ globals' destructors are registered against this.
extern "C" {
__attribute__((visibility("hidden"))) void *__dso_handle = &__dso_handle;
}

#define binderLog(...) fprintf(stderr, "IGS binder: " __VA_ARGS__)

// The Cozette font (MIT, src/fonts), in the library.
#define FONT_STR2(x) #x
#define FONT_STR(x) FONT_STR2(x)
__asm__(".section .rodata\n"
        ".balign 16\n"
        "igsCozetteTtf:\n"
        ".incbin \"" FONT_STR(OVERLAY_FONT_PATH) "\"\n"
        "igsCozetteTtfEnd:\n"
        ".previous\n");
extern "C" const unsigned char igsCozetteTtf[], igsCozetteTtfEnd[];
static const float COZETTE_SIZE = 13.0f;

#define CARD_TABLE "../media/localization/Card/QRCardRes.csv"
#define CARD_PICTURES "../media/localization/ui/common/card/"

// ---------------------------------------------------------------------------
// The cards.
struct Card
{
    char print[16], name[48];
    unsigned number; // what its QR stands for
    int kind;        // 0 robot, 1 power, 2 skill
    int series;      // 0: an event's or another card's
};
static std::vector<Card> cards;
enum { TAB_ROBOT, TAB_POWER, TAB_SKILL, TAB_EVENT, TAB_DISPENSED, TABS };
static const char *const tabNames[TABS] = {"Robot", "Power", "Skill", "Event", "Dispensed"};
static std::vector<int> tabCards[TABS];
static pthread_mutex_t shared = PTHREAD_MUTEX_INITIALIZER; // the dispensed tab, the hand
enum { HAND_MAX = 3 };
static int hand[HAND_MAX], handCount;
static std::atomic<int> isOpen{0};
static int newest; // the newest series

// One CSV line's fields (the table's: plain, no quotes).
static int fields(char *line, char **out, int max)
{
    int n = 0;
    char *p = line;
    while (n < max)
    {
        out[n++] = p;
        char *comma = strchr(p, ',');
        if (!comma)
            break;
        *comma = '\0';
        p = comma + 1;
    }
    for (int i = 0; i < n; i++)
        out[i][strcspn(out[i], "\r\n")] = '\0';
    return n;
}

extern "C" void igsBinderInstall(void)
{
    FILE *f = fopen(CARD_TABLE, "r");
    if (!f)
    {
        binderLog("no %s, no binder\n", CARD_TABLE);
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        char *fl[8];
        if (fields(line, fl, 8) < 6 || line[0] == '/')
            continue;
        const char *kind = fl[0], *number = fl[2], *print = fl[4], *name = fl[5];
        if (strlen(number) != 9 || strspn(number, "0123456789") != 9 || !*print)
            continue; // no QR: a boss, a special
        Card c = {};
        snprintf(c.print, sizeof c.print, "%s", print);
        snprintf(c.name, sizeof c.name, "%s", name);
        c.number = (unsigned)strtoul(number, NULL, 10);
        c.kind = !strcmp(kind, "POWER") ? 1 : !strcmp(kind, "SKILL") ? 2 : 0;
        if ((print[0] == 'M' || print[0] == 'P' || print[0] == 'S') && print[1] >= '0' && print[1] <= '9')
            c.series = atoi(print + 1);
        if (c.series > newest)
            newest = c.series;
        cards.push_back(c);
    }
    fclose(f);
    for (int i = 0; i < (int)cards.size(); i++)
        tabCards[cards[i].series ? cards[i].kind : TAB_EVENT].push_back(i);
    binderLog("%d cards (series 1 to %d)\n", (int)cards.size(), newest);
}

// ---------------------------------------------------------------------------
// A card's QR text.
static bool qrTables(unsigned char chars[8], unsigned char perm48[48], unsigned char perm32[32], uint32_t crc[256])
{
    // where the firmware keeps them (its literals): the digits, the 48 and 32 bit shuffles, the CRC
    uint32_t c = igsA36Rom32(0x1000520c), p48 = igsA36Rom32(0x10005210), p32 = igsA36Rom32(0x100052f8),
             t = igsA36Rom32(0x1000507c);
    if (!c || !p48 || !p32 || !t)
        return false;
    for (int i = 0; i < 8; i++)
        chars[i] = (unsigned char)igsA36Rom32(c + 4 * i);
    for (int i = 0; i < 48; i++)
        perm48[i] = (unsigned char)igsA36Rom32(p48 + 4 * i);
    for (int i = 0; i < 32; i++)
        perm32[i] = (unsigned char)(igsA36Rom32(p32 + (i & ~3)) >> (8 * (i & 3)));
    for (int i = 0; i < 256; i++)
        crc[i] = igsA36Rom32(t + 4 * i);
    return chars[0] >= '0' && chars[0] <= '9';
}

static bool qrText(unsigned number, char out[18])
{
    static unsigned char chars[8], perm48[48], perm32[32];
    static uint32_t crcTable[256];
    static bool loaded;
    if (!loaded && !(loaded = qrTables(chars, perm48, perm32, crcTable)))
        return false;
    uint32_t r1 = 0;
    for (int i = 0; i < 32; i++)
        if (number >> perm32[i] & 1)
            r1 |= 1u << i;
    unsigned char m[4] = {(unsigned char)r1, (unsigned char)(r1 >> 8), (unsigned char)(r1 >> 16),
                          (unsigned char)(r1 >> 24)};
    unsigned char b5 = (unsigned char)((number * 2654435761u) >> 13);
    unsigned char b[6] = {0, (unsigned char)(m[0] ^ b5), m[1], (unsigned char)(m[2] ^ m[1]),
                          (unsigned char)(m[3] ^ b5), b5};
    uint32_t crc = 0xffffffff;
    for (int i = 1; i < 6; i++)
        crc = crcTable[(crc ^ b[i]) & 0xff] ^ (crc >> 8);
    b[0] = (unsigned char)~crc;
    uint64_t packed = 0, bits = 0;
    for (int i = 0; i < 6; i++)
        packed |= (uint64_t)b[i] << (8 * i);
    for (int i = 0; i < 48; i++)
        if (packed >> perm48[i] & 1)
            bits |= 1ull << i;
    int n = 0;
    for (int k = 0; k < 16; k++)
    {
        if (k == 8)
            out[n++] = (char)('0' + b5 % 9); // (not read)
        out[n++] = (char)chars[(bits >> (3 * k)) & 7];
    }
    out[n] = '\0';
    return true;
}

// The hand to the scanner, a card at a time: each when the game has read
// the one before and had a while to show it.
static std::atomic<int> scanning{0};
static void *scanThread(void *arg)
{
    std::vector<unsigned> *numbers = (std::vector<unsigned> *)arg;
    for (size_t i = 0; i < numbers->size(); i++)
    {
        for (int wait = 0; igsCardPending() && wait < 600; wait++)
            usleep(50000);
        char text[18];
        if (qrText((*numbers)[i], text))
            igsCardScan(text);
        sleep(3);
    }
    delete numbers;
    scanning = 0;
    return NULL;
}

// ---------------------------------------------------------------------------
// The pictures (DXT compressed DDS files, uploaded as they are), on the GL
// thread, a few each frame.
enum { ART_NONE, ART_READY, ART_MISSING };
struct Art
{
    int state;
    GLuint tex;
};
static std::vector<Art> arts;
static int loadsThisFrame;

static GLuint loadDds(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned char head[128];
    GLuint tex = 0;
    if (fread(head, 1, 128, f) == 128 && !memcmp(head, "DDS ", 4))
    {
        uint32_t h, w, four;
        memcpy(&h, head + 12, 4);
        memcpy(&w, head + 16, 4);
        memcpy(&four, head + 84, 4);
        GLenum format = four == 0x35545844 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
                        : four == 0x33545844 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                        : four == 0x31545844 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                                             : 0;
        size_t size = (size_t)((w + 3) / 4) * ((h + 3) / 4) * (four == 0x31545844 ? 8 : 16);
        std::vector<unsigned char> data(size);
        if (format && w && h && w <= 4096 && h <= 4096 && fread(data.data(), 1, size, f) == size)
        {
            GLint old = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &old);
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, format, (GLsizei)w, (GLsizei)h, 0, (GLsizei)size, data.data());
            glBindTexture(GL_TEXTURE_2D, (GLuint)old);
        }
    }
    fclose(f);
    return tex;
}

static const Art &art(int card)
{
    if ((int)arts.size() < (int)cards.size())
        arts.resize(cards.size());
    Art &a = arts[card];
    if (a.state == ART_NONE && loadsThisFrame < 6)
    {
        loadsThisFrame++;
        char path[256];
        snprintf(path, sizeof path, CARD_PICTURES "%s_I1.dds", cards[card].print);
        a.tex = loadDds(path);
        a.state = a.tex ? ART_READY : ART_MISSING;
    }
    return a;
}

// ---------------------------------------------------------------------------
// The look.
namespace look
{
static const ImU32 FRAME = IM_COL32(0x2A, 0x2A, 0x28, 0xE6);
static const ImU32 INK = IM_COL32(0xEE, 0xEA, 0xE2, 0xFF);
static const ImU32 DIM = IM_COL32(0xB0, 0xAB, 0xA0, 0xFF);
static const ImU32 FAINT = IM_COL32(0x83, 0x7F, 0x75, 0xFF);
static const ImU32 LINE = IM_COL32(0xEE, 0xEA, 0xE2, 0x29);
static const ImU32 OUTLINE = IM_COL32(0xEE, 0xEA, 0xE2, 0x38);
static const ImU32 ACCENT = IM_COL32(0xE0, 0x66, 0x4A, 0xFF);
static const ImU32 ACTION = IM_COL32(0x6A, 0xA9, 0xB0, 0xFF);
static const ImU32 ON_ACTION = IM_COL32(0x2A, 0x2A, 0x28, 0xFF);
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

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// The card's picture, or a tile with its name.
static void cardPicture(int card, ImVec2 a, ImVec2 b)
{
    const Art &pic = art(card);
    if (pic.state == ART_READY)
    {
        dl->AddImage((ImTextureID)(uintptr_t)pic.tex, a, b);
        return;
    }
    frame(a, b, px(4), look::TILE, look::LINE);
    float wrap = b.x - a.x - px(10);
    ImVec2 ns = measure(cards[card].name, 0, wrap);
    dl->PushClipRect(a, b, true);
    write({a.x + px(5) + (wrap - ns.x) / 2, (a.y + b.y - ns.y) / 2}, look::DIM, cards[card].name, 0, wrap);
    dl->PopClipRect();
}

static const float CARD_ASPECT = 360.0f / 256.0f;

// ---------------------------------------------------------------------------
// The binder on screen (the GL thread's).
struct View
{
    int tab = 0, cols = 4;
    int focus[TABS] = {};
    float scroll[TABS] = {}, scrollShown[TABS] = {};
    char note[96] = "";
    double noteUntil = 0;
};
static View v;

// What the sampler pressed, for the GL thread.
static pthread_mutex_t pressLock = PTHREAD_MUTEX_INITIALIZER;
static unsigned pressed; // the buttons gone down since the last frame
static unsigned heldNow;
static double heldSince[16], repeatedAt[16];

// The card the dispenser gave last, and when.
static std::atomic<int> dispensedCard{-1};
static std::atomic<double> dispensedAt{0};

static void say(const char *what)
{
    snprintf(v.note, sizeof v.note, "%s", what);
    v.noteUntil = now() + 2.5;
}

static int handIndex(int card)
{
    for (int i = 0; i < handCount; i++)
        if (hand[i] == card)
            return i;
    return -1;
}

static void toggleCard(int card)
{
    pthread_mutex_lock(&shared);
    int at = handIndex(card);
    if (at >= 0)
    {
        memmove(hand + at, hand + at + 1, sizeof hand[0] * (size_t)(handCount - at - 1));
        handCount--;
    }
    else if (handCount >= HAND_MAX)
        say("The hand holds three cards");
    else
    {
        // one card of each kind: a robot, a power card, a skill card
        for (int i = 0; i < handCount; i++)
            if (cards[hand[i]].kind == cards[card].kind)
            {
                hand[i] = card;
                card = -1;
                break;
            }
        if (card >= 0)
            hand[handCount++] = card;
    }
    pthread_mutex_unlock(&shared);
}

static void scanHand(void)
{
    if (scanning)
    {
        say("Cards are still being scanned");
        return;
    }
    pthread_mutex_lock(&shared);
    std::vector<unsigned> *numbers = new std::vector<unsigned>;
    // the robot first: the game calls it, then takes the others
    for (int kind = 0; kind < 3; kind++)
        for (int i = 0; i < handCount; i++)
            if (cards[hand[i]].kind == kind)
                numbers->push_back(cards[hand[i]].number);
    pthread_mutex_unlock(&shared);
    if (numbers->empty())
    {
        delete numbers;
        say("Pick a card first");
        return;
    }
    scanning = 1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, scanThread, numbers) != 0)
    {
        scanning = 0;
        delete numbers;
        return;
    }
    pthread_detach(thread);
    isOpen = 0;
}

static void moveFocus(int dx, int dy)
{
    int n = (int)tabCards[v.tab].size(), &f = v.focus[v.tab];
    if (!n)
        return;
    if (dx)
        f = f + dx < 0 ? 0 : f + dx >= n ? n - 1 : f + dx;
    if (dy)
    {
        int to = f + dy * v.cols;
        if (to >= 0 && to < n)
            f = to;
        else if (dy > 0 && f / v.cols < (n - 1) / v.cols)
            f = n - 1;
    }
}

static void act(unsigned b)
{
    switch (b)
    {
    case IGS_BINDER_UP: moveFocus(0, -1); break;
    case IGS_BINDER_DOWN: moveFocus(0, 1); break;
    case IGS_BINDER_LEFT: moveFocus(-1, 0); break;
    case IGS_BINDER_RIGHT: moveFocus(1, 0); break;
    case IGS_BINDER_PICK:
        if (!tabCards[v.tab].empty())
            toggleCard(tabCards[v.tab][v.focus[v.tab]]);
        break;
    case IGS_BINDER_BACK: isOpen = 0; break;
    case IGS_BINDER_TAB: v.tab = (v.tab + 1) % TABS; break;
    case IGS_BINDER_SCAN: scanHand(); break;
    case IGS_BINDER_CLEAR:
        pthread_mutex_lock(&shared);
        handCount = 0;
        pthread_mutex_unlock(&shared);
        break;
    }
}

static void takePresses(void)
{
    pthread_mutex_lock(&pressLock);
    unsigned got = pressed, held = heldNow;
    pressed = 0;
    pthread_mutex_unlock(&pressLock);
    double t = now();
    for (int i = 0; i < 9 && isOpen; i++)
        if (got & (1u << i))
        {
            heldSince[i] = repeatedAt[i] = t;
            act(1u << i);
        }
    // a direction held moves again and again
    for (int i = 0; i < 4 && isOpen; i++)
        if ((held & (1u << i)) && !(got & (1u << i)) && t - heldSince[i] > 0.35 && t - repeatedAt[i] > 0.085)
        {
            repeatedAt[i] = t;
            act(1u << i);
        }
}

extern "C" int igsBinderButtons(unsigned held)
{
    static unsigned before;
    unsigned down = held & ~before;
    before = held;
    if (cards.empty())
        return 0;
    if (down & IGS_BINDER_TOGGLE)
    {
        isOpen = !isOpen;
        return 1;
    }
    if (!isOpen)
        return 0;
    pthread_mutex_lock(&pressLock);
    pressed |= down;
    heldNow = held;
    pthread_mutex_unlock(&pressLock);
    return 1;
}

extern "C" void igsBinderDispensed(void)
{
    // a card of the newest series, at random
    std::vector<int> pool;
    for (int i = 0; i < (int)cards.size(); i++)
        if (cards[i].series == newest)
            pool.push_back(i);
    if (pool.empty())
        return;
    int card = pool[(size_t)rand() % pool.size()];
    pthread_mutex_lock(&shared);
    tabCards[TAB_DISPENSED].insert(tabCards[TAB_DISPENSED].begin(), card);
    pthread_mutex_unlock(&shared);
    dispensedCard = card;
    dispensedAt = now();
    binderLog("the dispenser gave %s %s\n", cards[card].print, cards[card].name);
}

static void drawGrid(ImVec2 a, ImVec2 b)
{
    pthread_mutex_lock(&shared);
    std::vector<int> list = tabCards[v.tab];
    pthread_mutex_unlock(&shared);
    int n = (int)list.size();
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
    float top = (f / v.cols) * rowH, bottom = top + rowH - gap;
    if (top < sc)
        sc = top;
    if (bottom > sc + viewH)
        sc = bottom - viewH;
    sc = sc < 0 ? 0 : sc > maxScroll ? maxScroll : sc;
    shown += (sc - shown) * fminf(1.0f, ImGui::GetIO().DeltaTime * 16);
    if (fabsf(sc - shown) < 0.5f)
        shown = sc;

    if (!n)
        write(a, look::FAINT, v.tab == TAB_DISPENSED ? "The cards the dispenser gives show here" : "No cards");
    dl->PushClipRect({a.x - px(4), a.y - px(4)}, {b.x + px(4), b.y + px(4)}, true);
    char buf[96];
    for (int i = 0; i < n; i++)
    {
        int r = i / v.cols, c = i % v.cols, card = list[i];
        ImVec2 ta = {a.x + c * (tw + gap), a.y + r * rowH - floorf(shown)};
        ImVec2 tb = {ta.x + tw, ta.y + th};
        if (tb.y + FONT + px(6) < a.y - px(4) || ta.y > b.y + px(4))
            continue;
        cardPicture(card, ta, tb);
        int inHand = handIndex(card);
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
        const char *name = fitted(cards[card].print, tw, buf, sizeof buf);
        ImVec2 ns = measure(name);
        write({ta.x + (tw - ns.x) / 2, tb.y + px(3)}, i == f ? look::INK : inHand >= 0 ? look::ACTION : look::DIM,
              name);
    }
    dl->PopClipRect();
    if (maxScroll > 0)
    {
        float x = b.x - px(3), thumbH = fmaxf(px(24), viewH * viewH / total);
        float thumbY = a.y + (viewH - thumbH) * (shown / maxScroll);
        dl->AddRectFilled({x - px(1), a.y}, {x + px(1), a.y + viewH}, look::LINE);
        frame({x - px(3), thumbY}, {x + px(3), thumbY + thumbH}, px(2), look::FAINT, 0);
    }
}

static const char *const kindNames[3] = {"Robot", "Power", "Skill"};

static void drawSide(ImVec2 a, ImVec2 b)
{
    char buf[128], fit[96];
    pthread_mutex_lock(&shared);
    int n = (int)tabCards[v.tab].size();
    int card = n ? tabCards[v.tab][v.focus[v.tab] < n ? v.focus[v.tab] : 0] : -1;
    int count = handCount, mine[HAND_MAX];
    memcpy(mine, hand, sizeof mine);
    pthread_mutex_unlock(&shared);
    float y = a.y, w = b.x - a.x;
    if (card >= 0)
    {
        const Card &c = cards[card];
        float ph = fminf((b.y - a.y) * 0.55f, w * CARD_ASPECT), pw = floorf(ph / CARD_ASPECT);
        ImVec2 pa = {a.x + floorf((w - pw) / 2), y};
        cardPicture(card, pa, {pa.x + pw, pa.y + ph});
        y += ph + px(8);
        write({a.x, y}, look::INK, fitted(c.name, w, fit, sizeof fit));
        y += FONT + px(3);
        if (c.series)
            snprintf(buf, sizeof buf, "%s  Series %d  %s", kindNames[c.kind], c.series, c.print);
        else
            snprintf(buf, sizeof buf, "%s  %s", kindNames[c.kind], c.print);
        write({a.x, y}, look::FAINT, fitted(buf, w, fit, sizeof fit));
        y += FONT + px(10);
    }
    dl->AddRectFilled({a.x, y}, {b.x, y + px(1)}, look::LINE);
    y += px(7);
    spaced({a.x, y}, look::TITLE, "HAND");
    snprintf(buf, sizeof buf, "%d/%d", count, HAND_MAX);
    write({b.x - measure(buf).x, y}, look::FAINT, buf);
    y += FONT + px(6);
    for (int i = 0; i < count; i++)
    {
        const Card &c = cards[mine[i]];
        snprintf(buf, sizeof buf, "%s %s", c.print, c.name);
        write({a.x + px(3), y + px(2)}, look::ACTION, kindNames[c.kind]);
        write({a.x + px(52), y + px(2)}, look::INK, fitted(buf, w - px(56), fit, sizeof fit));
        y += FONT + px(5);
    }
    if (!count)
        write({a.x, y + px(2)}, look::FAINT, "A robot, a power card and a skill card", 0, w);
}

static void drawBinder(int W, int H)
{
    char buf[128];
    dl->AddRectFilled({0, 0}, {(float)W, (float)H}, look::SHADE);
    float m = fmaxf(px(16), floorf(fminf(W, H) * 0.04f));
    ImVec2 a = {m, m}, b = {W - m, H - m};
    frame(a, b, px(6), look::FRAME, look::OUTLINE);
    float pad = px(14), x0 = a.x + pad, x1 = b.x - pad, y = a.y + pad;

    spaced({x0, y + px(2)}, look::TITLE, "CARD BINDER");
    float segH = FONT + px(8);
    bool ready = igsCardScanning();
    snprintf(buf, sizeof buf, "%s", scanning ? "Scanning the hand" : ready ? "The game takes cards now"
                                                                           : "Cards wait for the insert card screen");
    float stW = measure(buf).x, dot = px(6), stX = x1 - stW;
    dl->AddRectFilled({stX - dot - px(6), y + (segH - dot) / 2}, {stX - px(6), y + (segH + dot) / 2},
                      ready ? look::ACTION : look::ACCENT);
    write({stX, y + (segH - FONT) / 2}, ready ? look::DIM : look::FAINT, buf);
    y += segH + px(10);

    float tabH = FONT + px(10), tabX = x0;
    for (int k = 0; k < TABS; k++)
    {
        char count[12];
        pthread_mutex_lock(&shared);
        snprintf(count, sizeof count, "%d", (int)tabCards[k].size());
        pthread_mutex_unlock(&shared);
        float tw = measure(tabNames[k]).x + measure(count).x + px(30);
        ImVec2 ta = {tabX, y}, tb = {tabX + tw, y + tabH};
        bool on = k == v.tab;
        frame(ta, tb, px(4), on ? alpha(look::ACTION, 0.16f) : 0, on ? look::ACTION : look::LINE);
        write({ta.x + px(10), y + (tabH - FONT) / 2}, on ? look::INK : look::DIM, tabNames[k]);
        write({tb.x - px(10) - measure(count).x, y + (tabH - FONT) / 2}, on ? look::ACTION : look::FAINT, count);
        tabX += tw + px(4);
    }
    y += tabH + px(10);

    float footH = FONT + px(4), footY = b.y - pad - footH;
    dl->AddRectFilled({a.x + px(1), footY - px(8)}, {b.x - px(1), footY - px(7)}, look::LINE);
    float kx = x0;
    kx += keyHint({kx, footY}, "Arrows", "move");
    kx += keyHint({kx, footY}, "Red/Space", "pick");
    kx += keyHint({kx, footY}, "Lever/X", "tab");
    kx += keyHint({kx, footY}, "Return/Start", "scan");
    kx += keyHint({kx, footY}, "Backspace", "clear");
    kx += keyHint({kx, footY}, "Blue/Z", "close");
    if (*v.note && now() < v.noteUntil)
        write({x1 - measure(v.note).x, footY + px(2)}, look::ACCENT, v.note);

    float sideW = floorf(fminf(fmaxf((x1 - x0) * 0.27f, px(200)), px(380)));
    float bodyBottom = footY - px(16);
    drawGrid({x0 + px(4), y + px(4)}, {x1 - sideW - px(16), bodyBottom});
    dl->AddRectFilled({x1 - sideW - px(8), y}, {x1 - sideW - px(7), bodyBottom}, look::LINE);
    drawSide({x1 - sideW, y + px(4)}, {x1, bodyBottom});
}

// The binder closed: a word while the game scans, the card just dispensed.
static void drawClosed(int W, int H)
{
    if (igsCardScanning() && !scanning)
    {
        const char *t = "Tab / LB: card binder";
        ImVec2 s = measure(t);
        float x = px(8), y = H - s.y - px(6);
        dl->AddRectFilled({x - px(4), y - px(2)}, {x + s.x + px(4), y + s.y + px(2)}, alpha(look::FRAME, 0.7f));
        write({x, y}, look::DIM, t);
    }
    int card = dispensedCard;
    if (card >= 0 && now() - dispensedAt < 8)
    {
        float ph = floorf(H * 0.32f), pw = floorf(ph / CARD_ASPECT), pad = px(8);
        char buf[96];
        snprintf(buf, sizeof buf, "%s %s", cards[card].print, cards[card].name);
        float tw = fmaxf(pw, measure(buf).x);
        ImVec2 a = {W - tw - 3 * pad, px(12)}, b = {W - pad, a.y + ph + FONT + 3 * pad + px(4)};
        frame(a, b, px(5), look::FRAME, look::ACTION);
        ImVec2 pa = {a.x + pad + floorf((tw - pw) / 2), a.y + pad};
        cardPicture(card, pa, {pa.x + pw, pa.y + ph});
        write({a.x + pad + floorf((tw - measure(buf).x) / 2), pa.y + ph + px(6)}, look::INK, buf);
    }
}

// ---------------------------------------------------------------------------
// Each picture, on the GL thread.
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
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.PixelSnapH = true;
    cfg.OversampleH = cfg.OversampleV = 1;
    font = io.Fonts->AddFontFromMemoryTTF((void *)igsCozetteTtf, (int)(igsCozetteTtfEnd - igsCozetteTtf),
                                          COZETTE_SIZE, &cfg);
    if (!font)
        font = io.Fonts->AddFontDefault();
    const char *glsl = glslVersion();
    if (!ImGui_ImplOpenGL3_Init(glsl))
    {
        binderLog("the GL renderer would not start (%s), no binder\n", glsl);
        return false;
    }
    binderLog("ready (GL %s, %s)\n", (const char *)glGetString(GL_VERSION), glsl);
    lastTime = now();
    return true;
}

extern "C" void igsBinderDraw(void)
{
    bool open = isOpen;
    if (cards.empty() || failed)
        return;
    bool hint = igsCardScanning() || (dispensedCard >= 0 && now() - dispensedAt < 8);
    if (!open && !hint)
        return;
    if (!glad_glGetIntegerv)
        return;
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int W = viewport[2], H = viewport[3];
    if (W <= 0 || H <= 0)
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
    ImGuiIO &io = ImGui::GetIO();
    double t = now();
    io.DeltaTime = (float)(t - lastTime > 0 ? (t - lastTime < 0.25 ? t - lastTime : 0.25) : 1.0 / 60);
    lastTime = t;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    S = (float)(int)(H / 600.0f + 0.5f);
    if (S < 1)
        S = 1;
    FONT = COZETTE_SIZE * S;
    loadsThisFrame = 0;
    takePresses();
    open = isOpen;

    // the game's state the GL3 renderer leaves alone: its assembly programs
    GLboolean vp = glIsEnabled(GL_VERTEX_PROGRAM_ARB), fp = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    GLint oldFb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(font, FONT);
    dl = ImGui::GetBackgroundDrawList();
    if (open)
        drawBinder(W, H);
    else
        drawClosed(W, H);
    ImGui::PopFont();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)oldFb);
    if (vp)
        glEnable(GL_VERTEX_PROGRAM_ARB);
    if (fp)
        glEnable(GL_FRAGMENT_PROGRAM_ARB);
    glGetError(); // (the ARB enables where the context has none)
}
