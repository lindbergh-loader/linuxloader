// IGS game descriptors (see igsGame.h).

#include <stddef.h>

#include "igsGame.h"
#include "../config/config.h"

static const IgsGame games[] = {
    {
        // Gold Captain (GoldCaptian-E2000-v1-e65, March 12, 2009).
        .crc32 = GOLD_CAPTAIN_IGS,
        .fileCrc32 = 0xd05b6728,
        .rootPath = "/GoldCaptain",
        .dataDirs = {"/fec_data", "/rf_data", NULL},
        .biosBoard = 5,
        .biosCheck = 0x0813b760,
        .setVideoMode = 0x080fd910,
        .frameWidth = 640,
        .frameHeight = 480,
        .keyLeft = 0x02,
        .keyRight = 0x04,
        .keyTest = 0x08,
        .keyService = 0x10,
        .helmMax = 512,
        .plxDeviceFind = 0x080ce840,
        .plxDeviceOpen = 0x0813ce10,
        .plxBarMap = 0x0813d250,
        .mcuExchange = 0x080cd1f0,
        .mcuProcess = 0x080ccea0,
        .mcuIn = 0x08691f00,
        .pcOut = 0x0868df00,
        .pendingCommand = 0x08695f10,
        .pendingArgument = 0x08695f14,
        .sodObjectDepth = 0x08597848,
        .sodObjects = 0x280,
        .sodObjectSize = 0x5f8,
        .stage = 0x086cff20,
        .segmentFight = 5,
        .fightTime = 0x086cff68,
        .segment = 0x086cff74,
        .segmentCount = 0x086d17e0,
        .fightSetups = {0x08087ce0, 0x08088dc0, 0x0808a000},
        .fightRelease = 0x08088d00,
        .fightReleaseFlag = 0x08595ce8,
        .weaponsRelease = 0x080bfda0,
    },
};

const IgsGame *igsGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const IgsGame *igsGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
