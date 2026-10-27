// Launcher side: dumps that lost their executable mode.
//
// Copies of a cabinet's disk may have lost the executables' mode (a dump
// made on another system): the kernel cannot start them. Those are run
// through the host's 32-bit dynamic linker ("/lib/ld-linux.so.2
// ./med_pball"), which LD_PRELOAD still applies to. For the Teamplay,
// Global VR and Roxor games, whose dumps come that way.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hostLinker.h"
#include "globalvr/gvrGame.h"
#include "log/log.h"
#include "roxor/roxorGame.h"
#include "teamplay/teamplayGame.h"

static const char *const hostLinkers[] = {
    "/lib/ld-linux.so.2",
    "/lib32/ld-linux.so.2",
    "/usr/lib32/ld-linux.so.2",
    "/usr/lib/i386-linux-gnu/ld-linux.so.2",
    NULL,
};

void hostLinkerPrepareCommand(char *command, size_t size, uint32_t fileCrc32)
{
    char elfPath[PATH_MAX], copy[PATH_MAX * 2];
    const char *space;

    if (!teamplayGetGameByFileCrc(fileCrc32) && !gvrGetGameByFileCrc(fileCrc32) && !roxorGetGameByFileCrc(fileCrc32))
        return;

    snprintf(elfPath, sizeof(elfPath), "%s", command);
    if ((space = strrchr(command, ' ')) && strcmp(space, " -t") == 0)
        elfPath[space - command] = '\0';
    if (access(elfPath, X_OK) == 0)
        return;

    for (const char *const *linker = hostLinkers; *linker; linker++)
    {
        if (access(*linker, X_OK) != 0)
            continue;
        snprintf(copy, sizeof(copy), "%s %s", *linker, command);
        if (strlen(copy) >= size)
            break;
        strcpy(command, copy);
        printf("%s is not executable, it runs through %s\n", elfPath, *linker);
        return;
    }
    log_error("No 32-bit dynamic linker found to run %s", elfPath);
}
