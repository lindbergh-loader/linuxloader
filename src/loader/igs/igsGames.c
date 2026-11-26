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

// Crazy Shoot's dongle file 0xfff5 (the layout of Fish Hunter SKL 2's,
// below): the packet keys its firmware sets (0x1000ad64) and its seed.
static const uint8_t crazyShootHasp[40] = {
    [0x10] = 0x63, 0x93, 0x97, 0x79, // 0x79979363
    [0x14] = 0x97, 0x78, 0x04, 0x02, // 0x02047897
    [0x18] = 0x41, 0x38, 0x00, 0x00, // 0x3841, the packets' checksum seed (0x1000af44)
    // what the board runs on the PC's command 0x30 (0x1000b838): its three
    // trampolines into the handlers it installs (0x10000244, 0x278, 0x298)
    [0x1c] = 0x44, 0x02, 0x00, 0x10,
    [0x20] = 0x78, 0x02, 0x00, 0x10,
    [0x24] = 0x98, 0x02, 0x00, 0x10,
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

// Percussion Master 3's chart keys (its board's table, AES::pbRecvAESKeyFrom36)
// and their masks: each chart (media/script_AES/*.enccut, its first 16 bytes
// the board's) starts "#------ BASE DATA", known plaintext for every key.
static const uint8_t pm3ChartKeys[10][2][16] = {
    {{0x4e, 0x67, 0xd5, 0x98, 0xe7, 0x3e, 0x6a, 0x6f, 0x67, 0x1d, 0x87, 0xcd, 0xa7, 0x13, 0xb2, 0x7c},
     {0x4c, 0x3c, 0x3e, 0x2c, 0x19, 0x38, 0xa7, 0xbc, 0xbd, 0x41, 0xf5, 0xb0, 0x10, 0xa7, 0x74, 0xa3}},
    {{0x33, 0x2d, 0x47, 0x3d, 0x27, 0x11, 0x02, 0x72, 0xee, 0xf7, 0xa5, 0x18, 0xe5, 0x1e, 0x4a, 0x62},
     {0x69, 0x9e, 0xe2, 0xc1, 0x6c, 0x03, 0x31, 0xec, 0xaf, 0x42, 0xa5, 0xd5, 0xc2, 0xde, 0x63, 0x20}},
    {{0x66, 0x11, 0x8f, 0xad, 0x67, 0x03, 0x12, 0x8c, 0x5a, 0x2d, 0xff, 0xb2, 0x5c, 0x72, 0x1e, 0x5e},
     {0x1b, 0x0e, 0x46, 0x07, 0xc4, 0x4a, 0x46, 0x83, 0x8c, 0xd3, 0xc1, 0x14, 0x72, 0xd2, 0x9f, 0x2b}},
    {{0x3e, 0x07, 0x55, 0x48, 0x07, 0xae, 0x5a, 0x1f, 0x3a, 0x9d, 0x3e, 0x22, 0x78, 0x91, 0x2e, 0x0e},
     {0x55, 0x8e, 0x5a, 0x8e, 0x8b, 0x7a, 0xa4, 0xd9, 0x78, 0xde, 0xc5, 0x84, 0x72, 0xbf, 0x08, 0x4a}},
    {{0x3a, 0x8d, 0x7e, 0x52, 0x77, 0xa2, 0xce, 0x3e, 0x9e, 0x68, 0x75, 0x98, 0xe6, 0xee, 0xca, 0x2f},
     {0x2b, 0xcb, 0xdd, 0xc0, 0x4a, 0xab, 0x19, 0xb4, 0x75, 0x72, 0xe5, 0x6c, 0x33, 0xdd, 0xc6, 0x4a}},
    {{0x45, 0x57, 0x85, 0x93, 0x07, 0x5e, 0x67, 0x5f, 0xc4, 0x7d, 0xf9, 0x32, 0x79, 0x02, 0x9e, 0x36},
     {0x25, 0x1b, 0x02, 0x07, 0x1d, 0x6f, 0xae, 0xf2, 0xa2, 0xd0, 0x82, 0xd4, 0x16, 0x57, 0x3c, 0xe3}},
    {{0xe7, 0x4d, 0x67, 0xce, 0xd7, 0x1b, 0xb3, 0x6c, 0x43, 0x62, 0x85, 0x38, 0xef, 0x3b, 0x2a, 0x66},
     {0x52, 0x9f, 0xa5, 0x01, 0x8c, 0x13, 0x25, 0xd4, 0x9f, 0xf1, 0xa4, 0xf0, 0xd3, 0xe4, 0xff, 0x92}},
    {{0x60, 0x8f, 0x87, 0x54, 0x65, 0x44, 0x75, 0x3d, 0x57, 0x5d, 0x87, 0xfd, 0x37, 0x13, 0x42, 0x74},
     {0x4c, 0xe8, 0xe6, 0xed, 0x4d, 0x0c, 0x37, 0x96, 0x96, 0xd0, 0xd4, 0xf5, 0x5c, 0xe7, 0x54, 0x8f}},
    {{0x62, 0xed, 0x67, 0xfd, 0xaf, 0x63, 0x22, 0xbc, 0xc2, 0xed, 0xf6, 0x3e, 0xcc, 0x62, 0x43, 0xae},
     {0xab, 0xdc, 0x8a, 0x67, 0xcc, 0x0a, 0x5d, 0xe4, 0x8d, 0xc4, 0x65, 0xd2, 0x77, 0xd2, 0xdb, 0xca}},
    {{0x60, 0x60, 0x3e, 0x59, 0x81, 0x94, 0x75, 0xd3, 0xc0, 0x0d, 0x7e, 0x92, 0x76, 0x99, 0x47, 0xce},
     {0x23, 0x9c, 0x12, 0xcb, 0xe7, 0xee, 0x34, 0x13, 0xe4, 0x4c, 0x4e, 0x23, 0x6b, 0xf3, 0x34, 0x86}},
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
        // Ocean Party (OceanParty, 2011): the E2100, its A36 board's firmware
        // embedded in the executable (0x08257c00, decrypted to roms/ by
        // tools/igs036-firmware.py; the game reads asic36.bin, then sends
        // the embedded one).
        .crc32 = OCEAN_PARTY_IGS,
        .fileCrc32 = 0x16a7235e,
        .rootPath = "/OceanParty",
        .dataDirs = {NULL},
        .a36Rom = "roms/oceanparty_eprom.bin",
        // the upload (P2A_LoaderAdv_iLoadData's of a buffer)
        .a36Loader = {0, 0, 0, 0x082152e0},
        .a36Sdl = 1,
        // the internal ROM's services' table (0x1001cb30 calls it)
        .a36Services = 0x0200f000,
        // all OFF: switch 1 the gold shark mode (ON: the overlord shark's)
        .a36E2100Dips = 1,
        // 65 MHz (the firmware's baud rates, 0x10014530)
        .a36TimerClock = 65000000,
        // its main loop polls (no idle loop): its time kept to the clock
        .a36CatchUp = 1,
        // the control board (RS485), on USART0
        .a36Stm32Usart = 1,
        .a36FishBoard = 4,
        // the assets' (the encrypted zero block, moved as the cipher's bits
        // are: the game asks for encryption)
        .a36CipherMask = {0x5b, 0xc9, 0x59, 0x14, 0x4a, 0xe9, 0x34, 0x11,
                          0x30, 0xbe, 0x78, 0x37, 0xef, 0xec, 0x78, 0x93},
    },
    {
        // Demon Hunter (zombie, M202CN, 2011): the E2100, its A36 board's
        // firmware embedded in the executable (0x08246be0, decrypted to
        // roms/; the asic36.bin beside it unused).
        .crc32 = DEMON_HUNTER_IGS,
        .fileCrc32 = 0xd3ab8aa8,
        .rootPath = "/Zombie",
        .dataDirs = {NULL},
        // its picture: 1280x720, whatever the screen
        .frameWidth = 1280,
        .frameHeight = 720,
        .a36AsksScreen = 1,
        .a36Rom = "roms/demonhunter_eprom.bin",
        // the upload (P2A_LoaderAdv_iLoadData's of a buffer)
        .a36Loader = {0, 0, 0, 0x0808a040},
        .a36Sdl = 1,
        // the internal ROM's services' table (0x1001236c)
        .a36Services = 0x0200f000,
        .a36TimerClock = 65000000,
        .a36CatchUp = 1,
        // the assets' (the encrypted zero block, moved as the cipher's bits
        // are: the game asks for encryption)
        .a36CipherMask = {0x42, 0x44, 0x1f, 0xc3, 0xdb, 0x08, 0xf3, 0x47,
                          0xe1, 0x25, 0xb1, 0xe6, 0xd1, 0xf1, 0xc3, 0x26},
        // its sound loader takes the size before its reader gives it (as
        // Fish Hunter SKL 2's): replaced
        .soundLoad = 0x080630f0,
        .soundSlot = 0x08062e10,
        .soundRead = 0x08066490,
        .soundTable = 0x083dea88,
        // the control board (RS485)
        .a36Stm32Usart = 2,
        .a36FishBoard = 5,
    },
    {
        // Scratch (SWIN, M101CN, 2011): the E2100, its A36 board's firmware
        // embedded in the executable (0x0817e900, decrypted to roms/; the
        // asic36.bin beside it unused); its Flash pictures and Ogg sounds
        // plain.
        .crc32 = SCRATCH_IGS,
        .fileCrc32 = 0x31b33103,
        .rootPath = "/Scratch",
        .dataDirs = {NULL},
        // its picture
        .frameWidth = 1920,
        .frameHeight = 1080,
        .a36Rom = "roms/scratch_eprom.bin",
        // the upload (P2A_LoaderAdv_iLoadData's of a buffer)
        .a36Loader = {0, 0, 0, 0x0814e620},
        .a36Sdl = 1,
        // its picture shader's "precision lowp float;"
        .glesPrecision = {0x0815ebb8},
        // the control board (RS485), Demon Hunter's (its fire button takes
        // a ticket: no free play)
        .a36Stm32Usart = 2,
        .a36FishBoard = 6,
        // its lamp board, on the PC's /dev/ttyS0
        .ttyZero = 1,
        // the internal ROM's services' table (0x1000f300)
        .a36Services = 0x0200f000,
        .a36TimerClock = 65000000,
        .a36CatchUp = 1,
    },
    {
        // Crazy Shoot (CrazyShoot, 2013): the E2100, a Unity 4.1 player run
        // from the dump's game/ (its ShootRope_Data also as
        // CrazyShoot_Data, the cabinet's link); its A36 board's firmware
        // (the plugin's A36ROM image, decrypted to roms/) and upload, and
        // the HASP dongle's API, in its plugin libPcShareram.so.
        .crc32 = CRAZY_SHOOT_IGS,
        .fileCrc32 = 0x0f8f5ede,
        .rootPath = "/CrazyShoot",
        .dataDirs = {NULL},
        .frameWidth = 1920,
        .frameHeight = 1080,
        .a36Rom = "roms/crazyshoot_eprom.bin",
        .a36Library = "libPcShareram.so",
        // its I/O board, on USART1
        .a36Stm32Usart = 2,
        .a36FishBoard = 8,
        // the internal ROM's services (Speed Driver 4's table)
        .a36Services = 0x0200f300,
        // the upload's header (the firmware checks it at start, 0x1000ac2c)
        .a36Project = "CrazyShootCN",
        .a36ProjectArea = "CrazyShootCN_E21",
        .a36ProjectVersion = 0x2e << 5 | 1,
        .haspFile = 0xfff5,
        .haspMemory = crazyShootHasp,
        .haspMemorySize = sizeof(crazyShootHasp),
    },
    {
        // Hero of Robots (child_linux, M100FA): the E2100, Ogre; its A36
        // board's firmware embedded in the executable (0x09d45d80, decrypted
        // to ../roms/), run from the dump's game/exe.
        .crc32 = HERO_OF_ROBOTS_IGS,
        .fileCrc32 = 0xd1319e55,
        .rootPath = "/Robots",
        .dataDirs = {NULL},
        .frameWidth = 1280,
        .frameHeight = 720,
        .a36Rom = "../roms/heroofrobots_eprom.bin",
        // the upload (P2A_Loader iInit, iReset, iUnInit, LoaderAdv
        // iLoadDataPointer, iWaitINT: the board's link set up before it)
        .a36Loader = {0x098ef8ba, 0x098efaf6, 0x098ef952, 0x098f098e, 0x098efed4},
        // its firmware's RAM: up to 0x20ff0a94
        .a36RamSize = 0x1000000,
        .a36ExtSramSize = 0x1000000,
        // its card reader: there, no card in it
        .casCard = 1,
        // the operator's chip (prepaid plays): there, with credit
        .casChip = {0x08cd9b40, 0x08cd6800},
        // its card dispenser and QR scanner, on serial ports
        .rs232Initialize = 0x089208f0,
        .rs232Dispenser = {0x0884a540, 0x0884b810},
        .rs232Qr = {0x08917a20, 0x08918df0},
        // its I/O board, on USART1 (the game's packets relayed)
        .a36Stm32Usart = 2,
        .a36FishBoard = 7,
        .a36Stm32ByteCycles = 8600,
        .a36Services = 0x0200f000,
        .loadingEnds = 0x088248c0,
        // (timers on the 20 MHz default: its watchdog's 10000 ticks outlast
        // the loading screen, its LED blinks at 1 Hz)
    },
    {
        // MuziBox (mb, TeknoRaven's package): the A36 board, run from game/
        // (its relative paths), its firmware in roms/.
        .crc32 = MUZIBOX_IGS,
        .fileCrc32 = 0x233690da,
        .a36Rom = "roms/muzibox_eprom.bin",
        // P2A_Loader iInit, iReset, iUnInit, LoaderAdv iLoadDataPointer (its
        // embedded firmware), the wait
        .a36Loader = {0x0847befa, 0x0847c136, 0x0847bf92, 0x0847cfce, 0x0847c514},
        .a36Sdl = 1,
        .a36FrameTurn = 1,
        .a36Offline = 1,
        // isDefaultStoreName (storeName from info/setting.cfg starts with
        // "MuziBox"): not, booting to attract
        .returnZero = 0x081d5a60,
        // its I/O board (switches, lamps) on USART1
        .a36Stm32Usart = 2,
        .a36FishBoard = 2,
        // Speed Rider 2's internal-ROM services and table
        .a36Services = 0x0200f000,
        // its scripts' (Lua 5.1 bytecode: from opening.mba's header), moved
        // as the cipher's bits are: the game asks for encryption
        .a36CipherMask = {0x04, 0x1b, 0x59, 0x25, 0xbb, 0xc4, 0x26, 0xdf,
                          0x50, 0x7f, 0x76, 0x15, 0xc4, 0x61, 0xec, 0x7c},
    },
    {
        // Percussion Master 3 (pm3, TeknoRaven's package): the A36 board,
        // Ogre 1.6.1; run from game/exe beside ../data, ../media and
        // ../rewrite, its firmware in game/roms.
        .crc32 = PERCUSSION_MASTER_3_IGS,
        .fileCrc32 = 0x841880de,
        .frameWidth = 1280,
        .frameHeight = 720,
        .a36Rom = "../roms/a36rom_fpga_decrypted.bin",
        // P2A_Loader iInit, iReset, iUnInit, LoaderAdv iLoadDataPointer (its
        // embedded firmware), iWaitINT
        .a36Loader = {0x0952797a, 0x09527bb6, 0x09527a12, 0x09528a4e, 0x09527f94},
        // Speed Rider 2's internal-ROM services and table
        .a36Services = 0x0200f000,
        .a36Stm32Usart = 2,
        .a36FishBoard = 3,
        // its song lists and charts' (SongList.enc, *.enccut: CSV text after
        // a 4-byte header; from the characters' frequencies), moved as the
        // cipher's bits are: the game asks for encryption
        .a36CipherMask = {0x44, 0x19, 0x74, 0x0c, 0x0b, 0x98, 0x27, 0xb4,
                          0xed, 0x10, 0xc5, 0xb4, 0x50, 0x2f, 0x64, 0x2a},
        .a36CipherKeys = pm3ChartKeys,
        .a36CipherKeyCount = 10,
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
