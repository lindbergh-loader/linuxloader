// Namco N2 game descriptors (see namcoN2Game.h).

#include <stddef.h>

#include "namcoN2Game.h"
#include "../config/config.h"

static const NamcoN2Game games[] = {
};

const NamcoN2Game *namcoN2GetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const NamcoN2Game *namcoN2GetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
