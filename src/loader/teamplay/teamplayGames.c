// Teamplay game descriptors (see teamplayGame.h).

#include <stddef.h>

#include "teamplayGame.h"
#include "../config/config.h"

static const TeamplayGame games[] = {
    {
        // Crossfire Maximum Paintball, med_pball (the medium-resolution
        // monitor's build, 512x384; std_pball is the standard one's).
        .crc32 = CROSSFIRE_TEAMPLAY,
        .fileCrc32 = 0xc2aff8d4,
        .rootPath = "/home/pball",
        .dataPath = "/data",
        .kernelVersion = "Linux version 2.4.22 (root@midas) (gcc version 3.2.3) #6 Tue Sep 2 17:43:01 PDT 2003\n",
        .iButtonCheck = 0x0805e7ca,
        .diskSerialCheck = 0x08056ec7,
        .xScale = 0.6946107745f, // 0x3f31d203
        .xOffset = -170.0f,
        .yScale = 1.0f,
        .yOffset = -25.0f,
        .width = 512,
        .height = 384,
    },
};

const TeamplayGame *teamplayGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const TeamplayGame *teamplayGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}

const TeamplayGame *teamplayGetGameBySoundDaemonCrc(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].soundDaemonCrc32 && games[i].soundDaemonCrc32 == crc32)
            return &games[i];
    return NULL;
}
