// Nerf Arcade's preload and native plugins (see nerf.h): the settings, the
// dongle (libUnityNatives.so's and libhasp's API), and the cabinet's system
// files kept out of the host.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nerf.h"
#include "../../config/iniParser.h"

// ---------------------------------------------------------------------------
// Settings: the loader's linuxloader.ini, named by LINUXLOADER_CONFIG (else
// linuxloader.ini in the game's directory).
//   [Input] INPUT_MODE   2: the guns and buttons of [EVDEV]; else the desktop
//                        mouse and keyboard
//   [EVDEV]              the inputs, as for the 32-bit games
// and the environment:
//   NERF_CAB_TEMPLATE    the dongle's cabinet template (the coin and ticket
//                        settings): 1 USA coin (default), 0 USA card swipe,
//                        2 CEC coin, 3 CEC card, 4 Dave & Buster's card, 5 UK,
//                        6 Europe, 7 Japan, 8 China, 9 Australia
static IniConfig *ini;

// Set once the library knows it is in the game; until then (and in the
// other 64-bit games) the hooks below change nothing.
static int active;
NerfGame nerfGame;

const char *nerfIniValue(const char *section, const char *key)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    return v && *v ? v : NULL;
}

int nerfIniInt(const char *section, const char *key, int def)
{
    const char *v = nerfIniValue(section, key);
    if (!v) return def;
    if (!strcasecmp(v, "true")) return 1;
    if (!strcasecmp(v, "false")) return 0;
    return atoi(v);
}

// ---------------------------------------------------------------------------
// The cabinet's system files. At boot the game writes a udev rule mounting
// USB sticks (/etc/udev/rules.d) and a script (/pm/FlushBuffers.sh) unless
// they exist, then reloads udev's rules: both are kept in LocalData/system,
// made beforehand, so that it finds them and leaves the host alone.
static const struct
{
    const char *path, *local;
} cabinetFiles[] = {
    {"/etc/udev/rules.d/10-usb-automount.rules", "LocalData/system/10-usb-automount.rules"},
    {"/pm/FlushBuffers.sh", "LocalData/system/FlushBuffers.sh"},
};

static const char *mapPath(const char *path)
{
    if (!active)
        return path;
    for (size_t i = 0; path && i < sizeof(cabinetFiles) / sizeof(cabinetFiles[0]); i++)
        if (!strcmp(path, cabinetFiles[i].path))
            return cabinetFiles[i].local;
    return path;
}

static int (*realXstat)(int, const char *, struct stat *), (*realLxstat)(int, const char *, struct stat *);
static int (*realOpen)(const char *, int, ...);
static FILE *(*realFopen)(const char *, const char *);
static int (*realAccess)(const char *, int);
static int (*realSystem)(const char *);

static void resolve(void)
{
    if (realOpen)
        return;
    *(void **)&realXstat = dlvsym(RTLD_NEXT, "__xstat", "GLIBC_2.2.5");
    *(void **)&realLxstat = dlvsym(RTLD_NEXT, "__lxstat", "GLIBC_2.2.5");
    *(void **)&realFopen = dlsym(RTLD_NEXT, "fopen");
    *(void **)&realAccess = dlsym(RTLD_NEXT, "access");
    *(void **)&realSystem = dlsym(RTLD_NEXT, "system");
    *(void **)&realOpen = dlsym(RTLD_NEXT, "open");
}

int __xstat(int ver, const char *path, struct stat *st)
{
    resolve();
    return realXstat(ver, mapPath(path), st);
}

int __lxstat(int ver, const char *path, struct stat *st)
{
    resolve();
    return realLxstat(ver, mapPath(path), st);
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    va_list ap;
    resolve();
    va_start(ap, flags);
    if (flags & (O_CREAT | O_TMPFILE))
        mode = va_arg(ap, mode_t);
    va_end(ap);
    return realOpen(mapPath(path), flags, mode);
}

FILE *fopen(const char *path, const char *mode)
{
    resolve();
    return realFopen(mapPath(path), mode);
}

int access(const char *path, int mode)
{
    resolve();
    return realAccess(mapPath(path), mode);
}

// The cabinet reboots itself (shutdown -r) and its test menu mounts USB
// sticks: neither on the host.
int system(const char *command)
{
    resolve();
    if (active && command && (strstr(command, "shutdown") || strstr(command, "reboot") || strstr(command, "udevadm") ||
                    strstr(command, "mount")))
    {
        nerfLog("not run: %s\n", command);
        return 0;
    }
    return realSystem(command);
}

void *nerfMonoHandle;

// The player loads Mono with RTLD_DEEPBIND: its libc calls would go
// straight to libc, past the ones above.
void *dlopen(const char *file, int mode)
{
    static void *(*realDlopen)(const char *, int);
    if (!realDlopen)
        *(void **)&realDlopen = dlsym(RTLD_NEXT, "dlopen");
    void *handle;
    if (active && file && strstr(file, "libmono"))
        mode &= ~RTLD_DEEPBIND;
    handle = realDlopen(file, mode);
    // Loaded locally: its API is reached through this handle.
    if (handle && file && strstr(file, "libmonobdwgc"))
        nerfMonoHandle = handle;
    return handle;
}

static void makeCabinetFiles(void)
{
    mkdir("LocalData", 0755);
    mkdir("LocalData/system", 0755);
    for (size_t i = 0; i < sizeof(cabinetFiles) / sizeof(cabinetFiles[0]); i++)
    {
        int fd = realOpen(cabinetFiles[i].local, O_WRONLY | O_CREAT, 0755);
        if (fd >= 0)
            close(fd);
    }
}

// ---------------------------------------------------------------------------
// The dongle. The game reads its identity through libUnityNatives.so
// (Hasp*), which calls back into the HASP library through the delegates the
// game hands it: answered here instead. It must read version 0, and a
// serial number, cabinet type 0 and a template for the game to take its
// settings from it (TestPreferences.BuildDefaultPrefs). What the game writes
// (the lifetime coin count, the identity from the factory setup menu) is
// kept in LocalData/system/dongle.
enum
{
    HASP_OK = 0,
};

static struct
{
    int32_t lifetimeCoins;
    int32_t serial;
    uint8_t country, cabType, cabTemplate;
} dongle = {0, 12345, 0, 0, 1};

#define DONGLE_FILE "LocalData/system/dongle"

static void dongleLoad(void)
{
    FILE *f = realFopen(DONGLE_FILE, "rb");
    const char *t = getenv("NERF_CAB_TEMPLATE");
    if (f)
    {
        if (fread(&dongle, sizeof(dongle), 1, f) != 1)
            nerfLog("%s: short read\n", DONGLE_FILE);
        fclose(f);
    }
    if (t && *t)
        dongle.cabTemplate = (uint8_t)atoi(t);
}

static int dongleSave(void)
{
    FILE *f = realFopen(DONGLE_FILE, "wb");
    if (f)
    {
        fwrite(&dongle, sizeof(dongle), 1, f);
        fclose(f);
    }
    return HASP_OK;
}

typedef void *Delegate;
void SetHaspLoginDelegate(Delegate d) { (void)d; }
void SetHaspLogoutDelegate(Delegate d) { (void)d; }
void SetHaspReadDelegate(Delegate d) { (void)d; }
void SetHaspWriteDelegate(Delegate d) { (void)d; }

int HaspLogin(int feature, int *handle)
{
    (void)feature;
    *handle = 1;
    return HASP_OK;
}
int HaspLogout(int handle) { (void)handle; return HASP_OK; }
int HaspReadDongleVersion(int h, int *v) { (void)h; *v = 0; return HASP_OK; }
int HaspReadLifetimeCoinCount(int h, int *v) { (void)h; *v = dongle.lifetimeCoins; return HASP_OK; }
int HaspReadCountryCode(int h, uint8_t *v) { (void)h; *v = dongle.country; return HASP_OK; }
int HaspReadCabType(int h, uint8_t *v) { (void)h; *v = dongle.cabType; return HASP_OK; }
int HaspReadCabTemplate(int h, uint8_t *v) { (void)h; *v = dongle.cabTemplate; return HASP_OK; }
int HaspReadCabSerialNum(int h, int *v) { (void)h; *v = dongle.serial; return HASP_OK; }
int HaspWriteLifetimeCoinCount(int h, int *v) { (void)h; dongle.lifetimeCoins = *v; return dongleSave(); }
int HaspWriteCountryCode(int h, uint8_t *v) { (void)h; dongle.country = *v; return dongleSave(); }
int HaspWriteCabType(int h, uint8_t *v) { (void)h; dongle.cabType = *v; return dongleSave(); }
int HaspWriteCabTemplate(int h, uint8_t *v) { (void)h; dongle.cabTemplate = *v; return dongleSave(); }
int HaspWriteCabSerialNum(int h, int *v) { (void)h; dongle.serial = *v; return dongleSave(); }

// The HASP library itself: the game only calls it once, with dummy
// arguments, to get the delegates' entry points marshalled (a buffer of
// address 999 included), and never reads what it answers.
int hasp_login_scope(int feature, const char *scope, const char *vendor, int *handle)
{
    (void)feature; (void)scope; (void)vendor;
    if (handle) *handle = 1;
    return HASP_OK;
}
int hasp_logout(int handle) { (void)handle; return HASP_OK; }
int hasp_read(int h, unsigned file, int off, int len, void *buf) { (void)h; (void)file; (void)off; (void)len; (void)buf; return HASP_OK; }
int hasp_write(int h, unsigned file, int off, int len, void *buf) { (void)h; (void)file; (void)off; (void)len; (void)buf; return HASP_OK; }

// ---------------------------------------------------------------------------
// The rest of libUnityNatives.so: the tickets owed and a backup of the coin
// audits (files of the cabinet's /pm, the game keeps its own audits), the
// RIO2's keys, and the periodic reboot, which never comes.

void ReadTicketsOwed(int *p1, int *p2) { *p1 = 0; *p2 = 0; }
void WriteTicketsOwed(int p1, int p2) { (void)p1; (void)p2; }
int ReadCoinAuditsFromFile(void *audits) { (void)audits; return 0; }
void WriteCoinAuditsToFile(void *audits) { (void)audits; }
void GetRIOKeys(void *pub, void *priv) { (void)pub; (void)priv; }

void GetPeriodicRebootInfo(float *rebootTime, float *rebootDelay)
{
    *rebootTime = 1e12f;
    *rebootDelay = 4.f;
}

// ---------------------------------------------------------------------------

__attribute__((constructor)) static void init(void)
{
    const char *path = getenv("LINUXLOADER_CONFIG");
    char exe[PATH_MAX], *dot;
    ssize_t n;

    resolve();
    // Only in the game (<name>.x86_64 beside <name>_Data), not in what it
    // starts.
    if ((n = readlink("/proc/self/exe", exe, sizeof(exe) - 8)) <= 0)
        return;
    exe[n] = 0;
    if ((dot = strrchr(exe, '.')) && !strchr(dot, '/'))
        *dot = 0;
    strcat(exe, "_Data");
    if (realAccess(exe, F_OK) != 0)
        return;
    active = 1;
    nerfGame = NERF_GAME_NERF;
    if (!path || !*path)
        path = realAccess("linuxloader.ini", R_OK) == 0 ? "linuxloader.ini" : NULL;
    if (path && !(ini = iniLoad(path)))
        nerfLog("cannot read %s\n", path);
    makeCabinetFiles();
    dongleLoad();
    if (nerfGame == NERF_GAME_NERF)
        nerfVideoInit();
    nerfLog("config %s, cabinet template %d\n", path ? path : "(none)", dongle.cabTemplate);
}
