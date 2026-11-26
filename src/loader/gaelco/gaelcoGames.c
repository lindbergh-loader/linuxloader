// Gaelco game descriptors (see gaelcoGame.h).

#include <stddef.h>

#include "gaelcoGame.h"
#include "../config/config.h"

// Tokyo Cop's dump is its salidas directory, with the save partitions
// (save0, save1) in it.
static const GaelcoPathAlias tokyoCopPaths[] = {
    {"/home/joc2001/joc2001/salidas", "."},
    {"/home/save0", "save0"},
    {"/home/save1", "save1"},
    {NULL, NULL},
};

// Ring Riders' and Tuning Race's dumps are the cabinet's whole disk: the
// game runs from home/joc2001/<game>/salidas, three levels under /home
// (the host's /home stays the host's). Tuning Race's dump has its aux
// partition (/home/aux, where the cabinet runs it: its records and
// settings) at the disk's root.
static const GaelcoPathAlias diskPaths[] = {
    {"/home/joc2001", "../.."},
    {"/home/save0", "../../../save0"},
    {"/home/save1", "../../../save1"},
    {"/home/aux", "../../../.."},
    {NULL, NULL},
};

static const GaelcoGame games[] = {
    {
        // Tokyo Cop (gameport, the same as car in the dump).
        .crc32 = TOKYO_COP_GAELCO,
        .fileCrc32 = 0x7a518e3b,
        .paths = tokyoCopPaths,
        .frameWidth = 640,
        .frameHeight = 480,
        .io = GAELCO_IO_PCI_CONFIG,
        .controls = GAELCO_TOKYO_COP,
        // G3Dintf_mandos::IniControllers(int *, POTE *), its menus' and
        // games' too.
        .iniControllers = 0x080e6b90,
        .iniControllersPrologue = 5,
        // pote[4], which IniControllers is given; IniPotes empties it again
        .potentiometers = 0x08adabe0,
        .iniPotes = 0x0808c150,
        .iniPotesPrologue = 5,
        .potentiometerCount = 4,
        .potentiometerMargin = 7,
        // ProcesaPlataforma: jne past "estadoPlataforma = 9" (the switch
        // setting at 0x87938a4, switch.bin's 0x34)
        .noMotionJump = 0x080c027e,
    },
    {
        // Ring Riders (gameport, joc2003).
        .crc32 = RING_RIDERS_GAELCO,
        .fileCrc32 = 0x51ea930f,
        .paths = diskPaths,
        .frameWidth = 640,
        .frameHeight = 480,
        .io = GAELCO_IO_MEMORY,
        .controls = GAELCO_RING_RIDERS,
        // G3Dintf_mandos::IniControllers(int *, POTE *)
        .rs485GetString = 0x08107a6c,
        .platformInit = 0x080bf60c,
        .iniControllers = 0x08105f3a,
        .iniControllersPrologue = 6,
        .potentiometerCount = 5,
        .potentiometerMargin = 7,
        .alignStack = 1,
    },
    {
        // Gaelco Championship Tuning Race (car, joc2004).
        .crc32 = TUNING_RACE_GAELCO,
        .fileCrc32 = 0x6f1e5179,
        .paths = diskPaths,
        .workDir = "../../../..",
        .frameWidth = 720,
        .frameHeight = 540,
        .io = GAELCO_IO_MEMORY,
        .controls = GAELCO_TUNING_RACE,
        // Mandos::IniControllers(SwitchCoinSettings *), its ranges static
        .iniControllers = 0x08189440,
        .iniControllersPrologue = 9,
        .potentiometers = 0x0a009260,
        .potentiometerCount = 6,
        .potentiometerMargin = 3,
        .alignStack = 1,
    },
};

const GaelcoGame *gaelcoGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const GaelcoGame *gaelcoGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
