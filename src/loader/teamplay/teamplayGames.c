// Teamplay game descriptors (see teamplayGame.h).

#include <stddef.h>

#include "teamplayGame.h"
#include "../config/config.h"

static const TeamplayGame games[] = {
};

const TeamplayGame *teamplayGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const TeamplayGame *teamplayGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}

const TeamplayGame *teamplayGetGameBySoundDaemonCrc(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].soundDaemonCrc32 && games[i].soundDaemonCrc32 == crc32)
            return &games[i];
    return NULL;
}
