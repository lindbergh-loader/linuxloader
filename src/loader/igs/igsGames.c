// IGS game descriptors (see igsGame.h).

#include <stddef.h>

#include "igsGame.h"
#include "../config/config.h"

// Speed Driver 4's cipher masks, one for each 64 KiB of a file (from the
// scripts; local_rank.lua.enc, the largest, has 16).
static const uint8_t sd4ChunkMasks[][16] = {
    {0xd0, 0x5e, 0x9a, 0xbf, 0xd7, 0xd2, 0xb8, 0x15, 0x92, 0x1d, 0x58, 0x85, 0x63, 0xb0, 0xe4, 0x9b},
    {0x75, 0xe5, 0x99, 0xad, 0xde, 0x6a, 0x1e, 0x96, 0x0c, 0xb5, 0x80, 0x2b, 0x46, 0x18, 0xde, 0x87},
    {0x46, 0xca, 0xd5, 0xa4, 0x52, 0xdf, 0x15, 0x1a, 0x12, 0xb8, 0xea, 0x4d, 0x0d, 0xef, 0x23, 0x3a},
    {0x46, 0x87, 0x22, 0x92, 0x81, 0xe3, 0x6e, 0x56, 0x57, 0x0b, 0xde, 0x60, 0x1e, 0x5b, 0xe2, 0xbf},
    {0x0a, 0x46, 0xef, 0xc3, 0x26, 0xd3, 0xe1, 0xc7, 0x3d, 0x38, 0x1d, 0xfe, 0x91, 0x06, 0x51, 0x9e},
    {0x11, 0x28, 0x7e, 0xb9, 0xc4, 0xfd, 0x2f, 0x42, 0x2e, 0xeb, 0x87, 0xd7, 0x85, 0xdd, 0x9a, 0x6f},
    {0xfb, 0x0c, 0x3a, 0xfd, 0xdf, 0xec, 0xfa, 0xfa, 0x60, 0x2b, 0x07, 0x04, 0x2c, 0xb4, 0xe8, 0x1d},
    {0xdb, 0x30, 0xcf, 0x72, 0x5d, 0xc1, 0x58, 0x9e, 0xcc, 0x07, 0xb4, 0x5c, 0x0f, 0xd6, 0x35, 0x00},
    {0xde, 0x0f, 0xe4, 0xc7, 0xfd, 0x5b, 0xc6, 0x27, 0xb4, 0xbb, 0x85, 0xc8, 0x29, 0x50, 0x6d, 0xec},
    {0x09, 0x2b, 0xae, 0x00, 0xc8, 0x90, 0xec, 0x18, 0x27, 0x26, 0x15, 0xb9, 0xcb, 0x62, 0x6d, 0x7e},
    {0x2a, 0xae, 0xb3, 0xfa, 0x18, 0xd3, 0xc1, 0x58, 0x8f, 0xb9, 0x45, 0xd9, 0x6e, 0xc1, 0x7a, 0x20},
    {0x07, 0x2e, 0x2d, 0xc3, 0xa8, 0x63, 0xac, 0x5a, 0x95, 0x30, 0x70, 0x1f, 0x63, 0xcf, 0x4e, 0xa2},
    {0x5a, 0x7e, 0x22, 0x05, 0xe7, 0xa6, 0x50, 0xe0, 0x38, 0x74, 0x30, 0x34, 0x66, 0x45, 0x26, 0xe0},
    {0x23, 0x00, 0x2a, 0xef, 0xc6, 0xcc, 0x6d, 0x6d, 0x9b, 0x02, 0x05, 0xe9, 0xeb, 0x56, 0xd0, 0xc6},
    {0x33, 0xb0, 0x2d, 0xe4, 0xc1, 0x71, 0x8e, 0xbb, 0x0f, 0x7e, 0x77, 0x7c, 0xbf, 0x10, 0xd8, 0x9c},
    {0x38, 0x07, 0x82, 0x04, 0xdc, 0xd6, 0x19, 0xd4, 0x1c, 0x71, 0x25, 0x96, 0x0f, 0xe4, 0x8d, 0x26},
};

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
        // Speed Driver 4 (olympus, UPX unpacked): Speed Driver 3's cabinet
        // and A36 board; its firmware, embedded in olympus, decrypted to
        // game/roms (tools/igs036-firmware.py).
        .crc32 = SPEED_DRIVER_4_IGS,
        .fileCrc32 = 0x7c88fcdb,
        .arguments = "24636 9344 2016",
        .frameWidth = 1366,
        .frameHeight = 768,
        .a36Rom = "roms/sd4_eprom.bin",
        .a36Loader = {0x088bfa40, 0x088bfad0, 0x088bfa00, 0x088bfca0},
        .a36Idle = {0x100029f0, 0x10002a04},
        .a36ReplyPublish = 0x08719301,
        // Speed Rider 2's internal-ROM services, another SRAM table
        .a36Services = 0x0200f300,
        .oisScanJoys = 0x08808500,
        .luabindGetErrorCallback = 0x087f7940,
        .luaToLString = 0x08792600,
        .luaPCall = 0x0878fb50,
        .serialIo = {0x08611940, 0x08611ad0, 0x086154e0, 0x08611840},
        // found from the scripts (frequency analysis, as Speed Rider 2's)
        .a36CipherMask = {0xd0, 0x5e, 0x9a, 0xbf, 0xd7, 0xd2, 0xb8, 0x15,
                          0x92, 0x1d, 0x58, 0x85, 0x63, 0xb0, 0xe4, 0x9b},
        .a36CipherChunkMasks = sd4ChunkMasks,
        .a36CipherChunks = sizeof(sd4ChunkMasks) / sizeof(sd4ChunkMasks[0]),
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
