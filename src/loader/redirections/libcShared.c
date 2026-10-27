#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libcShared.h"
#ifdef __linux__
#include <limits.h>
#include "../rawthrills/rawthrills.h"
#include "../namco/namcoEs1.h"
#include "../namco/namcoN2.h"
#include "../teamplay/teamplay.h"
#include "../globalvr/gvr.h"
#endif
#include "../config/config.h"
#include "loader/elfLoader/symbolResolver.hpp"

#ifdef __linux__
#include <dlfcn.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>

#define REAL_FUNC(name) dlsym(RTLD_NEXT, #name)
#else
#include <processenv.h>
#define REAL_FUNC(name) name
#endif

extern uint32_t gId;
extern int gGrp;
extern char envpath[100];

/**
 * Hook for the only function provided by kswapapi.so
 * @param p No idea this gets discarded
 */
int sharedKswap_collect(void *p)
{
    return 0;
}

char *sharedStrncpy(char *dest, const char *src, size_t n)
{
    static char *(*_strncpy)(char *dest, const char *src, size_t n) = NULL;
    if (_strncpy == NULL)
        _strncpy = REAL_FUNC(strncpy);

    switch (gId)
    {
        case HARLEY_DAVIDSON_SBRG:
        case RAMBO_SBQL:
        case RAMBO_SBSS_CHINA:
        case THE_HOUSE_OF_THE_DEAD_EX_SBRC:
        case TOO_SPICY_SBMV:
            if (getConfig()->GPUVendor != NVIDIA_GPU &&
                (strstr(src, "../fs/compiledshader") != NULL || strstr(src, "../fs/compiled") != NULL))
                return _strncpy(dest, "../fs/compiledmesa", n);
            break;
        case QUIZ_AXA_SBMS:
        case QUIZ_AXA_SBUR_LIVE:
            if (getConfig()->GPUVendor != NVIDIA_GPU && strstr(src, "../../data/compiledshader") != NULL)
                return _strncpy(dest, "../../data/compiledmesa", n);
            break;
    }
    return _strncpy(dest, src, n);
}

int sharedSetenv(const char *name, const char *value, int overwrite)
{
#ifdef __linux__
    static int (*_setenv)(const char *name, const char *value, int overwrite) = NULL;
    if (_setenv == NULL)
        _setenv = REAL_FUNC(setenv);
#endif

    if (strcmp(name, "DISPLAY") == 0)
    {
        return 0;
    }

#ifdef __linux__
    return _setenv(name, value, overwrite);
#else
    return _putenv_s(name, value);
#endif
}

static char *(*_getenv)(const char *name) = NULL;
char *sharedGetenv(const char *name)
{
    if (_getenv == NULL)
        _getenv = REAL_FUNC(getenv);

    if (strcmp(name, "TEA_DIR") == 0)
    {
#ifdef __linux__
        if (getcwd(envpath, 100) == NULL)
            return "";
#else
        if (GetCurrentDirectoryA(MAX_PATH, (LPSTR)envpath) == 0)
            return "";
#endif
        if (gGrp == GROUP_VT3 || gGrp == GROUP_VT3_TEST || gId == RAMBO_SBQL || gId == RAMBO_SBSS_CHINA || gId == TOO_SPICY_SBMV)
        {
#ifdef __linux__
            char *ptr = strrchr(envpath, '/');
            if (ptr == NULL)
                return "";
            *ptr = '\0';
            return envpath;
#else
            char *ptr = strrchr(envpath, '\\');
            if (ptr == NULL)
                return "";
            *ptr = '\0';
            return envpath;
#endif
        }
        else
        {
            return envpath;
        }
    }

    if (strcmp(name, "__GL_SYNC_TO_VBLANK") == 0)
    {
        return "";
    }
    return _getenv(name);
}

int sharedUnsetenv(const char *name)
{
#ifdef __linux__
    static int (*_unsetenv)(const char *name) = NULL;
    if (_unsetenv == NULL)
        _unsetenv = (int (*)(const char *))dlsym(RTLD_NEXT, "unsetenv");
#endif

    if (strcmp(name, "DISPLAY") == 0)
    {
        return 0;
    }

#ifdef __linux__
    return _unsetenv(name);
#else
    return _putenv_s(name, "");
#endif
}


struct tm *sharedLocaltime_R(const time_t *timep, struct tm *result)
{
#ifdef __linux__
    static struct tm *(*_gmtime_r)(const time_t *, struct tm *) = NULL;
    if (_gmtime_r == NULL)
        _gmtime_r = (struct tm * (*)(const time_t *, struct tm *)) dlsym(RTLD_NEXT, "gmtime_r");

    if ((gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA) && getConfig()->mj4EnabledAtT == 1)
    {
        time_t target_time = 1735286445;
        struct tm *res = _gmtime_r(&target_time, result);
        return res;
    }
    return _gmtime_r(timep, result);
#else
    const int32_t *t32 = (const int32_t *)timep;
    time_t t = (time_t)*t32;

    if (localtime_s(result, &t) == 0)
        return result;
    return NULL;
#endif
}

void *sharedDlopen(const char *filename, int flags)
{
#ifdef _WIN32
    if (!filename)
        return (void *)-1; // RTLD_DEFAULT handle

    bridgeLoadNeededLibrary(filename);

    return (void *)0xDEADBEEF;
#else
    return dlopen(filename, flags);
#endif
}

void *sharedDlsym(void *handle, const char *symbol)
{
#ifdef _WIN32
    void *func = bridgeResolveSymbol(symbol);
    return func;
#else
    return dlsym(handle, symbol);
#endif
}

int sharedDlclose(void *handle)
{
#ifdef _WIN32
    return 0;
#else
    return dlclose(handle);
#endif
}

char *sharedDlerror()
{
#ifdef _WIN32
    return NULL;
#else
    return dlerror();
#endif
}

#ifdef __linux__

static __thread timer_t alarmTimer = NULL;

static void deleteTimer(void)
{
    if (alarmTimer != NULL)
    {
        timer_delete(alarmTimer);
        alarmTimer = NULL;
    }
}

static unsigned int (*_alarm)(unsigned int) = NULL;
unsigned int alarm(unsigned int seconds)
{
    if (_alarm == NULL)
        _alarm = (unsigned int (*)(unsigned int))dlsym(RTLD_NEXT, "alarm");

    // alarm(0) cancels
    if (seconds == 0)
    {
        deleteTimer();
        return _alarm(0); // also cancel any process-wide alarm
    }

    // Create per-thread timer delivering SIGALRM ONLY to this thread
    struct sigevent sev = {0};
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGALRM;
    sev._sigev_un._tid = syscall(SYS_gettid);

    timer_t newTimer;
    if (timer_create(CLOCK_MONOTONIC, &sev, &newTimer) != 0)
    {
        perror("timer_create");
        return -1;
    }

    struct itimerspec its = {0};
    its.it_value.tv_sec = seconds;

    if (timer_settime(newTimer, 0, &its, NULL) != 0)
    {
        perror("timer_settime");
        timer_delete(newTimer);
        return -1;
    }

    // Replace old timer
    deleteTimer();
    alarmTimer = newTimer;

    return seconds; // approximate — alarm() returns previous timer's remaining
}

int kswap_collect(void *p)
{
    return sharedKswap_collect(p);
}

static int (*_system)(const char *command) = NULL;
int system(const char *command)
{
    if (_system == NULL)
        _system = (int (*)(const char *))dlsym(RTLD_NEXT, "system");

#ifdef __linux__
    char rtCommand[PATH_MAX * 2];
    command = rtRedirectCommand(command, rtCommand, sizeof(rtCommand));
    if (namcoEs1DropCommand(command) || teamplayDropCommand(command))
        return 0;
    int n2Status;
    if (namcoN2Command(command, &n2Status))
        return n2Status;
#endif

    if (strcmp(command, "lsmod | grep basebd > /dev/null") == 0)
        return 0;

    if (strcmp(command, "cd /tmp/segaboot > /dev/null") == 0)
        return system("cd tmp/segaboot > /dev/null");

    if (strcmp(command, "mkdir /tmp/segaboot > /dev/null") == 0)
        return system("mkdir tmp/segaboot > /dev/null");

    if (strcmp(command, "lspci | grep \"Multimedia audio controller: %Creative\" > /dev/null") == 0)
        return 0;

    if (strcmp(command, "lsmod | grep ctaud") == 0)
        return 0;

    if (strcmp(command, "lspci | grep MPC8272 > /dev/null") == 0)
        return 0;

    if (strcmp(command, "uname -r | grep mvl") == 0)
        return 0;

    if (strstr(command, "hwclock") != NULL)
        return 0;

    if (strstr(command, "losetup") != NULL)
        return 0;

    if (strstr(command, "check_ip.sh") != NULL)
        return 0;

    if (strcmp(command, "ifconfig eth0 10.0.0.1 netmask 255.255.255.0") == 0)
        return 0;

    if (strcmp(command, "ifconfig eth0 10.0.0.2 netmask 255.255.255.0") == 0)
        return 0;

    if (strcmp(command, "touch /tmp/segaboot/test") == 0)
        command = "touch tmp/segaboot/test";

    return _system(command);
}

char *strncpy(char *dest, const char *src, size_t n)
{
    return sharedStrncpy(dest, src, n);
}

int setenv(const char *name, const char *value, int overwrite)
{
    return sharedSetenv(name, value, overwrite);
}

char *getenv(const char *name)
{
    return sharedGetenv(name);
}

int unsetenv(const char *name)
{
    return sharedUnsetenv(name);
}

static char *(*___strdup)(const char *string) = NULL;
char *__strdup(const char *string)
{
    if (___strdup == NULL)
        ___strdup = (char *(*)(const char *))dlsym(RTLD_NEXT, "__strdup");
    if (strcmp(string, "plughw:0, 0") == 0)
    {
        return ___strdup("default");
    }
    return ___strdup(string);
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return sharedLocaltime_R(timep, result);
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
#ifdef __linux__
    static struct tm *(*_gmtime_r)(const time_t *, struct tm *) = NULL;
    if (_gmtime_r == NULL)
        _gmtime_r = (struct tm * (*)(const time_t *, struct tm *)) dlsym(RTLD_NEXT, "gmtime_r");

    if ((gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE) && getConfig()->mj4EnabledAtT == 1)
    {
        time_t target_time = 1735286445;
        struct tm *res = _gmtime_r(&target_time, result);
        return res;
    }
    return _gmtime_r(timep, result);
#else
    const int32_t *t32 = (const int32_t *)timep;
    time_t t = (time_t)*t32;

    if (gmtime_s(result, &t) == 0)
        return result;
    return NULL;
#endif
}

float powf(float base, float exponent)
{
    return (float)pow((double)base, (double)exponent);
}

int iopl(int level)
{
    return 0;
}

// America's Army reaches its cabinet's I/O daemon over TCP: the loader's.
int connect(int fd, const struct sockaddr *addr, socklen_t length)
{
    static int (*_connect)(int, const struct sockaddr *, socklen_t) = NULL;
    if (_connect == NULL)
        _connect = REAL_FUNC(connect);
    if (isGvrGame())
        return gvrLinkConnect(fd, addr, length, _connect);
    return _connect(fd, addr, length);
}

// Teamplay and Global VR games lock their memory, which as root (Batocera)
// would pin the whole process in RAM: SDL's and the GPU driver's mappings too.
int mlockall(int flags)
{
    static int (*_mlockall)(int) = NULL;
    if (isTeamplayGame() || isGvrGame())
        return 0;
    if (_mlockall == NULL)
        _mlockall = REAL_FUNC(mlockall);
    return _mlockall(flags);
}
#endif
