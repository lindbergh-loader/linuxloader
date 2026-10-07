// Namco ES1 game descriptors (see namcoEs1Game.h).

#include "namcoEs1Game.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"

static const NamcoEs1Game games[] = {
};

const NamcoEs1Game *namcoEs1GetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const NamcoEs1Game *namcoEs1GetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
