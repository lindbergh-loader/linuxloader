// A trackball's motion (see trackball.h).

#include <SDL3/SDL.h>

#include "trackball.h"

static int motionX, motionY;
static unsigned int buttons;
static int captureRequested, captureApplied;

void trackballAdd(int dx, int dy)
{
    __atomic_add_fetch(&motionX, dx, __ATOMIC_RELAXED);
    __atomic_add_fetch(&motionY, dy, __ATOMIC_RELAXED);
}

void trackballTake(int *dx, int *dy)
{
    *dx = __atomic_exchange_n(&motionX, 0, __ATOMIC_RELAXED);
    *dy = __atomic_exchange_n(&motionY, 0, __ATOMIC_RELAXED);
}

void trackballSetButton(int button, int held)
{
    if (button < 1 || button > 32)
        return;
    if (held)
        __atomic_or_fetch(&buttons, 1u << (button - 1), __ATOMIC_RELAXED);
    else
        __atomic_and_fetch(&buttons, ~(1u << (button - 1)), __ATOMIC_RELAXED);
}

unsigned int trackballButtons(void)
{
    return __atomic_load_n(&buttons, __ATOMIC_RELAXED);
}

void trackballRequestCapture(void)
{
    __atomic_store_n(&captureRequested, 1, __ATOMIC_RELAXED);
}

void trackballApplyCapture(void *sdlWindow)
{
    if (!sdlWindow || captureApplied || !__atomic_load_n(&captureRequested, __ATOMIC_RELAXED))
        return;
    captureApplied = SDL_SetWindowRelativeMouseMode((SDL_Window *)sdlWindow, true);
}
