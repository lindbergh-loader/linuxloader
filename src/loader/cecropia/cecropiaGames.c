// Cecropia game descriptors (see cecropiaGame.h).

#include <stddef.h>

#include "cecropiaGame.h"
#include "../config/config.h"

static const CecropiaGame games[] = {
    {
        // The Act, release009g (12/12/2006).
        .crc32 = THE_ACT_CECROPIA,
        .fileCrc32 = 0xe98156f6,
        .statePath = "/var/decaf/state.db",
        .stateDir = "var",
        .movieCtor = 0x0806d190,
        .gibOpen = 0x0808d770,
        .gibClose = 0x0808d010,
        .gibReset = 0x0808d960,
        .gibChallenge = 0x0808e880,
        .gibGetVersion = 0x0808ea10,
        .gibGetBoardId = 0x0808f080,
        .getActiveControlValue = 0x08060950,
        .knobBackwards = 0x04,
        .knobValue = 0x08,
        .knobMin = 0x18,
        .knobMax = 0x1c,
        .knobRelative = 0x20,
        .controllerGetInstance = 0x08056ad0,
        .controllerHandleEvent = 0x08055f30,
        .eventCoin = 0x14,
        .eventButton = 0x15,
    },
};

const CecropiaGame *cecropiaGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const CecropiaGame *cecropiaGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
