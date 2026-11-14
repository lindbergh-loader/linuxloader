// Gaelco games (see gaelco.h): their cabinet's paths, and the system calls
// of Gaelco's kernel module.
//
// sndgaelco.o puts its functions in the kernel's system call table
// (222-233), the game reaches them through wrappers of its own (int 0x80):
// iogaelco_start_sound... in a G3Dlib file, and, in Tokyo Cop, the same
// calls inline in the iolinux class's methods. On today's kernels these
// numbers are the xattr calls: each wrapper is replaced by the module's
// function here, found by its name in the game's symbols (old and new g++
// manglings).

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gaelco.h"
#include "../common/nvGl.h"
#include "../common/importHook.h"
#include "../graphics/sdlCalls.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const GaelcoGame *gaelcoCurrentGame(void)
{
    static uint32_t cachedId;
    static const GaelcoGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = gaelcoGetGame(gId);
    }
    return game;
}

int isGaelcoGame(void)
{
    return gaelcoCurrentGame() != NULL;
}

const char *gaelcoRedirectPath(const char *path, char *buf, size_t size)
{
    const GaelcoGame *g = gaelcoCurrentGame();

    if (!g || !path || !gameDir[0])
        return path;
    for (const GaelcoPathAlias *a = g->paths; a->from; a++)
    {
        size_t n = strlen(a->from);
        if (!strncmp(path, a->from, n) && (path[n] == '/' || path[n] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, a->to, path + n);
            return buf;
        }
    }
    return path;
}

static double nowSeconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

uint32_t gaelcoVsyncCount(void)
{
    static double start;
    double now = nowSeconds();

    if (!start)
        start = now;
    return (uint32_t)((now - start) * 60.0);
}

// The module's functions, as the wrappers' callers see them.

__attribute__((force_align_arg_pointer)) static int startSound(int dummy)
{
    (void)dummy;
    return gaelcoSoundStart();
}

__attribute__((force_align_arg_pointer)) static int stopSound(int dummy)
{
    (void)dummy;
    return gaelcoSoundStop();
}

__attribute__((force_align_arg_pointer)) static int setMute(int sound)
{
    (void)sound;
    return 0;
}

__attribute__((force_align_arg_pointer)) static int dumpSoundBuffer(int *address, int size)
{
    return gaelcoSoundLoad(address, size);
}

__attribute__((force_align_arg_pointer)) static int inport(int port, char *value)
{
    return gaelcoIoInport(port, value);
}

__attribute__((force_align_arg_pointer)) static int outport(int port, char *value)
{
    return gaelcoIoOutport(port, value);
}

__attribute__((force_align_arg_pointer)) static int addComm(int command, int channel, int p0p1, int p2p3)
{
    return gaelcoSoundAddCommand(command, channel, p0p1, p2p3);
}

__attribute__((force_align_arg_pointer)) static int intfCommand(int command, int *parms)
{
    static unsigned unknown;
    int result = 1;

    if (gaelcoSoundCommand(command, parms, &result) || gaelcoIoCommand(command, parms, &result))
        return result;
    if (!(unknown & (1u << (command & 31))))
    {
        unknown |= 1u << (command & 31);
        log_warn("NV GL: driver command %d not emulated", command);
    }
    return 1;
}

// The module's unused calls (gaelco_dummy0-3).
__attribute__((force_align_arg_pointer)) static int dummy(int value)
{
    (void)value;
    return 0;
}

// Tokyo Cop's iolinux methods: the same, after their object.
__attribute__((force_align_arg_pointer)) static int methodStartSound(void *self)
{
    (void)self;
    return gaelcoSoundStart();
}

__attribute__((force_align_arg_pointer)) static int methodStopSound(void *self)
{
    (void)self;
    return gaelcoSoundStop();
}

__attribute__((force_align_arg_pointer)) static int methodSetMute(void *self, int sound)
{
    (void)self;
    (void)sound;
    return 0;
}

__attribute__((force_align_arg_pointer)) static int methodDumpSoundBuffer(void *self, int *address, int size)
{
    (void)self;
    return gaelcoSoundLoad(address, size);
}

__attribute__((force_align_arg_pointer)) static int methodInport(void *self, int port, char *value)
{
    (void)self;
    return gaelcoIoInport(port, value);
}

__attribute__((force_align_arg_pointer)) static int methodOutport(void *self, int port, char *value)
{
    (void)self;
    return gaelcoIoOutport(port, value);
}

__attribute__((force_align_arg_pointer)) static int methodAddComm(void *self, int command, int channel, int p0p1, int p2p3)
{
    (void)self;
    return gaelcoSoundAddCommand(command, channel, p0p1, p2p3);
}

__attribute__((force_align_arg_pointer)) static int methodIntfCommand(void *self, int command, int *parms)
{
    (void)self;
    return intfCommand(command, parms);
}

static const struct
{
    const char *name;
    void *function;
} wrappers[] = {
    {"iogaelco_start_sound__Fi", startSound},
    {"_Z20iogaelco_start_soundi", startSound},
    {"iogaelco_stop_sound__Fi", stopSound},
    {"_Z19iogaelco_stop_soundi", stopSound},
    {"iogaelco_set_mute__Fi", setMute},
    {"_Z17iogaelco_set_mutei", setMute},
    {"iogaelco_dump_sound_buffer__FPii", dumpSoundBuffer},
    {"_Z26iogaelco_dump_sound_bufferPii", dumpSoundBuffer},
    {"iogaelco_inport__FiPc", inport},
    {"_Z15iogaelco_inportiPc", inport},
    {"iogaelco_outport__FiPc", outport},
    {"_Z16iogaelco_outportiPc", outport},
    {"iogaelco_add_comm__Fiiii", addComm},
    {"_Z17iogaelco_add_commiiii", addComm},
    {"iogaelco_intf_command__FiPi", intfCommand},
    {"_Z21iogaelco_intf_commandiPi", intfCommand},
    {"gaelco_dummy0__Fi", dummy},
    {"gaelco_dummy1__Fi", dummy},
    {"gaelco_dummy2__Fi", dummy},
    {"gaelco_dummy3__Fi", dummy},
    {"start_sound__7iolinux", methodStartSound},
    {"stop_sound__7iolinux", methodStopSound},
    {"set_mute__7iolinuxi", methodSetMute},
    {"dump_sound_buffer__7iolinuxPii", methodDumpSoundBuffer},
    {"inport__7iolinuxiPc", methodInport},
    {"outport__7iolinuxiPc", methodOutport},
    {"add_comm__7iolinuxiiii", methodAddComm},
    {"intf_command__7iolinuxiPi", methodIntfCommand},
};

// The cabinet's commands (copying the game to the save partition, loading
// the module): none is run.
__attribute__((force_align_arg_pointer)) static int gameSystem(const char *command)
{
    if (command)
        printf("NV GL: not run: %s\n", command);
    return 0;
}

// The clock the game sets (from its operator menu, or the master
// cabinet's): the host's stays.
__attribute__((force_align_arg_pointer)) static int gameStime(const time_t *t)
{
    (void)t;
    return 0;
}

// Intel's compiler's CPU dispatch (Ring Riders, Tuning Race): its
// indicator takes only Intel's CPUs of the time for SSE2 ones (any other
// is 1, "generic"), and some of the game's dispatched functions have no
// generic version (__intel_cpu_dispatch_fail). Every CPU today has SSE2: the
// indicator is a Pentium 4's.
#define INTEL_CPU_PENTIUM4_SSE2 0x200

static uint32_t *intelCpuIndicator;

__attribute__((force_align_arg_pointer)) static void intelCpuIndicatorInit(void)
{
    *intelCpuIndicator = INTEL_CPU_PENTIUM4_SSE2;
}

// Tuning Race lowers its threads' priority with pthread_create's result (0)
// for the thread: LinuxThreads answered ESRCH, today's glibc dereferences
// it. Thread handles are pointers now: anything that low is no thread.
static int (*realGetSchedParam)(pthread_t, int *, struct sched_param *);
static int (*realSetSchedParam)(pthread_t, int, const struct sched_param *);

__attribute__((force_align_arg_pointer)) static int gameGetSchedParam(pthread_t thread, int *policy,
                                                                       struct sched_param *param)
{
    return thread < 0x10000 ? ESRCH : realGetSchedParam(thread, policy, param);
}

__attribute__((force_align_arg_pointer)) static int gameSetSchedParam(pthread_t thread, int policy,
                                                                       const struct sched_param *param)
{
    return thread < 0x10000 ? ESRCH : realSetSchedParam(thread, policy, param);
}

// The window's events are handled at each frame (the GLX bridge's swap):
// while the game loads (Ring Riders' data, a few seconds before its first
// frame), its reads handle them, or the desktop takes the window for hung.
static ssize_t (*realRead)(int, void *, size_t);
static pthread_t mainThread;

__attribute__((force_align_arg_pointer)) static ssize_t gameRead(int fd, void *buf, size_t count)
{
    static double last;
    double now = nowSeconds();

    if (now - last > 0.1 && pthread_equal(pthread_self(), mainThread) && getSDLWindow())
    {
        last = now;
        pollEvents();
    }
    return realRead(fd, buf, count);
}

// Tuning Race has Xlib's VidMode client in it (its video mode, viewport
// and gamma ramp): on a server with the extension (Xwayland), a refused
// request ends the game. They all fail, as without the extension.
static const char *const vidModeCalls[] = {
    "XF86VidModeQueryExtension", "XF86VidModeQueryVersion",  "XF86VidModeSetClientVersion",
    "XF86VidModeGetGamma",       "XF86VidModeSetGamma",      "XF86VidModeGetGammaRamp",
    "XF86VidModeSetGammaRamp",   "XF86VidModeGetGammaRampSize", "XF86VidModeGetModeLine",
    "XF86VidModeGetAllModeLines", "XF86VidModeAddModeLine",  "XF86VidModeDeleteModeLine",
    "XF86VidModeModModeLine",    "XF86VidModeValidateModeLine", "XF86VidModeSwitchMode",
    "XF86VidModeSwitchToMode",   "XF86VidModeLockModeSwitch", "XF86VidModeGetMonitor",
    "XF86VidModeGetViewPort",    "XF86VidModeSetViewPort",   "XF86VidModeGetDotClocks",
};

__attribute__((force_align_arg_pointer)) static int noVidMode(void)
{
    return 0;
}

int gaelcoInit(void)
{
    int found = 0;

    // Started in the game's directory, the cabinet's salidas.
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    // (the vertical blank count starts with the game)
    gaelcoVsyncCount();
    if (gaelcoCurrentGame()->workDir && chdir(gaelcoCurrentGame()->workDir) != 0)
        log_warn("NV GL: no %s in the game's directory", gaelcoCurrentGame()->workDir);

    for (size_t i = 0; i < sizeof(wrappers) / sizeof(wrappers[0]); i++)
    {
        void *address = findStaticFnAddr(wrappers[i].name);
        if (!address)
            continue;
        detourFunction((size_t)address, wrappers[i].function);
        found++;
    }
    if (!found)
        log_error("NV GL: no sndgaelco wrappers in the game, no sound or I/O");

    intelCpuIndicator = findStaticFnAddr("__intel_cpu_indicator");
    void *indicatorInit = findStaticFnAddr("__intel_cpu_indicator_init");
    if (intelCpuIndicator && indicatorInit)
    {
        *intelCpuIndicator = INTEL_CPU_PENTIUM4_SSE2;
        detourFunction((size_t)indicatorInit, intelCpuIndicatorInit);
    }

    if (hookExecutableImport("pthread_getschedparam", (void *)gameGetSchedParam))
        *(void **)&realGetSchedParam = importTarget("pthread_getschedparam");
    if (hookExecutableImport("pthread_setschedparam", (void *)gameSetSchedParam))
        *(void **)&realSetSchedParam = importTarget("pthread_setschedparam");
    for (size_t i = 0; i < sizeof(vidModeCalls) / sizeof(vidModeCalls[0]); i++)
    {
        void *address = findStaticFnAddr(vidModeCalls[i]);
        if (address)
            detourFunction((size_t)address, noVidMode);
    }

    if (gaelcoCurrentGame()->noMotionJump)
        patchMemoryFromString(gaelcoCurrentGame()->noMotionJump, "9090");

    mainThread = pthread_self();
    if (hookExecutableImport("read", (void *)gameRead))
        *(void **)&realRead = importTarget("read");
    hookExecutableImport("system", (void *)gameSystem);
    hookExecutableImport("stime", (void *)gameStime);
    nvGlInstall();
    gaelcoIoInstall();
    gaelcoVariadorInstall();
    if (gaelcoCurrentGame()->alignStack)
        alignExecutableImports();
    return 0;
}
