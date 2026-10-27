// The Namco steering wheels' force feedback (WMMT3, Dead Heat, Maximum Heat
// 3D, Dead Heat Riders), as FFB Arcade Plugin gives it (WMMT3.cpp,
// DeadHeat.cpp, DeadHeatRiders.cpp).
//
// The game's clKickback object holds the values its setters were given
// (setSpring, setViosity, setReflect, setCenterOffset; exec() also turns the
// reflect over for setVibrate): read there every 8 ms. Not in the board's
// command, which exec() builds from them: it scales them by the board's
// power-on ramp, 0 for a game that has no board (Dead Heat Riders' send()
// is a stub). Then:
// - the spring is the wheel's centering spring, value / springRange (see
//   namcoFfb.h);
// - the viscosity a damper (the plugin's friction buzzes on a direct drive
//   wheel), value / viscosityRange;
// - the reflect (signed) a force pulling left when positive, the centre
//   offset (signed) one pulling right when positive, each / reflectRange
//   (63 in the plugin, see namcoFfb.h). The plugin
//   plays the one it sees last; here they add up.
// They go to the evdev device (input/evdevFfb.c): a pad rumbles with the
// force.
//
// NAMCO_FFB_TRACE set: the values on stderr as they change (with no device
// too).

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "namcoFfb.h"
#include "../input/evdevFfb.h"
#include "../input/evdevInput.h"
#include "../log/log.h"

#define PERIOD_NS 8000000L

static const NamcoFfb *ffbGame;
static void *const *ffbInstance;
static int trace;

static float unit(int value, int range)
{
    float f = (float)value / range;
    return f > 1.0f ? 1.0f : f < -1.0f ? -1.0f : f;
}

static void update(void)
{
    const volatile uint8_t *board = *ffbInstance;

    // Not created yet.
    if (!board)
        return;
    const volatile uint8_t *effects = board + ffbGame->effectsField;
    int spring = effects[0], viscosity = effects[1], reflect = (int8_t)effects[2];
    int centerOffset = *(const volatile int32_t *)(board + ffbGame->centerOffsetField);

    if (trace)
    {
        static int last[4] = {-1, -1, -1, -1};
        if (spring != last[0] || viscosity != last[1] || reflect != last[2] || centerOffset != last[3])
            fprintf(stderr, "Namco FFB: spring %d viscosity %d reflect %d centre offset %d\n", spring, viscosity,
                    reflect, centerOffset);
        last[0] = spring, last[1] = viscosity, last[2] = reflect, last[3] = centerOffset;
    }
    evdevFfbSpring(unit(spring, ffbGame->springRange));
    evdevFfbDamper(unit(viscosity, ffbGame->viscosityRange));
    evdevFfbConstant(unit(reflect, ffbGame->reflectRange) - unit(centerOffset, ffbGame->reflectRange));
}

static void *ffbThread(void *arg)
{
    (void)arg;
    struct timespec period = {0, PERIOD_NS};
    for (;;)
    {
        update();
        nanosleep(&period, NULL);
    }
    return NULL;
}

void namcoFfbStart(const NamcoFfb *ffb, void *const *instance, const char *who)
{
    pthread_t thread;

    trace = getenv("NAMCO_FFB_TRACE") != NULL;
    if (!instance || (!evdevFfbOpen(who) && !trace))
        return;
    ffbGame = ffb;
    ffbInstance = instance;
    if (createQuietThread(&thread, ffbThread, NULL) != 0)
    {
        log_error("%s: force feedback thread not started", who);
        return;
    }
    pthread_detach(thread);
    log_info("%s: steering force feedback from clKickback", who);
}
