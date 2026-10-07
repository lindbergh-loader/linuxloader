// Namco N2 game descriptors (see namcoN2Game.h).

#include <stddef.h>

#include "namcoN2Game.h"
#include "../config/config.h"

static const NamcoN2Game games[] = {
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
