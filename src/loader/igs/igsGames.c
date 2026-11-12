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
    {
        // Pirate Revenge (M-101CN, IGS-Linux of August 2008). Each fight
        // frees what it loaded (a fight whose time is up and continued plays
        // again): none of Gold Captain's fight fixes.
        .crc32 = PIRATE_REVENGE_IGS,
        .fileCrc32 = 0x16f1257a,
        .rootPath = "/pr/linux",
        .dataDirs = {"/fec_data", "/rf_data", NULL},
        .biosBoard = 5,
        .biosCheck = 0x0813a4d0,
        .setVideoMode = 0x080fc680,
        .frameWidth = 640,
        .frameHeight = 480,
        .keyLeft = 0x02,
        .keyRight = 0x01,
        .keyBomb = 0x04,
        .keyTest = 0x20,
        .keyService = 0x40,
        .helmMax = 1024,
        .helmReversed = 1,
        .helmSpeed = 0.08,
        .helmCalibration = 0x08797018,
        .helmCalibrationWord = 0x2e6,
        .keyboardReads = {0x0806a6f0, 0x0806a700, 0x0806a710},
        .plxDeviceFind = 0x080cd5b0,
        .plxDeviceOpen = 0x0813bb80,
        .plxBarMap = 0x0813bfc0,
        .mcuExchange = 0x080cd4f0,
        .mcuProcess = 0x080cd250,
        .mcuIn = 0x08758340,
        .pcOut = 0x08754340,
        .pendingCommand = 0x0875c350,
        .pendingArgument = 0x0875c354,
        .sodObjectDepth = 0x0865dd08,
        .sodObjects = 0x280,
        .sodObjectSize = 0x5f8,
        .stage = 0x08796340,
    },
    {
        // Speed Driver (2003): the A27 board, its ROM in the dump's roms/.
        .crc32 = SPEED_DRIVER_IGS,
        .fileCrc32 = 0x2336a62b,
        .dataDirs = {"/sd", NULL},
        .frameWidth = 1280,
        .frameHeight = 480,
        .a27Rom = "roms/sd1-yueluo.bin",
    },
    {
        .crc32 = SPEED_DRIVER_2_IGS,
        .fileCrc32 = 0x2593d7f6,
        .dataDirs = {"/data", NULL},
        .frameWidth = 640,
        .frameHeight = 480,
        .sd2Rom = "roms/SD2_EPROM.dec.bin",
    },
    {
        // Speed Rider (M109): the cabinet runs from game/, beside data/.
        .crc32 = SPEED_RIDER_IGS,
        .fileCrc32 = 0x2770c27e,
        .frameWidth = 848,
        .frameHeight = 480,
        .sr1Rom = "roms/sr1_eprom.bin",
        .ogrePlugins = "../data/LinuxPlugins.cfg",
        .ogreAutoTextureMatrix = 0x400,
    },
    {
        // Speed Driver 3 (2010): the A36 board, its firmware in the dump's
        // roms/ (TeknoRaven's game/).
        .crc32 = SPEED_DRIVER_3_IGS,
        .fileCrc32 = 0x760014aa,
        .arguments = "24836 7056 3728",
        .frameWidth = 1360,
        .frameHeight = 768,
        .a36Rom = "roms/sd3_eprom.bin",
        .a36Idle = {0x10002798, 0x100027ac},
        .a36Loader = {0x08fffc00, 0x09000310, 0x08fffbc0, 0x08fffc70},
        .a36ReplyPublish = 0x08b9ccb1,
        .oisScanJoys = 0x08ccb0c0,
        .luabindGetErrorCallback = 0x08cbaea0,
        .luaToLString = 0x08c81fe0,
        .luaPCall = 0x08c80000,
        .serialIo = {0x08aa8e80, 0x08aa9390, 0x08aaa8b0, 0x08aa9690},
        .ogreAutoTextureMatrix = 0x4c0,
    },
    {
        // Speed Rider 2: the A36 board, run from TeknoRaven's game/bin_linux
        // beside ../data; its decrypted firmware in ../roms.
        .crc32 = SPEED_RIDER_2_IGS,
        .fileCrc32 = 0xf10a20c0,
        .a36Rom = "../roms/sr2_eprom_final.bin",
        .a36Loader = {0x09091250, 0x09091490, 0x090912d0, 0x09091b90},
        .a36ReplyPublish = 0x0890fc59,
        .a36Idle = {0x10002e0c, 0x10002e5c},
        .a36Services = 0x0200f000,
        .a36Stm32Usart = 2,
        // Lean; a brake, the throttle, a brake, which read 1023 released.
        .a36Stm32Pots = {{12, 512, 1012}, {1023, 0, 0}, {1023, 0, 0}, {1023, 0, 0}},
        .a36Stm32Throttle = 2,
        .ogrePlugins = "../data/general/plugins.cfg",
        // found from the scripts (SD3's key, recovered from SR2's Lua)
        .a36CipherMask = {0xf2, 0x14, 0x52, 0x80, 0x62, 0x8f, 0x0d, 0x3a,
                          0xb5, 0xf0, 0x14, 0xe8, 0xe5, 0x06, 0xb8, 0x38},
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
