// Incredible Technologies games (see itech.h): their cabinet's paths and the
// start-up they need.

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "itech.h"
#include "../common/importHook.h"
#include "../common/nvGl.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const ItechGame *itechCurrentGame(void)
{
    static uint32_t cachedId;
    static const ItechGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = itechGetGame(gId);
    }
    return game;
}

int isItechGame(void)
{
    return itechCurrentGame() != NULL;
}

static int startsWith(const char *path, const char *prefix, const char **rest)
{
    size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) || (path[n] && path[n] != '/'))
        return 0;
    *rest = path + n;
    return 1;
}

const char *itechRedirectPath(const char *path, char *buf, size_t size)
{
    const char *rest;

    if (!path || !gameDir[0])
        return path;
    // The cabinet's disk, /game (its working directory): static/ the
    // read-only data, dynamic/ the writable one (settings, logs, saves),
    // db_primary/ the persistence layer's. The dump is the two merged: all
    // are the game directory, by absolute or relative path.
    if (startsWith(path, "/game/static", &rest) || startsWith(path, "/game/dynamic", &rest) ||
        startsWith(path, "static", &rest) || startsWith(path, "dynamic", &rest) || startsWith(path, "/game", &rest))
        snprintf(buf, size, "%s%s", gameDir, rest);
    // The cabinet's devices: none opens (the loader's stand in for those the
    // game needs, see itechIo.c).
    else if (!strcmp(path, "/dev/ioboard1") || !strcmp(path, "/dev/itdongle1") || !strcmp(path, "/dev/biosworm") ||
             !strcmp(path, "/dev/xico7100") || !strcmp(path, "/dev/itcardreader") || !strcmp(path, "/dev/itmodem") ||
             !strcmp(path, "/dev/accelerometer") || !strcmp(path, "/dev/mem") || !strcmp(path, "/dev/sda") ||
             !strncmp(path, "/dev/ttyS", 9) || !strncmp(path, "/dev/usb/", 9))
        snprintf(buf, size, "/dev/null/%s", path + 5);
    else
        return path;
    return buf;
}

__attribute__((force_align_arg_pointer)) static int gameSystem(const char *command)
{
    if (command)
        printf("IT: not run: %s\n", command);
    return 0;
}

// Its reads of /proc run (the CPU's clock) but the disk's partitions, the
// cabinet's one drive here (the host's many stall its parser); its other
// commands (the cabinet's network, clock and maintenance) read nothing: the
// game takes no stream as a crash.
__attribute__((force_align_arg_pointer)) static FILE *gamePopen(const char *command, const char *type)
{
    if (command && !strcmp(command, "cat /proc/partitions"))
        return popen("printf 'major minor  #blocks  name\\n\\n   8     0   39082680 sda\\n"
                     "   8     1   39082648 sda1\\n'",
                     type);
    if (command && !strncmp(command, "cat /proc/", 10))
        return popen(command, type);
    printf("IT: not run: %s\n", command ? command : "(null)");
    return popen("true", type);
}

// Its fault handler (a log and a reboot) is not installed: the loader's
// stands in for the cabinet's I/O port access (the parallel port's LED the
// controls task blinks).
__attribute__((force_align_arg_pointer)) static void (*gameSignal(int sig, void (*handler)(int)))(int)
{
    if (sig == SIGSEGV)
        return SIG_DFL;
    return signal(sig, handler);
}

// The cabinet's X server, started by the game (StartXServer) on a screen of
// its resolution: the desktop's display, as the game's window is.
__attribute__((force_align_arg_pointer)) static void *gameXOpenDisplay(const char *name)
{
    static void *(*real)(const char *);
    if (!real)
        real = importTarget("XOpenDisplay");
    return real(NULL);
}

__attribute__((force_align_arg_pointer)) static int startXServer(void)
{
    void **display = dlsym(RTLD_DEFAULT, "__nighthawk_display");
    return display && (*display = gameXOpenDisplay(NULL)) ? 0 : -1;
}

// Its timed waits (ITWaitEventSetDecrement) add the milliseconds to the
// microseconds of the time of day without carrying into the seconds: a
// deadline whose nanoseconds pass a second, which glibc refuses (EINVAL,
// the mutex kept). The wait loop then spins holding it, and every other
// thread on that event blocks. The deadline is carried here.
__attribute__((force_align_arg_pointer)) static int gameCondTimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                                                      const struct timespec *abstime)
{
    struct timespec t = *abstime;
    while (t.tv_nsec >= 1000000000L)
    {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(cond, mutex, &t);
}

// std::ends, which the file store writes at the end of its paths (an
// ostringstream's): a NUL inside the string, before the file name appended
// to it, so that every file is opened as its directory. That store was
// never used on the cabinet (its PostgreSQL was). The game makes no
// ostrstream, the one stream that needs it: nothing is written.
__attribute__((force_align_arg_pointer)) static void *gameEnds(void *stream)
{
    return stream;
}

// The cabinet ran as root: its memory locked, I/O ports and user switches.
__attribute__((force_align_arg_pointer)) static int gameSucceed(void)
{
    return 0;
}

// The executable's own function name (it exports them all), detoured to
// replacement.
void itechDetour(const char *name, void *replacement)
{
    void *fn = dlsym(RTLD_DEFAULT, name);
    if (!fn)
    {
        log_warn("IT: no function %s in the game", name);
        return;
    }
    detourFunction((size_t)fn, replacement);
}

int itechInit(void)
{
    const ItechGame *g = itechCurrentGame();
    char path[PATH_MAX];

    // Started in the game's directory, the cabinet's /game: the directories
    // the game writes in that the dump lacks.
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    static const char *const dirs[] = {"syslog", "saved_cores", "db_primary", "db_primary/filestore", NULL};
    for (const char *const *d = dirs; *d; d++)
    {
        snprintf(path, sizeof(path), "%s/%s", gameDir, *d);
        mkdir(path, 0755);
    }

    hookExecutableImport("system", (void *)gameSystem);
    hookExecutableImport("popen", (void *)gamePopen);
    static const char *const succeed[] = {"mlockall", "mlock", "munlock", "iopl", "setuid", "seteuid",
                                          "setgid", "setegid", "setgroups", NULL};
    for (const char *const *s = succeed; *s; s++)
        hookExecutableImport(*s, (void *)gameSucceed);

    hookExecutableImport("signal", (void *)gameSignal);
    hookExecutableImport("pthread_cond_timedwait", (void *)gameCondTimedwait);
    hookExecutableImport("XOpenDisplay", (void *)gameXOpenDisplay);
    detourFunction(g->startXServer, (void *)startXServer);

    // The cabinet's drivers: loaded (the loader's devices stand in for them,
    // itechIo.c), their firmware never updated.
    itechDetour("HwrReLoadLinuxDriver", (void *)stubRetOne);
    itechDetour("HwrIsLinuxDriverLoaded", (void *)stubRetOne);
    itechDetour("HwrMgrAllowUpdates", (void *)stubRetZero);
    // The I/O board's security: the answers it gives in its packets to the
    // game's challenges, checked by the controls task (an unanswered or a
    // wrong one reboots the cabinet), are not given: the checks pass.
    static const char *const security[] = {"SecurityControlsWritePacket", "SecurityControlsReadPacket",
                                           "SecurityControlsDetached", "SecurityIOBoardInitializeResults",
                                           "SecurityCrash", NULL};
    for (const char *const *s = security; *s; s++)
        itechDetour(*s, (void *)stubReturn);
    itechIoInstall();
    // Drawn for a GeForce with NVIDIA's extensions: vertex array range,
    // register combiners, NV programs.
    nvGlInstall();
    itechFrameInstall();
    // The persistence layer (settings, audits, high scores): its file store
    // (/game/db_primary/filestore), not the cabinet's PostgreSQL.
    itechDetour("_ZNK2IT11Persistence17PersistenceConfig4toDBEv", (void *)stubRetZero);
    hookExecutableImport("_ZSt4endsIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_", (void *)gameEnds);

    // The motherboard's PCI devices, scanned through the configuration
    // ports, only named in the hardware report: none.
    itechDetour("LinuxScanPciBus", (void *)stubReturn);
    return 0;
}
