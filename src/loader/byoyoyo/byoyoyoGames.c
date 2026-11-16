// Byoyoyo game descriptors (see byoyoyoGame.h).

#include <stddef.h>

#include "byoyoyoGame.h"
#include "../config/config.h"

static const ByoyoyoGame games[] = {
    {
        // Skycurser (runner, its data 17.10.01).
        .crc32 = SKYCURSER_BYOYOYO,
        .fileCrc32 = 0x3ce2194f,
    },
};

const ByoyoyoGame *byoyoyoGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const ByoyoyoGame *byoyoyoGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
