// Global VR game descriptors (see gvrGame.h).

#include <stddef.h>

#include "gvrGame.h"
#include "../config/config.h"

static const GvrGame games[] = {
    {
        // Puck Off, v0710240921.
        .crc32 = PUCK_OFF_GVR,
        .fileCrc32 = 0x210f3077,
        .rootPath = "/game",
        .diskSerialCheck = 0x80a8030,
        .frameWidth = 640,
        .frameHeight = 480,
    },
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
