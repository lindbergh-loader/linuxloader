// Namco N2 game descriptors (see namcoN2Game.h).

#include <stddef.h>

#include "namcoN2Game.h"
#include "../config/config.h"

static const NamcoN2Game games[] = {
    {
        // Wangan Midnight Maximum Tune 3 (export, PROJECT V337, Jul 17 2007).
        .crc32 = WMMT3_N2,
        .fileCrc32 = 0x41abda0c,
        .revision = "WM3100",
        .jvsDevice = "/dev/ttyM3",
        .kickbackDevice = "/dev/ttyM1",
        .cardDevice = "/dev/ttyM2",
        .linkSearchMov = 0x084a2607, // movl $0x4650,0xc(%eax)
        .ffb = {.effectsField = 0x3d, .centerOffsetField = 0x20, .springRange = 254, .viscosityRange = 254, .reflectRange = 40},
    },
    {
        // Wangan Midnight Maximum Tune 3DX+ (PROJECT V386, Mar 9 2010).
        .crc32 = WMMT3DXPLUS_N2,
        .fileCrc32 = 0x509a97c4,
        .revision = "W3P",
        .jvsDevice = "/dev/ttyM3",
        .kickbackDevice = "/dev/ttyM1",
        .cardDevice = "/dev/ttyM2",
        .linkSearchMov = 0x088d848f, // movl $0x4650,0xc(%edx)
        .ffb = {.effectsField = 0x3d, .centerOffsetField = 0x20, .springRange = 254, .viscosityRange = 254, .reflectRange = 40},
    },
    {
        // Counter Strike NEO (csneo2): unlike the Wangan titles (a Namco
        // clSystemN2 app), this is a Valve GoldSrc HLDS (hlds_i486 launcher
        // dlopening engine_i486.so, the czero mod) on the N2 board. The
        // Wangan hooks (clSystemN2, adm*, gRomInfo, Alchemy) are absent, so
        // only the GoldSrc/HASP-specific install applies (namcoN2Csneo.c).
        .crc32 = CSNEO_N2,
        .fileCrc32 = 0x0,
        .revision = "",
        .arguments = "-game czero -console -norestart",
        .workDir = "csneo2/linux",
    },
};

const NamcoN2Game *namcoN2GetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const NamcoN2Game *namcoN2GetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
