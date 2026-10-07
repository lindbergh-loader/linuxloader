#include "rtGame.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"

// SDL 1.2 key codes (engine input ids of the keyboard).
enum
{
    KEY_BACKSPACE = 8,
    KEY_RETURN = 13,
    KEY_UP = 273,
    KEY_DOWN = 274,
    KEY_F1 = 282,
    KEY_F2 = 283,
    KEY_KP0 = 256, // to KP9 = 265
    KEY_KP_PERIOD = 266,
    KEY_KP_MULTIPLY = 268,
    KEY_KP_ENTER = 271,
};

static const RtGame rtGames[] = {
};

const RtGame *rtGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(rtGames) / sizeof(rtGames[0]); i++)
        if (rtGames[i].crc32 == crc32)
            return &rtGames[i];
    return NULL;
}

const RtGame *rtGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(rtGames) / sizeof(rtGames[0]); i++)
        if ((rtGames[i].fileCrc32 && rtGames[i].fileCrc32 == fileCrc32) ||
            (rtGames[i].altFileCrc32 && rtGames[i].altFileCrc32 == fileCrc32))
            return &rtGames[i];
    return NULL;
}
