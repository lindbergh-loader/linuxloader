#ifndef COSMODOG_GAME_H
#define COSMODOG_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Cosmodog game. Rockin'
// Bowl-O-Rama (2007): a 32-bit Linux glut game (Gentoo, kernel 2.6.20) on a
// trackball cabinet. Its I/O controller (switches, coin counters, the
// trackball's counts, lamps, the ticket dispenser, the licences of its
// features) is on the PC's serial port, its packets 7-bit, checked and
// keyed (IOPACKET_*); the game's own sources' names are in the ELF.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // The cabinet's directories, the game's directory here: the game's
    // (rootPath, where it runs from) and its data partition's (dataPath:
    // dataPath/... is the game directory's data/...).
    const char *rootPath;
    const char *dataPath;

    // The controller's start (IO_Init: its serial port opened and set up)
    // and packets, sent and received (IOPACKET_SendMessage,
    // IOPACKET_GetMessage): the loader is the controller.
    uint32_t ioInit, sendMessage, getMessage;
    // The game's SHA-1 and hash conversion, which the controller's licence
    // answers are checked with (sha1, KEYGEN_ConvertHashToBytes).
    uint32_t sha1, convertHash;
    // The check of the files the cabinet's image must hold
    // (FILESIG_DataValid: Gentoo's and the wireless card's among them).
    uint32_t dataValid;
} CosmodogGame;

const CosmodogGame *cosmodogGetGame(uint32_t crc32);
const CosmodogGame *cosmodogGetGameByFileCrc(uint32_t fileCrc32);

#endif // COSMODOG_GAME_H
