#ifndef PAOKAI_GAME_H
#define PAOKAI_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Paokai game. Go Go Strike
// (2004): a 32-bit Linux Allegro 4.0 game (its X11 window here) on a
// trackball bowling cabinet with a coin, key-in and hopper payout. Its I/O
// card (the w040101 driver) is three byte-wide ports, /dev/w040101_io0..2:
// a read gives a port's switches (set bits pressed), a write its outputs.
// Its battery-backed SRAM is /dev/w040101sram0, mapped (64 KiB). The
// trackball is a PS/2 mouse read by the cabinet's own Allegro mouse driver,
// which left each packet's motion in the game's ps2mousepx/ps2mousepy and
// their count in ps2mouseidx. The game's own sources' names are in the ELF.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // A port read (ubKeyIoRead(port)): the loader is the card.
    uint32_t keyIoRead;
    // The trackball's packets, as the PS/2 driver left them.
    uint32_t ps2MouseIdx, ps2MousePx, ps2MousePy;
} PaokaiGame;

const PaokaiGame *paokaiGetGame(uint32_t crc32);
const PaokaiGame *paokaiGetGameByFileCrc(uint32_t fileCrc32);

#endif // PAOKAI_GAME_H
