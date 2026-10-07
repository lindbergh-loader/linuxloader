#ifndef TEAMPLAY_GAME_H
#define TEAMPLAY_GAME_H

#include <stdint.h>

// A cabinet directory outside rootPath and dataPath, in the game's
// directory.
typedef struct
{
    const char *from; // cabinet path, e.g. "/var/pt2"
    const char *to;   // under the game's directory, e.g. "audit"
} TeamplayRootAlias;

// Everything the loader needs to know about one Teamplay game. They are
// 32-bit Linux 2.4 executables (glut or X11/GLX, OpenGL, OSS), started by
// the cabinet's scripts from their directory (/home/pball, /home/rocky/pt2,
// /home/jacko/stv), their data on a read-only /data. The cabinet's
// MegaJamma board (a kernel driver, /dev/mjg or /dev/mjf) has the switches,
// the coin counters and the light guns; an iButton on the super I/O chip
// (/dev/p37c) is the dongle.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it
    // The game's sound daemon, a process of its own (the loader's partial
    // CRC of its code segment), 0: none. It gets the cabinet's paths and
    // /dev/dsp, none of the game's checks.
    uint32_t soundDaemonCrc32;

    // Cabinet layout: the game's directory and the data partition, both the
    // game's directory here.
    const char *rootPath;
    const char *dataPath;
    // Other cabinet directories, NULL-terminated; NULL: none.
    const TeamplayRootAlias *rootAliases;
    // The cabinet's /proc/cpuinfo, which the game parses, its "cpu MHz" a
    // %.3f for the host's TSC rate; NULL: the host's.
    const char *cpuinfo;
    // The jump (a jbe) past the game's "cpu seems out of range" bail-out,
    // made unconditional: the host's TSC can run faster than the game's
    // limit (3000 MHz for Police Trainer 2); 0: none.
    uint32_t cpuRangeCheck;
    // The cabinet's /proc/version, which the game wants before it lets a
    // game start; NULL: the host's.
    const char *kernelVersion;

    // The game's vblank count (a function reading the nvidia driver's
    // interrupt count in /proc/interrupts), which paces its frames: a 60 Hz
    // count; 0: none.
    uint32_t vblankCount;

    // Checks run before a game starts, made to pass: the iButton's
    // challenges (on the super I/O chip's ports) and the hard disk's serial
    // number against the code in adjust.dat. Function addresses.
    uint32_t iButtonCheck;
    uint32_t diskSerialCheck;

    // The guns: the game averages the board's samples (12-bit x and y),
    // then turns them into screen pixels as x * xScale + xOffset, y * yScale
    // + yOffset, on a width x height screen (the size it gives the board's
    // open).
    float xScale, xOffset, yScale, yOffset;
    int width, height;
    // The game's gun calibration (functions of the gun returning the offset
    // it adds in pixels, from its settings), taken out of the samples so that
    // a shot lands under the pointer; 0: none.
    uint32_t calibrationX, calibrationY;
    // The program the cabinet's driver runs next, in the exiting game's place
    // here, when the game exits with handOffStatus (-1: any): Star Trek:
    // Voyager's service mode when it exits for the test switch, the game
    // when the service mode exits. NULL: none.
    const char *handOffProgram;
    int handOffStatus;
    // The board's device and the size of its status packets; NULL, 0:
    // /dev/mjg, 0xb8 (Star Trek: Voyager's older driver is /dev/mjf, 0xa8).
    const char *mjPath;
    int mjPacketSize;
} TeamplayGame;

const TeamplayGame *teamplayGetGame(uint32_t crc32);
const TeamplayGame *teamplayGetGameByFileCrc(uint32_t fileCrc32);
const TeamplayGame *teamplayGetGameBySoundDaemonCrc(uint32_t crc32);

#endif // TEAMPLAY_GAME_H
