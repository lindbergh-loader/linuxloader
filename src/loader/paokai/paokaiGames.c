// Paokai game descriptors (see paokaiGame.h).

#include <stddef.h>

#include "paokaiGame.h"
#include "../config/config.h"

static const PaokaiGame games[] = {
};

const PaokaiGame *paokaiGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const PaokaiGame *paokaiGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
