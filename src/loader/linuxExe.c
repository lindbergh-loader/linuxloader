#ifdef __linux__
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log/log.h"
#include "mainShared.h"
#include "loader64/game64.h"

uint32_t partialElfCrc = 0;

// A 64-bit game (game64.h) is linuxloader64's, beside this one: started in
// our place, with the same arguments.
static void handOff64(int argc, char *argv[])
{
    const char *gameDir = ".";
    char self[PATH_MAX], loader64[PATH_MAX + 32], *slash;
    ssize_t n;

    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--gamepath"))
            gameDir = argv[i + 1];
    if (game64Find(gameDir).kind == GAME64_NONE)
        return;
    if ((n = readlink("/proc/self/exe", self, sizeof(self) - 1)) <= 0)
        return;
    self[n] = 0;
    if ((slash = strrchr(self, '/')))
        *slash = 0;
    snprintf(loader64, sizeof(loader64), "%s/linuxloader64", self);
    log_info("64-bit game: starting %s", loader64);
    argv[0] = loader64;
    execv(loader64, argv);
    log_error("Could not start %s, a 64-bit game needs it.", loader64);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    char command[MAX_PATH_LENGTH] = {0};
    char originalDir[MAX_PATH_LENGTH] = {0};
    char gameELF[MAX_PATH_LENGTH] = {0};
    char libraryPath[MAX_PATH_LENGTH] = {0};

    handOff64(argc, argv);

    if (parseArgs(argc, argv, command, originalDir, gameELF, libraryPath) != PARSE_ARGS_SUCCESS)
        return EXIT_SUCCESS;

    log_info("Starting $ %s", command);

    int sysCmd = system(command);

    if (chdir(originalDir) != 0)
    {
        log_error("Could not return to the original directory.");
        return EXIT_FAILURE;
    }

    return sysCmd;
}
#endif
