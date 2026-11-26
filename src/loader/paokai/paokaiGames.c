// Paokai game descriptors (see paokaiGame.h).

#include <stddef.h>

#include "paokaiGame.h"
#include "../config/config.h"

static const PaokaiGame games[] = {
    {
        // Go Go Strike (bowling_usa, BOWLING E.1.0.0).
        .crc32 = GO_GO_STRIKE_PAOKAI,
        .fileCrc32 = 0x5356b0a1,
        .keyIoRead = 0x0806fbf0,
        .ps2MouseIdx = 0x080bb86c,
        .ps2MousePx = 0x080bbe40,
        .ps2MousePy = 0x080bc478,
    },
};

const PaokaiGame *paokaiGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const PaokaiGame *paokaiGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
