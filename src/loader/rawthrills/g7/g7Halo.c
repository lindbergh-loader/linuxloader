// Halo: Fireteam Raven's own hooks (see g7.h): its cabinets, the RIO boards
// it waits for, its switch records fed from the keyboard, the mouse and
// evdev, and its IR guns.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "g7.h"
#include "../../config/iniParser.h"
#include "../../graphics/frameScale.h"

// Halo's four cabinets, as TeknoParrot names them:
//   3 = Super Deluxe    "SUPER DELUXE TETHERED"  4 players, data/prod/conf
//   4 = Mounted Gun     "STANDARD MOUNTED"       2 players, data/prod/conf_2p
//   8 = 55" Dedicated   "STANDARD TETHERED"      2 players, data/prod/conf_2p
//   9 = Dual Screen     "DUAL-SCREEN MOUNTED"    4 players, data/prod/conf
// The default is the single-screen 4 player cabinet; HALO_CABINET_TYPE
// picks another.
static long cabinetType(void)
{
    return g7CabinetType();
}
static int cabinetPlayers(void)
{
    long cabinet = cabinetType();
    return cabinet == 4 || cabinet == 8 ? 2 : 4;
}
// The cabinet with the operator's test, service and volume buttons: a 4
// player cabinet is two side by side, and its menus answer to the right
// one's (cabinet 1, the P3/P4 side).
static int operatorCabinet(void)
{
    return cabinetPlayers() == 4 ? 1 : 0;
}

// Keyboard and mouse input.
//
// The RIO board only carries the cabinet panel -- the coin door, start
// buttons and guns come from other boards -- so rather than emulate any of
// them the loader writes the game's own switch records.
//
// Timing is the whole trick.  Once a frame the input update at 0x54cdc0
// (called from the main loop at 0x413eb3) clears every switch record, then
// rebuilds it from the device layer and propagates aliases.  Anything
// written before that is wiped; anything written after it survives for the
// rest of the frame, which is when every consumer runs.  So the loader hooks
// that function and fills the records in on the way out.
//
// Record layout is the one the RIO's own switch setter uses at 0x61a810:
// +0x04 presses seen this frame, +0x08 releases, +0x0c how long the switch
// has been held (0 while it is up).  The other fields, +0x20 among them,
// hold pointers the game dereferences, so only these three are touched.
//
// A raw cabinet input drives several logical switches at once -- the game
// registers the fan-out through io_sw_alias (0x4acdb0) -- and the menus read
// the action switch at the end of each chain (0x2a1 select, 0x2a2 service,
// 0x29f/0x2a0 volume) rather than the per-cabinet one, so each key raises
// the whole chain.
//
// Switch ids come from the dump's diagnostics switch-test table (id/name
// pairs at 0x16f7c20).  Cabinets are numbered 0..3 and so are the gun
// stations; a standard (2 player) cabinet wires stations 0 and 3, the
// 4 player cabinets wire 0..3 in order.
#define SW_GUN_TRIGGER(station, n) (0x208 + (station) * 0xc + (n))
#define SW_START(station)          (0x24e + (station))
#define SW_VOL_UP                  0x254
#define SW_VOL_DOWN                0x255
#define SW_DIAG                    0x256
#define SW_COIN(cab, n)            (0x257 + (cab) * 8 + (n))
#define SW_CAB_DIAG(cab)           (0x287 + (cab) * 4)
#define SW_CAB_SERVICE(cab)        (0x288 + (cab) * 4)
#define SW_CAB_VOL_UP(cab)         (0x289 + (cab) * 4)
#define SW_CAB_VOL_DOWN(cab)       (0x28a + (cab) * 4)
// The menus read the action switches at the end of each alias chain --
// 0x2a1 select, 0x2a2 service, 0x29f/0x2a0 volume -- but the loader never
// drives them directly: the game raises them itself from the raw inputs.

// The IR gun manager reports no guns without the real hardware, and the
// game refuses to start with a gun missing: the attract loop checks each
// station through 0x417880, which maps the station to an IR gun index (on a
// standard cabinet station 3 is gun 1) and asks the manager at 0xdd4b10.
// Answer for as many guns as the cabinet seats (HALO_GUNS to change).
static int gunConnected(int gun)
{
    long guns = g7EnvNum("HALO_GUNS", cabinetPlayers());
    return gun >= 0 && gun < (int)guns;
}

// The device layer.  0x61e890 hands out the raw input record for a hardware
// id (0x239..0x298 for the cabinet), and the alias propagation at 0x4ad764
// adds that record's counters into every logical switch the id drives:
//     dst[0x0c] = max(dst[0x0c], src[0x0c])   held
//     dst[0x04] += src[0x04]                  presses
//     dst[0x08] += src[0x08]                  releases
// Driving the raw record is therefore how real hardware feeds the game, and
// the fan-out to 0x2a1/0x287/0x256 and friends comes for free.  It is also
// safe to shadow: the propagation only ever reads those five integers from
// it, unlike a logical record, whose +0x20 pointer the game dereferences.
static uint32_t *(*devGetOrig)(int id);
static struct { int raw; uint32_t rec[20]; } devs[64];
static int devCount;

static uint32_t *devGet(int id)
{
    for (int i = 0; i < devCount; i++)
        if (devs[i].raw == id)
            return devs[i].rec;
    return devGetOrig(id);
}

// Which raw ids drive a logical switch, read out of the game's own alias
// table (count at 0x1771850, {raw, logical} pairs from 0x1766f40 + 0x1122*8)
// so the mapping is whatever this cabinet actually registered.
// Ids 0x24e..0x2ff are logical switches, and some of them appear in the
// table as sources: a cabinet 0 coin input doubles as cabinet 1's diag or
// service button, for instance.  Driving one of those presses the coin as
// well, so prefer the dedicated raw inputs (0x239..0x24d for the panel,
// 0x31b and up for the guns) and only fall back to a logical source if a
// switch has no raw one at all.
static int rawInput(int id) { return id < 0x24e || id > 0x2ff; }

static int aliasSources(int logical, int *out, int max)
{
    const int *table = (const int *)(0x1766f40 + 0x1122 * 8);
    int count = *(const int *)0x1771850, n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++)
        for (int i = 0; i < count && n < max; i++)
            if (table[i * 2 + 1] == logical && rawInput(table[i * 2]) == !pass)
                out[n++] = table[i * 2];
    return n;
}

// One switch per key.  Each names the raw input to drive; the game then
// propagates it to the rest of its alias chain by itself, so the action
// switches (0x2a1 select, 0x2a2 service, 0x29f/0x2a0 volume) must NOT be
// listed here -- they have many raw sources, and binding one would press
// diag and both start buttons at once.
// Operator entries name cabinet 0's switch, moved to the operator's cabinet.
static const struct { unsigned long keysym; int sw, operator; } keymap[] = {
    {0xffbf, SW_CAB_DIAG(0), 1},                                         // F2: test
    {0xffbe, SW_CAB_SERVICE(0), 1}, {0x70, SW_CAB_SERVICE(0), 1},        // F1, p: service
    {0xff52, SW_CAB_VOL_UP(0), 1}, {0xff54, SW_CAB_VOL_DOWN(0), 1},      // up/down: volume
    {0x35, SW_COIN(0, 0)}, {0x75, SW_COIN(0, 0)},                        // 5, u: coin 1
    {0x36, SW_COIN(0, 1)},                                               // 6:    coin 2
    // A 4 player cabinet is two cabinets side by side, the right one (P3/P4)
    // with coin slots of its own: its credits start players 3 and 4.
    {0x37, SW_COIN(1, 0)}, {0x38, SW_COIN(1, 1)},                        // 7, 8: coins 3, 4
    // Start buttons: the station depends on the cabinet, so these are
    // placeholders for players 1..4, resolved by startSwitch().
    {0x31, -1}, {0x69, -1}, {0x32, -2}, {0x6f, -2}, {0x33, -3}, {0x34, -4},
};

// The gun's three buttons, on the pointer.  Station 0 is player 1; the
// trigger also raises 0x2a5, the action the alias table pairs with it.
static const struct { unsigned int mask; int sw; } pointerMap[] = {
    {1u << 8, SW_GUN_TRIGGER(0, 0)},                     // left:   trigger
    {1u << 10, SW_GUN_TRIGGER(0, 1)},                    // right:  reload
    {1u << 9, SW_GUN_TRIGGER(0, 2)},                     // middle: action
};

static struct { int id; int held, wasHeld; uint32_t heldFor; } switches[64];
static int switchCount;
// A bind drives a slot from a key (BIND_KEY, X keycode) or from an evdev
// source (BIND_EVDEV, index into evSources).
enum { BIND_KEY, BIND_EVDEV };
static struct { int kind, code, slot; } binds[128];
static int bindCount;
static int pointerSlots[4];

// Which station each player (0-based) plays at, -1 if the cabinet has no
// such player.
static int playerStation(int player)
{
    static const int stations2p[] = {0, 3}, stations4p[] = {0, 1, 2, 3};
    int twoPlayer = cabinetPlayers() == 2;
    if (player < 0 || player >= (twoPlayer ? 2 : 4))
        return -1;
    return twoPlayer ? stations2p[player] : stations4p[player];
}

static int startSwitch(int player)
{
    int station = playerStation(player);
    return station < 0 ? 0 : SW_START(station);
}

// A slot per raw input id, with a shadow device record to go with it.
static int slotFor(int raw)
{
    int slot;
    for (slot = 0; slot < switchCount; slot++)
        if (switches[slot].id == raw)
            return slot;
    if (switchCount == (int)(sizeof(switches) / sizeof(switches[0])) ||
        devCount == (int)(sizeof(devs) / sizeof(devs[0])))
        return -1;
    switches[switchCount].id = raw;
    devs[devCount++].raw = raw;
    return switchCount++;
}

// Bind a key or evdev source to every raw id that drives this logical switch.
static int bindLogical(int kind, int code, int logical)
{
    int raws[8], n = aliasSources(logical, raws, 8), bound = 0;
    for (int i = 0; i < n; i++)
    {
        int slot = slotFor(raws[i]);
        if (slot < 0 || bindCount == (int)(sizeof(binds) / sizeof(binds[0])))
            continue;
        binds[bindCount].kind = kind;
        binds[bindCount].code = code;
        binds[bindCount].slot = slot;
        bindCount++;
        bound++;
    }
    return bound;
}

static void *xdpy;
static unsigned long xroot;
static int (*queryKeymap)(void *, char[32]);
static unsigned char (*keysymToKeycode)(void *, unsigned long);
static int (*queryPointer)(void *, unsigned long, unsigned long *, unsigned long *,
                           int *, int *, int *, int *, unsigned int *);
static unsigned long (*defaultRootWindow)(void *);
static int (*getGeometry)(void *, unsigned long, unsigned long *, int *, int *,
                          unsigned int *, unsigned int *, unsigned int *, unsigned int *);

static void bindKeys(void)
{
    int unmapped = 0;
    for (size_t i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++)
    {
        int sw = keymap[i].sw;
        unsigned char code = keysymToKeycode(xdpy, keymap[i].keysym);
        if (sw < 0)
            sw = startSwitch(-sw - 1);
        else if (keymap[i].operator)
            sw += operatorCabinet() * 4;
        if (sw <= 0 || !code)
            continue;
        if (!bindLogical(BIND_KEY, code, sw))
            unmapped++;
    }
    for (size_t i = 0; i < sizeof(pointerMap) / sizeof(pointerMap[0]); i++)
        pointerSlots[i] = -1;
    for (size_t i = 0; i < sizeof(pointerMap) / sizeof(pointerMap[0]); i++)
    {
        int raws[8], n = aliasSources(pointerMap[i].sw, raws, 8);
        if (n > 0)
            pointerSlots[i] = slotFor(raws[0]);
    }
    fprintf(stderr, "g7_rt: halo: %d bindings over %d raw inputs (%d switches had no raw id)\n",
            bindCount, switchCount, unmapped);
    if (getenv("HALO_INPUT_DEBUG"))
    {
        const int *table = (const int *)(0x1766f40 + 0x1122 * 8);
        int count = *(const int *)0x1771850;
        fprintf(stderr, "g7_rt: halo: alias table has %d entries\n", count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "g7_rt: halo:   alias %#x -> %#x\n", table[i * 2], table[i * 2 + 1]);
        for (int i = 0; i < bindCount; i++)
            if (binds[i].kind == BIND_KEY)
                fprintf(stderr, "g7_rt: halo:   key code %d -> raw %#x\n",
                        binds[i].code, switches[binds[i].slot].id);
    }
}

static int openX(void)
{
    void *x11 = dlopen("libX11.so.6", RTLD_NOW);
    void *(*openDisplay)(const char *);
    if (!x11)
    {
        fprintf(stderr, "g7_rt: halo: dlopen libX11.so.6 failed: %s\n", dlerror());
        return 0;
    }
    openDisplay = dlsym(x11, "XOpenDisplay");
    queryKeymap = dlsym(x11, "XQueryKeymap");
    keysymToKeycode = dlsym(x11, "XKeysymToKeycode");
    queryPointer = dlsym(x11, "XQueryPointer");
    defaultRootWindow = dlsym(x11, "XDefaultRootWindow");
    getGeometry = dlsym(x11, "XGetGeometry");
    if (!openDisplay || !queryKeymap || !keysymToKeycode)
    {
        fprintf(stderr, "g7_rt: halo: libX11 is missing the entry points needed\n");
        return 0;
    }
    if (!(xdpy = openDisplay(NULL)))
    {
        static int said;
        if (!said++)
            fprintf(stderr, "g7_rt: halo: XOpenDisplay failed (DISPLAY=%s)\n",
                    getenv("DISPLAY") ? getenv("DISPLAY") : "unset");
        return 0;
    }
    if (defaultRootWindow)
        xroot = defaultRootWindow(xdpy);
    bindKeys();
    return 1;
}


// Evdev input ([Input] INPUT_MODE 2), configured as for the loader's other
// games: each [EVDEV] entry names an arcade input and the device input that
// drives it, in the loader's technical form:
//   /dev/input/event5:KEY:272      a key or button
//   /dev/input/event5:ABS:0        an axis (ABS_NEG: reversed)
//   /dev/input/event5:ABS:16:MIN   an axis pushed to one end, as a button
//   /dev/input/event7:REL:0        a mouse axis: its moves make a position
//                                  on the screen, a count per pixel
// The arcade inputs Halo has, for player n (the n-th station of the cabinet):
//   PLAYER_n_BUTTON_1/2/3   gun trigger, reload, action
//   PLAYER_n_BUTTON_START, PLAYER_n_COIN, PLAYER_n_BUTTON_SERVICE, TEST_BUTTON
//   ANALOGUE_2n-1/2n        gun n aim, X and Y
// The keyboard keeps working alongside it, as for the other games; the
// pointer stops driving a gun once evdev aims that gun.
#include <linux/input.h>

#define EV_BITS (8 * sizeof(unsigned long))
static struct
{
    char path[128];
    int fd;
    struct timespec retry;
    unsigned long keys[KEY_CNT / EV_BITS + 1];
    struct input_absinfo abs[ABS_CNT];
    float rel[REL_Y + 1]; // mouse position, 0..1
} evDevices[8];
static int evDeviceCount;
static struct { int device, type, code, reversed, end; } evSources[64];
static int evSourceCount;
static int evAim[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};   // gun -> X, Y source

static int evDevice(const char *path)
{
    for (int i = 0; i < evDeviceCount; i++)
        if (!strcmp(evDevices[i].path, path))
            return i;
    if (evDeviceCount == (int)(sizeof(evDevices) / sizeof(evDevices[0])))
        return -1;
    snprintf(evDevices[evDeviceCount].path, sizeof(evDevices[0].path), "%s", path);
    evDevices[evDeviceCount].fd = -1;
    evDevices[evDeviceCount].rel[REL_X] = evDevices[evDeviceCount].rel[REL_Y] = 0.5f;
    return evDeviceCount++;
}

static int evSource(const char *spec)
{
    char path[128], type[16], end[8] = "";
    int code, n = sscanf(spec, "%127[^:]:%15[^:]:%d:%7s", path, type, &code, end);
    int kind = n >= 3 && !strcmp(type, "KEY") ? EV_KEY
             : n >= 3 && (!strcmp(type, "ABS") || !strcmp(type, "ABS_NEG")) ? EV_ABS
             : n >= 3 && !strcmp(type, "REL") ? EV_REL : -1;
    int device;

    if (kind < 0 || code < 0 || code >= (kind == EV_KEY ? KEY_CNT : kind == EV_REL ? REL_Y + 1 : ABS_CNT) ||
        evSourceCount == (int)(sizeof(evSources) / sizeof(evSources[0])) || (device = evDevice(path)) < 0)
        return -1;
    evSources[evSourceCount].device = device;
    evSources[evSourceCount].type = kind;
    evSources[evSourceCount].code = code;
    evSources[evSourceCount].reversed = !strcmp(type, "ABS_NEG");
    evSources[evSourceCount].end = !strcmp(end, "MIN") ? -1 : !strcmp(end, "MAX") ? 1 : 0;
    return evSourceCount++;
}

static void evOpen(int i, const struct timespec *now)
{
    int fd;
    if (now->tv_sec < evDevices[i].retry.tv_sec)
        return;
    evDevices[i].retry.tv_sec = now->tv_sec + 2;
    if ((fd = open(evDevices[i].path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
        return;
    memset(evDevices[i].keys, 0, sizeof(evDevices[i].keys));
    ioctl(fd, EVIOCGKEY(sizeof(evDevices[i].keys)), evDevices[i].keys);
    for (int code = 0; code < ABS_CNT; code++)
        if (ioctl(fd, EVIOCGABS(code), &evDevices[i].abs[code]) < 0)
            memset(&evDevices[i].abs[code], 0, sizeof(evDevices[i].abs[code]));
    evDevices[i].fd = fd;
    fprintf(stderr, "g7_rt: halo: evdev %s open\n", evDevices[i].path);
}

static void evPoll(const struct timespec *now)
{
    for (int i = 0; i < evDeviceCount; i++)
    {
        struct input_event ev[64];
        ssize_t got;
        if (evDevices[i].fd < 0)
            evOpen(i, now);
        if (evDevices[i].fd < 0)
            continue;
        while ((got = read(evDevices[i].fd, ev, sizeof(ev))) > 0)
            for (size_t e = 0; e < (size_t)got / sizeof(ev[0]); e++)
            {
                if (ev[e].type == EV_KEY && ev[e].code < KEY_CNT)
                {
                    unsigned long bit = 1ul << (ev[e].code % EV_BITS);
                    if (ev[e].value)
                        evDevices[i].keys[ev[e].code / EV_BITS] |= bit;
                    else
                        evDevices[i].keys[ev[e].code / EV_BITS] &= ~bit;
                }
                else if (ev[e].type == EV_ABS && ev[e].code < ABS_CNT)
                    evDevices[i].abs[ev[e].code].value = ev[e].value;
                else if (ev[e].type == EV_REL && ev[e].code <= REL_Y)
                {
                    int span = ev[e].code == REL_X ? g7Config.width : g7Config.height;
                    float *p = &evDevices[i].rel[ev[e].code];
                    if (span <= 0)
                        span = ev[e].code == REL_X ? 1920 : 1080;
                    *p += (float)ev[e].value / (float)span;
                    *p = *p < 0.f ? 0.f : *p > 1.f ? 1.f : *p;
                }
            }
        if (got < 0 && errno != EAGAIN && errno != EINTR)
        {
            // Unplugged: forget its state, and look for it again later.
            fprintf(stderr, "g7_rt: halo: evdev %s lost\n", evDevices[i].path);
            close(evDevices[i].fd);
            evDevices[i].fd = -1;
            memset(evDevices[i].keys, 0, sizeof(evDevices[i].keys));
        }
    }
}

// An axis as 0..1 over its range (reversed for ABS_NEG), -1 if unknown.
static float evAxis(int s)
{
    const struct input_absinfo *a = &evDevices[evSources[s].device].abs[evSources[s].code];
    float t;
    if (evSources[s].type == EV_REL)
        return evDevices[evSources[s].device].fd < 0 ? -1.f : evDevices[evSources[s].device].rel[evSources[s].code];
    if (evDevices[evSources[s].device].fd < 0 || a->maximum <= a->minimum)
        return -1.f;
    t = (float)(a->value - a->minimum) / (float)(a->maximum - a->minimum);
    return evSources[s].reversed ? 1.f - t : t;
}

static int evHeld(int s)
{
    const unsigned long *keys = evDevices[evSources[s].device].keys;
    float t;
    if (evSources[s].type == EV_KEY)
        return (keys[evSources[s].code / EV_BITS] >> (evSources[s].code % EV_BITS)) & 1;
    if ((t = evAxis(s)) < 0.f)
        return 0;
    return evSources[s].end < 0 ? t < 0.25f : evSources[s].end > 0 ? t > 0.75f : t > 0.5f;
}

// The logical switch an [EVDEV] arcade input drives, 0 if none; aim axes
// are recorded in evAim instead.
static int evArcadeInput(const char *name, int source)
{
    int player, n, station;
    char what[32];

    if (!strcmp(name, "TEST_BUTTON"))
        return SW_CAB_DIAG(operatorCabinet());
    if (sscanf(name, "ANALOGUE_%d", &n) == 1)
    {
        if (n >= 1 && n <= 8)
            evAim[(n - 1) / 2][(n - 1) % 2] = source;
        return 0;
    }
    if (sscanf(name, "PLAYER_%d_%31s", &player, what) != 2 || (station = playerStation(player - 1)) < 0)
        return 0;
    if (sscanf(what, "BUTTON_%d", &n) == 1 && n >= 1 && n <= 3)
        return SW_GUN_TRIGGER(station, n - 1);
    if (!strcmp(what, "BUTTON_START"))
        return SW_START(station);
    // Players 1/2 coin the left cabinet, 3/4 the right one (see keymap).
    if (!strcmp(what, "COIN"))
        return SW_COIN((player - 1) / 2, (player - 1) % 2);
    if (!strcmp(what, "BUTTON_SERVICE"))
        return SW_CAB_SERVICE(operatorCabinet());
    return 0;
}

static void bindEvdev(void)
{
    IniSection *section = g7Ini() ? iniGetSection(g7Ini(), "EVDEV") : NULL;
    int bound = 0;
    if (!section)
    {
        fprintf(stderr, "g7_rt: halo: evdev input asked, but there is no [EVDEV] section\n");
        return;
    }
    for (int i = 0; i < section->numPairs; i++)
    {
        const char *name = section->pairs[i].key, *spec = section->pairs[i].value;
        int source, sw;
        if (!spec || !*spec)
            continue;
        if ((source = evSource(spec)) < 0)
        {
            fprintf(stderr, "g7_rt: halo: evdev %s = %s: not a device input (/dev/input/eventN:KEY|ABS|REL:code)\n", name, spec);
            continue;
        }
        sw = evArcadeInput(name, source);
        if (sw > 0 && bindLogical(BIND_EVDEV, source, sw))
            bound++;
        else if (sw > 0)
            fprintf(stderr, "g7_rt: halo: evdev %s: switch %#x has no raw input\n", name, sw);
    }
    fprintf(stderr, "g7_rt: halo: evdev: %d inputs over %d devices\n", bound, evDeviceCount);
    for (int gun = 0; gun < 4; gun++)
        if (evAim[gun][0] >= 0 && evAim[gun][1] >= 0)
            fprintf(stderr, "g7_rt: halo: evdev: gun %d aimed by %s\n", gun,
                    evDevices[evSources[evAim[gun][0]].device].path);
}

// Gun aim.  Once per frame and gun the game asks the irt layer for the gun
// position through 0xdd6c60 (gun, &x, &y), expecting normalized screen
// coordinates: 0..1 between the axis bounds it reads from its own objects,
// (-1, -1) for offscreen.  The pipeline that feeds it (cmgr serial link to
// the gun boards, LED pose solve at 0xdd8350, live array at 0x1e94680) never
// runs without the hardware, so the loader answers from evdev or the X
// pointer instead: the whole calibration transform downstream is bypassed.
// The game's Y axis points up; HALO_INVERT_X/Y override the directions.
static float gunAimX = -1.f, gunAimY = -1.f;   // pointer, normalized, -1 = unknown/offscreen

static int evAims(int gun)
{
    return gun >= 0 && gun < 4 && evAim[gun][0] >= 0 && evAim[gun][1] >= 0;
}

// A point of the game window (0..1 from its top left) to the game's
// coordinates, through the picture's place in the window when it is scaled.
// 0 when the point is off the picture.
static int windowToGame(float *x, float *y)
{
    static int invertX = -1, invertY = -1;
    if (invertX < 0)
    {
        invertX = g7EnvNum("HALO_INVERT_X", 0) != 0;
        invertY = g7EnvNum("HALO_INVERT_Y", 1) != 0;
    }
    if (!frameScaleWindowToGame(x, y))
        return 0;
    if (invertX) *x = 1.f - *x;
    if (invertY) *y = 1.f - *y;
    return 1;
}

static void gunGetPos(int gun, float *x, float *y)
{
    if (evAims(gun))
    {
        // A light gun pointed off the screen reports the edge of its range.
        float ex = evAxis(evAim[gun][0]), ey = evAxis(evAim[gun][1]);
        if (ex > 0.f && ex < 1.f && ey > 0.f && ey < 1.f && windowToGame(&ex, &ey))
        {
            *x = ex;
            *y = ey;
            return;
        }
    }
    else if (gun == (int)g7EnvNum("HALO_MOUSE_GUN", 0) && gunAimX >= 0.f)
    {
        *x = gunAimX;
        *y = gunAimY;
        return;
    }
    *x = -1.f;
    *y = -1.f;
}

// The game's per-frame input update.  Everything below runs after it has
// rebuilt the switch records, so the presses survive to be read.
static int (*inputOrig)(int);
static int inputUpdate(int arg)
{
    static int debug = -1;
    static struct timespec last;
    struct timespec now;
    char km[32];
    int dt, pointerGun;

    if (debug < 0)
    {
        debug = getenv("HALO_INPUT_DEBUG") != NULL;
        fprintf(stderr, "g7_rt: halo: input update hooked, loader live\n");
        if (g7Config.inputMode == 2)
            bindEvdev();
    }
    if (!xdpy)
        openX();
    clock_gettime(CLOCK_MONOTONIC, &now);
    dt = last.tv_sec ? (int)((now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000) : 0;
    last = now;

    for (int i = 0; i < switchCount; i++)
        switches[i].held = 0;
    if (evSourceCount)
        evPoll(&now);
    if (xdpy)
        queryKeymap(xdpy, km);
    for (int i = 0; i < bindCount; i++)
        if (binds[i].kind == BIND_EVDEV ? evHeld(binds[i].code)
                                        : xdpy && (km[binds[i].code / 8] & (1 << (binds[i].code % 8))))
            switches[binds[i].slot].held = 1;
    // The pointer is player 1's gun (station 0 buttons) unless evdev aims it.
    pointerGun = (int)g7EnvNum("HALO_MOUSE_GUN", 0);
    if (xdpy && queryPointer && xroot && !evAims(pointerGun))
    {
        unsigned long r, c;
        int rx, ry, wx, wy;
        unsigned int mask = 0;
        // Relative to the game's window once it has one, else to the screen.
        unsigned long window = g7GameWindow() ? g7GameWindow() : xroot;
        queryPointer(xdpy, window, &r, &c, &rx, &ry, &wx, &wy, &mask);
        for (size_t i = 0; i < sizeof(pointerMap) / sizeof(pointerMap[0]); i++)
            if ((mask & pointerMap[i].mask) && pointerSlots[i] >= 0)
                switches[pointerSlots[i]].held = 1;
        // Gun aim: normalize the pointer into the game's 0..1 coordinates.
        if (getGeometry)
        {
            unsigned long rr;
            int gx, gy;
            unsigned int ww = 0, wh = 0, bw, bd;
            float x, y;
            if (!getGeometry(xdpy, window, &rr, &gx, &gy, &ww, &wh, &bw, &bd))
                ww = wh = 0;
            x = ww ? (float)wx / (float)ww : -1.f;
            y = wh ? (float)wy / (float)wh : -1.f;
            if (ww && wh && windowToGame(&x, &y))
            {
                gunAimX = x;
                gunAimY = y;
            }
            else
                gunAimX = gunAimY = -1.f;
        }
        if (debug)
        {
            static unsigned int wasMask;
            if (mask != wasMask)
                fprintf(stderr, "g7_rt: halo: pointer buttons %#x at %d,%d\n", mask, rx, ry);
            wasMask = mask;
        }
    }
    for (int i = 0; i < switchCount; i++)
    {
        uint32_t *rec = devGet(switches[i].id);
        int held = switches[i].held;
        if (!rec)
            continue;
        // A press and a release each last one frame, which is what the RIO's
        // own switch setter produces.
        rec[1] = held && !switches[i].wasHeld;
        rec[2] = !held && switches[i].wasHeld;
        switches[i].heldFor = held ? switches[i].heldFor + (uint32_t)dt + 1 : 0;
        rec[3] = switches[i].heldFor;
        if (debug && (rec[1] || rec[2]))
            fprintf(stderr, "g7_rt: halo: raw %#x %s\n", switches[i].id,
                    rec[1] ? "pressed" : "released");
        switches[i].wasHeld = held;
    }
    if (debug && evSourceCount)
    {
        static int frames;
        if (frames++ % 120 == 0)
            for (int gun = 0; gun < 4; gun++)
                if (evAims(gun))
                    fprintf(stderr, "g7_rt: halo: evdev gun %d at %.3f,%.3f\n", gun,
                            evAxis(evAim[gun][0]), evAxis(evAim[gun][1]));
    }
    // Only now let the game run its update: it clears the logical records,
    // then propagates these raw ones into them.
    return inputOrig(arg);
}

// ---------------------------------------------------------------------------

#define HALO_DIR "/pm/g7/halo"

const char *g7HaloMapPath(const char *p, char *buf)
{
    // Halo's game-specific shaders include the shared g7 shader headers
    // through gameshaders/include, while this dump installs them under
    // shaders/include (asked for by the cabinet path, or already mapped).
    static const char gameShaderIncludes[] = "/data/prod/gameshaders/include/";
    const char *include = strstr(p, gameShaderIncludes);
    if (include)
    {
        snprintf(buf, PATH_MAX, "%s/data/prod/shaders/include/%s", g7GameDir, include + sizeof(gameShaderIncludes) - 1);
        return buf;
    }
    // The file check hashes the executable: the original, not the dump.
    if (!strcmp(p, HALO_DIR "/game"))
    {
        snprintf(buf, PATH_MAX, "%s/game.orig", g7GameDir);
        return buf;
    }
    return NULL;
}

static int one(void) { return 1; }
static int zero(void) { return 0; }

void g7HaloInstall(const G7Game *game)
{
    (void)game;
    g7Detour(0x6278d0, zero);  // RIO_Connected: 0
    // A 4 player cabinet has a RIO2 board per player pair, told apart by a
    // jumper, and the boot check at 0x4adac0 waits on "P1/P2 RIO2: 0 (ERROR -
    // Should be 1)" when they are missing.  It is the only caller of the
    // expected board count getter: answering 1 skips it, as on a 2 player
    // cabinet, while the RIO layer keeps its own (zero) board count.
    g7Detour(0x6213f0, one);
    // Per-frame input update: "push %r12; push %rbp; push %rbx; mov %edi,%ebx"
    *(void **)&inputOrig = g7HookNear(0x54cdc0, 6, inputUpdate);
    // Raw input record accessor: "test %edi,%edi; js ..."
    *(void **)&devGetOrig = g7HookNear(0x61e890, 8, devGet);
    g7Detour(0xdd4b10, gunConnected);  // IR gun manager: is gun N connected
    g7Detour(0xdd6c60, gunGetPos);     // IR gun manager: gun N aim, 0..1 screen coords
}
