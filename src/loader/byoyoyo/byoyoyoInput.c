// The Byoyoyo games' controls (see byoyoyoGame.h): the cabinet's keyboard
// encoder, the keys the game's controls script (scr_update_controls) reads:
//   player 1: the arrows, Left Ctrl the shotgun, Left Alt the katana,
//             Space the weapon switch, 1 start, 5 coin
//   player 2: R/F/D/G (up, down, left, right), A the shotgun, S the katana,
//             Q the switch, 2 start, 6 coin
//   F2 the operator's menu
// The desktop keyboard is the game's own. The loader's input is sent to the
// game's window as those keys (XSendEvent, from a connection of its own),
// pressed and released as its switches change, with Shift where the layout
// has the key's symbol shifted (the runner reads the symbols with
// XLookupString: on AZERTY, 1 and 5 are Shift+& and Shift+():
// - [Input] INPUT_MODE 2: evdev's JVS state, PLAYER_1/2's joystick,
//   BUTTON_1 (shotgun), BUTTON_2 (katana), BUTTON_3 (switch), START, the
//   coins (a press of COIN_MS each), and TEST (F2).
// - otherwise the desktop's two first SDL gamepads, one a player: the d-pad
//   or left stick the joystick, A the shotgun, B the katana, X the switch,
//   Start start, Back coin, R3 (player 1's) the operator's menu.

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL_gamepad.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "byoyoyo.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define POLL_US 5000
#define COIN_MS 150
#define STICK_THRESHOLD 16384

enum
{
    K_UP,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_FIRE,
    K_KATANA,
    K_SWITCH,
    K_START,
    K_COIN,
    K_PLAYER_KEYS
};

static const KeySym playerKeys[2][K_PLAYER_KEYS] = {
    {XK_Up, XK_Down, XK_Left, XK_Right, XK_Control_L, XK_Alt_L, XK_space, XK_1, XK_5},
    {XK_r, XK_f, XK_d, XK_g, XK_a, XK_s, XK_q, XK_2, XK_6},
};
#define KEY_COUNT (2 * K_PLAYER_KEYS + 1)
#define KEY_OP_MENU (2 * K_PLAYER_KEYS)

static volatile Window gameWindow;

static struct
{
    Display *(*openDisplay)(const char *);
    KeyCode (*keysymToKeycode)(Display *, KeySym);
    KeySym (*lookupKeysym)(XKeyEvent *, int);
    int (*pending)(Display *);
    int (*nextEvent)(Display *, XEvent *);
    int (*refreshKeyboardMapping)(XMappingEvent *);
    Status (*sendEvent)(Display *, Window, Bool, long, XEvent *);
    int (*flush)(Display *);
} x;

void byoyoyoInputWindow(unsigned long window)
{
    gameWindow = window;
}

static long long nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

// A coin slot: its count's increases given as presses of COIN_MS, one after
// the other, released COIN_MS in between (the game counts the presses).
typedef struct
{
    int counted, pending;
    long long until;
} Coin;

static int coinHeld(Coin *c, int count, long long now)
{
    if (count > c->counted)
        c->pending += count - c->counted;
    c->counted = count;
    if (c->until && now >= c->until + 2 * COIN_MS)
        c->until = 0;
    if (!c->until && c->pending)
    {
        c->pending--;
        c->until = now;
    }
    return c->until && now < c->until + COIN_MS;
}

static void evdevKeys(uint8_t held[KEY_COUNT], long long now)
{
    static Coin coins[2];
    JVSIO *io = getJVSIO();

    for (int p = 0; p < 2; p++)
    {
        unsigned int in = io->state.inputSwitch[PLAYER_1 + p];
        uint8_t *k = held + p * K_PLAYER_KEYS;
        k[K_UP] = (in & BUTTON_UP) != 0;
        k[K_DOWN] = (in & BUTTON_DOWN) != 0;
        k[K_LEFT] = (in & BUTTON_LEFT) != 0;
        k[K_RIGHT] = (in & BUTTON_RIGHT) != 0;
        k[K_FIRE] = (in & BUTTON_1) != 0;
        k[K_KATANA] = (in & BUTTON_2) != 0;
        k[K_SWITCH] = (in & BUTTON_3) != 0;
        k[K_START] = (in & BUTTON_START) != 0;
        k[K_COIN] = coinHeld(&coins[p], io->state.coinCount[p], now);
    }
    held[KEY_OP_MENU] = (io->state.inputSwitch[SYSTEM] & BUTTON_TEST) != 0;
}

static void gamepadKeys(uint8_t held[KEY_COUNT])
{
    for (int p = 0; p < 2; p++)
    {
        uint8_t *k = held + p * K_PLAYER_KEYS;
        int sx = desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX);
        int sy = desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY);
        k[K_UP] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP) || sy < -STICK_THRESHOLD;
        k[K_DOWN] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || sy > STICK_THRESHOLD;
        k[K_LEFT] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || sx < -STICK_THRESHOLD;
        k[K_RIGHT] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || sx > STICK_THRESHOLD;
        k[K_FIRE] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH);
        k[K_KATANA] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST);
        k[K_SWITCH] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST);
        k[K_START] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_START);
        k[K_COIN] = desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_BACK);
    }
    held[KEY_OP_MENU] = desktopPlayerGamepadButton(0, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
}

// The state that gives a key's symbol on its keycode: none, or Shift.
static unsigned int keyState(Display *display, KeyCode code, KeySym sym)
{
    XKeyEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.display = display;
    ev.keycode = code;
    return x.lookupKeysym(&ev, 0) != sym && x.lookupKeysym(&ev, 1) == sym ? ShiftMask : 0;
}

static void sendKey(Display *display, Window window, KeyCode code, unsigned int state, int press)
{
    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.xkey.type = press ? KeyPress : KeyRelease;
    ev.xkey.display = display;
    ev.xkey.window = window;
    ev.xkey.root = DefaultRootWindow(display);
    ev.xkey.time = CurrentTime;
    ev.xkey.same_screen = True;
    ev.xkey.state = state;
    ev.xkey.keycode = code;
    x.sendEvent(display, window, True, press ? KeyPressMask : KeyReleaseMask, &ev);
}

static void *inputThread(void *arg)
{
    uint8_t held[KEY_COUNT] = {0}, sent[KEY_COUNT] = {0};
    KeySym syms[KEY_COUNT];
    KeyCode codes[KEY_COUNT] = {0};
    unsigned int states[KEY_COUNT] = {0};
    int evdev = getConfig()->inputMode == 2;
    Display *display;

    (void)arg;
    if (!(display = x.openDisplay(NULL)))
    {
        log_warn("Byoyoyo: no X display, no controls but the keyboard");
        return NULL;
    }
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < K_PLAYER_KEYS; i++)
            syms[p * K_PLAYER_KEYS + i] = playerKeys[p][i];
    syms[KEY_OP_MENU] = XK_F2;

    for (;;)
    {
        Window window = gameWindow;
        int changed = 0;

        usleep(POLL_US);
        if (evdev)
            evdevKeys(held, nowMs());
        else
            gamepadKeys(held);
        // The keymap can change after the start (Xwayland takes the
        // desktop's layout late): MappingNotify comes to every client.
        while (x.pending(display))
        {
            XEvent ev;
            x.nextEvent(display, &ev);
            if (ev.type == MappingNotify)
                x.refreshKeyboardMapping(&ev.xmapping);
        }
        if (!window)
            continue;
        for (int i = 0; i < KEY_COUNT; i++)
        {
            if (held[i] == sent[i])
                continue;
            // The key and its state looked up at the press, the release
            // the same.
            if (held[i])
            {
                codes[i] = x.keysymToKeycode(display, syms[i]);
                states[i] = codes[i] ? keyState(display, codes[i], syms[i]) : 0;
            }
            sent[i] = held[i];
            if (!codes[i])
                continue;
            sendKey(display, window, codes[i], states[i], held[i]);
            changed = 1;
        }
        if (changed)
            x.flush(display);
    }
    return NULL;
}

void byoyoyoInputInstall(void)
{
    pthread_t thread;
    void *lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);

    if (!lib)
        lib = RTLD_DEFAULT;
    *(void **)&x.openDisplay = dlsym(lib, "XOpenDisplay");
    *(void **)&x.keysymToKeycode = dlsym(lib, "XKeysymToKeycode");
    *(void **)&x.lookupKeysym = dlsym(lib, "XLookupKeysym");
    *(void **)&x.pending = dlsym(lib, "XPending");
    *(void **)&x.nextEvent = dlsym(lib, "XNextEvent");
    *(void **)&x.refreshKeyboardMapping = dlsym(lib, "XRefreshKeyboardMapping");
    *(void **)&x.sendEvent = dlsym(lib, "XSendEvent");
    *(void **)&x.flush = dlsym(lib, "XFlush");
    if (!x.openDisplay || !x.keysymToKeycode || !x.lookupKeysym || !x.pending || !x.nextEvent || !x.refreshKeyboardMapping || !x.sendEvent || !x.flush)
    {
        log_warn("Byoyoyo: no libX11, no controls but the keyboard");
        return;
    }
    if (getConfig()->inputMode != 2)
        desktopGamepadEnableTwoPlayers();
    if (pthread_create(&thread, NULL, inputThread, NULL) == 0)
        pthread_detach(thread);
}
