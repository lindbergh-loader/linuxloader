// Cosmodog game descriptors (see cosmodogGame.h).

#include <stddef.h>

#include "cosmodogGame.h"
#include "../config/config.h"

static const CosmodogGame games[] = {
};

const CosmodogGame *cosmodogGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const CosmodogGame *cosmodogGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
