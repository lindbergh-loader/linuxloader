// Force feedback of the driving games (Cruis'n Blast).
//
// The cabinet's wheel has a motor, driven on the parallel port by the game's
// Wheel_Set(force): force from -1 to 1, positive turning the wheel left. It
// is hooked, as FFB Arcade Plugin does: in a race the game's force is taken
// and its wheel effects (bumps, crashes, off-road) are turned on; in the
// menus the force is the one the menus ask for (the centering), made
// lighter: at full strength the wheel is hard to turn to pick a track.
//
// The force goes to the evdev force feedback device (input/evdevFfb.c): the
// wheel steering, as a constant force; a pad rumbles.

#include <stdint.h>

#include "rawthrills.h"
#include "../input/evdevFfb.h"
#include "../log/log.h"

static const RtGame *ffbGame;

// Share of the menus' centering force kept.
#define FFB_MENU_SCALE 0.25f

// Wheel_Set(force): 0, the motor answering nothing.
static int wheelSet(float force)
{
    static int inRace;
    int nowInRace = *(volatile int32_t *)(uintptr_t)ffbGame->wheelInRace == 1;

    if (nowInRace != inRace)
    {
        *(volatile uint8_t *)(uintptr_t)ffbGame->wheelEffectsFlag = nowInRace;
        inRace = nowInRace;
        force = 0;
    }
    else if (!inRace)
        force = *(volatile float *)(uintptr_t)ffbGame->wheelMenuForce * FFB_MENU_SCALE;
    evdevFfbConstant(force);
    return 0;
}

void rtInstallFfb(const RtGame *game)
{
    if (!game->wheelSetSymbol)
        return;
    ffbGame = game;
    evdevFfbOpen("Raw Thrills");
    if (rtDetour(game->wheelSetSymbol, wheelSet) != 0)
        log_error("Raw Thrills: wheel force feedback not installed");
}
