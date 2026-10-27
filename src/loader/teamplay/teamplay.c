// Teamplay games (see teamplay.h): their cabinet's paths, and the checks
// that keep a game from starting off the cabinet.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#include "teamplay.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

extern uint32_t gId;
extern uint32_t partialElfCrc;

static char gameDir[PATH_MAX];
static char procVersion[64];
static char procCpuinfo[64];
static int soundDaemon;

// The game, or the game whose sound daemon this process is: the daemon is
// no game the loader knows (gId is UNKNOWN), its code's CRC tells it.
const TeamplayGame *teamplayCurrentGame(void)
{
    static uint32_t cachedId;
    static const TeamplayGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = teamplayGetGame(gId);
        soundDaemon = !game && (game = teamplayGetGameBySoundDaemonCrc(partialElfCrc)) != NULL;
    }
    return game;
}

int isTeamplayGame(void)
{
    return teamplayCurrentGame() != NULL;
}

// prefix, or prefix/..., in the game's directory.
static const char *underDir(const char *path, const char *prefix, char *buf, size_t size)
{
    size_t n = strlen(prefix);

    if (strncmp(path, prefix, n) || (path[n] != '/' && path[n] != '\0'))
        return NULL;
    snprintf(buf, size, "%s%s", gameDir, path + n);
    return buf;
}

// alias->from, or alias->from/..., in alias->to under the game's directory.
static const char *underAlias(const char *path, const TeamplayRootAlias *alias, char *buf, size_t size)
{
    size_t n = strlen(alias->from);

    if (strncmp(path, alias->from, n) || (path[n] != '/' && path[n] != '\0'))
        return NULL;
    snprintf(buf, size, "%s/%s%s", gameDir, alias->to, path + n);
    return buf;
}

const char *teamplayRedirectPath(const char *path, char *buf, size_t size)
{
    const TeamplayGame *g = teamplayCurrentGame();
    const char *mapped;

    if (!g || !path || !gameDir[0])
        return path;
    if (procVersion[0] && !strcmp(path, "/proc/version"))
        return procVersion;
    if (procCpuinfo[0] && !strcmp(path, "/proc/cpuinfo"))
        return procCpuinfo;
    if ((mapped = underDir(path, g->rootPath, buf, size)) || (mapped = underDir(path, g->dataPath, buf, size)))
        return mapped;
    for (const TeamplayRootAlias *a = g->rootAliases; a && a->from; a++)
        if ((mapped = underAlias(path, a, buf, size)))
            return mapped;
    return path;
}

int teamplayDropCommand(const char *command)
{
    return isTeamplayGame() && command && !strncmp(command, "mixer ", 6);
}

static int one(void)
{
    return 1;
}

static double nowSeconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// The TSC's rate in MHz, over 100 ms: a game that counts time in TSC ticks
// divides them by its CPU's "cpu MHz".
static double tscMHz(void)
{
    struct timespec delay = {0, 100000000};
    double start = nowSeconds();
    unsigned long long ticks = __rdtsc();

    nanosleep(&delay, NULL);
    ticks = __rdtsc() - ticks;
    return ticks / (nowSeconds() - start) / 1e6;
}

// A file of our own, in memory, holding text: its /proc/self/fd path in
// path, or an empty one.
static void memoryFile(const char *name, const char *text, char *path, size_t size)
{
    int fd = memfd_create(name, 0);

    path[0] = '\0';
    if (fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text))
        snprintf(path, size, "/proc/self/fd/%d", fd);
    else if (fd >= 0)
        close(fd);
}

int teamplayInit(void)
{
    const TeamplayGame *g = teamplayCurrentGame();

    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';

    // The sound daemon only needs the cabinet's paths.
    if (soundDaemon)
        return 0;

    // The kernel the game was released with.
    if (g->kernelVersion)
    {
        memoryFile("teamplay-proc-version", g->kernelVersion, procVersion, sizeof(procVersion));
        if (!procVersion[0])
            log_warn("Teamplay: no /proc/version of the cabinet, games will not start");
    }

    // The cabinet's CPU, at the host's TSC rate.
    if (g->cpuinfo)
    {
        char cpuinfo[1024];
        snprintf(cpuinfo, sizeof(cpuinfo), g->cpuinfo, tscMHz());
        memoryFile("teamplay-proc-cpuinfo", cpuinfo, procCpuinfo, sizeof(procCpuinfo));
        if (!procCpuinfo[0])
            log_warn("Teamplay: no /proc/cpuinfo of the cabinet, the game's clock will be wrong");
    }
    if (g->cpuRangeCheck)
        patchMemoryFromString(g->cpuRangeCheck, "eb");

    // No iButton and another disk: the checks pass (on Crossfire they gate
    // the start of a game, "IB invalid" at boot; Police Trainer 2 exits
    // without /dev/p37c).
    if (g->iButtonCheck)
        detourFunction(g->iButtonCheck, one);
    if (g->diskSerialCheck)
        detourFunction(g->diskSerialCheck, one);
    return 0;
}
