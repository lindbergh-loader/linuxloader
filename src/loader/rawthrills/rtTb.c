// Tippin' Bloks (ICE, PlayMechanix g3): its controller, a handle that moves
// and tilts the block.
//
// The game reads the handle from a USB board of its own (0483:0003,
// libusb-0.1, the "uioai" module: 8-byte reports, a byte per axis), or from
// the RIO board's analog channels 0x13 (move) and 0x14 (tilt) once the RIO
// is connected, which it is here (RIO_Connected stubbed): a sampler thread
// reads them every millisecond (0x8162ac0). Both reads are answered with the
// loader's input, worked out on the game's main thread each frame
// (RIO_SampleInput, as the switches are: the desktop keyboard's X connection
// is not shared between threads).
//
// The move, a position the handle keeps: the mouse's horizontal position,
// the gamepad's left stick, or a stick on ANALOGUE_1 (evdev input), each
// taking it where it is whenever it moves; the left and right arrows (the
// d-pad, P1 LEFT/RIGHT) push it, a step at a tap, faster while held.
// The tilt, back in the middle when let go: z and x (P1 BUTTON_2/BUTTON_3),
// the up and down arrows (the d-pad, P1 UP/DOWN) or the mouse's left and
// right buttons tilt it all the way (its middle
// button is Start); else the
// gamepad's right stick, L2 and R2 (left, right), or a stick on ANALOGUE_3.

#include <stdint.h>
#include <time.h>

#include <SDL3/SDL_gamepad.h>

#include "rawthrills.h"
#include "../common/desktopInput.h"
#include "rtGame.h"
#include "../config/config.h"
#include "../input/evdevInput.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define TB_AXIS_MAX 4095
#define TB_AXIS_MID (TB_AXIS_MAX / 2)
// The keys' push: a step at the press, then faster and faster, to the whole
// range in a second (TB_KEY_RAMP_MS: a tap moves a little, a hold sweeps).
#define TB_KEY_STEP 48
#define TB_KEY_RAMP_MS 400
// How far an absolute input (mouse, stick) moves before it takes the handle.
#define TB_FOLLOW_MIN 16

extern Controllers controllers;

static volatile int axisMove = TB_AXIS_MID, axisTilt = TB_AXIS_MID;
static int moveMapped, tiltMapped;
static int (*rioSampleInputOrig)(void);

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int clampAxis(int v)
{
    return v < 0 ? 0 : v > TB_AXIS_MAX ? TB_AXIS_MAX : v;
}

// A stick's axis (-32768..32767) on the handle's range.
static int stickAxis(int v)
{
    return clampAxis(TB_AXIS_MID + v * (TB_AXIS_MID + 1) / 32768);
}

// An absolute input takes the handle when it has moved since it last did
// (last: its previous reading, -1 at first).
static int follows(int value, int *last)
{
    int moved = *last >= 0 && (value - *last > TB_FOLLOW_MIN || *last - value > TB_FOLLOW_MIN);
    if (*last < 0 || moved)
        *last = value;
    return moved;
}

static void updateMove(JVSIO *io, uint32_t held)
{
    static int lastMouse = -1, lastPad = -1, lastStick = -1, direction;
    static uint64_t lastTime, heldSince;
    uint64_t now = nowMs();
    int dt = lastTime ? (int)(now - lastTime) : 0, pos = axisMove;
    lastTime = now;

    // The mouse over the picture (ANALOGUE_1 of the desktop state, 0 outside).
    JVSIO *desktop = desktopInputState();
    int mouse = desktop->state.analogueChannel[ANALOGUE_1];
    if (mouse > 0 && desktop->analogueMax > 0 &&
        follows((int)((double)mouse * TB_AXIS_MAX / desktop->analogueMax), &lastMouse))
        pos = lastMouse;
    if (follows(stickAxis(desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX)), &lastPad))
        pos = lastPad;
    if (moveMapped && io->analogueMax > 0 &&
        follows((int)((double)io->state.analogueChannel[ANALOGUE_1] * TB_AXIS_MAX / io->analogueMax), &lastStick))
        pos = lastStick;

    int dir = (held & BUTTON_LEFT) && !(held & BUTTON_RIGHT) ? -1 : (held & BUTTON_RIGHT) && !(held & BUTTON_LEFT) ? 1 : 0;
    if (dir && dir != direction)
    {
        heldSince = now;
        pos += dir * TB_KEY_STEP;
    }
    else if (dir)
    {
        // The speed grows with the square of the time held, to the whole
        // range a second at TB_KEY_RAMP_MS.
        double t = (double)(now - heldSince) / TB_KEY_RAMP_MS;
        double speed = TB_AXIS_MAX / 1000.0 * (t < 1 ? t * t : 1);
        pos += dir * (int)(speed * dt + 0.5);
    }
    direction = dir;
    axisMove = clampAxis(pos);
}

static void updateTilt(JVSIO *io, uint32_t held)
{
    unsigned int mouse = desktopPointerButtons();
    // The keys, not the mouse's right button the desktop state also gives
    // as BUTTON_2.
    int left = ((held & BUTTON_2) && !(mouse & DESKTOP_POINTER_RIGHT)) || (held & BUTTON_UP) ||
               (mouse & DESKTOP_POINTER_LEFT);
    int right = (held & BUTTON_3) || (held & BUTTON_DOWN) || (mouse & DESKTOP_POINTER_RIGHT);

    if (left && !right)
        axisTilt = 0;
    else if (right && !left)
        axisTilt = TB_AXIS_MAX;
    else
    {
        int tilt = stickAxis(desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHTX)) - TB_AXIS_MID;
        tilt += (desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) - desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) *
                (TB_AXIS_MID + 1) / 32768;
        if (tiltMapped && io->analogueMax > 0)
            tilt += (int)((double)io->state.analogueChannel[ANALOGUE_3] * TB_AXIS_MAX / io->analogueMax) - TB_AXIS_MID;
        axisTilt = clampAxis(TB_AXIS_MID + tilt);
    }
}

// The handle's calibration (0x8707520, set by the operator menu's
// calibration, 0x8162a30, and saved in blocksuser/aud/calibration.*.aud):
// the tilt's left end, right end, the move's centre, left end, right end,
// then the tilt's centre. Its calibration screen records each one on a
// press, which the keys cannot hold at the same time: it is the loader's
// range each frame (every input is brought to it), whatever was saved.
static void loaderCalibration(void)
{
    volatile float *cal = (volatile float *)0x08707520;

    cal[0] = 0;
    cal[1] = TB_AXIS_MAX;
    cal[2] = TB_AXIS_MID;
    cal[3] = 0;
    cal[4] = TB_AXIS_MAX;
    cal[5] = TB_AXIS_MID;
}

static int sampleInput(void)
{
    int evdev = getConfig()->inputMode == 2;
    JVSIO *io = evdev ? getJVSIO() : desktopInputState();
    uint32_t held = io->state.inputSwitch[PLAYER_1];

    // With evdev input, the desktop keys and the gamepad too (as the
    // switches, rioDesktopKeys).
    if (evdev)
        held |= desktopInputState()->state.inputSwitch[PLAYER_1];
    loaderCalibration();
    updateMove(io, held);
    updateTilt(io, held);
    return rioSampleInputOrig ? rioSampleInputOrig() : 0;
}

// The uioai board: connected, no reader thread.
static int uioaiInit(void)
{
    *(int32_t *)0x08706da4 = 1;
    return 0;
}

// uioai's read (move, tilt).
static int uioaiRead(int *move, int *tilt)
{
    if (move)
        *move = axisMove;
    if (tilt)
        *tilt = axisTilt;
    return 0;
}

// The RIO analog channels' read (0x13, 0x14, 0x15, 0x16): the move, the tilt.
static int rioAnalogRead(int *move, int *tilt, int *c, int *d)
{
    if (move)
        *move = axisMove;
    if (tilt)
        *tilt = axisTilt;
    if (c)
        *c = 0;
    if (d)
        *d = 0;
    return 0;
}

void rtTbInstall(const RtGame *game)
{
    (void)game;
    moveMapped = getConfig()->inputMode == 2 && evdevAxisController(&controllers, "ANALOGUE_1");
    tiltMapped = getConfig()->inputMode == 2 && evdevAxisController(&controllers, "ANALOGUE_3");
    desktopGamepadEnable();
    desktopPointerMiddle(BUTTON_START);
    rioSampleInputOrig = rtTrampoline("RIO_SampleInput", 6);
    if (rtDetour("RIO_SampleInput", sampleInput) != 0 || rtDetour("uioai_init", uioaiInit) != 0 ||
        rtDetour("uioai_read", uioaiRead) != 0 || rtDetour("rio_analog_read", rioAnalogRead) != 0)
        log_warn("Raw Thrills: Tippin' Bloks' controller not installed");
}
