// Incredible Technologies game descriptors (see itechGame.h).

#include <stddef.h>

#include "itechGame.h"
#include "../config/config.h"

static const ItechGame games[] = {
    {
        // Orange County Choppers Pinball (game.bin).
        .crc32 = OCC_PINBALL_ITECH,
        .fileCrc32 = 0x91bfaf71,
        .startXServer = 0x082b9903,
    },
};

const ItechGame *itechGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const ItechGame *itechGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
