// Ghostbusters Arcade: enable its IR backend and handle an absent shader
// backup listed in the shipping integrity manifest.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../log/log.h"

static int (*inputInitOrig)(int flags);
static int (*checkFileOrig)(const char *path, void *buffer, int length);

static int inputInit(int flags)
{
    // This build requests 0x482 even though its default gun map needs IR.
    return inputInitOrig(flags | 4);
}

static int checkFile(const char *path, void *buffer, int length)
{
    static const char backup[] = "/pm/g6/ghostb/data/prod/shaders/shader7_sk/vert.gls.bak";
    char local[PATH_MAX];

    // Only the checksum-only request for this unused, missing backup is
    // exempt. A present backup and every runtime asset are still checked.
    if (path && !buffer && !strcmp(path, backup))
    {
        int n = snprintf(local, sizeof(local), "%s/%s", rtGameDir(), backup + 4);
        if (n > 0 && n < (int)sizeof(local) && access(local, F_OK) != 0 && errno == ENOENT)
            return 0;
    }
    return checkFileOrig(path, buffer, length);
}

void rtGbInstall(const RtGame *game)
{
    (void)game;
    inputInitOrig = rtTrampoline("InputInit", 6);
    if (!inputInitOrig || rtDetour("InputInit", inputInit) != 0)
        log_error("Ghostbusters: IR input backend could not be enabled");
    // push ebp; mov esp,ebp; sub $0x1d8,esp (9 whole bytes).
    checkFileOrig = rtTrampoline("CheckFileIntegrity", 9);
    if (!checkFileOrig || rtDetour("CheckFileIntegrity", checkFile) != 0)
        log_error("Ghostbusters: missing shader backup exception could not be installed");
}
