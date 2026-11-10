// Cecropia game descriptors (see cecropiaGame.h).

#include <stddef.h>

#include "cecropiaGame.h"
#include "../config/config.h"

static const CecropiaGame games[] = {
};

const CecropiaGame *cecropiaGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const CecropiaGame *cecropiaGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
