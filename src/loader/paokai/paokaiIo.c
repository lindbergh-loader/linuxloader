// The Paokai games' I/O card (see paokaiGame.h), stood in for at the game's port
// reads (ubKeyIoRead). Its timer reads ports 0, 1 and 2 in turn: port 0's
// read gives the loader's input state, latched for the other two, and the
// trackball's motion since the last one, as one PS/2 packet.
//
// The switches (set bits pressed), as the game's key and coin tables have
// them, and the controls the loader gives them:
//   port 0: bit 0 coin B (6), bit 5 setup (F2)
//   port 1: bit 0 key-in credit (F1 + 5), bit 3 start (1, Space, a mouse
//           button, a gamepad's Start or A), bit 5 record, the bookkeeping
//           (F1)
//   port 2: bit 5 coin A (5), bit 6 ERR_RESET, which clears errors (z)
// A coin is held for COIN_TICKS reads (the game counts one held 15 to 200),
// then released as long. The outputs (lamps, the hopper's motor, the
// meters) are written to /dev/null.
//
// The trackball: the motion added up from evdev's mice (evdevInput.c) or,
// on the desktop, from the mouse over the game's window (the pointer kept
// away from its edges), the arrows or a gamepad's left stick.

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL_gamepad.h>

#include "paokai.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../input/trackball.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

#define P0_COIN_B 0x01
#define P0_SETUP 0x20
#define P1_KEY_IN 0x01
#define P1_START 0x08
#define P1_RECORD 0x20
#define P2_COIN_A 0x20
#define P2_ERR_RESET 0x40

#define COIN_TICKS 40
// The arrows and the stick at full tilt roll the trackball this many counts
// a read.
#define KEY_SPEED 6
#define STICK_SPEED 12
#define STICK_DEADZONE 0.15
// A PS/2 packet's motion.
#define PACKET_MAX 255

static const PaokaiGame *game;
static uint8_t ports[3];

static JVSIO *inputState(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
}

// A coin slot's pulses: the coins counted and not yet given, the reads left
// in the current pulse (held, then released).
typedef struct
{
    int counted, pending, ticks;
} Coin;

static int coinTick(Coin *c, int count)
{
    if (count > c->counted)
        c->pending += count - c->counted;
    c->counted = count;
    if (!c->ticks && c->pending)
    {
        c->pending--;
        c->ticks = 2 * COIN_TICKS;
    }
    if (!c->ticks)
        return 0;
    return c->ticks-- > COIN_TICKS;
}

static int clampPacket(int v)
{
    return v > PACKET_MAX ? PACKET_MAX : v < -PACKET_MAX ? -PACKET_MAX : v;
}

static void latchInputs(void)
{
    static Coin coinA, coinB;
    JVSIO *io = inputState();
    unsigned int in = io->state.inputSwitch[PLAYER_1];
    int service = (in & BUTTON_SERVICE) != 0;
    uint8_t p[3] = {0, 0, 0};
    int dx, dy;

    if (coinTick(&coinB, io->state.coinCount[1]))
        p[0] |= P0_COIN_B;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        p[0] |= P0_SETUP;
    if (in & (BUTTON_START | BUTTON_1) || desktopGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH))
        p[1] |= P1_START;
    // F1 alone is the bookkeeping; F1 with a coin, a key-in credit.
    if (coinTick(&coinA, io->state.coinCount[0]))
        p[service ? 1 : 2] |= service ? P1_KEY_IN : P2_COIN_A;
    else if (service)
        p[1] |= P1_RECORD;
    if (in & BUTTON_2)
        p[2] |= P2_ERR_RESET;
    memcpy(ports, p, sizeof(ports));

    trackballTake(&dx, &dy);
    if (in & BUTTON_LEFT)
        dx -= KEY_SPEED;
    if (in & BUTTON_RIGHT)
        dx += KEY_SPEED;
    if (in & BUTTON_UP)
        dy -= KEY_SPEED;
    if (in & BUTTON_DOWN)
        dy += KEY_SPEED;
    double sx = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;
    double sy = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY) / 32768.0;
    if (sx < -STICK_DEADZONE || sx > STICK_DEADZONE)
        dx += (int)(sx * STICK_SPEED);
    if (sy < -STICK_DEADZONE || sy > STICK_DEADZONE)
        dy += (int)(sy * STICK_SPEED);
    // One packet a read; PS/2's y is upwards, the screen's down.
    if (dx || dy)
    {
        int *idx = (int *)(size_t)game->ps2MouseIdx;
        int *px = (int *)(size_t)game->ps2MousePx;
        int *py = (int *)(size_t)game->ps2MousePy;

        px[0] = clampPacket(dx);
        py[0] = clampPacket(-dy);
        *idx = 1;
    }
}

__attribute__((force_align_arg_pointer)) static uint8_t keyIoRead(int port)
{
    if (port == 0)
        latchInputs();
    return port >= 0 && port < 3 ? ports[port] : 0;
}

// The desktop mouse over the game's window (Allegro's, from its _xwin): its
// motion goes to the trackball while the window has the focus.
typedef struct
{
    void *display;
    volatile int lockCount;
    int screen;
    unsigned long window;
} AllegroXwin;

static void *desktopMouse(void *arg)
{
    void *(*openDisplay)(const char *) = dlsym(RTLD_NEXT, "XOpenDisplay");
    int (*getInputFocus)(void *, unsigned long *, int *) = dlsym(RTLD_NEXT, "XGetInputFocus");
    int (*queryPointer)(void *, unsigned long, unsigned long *, unsigned long *, int *, int *, int *, int *,
                        unsigned int *) = dlsym(RTLD_NEXT, "XQueryPointer");
    int (*getGeometry)(void *, unsigned long, unsigned long *, int *, int *, unsigned int *, unsigned int *,
                       unsigned int *, unsigned int *) = dlsym(RTLD_NEXT, "XGetGeometry");
    int (*warpPointer)(void *, unsigned long, unsigned long, int, int, unsigned int, unsigned int, int, int) =
        dlsym(RTLD_NEXT, "XWarpPointer");
    int (*flush)(void *) = dlsym(RTLD_NEXT, "XFlush");
    AllegroXwin *xwin = dlsym(RTLD_DEFAULT, "_xwin");
    void *display;
    int lastX = 0, lastY = 0, seen = 0;
    unsigned long noted = 0;

    (void)arg;
    if (!openDisplay || !getInputFocus || !queryPointer || !getGeometry || !warpPointer || !flush || !xwin ||
        !(display = openDisplay(NULL)))
    {
        log_warn("Paokai: no desktop trackball (no X display or Allegro window)");
        return NULL;
    }
    for (;;)
    {
        unsigned long focus, root, child, window = xwin->window;
        int revert, rx, ry, x, y, gx, gy;
        unsigned int mask, w, h, border, depth;

        usleep(4000);
        if (window && window != noted)
            desktopNoteGameWindow(noted = window);
        getInputFocus(display, &focus, &revert);
        if (!window || (focus != window && focus > 1) ||
            !getGeometry(display, window, &root, &gx, &gy, &w, &h, &border, &depth) ||
            !queryPointer(display, window, &root, &child, &rx, &ry, &x, &y, &mask) || x < 0 || y < 0 ||
            x >= (int)w || y >= (int)h)
        {
            seen = 0;
            continue;
        }
        if (seen)
            trackballAdd(x - lastX, y - lastY);
        seen = 1;
        lastX = x;
        lastY = y;
        if (x < (int)w / 4 || x > (int)w - (int)w / 4 || y < (int)h / 4 || y > (int)h - (int)h / 4)
        {
            lastX = w / 2;
            lastY = h / 2;
            warpPointer(display, 0, window, 0, 0, 0, 0, lastX, lastY);
            flush(display);
        }
    }
    return NULL;
}

void paokaiIoInstall(const PaokaiGame *g)
{
    pthread_t thread;

    game = g;
    detourFunction(g->keyIoRead, keyIoRead);
    desktopGamepadEnable();
    if (getConfig()->inputMode != 2 && pthread_create(&thread, NULL, desktopMouse, NULL) == 0)
        pthread_detach(thread);
}
