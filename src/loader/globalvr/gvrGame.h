#ifndef GVR_GAME_H
#define GVR_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Global VR game. They are
// stripped 32-bit Linux 2.4 executables, built for a 4-byte-aligned stack
// (an old gcc), drawing in their own X11/GLX window, started from /game by
// the cabinet's run scripts. The cabinet's GFXIO board (an IO-Warrior USB
// chip behind the gfxio kernel driver, /dev/usb/gfxio*) has the buttons,
// the coin mechs and the trackball.
//
// America's Army is another kind: the Linux port of the PC game (Unreal
// Engine 2, SDL 1.2), its arcade mode in UnrealScript. It keeps its own
// window, and its cabinet's I/O (guns, buttons, coins) comes from a daemon
// over TCP (see gvrLink.c).
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // Cabinet layout: the game's directory, the game's directory here.
    const char *rootPath;

    // The check a game's start goes through: the hash of the cabinet's
    // disk serial (/dev/hda, HDIO_GET_IDENTITY) against data/misc.dat's
    // misc.bin. Made to pass (0: none).
    uint32_t diskSerialCheck;

    // An SDL game keeping its own window, run from its System directory
    // (America's Army): none of the Lindbergh path, nor the stack-aligning
    // stubs. Its home ($HOME/.armyops260) is home/ in the game's directory.
    int ownWindow;

    // The port the game reaches its cabinet's I/O daemon on (0: none), at
    // its host name's address: served by the loader (gvrLink.c).
    int linkPort;

    // The size the game draws at, whatever its window's (0: the window's):
    // fitted to the loader's window (gvrFrame.c).
    int frameWidth, frameHeight;
} GvrGame;

const GvrGame *gvrGetGame(uint32_t crc32);
const GvrGame *gvrGetGameByFileCrc(uint32_t fileCrc32);

#endif // GVR_GAME_H
