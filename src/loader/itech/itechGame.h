#ifndef ITECH_GAME_H
#define ITECH_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Incredible Technologies
// game. Orange County Choppers Pinball (2005): a 32-bit Linux OpenGL game on
// IT's "Nighthawk" PC, its own X11 window. Its cabinet: a USB I/O board
// (ioboard1, the flippers, buttons, coins and lamps), a USB dongle
// (itdongle1), a BIOS "worm" and a local PostgreSQL database (ITNet's
// queues, and the persistence layer). The executable exports its symbols:
// the IT platform layer's functions (ITInitializeUsbIoBoard1, ...) are hooked
// by name.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // StartXServer (graphics.c, static): forks the cabinet's X server and
    // opens its display.
    uint32_t startXServer;
} ItechGame;

const ItechGame *itechGetGame(uint32_t crc32);
const ItechGame *itechGetGameByFileCrc(uint32_t fileCrc32);

#endif // ITECH_GAME_H
