#ifndef CECROPIA_GAME_H
#define CECROPIA_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Cecropia game. The Act
// (2007): "decaf", a 32-bit Linux SDL 1.2 player of hand-drawn movies
// (H.264 in QuickTime files, FFmpeg 2006) and their scripts, built against
// uClibc 0.9.28. The cabinet: a knob (a mouse's horizontal motion), a
// button and a coin door on keys, and the GIB, a USB serial board for the
// security handshake (and coin counts the game does not use).
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // The cabinet's game directory (~/dist/decaf: bin/, release*/, current
    // naming the release run) is the one the ELF's bin/ is in. Its state
    // database (statePath) goes in stateDir there.
    const char *statePath;
    const char *stateDir;

    // Movie::Movie(const char *path, bool): its movie, relative to the
    // directory the game is in then, is opened by another thread later.
    uint32_t movieCtor;

    // The GIB board's functions the loader stands in for.
    uint32_t gibOpen, gibClose, gibReset, gibChallenge, gibGetVersion, gibGetBoardId;

    // InputManager::getActiveControlValue (the knob's value), and the
    // InputManager's fields it reads: the knob turned backwards (a char),
    // the value, its range, the knob read as motion (a char, else the
    // pointer's position across the screen).
    uint32_t getActiveControlValue;
    int knobBackwards, knobValue, knobMin, knobMax, knobRelative;

    // Controller::getInstance and Controller::handleEvent(int, int): the
    // keys' events, a coin (eventCoin) and the button (eventButton).
    uint32_t controllerGetInstance, controllerHandleEvent;
    int eventCoin, eventButton;
} CecropiaGame;

const CecropiaGame *cecropiaGetGame(uint32_t crc32);
const CecropiaGame *cecropiaGetGameByFileCrc(uint32_t fileCrc32);

#endif // CECROPIA_GAME_H
