// Speed Driver 3's serial cabinet input at GameIO and motor output at VR.
// Keep the game's VR processing, switch edges and calibration, supplying the same
// three raw ADC readings and sixteen switches as GameIO::read.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_gamepad.h>

#include "igs.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../input/evdevFfb.h"
#include "../patching/flowControl.h"

typedef struct
{
    uint32_t steering, brake, throttle;
    uint32_t switches[2][8];
} Sd3Inputs;
_Static_assert(sizeof(Sd3Inputs) == 0x4c, "SD3 GameIO input layout");

static unsigned int steerMin = 20, steerCenter = 500, steerMax = 980;
static int lastCoins;
static int gearLever, gear = 1, lastGearButtons;
static int ffbOpen = -1, ffbTrace;

__attribute__((force_align_arg_pointer)) static int ioReady(void *self)
{
    (void)self;
    return 1;
}

__attribute__((destructor)) static void stopMotor(void)
{
    if (ffbOpen > 0)
        evdevFfbConstant(0);
}

// OutputJob::execute converts the physics force to a direction byte (1 for
// negative) and an unsigned magnitude. GameIO::setMotor saturates the latter
// to 127 before packing it into the serial command. The game's FFB manager
// already mixes steering, conditional forces and transient road/boost effects.
// Hook VR::setFFB, before it replaces the command with zeros when the dump's
// config.io.enableFFB is false, rather than the lower GameIO::setMotor.
__attribute__((force_align_arg_pointer)) static int motor(void *self, const void *value)
{
    const uint8_t *command = value;
    unsigned int strength = command[1] > 127 ? 127 : command[1];
    float force = (command[0] ? -1.f : 1.f) * strength / 127.f;
    static int last = -1;
    int packed = (command[0] << 8) | command[1];

    (void)self;
    if (ffbTrace && packed != last)
    {
        printf("IGS SD3 FFB: direction %u magnitude %u force %.3f\n", command[0], command[1], force);
        fflush(stdout);
        last = packed;
    }
    if (ffbOpen < 0)
        ffbOpen = evdevFfbOpen("IGS SD3");
    if (ffbOpen)
        evdevFfbConstant(force);
    return 1;
}

__attribute__((force_align_arg_pointer)) static int setBound(void *self, unsigned short low,
                                                           unsigned short center, unsigned short high)
{
    (void)self;
    if (low < center && center < high)
    {
        steerMin = low;
        steerCenter = center;
        steerMax = high;
    }
    return 1;
}

static double clamp(double value, double low, double high)
{
    return value < low ? low : value > high ? high : value;
}

__attribute__((force_align_arg_pointer)) static int readInputs(Sd3Inputs *out)
{
    int evdev = getConfig()->inputMode == 2;
    JVSIO *io = evdev ? getJVSIO() : desktopKeyboardState();
    unsigned int player = io->state.inputSwitch[PLAYER_1];
    double turn = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;
    double throttle = desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0;
    double brake = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0;
    if (evdev && io->analogueMax > 0)
    {
        turn = io->state.analogueChannel[ANALOGUE_1] * 2.0 / io->analogueMax - 1;
        throttle = io->state.analogueChannel[ANALOGUE_2] / (double)io->analogueMax;
        brake = io->state.analogueChannel[ANALOGUE_3] / (double)io->analogueMax;
    }
    if (player & BUTTON_LEFT) turn = -1;
    if (player & BUTTON_RIGHT) turn = 1;
    if (player & BUTTON_UP) throttle = 1;
    if (player & BUTTON_DOWN) brake = 1;
    turn = clamp(turn, -1, 1);
    // SD3's calibrated VR path expects the ADC to increase to the right.
    memset(out, 0, sizeof(*out));
    out->steering = steerCenter + turn * (turn < 0 ? steerCenter - steerMin : steerMax - steerCenter);
    out->throttle = 68 + clamp(throttle, 0, 1) * (800 - 68);
    out->brake = 42 + clamp(brake, 0, 1) * (370 - 42);
    out->switches[0][0] = !!(player & BUTTON_START);
    out->switches[0][1] = !!(player & BUTTON_2); // view
    out->switches[0][2] = !!(player & BUTTON_1); // boost
    if (gearLever)
    {
        // G4, G3 here, G1, G2 with the arrows; one held at a time
        int buttons = player & (BUTTON_4 | BUTTON_5);
        if (buttons & ~lastGearButtons & BUTTON_4 && gear < 4)
            gear++;
        if (buttons & ~lastGearButtons & BUTTON_5 && gear > 1)
            gear--;
        lastGearButtons = buttons;
        out->switches[0][3] = gear == 4;
        out->switches[0][4] = gear == 3;
    }
    else
    {
        out->switches[0][3] = !!(player & BUTTON_4); // gear up
        out->switches[0][4] = !!(player & BUTTON_5); // gear down
    }
    out->switches[0][5] = io->state.coinCount[0] != lastCoins;
    lastCoins = io->state.coinCount[0];
    out->switches[0][6] = !!(io->state.inputSwitch[SYSTEM] & BUTTON_TEST);
    out->switches[0][7] = !!(player & BUTTON_SERVICE);
    // Cabinet arrow buttons (menus), independent of the wheel and pedals.
    out->switches[1][0] = !!(player & BUTTON_LEFT);
    out->switches[1][2] = !!(player & BUTTON_RIGHT);
    out->switches[1][3] = !!(player & BUTTON_UP);
    out->switches[1][4] = !!(player & BUTTON_DOWN);
    if (gearLever)
    {
        out->switches[1][5] = gear == 1;
        out->switches[1][6] = gear == 2;
    }
    return 1;
}

void igsSd3InputInstall(const IgsGame *game)
{
    if (!game->serialIo[0])
        return;
    ffbTrace = getenv("IGS_FFB_TRACE") != NULL;
    gearLever = game->serialIoGearLever;
    desktopGamepadEnable();
    detourFunction(game->serialIo[0], (void *)ioReady);
    detourFunction(game->serialIo[1], (void *)readInputs);
    detourFunction(game->serialIo[2], (void *)motor);
    detourFunction(game->serialIo[3], (void *)setBound);
}
