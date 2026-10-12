// Roxor game descriptors (see roxorGame.h).

#include <stddef.h>

#include "roxorGame.h"
#include "../config/config.h"

static const RoxorGame games[] = {
    {
        // Tux Racer 4.1 ARCADE.
        .crc32 = TUX_RACER_ROXOR,
        .fileCrc32 = 0x4df577f6,
        .rootPath = "/tux",
        .dataDir = "tuxracer-data-arcade",
        .settings = ".tuxracer-v1",
        .saveDir = "save",
        .openIoBoard = 0x080ca348,
        .closeIoBoard = 0x080ca43c,
        .checkIoBoardError = 0x080ca4b8,
        .ioOut = 0x080ca4d0,
        .ioIn = 0x080ca4f8,
        .bitFromPin = 0x080cb3c0,
        .ioBoardAttached = 0x081a067c,
        .ioBoardSwitchMask = 0x081a0698,
        .coin1 = "J2P3",
        .coin2 = "J2P5",
        .start = "J3P4",
        .left = "J3P8",
        .right = "J3P9",
        .operatorMenu = "J3P6",
    },
};

const RoxorGame *roxorGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const RoxorGame *roxorGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
