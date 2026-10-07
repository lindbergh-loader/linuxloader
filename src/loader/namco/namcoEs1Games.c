// Namco ES1 game descriptors (see namcoEs1Game.h).

#include "namcoEs1Game.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"

static const NamcoEs1Game games[] = {
    {
        // Nirin (NRN100-4-NA-DAT0-A37)
        .crc32 = NIRIN_ES1,
        .fileCrc32 = 0x6d238b08,
        .jvsDevice = "/dev/ttyS2",
        .haspLogin = 0x083455c0,
        .haspLogout = 0x08345660,
        .haspRead = 0x083463b8,
        .haspDecrypt = 0x08345838,
        .haspFeature = 0xffff0000, // clHASP::check()
        .dongleSerial = "280911000001",
        .calibration = 0x09126698,
        .mesaOnNvidia = 1,
    },
};

const NamcoEs1Game *namcoEs1GetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const NamcoEs1Game *namcoEs1GetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
