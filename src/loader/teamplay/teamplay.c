// Teamplay games (see teamplay.h): their cabinet's paths, and the checks
// that keep a game from starting off the cabinet.

#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#include "teamplay.h"
#include "../hostLinker.h"
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

// path, a missing file of the game's directory, under the name of a file
// there that differs only in case: Star Trek: Voyager's dump has names in
// another case than the game's lists (Hlk_advance_hit_050 is
// hlk_advance_hit_050.bhs).
static const char *anyCase(char *path)
{
    char *slash = strrchr(path, '/');
    struct stat st;
    DIR *dir;
    struct dirent *e;

    if (!slash || stat(path, &st) == 0)
        return path;
    *slash = '\0';
    dir = opendir(path);
    *slash = '/';
    if (!dir)
        return path;
    while ((e = readdir(dir)))
        if (!strcasecmp(e->d_name, slash + 1))
        {
            strcpy(slash + 1, e->d_name);
            break;
        }
    closedir(dir);
    return path;
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
        return anyCase(buf);
    for (const TeamplayRootAlias *a = g->rootAliases; a && a->from; a++)
        if ((mapped = underAlias(path, a, buf, size)))
            return mapped;
    return path;
}

const char *teamplayRedirectCommand(const char *command, char *buf, size_t size)
{
    const char *linker;
    char program[PATH_MAX];
    size_t n;

    if (!isTeamplayGame() || !command || strncmp(command, "./", 2))
        return command;
    n = strcspn(command, " \t");
    if (n >= sizeof(program))
        return command;
    memcpy(program, command, n);
    program[n] = '\0';
    if (access(program, X_OK) == 0 || access(program, R_OK) != 0 || !(linker = hostLinkerPath()))
        return command;
    if ((size_t)snprintf(buf, size, "%s %s", linker, command) >= size)
        return command;
    return buf;
}

// The program the cabinet's driver would run next (see teamplayGame.h).
static const char *handOffProgram;

// The game's exit: with its handOffStatus, its handOffProgram is started in
// its place once the process is done exiting (the game's atexit handlers
// have saved its settings and audits). Exits of the loader's own (a quit
// key) are exits.
void exit(int status)
{
    static void (*real)(int) __attribute__((noreturn));
    const TeamplayGame *g = teamplayCurrentGame();

    if (!real)
        real = (void (*)(int))dlsym(RTLD_NEXT, "exit");
    if (g && !soundDaemon && g->handOffProgram && (uintptr_t)__builtin_return_address(0) < 0x10000000 &&
        (g->handOffStatus == -1 || g->handOffStatus == status))
        handOffProgram = g->handOffProgram;
    real(status);
}

// After the game's atexit handlers: the next program, in this process (the
// launcher waits on it), through the host's dynamic linker when the dump
// lost its executable mode. The game's descriptors go: the sound daemon
// it fed sees its end and exits, the next program starts its own.
__attribute__((destructor)) static void teamplayHandOff(void)
{
    const char *linker = hostLinkerPath();

    if (!handOffProgram)
        return;
    printf("Teamplay: running %s\n", handOffProgram);
    fflush(stdout);
    for (int fd = 3; fd < 1024; fd++)
        close(fd);
    if (access(handOffProgram, X_OK) == 0)
        execl(handOffProgram, handOffProgram, (char *)NULL);
    else if (linker)
        execl(linker, linker, handOffProgram, (char *)NULL);
    log_error("Teamplay: cannot run %s", handOffProgram);
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

// The vblank interrupts' count at 60 Hz. The game waits on it in a busy
// loop until enough frames have gone by: a count the caller already has is
// the next one, once it is due.
static int vblankCount(void)
{
    static double start;
    static int last = -1;
    double now = nowSeconds();
    int count;

    if (!start)
        start = now;
    count = (int)((now - start) * 60.0);
    if (count == last)
    {
        double wait = start + (count + 1) / 60.0 - now;
        struct timespec delay = {0, wait > 0 ? (long)(wait * 1e9) : 0};
        nanosleep(&delay, NULL);
        count++;
    }
    last = count;
    return count;
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

    // No nvidia driver's interrupts to pace the frames.
    if (g->vblankCount)
        detourFunction(g->vblankCount, vblankCount);

    // No iButton and another disk: the checks pass (on Crossfire they gate
    // the start of a game, "IB invalid" at boot; Police Trainer 2 exits
    // without /dev/p37c).
    if (g->iButtonCheck)
        detourFunction(g->iButtonCheck, one);
    if (g->diskSerialCheck)
        detourFunction(g->diskSerialCheck, one);
    return 0;
}
