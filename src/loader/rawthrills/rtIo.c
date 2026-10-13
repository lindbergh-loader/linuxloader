// RIO I/O board and input for g5 engine games.
//
// The USB transport of the RIO board is stubbed (a connected but silent
// board). Input goes through the engine's own input map, adjusted per game
// (see the input fields of RtGame):
//  - games can have the developer mode that maps the mouse to the guns (its
//    command-line switch was removed from the release builds) turned back
//    on, and keyboard keys added for the cabinet buttons;
//  - with evdev input (INPUT_MODE 2), the loader's evdev controllers fill the
//    emulated JVS state from the [EVDEV] mappings as for Lindbergh games, and
//    that state is fed each frame to the engine's I/O slots, where the
//    cabinet board's inputs arrive.

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

// ---------------------------------------------------------------------------
// RIO board: stub its USB transport so the game sees a connected, silent board.

static int retZero(void)
{
    return 0;
}

static int retOne(void)
{
    return 1;
}

void rtInstallIo(const RtGame *game)
{
    for (const RtStub *stub = game->stubs; stub && stub->name; stub++)
        rtDetour(stub->name, stub->value ? (void *)retOne : (void *)retZero);
    if (game->install)
        game->install(game);
    rtDetour("pmrt_usb_open", retOne);
    rtDetour("pmrt_usb_interrupt_read", retZero);
    rtDetour("pmrt_usb_interrupt_write", retZero);
    rtDetour("pmrt_usb_control_xfr", retZero);
    rtDetour("io_usbdev_init", retZero);
    rtDetour("read_thread_func", retZero);
    rtDetour("parse_in_report", retZero);
    rtDetour("RIO_FileLog", retZero);
    rtDetour("_Z30cpp_gyrogun_update_rt_wirelessi", retZero);
    rtDetour("xicoCardThread", retZero);
}

// ---------------------------------------------------------------------------
// Engine input map.

static const RtGame *currentGame;
static int (*inputAddMap)(int src, int dst);
static int (*gameInputMapsOrig)(void);
static int evdevInput;

static void addMaps(const RtMap *maps)
{
    for (const RtMap *m = maps; m && (m->src || m->dst); m++)
        inputAddMap(m->src, m->dst);
}

static int filtered(const RtMapFilter *filters, uint32_t src, uint32_t dst)
{
    for (const RtMapFilter *f = filters; f && f->srcMax; f++)
        if (src >= f->srcMin && src <= f->srcMax && dst >= f->dstMin && dst <= f->dstMax)
            return 1;
    return 0;
}

// Drop the map entries matching one of the filters.
static void filterInputMaps(const RtMapFilter *filters)
{
    uint32_t(*map)[2] = (uint32_t(*)[2])(uintptr_t)currentGame->inputMap;
    uint32_t *count = (uint32_t *)(uintptr_t)currentGame->inputMapCount;
    uint32_t kept = 0;

    if (!filters)
        return;
    for (uint32_t i = 0; i < *count; i++)
    {
        if (filtered(filters, map[i][0], map[i][1]))
            continue;
        map[kept][0] = map[i][0];
        map[kept][1] = map[i][1];
        kept++;
    }
    *count = kept;
}

// Games registering their maps inline: each entry as it is added.
static int (*inputAddMapOrig)(int src, int dst);

static int inputAddMapHook(int src, int dst)
{
    static int added;
    int ret;

    if (filtered(currentGame->devMapFilters, src, dst) || (evdevInput && filtered(currentGame->evdevMapFilters, src, dst)))
        return 0;
    ret = inputAddMapOrig(src, dst);
    if (!added)
    {
        added = 1;
        addMaps(currentGame->extraMaps);
        if (evdevInput)
            addMaps(currentGame->evdevMaps);
    }
    return ret;
}

static int gameInputMaps(void)
{
    if (currentGame->devInputFlag)
        *(volatile uint32_t *)(uintptr_t)currentGame->devInputFlag = 1;
    int ret = gameInputMapsOrig();

    filterInputMaps(currentGame->devMapFilters);
    addMaps(currentGame->extraMaps);
    if (evdevInput)
    {
        filterInputMaps(currentGame->evdevMapFilters);
        addMaps(currentGame->evdevMaps);
    }
    return ret;
}

// ---------------------------------------------------------------------------
// Evdev input through the loader's JVS state, fed to the engine's I/O slots
// (where the cabinet board's inputs arrive) from its SDL input backend.

// Engine digital input slot (io_input_digital).
typedef struct
{
    uint32_t unused;
    uint32_t pressed;  // became held this frame
    uint32_t released; // stopped being held this frame
    uint32_t heldTime;
    uint32_t pressCount;
    uint32_t releaseCount;
    uint32_t raw;
    uint32_t rawCount;
} IoDigital;

static int ioFromDesktop;

static void *(*ioGetInputAnalog)(int id);
static void *(*ioGetInputDigital)(int id);
static void (*ioInputAnalogUpdate)(void *slot, float raw);
static int (*ioSetInputRawRange)(int id, float min, float max);
static int (*ioLoopOrig)(int dt);
static volatile uint32_t *ioNewData;

// io_new_data_present only returns a flag: "mov flag,%eax" (after a frame
// setup in some builds). Find that flag.
static volatile uint32_t *findNewDataFlag(void)
{
    const uint8_t *code = rtSymbol("io_new_data_present");
    for (int i = 0; code && i < 16; i++)
        if (code[i] == 0xa1)
            return (volatile uint32_t *)(uintptr_t) * (const uint32_t *)(code + i + 1);
    return NULL;
}

// The engine's io_input_digital_update (not exported by every game) for a
// plain on/off switch.
static void ioDigitalSet(int id, int dt, int held)
{
    IoDigital *slot = ioGetInputDigital(id), old;

    if (!slot)
        return;
    old = *slot;
    slot->pressed = !old.pressed && !old.heldTime && held;
    slot->released = !old.released && old.heldTime && !held;
    slot->heldTime = held ? old.heldTime + dt + slot->pressed : 0;
    slot->raw = held;
    slot->pressCount += slot->pressed;
    slot->releaseCount += slot->released;
    slot->rawCount += held >= (int)old.raw ? held - old.raw : held + (0x100 - old.raw);
}

static void ioAnalogSet(int id, float raw)
{
    void *slot = ioGetInputAnalog(id);
    if (slot)
        ioInputAnalogUpdate(slot, raw);
}

// A coin is a count in the JVS state: each increment becomes a press of the
// coin switch, held COIN_PULSE_MS and followed by as long a gap, as a coin
// mech's. A one-frame press is too short for games filtering their coin
// switch at a high frame rate (MotoGP).
#define COIN_PULSE_MS 80

typedef struct
{
    int seen, held;
    uint64_t until;
} CoinPulse;

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int coinPulseState(CoinPulse *p, int count)
{
    uint64_t now = nowMs();

    if (p->held)
    {
        if (now < p->until)
            return 1;
        p->held = 0;
        p->until = now + COIN_PULSE_MS;
        return 0;
    }
    if (now < p->until)
        return 0;
    if (count > p->seen)
    {
        p->seen++;
        p->held = 1;
        p->until = now + COIN_PULSE_MS;
        return 1;
    }
    p->seen = count;
    return 0;
}

static int coinPulse(int slot, int count)
{
    static CoinPulse pulses[2];
    return coinPulseState(&pulses[slot], count);
}

int rtIoSwitchState(const RtIoInput *in, JVSIO *io)
{
    switch (in->type)
    {
    case RT_IO_ANALOG_SWITCH:
        return io->state.analogueChannel[in->source] > io->analogueMax / 2;
    case RT_IO_SWITCH:
        return (io->state.inputSwitch[in->player] & in->source) != 0;
    case RT_IO_COIN:
        return coinPulse(in->player, io->state.coinCount[in->player]);
    default:
        return 0;
    }
}

// A switch's presses since the last read, for readers polling once a frame:
// a press shorter than that is still seen (see JVSState.switchPresses).
int rtSwitchTaps(JVSIO *io, int player, uint32_t bit, unsigned int *last)
{
    unsigned int presses = io->state.switchPresses[player][__builtin_ctz(bit)];
    int taps = (int)(presses - *last);
    *last = presses;
    return taps;
}

// Each press a press and a release, but for the state the switch is now in.
// A press already over is held for this frame (a light gun's pump is a
// click, whatever the player does): the games act on a held switch (a
// reload once), not on its transitions alone.
void rtSwitchCount(uint32_t *count, int *wasHeld, int held, int taps, int inverted)
{
    held |= taps > 0;
    if (taps > 0)
        *count += 2 * taps + *wasHeld - held;
    *wasHeld = held;
    if ((held ^ inverted) != (int)(*count & 1))
        (*count)++;
}

// A switch of the desktop keys (their coins as counted presses).
static int desktopSwitchState(const RtIoInput *in)
{
    static CoinPulse pulses[2];
    JVSIO *desktop = desktopKeyboardState();

    if (in->type == RT_IO_SWITCH)
        return (desktop->state.inputSwitch[in->player] & in->source) != 0;
    if (in->type != RT_IO_COIN || in->player > 1)
        return 0;
    return coinPulseState(&pulses[in->player], desktop->state.coinCount[in->player]);
}

// Whether desktop keys drive this analog slot.
static int keysDriveSlot(const RtIoInput *inputs, uint16_t slot)
{
    for (const RtIoInput *in = inputs; in->type != RT_IO_END; in++)
        if (in->type == RT_IO_SWITCH_ANALOG && in->io == slot)
            return 1;
    return 0;
}

// Runs as one of the engine's I/O backends, once per frame inside InputLoop.
static int ioLoopHook(int dt)
{
    static int rangesSet;
    const RtIoInput *inputs = currentGame->ioInputs;
    JVSIO *io = ioFromDesktop ? desktopInputState() : getJVSIO();
    // With evdev input, the desktop keys of ioDesktop games still work (the
    // operator's test and service keys on a gun-only setup).
    int desktopToo = !ioFromDesktop && currentGame->ioDesktop;
    int ret = ioLoopOrig(dt);

    for (const RtIoInput *in = inputs; in->type != RT_IO_END; in++)
    {
        if (in->type == RT_IO_SWITCH_ANALOG)
        {
            // The desktop keys: in desktop mode they are the slot's only
            // input (at rest when none is held); with evdev input they
            // override the controller's axis while held.
            JVSIO *keys = ioFromDesktop ? io : desktopToo ? desktopKeyboardState() : NULL;
            uint32_t low = in->source & 0xffff, high = in->source >> 16;
            uint32_t held = keys ? keys->state.inputSwitch[in->player] & (low | high) : 0;
            if (!held && !ioFromDesktop)
                continue;
            int value = !low ? 0 : io->analogueMax / 2;
            if ((held & high) && !(held & low))
                value = io->analogueMax;
            else if ((held & low) && !(held & high))
                value = 0;
            if (ioFromDesktop && currentGame->ioRawRange && (!rangesSet || currentGame->ioRawRange == 2))
                ioSetInputRawRange(in->io, 0, io->analogueMax);
            ioAnalogSet(in->io, value);
            continue;
        }
        if (in->type == RT_IO_ANALOG || in->type == RT_IO_ANALOG_INVERTED)
        {
            // In desktop mode, slots driven by keys (RT_IO_SWITCH_ANALOG) are
            // not the mouse's.
            if (ioFromDesktop && keysDriveSlot(inputs, in->io))
                continue;
            int value = io->state.analogueChannel[in->source];
            if (currentGame->ioRawRange && (!rangesSet || currentGame->ioRawRange == 2))
                ioSetInputRawRange(in->io, 0, io->analogueMax);
            ioAnalogSet(in->io, in->type == RT_IO_ANALOG ? value : io->analogueMax - value);
            continue;
        }
        // Switches sharing an I/O slot are or-ed: handle the slot once.
        const RtIoInput *first = inputs;
        while (first->io != in->io || first->type == RT_IO_ANALOG || first->type == RT_IO_ANALOG_INVERTED ||
               first->type == RT_IO_SWITCH_ANALOG)
            first++;
        if (first != in)
            continue;
        int held = 0;
        for (const RtIoInput *other = in; other->type != RT_IO_END; other++)
            if (other->io == in->io && other->type != RT_IO_ANALOG && other->type != RT_IO_ANALOG_INVERTED &&
                other->type != RT_IO_SWITCH_ANALOG)
                held |= rtIoSwitchState(other, io) || (desktopToo && desktopSwitchState(other));
        ioDigitalSet(in->io, dt, held);
    }
    rangesSet = 1;
    if (currentGame->ioFrame)
        currentGame->ioFrame(io);
    // The engine only reads the I/O slots in frames flagged as having new
    // data: a press edge set in another frame would be lost.
    *ioNewData = 1;
    return ret;
}

// IR tracked guns: the camera manager's answers (see RtGame).
#define IR_CAMERA_WIDTH 800
#define IR_CAMERA_HEIGHT 600

// Whether this gun axis comes from an absolute evdev device. A relative
// mouse remains on the existing orientation even in INPUT_MODE 2.
static int irGunAbsoluteAxis(int gun, int y)
{
    EmulatorConfig *config = getConfig();
    const char *mapping = gun ? (y ? config->arcadeInputs.analogue_4 : config->arcadeInputs.analogue_3)
                              : (y ? config->arcadeInputs.analogue_2 : config->arcadeInputs.analogue_1);
    return !ioFromDesktop && (strstr(mapping, ":ABS:") || strstr(mapping, ":ABS_NEG:"));
}

// A gun's position from the loader's input, 0..1 from the top left; 0 off
// the screen (a light gun reports the edge of its range there, the desktop
// pointer 0).
static int irGunOnScreen(int gun, float *x, float *y)
{
    JVSIO *io = ioFromDesktop ? desktopInputState() : getJVSIO();
    int max = io->analogueMax, ax, ay;

    if (gun < 0 || gun > 1 || max <= 0)
        return 0;
    ax = io->state.analogueChannel[ANALOGUE_1 + 2 * gun];
    ay = io->state.analogueChannel[ANALOGUE_2 + 2 * gun];
    if (ax <= 0 || ax >= max || ay <= 0 || ay >= max)
        return 0;
    *x = (float)ax / max;
    *y = (float)ay / max;
    // The evdev guns aim at the screen: to the picture, when that does not
    // fill it (the desktop pointer is already the picture's).
    if (!ioFromDesktop && !frameScaleWindowToGame(x, y))
        return 0;
    if (currentGame->irGunInvertX)
        *x = 1.f - *x;
    if (currentGame->irAbsGunInvertX && irGunAbsoluteAxis(gun, 0))
        *x = 1.f - *x;
    if (currentGame->irAbsGunInvertY && irGunAbsoluteAxis(gun, 1))
        *y = 1.f - *y;
    return 1;
}

// A connected gun that never points at the screen stalls the game: the
// first gun is always connected, the second when evdev aims it (a gun or a
// mouse is mapped on ANALOGUE_3), else once it has been on the screen. The
// games check it for their "Right gun not connected!" and their calibration.
static int irGunActive(int gun)
{
    static int seen[2] = {1, -1};
    float x, y;

    if (gun < 0 || gun > 1)
        return 0;
    if (seen[1] < 0)
        seen[1] = evdevInput && getConfig()->arcadeInputs.analogue_3[0] != 0;
    if (!seen[gun] && irGunOnScreen(gun, &x, &y))
        seen[gun] = 1;
    return seen[gun];
}

static int irGunRaw(int gun, float *x, float *y)
{
    if (!x || !y)
        return -1;
    if (!irGunOnScreen(gun, x, y))
    {
        *x = *y = -1.f;
        return 0;
    }
    *x *= IR_CAMERA_WIDTH;
    *y *= IR_CAMERA_HEIGHT;
    return 0;
}

// The aim, in the gun slots' raw range (min at +0x14 of a slot, max at
// +0x18, as io_irtrack checks it: 0..1, Y up as in Halo's): the loader's
// guns and mice point where they aim already, so the game's calibration,
// which only worked for the first gun, is left out.
#define IR_GUN_SLOT 0x181
#define IR_GUN_BUTTON_SLOT_SIZE 0x2c
static int irGunAim(int gun, float *x, float *y)
{
    float px, py;

    if (!x || !y)
        return 0;
    if (!irGunOnScreen(gun, &px, &py))
    {
        *x = *y = -1.f;
        return 0;
    }
    const float *sx = ioGetInputAnalog(IR_GUN_SLOT + 2 * gun);
    const float *sy = ioGetInputAnalog(IR_GUN_SLOT + 2 * gun + 1);
    // A calibration saved before a gun was mapped can have identical min
    // and max values. The native update then treats camera coordinates as
    // normalized input, putting the aim offscreen and blocking its buttons.
    // Repair only that collapsed range, using the emulated camera's units.
    if (ioSetInputRawRange && sx[5] == sx[6])
    {
        ioSetInputRawRange(IR_GUN_SLOT + 2 * gun, 0, IR_CAMERA_WIDTH);
        log_warn("Raw Thrills: gun %d X calibration has no range; using 0..%d", gun + 1, IR_CAMERA_WIDTH);
    }
    if (ioSetInputRawRange && sy[5] == sy[6])
    {
        ioSetInputRawRange(IR_GUN_SLOT + 2 * gun + 1, 0, IR_CAMERA_HEIGHT);
        log_warn("Raw Thrills: gun %d Y calibration has no range; using 0..%d", gun + 1, IR_CAMERA_HEIGHT);
    }
    // A calibration made with reversed axes can save max before min.
    // Normalization still produces valid aim, but io_irtrack's bounds
    // checks mark every position offscreen. GUN_FOLLOW then parks the gun
    // model and beam. Sort the bounds before computing the raw position:
    // normalized X remains px and Y remains 1-py, within valid bounds.
    if (ioSetInputRawRange && sx[5] > sx[6])
    {
        ioSetInputRawRange(IR_GUN_SLOT + 2 * gun, sx[6], sx[5]);
        log_info("Raw Thrills: gun %d reversed X calibration bounds normalized", gun + 1);
    }
    if (ioSetInputRawRange && sy[5] > sy[6])
    {
        ioSetInputRawRange(IR_GUN_SLOT + 2 * gun + 1, sy[6], sy[5]);
        log_info("Raw Thrills: gun %d reversed Y calibration bounds normalized", gun + 1);
    }
    *x = sx[5] + px * (sx[6] - sx[5]);
    *y = sy[5] + (1.f - py) * (sy[6] - sy[5]);
    return 0;
}

// Whether io_irtrack reads a gun button's count inverted: bit 0 of its slot's
// flags, which the games set on their gun buttons (see RtGame).
static int irGunButtonInverted(int gun, int button)
{
    const RtGame *g = currentGame;
    const uint8_t *slots = g->irGunSlots ? *(uint8_t *const *)(uintptr_t)g->irGunSlots : NULL;

    if (!slots)
        return 0;
    slots += gun * g->irGunSlotStride + g->irGunButtonSlot + (button == 3 ? 2 : button) * IR_GUN_BUTTON_SLOT_SIZE;
    return *(const uint32_t *)slots & 1;
}

// Buttons 0 and 1 are the trigger and the pump; Aliens Armageddon's guns
// have a grenade button too, read as button 3 (button 2 is never asked for).
// A count of the button's transitions: odd while held, or even for a slot
// the game inverts. The engine reads it once a frame, and takes several
// transitions at once: the presses the evdev input counted since (a quick
// pull pressed and released between two frames) are all in it.
static int irGunButton(int gun, int button)
{
    static const int bits[] = {BUTTON_1, BUTTON_2, 0, BUTTON_3};
    static uint32_t count[2][4];
    static int wasHeld[2][4];
    static unsigned int lastPresses[2][4];
    JVSIO *io = ioFromDesktop ? desktopInputState() : getJVSIO();

    if (!irGunActive(gun) || button < 0 || button >= 4 || !bits[button])
        return 0;
    rtSwitchCount(&count[gun][button], &wasHeld[gun][button],
                  (io->state.inputSwitch[PLAYER_1 + gun] & bits[button]) != 0,
                  rtSwitchTaps(io, PLAYER_1 + gun, bits[button], &lastPresses[gun][button]),
                  irGunButtonInverted(gun, button));
    return (int)count[gun][button];
}

static void installEvdevInput(const RtGame *game)
{
    if (!game->ioInputs)
        return;
    *(void **)&ioGetInputAnalog = rtSymbol("io_get_input_analog");
    *(void **)&ioGetInputDigital = rtSymbol("io_get_input_digital");
    *(void **)&ioInputAnalogUpdate = rtSymbol("io_input_analog_update");
    *(void **)&ioSetInputRawRange = rtSymbol("io_set_input_raw_range");
    const char *ioLoopSymbol = game->ioLoopSymbol ? game->ioLoopSymbol : "io_sdl_loop";
    ioLoopOrig = rtTrampoline(ioLoopSymbol, game->ioLoopPrologue);
    ioNewData = findNewDataFlag();
    // The analog functions are only needed by games feeding an analog slot.
    int analog = 0;
    for (const RtIoInput *in = game->ioInputs; in->type != RT_IO_END; in++)
        analog |= in->type == RT_IO_ANALOG || in->type == RT_IO_ANALOG_INVERTED || in->type == RT_IO_SWITCH_ANALOG;
    if ((analog && (!ioGetInputAnalog || !ioInputAnalogUpdate || !ioSetInputRawRange)) || !ioGetInputDigital ||
        !ioLoopOrig || !ioNewData)
    {
        log_error("Raw Thrills: evdev input disabled, engine I/O functions not found");
        return;
    }
    rtDetour(ioLoopSymbol, ioLoopHook);
    evdevInput = getConfig()->inputMode == 2;
    ioFromDesktop = !evdevInput;
    if (game->irGunActiveSymbol &&
        (rtDetour(game->irGunActiveSymbol, irGunActive) != 0 || rtDetour(game->irGunRawSymbols[0], irGunRaw) != 0 ||
         rtDetour(game->irGunRawSymbols[1], irGunRaw) != 0 || rtDetour(game->irGunButtonSymbol, irGunButton) != 0))
        log_error("Raw Thrills: IR gun manager not answered, the guns stay disconnected");
    if (game->irGunAimSymbol && (!ioGetInputAnalog || rtDetour(game->irGunAimSymbol, irGunAim) != 0))
        log_error("Raw Thrills: IR gun aim not answered, the game's calibration applies");
}

// ---------------------------------------------------------------------------
// RIO API (games not built on the g5 engine): switch state and press count.

#define RIO_SWITCHES 64

static int rioHeld[RIO_SWITCHES];
static uint32_t rioCount[RIO_SWITCHES];

int rtGunOnScreen(int gun, float *x, float *y)
{
    return irGunOnScreen(gun, x, y);
}

// With evdev input, the desktop keys of rioDesktopKeys games still work
// (the operator's test and service keys on a gun-only setup).
static int desktopKeyState(const RtIoInput *in)
{
    static CoinPulse pulses[2];
    JVSIO *desktop = desktopKeyboardState();

    if (in->type == RT_IO_SWITCH)
        return (desktop->state.inputSwitch[in->player] & in->source) != 0;
    if (in->type != RT_IO_COIN || in->player > 1)
        return 0;
    return coinPulseState(&pulses[in->player], desktop->state.coinCount[in->player]);
}

static int rioSwState(int sw)
{
    int evdev = getConfig()->inputMode == 2;
    JVSIO *io = evdev ? getJVSIO() : desktopInputState();
    int held = 0;

    if (sw < 0 || sw >= RIO_SWITCHES)
        return 0;
    for (const RtIoInput *in = currentGame->rioSwitches; in->type != RT_IO_END; in++)
        if (in->io == sw)
            held |= rtIoSwitchState(in, io) | (evdev && currentGame->rioDesktopKeys && desktopKeyState(in));
    if (held != rioHeld[sw] && (held || currentGame->rioTransitionCounts))
        rioCount[sw]++;
    rioHeld[sw] = held;
    // This RIO build's native API uses 1 for pressed and 2 for released;
    // its gameplay code checks those values rather than treating state as a
    // plain boolean. Other profiles retain the 0/1 result used by their API.
    return held ? 1 : currentGame->rioTransitionCounts ? 2 : 0;
}

static uint32_t rioSwCount(int sw)
{
    if (currentGame->rioTransitionCounts && sw >= 0 && sw < RIO_SWITCHES)
        rioSwState(sw);
    return sw >= 0 && sw < RIO_SWITCHES ? rioCount[sw] : 0;
}

// The board's switch reports: a transition count per switch (odd: held).
static int (*rioLoopOrig)(int dt);
static int (*rioEvent)(int sw, int type, uint32_t count, uint32_t time);
static uint32_t rioTransitions[RIO_SWITCHES];

static int rioLoopHook(int dt)
{
    int ret = rioLoopOrig(dt), changed = 0;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    for (const RtIoInput *in = currentGame->rioSwitches; in->type != RT_IO_END; in++)
    {
        int sw = in->io;
        const RtIoInput *first = currentGame->rioSwitches;
        while (first->io != sw)
            first++;
        if (first != in || sw < 0 || sw >= RIO_SWITCHES)
            continue;
        int held = rioSwState(sw) == 1;
        if (held != (int)(rioTransitions[sw] & 1))
        {
            rioTransitions[sw]++;
            rioEvent(sw, 3, rioTransitions[sw], (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
            changed = 1;
        }
    }
    return ret || changed;
}

// The board's analog inputs (RIO_ADC_Val): 12-bit values. A channel moves
// to where its analog input is when that input moves (evdev's axis, or the
// desktop mouse, which reads 0 when it is not over the window: a position
// it has not left is no move). The switches move it while held, over the
// analog input (the desktop arrows, and the controller's with evdev input),
// as a hand would: a step at the press, as far as a menu item of Doodle
// Jump's operator menu (65 per item), then faster and faster, to twice the
// range a second (RIO_ADC_RAMP_MS: a tap moves one item, a hold sweeps it
// in under a second).
#define RIO_ADC_MAX 4095
#define RIO_ADC_STEP 66
#define RIO_ADC_RAMP_MS 500
#define RIO_ADCS 32

static int rioAdcVal(int adc)
{
    static int value[RIO_ADCS], lastInput[RIO_ADCS], seen[RIO_ADCS], direction[RIO_ADCS];
    static uint64_t lastTime[RIO_ADCS], heldSince[RIO_ADCS];
    int evdev = getConfig()->inputMode == 2;
    JVSIO *io = evdev ? getJVSIO() : desktopInputState();
    uint64_t now = nowMs();

    if (adc < 0 || adc >= RIO_ADCS)
        return 0;
    if (!seen[adc])
    {
        value[adc] = RIO_ADC_MAX / 2;
        lastTime[adc] = now;
        seen[adc] = 1;
    }
    int dt = (int)(now - lastTime[adc]), keys = 0;
    lastTime[adc] = now;
    for (const RtIoInput *in = currentGame->rioAnalogs; in->type != RT_IO_END && !keys; in++)
    {
        if (in->io != adc || in->type != RT_IO_SWITCH_ANALOG)
            continue;
        uint32_t low = in->source & 0xffff, high = in->source >> 16;
        uint32_t held = io->state.inputSwitch[in->player];
        if (evdev && currentGame->rioDesktopKeys)
            held |= desktopKeyboardState()->state.inputSwitch[in->player];
        int dir = (held & low) && !(held & high) ? -1 : (held & high) && !(held & low) ? 1 : 0;
        if (!dir)
            continue;
        keys = 1;
        if (dir != direction[adc])
        {
            direction[adc] = dir;
            heldSince[adc] = now;
            value[adc] += dir * RIO_ADC_STEP;
            continue;
        }
        // The speed grows with the square of the time held, to twice the
        // range a second at RIO_ADC_RAMP_MS.
        double t = (double)(now - heldSince[adc]) / RIO_ADC_RAMP_MS;
        double speed = RIO_ADC_MAX / 500.0 * (t < 1 ? t * t : 1);
        value[adc] += dir * (int)(speed * dt + 0.5);
    }
    if (!keys)
        direction[adc] = 0;
    for (const RtIoInput *in = currentGame->rioAnalogs; in->type != RT_IO_END; in++)
    {
        if (in->io != adc || in->type != RT_IO_ANALOG || io->analogueMax <= 0)
            continue;
        int input = io->state.analogueChannel[in->source];
        if ((!evdev && input <= 0) || input == lastInput[adc])
            continue;
        // While a switch is held, the analog input's position is no move.
        if (!keys)
            value[adc] = (int)((double)input * RIO_ADC_MAX / io->analogueMax);
        lastInput[adc] = input;
    }
    value[adc] = value[adc] < 0 ? 0 : value[adc] > RIO_ADC_MAX ? RIO_ADC_MAX : value[adc];
    return value[adc];
}

// ---------------------------------------------------------------------------
// UMC board (mounted guns): pots and switches from the loader's input
// (evdev, or the desktop mouse), in its report.

#define UMC_SWITCHES 8
#define UMC_ANALOGS 8
#define UMC_MAX 1023

static JVSIO *inputIo(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
}

static int umcConnected(void)
{
    return 0;
}

static int umcRead(int *report)
{
    static uint32_t transitions[UMC_SWITCHES];
    static int pots[UMC_ANALOGS];
    JVSIO *io = inputIo();
    int max = io->analogueMax;

    for (int g = 0; g < currentGame->umcGunCount; g++)
    {
        const RtUmcGun *gun = &currentGame->umcGuns[g];
        int ax = io->state.analogueChannel[gun->xChannel], ay = io->state.analogueChannel[gun->yChannel];
        int buttons = io->state.inputSwitch[gun->player];

        // Off the screen: the gun stays where it was.
        if (max > 0 && ax > 0 && ax < max && ay > 0 && ay < max)
        {
            pots[gun->umcX] = (int)((double)ax * UMC_MAX / max);
            pots[gun->umcY] = (int)((double)(max - ay) * UMC_MAX / max);
        }
        pots[gun->umcLever] = buttons & BUTTON_2 ? UMC_MAX : 0;
        int held = (buttons & BUTTON_1) != 0;
        if (held != (int)(transitions[gun->umcTrigger] & 1))
            transitions[gun->umcTrigger] = (transitions[gun->umcTrigger] + 1) & 0xff;
    }
    for (int i = 0; i < UMC_SWITCHES; i++)
        report[i] = (int)transitions[i];
    for (int i = 0; i < UMC_ANALOGS; i++)
        report[UMC_SWITCHES + i] = pots[i];
    return 0;
}

static void installUmc(const RtGame *game)
{
    currentGame = game;
    if (rtDetour(game->umcConnectedSymbol, umcConnected) != 0 || rtDetour(game->umcReadSymbol, umcRead) != 0 ||
        (game->umcStatusSymbol && rtDetour(game->umcStatusSymbol, umcConnected) != 0))
        log_error("Raw Thrills: UMC board hooks not installed");
}

static void installRioInput(const RtGame *game)
{
    currentGame = game;
    rtDetour("RIO_SW_State", rioSwState);
    rtDetour("RIO_SW_Count", rioSwCount);
    if (game->rioAnalogs)
        rtDetour("RIO_ADC_Val", rioAdcVal);
    if (!game->rioEventSymbol || !game->ioLoopSymbol)
        return;
    *(void **)&rioEvent = rtSymbol(game->rioEventSymbol);
    rioLoopOrig = rtTrampoline(game->ioLoopSymbol, game->ioLoopPrologue);
    if (!rioEvent || !rioLoopOrig || rtDetour(game->ioLoopSymbol, rioLoopHook) != 0)
        log_error("Raw Thrills: RIO switch reports not installed");
}

// ---------------------------------------------------------------------------

void rtInstallInput(const RtGame *game)
{
    if (game->rioSwitches)
    {
        if (getConfig()->inputMode == 2 || game->rioDesktopKeys)
            installRioInput(game);
        if (game->umcReadSymbol)
            installUmc(game);
        return;
    }

    const char *gameInputMapsSymbol = game->gameInputMapsSymbol ? game->gameInputMapsSymbol : "GameInputMaps";

    if (game->inputAddMapPrologue)
    {
        // The evdev input decides which maps are dropped and added: set up
        // first, the maps are registered when main runs.
        currentGame = game;
        if (getConfig()->inputMode == 2 || game->ioDesktop)
            installEvdevInput(game);
        inputAddMapOrig = rtTrampoline("InputAddMap", game->inputAddMapPrologue);
        inputAddMap = inputAddMapOrig;
        if (!inputAddMapOrig || rtDetour("InputAddMap", inputAddMapHook) != 0)
            log_error("Raw Thrills: input hooks not installed");
        return;
    }
    // Games whose input map needs nothing added or dropped (Pink Panther
    // Jewel Heist): only their slots are fed.
    if (!game->gameInputMapsPrologue)
    {
        currentGame = game;
        if (getConfig()->inputMode == 2 || game->ioDesktop)
            installEvdevInput(game);
        return;
    }
    currentGame = game;
    *(void **)&inputAddMap = rtSymbol("InputAddMap");
    gameInputMapsOrig = rtTrampoline(gameInputMapsSymbol, game->gameInputMapsPrologue);
    if (!inputAddMap || !gameInputMapsOrig)
    {
        log_error("Raw Thrills: input hooks not installed");
        return;
    }
    rtDetour(gameInputMapsSymbol, gameInputMaps);
    if (getConfig()->inputMode == 2 || game->ioDesktop)
        installEvdevInput(game);
}
