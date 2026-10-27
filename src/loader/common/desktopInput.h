#ifndef DESKTOP_INPUT_H
#define DESKTOP_INPUT_H

// The desktop keyboard as a JVS state (1 start, 5 coin, F1 service, F2 test,
// arrows, see desktopInput.c), read from the X server's key map, and the
// mouse over the game's window as player 1's gun.
struct JVSIO;
struct JVSIO *desktopInputState(void);
// The same without the mouse: the keys (and the SDL gamepads asked for) that
// still work beside evdev input. There the mouse is the X server's pointer,
// which every mouse and light gun moves and clicks: its buttons, player 1's
// switches here, would also be pressed by player 2's gun.
struct JVSIO *desktopKeyboardState(void);

// The mouse buttons held, as last read by desktopInputState() (which also
// gives them as BUTTON_1, BUTTON_2 and BUTTON_3).
#define DESKTOP_POINTER_LEFT 1
#define DESKTOP_POINTER_RIGHT 2
#define DESKTOP_POINTER_MIDDLE 4
unsigned int desktopPointerButtons(void);
// The size of the focused window the pointer was last read over: 0 if none.
int desktopPointerWindowSize(int *width, int *height);
// The switch the middle button gives in desktopInputState() (BUTTON_3 unless
// a game chooses another, on PLAYER_1).
void desktopPointerMiddle(int bit);

// The first SDL gamepad in desktopInputState(), for the games that ask (Start
// starts, Back is coin 1, A BUTTON_1, the d-pad the joystick, R3 test, L3
// service), and its axes (an SDL_GamepadAxis: the sticks -32768..32767, the
// triggers 0..32767; 0 without one).
void desktopGamepadEnable(void);
// Also map the second SDL gamepad to player 2 for two-player games.
void desktopGamepadEnableTwoPlayers(void);
// Also read WASD as player 2's joystick, alongside player 1's arrows.
void desktopKeyboardEnableTwoPlayers(void);
int desktopGamepadAxis(int axis);

// Esc or Alt+F4 quits, while the game's window has the focus.
void desktopStartQuitWatch(void);

#endif // DESKTOP_INPUT_H
