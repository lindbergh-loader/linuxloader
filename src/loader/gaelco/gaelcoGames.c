// Gaelco game descriptors (see gaelcoGame.h).

#include <stddef.h>

#include "gaelcoGame.h"
#include "../config/config.h"

static const GaelcoGame games[] = {
};

const GaelcoGame *gaelcoGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const GaelcoGame *gaelcoGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
