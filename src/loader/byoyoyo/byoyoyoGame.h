#ifndef BYOYOYO_GAME_H
#define BYOYOYO_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one Byoyoyo game. Skycurser
// (2017): a GameMaker: Studio 1.4 game, its YoYo Linux runner (32-bit, X11
// + GLX, OpenAL) and its data (assets/game.unx), on a two-player cabinet
// whose controls are a keyboard encoder's keys: the game reads nothing but
// the X keyboard. Its settings, scores and credits are GameMaker's save
// area, ~/.config/skycurser_BGS/.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it
} ByoyoyoGame;

const ByoyoyoGame *byoyoyoGetGame(uint32_t crc32);
const ByoyoyoGame *byoyoyoGetGameByFileCrc(uint32_t fileCrc32);

#endif // BYOYOYO_GAME_H
