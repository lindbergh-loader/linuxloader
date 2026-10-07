// The g7 games' descriptors (see g7.h).
#include <stdio.h>
#include "g7.h"
#include "g7HaloImports.h"
#include "g7CentipedeImports.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const G7Game games[] = {
    {
        .name = "Halo: Fireteam Raven",
        .dynamic = 0x16908a8,
        .cabinetDir = "/pm/g7/halo",
        .envPrefix = "HALO",
        .imports = haloImports,
        .importCount = COUNT(haloImports),
        .hasp = {.login = 0xb2d630, .logout = 0xb2dd48, .encrypt = 0xb2dfd1, .decrypt = 0xb2e1aa, .free = 0xb2e391,
                 .sessionInfo = 0xb2f5ac, .read = 0xb2fd63, .write = 0xb2fe60},
        // Halo's four cabinets, as TeknoParrot names them (see g7Halo.c): the
        // single-screen 4 player one.
        .defaultCabinet = 3,
        // Command-line parser: "push %rbp; push %rbx; sub $0x18,%rsp"
        .parseArgs = 0x435560,
        .parseArgsPrologue = 6,
        .videoSettings = 0x1749f80,
        .mapPath = g7HaloMapPath,
        .install = g7HaloInstall,
    },
    {
        // Centipede Chaos, CENT_M70t v01.11 (game2, the dump of game).
        // Working: gameplay, the 3 players' controls, test menu.
        .name = "Centipede Chaos",
        .dynamic = 0x1570b08,
        .cabinetDir = "/pm/g7/centipede",
        .envPrefix = "CENT",
        .imports = centipedeImports,
        .importCount = COUNT(centipedeImports),
        .hasp = {.login = 0xa59bb0, .logout = 0xa59eb3, .encrypt = 0xa5a029, .decrypt = 0xa5a13b, .free = 0xa5a5bb,
                 .sessionInfo = 0xa5e824, .read = 0xa5f246, .write = 0xa5f397},
        .defaultCabinet = 1,
        .factorySetupDone = 1,
        .install = g7CentipedeInstall,
    },
};

const G7Game *g7Find(uintptr_t dynamic)
{
    for (size_t i = 0; i < COUNT(games); i++)
        if (games[i].dynamic == dynamic)
            return &games[i];
    return NULL;
}
