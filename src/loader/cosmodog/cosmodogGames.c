// Cosmodog game descriptors (see cosmodogGame.h).

#include <stddef.h>

#include "cosmodogGame.h"
#include "../config/config.h"

static const CosmodogGame games[] = {
    {
        // Rockin' Bowl-O-Rama gold (bowlgold, its 2.2.1).
        .crc32 = ROCKIN_BOWL_O_RAMA_COSMODOG,
        .fileCrc32 = 0x7db5f008,
        .rootPath = "/bowl",
        .dataPath = "/data",
        .ioInit = 0x08092790,
        .sendMessage = 0x08092b10,
        .getMessage = 0x08092e50,
        .sha1 = 0x0804edd0,
        .convertHash = 0x0804c610,
        .dataValid = 0x080713d0,
    },
};

const CosmodogGame *cosmodogGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const CosmodogGame *cosmodogGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
