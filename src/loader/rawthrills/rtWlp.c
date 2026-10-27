// World's Largest Pac-Man uses SDL_GetWindowSurface only to create an
// offscreen software renderer. Give it a separate surface: querying the
// OpenGL window's surface can replace its GLX drawable (native SDL2/NVIDIA)
// or change its current context (sdl2-compat).

#include <dlfcn.h>
#include <stdint.h>

#include "rawthrills.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../log/log.h"

static void *(*getWindowSurfaceReal)(void *window);
static uint32_t (*getWindowFlagsReal)(void *window);
static void (*getWindowSizeReal)(void *window, int *width, int *height);
static void *(*createRGBSurfaceReal)(uint32_t flags, int width, int height, int depth,
                                   uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha);
static void (*freeSurfaceReal)(void *surface);
static int (*pollEventReal)(void *event);
static void *softwareSurface, *surfaceWindow;
static int surfaceWidth, surfaceHeight;

// WLP has keyboard shortcuts that conflict with desktop cabinet controls.
// Its own SDL keyboard API is blocked; desktopInputState() reads the keys
// independently and continues to provide the cabinet mappings.
static int pollEventWithoutKeyboard(void *event)
{
    if (!pollEventReal)
        return 0;
    while (pollEventReal(event))
    {
        uint32_t type = event ? *(const uint32_t *)event : 0;
        if (type >= 0x300 && type <= 0x304) // SDL_KEYDOWN through SDL_KEYMAPCHANGED
            continue;
        return 1;
    }
    return 0;
}

static const uint8_t *blockedKeyboardState(int *numkeys)
{
    static const uint8_t noKeys[512]; // SDL_NUM_SCANCODES
    if (numkeys)
        *numkeys = sizeof(noKeys);
    return noKeys;
}

static void *getWindowSurface(void *window)
{
    // SDL2's SDL_WINDOW_OPENGL flag. Ordinary software windows retain SDL's
    // own surface handling; Pac-Man has one OpenGL window.
    if (!window || !(getWindowFlagsReal(window) & 0x00000002))
        return getWindowSurfaceReal(window);

    int width, height;
    getWindowSizeReal(window, &width, &height);
    if (!softwareSurface || surfaceWindow != window || surfaceWidth != width || surfaceHeight != height)
    {
        // RGB888 with a 32-bit pixel stride, as for the cabinet's window
        // surface. The game keeps ownership with SDL, so cache it here.
        void *surface = createRGBSurfaceReal(0, width, height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0);
        if (!surface)
        {
            log_error("World's Largest Pac-Man: cannot create %dx%d software surface", width, height);
            return NULL;
        }
        if (softwareSurface)
            freeSurfaceReal(softwareSurface);
        softwareSurface = surface;
        surfaceWindow = window;
        surfaceWidth = width;
        surfaceHeight = height;
    }
    return softwareSurface;
}

void rtWlpInstall(const RtGame *game)
{
    if (getConfig()->keepAspectRatio)
        frameScaleSetAspect(7, 9);
    // Each gamepad drives its own player: d-pad steers, Start starts, Back
    // inserts that player's coin, and L3 is service for player 1.
    desktopGamepadEnableTwoPlayers();
    desktopKeyboardEnableTwoPlayers();
    void *lib = dlopen(game->replacedLib, RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return;
    getWindowSurfaceReal = dlsym(lib, "SDL_GetWindowSurface");
    getWindowFlagsReal = dlsym(lib, "SDL_GetWindowFlags");
    getWindowSizeReal = dlsym(lib, "SDL_GetWindowSize");
    createRGBSurfaceReal = dlsym(lib, "SDL_CreateRGBSurface");
    freeSurfaceReal = dlsym(lib, "SDL_FreeSurface");
    *(void **)&pollEventReal = dlsym(lib, "SDL_PollEvent");
    if (pollEventReal)
    {
        for (size_t i = 0; i < game->gameImportCount; i++)
        {
            const RtGameImport *imp = &game->gameImports[i];
            if (!strcmp(imp->name, "SDL_PollEvent"))
                *(uint32_t *)(uintptr_t)imp->slot = (uint32_t)(uintptr_t)pollEventWithoutKeyboard;
            else if (!strcmp(imp->name, "SDL_GetKeyboardState"))
                *(uint32_t *)(uintptr_t)imp->slot = (uint32_t)(uintptr_t)blockedKeyboardState;
        }
        rtDetour("SDL_PollEvent", pollEventWithoutKeyboard);
        rtDetour("SDL_GetKeyboardState", blockedKeyboardState);
    }
    else
        log_warn("World's Largest Pac-Man: cannot block native keyboard events");
    if (getWindowSurfaceReal && getWindowFlagsReal && getWindowSizeReal && createRGBSurfaceReal && freeSurfaceReal)
        rtDetour("SDL_GetWindowSurface", getWindowSurface);
}
