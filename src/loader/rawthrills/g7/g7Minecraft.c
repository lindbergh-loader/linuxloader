// Minecraft Dungeons Arcade: the cabinet around the game.
//
// - The four stations' joysticks (two axes, three buttons each): SDL's Linux
//   joystick driver in the game is replaced by four virtual ones, fed by the
//   keyboard (player 1) and the pads (each player's own).
// - The cabinet switches (START, coins, test): the I/O board's raw switch
//   records, answered by the loader.
// - The cabinet's setup answered as Ultra Loader does (webcams, factory setup),
//   the card dispenser made virtual, the game's code hash made over the
//   unpatched code.
// - The cards: F3..F6 (or a pad's Select+Start) open the card binder drawn over
//   the game (g7McdBinder.cpp); the hand picked there is scanned as if held to
//   the station's webcam.
//
// The keyboard and pads are read at the evdev level (/dev/input), not through
// X: XQueryKeymap only reflects the focused surface under Wayland.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "g7.h"
#include "g7McdBinder.h"



// ---------------------------------------------------------------------------
// The cabinet's joysticks.
//
// Each station has a joystick with two axes and three buttons, and the game
// wants all four ("P2 joystick not detected!").  Its statically linked SDL's
// Linux joystick driver is replaced, as Ultra Loader does, by one that always
// has four: its entry points jump to the functions below.  The stations are
// fed by the loader: player 1 by the keyboard, each player by the pad of that
// number (g7Pads-style evdev reading in this file); nothing goes through
// /dev/uinput.
//
// SDL 2.0's SDL_Joystick here: +0 instance id, +0x10 axes, +0x20 hats, +0x30
// balls, +0x40 buttons, +0x50 the driver's data.  Changes are reported
// through SDL's private SDL_PrivateJoystickAxis (0x657560) and
// SDL_PrivateJoystickButton (0x657810).
#define JOY_INIT 0x658b40          // -> 4
#define JOY_COUNT 0x658c10         // -> 4
#define JOY_DETECT 0x658c20        // nothing
#define JOY_NAME 0x658c40          // (index) -> name
#define JOY_INSTANCE 0x658c80      // (index) -> instance id
#define JOY_OPEN 0x658cc0          // (joystick, index) -> 0
#define JOY_ATTACHED 0x659160      // (joystick) -> 1
#define JOY_UPDATE 0x659180        // (joystick)
#define JOY_CLOSE 0x659200         // (joystick)
#define JOY_QUIT 0x659270          // nothing
#define JOY_GUID_INDEX 0x6592f0    // (index) -> a zero GUID
#define JOY_GUID 0x659370          // (joystick) -> a zero GUID
#define SDL_PRIVATE_AXIS 0x657560
#define SDL_PRIVATE_BUTTON 0x657810
#define JOY_AXES 2
#define JOY_BUTTONS 3

// What each station holds now, written by the input thread.
static volatile int stationAxis[MCD_STATIONS][JOY_AXES];
static volatile unsigned stationButtons[MCD_STATIONS];

typedef struct
{
    int fd;                 // -1 (where SDL's Linux driver keeps its fd)
    void *self;
    char pad[0x10];
    const char *name;
    int index, axis[JOY_AXES];
    unsigned buttons;
} JoyData;

static const char *const joyNames[MCD_STATIONS] = {"Arcade Joystick 1", "Arcade Joystick 2", "Arcade Joystick 3",
                                                   "Arcade Joystick 4"};

static const char *joyName(int index)
{
    return index >= 0 && index < MCD_STATIONS ? joyNames[index] : NULL;
}

static int joyInstance(int index)
{
    return index >= 0 && index < MCD_STATIONS ? index : -1;
}

static int joyOpen(uint8_t *joystick, int index)
{
    if (!joystick || index < 0 || index >= MCD_STATIONS)
        return -1;
    JoyData *d = calloc(1, sizeof *d);
    if (!d)
        return -1;
    d->fd = -1;
    d->self = d;
    d->name = joyNames[index];
    d->index = index;
    *(int *)(joystick + 0x00) = index;
    *(int *)(joystick + 0x10) = JOY_AXES;
    *(int *)(joystick + 0x20) = 0;
    *(int *)(joystick + 0x30) = 0;
    *(int *)(joystick + 0x40) = JOY_BUTTONS;
    *(JoyData **)(joystick + 0x50) = d;
    g7Log("minecraft: joystick %d opened (virtual station)\n", index + 1);
    return 0;
}

static void joyUpdate(uint8_t *joystick)
{
    int (*axis)(void *, uint8_t, int16_t) = (void *)SDL_PRIVATE_AXIS;
    int (*button)(void *, uint8_t, uint8_t) = (void *)SDL_PRIVATE_BUTTON;
    JoyData *d = joystick ? *(JoyData **)(joystick + 0x50) : NULL;
    if (!d || d->index < 0 || d->index >= MCD_STATIONS)
        return;
    for (int a = 0; a < JOY_AXES; a++)
    {
        int v = stationAxis[d->index][a];
        if (v != d->axis[a])
        {
            axis(joystick, (uint8_t)a, (int16_t)v);
            d->axis[a] = v;
        }
    }
    unsigned b = stationButtons[d->index];
    for (int i = 0; i < JOY_BUTTONS; i++)
        if ((b ^ d->buttons) & (1u << i))
            button(joystick, (uint8_t)i, (b >> i) & 1);
    d->buttons = b;
}

static void joyClose(uint8_t *joystick)
{
    if (!joystick)
        return;
    free(*(JoyData **)(joystick + 0x50));
    *(JoyData **)(joystick + 0x50) = NULL;
}

// Writes those bytes over the game's code.
static void patchBytes(uintptr_t at, const char *hex)
{
    uint8_t *p = (uint8_t *)at;
    for (; hex[0] && hex[1]; hex += 2)
    {
        unsigned v;
        sscanf(hex, "%2x", &v);
        *p++ = (uint8_t)v;
    }
}

static void installJoysticks(void)
{
    patchBytes(JOY_INIT, "b804000000c3");     // mov eax,4; ret
    patchBytes(JOY_COUNT, "b804000000c3");
    patchBytes(JOY_DETECT, "c3");             // ret
    patchBytes(JOY_QUIT, "c3");
    patchBytes(JOY_ATTACHED, "b801000000c3"); // mov eax,1; ret
    patchBytes(JOY_GUID_INDEX, "31c031d2c3"); // xor eax,eax; xor edx,edx; ret
    patchBytes(JOY_GUID, "31c031d2c3");
    g7Detour(JOY_NAME, (void *)joyName);
    g7Detour(JOY_INSTANCE, (void *)joyInstance);
    g7Detour(JOY_OPEN, (void *)joyOpen);
    g7Detour(JOY_UPDATE, (void *)joyUpdate);
    g7Detour(JOY_CLOSE, (void *)joyClose);
}

// The cabinet's setup, as Ultra Loader answers it: the webcams' auto-id done
// (0x4199c0), each player's webcam there (0x4a3790), the factory setup done
// (0x439620), and flag 0x17a8b58 (0x430cf0, read by the card dispenser's
// process).
static void installCabinet(void)
{
    patchBytes(0x4199c0, "b801000000c39090");
    patchBytes(0x4a3790, "b801000000c3");
    patchBytes(0x439620, "b801000000c39090");
    patchBytes(0x430cf0, "b801000000c39090");
    // the engine's allocator as Ultra Loader sizes it: blocks of 0x8000, not
    // 0x2000 (0x4d9640..), and larger pools (0x16de184/8)
    patchBytes(0x4d9659, "0f");
    patchBytes(0x4d9671, "00800000");
    patchBytes(0x4d9693, "00800000");
    patchBytes(0x4d96ca, "00800000");
    patchBytes(0x4d96d0, "00800000");
    *(uint32_t *)0x16de184 = 0x180000;
    *(uint32_t *)0x16de188 = 0x100000;
}

// The card dispenser.  The cabinet drops a card in the player's hand at the
// end of a game; there is no hopper here, so the cards the game owes would only
// pile up (a request per finished game, saved in GameCardAudits, restored at
// each start, "P%u is owed cards").  As Ultra Loader's virtual dispenser does
// (mcd_disp_hook.c, its C83 offsets): before each run of the dispenser's
// per-frame update (0x41b010), every idle request is taken off the four queues
// (0x178a600 + p * 0x40: head, tail, count at +0x10, state byte at +0x28) and
// counted as dispensed; the update then sees nothing owed.  The binder has
// every card, so the dispensed card is only logged.
#define DISP_UPDATE 0x41b010
#define DISP_RESUME 0x41b01d     // after the 13 bytes the trampoline re-runs
#define DISP_AUDITS 0x117c65b    // "GameCardAudits", the relocated lea's target
#define DISP_QUEUES 0x178a600
static const uint8_t dispOrig[13] = {0x41, 0x57, 0x41, 0x56, 0x48, 0x8d, 0x3d, 0x40, 0x16, 0xd6, 0x00, 0x41, 0x55};
static void *dispTramp;
static volatile int dispensed[MCD_STATIONS];

static int plausible(uint64_t p)
{
    return p >= 0x10000 && p < 0x7fffffff0000ull && !(p & 7);
}

static void drainQueues(void)
{
    for (int q = 0; q < MCD_STATIONS; q++)
    {
        uint8_t *Q = (uint8_t *)(uintptr_t)(DISP_QUEUES + q * 0x40);
        uint64_t node = *(volatile uint64_t *)Q;
        int ok = 1, n = 0;
        if (!node || Q[0x28])
            continue;
        // the whole list checked first: a bad link leaves it alone
        for (uint64_t v = node; v; v = *(volatile uint64_t *)v)
        {
            uint64_t d = *(volatile uint64_t *)(v + 0x10);
            if (!plausible(v) || ++n > 4096 || (d && (d < 0x10000 || d >= 0x7fffffff0000ull)))
            {
                ok = 0;
                break;
            }
        }
        if (!ok)
            continue;
        while (node)
        {
            uint64_t next = *(volatile uint64_t *)node, data = *(volatile uint64_t *)(node + 0x10);
            const volatile uint8_t *d = (const volatile uint8_t *)(uintptr_t)data;
            if (d && !d[7] && !d[8])
            {
                uint64_t prev = *(volatile uint64_t *)(node + 8);
                if (prev)
                    *(volatile uint64_t *)prev = next;
                else
                    *(volatile uint64_t *)Q = next;
                if (next)
                    *(volatile uint64_t *)(next + 8) = prev;
                else
                    *(volatile uint64_t *)(Q + 8) = prev;
                if (*(volatile uint32_t *)(Q + 0x10))
                    (*(volatile uint32_t *)(Q + 0x10))--;
                *(volatile uint64_t *)node = 0;
                *(volatile uint64_t *)(node + 8) = 0;
                // the node is not freed: a few bytes a card, no double free
                dispensed[d[4] < MCD_STATIONS ? d[4] : q]++;
            }
            node = next;
        }
    }
}

static uint64_t dispUpdate(void)
{
    drainQueues();
    return ((uint64_t(*)(void))dispTramp)();
}

static void installDispenser(void)
{
    uint8_t *at = (uint8_t *)DISP_UPDATE, *t;
    uint64_t v;
    int n = 0;
    if (memcmp(at, dispOrig, sizeof dispOrig))
    {
        g7Log("minecraft: card dispenser update not the C83 one, no virtual dispenser\n");
        return;
    }
    t = mmap(NULL, 64, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (t == MAP_FAILED)
        return;
    t[n++] = 0x41; t[n++] = 0x57;              // push r15
    t[n++] = 0x41; t[n++] = 0x56;              // push r14
    t[n++] = 0x48; t[n++] = 0xbf;              // movabs rdi, "GameCardAudits"
    v = DISP_AUDITS;
    memcpy(t + n, &v, 8);
    n += 8;
    t[n++] = 0x41; t[n++] = 0x55;              // push r13
    t[n++] = 0xff; t[n++] = 0x25;              // jmp *0(%rip)
    memset(t + n, 0, 4);
    n += 4;
    v = DISP_RESUME;
    memcpy(t + n, &v, 8);
    dispTramp = t;
    at[0] = 0x48; at[1] = 0xb8;                // movabs rax, dispUpdate
    v = (uint64_t)(uintptr_t)dispUpdate;
    memcpy(at + 2, &v, 8);
    at[10] = 0xff; at[11] = 0xe0;              // jmp rax
    at[12] = 0x90;
    g7Log("minecraft: virtual card dispenser installed\n");
}

// The cabinet switches: START, coins, the operator's buttons.
//
// They are not on the joysticks but on the cabinet's I/O board (RIO), read
// through the engine's switch records as in Halo and Centipede (g7Halo.c).
// 0x638500 hands out the raw input record of a hardware id (0x239..0x298 for
// the cabinet; NULL while the RIO is missing: a slot is a switch only once the
// board says so, +0xaea0 = 1 or 2; 0x638ae0 is the analog one), and the game links each
// logical switch to the raw inputs registered for it through io_sw_alias
// (0x4b0f20: count at 0x17d13f0, {raw, logical} pairs from 0x17cf3f0).  The
// loader answers every cabinet id with a record of its own and, once a frame,
// fills the ones behind the logical switches it drives: +0x04 presses this
// frame, +0x08 releases, +0x0c how long it has been held.  The logical ids are
// the switch test's (id/name pairs at 0x173cac0): START 0x24e + station,
// a cabinet's coin 0x257 + cabinet * 8, diag 0x256, cabinet 0's service 0x288;
// START is a raw input, RIO board 0's switch 4 * station (0x239 + 4 * s, as
// Ultra Loader's I/O board map has it).
#define RAW_RECORD 0x638500
#define RAW_FIRST 0x239
#define RAW_COUNT 0x60
#define ALIAS_COUNT 0x17d13f0
#define ALIAS_PAIRS 0x17cf3f0
#define SW_START(s) (0x239 + (s) * 4) // raw: RIO board 0 switch 4 * station (Ultra's 0.0/0.4/0.8/0.12)
#define SW_COIN(s) (0x257 + (s) * 8)
#define SW_DIAG 0x256
#define SW_SERVICE 0x288
static uint32_t rawRecords[RAW_COUNT][20];
static uint32_t *(*rawRecordOrig)(int);

static uint32_t *rawRecord(int id)
{
    if (id < 0)
        return NULL;
    if (id >= RAW_FIRST && id < RAW_FIRST + RAW_COUNT)
        return rawRecords[id - RAW_FIRST];
    return rawRecordOrig(id);
}

// The logical switches driven, and what holds them now (input thread).
// A station's three buttons are the switches after its START (Ultra Loader's
// map: action, reload, trigger), RIO board 0 switches 4 * station + 1..3.
#define SW_BUTTON(s, n) (0x239 + (s) * 4 + 1 + (n))
enum { SWI_START, SWI_COIN = SWI_START + MCD_STATIONS, SWI_DIAG = SWI_COIN + MCD_STATIONS, SWI_SERVICE,
       SWI_BUTTON, SWI_COUNT = SWI_BUTTON + MCD_STATIONS * 3 };
static volatile unsigned char switchHeld[SWI_COUNT];

static int switchLogical(int i)
{
    if (i >= SWI_BUTTON)
        return SW_BUTTON((i - SWI_BUTTON) / 3, (i - SWI_BUTTON) % 3);
    return i < SWI_COIN ? SW_START(i - SWI_START) : i < SWI_DIAG ? SW_COIN(i - SWI_COIN) : i == SWI_DIAG ? SW_DIAG : SW_SERVICE;
}

// Once a frame (from the swap): each driven switch's raw records filled.
static void switchFrame(void)
{
    static struct timespec last;
    static unsigned char was[SWI_COUNT];
    static uint32_t heldFor[SWI_COUNT];
    static int said;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t dt = last.tv_sec ? (uint32_t)((now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000) : 16;
    last = now;
    int count = *(volatile int *)ALIAS_COUNT;
    const int *pairs = (const int *)ALIAS_PAIRS;
    for (int i = 0; i < SWI_COUNT; i++)
    {
        int held = switchHeld[i], logical = switchLogical(i), raws = 0;
        heldFor[i] = held ? heldFor[i] + dt + 1 : 0;
        for (int k = 0; k < count && k < 0x400; k++)
        {
            int raw = pairs[k * 2];
            if (pairs[k * 2 + 1] != logical || raw < RAW_FIRST || raw >= RAW_FIRST + RAW_COUNT)
                continue;
            uint32_t *r = rawRecords[raw - RAW_FIRST];
            r[1] = held && !was[i];
            r[2] = !held && was[i];
            r[3] = heldFor[i];
            raws++;
            if (held && !was[i])
                g7Log("minecraft: switch %#x (raw %#x) pressed\n", logical, raw);
        }
        // no alias: the switch test's id is a raw input itself (START here)
        if (!raws && logical >= RAW_FIRST && logical < RAW_FIRST + RAW_COUNT)
        {
            uint32_t *r = rawRecords[logical - RAW_FIRST];
            r[1] = held && !was[i];
            r[2] = !held && was[i];
            r[3] = heldFor[i];
            raws++;
            if (held && !was[i])
                g7Log("minecraft: switch %#x pressed\n", logical);
        }
        if (!raws && held && !was[i] && said++ < 8)
            g7Log("minecraft: switch %#x has no raw input\n", logical);
        was[i] = (unsigned char)held;
    }
    // MCD_TEST_RAWS=<id,id,...>: each raw input pressed in turn, every 4 s
    static int testRaws[16], testRawCount = -1, testRaw = -1;
    static double testAt;
    if (testRawCount < 0)
    {
        const char *t = getenv("MCD_TEST_RAWS");
        testRawCount = 0;
        while (t && *t && testRawCount < 16)
        {
            testRaws[testRawCount++] = (int)strtol(t, (char **)&t, 0);
            t += strspn(t, ", ");
        }
    }
    if (testRawCount)
    {
        static double t0;
        double sec = now.tv_sec + now.tv_nsec / 1e9;
        if (!t0)
            t0 = sec;
        if (testRaw >= 0)
            rawRecords[testRaws[testRaw] - RAW_FIRST][1] = 0, rawRecords[testRaws[testRaw] - RAW_FIRST][3] = 0;
        if (sec - testAt > 4 && sec - t0 > 40)
        {
            testAt = sec;
            testRaw = (testRaw + 1) % testRawCount;
            rawRecords[testRaws[testRaw] - RAW_FIRST][1] = 1;
            rawRecords[testRaws[testRaw] - RAW_FIRST][3] = 100;
            g7Log("minecraft: test press raw %#x\n", testRaws[testRaw]);
        }
        else if (testRaw >= 0 && sec - testAt < 0.15)
            rawRecords[testRaws[testRaw] - RAW_FIRST][3] = 100;
    }
    static int dumped;
    if (!dumped && count > 0 && getenv("MCD_TRACE_SWITCHES"))
    {
        static int frames;
        if (++frames == 600)
        {
            dumped = 1;
            for (int k = 0; k < count && k < 0x400; k++)
                g7Log("minecraft: alias raw %#x -> %#x\n", pairs[k * 2], pairs[k * 2 + 1]);
        }
    }
}

static void mcdFrame(void *display, unsigned long drawable, int width, int height)
{
    switchFrame();
    mcdBinderDraw(display, drawable, width, height);
}

// MCD_TRACE_CMDS: the scene scripts' commands counted (each command table
// entry's handler, rec - 0x10, swapped for a thunk counting it), and the
// counts of the 2 s after TRANSITION_OUT_CAM5 logged by the input thread.
#define CMD_MAX 512
static const struct { uintptr_t table; int count; } cmdTables[] = {{0x171f0e0, 0xd}, {0x1720560, 0x18b}};
static const char *cmdName[CMD_MAX];
static volatile unsigned cmdCount[CMD_MAX];
static volatile int cmdWindow; // 1: counting, 2: done, to log
static struct timespec cmdWindowAt;

static uint8_t *volatile cmdLastInst[CMD_MAX];
static void cmdSeen(int g, uint8_t *inst)
{
    if (cmdWindow == 1 && g >= 0 && g < CMD_MAX)
    {
        __atomic_fetch_add(&cmdCount[g], 1, __ATOMIC_RELAXED);
        cmdLastInst[g] = inst;
    }
}

static void installCmdTrace(void)
{
    uint8_t *page = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int g = 0;
    if (page == MAP_FAILED)
        return;
    uint8_t *p = page;
    for (size_t t = 0; t < sizeof cmdTables / sizeof cmdTables[0]; t++)
        for (int i = 0; i < cmdTables[t].count && g < CMD_MAX; i++, g++)
        {
            uint8_t *rec = (uint8_t *)cmdTables[t].table + i * 0x80;
            uint64_t *slot = (uint64_t *)(rec - 0x10), orig = *slot, fn = (uint64_t)(uintptr_t)cmdSeen;
            cmdName[g] = (const char *)rec + 4;
            if (!orig)
                continue;
            uint8_t *th = p;
            static const uint8_t save[] = {0x50, 0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53};
            static const uint8_t restore[] = {0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f, 0x58};
            memcpy(p, save, sizeof save);
            p += sizeof save;
            *p++ = 0x48; *p++ = 0x89; *p++ = 0xfe; // mov rsi, rdi (the command instance)
            *p++ = 0xbf; // mov edi, g
            memcpy(p, &g, 4);
            p += 4;
            *p++ = 0x48; *p++ = 0xb8; // movabs rax, cmdSeen
            memcpy(p, &fn, 8);
            p += 8;
            *p++ = 0xff; *p++ = 0xd0; // call rax
            memcpy(p, restore, sizeof restore);
            p += sizeof restore;
            *p++ = 0xff; *p++ = 0x25; memset(p, 0, 4); p += 4; // jmp *0(%rip)
            memcpy(p, &orig, 8);
            p += 8;
            *slot = (uint64_t)(uintptr_t)th;
        }
    g7Log("minecraft: %d script commands traced\n", g);
}

// MCD_TRACE_EVENTS: the game's script events logged (0x4af4b0).
static int (*eventSendOrig)(int, const char *);
static int traceEvents;
static int eventSend(int target, const char *name)
{
    if (name && cmdWindow == 0 && !strcmp(name, "TRANSITION_OUT_CAM5") && cmdName[0])
    {
        clock_gettime(CLOCK_MONOTONIC, &cmdWindowAt);
        cmdWindow = 1;
    }
    if (traceEvents && name && !strstr(name, "MONEYIN_HAVE") && !strstr(name, "LAMP"))
        g7Log("minecraft: event %s\n", name);
    return eventSendOrig(target, name);
}

// MCD_TRACE_CHECKS: the flags the game's scripts poll (0x4af990) logged, each
// the first time and every 1000th.
static int (*flagCheckOrig)(int, const char *);
static int traceChecks;
static int flagCheck(int target, const char *name)
{
    static struct { char name[48]; unsigned n; } seen[256];
    static int nseen;
    int r = flagCheckOrig(target, name);
    if (!name || !traceChecks)
        return r;
    int i;
    for (i = 0; i < nseen && strcmp(seen[i].name, name); i++)
        ;
    if (i == nseen && nseen < 256)
        snprintf(seen[nseen++].name, sizeof seen[0].name, "%s", name);
    if (i < 256 && seen[i].n++ % 1000 == 0)
        g7Log("minecraft: check %s -> %d (%u)\n", name, r, seen[i].n);
    return r;
}

static void installSwitches(void)
{
    // the accessor goes on past its negative-id test, which rawRecord makes
    g7HookNear(RAW_RECORD, 8, rawRecord);
    rawRecordOrig = (uint32_t * (*)(int))(RAW_RECORD + 8);
    g7OverlayDraw = mcdFrame;
    traceEvents = getenv("MCD_TRACE_EVENTS") != NULL;
    *(void **)&eventSendOrig = g7HookNear(0x4af4b0, 8, eventSend);
    if (getenv("MCD_TRACE_CMDS"))
        installCmdTrace();
    traceChecks = getenv("MCD_TRACE_CHECKS") != NULL;
    *(void **)&flagCheckOrig = g7HookNear(0x4af990, 5, flagCheck);
}

// The game hashes its own code (0x78d230: 0x416b20..0x117bdb9 through
// 0xb1e4a0, for its license and integrity checks), and the loader's patches
// are in it.  As Ultra Loader does, the hash is made over the code as the dump
// has it: a copy read from gamedump.exec.
#define CODE_HASH 0x78d230
#define CODE_HASH_IMPL 0xb1e4a0
#define CODE_START 0x416b20
#define CODE_END 0x117bdb9
static uint8_t *pristineCode;

static long codeHash(void *out, int n)
{
    long (*hash)(const void *, long, void *, int) = (void *)CODE_HASH_IMPL;
    return hash(pristineCode, CODE_END - CODE_START, out, n);
}

static void installCodeHash(void)
{
    char path[PATH_MAX];
    FILE *f;
    snprintf(path, sizeof path, "%s/gamedump.exec", g7GameDir);
    if (!(pristineCode = malloc(CODE_END - CODE_START)) || !(f = fopen(path, "rb")))
    {
        g7Log("minecraft: cannot read %s, the code hash sees the patches\n", path);
        free(pristineCode);
        pristineCode = NULL;
        return;
    }
    int ok = fseek(f, CODE_START - 0x400000, SEEK_SET) == 0 &&
             fread(pristineCode, 1, CODE_END - CODE_START, f) == CODE_END - CODE_START;
    fclose(f);
    if (!ok)
    {
        free(pristineCode);
        pristineCode = NULL;
        return;
    }
    // The code as the game was built, which its hash keys the decryption of
    // its scene data with (0x5169b2, 0x51911c: the hash into 0xb1d5f0 with the
    // blobs at 0x16de1c0..): two branches of the dump made six NOPs are put
    // back, the integrity check's failure screen (jne +0x2b6 at 0x4a4b37) and
    // the controller check (jne +0xa7 at 0x4b1e6b, as Ultra Loader restores
    // it).  The code that runs keeps both skipped.  With the hash right, this
    // range of the dump hashes to Ultra Loader's C83 identity (sha256 c83f6e..);
    // with it wrong the scenes' animations never run (the transitions, the
    // player boxes).
    static const struct { uintptr_t at; const char *orig; } branches[] = {
        {0x4a4b37, "\x0f\x85\xb6\x02\x00\x00"},
        {0x4b1e6b, "\x0f\x85\xa7\x00\x00\x00"},
    };
    for (size_t i = 0; i < sizeof branches / sizeof branches[0]; i++)
    {
        uint8_t *at = pristineCode + (branches[i].at - CODE_START);
        if (!memcmp(at, "\x90\x90\x90\x90\x90\x90", 6))
            memcpy(at, branches[i].orig, 6);
    }
    g7Detour(CODE_HASH, (void *)codeHash);
}

// Two calls of the game's start, bridged as Ultra Loader's "mcd-v5" startup
// bridge does (built in a page at 0x70000000, in reach of their rel32 calls):
// - before the license routine (call 0x4a7850 at 0x430977), the data paths
//   registered again (0x42e3a0: data/, pmuser/, the attract pages, the card
//   tables...), the dump holding the state of a run already started;
// - before a loaded script is parsed (call 0x516ba0 at 0x540523; the buffer
//   is at the caller's rsp+0x28, its length at rsp+0x1c), a script with no
//   NUL in it copied into a game allocation 0x20 bytes longer, ending with
//   zeroes: the parser reads on past its end otherwise.
#define BRIDGE_PAGE 0x70000000
#define LICENSE_CALL 0x430977
#define LICENSE_ROUTINE 0x4a7850
#define REGISTER_PATHS 0x42e3a0
#define SCRIPT_CALL 0x540523
#define SCRIPT_PARSE 0x516ba0
#define GAME_MALLOC 0x4db580
#define GAME_FREE 0x4d9f90

static char *terminateScript(char *text, unsigned len)
{
    void *(*gameMalloc)(size_t) = (void *)GAME_MALLOC;
    void (*gameFree)(void *) = (void *)GAME_FREE;
    char *copy;
    if (!text || len - 1 >= 0xffffff || memchr(text, 0, len))
        return NULL;
    if (!(copy = gameMalloc((size_t)len + 0x20)))
        return NULL;
    memcpy(copy, text, len);
    memset(copy + len, 0, 0x20);
    gameFree(text);
    g7Log("minecraft: script of %u bytes NUL-terminated\n", len);
    return copy;
}

static void emitAbs(uint8_t **p, uint8_t op, uint64_t to) // call/jmp *0(%rip)
{
    uint8_t *q = *p;
    *q++ = 0xff;
    *q++ = op; // 0x15 call, 0x25 jmp
    *q++ = 0x02; *q++ = 0; *q++ = 0; *q++ = 0; // the address after a 2-byte jump
    *q++ = 0xeb; *q++ = 0x08;                  // jmp over it
    memcpy(q, &to, 8);
    q += 8;
    *p = q;
}

static void relCall(uintptr_t at, const void *to)
{
    int32_t rel = (int32_t)((intptr_t)to - (intptr_t)(at + 5));
    *(uint8_t *)at = 0xe8;
    memcpy((void *)(at + 1), &rel, 4);
}

static void installBridges(void)
{
    static const uint8_t licenseCall[5] = {0xe8, 0xd4, 0x6e, 0x07, 0x00}, scriptCall[5] = {0xe8, 0x78, 0x66, 0xfd, 0xff};
    uint8_t *page, *p, *b1, *b2;
    if (memcmp((void *)LICENSE_CALL, licenseCall, 5) || memcmp((void *)SCRIPT_CALL, scriptCall, 5))
    {
        g7Log("minecraft: startup calls not the C83 ones, no bridges\n");
        return;
    }
    page = mmap((void *)BRIDGE_PAGE, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page == MAP_FAILED)
    {
        g7Log("minecraft: no page for the startup bridges\n");
        return;
    }
    // 1: push rdi, rsi, rdx, rcx, r8, r9; align; call REGISTER_PATHS; pop; jmp LICENSE_ROUTINE
    p = b1 = page;
    static const uint8_t save[] = {0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51, 0x48, 0x83, 0xec, 0x08, 0x31, 0xc0};
    static const uint8_t restore[] = {0x48, 0x83, 0xc4, 0x08, 0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f};
    memcpy(p, save, sizeof save);
    p += sizeof save;
    emitAbs(&p, 0x15, REGISTER_PATHS);
    memcpy(p, restore, sizeof restore);
    p += sizeof restore;
    emitAbs(&p, 0x25, LICENSE_ROUTINE);
    // 2: mov esi,[rsp+0x24]; push rdi; call terminateScript; pop rdi;
    //    test rax,rax; cmovne rdi,rax; mov [rsp+0x30],rdi; jmp SCRIPT_PARSE
    p = b2 = page + 0x300;
    static const uint8_t pre[] = {0x8b, 0x74, 0x24, 0x24, 0x57};
    static const uint8_t post[] = {0x5f, 0x48, 0x85, 0xc0, 0x48, 0x0f, 0x45, 0xf8, 0x48, 0x89, 0x7c, 0x24, 0x30};
    memcpy(p, pre, sizeof pre);
    p += sizeof pre;
    emitAbs(&p, 0x15, (uint64_t)(uintptr_t)terminateScript);
    memcpy(p, post, sizeof post);
    p += sizeof post;
    emitAbs(&p, 0x25, SCRIPT_PARSE);
    if (getenv("MCD_BRIDGE_PATHS"))
        relCall(LICENSE_CALL, b1);
    relCall(SCRIPT_CALL, b2);
    // and Ultra's data patches: heap pointers of the dumped run cleared, the
    // license state set
    if (getenv("MCD_ULTRA_DATA"))
    {
        *(uint64_t *)0x23fd048 = 0;
        *(uint64_t *)0x23fd050 = 0;
        *(uint64_t *)0x23fd058 = 0;
        *(uint32_t *)0x17c69e0 = 1;
        *(uint32_t *)0x17c69ec = 1;
    }
    g7Log("minecraft: startup bridges installed\n");
}

// The keyboard, MAME's default layout: each station's joystick (up, down,
// left, right), its three buttons, its START and coin.
static const struct
{
    int up, down, left, right, button[3], start, coin;
} stationKeys[MCD_STATIONS] = {
    {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, {KEY_LEFTCTRL, KEY_LEFTALT, KEY_SPACE}, KEY_1, KEY_5},
    {KEY_R, KEY_F, KEY_D, KEY_G, {KEY_A, KEY_S, KEY_Q}, KEY_2, KEY_6},
    {KEY_I, KEY_K, KEY_J, KEY_L, {KEY_RIGHTCTRL, KEY_RIGHTSHIFT, KEY_ENTER}, KEY_3, KEY_7},
    {KEY_KP8, KEY_KP2, KEY_KP4, KEY_KP6, {KEY_KP0, KEY_KPDOT, KEY_KPENTER}, KEY_4, KEY_8},
};

// Open every evdev keyboard (a device that reports KEY_ENTER and the letters).
#define KBD_MAX 8
static int kbdFd[KBD_MAX];
static int kbdCount;

static int isKeyboard(int fd)
{
    unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return 0;
#define HAS(b) (bits[(b) / (8 * sizeof(long))] & (1UL << ((b) % (8 * sizeof(long)))))
    return HAS(KEY_ENTER) && HAS(KEY_A) && HAS(KEY_Z) && !HAS(BTN_LEFT);
#undef HAS
}

static void openKeyboards(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    if (!d)
        return;
    while ((e = readdir(d)) && kbdCount < KBD_MAX)
    {
        char path[300];
        if (strncmp(e->d_name, "event", 5))
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (isKeyboard(fd))
            kbdFd[kbdCount++] = fd;
        else
            close(fd);
    }
    closedir(d);
}

// Current key state, indexed by KEY_* code.
static unsigned char keyState[KEY_CNT];

// ---------------------------------------------------------------------------
// Card reader.
//
// The cabinet reads its cards as QR codes through one webcam per station
// (zbar).  The webcam thread (0x429ab0) puts the first 8 bytes of a decoded
// QR code in that station's scan slot, under the station's pthread mutex; the
// game's main loop (0x44ced0) takes the slot and calls the scan gate
// 0x42abf0(station, code, notify, force).  That gate accepts the card only
// on a scan screen (byte 0x178cb10) and only when the station can take a card
// (0x444c40).  The loader skips the camera and fills the slot itself, so the
// game's own checks still decide.  (Ultra Loader calls the handler 0x42a7e0
// directly from its SwapBuffers hook; filling the slot works for all four
// stations without calling into the game from another thread.)
//
// The cards come from the binder (g7McdBinder.cpp): the hand a player scans
// waits here and goes into the slot one card after another, each once the
// game has taken the one before, and only while the game is on a scan screen
// (a card put in the slot elsewhere is taken and thrown away).
//
// Codes are the 8-character card ids: items a0000000.., skins/pets a30E0000..,
// heroes c01A1000.. (docs/minecraft-ultraloader-card-reader.md).
typedef struct
{
    char code[8];
    uint8_t zero, pending, notify, force;
} McdScanSlot;
#define MCD_SCAN_SLOTS ((volatile McdScanSlot *)0x178cae0)
#define MCD_SCAN_MUTEX(p) ((pthread_mutex_t *)(uintptr_t)(0x1c6f120 + (p) * 0x28))
#define MCD_SCAN_ENABLED (*(volatile uint8_t *)0x178cb10)

static pthread_mutex_t queueLock = PTHREAD_MUTEX_INITIALIZER;
static char queue[MCD_STATIONS][MCD_HAND_MAX][9];
static int queueLen[MCD_STATIONS], queuePos[MCD_STATIONS];

void mcdPresent(int station, const char (*codes)[9], int count)
{
    if (station < 0 || station >= MCD_STATIONS)
        return;
    pthread_mutex_lock(&queueLock);
    if (count > MCD_HAND_MAX)
        count = MCD_HAND_MAX;
    memcpy(queue[station], codes, sizeof queue[0][0] * count);
    queueLen[station] = count;
    queuePos[station] = 0;
    pthread_mutex_unlock(&queueLock);
}

int mcdScanScreen(void)
{
    return MCD_SCAN_ENABLED != 0;
}

int mcdWaiting(int station)
{
    pthread_mutex_lock(&queueLock);
    int n = queueLen[station] - queuePos[station];
    pthread_mutex_unlock(&queueLock);
    return n;
}

static void scanTick(void)
{
    static int saidWaiting[MCD_STATIONS];
    pthread_mutex_lock(&queueLock);
    for (int p = 0; p < MCD_STATIONS; p++)
    {
        if (queuePos[p] >= queueLen[p])
            continue;
        if (!MCD_SCAN_ENABLED)
        {
            if (!saidWaiting[p]++)
                g7Log("minecraft: P%d: %d card(s) wait for a card scan screen\n", p + 1, queueLen[p] - queuePos[p]);
            continue;
        }
        saidWaiting[p] = 0;
        if (pthread_mutex_trylock(MCD_SCAN_MUTEX(p)))
            continue;
        volatile McdScanSlot *s = &MCD_SCAN_SLOTS[p];
        if (!s->pending)
        {
            const char *c = queue[p][queuePos[p]++];
            memcpy((void *)s->code, c, 8);
            s->zero = 0;
            s->notify = 1;
            s->force = 0;
            s->pending = 1;
            g7Log("minecraft: P%d card %s scanned (%d/%d)\n", p + 1, c, queuePos[p], queueLen[p]);
        }
        pthread_mutex_unlock(MCD_SCAN_MUTEX(p));
    }
    pthread_mutex_unlock(&queueLock);
}

// ---------------------------------------------------------------------------
// The binder's controls.  F3..F6 open it for station 1..4 (again: close it);
// a pad's Select+Start opens and closes it too.  While it is open the keys and
// the pads drive it and the stations' joysticks and switches rest, so the game
// sees none of it.
static const int binderOpenKeys[MCD_STATIONS] = {KEY_F3, KEY_F4, KEY_F5, KEY_F6};
static const struct { int key, in; } binderKeys[] = {
    {KEY_UP, MCD_IN_UP}, {KEY_W, MCD_IN_UP}, {KEY_DOWN, MCD_IN_DOWN}, {KEY_S, MCD_IN_DOWN},
    {KEY_LEFT, MCD_IN_LEFT}, {KEY_A, MCD_IN_LEFT}, {KEY_RIGHT, MCD_IN_RIGHT}, {KEY_D, MCD_IN_RIGHT},
    {KEY_SPACE, MCD_IN_PICK}, {KEY_Z, MCD_IN_PICK},
    {KEY_BACKSPACE, MCD_IN_BACK}, {KEY_X, MCD_IN_BACK}, // not Escape: it quits the game
    {KEY_C, MCD_IN_PLAYER}, {KEY_V, MCD_IN_PLAYER},
    {KEY_Q, MCD_IN_TAB_PREV}, {KEY_PAGEUP, MCD_IN_TAB_PREV}, {KEY_E, MCD_IN_TAB_NEXT}, {KEY_PAGEDOWN, MCD_IN_TAB_NEXT},
    {KEY_ENTER, MCD_IN_SCAN}, {KEY_KPENTER, MCD_IN_SCAN},
    {KEY_TAB, MCD_IN_CLEAR}, {KEY_DELETE, MCD_IN_CLEAR},
    {KEY_1, MCD_IN_P1}, {KEY_2, MCD_IN_P2}, {KEY_3, MCD_IN_P3}, {KEY_4, MCD_IN_P4},
};

// A key went down or up.
static void binderKey(int key, int down)
{
    for (int p = 0; p < MCD_STATIONS; p++)
        if (key == binderOpenKeys[p])
        {
            if (!down)
                return;
            if (!mcdBinderIsOpen())
                mcdBinderOpen(p);
            else
                mcdBinderClose();
            return;
        }
    if (!mcdBinderIsOpen())
        return;
    for (size_t i = 0; i < sizeof binderKeys / sizeof binderKeys[0]; i++)
        if (binderKeys[i].key == key)
            mcdBinderButton(binderKeys[i].in, down, 0);
}

// The real pads, read at the evdev level as the keyboards are.  The loader's
// own virtual pad has no physical path, which tells it apart.
#define PAD_MAX 8
typedef struct
{
    int fd;
    char path[300];
    int absMin[ABS_CNT], absMax[ABS_CNT], abs[ABS_CNT];
    unsigned char btn[KEY_CNT - BTN_MISC];
    int dirs;           // the directions held: bit per MCD_IN_UP..RIGHT
    int startDuring;    // Start went down while Select was held
    int latched;        // Select+Start opened or closed it: wait for both up
} Pad;
static Pad pads[PAD_MAX];
static int padCount;

static int isPad(int fd)
{
    unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
    char phys[64] = "";
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return 0;
#define HAS(b) (bits[(b) / (8 * sizeof(long))] & (1UL << ((b) % (8 * sizeof(long)))))
    if (!HAS(BTN_SOUTH) && !HAS(BTN_GAMEPAD))
        return 0;
#undef HAS
    return ioctl(fd, EVIOCGPHYS(sizeof phys - 1), phys) > 0 && phys[0];
}

static int noInput;

static void scanPads(void)
{
    DIR *d = noInput ? NULL : opendir("/dev/input");
    struct dirent *e;
    if (!d)
        return;
    while ((e = readdir(d)) && padCount < PAD_MAX)
    {
        char path[300];
        int known = 0;
        if (strncmp(e->d_name, "event", 5))
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        for (int i = 0; i < padCount; i++)
            known |= !strcmp(pads[i].path, path);
        for (int i = 0; i < kbdCount && !known; i++)
        {
            char fdPath[64], target[300] = "";
            snprintf(fdPath, sizeof fdPath, "/proc/self/fd/%d", kbdFd[i]);
            ssize_t n = readlink(fdPath, target, sizeof target - 1);
            known = n > 0 && (target[n] = 0, !strcmp(target, path));
        }
        if (known)
            continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (!isPad(fd))
        {
            close(fd);
            continue;
        }
        Pad *p = &pads[padCount++];
        memset(p, 0, sizeof *p);
        p->fd = fd;
        snprintf(p->path, sizeof p->path, "%s", path);
        for (int a = 0; a < ABS_CNT; a++)
        {
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(a), &ai) == 0)
            {
                p->absMin[a] = ai.minimum;
                p->absMax[a] = ai.maximum;
                p->abs[a] = ai.value;
            }
        }
        char name[128] = "";
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        g7Log("minecraft: pad %s (%s): Select+Start opens the card binder\n", name, path);
    }
    closedir(d);
}

// Where the stick or the hat points: -1, 0 or 1.
static int axisSide(const Pad *p, int a)
{
    int lo = p->absMin[a], hi = p->absMax[a];
    if (hi <= lo)
        return 0;
    int mid = (lo + hi) / 2, half = (hi - lo) / 2, v = p->abs[a] - mid;
    if (hi - lo <= 2)
        return v < 0 ? -1 : v > 0;
    return v < -half / 2 ? -1 : v > half / 2;
}

static int padBtn(const Pad *p, int code)
{
    return code >= BTN_MISC && code < KEY_CNT && p->btn[code - BTN_MISC];
}

static void padUpdateDirs(Pad *p)
{
    int x = axisSide(p, ABS_HAT0X), y = axisSide(p, ABS_HAT0Y);
    if (!x)
        x = axisSide(p, ABS_X);
    if (!y)
        y = axisSide(p, ABS_Y);
    if (padBtn(p, BTN_DPAD_LEFT)) x = -1;
    if (padBtn(p, BTN_DPAD_RIGHT)) x = 1;
    if (padBtn(p, BTN_DPAD_UP)) y = -1;
    if (padBtn(p, BTN_DPAD_DOWN)) y = 1;
    int dirs = (y < 0) << MCD_IN_UP | (y > 0) << MCD_IN_DOWN | (x < 0) << MCD_IN_LEFT | (x > 0) << MCD_IN_RIGHT;
    for (int b = MCD_IN_UP; b <= MCD_IN_RIGHT; b++)
        if ((dirs ^ p->dirs) & (1 << b) && mcdBinderIsOpen())
            mcdBinderButton(b, (dirs >> b) & 1, 1);
    p->dirs = dirs;
}

static void padButton(Pad *p, int code, int down)
{
    int select = padBtn(p, BTN_SELECT), start = padBtn(p, BTN_START);
    if (code >= BTN_MISC && code < KEY_CNT)
        p->btn[code - BTN_MISC] = (unsigned char)down;
    if (p->latched)
    {
        // the presses of Select+Start do nothing more until both are up
        if (!padBtn(p, BTN_SELECT) && !padBtn(p, BTN_START))
            p->latched = 0;
        if (code == BTN_SELECT || code == BTN_START)
            return;
    }
    if (down && ((code == BTN_START && select) || (code == BTN_SELECT && start)))
    {
        if (mcdBinderIsOpen())
            mcdBinderClose();
        else
            mcdBinderOpen(0);
        p->latched = 1;
        return;
    }
    if (code == BTN_DPAD_UP || code == BTN_DPAD_DOWN || code == BTN_DPAD_LEFT || code == BTN_DPAD_RIGHT)
    {
        padUpdateDirs(p);
        return;
    }
    if (!mcdBinderIsOpen())
        return;
    switch (code)
    {
    case BTN_SOUTH: mcdBinderButton(MCD_IN_PICK, down, 1); break;
    case BTN_EAST: mcdBinderButton(MCD_IN_BACK, down, 1); break;
    case BTN_NORTH: case BTN_WEST: mcdBinderButton(MCD_IN_PLAYER, down, 1); break;
    case BTN_TL: mcdBinderButton(MCD_IN_TAB_PREV, down, 1); break;
    case BTN_TR: mcdBinderButton(MCD_IN_TAB_NEXT, down, 1); break;
    case BTN_START: mcdBinderButton(MCD_IN_SCAN, down, 1); break;
    case BTN_SELECT:
        // Select alone clears the hand, when it is let go (it may be the start
        // of Select+Start)
        if (down)
            p->startDuring = 0;
        else if (!p->startDuring)
        {
            mcdBinderButton(MCD_IN_CLEAR, 1, 1);
            mcdBinderButton(MCD_IN_CLEAR, 0, 1);
        }
        break;
    }
    if (code == BTN_START && down && select)
        p->startDuring = 1;
}

static void readPads(void)
{
    for (int i = 0; i < padCount; i++)
    {
        Pad *p = &pads[i];
        struct input_event ev;
        ssize_t n;
        while ((n = read(p->fd, &ev, sizeof ev)) == (ssize_t)sizeof ev)
        {
            if (ev.type == EV_KEY && ev.value != 2)
                padButton(p, ev.code, ev.value != 0);
            else if (ev.type == EV_ABS && ev.code < ABS_CNT)
            {
                p->abs[ev.code] = ev.value;
                if (ev.code == ABS_X || ev.code == ABS_Y || ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y)
                    padUpdateDirs(p);
            }
        }
        if (n < 0 && errno == ENODEV)
        {
            // unplugged
            close(p->fd);
            pads[i--] = pads[--padCount];
        }
    }
}

// A pad's stick (or d-pad, or hat) on that axis, -32767..32767.
static int padAxis(const Pad *p, int a)
{
    int hat = axisSide(p, a ? ABS_HAT0Y : ABS_HAT0X);
    if (padBtn(p, a ? BTN_DPAD_UP : BTN_DPAD_LEFT))
        hat = -1;
    if (padBtn(p, a ? BTN_DPAD_DOWN : BTN_DPAD_RIGHT))
        hat = 1;
    if (hat)
        return hat * 32767;
    int code = a ? ABS_Y : ABS_X, lo = p->absMin[code], hi = p->absMax[code];
    if (hi <= lo)
        return 0;
    long mid = ((long)lo + hi) / 2, half = ((long)hi - lo) / 2;
    long v = half ? ((long)p->abs[code] - mid) * 32767 / half : 0;
    if (v > -6000 && v < 6000) // the dead zone
        v = 0;
    return v < -32767 ? -32767 : v > 32767 ? 32767 : (int)v;
}

static void *poller(void *arg)
{
    (void)arg;
    static unsigned char keyBlocked[KEY_CNT];
    int wasOpen = 0, padScanTick = 0;
    // MCD_AUTOSCAN=<seconds>: scan P1's hand on that period (testing without
    // a keyboard, e.g. a headless run).
    const char *autoEnv = getenv("MCD_AUTOSCAN");
    int autoPeriod = autoEnv ? atoi(autoEnv) * 1000000 / 6000 : 0, autoTick = 0;
    // MCD_TEST_BUTTON=<station>:<button>: press that joystick button every
    // few seconds (testing).
    const char *testEnv = getenv("MCD_TEST_BUTTON");
    int testStation = -1, testButton = 0, testTick = 0;
    if (testEnv && sscanf(testEnv, "%d:%d", &testStation, &testButton) != 2)
        testStation = -1;
    g7Log("minecraft: the four stations on the keyboard, MAME's layout, over %d evdev keyboard(s)\n", kbdCount);
    g7Log("minecraft: card binder: F3..F6 open it for P1..P4, Select+Start on a pad\n");
    scanPads();

    for (;;)
    {
        for (int i = 0; i < kbdCount; i++)
        {
            struct input_event ev;
            while (read(kbdFd[i], &ev, sizeof ev) == (ssize_t)sizeof ev)
                if (ev.type == EV_KEY && ev.code < KEY_CNT && ev.value != 2)
                {
                    keyState[ev.code] = ev.value ? 1 : 0;
                    if (!ev.value)
                        keyBlocked[ev.code] = 0;
                    binderKey(ev.code, ev.value != 0);
                }
        }
        readPads();
        if (++padScanTick >= 2000000 / 6000)
        {
            padScanTick = 0;
            scanPads();
        }
        if (autoPeriod > 0 && ++autoTick >= autoPeriod)
        {
            char codes[MCD_HAND_MAX][9];
            int n = mcdBinderHand(0, codes, MCD_HAND_MAX);
            autoTick = 0;
            if (n)
                mcdPresent(0, (const char(*)[9])codes, n);
        }
        scanTick();

        // the binder opened or closed: the keys held when it closes kept from
        // the game until they are let go
        int open = mcdBinderIsOpen();
        if (open != wasOpen)
        {
            g7Log("minecraft: card binder %s\n", open ? "opened" : "closed");
            if (!open)
                memcpy(keyBlocked, keyState, sizeof keyBlocked);
            wasOpen = open;
        }

        // the stations' joysticks: nothing while the binder is open
        for (int s = 0; s < MCD_STATIONS; s++)
        {
            int ax[JOY_AXES] = {0, 0};
            unsigned b = 0;
            if (!open && s < padCount)
            {
                const Pad *p = &pads[s];
                ax[0] = padAxis(p, 0);
                ax[1] = padAxis(p, 1);
                b |= padBtn(p, BTN_SOUTH) ? 1 : 0;
                b |= padBtn(p, BTN_EAST) ? 2 : 0;
                b |= padBtn(p, BTN_WEST) || padBtn(p, BTN_NORTH) ? 4 : 0;
            }
            if (!open)
            {
#define KEYDOWN(k) (keyState[k] && !keyBlocked[k])
                for (int n = 0; n < 3; n++)
                    if (KEYDOWN(stationKeys[s].button[n]))
                        b |= 1u << n;
                if (KEYDOWN(stationKeys[s].left) != KEYDOWN(stationKeys[s].right))
                    ax[0] = KEYDOWN(stationKeys[s].left) ? -32767 : 32767;
                if (KEYDOWN(stationKeys[s].up) != KEYDOWN(stationKeys[s].down))
                    ax[1] = KEYDOWN(stationKeys[s].up) ? -32767 : 32767;
            }
            // START: the station's key (1..4) or its pad's Start
            int start = !open && ((s < padCount && padBtn(&pads[s], BTN_START) && !padBtn(&pads[s], BTN_SELECT)) ||
                                  KEYDOWN(stationKeys[s].start));
            if (s == testStation && testButton == 9 && (testTick % (3000000 / 6000)) < 100000 / 6000)
                start = 1;
            switchHeld[SWI_START + s] = (unsigned char)start;
            // the station's buttons: the joystick's three, as switches too
            for (int n = 0; n < 3; n++)
                switchHeld[SWI_BUTTON + s * 3 + n] = (b >> n) & 1;
            // a cabinet's coin, one for two stations: 5 or 6 for P1/P2, 7 or 8 for
            // P3/P4
            switchHeld[SWI_COIN + s] =
                s < 2 && !open && (KEYDOWN(stationKeys[2 * s].coin) || KEYDOWN(stationKeys[2 * s + 1].coin));
            if (s == testStation && (++testTick % (3000000 / 6000)) < 100000 / 6000)
                b |= 1u << testButton;
            // MCD_TEST_JOY=<button>: P1's joystick button pressed too, half a
            // period after START (testing)
            static int testJoy = -2;
            if (testJoy == -2)
                testJoy = getenv("MCD_TEST_JOY") ? atoi(getenv("MCD_TEST_JOY")) : -1;
            if (testJoy >= 0 && (testTick + 250) % (3000000 / 6000) < 100000 / 6000)
                b |= 1u << testJoy;
            stationAxis[s][0] = ax[0];
            stationAxis[s][1] = ax[1];
            stationButtons[s] = b;
        }
        static int dispensedSaid[MCD_STATIONS];
        for (int s = 0; s < MCD_STATIONS; s++)
            if (dispensed[s] != dispensedSaid[s])
            {
                g7Log("minecraft: P%d's owed card dispensed (%d so far)\n", s + 1, dispensed[s]);
                dispensedSaid[s] = dispensed[s];
            }
        if (cmdWindow == 1)
        {
            struct timespec nowc;
            clock_gettime(CLOCK_MONOTONIC, &nowc);
            if ((nowc.tv_sec - cmdWindowAt.tv_sec) + (nowc.tv_nsec - cmdWindowAt.tv_nsec) / 1e9 > 2)
            {
                cmdWindow = 2;
                for (int i = 0; i < CMD_MAX; i++)
                    if (cmdCount[i])
                    {
                        const uint8_t *inst = cmdLastInst[i];
                        const uint8_t *prm = inst ? *(uint8_t *const *)(inst + 0x10) : NULL;
                        float t = 0;
                        if (prm)
                            memcpy(&t, prm + 0x40, 4);
                        g7Log("minecraft: cmd %-28s x%u inst %p +1c=%u +18=%u params '%.32s' +40=%g +44=%u\n", cmdName[i],
                              cmdCount[i], (void *)inst, inst ? *(const uint32_t *)(inst + 0x1c) : 0,
                              inst ? *(const uint32_t *)(inst + 0x18) : 0, prm ? (const char *)prm : "", t,
                              prm ? *(const uint32_t *)(prm + 0x44) : 0);
                    }
            }
        }
        if (getenv("MCD_TRACE_LICENSE"))
        {
            static int last0 = -1, last1 = -1, lastF = -1;
            int v0 = *(volatile int *)0x17c69e0, v1 = *(volatile int *)0x17c69ec, f = *(volatile uint8_t *)0x17a8b58;
            if (v0 != last0 || v1 != last1 || f != lastF)
                g7Log("minecraft: license state %d / %d, flag 0x17a8b58 %d, variant 0x17c69c0 %d\n", v0, v1, f,
                      *(volatile uint8_t *)0x17c69c0);
            last0 = v0;
            last1 = v1;
            lastF = f;
        }
        fflush(stderr); // the game buffers stderr: our lines out now
        switchHeld[SWI_SERVICE] = !open && (keyState[KEY_9] || keyState[KEY_F1]); // 9: MAME's service
        switchHeld[SWI_DIAG] = !open && keyState[KEY_F2];
        usleep(6000);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// PC/SC card reader stub.
//
// The game reads its NFC cards through the PC/SC API (libpcsclite: SCard*).
// On a cabinet (and under TeknoParrot, which stubs winscard) the reader is
// present, so the card system comes up and the game is "authorized"; on a bare
// Linux box with no reader those calls fail and the game shows "needs to be
// authorized to dispense more cards".  So the loader presents a reader itself.
//
// This first cut reports one reader with no card inserted -- enough to bring
// the card system up.  Injecting a card (for actual play) is a later step:
// SCardGetStatusChange would report PRESENT with an ATR and SCardTransmit would
// answer the NTAG reads.  On this Linux PC/SC's LONG/DWORD/SCARDCONTEXT are all
// 64-bit (wintypes.h non-Apple branch).
typedef long SC_LONG;
typedef unsigned long SC_DWORD;
typedef unsigned long SC_CTX;
typedef unsigned long SC_CARD;
#define SCARD_S_SUCCESS 0L
#define SCARD_E_TIMEOUT 0x8010000AL
#define SCARD_E_NO_SMARTCARD 0x8010000CL
#define SCARD_E_INSUFFICIENT_BUFFER 0x80100008L
#define SCARD_STATE_CHANGED 0x0002
#define SCARD_STATE_EMPTY 0x0010
#define SCARD_AUTOALLOCATE ((SC_DWORD) -1)

typedef struct
{
    const char *szReader;
    void *pvUserData;
    SC_DWORD dwCurrentState;
    SC_DWORD dwEventState;
    SC_DWORD cbAtr;
    unsigned char rgbAtr[33];
} SC_READERSTATE;

static const char scReaderName[] = "ACS ACR122U PICC Interface 00 00"; // one reader, double-null below

static SC_LONG scEstablish(SC_DWORD scope, const void *r1, const void *r2, SC_CTX *ctx)
{
    (void)scope; (void)r1; (void)r2;
    g7Log("minecraft: SCardEstablishContext (stub)\n");
    if (ctx) *ctx = 1;
    return SCARD_S_SUCCESS;
}
static SC_LONG scRelease(SC_CTX ctx) { (void)ctx; return SCARD_S_SUCCESS; }
static SC_LONG scCancel(SC_CTX ctx) { (void)ctx; return SCARD_S_SUCCESS; }

static SC_LONG scListReaders(SC_CTX ctx, const char *groups, char *readers, SC_DWORD *len)
{
    (void)ctx; (void)groups;
    SC_DWORD need = sizeof scReaderName + 1; // name + its NUL + list-terminating NUL
    if (!len) return SCARD_E_INSUFFICIENT_BUFFER;
    if (readers && *len == SCARD_AUTOALLOCATE)
    {
        char *buf = malloc(need);
        if (!buf) return SCARD_E_INSUFFICIENT_BUFFER;
        memcpy(buf, scReaderName, sizeof scReaderName);
        buf[sizeof scReaderName] = 0;
        *(char **)readers = buf;
        *len = need;
        return SCARD_S_SUCCESS;
    }
    if (!readers || *len < need) { *len = need; return readers ? SCARD_E_INSUFFICIENT_BUFFER : SCARD_S_SUCCESS; }
    memcpy(readers, scReaderName, sizeof scReaderName);
    readers[sizeof scReaderName] = 0;
    *len = need;
    return SCARD_S_SUCCESS;
}

// No card present: report EMPTY once, then wait out the timeout so the poll
// loop neither spins nor sees a phantom card.
static SC_LONG scGetStatusChange(SC_CTX ctx, SC_DWORD timeout, SC_READERSTATE *states, SC_DWORD n)
{
    (void)ctx;
    SC_LONG ret = SCARD_E_TIMEOUT;
    for (SC_DWORD i = 0; i < n; i++)
    {
        SC_DWORD ev = SCARD_STATE_EMPTY;
        if (!(states[i].dwCurrentState & SCARD_STATE_EMPTY))
        {
            ev |= SCARD_STATE_CHANGED;
            ret = SCARD_S_SUCCESS;
        }
        states[i].dwEventState = ev;
        states[i].cbAtr = 0;
    }
    if (ret == SCARD_E_TIMEOUT)
        usleep((timeout == (SC_DWORD)-1 || timeout > 400 ? 400 : timeout) * 1000);
    return ret;
}
static SC_LONG scConnect(SC_CTX ctx, const char *reader, SC_DWORD share, SC_DWORD proto,
                         SC_CARD *card, SC_DWORD *active)
{
    (void)ctx; (void)reader; (void)share; (void)proto; (void)card; (void)active;
    return SCARD_E_NO_SMARTCARD; // no card to connect to yet
}
static SC_LONG scDisconnect(SC_CARD card, SC_DWORD disp) { (void)card; (void)disp; return SCARD_S_SUCCESS; }
static SC_LONG scTransmit(SC_CARD card, const void *spci, const unsigned char *sbuf, SC_DWORD slen,
                          void *rpci, unsigned char *rbuf, SC_DWORD *rlen)
{
    (void)card; (void)spci; (void)sbuf; (void)slen; (void)rpci; (void)rbuf;
    if (rlen) *rlen = 0;
    return SCARD_E_NO_SMARTCARD;
}

// The game resolves SCard* through dlsym, so both the import slots and the
// dlsym path must land on the stubs.
static const struct { const char *name; uintptr_t slot; void *fn; } scard[] = {
    {"SCardEstablishContext", 0x16d2c28, scEstablish},
    {"SCardReleaseContext", 0x16d3600, scRelease},
    {"SCardCancel", 0x16d2f70, scCancel},
    {"SCardListReaders", 0x16d2af8, scListReaders},
    {"SCardGetStatusChange", 0x16d3850, scGetStatusChange},
    {"SCardConnect", 0x16d3d08, scConnect},
    {"SCardDisconnect", 0x16d3c20, scDisconnect},
    {"SCardTransmit", 0x16d3cd0, scTransmit},
};

static void *mcdDlsym(const char *name)
{
    for (size_t i = 0; i < sizeof scard / sizeof scard[0]; i++)
        if (!strcmp(name, scard[i].name))
        {
            static int said[sizeof scard / sizeof scard[0]];
            if (!said[i]++) g7Log("minecraft: dlsym %s -> PC/SC stub\n", name);
            return scard[i].fn;
        }
    return NULL;
}

static void installPcsc(void)
{
    int ok = 0;
    for (size_t i = 0; i < sizeof scard / sizeof scard[0]; i++)
    {
        *(void **)scard[i].slot = scard[i].fn;
        if (*(void **)scard[i].slot == scard[i].fn) ok++;
    }
    g7GameDlsymHook = mcdDlsym;
    g7Log("minecraft: PC/SC card reader stub installed (%d/%zu slots + dlsym hook)\n",
          ok, sizeof scard / sizeof scard[0]);
}

void g7MinecraftInstall(const G7Game *game)
{
    (void)game;
    installPcsc();
    // MCD_CARDS: P1's hand to begin with (comma separated card codes)
    char hand[MCD_HAND_MAX][9];
    mcdBinderSetHand(0, getenv("MCD_CARDS"));
    if (getenv("MCD_AUTOSCAN") && !mcdBinderHand(0, hand, MCD_HAND_MAX))
        mcdBinderSetHand(0, "a30E0000,a0000000,a10f0000,a20r0000,a40Q0000");
    // MCD_NO_INPUT=1: no keyboard or pad read (test runs beside a desktop)
    noInput = getenv("MCD_NO_INPUT") != NULL;
    if (!noInput)
        openKeyboards();
    if (!kbdCount)
        g7Log("minecraft: no evdev keyboard found (need read on /dev/input/event*); no keyboard input\n");
    installJoysticks();
    installCabinet();
    installDispenser();
    installSwitches();
    installCodeHash();
    installBridges();
    // MCD_BINDER_OPEN=1: open the binder at once (testing)
    if (getenv("MCD_BINDER_OPEN"))
        mcdBinderOpen(0);
    pthread_t t;
    pthread_create(&t, NULL, poller, NULL);
    pthread_detach(t);
}
