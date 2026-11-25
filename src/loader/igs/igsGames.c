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

// Fish Hunter SKL 2's dongle file 0xfff5 as the game uses it: the FPGA user
// key (0-15), the PC's and the board's first packet keys (0x10, 0x14), the
// packets' checksum seed (0x18), the three words the board answers
// (0x1c-0x24).
static const uint8_t fishHunterHasp[40] = {
    [0x10] = 0x64, 0xa4, 0x1a, 0x0d, // 0x0d1aa464, the firmware's own
    [0x14] = 0x63, 0xb4, 0x3f, 0x54, // 0x543fb463, the firmware's own
    [0x18] = 0x51, 0x0e, 0x80, 0x11, // 0x11800e51, the firmware's own
    // what the board answers (packet 46)
    [0x1c] = 0x94, 0x03, 0x00, 0x10, // 0x10000394
    [0x20] = 0xb4, 0x03, 0x00, 0x10, // 0x100003b4
    [0x24] = 0xd4, 0x03, 0x00, 0x10, // 0x100003d4
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
        // Power Truck (pt): Speed Driver 3's cabinet and A36 board, run from
        // TeknoRaven's game/ beside ../data; its firmware in roms/.
        .crc32 = POWER_TRUCK_IGS,
        .fileCrc32 = 0x699ae32a,
        .arguments = "25020 7056 10080",
        .frameWidth = 1366,
        .frameHeight = 768,
        .a36Rom = "roms/powertruck_eprom.bin",
        .a36Loader = {0x098b51f0, 0x098b5430, 0x098b5270, 0x098b5b30},
        .a36Idle = {0x1000288c, 0x100028a0},
        // Speed Rider 2's internal-ROM services and table
        .a36Services = 0x0200f000,
        .oisScanJoys = 0x09580ee0,
        .luabindGetErrorCallback = 0x09570b70,
        .luaToLString = 0x09551820,
        .luaPCall = 0x0954f820,
        .luabindByteCastFailed = 0x08daf9c4,
        .serialIo = {0x0930e2c6, 0x0930e0fc, 0x0931042c, 0x0930dab4},
        .serialIoGearLever = 1,
        .ogreAutoTextureMatrix = 0x4c0,
        .ogreKeepArchives = 1,
        // found from the scripts (frequency analysis, as Speed Rider 2's)
        .a36CipherMask = {0xe6, 0x88, 0x37, 0xed, 0x73, 0x50, 0xdc, 0xfe,
                          0x4f, 0x16, 0xef, 0x54, 0xd2, 0x3b, 0x10, 0x7e},
    },
    {
        // Power Truck S: Power Truck's newer version, its pt repaired
        // (tools/igs-pts-repair.py: TeknoRaven stripped its imports); its
        // firmware another build, with Speed Driver 4's internal-ROM table.
        .crc32 = POWER_TRUCK_S_IGS,
        .fileCrc32 = 0xb450a566,
        .arguments = "24828 7056 10080",
        .frameWidth = 1366,
        .frameHeight = 768,
        .a36Rom = "roms/pts_eprom.bin",
        .a36Loader = {0x098b8a30, 0x098b8f60, 0x098b8aa0, 0x098b9900},
        .a36Idle = {0x1000122c, 0x10001234},
        .a36Services = 0x0200f300,
        .oisScanJoys = 0x095845b0,
        .luabindGetErrorCallback = 0x09574240,
        .luaToLString = 0x09554ef0,
        .luaPCall = 0x09552ef0,
        .luabindByteCastFailed = 0x08db5d08,
        .serialIo = {0x09310bae, 0x093109e4, 0x09312d14, 0x0931039c},
        .serialIoGearLever = 1,
        .ogreAutoTextureMatrix = 0x4c0,
        // found from the scripts (frequency analysis, as Power Truck's)
        .a36CipherMask = {0xd6, 0x9c, 0xa4, 0x6f, 0x70, 0x38, 0xc5, 0x5f,
                          0xd6, 0x2b, 0x3b, 0x94, 0x13, 0x21, 0xb6, 0x3c},
    },
    {
        // Percussion Master 2008 (PM2008v2, TeknoRaven's package): an IGS036
        // behind a PLX9030 (its PC side's names say A27), its decrypted
        // firmware and its internal ROM; run from game/.
        .crc32 = PERCUSSION_MASTER_2008_IGS,
        .fileCrc32 = 0x8567b838,
        // its SDL 1.2's SDL_SetVideoMode (full screen: the X server
        // switched to 640x480, and left so)
        .setVideoMode = 0x08121830,
        .frameWidth = 640,
        .frameHeight = 480,
        .a36Rom = "roms/PM2008_EPROM.dec.bin",
        .a36InternalRom = "roms/pm2008_internal_rom.bin",
        // its PLX SDK: PlxPciDeviceOpen, PlxPciBarMap; chip type,
        // PlxPciDeviceClose, the interrupt and register calls; the doorbell
        .a36PlxOpen = 0x08161440,
        .a36PlxBarMap = 0x08161880,
        .a36PlxDone = {0x08160e30, 0x08160f40, 0x081615a0, 0x08161e20},
        .a36PlxDoorbell = 0x080983bd,
    },
    {
        // Rock Fever 5 (RF5v2): the same board and cabinet; its doorbell
        // writes 1. Its package has no internal ROM: Percussion Master
        // 2008's.
        .crc32 = ROCK_FEVER_5_IGS,
        .fileCrc32 = 0x39386fac,
        .setVideoMode = 0x08150c70,
        .frameWidth = 640,
        .frameHeight = 480,
        .a36Rom = "roms/RF5v2_EPROM.bin",
        .a36PlxOpen = 0x081903b0,
        .a36PlxBarMap = 0x081907f0,
        .a36PlxDone = {0x0818fda0, 0x0818feb0, 0x08190510, 0x08190d90, 0x08190e00},
        .a36PlxDoorbell = 0x080978d7,
        .a36PlxFind = 0x080976b0,
        .a36Rf5Services = 1,
        .a36PlxDoorbellByte = 1,
    },
    {
        // Fish Hunter SKL 2 (FHSKLv2-8p, 2012): the E2200, its A36 board's
        // firmware embedded in the executable (decrypted to roms/ by
        // tools/igs036-firmware.py), its dongle's API linked in.
        .crc32 = FISH_HUNTER_SKL2_IGS,
        .fileCrc32 = 0x12dbf836,
        .rootPath = "/FishSKLv2-8p",
        .dataDirs = {NULL},
        // its picture, scaled to a larger screen
        .frameWidth = 1280,
        .frameHeight = 720,
        .a36Rom = "roms/fhskl2_eprom.bin",
        // the upload (P2A_LoaderAdv_iLoadData's of a buffer)
        .a36Loader = {0, 0, 0, 0x08095940},
        .a36Sdl = 1,
        // Speed Driver 4's internal-ROM services and table
        .a36Services = 0x0200f300,
        // the control board, on USART1
        .a36Stm32Usart = 2,
        .a36FishBoard = 1,
        // the upload's header (the firmware checks it at start)
        .a36Project = "FishHunterSKL28P",
        .a36ProjectArea = "FishSKL28P_E2200",
        .a36ProjectVersion = 0x24 << 5 | 2,
        // the assets' (the mask from their known plaintext, moved as the
        // cipher's bits are: the game asks for encryption)
        .a36CipherMask = {0xe4, 0xe1, 0x5d, 0x82, 0xe5, 0xa6, 0x01, 0x28,
                          0xfe, 0x7b, 0x75, 0x1a, 0x34, 0xd1, 0xd7, 0xbd},
        .soundLoad = 0x08064450,
        .soundSlot = 0x08064180,
        .soundRead = 0x080678c0,
        .soundTable = 0x084800c8,
        .haspApi = {0x08214880, 0x08214820, 0x08213750, 0x082136a0, 0x08214700, 0x08214780},
        .haspFile = 0xfff5,
        .haspMemory = fishHunterHasp,
        .haspMemorySize = sizeof(fishHunterHasp),
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
