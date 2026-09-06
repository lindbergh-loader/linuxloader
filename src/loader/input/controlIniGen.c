#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "controlIniGen.h"
#include "../config/iniParser.h"
#include "sdlInput.h"

const ControlBinding gDefaultCommonBindings[] = {
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_T, AXIS_MODE_DIGITAL, 0, false, SYSTEM, LA_Test, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_ESCAPE, AXIS_MODE_DIGITAL, 0, false, SYSTEM, LA_ExitGame, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_START, AXIS_MODE_DIGITAL, 0, false, SYSTEM, LA_ExitGame, 1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_BACK, AXIS_MODE_DIGITAL, 0, false, SYSTEM, LA_ExitGame, 1, 1},
    {INPUT_TYPE_JOY_BUTTON, 0, 11, AXIS_MODE_DIGITAL, 0, false, SYSTEM, LA_ExitGame, 2, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 10, AXIS_MODE_DIGITAL, 0, false, SYSTEM, LA_ExitGame, 2, 1},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_5, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Coin, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_6, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Coin, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_1, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Start, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_2, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Start, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 11, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Start, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_START, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Start, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_S, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Service, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 10, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Service, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_BACK, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Service, -1, 0},
};

const size_t gDefaultCommonBindingsSize = sizeof(gDefaultCommonBindings) / sizeof(gDefaultCommonBindings[0]);

const ControlBinding gDefaultDigitalBindings[] = {
    // Keyboard
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_UP, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_DOWN, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Q, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button1, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_W, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button2, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_E, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button3, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F7, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Card1Insert, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F8, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Card2Insert, -1, 0},

    // Controller
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_UP, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_DOWN, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_SOUTH, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button1, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_EAST, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button2, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_WEST, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button3, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTY, AXIS_MODE_DIGITAL, -1, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTY, AXIS_MODE_DIGITAL, 1, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTX, AXIS_MODE_DIGITAL, 1, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTX, AXIS_MODE_DIGITAL, -1, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_GUIDE, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Card1Insert, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 1, SDL_GAMEPAD_BUTTON_GUIDE, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Card2Insert, -1, 0},

    // Joystick
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_UP, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_DOWN, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_LEFT, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_RIGHT, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 0, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button1, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 1, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button2, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 2, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Button3, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 1, AXIS_MODE_DIGITAL, -1, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 1, AXIS_MODE_DIGITAL, 1, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 0, AXIS_MODE_DIGITAL, -1, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 0, AXIS_MODE_DIGITAL, 1, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 11, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Card1Insert, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 1, 11, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Card2Insert, -1, 0},
};

const size_t gDefaultDigitalBindingsSize = sizeof(gDefaultDigitalBindings) / sizeof(gDefaultDigitalBindings[0]);

const ControlBinding gDefaultDrivingBindings[] = {
    // Keyboard
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Steer_Left, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Steer_Right, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_UP, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Gas_Digital, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_DOWN, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Brake_Digital, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_V, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ViewChange, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Q, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Boost, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_W, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_BoostRight, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_A, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GearUp, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Z, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GearDown, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_M, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_MusicChange, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_I, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_K, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_J, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_L, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F7, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_CardInsert, -1, 0},

    // Controller
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTX, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Steer, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Gas, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Brake, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_SOUTH, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Boost, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_EAST, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_BoostRight, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_NORTH, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ViewChange, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_WEST, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_MusicChange, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GearUp, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GearDown, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_UP, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_DOWN, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_GUIDE, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_CardInsert, -1, 0},

    // Joystick
    {INPUT_TYPE_JOY_AXIS, 0, 0, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Steer, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 5, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Gas, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 2, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Brake, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 0, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Boost, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 1, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_BoostRight, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 2, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ViewChange, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 5, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GearUp, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 4, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GearDown, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 3, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_MusicChange, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_UP, false, PLAYER_1, LA_Up, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_DOWN, false, PLAYER_1, LA_Down, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_LEFT, false, PLAYER_1, LA_Left, -1, 0},
    {INPUT_TYPE_JOY_HAT, 0, 0, AXIS_MODE_DIGITAL, SDL_HAT_RIGHT, false, PLAYER_1, LA_Right, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 12, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_CardInsert, -1, 0},
};

const size_t gDefaultDrivingBindingsSize = sizeof(gDefaultDrivingBindings) / sizeof(gDefaultDrivingBindings[0]);

const ControlBinding gDefaultFlyingBindings[] = {
    // Keyboard
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Flying_Left, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Flying_Right, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_UP, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Flying_Up, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_DOWN, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Flying_Down, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_A, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Throttle_Accelerate, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Z, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Throttle_Slowdown, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Q, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_GunTrigger, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_W, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_MissileTrigger, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_E, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ClimaxSwitch, -1, 0},

    // Controller
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTX, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Flying_X, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFTY, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Flying_Y, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_RIGHTY, AXIS_MODE_FULL, 0, true, PLAYER_1, LA_Throttle, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_MODE_POSITIVE_HALF, 0, false, PLAYER_1, LA_Throttle, -1, 0},
    {INPUT_TYPE_GAMEPAD_AXIS, 0, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_MODE_NEGATIVE_HALF, 0, false, PLAYER_1, LA_Throttle, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_SOUTH, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_GunTrigger, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_EAST, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_MissileTrigger, -1, 0},
    {INPUT_TYPE_GAMEPAD_BUTTON, 0, SDL_GAMEPAD_BUTTON_NORTH, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ClimaxSwitch, -1, 0},

    // Joystick
    {INPUT_TYPE_JOY_AXIS, 0, 0, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Flying_X, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 1, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_Flying_Y, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 4, AXIS_MODE_FULL, 0, true, PLAYER_1, LA_Throttle, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 5, AXIS_MODE_POSITIVE_HALF, 0, false, PLAYER_1, LA_Throttle, -1, 0},
    {INPUT_TYPE_JOY_AXIS, 0, 4, AXIS_MODE_NEGATIVE_HALF, 0, false, PLAYER_1, LA_Throttle, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 0, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_GunTrigger, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 1, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_MissileTrigger, -1, 0},
    {INPUT_TYPE_JOY_BUTTON, 0, 4, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ClimaxSwitch, -1, 0},

};

const size_t gDefaultFlyingBindingsSize = sizeof(gDefaultFlyingBindings) / sizeof(gDefaultFlyingBindings[0]);

const ControlBinding gDefaultShootingBindings[] = {
    // Mouse / Gun
    {INPUT_TYPE_MOUSE_AXIS, 0, 0, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_GunX, -1, 0},
    {INPUT_TYPE_MOUSE_AXIS, 0, 1, AXIS_MODE_FULL, 0, false, PLAYER_1, LA_GunY, -1, 0},
    {INPUT_TYPE_MOUSE_BUTTON, 0, SDL_BUTTON_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Trigger, -1, 0},
    {INPUT_TYPE_MOUSE_BUTTON, 0, SDL_BUTTON_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Reload, -1, 0},
    {INPUT_TYPE_MOUSE_BUTTON, 0, SDL_BUTTON_MIDDLE, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_GunButton, -1, 0},

    // {INPUT_TYPE_MOUSE_AXIS, 0, 0, AXIS_MODE_FULL, 0, false, PLAYER_2, LA_GunX},
    // {INPUT_TYPE_MOUSE_AXIS, 0, 1, AXIS_MODE_FULL, 0, false, PLAYER_2, LA_GunY},
    // {INPUT_TYPE_MOUSE_BUTTON, 0, SDL_BUTTON_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Trigger},
    // {INPUT_TYPE_MOUSE_BUTTON, 0, SDL_BUTTON_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Reload},
    // {INPUT_TYPE_MOUSE_BUTTON, 0, SDL_BUTTON_MIDDLE, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_GunButton},

    // Keyboard
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Q, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Trigger, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_W, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_Reload, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_E, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_GunButton, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_R, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_ActionButton, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_LEFT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_PedalLeft, -1, 0},
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_RIGHT, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_PedalRight, -1, 0},
};

const size_t gDefaultShootingBindingsSize = sizeof(gDefaultShootingBindings) / sizeof(gDefaultShootingBindings[0]);

const ControlBinding gDefaultMahjongBindings[] = {
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_Y, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_A, -1, 0}, // A
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_U, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_B, -1, 0}, // B
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_I, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_C, -1, 0}, // C
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_O, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_D, -1, 0}, // D

    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_G, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_E, -1, 0}, // E
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_H, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_F, -1, 0}, // F
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_J, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_G, -1, 0}, // G
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_K, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_H, -1, 0}, // H

    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_L, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_I, -1, 0},     // I
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_V, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_J, -1, 0},     // J
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_B, AXIS_MODE_DIGITAL, 0, false, PLAYER_1, LA_K, -1, 0},     // K
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_N, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_L, -1, 0},     // L
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_M, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_M, -1, 0},     // M
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_COMMA, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_N, -1, 0}, // N

    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F1, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Chi, -1, 0},        // CHI
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F2, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Pon, -1, 0},        // PON
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F3, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Kan, -1, 0},        // KAN
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F4, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Reach, -1, 0},      // REACH
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F5, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Agari, -1, 0},      // AGARI
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F6, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_Cancel, -1, 0},     // CANCEL
    {INPUT_TYPE_KEY, 0, SDL_SCANCODE_F7, AXIS_MODE_DIGITAL, 0, false, PLAYER_2, LA_CardInsert, -1, 0}, // Cards In
};

const size_t gDefaultMahjongBindingsSize = sizeof(gDefaultMahjongBindings) / sizeof(gDefaultMahjongBindings[0]);

char *toUpperCase(const char *str)
{
    if (str == NULL)
        return NULL;

    char *result = (char *)malloc(strlen(str) + 1);

    if (result == NULL)
        return NULL;

    int i = 0;
    while (str[i] != '\0')
    {
        result[i] = toupper((unsigned char)str[i]);
        i++;
    }
    result[i] = '\0';

    return result;
}

/**
 * @brief Helper function to convert a ControlBinding struct back into an INI string.
 * This is the reverse of parseSdlSource, used for generating a default controls.ini.
 * @param binding Pointer to the ControlBinding to convert.
 * @param buffer Character buffer to write the string into.
 * @param buffer_size The size of the character buffer.
 */
static void getBindingString(const ControlBinding *binding, char *buffer, size_t buffer_size)
{
    char temp_buffer[128] = {0};
    switch (binding->type)
    {
        case INPUT_TYPE_KEY:
            snprintf(temp_buffer, sizeof(temp_buffer), "KEY_%s", SDL_GetScancodeName(binding->sdlId));
            break;
        case INPUT_TYPE_MOUSE_BUTTON:
            if (binding->sdlId == SDL_BUTTON_LEFT)
                strcpy(temp_buffer, "MOUSE_LEFT_BUTTON");
            else if (binding->sdlId == SDL_BUTTON_RIGHT)
                strcpy(temp_buffer, "MOUSE_RIGHT_BUTTON");
            else if (binding->sdlId == SDL_BUTTON_MIDDLE)
                strcpy(temp_buffer, "MOUSE_MIDDLE_BUTTON");
            else
                snprintf(temp_buffer, sizeof(temp_buffer), "MOUSE_BUTTON_%d", binding->sdlId);
            break;
        case INPUT_TYPE_MOUSE_AXIS:
            snprintf(temp_buffer, sizeof(temp_buffer), "MOUSE_AXIS_%c", binding->sdlId == 0 ? 'X' : 'Y');
            break;
        case INPUT_TYPE_JOY_BUTTON:
            snprintf(temp_buffer, sizeof(temp_buffer), "JOY%d_BUTTON_%d", binding->deviceIndex, binding->sdlId);
            break;
        case INPUT_TYPE_JOY_HAT:
            if (binding->axisThreshold == SDL_HAT_UP)
                snprintf(temp_buffer, sizeof(temp_buffer), "JOY%d_HAT%d_UP", binding->deviceIndex, binding->sdlId);
            else if (binding->axisThreshold == SDL_HAT_DOWN)
                snprintf(temp_buffer, sizeof(temp_buffer), "JOY%d_HAT%d_DOWN", binding->deviceIndex, binding->sdlId);
            else if (binding->axisThreshold == SDL_HAT_LEFT)
                snprintf(temp_buffer, sizeof(temp_buffer), "JOY%d_HAT%d_LEFT", binding->deviceIndex, binding->sdlId);
            else if (binding->axisThreshold == SDL_HAT_RIGHT)
                snprintf(temp_buffer, sizeof(temp_buffer), "JOY%d_HAT%d_RIGHT", binding->deviceIndex, binding->sdlId);
            break;
        case INPUT_TYPE_GAMEPAD_BUTTON:
        {
            char *buttonName = toUpperCase(SDL_GetGamepadStringForButton(binding->sdlId));
            if (buttonName)
            {
                snprintf(temp_buffer, sizeof(temp_buffer), "GC%d_BUTTON_%s", binding->deviceIndex, buttonName);
                free(buttonName);
            }
            break;
        }
        case INPUT_TYPE_JOY_AXIS:
        case INPUT_TYPE_GAMEPAD_AXIS:
        {
            char prefix[64];
            if (binding->type == INPUT_TYPE_JOY_AXIS)
                snprintf(prefix, sizeof(prefix), "JOY%d_AXIS_%d", binding->deviceIndex, binding->sdlId);
            else
            {
                char *axisName = toUpperCase(SDL_GetGamepadStringForAxis(binding->sdlId));
                if (axisName)
                {
                    snprintf(prefix, sizeof(prefix), "GC%d_AXIS_%s", binding->deviceIndex, axisName);
                    free(axisName);
                }
            }

            switch (binding->axisMode)
            {
                case AXIS_MODE_DIGITAL:
                    if (binding->axisThreshold > 0)
                        snprintf(temp_buffer, sizeof(temp_buffer), "%s_POSITIVE", prefix);
                    else
                        snprintf(temp_buffer, sizeof(temp_buffer), "%s_NEGATIVE", prefix);
                    break;
                case AXIS_MODE_POSITIVE_HALF:
                    snprintf(temp_buffer, sizeof(temp_buffer), "%s_POSITIVE_HALF", prefix);
                    break;
                case AXIS_MODE_NEGATIVE_HALF:
                    snprintf(temp_buffer, sizeof(temp_buffer), "%s_NEGATIVE_HALF", prefix);
                    break;
                case AXIS_MODE_FULL:
                default:
                    strcpy(temp_buffer, prefix);
                    break;
            }
            break;
        }
        default:
            break;
    }

    if (binding->isInverted)
    {
        strncat(temp_buffer, "_INVERTED", sizeof(temp_buffer) - strlen(temp_buffer) - 1);
    }
    strncpy(buffer, temp_buffer, buffer_size);
}

/**
 * @brief Helper to add a set of default bindings to an in-memory INI config.
 */
static void addBindingsToIni(IniConfig *cfg, const char *section_name, const ControlBinding *bindings, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        const ControlBinding *b = &bindings[i];
        char action_str[128];
        char binding_str[128];
        getLogicalActionString(b, action_str, sizeof(action_str), section_name);
        getBindingString(b, binding_str, sizeof(binding_str));

        IniSection *sec = iniGetSection(cfg, section_name);
        IniKeyValuePair *pair = NULL;
        if (sec)
        {
            for (int j = 0; j < sec->numPairs; j++)
            {
                if (strcmp(sec->pairs[j].key, action_str) == 0)
                {
                    pair = &sec->pairs[j];
                    break;
                }
            }
        }

        if (pair)
        { // Key already exists, append to it
            char new_value[1024];
            if (b->comboGroupId > 0 && b->comboInputIndex > 0)
            {
                snprintf(new_value, sizeof(new_value), "%s + %s", pair->value, binding_str);
            }
            else
            {
                snprintf(new_value, sizeof(new_value), "%s, %s", pair->value, binding_str);
            }
            iniSetValue(cfg, section_name, action_str, new_value);
        }
        else
        { // New key, just set it
            iniSetValue(cfg, section_name, action_str, binding_str);
        }
    }
}

/**
 * @brief Generates a default controls.ini file based on the hardcoded default binding arrays.
 */
int createDefaultControlsIni(const char *fileName)
{
    printf("Generating default %s\n", fileName);

    IniConfig *ini = calloc(1, sizeof(IniConfig));
    if (!ini)
    {
        perror("Failed to allocate memory for INI config");
        return -1;
    }

    iniSetValue(ini, "Config", "Steer_DeadZone", "500");
    iniSetValue(ini, "Config", "Gas_DeadZone", "500");
    iniSetValue(ini, "Config", "Brake_DeadZone", "500");
    iniSetValue(ini, "Config", "FLYING_X_DeadZone", "500");
    iniSetValue(ini, "Config", "FLYING_Y_DeadZone", "500");
    iniSetValue(ini, "Config", "Throttle_DeadZone", "500");
    iniSetValue(ini, "Config", "GunX_Centering", "0");
    iniSetValue(ini, "Config", "GunY_Centering", "0");
    iniSetValue(ini, "Config", "ShakeIncreaseRate", "1.0");
    iniSetValue(ini, "Config", "ShakeDecayRate", "0.95");
    iniSetValue(ini, "Config", "ShakeMinScreenFraction", "0.15");
    iniSetValue(ini, "Config", "FFBGlobalGain", "1");
    iniSetValue(ini, "Config", "FFBAutocenterGain", "1");
    iniSetValue(ini, "Config", "FFBRumbleGain", "1");
    iniSetValue(ini, "Config", "CardInsert_Toggle", "1");
    iniSetValue(ini, "Config", "Card1Insert_Toggle", "1");
    iniSetValue(ini, "Config", "Card2Insert_Toggle", "1");

    addBindingsToIni(ini, "Common", gDefaultCommonBindings, gDefaultCommonBindingsSize);
    addBindingsToIni(ini, "Digital", gDefaultDigitalBindings, gDefaultDigitalBindingsSize);
    addBindingsToIni(ini, "Driving", gDefaultDrivingBindings, gDefaultDrivingBindingsSize);
    addBindingsToIni(ini, "Flying", gDefaultFlyingBindings, gDefaultFlyingBindingsSize);
    addBindingsToIni(ini, "Shooting", gDefaultShootingBindings, gDefaultShootingBindingsSize);
    addBindingsToIni(ini, "Mahjong", gDefaultMahjongBindings, gDefaultMahjongBindingsSize);

    int ret = iniSave(ini, fileName);
    iniFree(ini);
    return ret;
}
