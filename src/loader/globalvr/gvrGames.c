// Global VR game descriptors (see gvrGame.h).

#include <stddef.h>

#include "gvrGame.h"
#include "../config/config.h"

static const GvrGame games[] = {
};

const GvrGame *gvrGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const GvrGame *gvrGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
