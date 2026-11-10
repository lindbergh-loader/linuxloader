// The Act's controls: a knob, a button and the coin door.
//
// On the cabinet the knob is a mouse's horizontal motion, the button and
// the coins keys (Enter or 3, 1 or 2), all read by the game through SDL:
// the desktop's mouse and keys still work so, with the loader's usual keys
// besides: 1 is the button (start), 5 a coin. Besides them:
//
// - The knob, turned by a stick: the gamepad's left stick (desktop) or a
//   stick on ANALOGUE_1 (evdev), and the d-pad (the gamepad's, P1 LEFT and
//   RIGHT with evdev). The stick turns it, the further the faster, to the
//   whole of its range in KNOB_SWEEP_MS held all the way: it stays where it
//   is let go, as the cabinet's knob does. The game reads it each frame
//   (InputManager::getActiveControlValue): the stick's turn is added to the
//   mouse's there.
// - The button: the gamepad's A or Start (desktop), P1 START or BUTTON_1
//   (evdev). The coins: the gamepad's Back (desktop), the coin switches
//   (evdev). Sent as the game's key events are, from its event loop
//   (SDL_PollEvent, on its main thread).

#include <stdint.h>
#include <time.h>

#include <SDL3/SDL_gamepad.h>

#include "cecropia.h"
#include "../common/desktopInput.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"
#include "../../minhook/include/MinHook.h"

// Held all the way, the stick turns the knob across its range in this long.
#define KNOB_SWEEP_MS 1000.0
// The stick's middle that does not turn it (of 1), and its response: the
// turn grows with the square of the stick's travel past it, for the fine
// moves.
#define KNOB_DEADZONE 0.15

static const CecropiaGame *game;
static int (*realGetActiveControlValue)(void *);
static int (*realPollEvent)(void *);
static void *(*controllerGetInstance)(void);
static void (*controllerHandleEvent)(void *, int, int);

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// A stick's travel (-1..1) as the knob's turn (-1..1).
static double stickTurn(double v)
{
    double a = v < 0 ? -v : v;

    if (a <= KNOB_DEADZONE)
        return 0;
    a = (a - KNOB_DEADZONE) / (1 - KNOB_DEADZONE);
    a = a > 1 ? 1 : a * a;
    return v < 0 ? -a : a;
}

// The knob's turn asked for now (-1..1).
static double knobTurn(void)
{
    double turn = stickTurn(desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0);
    int left = desktopGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT), right = desktopGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    if (getConfig()->inputMode == 2)
    {
        JVSIO *io = getJVSIO();
        if (io->analogueMax > 0)
            turn += stickTurn(io->state.analogueChannel[ANALOGUE_1] * 2.0 / io->analogueMax - 1);
        left |= !!(io->state.inputSwitch[PLAYER_1] & BUTTON_LEFT);
        right |= !!(io->state.inputSwitch[PLAYER_1] & BUTTON_RIGHT);
    }
    turn += right - left;
    return turn > 1 ? 1 : turn < -1 ? -1 : turn;
}

// The knob's value, the mouse's (the game's own read) and the stick's turn,
// in the range the game gives it. Read as motion, the value is the
// InputManager's; else (The Act's way) it is the pointer's position across
// the screen, the stick moving a position of its own that the mouse takes
// back when it moves (or the game puts the pointer elsewhere).
__attribute__((force_align_arg_pointer)) static int getActiveControlValue(void *im)
{
    static uint64_t last;
    static double turned, position = -1;
    static int lastMouse = -1;
    uint8_t *self = im;
    int value = realGetActiveControlValue(im), relative = *(uint8_t *)(self + game->knobRelative);
    int min = *(int *)(self + game->knobMin), max = *(int *)(self + game->knobMax);
    uint64_t now = nowMs();
    double dt = last && now - last < 200 ? (double)(now - last) : 0, turn = knobTurn();

    last = now;
    if (*(uint8_t *)(self + game->knobBackwards))
        turn = -turn;
    if (max <= min)
        return value;
    if (relative)
    {
        if (turn == 0)
        {
            turned = 0;
            return value;
        }
        // The fraction of a step kept between frames.
        turned += turn * (max - min) * dt / KNOB_SWEEP_MS;
        int steps = (int)turned;
        turned -= steps;
        value += steps;
        value = value < min ? min : value > max ? max : value;
        *(int *)(self + game->knobValue) = value;
        return value;
    }
    if (value != lastMouse || position < 0)
        position = value;
    lastMouse = value;
    position += turn * (max - min) * dt / KNOB_SWEEP_MS;
    position = position < min ? min : position > max ? max : position;
    return (int)position;
}

// The button and coins: their presses, as the keys' events.
static void sendEvents(void)
{
    static int lastButton, lastBack, lastCoins = -1;
    int button = desktopGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH) || desktopGamepadButton(SDL_GAMEPAD_BUTTON_START);
    int back = desktopGamepadButton(SDL_GAMEPAD_BUTTON_BACK), coins = 0, coinPresses = back && !lastBack;
    void *controller;

    if (getConfig()->inputMode == 2)
    {
        JVSIO *io = getJVSIO();
        button |= !!(io->state.inputSwitch[PLAYER_1] & (BUTTON_START | BUTTON_1));
        coins = io->state.coinCount[PLAYER_1] + io->state.coinCount[PLAYER_2];
        if (lastCoins >= 0 && coins > lastCoins)
            coinPresses += coins - lastCoins;
        lastCoins = coins;
    }
    lastBack = back;
    if ((button && !lastButton) || coinPresses)
    {
        controller = controllerGetInstance();
        for (; coinPresses > 0; coinPresses--)
            controllerHandleEvent(controller, game->eventCoin, 1);
        if (button && !lastButton)
            controllerHandleEvent(controller, game->eventButton, 1);
    }
    lastButton = button;
}

// SDL 1.2's focus and key events: their type, and the keys' key
// (keysym.sym).
#define SDL12_ACTIVEEVENT 1
#define SDL12_KEYDOWN 2
#define SDL12_KEYUP 3
#define SDL12_KEY_SCANCODE 4
#define SDL12_KEY_SYM 8

// The loader's keys as the cabinet's: 1 (start) is the button (Enter), 5 a
// coin (the cabinet's 1). They are the keys where 1 and 5 are on a US
// keyboard, whatever the layout: SDL 1.2's key (sym) is the layout's (& and
// ( for them on AZERTY), its scancode the X key code, the key's place: the
// number row's 1 and 5, the keypad's.
#define KEY_ROW_1 10
#define KEY_ROW_5 14
#define KEY_PAD_1 87
#define KEY_PAD_5 84

static void mapKey(uint8_t *event)
{
    int *sym = (int *)(event + SDL12_KEY_SYM);
    uint8_t key = event[SDL12_KEY_SCANCODE];

    if (event[0] != SDL12_KEYDOWN && event[0] != SDL12_KEYUP)
        return;
    if (key == KEY_ROW_1 || key == KEY_PAD_1)
        *sym = '\r';
    else if (key == KEY_ROW_5 || key == KEY_PAD_5)
        *sym = '1';
}

__attribute__((force_align_arg_pointer)) static int pollEvent(void *event)
{
    static uint64_t lastSent;
    uint64_t now = nowMs();
    int ret;

    // The loop polls until the queue is empty, each frame: once a frame.
    if (now != lastSent)
    {
        lastSent = now;
        sendEvents();
    }
    // Each focus change toggles the game's pause (as its P key does): the
    // cabinet has no window manager to send any, a desktop sends one at each
    // click in the window and out of it. They are not passed on.
    do
        ret = realPollEvent(event);
    while (ret && event && *(uint8_t *)event == SDL12_ACTIVEEVENT);
    if (ret && event)
        mapKey(event);
    return ret;
}

void cecropiaInputInstall(const CecropiaGame *g)
{
    game = g;
    *(void **)&controllerGetInstance = (void *)(uintptr_t)g->controllerGetInstance;
    *(void **)&controllerHandleEvent = (void *)(uintptr_t)g->controllerHandleEvent;
    // With evdev input the pads are evdev's: the SDL gamepad, read as well,
    // would give each press and turn twice.
    if (getConfig()->inputMode != 2)
        desktopGamepadEnable();
    if (MH_CreateHook((void *)(uintptr_t)g->getActiveControlValue, (void *)getActiveControlValue,
                      (void **)&realGetActiveControlValue) != MH_OK)
        log_error("The Act: cannot hook the knob");
    *(void **)&realPollEvent = hookExecutableImport("SDL_PollEvent", (void *)pollEvent);
    if (!realPollEvent)
        log_error("The Act: no SDL_PollEvent import, no button");
}
