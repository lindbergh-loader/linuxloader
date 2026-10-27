// Centipede Chaos's own hooks (see g7.h): its cabinet switches, from the
// desktop's keyboard and mouse.
//
// The spinner needs nothing: the game's io_sdl device (type 0x800 in the
// io device table at 0x15cd020, which the io layer starts, 0x400, the raw
// USB spinner, being left out) gives the mouse's motion to the spinner's
// analog slot itself, through its statically linked SDL. A USB spinner is a
// mouse too.
//
// The switches go through the engine's switch records as Halo's do (see
// g7Halo.c): once a frame the input update (0x4fac90) rebuilds them from the
// raw input records (0x5e8cf0 hands them out by id) and the alias table
// (io_sw_alias, 0x46b9b0: count at 0x15fca30, {raw, logical} pairs from
// 0x15f2120 + 0x1122 * 8). The loader shadows the raw records of the
// cabinet's panel and players' buttons and fills them just before. The raw
// ids, from the alias registrations (the logical ones in the switch test's
// table at 0x15cc8c0):
//   0x239 diag (0x256 CabDiag, 0x2a1 select): held a second, it opens the
//   test menu (its callback, 0x41f850, counts 1000 ms down), a tap does
//   nothing   0x23a service (0x288, 0x2a2)
//   0x23b/0x23c volume up/down
//   0x23d/0x23e player 1's coins 0, 3 (Cab0Coin0/3); 0x246/0x247 player 2's
//   (Cab1, 0x25f/0x262), 0x24f/0x250 player 3's (Cab2, 0x267/0x26a)
//   0x23f..0x243 player 1's buttons 0..4 (station 0: 0x208..0x20c): its
//   joystick's left, right, up, down, and its action button, also its start
//   (0x24e) and select; 0x248..0x24c player 2's (0x214.., start 0x24f),
//   0x251..0x255 player 3's (0x220..)
//   0x2b6..0x2c1 the operator's keypad: 0..9, *, # (no alias: read as is)
// The game's own SDL keyboard (raw ids = scancodes) already gives player 1's
// buttons 0..3 to the arrows.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "g7.h"

// ---------------------------------------------------------------------------
// The X display: the keyboard, with a connection of our own.

static struct
{
    void *(*openDisplay)(const char *);
    int (*queryKeymap)(void *, char[32]);
    unsigned char (*keysymToKeycode)(void *, unsigned long);
} x;

typedef struct
{
    void *display;
    int tried;
} XConnection;

static XConnection switchX;

// The libX11 entry points, loaded once (either thread may come first).
static void loadX(void)
{
    void *x11 = dlopen("libX11.so.6", RTLD_NOW);
    if (!x11)
        return;
    *(void **)&x.openDisplay = dlsym(x11, "XOpenDisplay");
    *(void **)&x.queryKeymap = dlsym(x11, "XQueryKeymap");
    *(void **)&x.keysymToKeycode = dlsym(x11, "XKeysymToKeycode");
}

static int openX(XConnection *c)
{
    static pthread_once_t loaded = PTHREAD_ONCE_INIT;

    if (c->display || c->tried++)
        return c->display != NULL;
    pthread_once(&loaded, loadX);
    if (!x.openDisplay || !x.queryKeymap || !x.keysymToKeycode ||
        !(c->display = x.openDisplay(NULL)))
    {
        g7Log("centipede: no X display, no keyboard input\n");
        c->display = NULL;
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The switches.

#define INPUT_UPDATE 0x4fac90   // "push %r12; push %rbp; push %rbx; mov %edi,%ebx"
#define RAW_RECORD 0x5e8cf0     // "test %edi,%edi; js <return NULL>"

// The keys, to raw ids.
static const struct { unsigned long keysym; int raw; } inputs[] = {
    {0xffbf, 0x239},                       // F2: diag
    {0xffbe, 0x23a}, {0x70, 0x23a},     // F1, p: service
    {0xff55, 0x23b}, {0xff56, 0x23c},   // Page Up/Down: volume
    // 5, 6, 7: each player's coin slot (cabinet 0, 1, 2's coin 0: the
    // players' credits are their own).
    {0x35, 0x23d}, {0x36, 0x246}, {0x37, 0x24f},
    {0x31, 0x243}, {0x32, 0x24c}, {0x33, 0x255},   // 1, 2, 3: start
    // The players' action buttons (button 4, their start too) and joysticks
    // (buttons 0..3: left, right, up, down), as MAME lays them out: player 1
    // the arrows (the game's own SDL keyboard has them too), space or left
    // Ctrl; player 2 R, F, D, G and A; player 3 I, K, J, L and right Ctrl.
    {0x20, 0x243}, {0xffe3, 0x243},
    {0xff51, 0x23f}, {0xff53, 0x240}, {0xff52, 0x241}, {0xff54, 0x242},
    {0x61, 0x24c},
    {0x64, 0x248}, {0x67, 0x249}, {0x72, 0x24a}, {0x66, 0x24b},
    {0xffe4, 0x255},
    {0x6a, 0x251}, {0x6c, 0x252}, {0x69, 0x253}, {0x6b, 0x254},
    // The operator's keypad (switch test: SwitchTestNumpad0..9, *, #, ids
    // 0x2b6..0x2c1, served by the same accessor): the numpad, # on its Enter
    // (the keypad's confirm key) and its period, as for the other games.
    {0xffb0, 0x2b6}, {0xffb1, 0x2b7}, {0xffb2, 0x2b8}, {0xffb3, 0x2b9}, {0xffb4, 0x2ba},
    {0xffb5, 0x2bb}, {0xffb6, 0x2bc}, {0xffb7, 0x2bd}, {0xffb8, 0x2be}, {0xffb9, 0x2bf},
    {0xffaa, 0x2c0},                       // numpad *: *
    {0xff8d, 0x2c1}, {0xffae, 0x2c1},   // numpad Enter, period: #
};
#define INPUTS (sizeof(inputs) / sizeof(inputs[0]))

static struct
{
    int raw;
    unsigned char keycode;
    int held, wasHeld;
    uint32_t heldFor;
    uint32_t record[20];
} switches[INPUTS];

#define PLAYERS 3
// Each player's first button (raw id: the joystick's left, then right, up,
// down, the action button) and coin slot.
static const int playerButtons[PLAYERS] = {0x23f, 0x248, 0x251};
static const int playerCoins[PLAYERS] = {0x23d, 0x246, 0x24f};

// Holds the switch of a raw id (its first input's record).
static void holdRaw(int raw)
{
    for (size_t i = 0; i < INPUTS; i++)
        if (switches[i].raw == raw)
        {
            switches[i].held = 1;
            return;
        }
}

static uint32_t *(*rawRecordOrig)(int id);

static uint32_t *rawRecord(int id)
{
    if (id < 0)
        return NULL;
    for (size_t i = 0; i < INPUTS; i++)
        if (switches[i].raw == id)
            return switches[i].record;
    return rawRecordOrig(id);
}

static int (*inputUpdateOrig)(int);

static int inputUpdate(int arg)
{
    static struct timespec last;
    static int bound, debug = -1;
    struct timespec now;
    char keys[32];
    int dt;

    if (debug < 0)
    {
        // CENT_INPUT_DEBUG: the raw switches' presses and releases logged.
        debug = g7GameEnv("INPUT_DEBUG", 0) != 0;
        g7Log("centipede: input update hooked\n");
    }
    if (openX(&switchX) && !bound)
    {
        for (size_t i = 0; i < INPUTS; i++)
            switches[i].keycode = inputs[i].keysym ? x.keysymToKeycode(switchX.display, inputs[i].keysym) : 0;
        bound = 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    dt = last.tv_sec ? (int)((now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000) : 0;
    last = now;
    memset(keys, 0, sizeof(keys));
    if (switchX.display)
        x.queryKeymap(switchX.display, keys);
    // Inputs sharing a raw id are or-ed into the first one's record.
    for (size_t i = 0; i < INPUTS; i++)
        switches[i].held = 0;
    for (size_t i = 0; i < INPUTS; i++)
    {
        unsigned char code = switches[i].keycode;
        size_t first = 0;
        while (switches[first].raw != switches[i].raw)
            first++;
        if (code && (keys[code / 8] & (1 << (code % 8))))
            switches[first].held = 1;
    }
    // Each player's gamepad (g7Pads.c): its joystick, button, start, and a
    // coin on select.
    g7PadsPoll(PLAYERS);
    for (int p = 0; p < PLAYERS; p++)
    {
        unsigned int pad = g7PadHeld(p);
        static const unsigned int bits[] = {G7_PAD_LEFT, G7_PAD_RIGHT, G7_PAD_UP, G7_PAD_DOWN,
                                            G7_PAD_BUTTON | G7_PAD_START};
        for (int b = 0; b < 5; b++)
            if (pad & bits[b])
                holdRaw(playerButtons[p] + b);
        if (pad & G7_PAD_SELECT)
            holdRaw(playerCoins[p]);
        // Any player's pad has the operator's buttons.
        if (pad & G7_PAD_TEST)
            holdRaw(0x239);
        if (pad & G7_PAD_SERVICE)
            holdRaw(0x23a);
    }
    for (size_t i = 0; i < INPUTS; i++)
    {
        uint32_t *rec = switches[i].record;
        int held = switches[i].held;
        size_t first = 0;
        while (switches[first].raw != switches[i].raw)
            first++;
        if (first != i)
            continue;
        // A press and a release each last one frame, as the RIO's own
        // switch setter makes them.
        rec[1] = held && !switches[i].wasHeld;
        rec[2] = !held && switches[i].wasHeld;
        if (debug && (rec[1] || rec[2]))
            g7Log("centipede: raw %#x %s\n", switches[i].raw, rec[1] ? "pressed" : "released");
        switches[i].heldFor = held ? switches[i].heldFor + (uint32_t)dt + 1 : 0;
        rec[3] = switches[i].heldFor;
        switches[i].wasHeld = held;
    }
    return inputUpdateOrig(arg);
}

void g7CentipedeInstall(const G7Game *game)
{
    (void)game;
    for (size_t i = 0; i < INPUTS; i++)
        switches[i].raw = inputs[i].raw;
    *(void **)&inputUpdateOrig = g7HookNear(INPUT_UPDATE, 6, inputUpdate);
    // The accessor goes on past its negative-id test, which rawRecord makes.
    g7HookNear(RAW_RECORD, 8, rawRecord);
    rawRecordOrig = (uint32_t * (*)(int))(RAW_RECORD + 8);
}
