// Launcher side of Namco N2 support.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "namcoN2.h"

// The cabinet command line of the N2 games that take one (the arguments and
// working directory in their descriptor). The launcher runs the game's own
// ELF from the game's root; this appends the arguments (unless a test-mode
// flag is already there).
int namcoN2PrepareCommand(char *command, size_t size, uint32_t crc32)
{
    const NamcoN2Game *g = namcoN2GetGame(crc32);
    char copy[PATH_MAX * 2];

    if (!g || !g->arguments)
        return 0;
    // " -t" is the loader's test-mode flag, not the game's: leave it alone.
    if (strrchr(command, ' ') && !strcmp(strrchr(command, ' '), " -t"))
        return 0;
    snprintf(copy, sizeof(copy), "%s %s", command, g->arguments);
    if (strlen(copy) >= size)
        return 0;
    strcpy(command, copy);
    return 1;
}
