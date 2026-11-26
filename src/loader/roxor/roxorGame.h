#ifndef ROXOR_GAME_H
#define ROXOR_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Roxor game. Tux Racer
// arcade (Sunspire Studios' Tux Racer, 2002): a 32-bit Linux SDL 1.2 game,
// its menus and settings in Tcl 8.3, drawing in its own SDL window. The
// cabinet's I/O board (switches, lamps, coin counter, ticket dispenser) is
// on the PC's parallel port (0x378, EPP: an address byte at 0x37b, a data
// byte at 0x37c), driven by the game itself through ioperm and in/out.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // Cabinet layout: the game's directory (rootPath, "/tux"), the game's
    // directory here; the data directory in it the game runs from (and its
    // options' data_dir); the user's settings directory ($HOME/<settings>),
    // saveDir in the game's directory.
    const char *rootPath;
    const char *dataDir;
    const char *settings;
    const char *saveDir;

    // The board's functions the loader stands in for (OS_*: open, close,
    // error check, register read and write) and the one naming its pins
    // ("J2P3": a bit of the 16-bit switch word). Function addresses.
    uint32_t openIoBoard, closeIoBoard, checkIoBoardError, ioIn, ioOut;
    uint32_t bitFromPin;
    // Its variables the open sets up: the board attached (a char), the
    // switches' change mask (all set).
    uint32_t ioBoardAttached, ioBoardSwitchMask;

    // The pins of the switches the cabinet has.
    const char *coin1, *coin2, *start, *left, *right, *operatorMenu;
} RoxorGame;

const RoxorGame *roxorGetGame(uint32_t crc32);
const RoxorGame *roxorGetGameByFileCrc(uint32_t fileCrc32);

#endif // ROXOR_GAME_H
