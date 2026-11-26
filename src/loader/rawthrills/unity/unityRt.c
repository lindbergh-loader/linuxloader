// The Raw Thrills Unity games' preload and native plugins (see unity.h):
// which game runs, the settings, the cabinet's system files kept out of the
// host (and what the game changes there, its UnityGame), the dongle
// (libUnityNatives.so's and libhasp's API).
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include "unity.h"
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

// Set once the library knows it is in a game; until then (and in the
// other 64-bit games) the hooks below change nothing.
static int active;
static const UnityGame noGame = {.name = "none"};
const UnityGame *unityGame = &noGame;

const char *unityIniValue(const char *section, const char *key)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    return v && *v ? v : NULL;
}

int unityIniInt(const char *section, const char *key, int def)
{
    const char *v = unityIniValue(section, key);
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

const char *unityCabinetFile(const char *path)
{
    for (size_t i = 0; i < sizeof(cabinetFiles) / sizeof(cabinetFiles[0]); i++)
        if (!strcmp(path, cabinetFiles[i].path))
            return cabinetFiles[i].local;
    return NULL;
}

// The game's own paths first (UnityGame.mapPath), then the cabinet's files.
static const char *mapPath(const char *path)
{
    const char *mapped;
    if (!active || !path)
        return path;
    if (unityGame->mapPath && (mapped = unityGame->mapPath(path)) != path)
        return mapped;
    return (mapped = unityCabinetFile(path)) ? mapped : path;
}

static int (*realXstat)(int, const char *, struct stat *), (*realLxstat)(int, const char *, struct stat *);
static int (*realOpen)(const char *, int, ...);
static FILE *(*realFopen)(const char *, const char *);
static int (*realAccess)(const char *, int);
static int (*realSystem)(const char *);
static int (*realMkdir)(const char *, mode_t);
static DIR *(*realOpendir)(const char *);
static int (*realExecve)(const char *, char *const[], char *const[]);
static int (*realExecvp)(const char *, char *const[]);
static int (*realExecv)(const char *, char *const[]);

static void resolve(void)
{
    if (realOpen)
        return;
    *(void **)&realXstat = dlvsym(RTLD_NEXT, "__xstat", "GLIBC_2.2.5");
    *(void **)&realLxstat = dlvsym(RTLD_NEXT, "__lxstat", "GLIBC_2.2.5");
    *(void **)&realFopen = dlsym(RTLD_NEXT, "fopen");
    *(void **)&realAccess = dlsym(RTLD_NEXT, "access");
    *(void **)&realSystem = dlsym(RTLD_NEXT, "system");
    *(void **)&realMkdir = dlsym(RTLD_NEXT, "mkdir");
    *(void **)&realOpendir = dlsym(RTLD_NEXT, "opendir");
    *(void **)&realExecve = dlsym(RTLD_NEXT, "execve");
    *(void **)&realExecvp = dlsym(RTLD_NEXT, "execvp");
    *(void **)&realExecv = dlsym(RTLD_NEXT, "execv");
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

int mkdir(const char *path, mode_t mode)
{
    resolve();
    return realMkdir(mapPath(path), mode);
}

DIR *opendir(const char *path)
{
    resolve();
    return realOpendir(mapPath(path));
}

// The commands the game runs (Process.Start: a fork, then execve), seen by
// the game's UnityGame.command.
//
static void gameCommand(const char *path, char *const argv[])
{
    const char *name = path ? strrchr(path, '/') : NULL;
    name = name ? name + 1 : path;
    if (active && unityGame->command && name)
        unityGame->command(name, argv);
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    resolve();
    gameCommand(path, argv);
    return realExecve(path, argv, envp);
}

int execvp(const char *file, char *const argv[])
{
    resolve();
    gameCommand(file, argv);
    return realExecvp(file, argv);
}

int execv(const char *path, char *const argv[])
{
    resolve();
    gameCommand(path, argv);
    return realExecv(path, argv);
}

// A game offline (UnityGame.offline) never reaches the network: no name is
// resolved but the local host's, no Internet connection made but to it
// (127.0.0.0/8, ::1). Unity's curl and Mono's System.Native go through
// these. The other games are left alone.
static int offline(void)
{
    resolve();
    return active && unityGame->offline;
}

static int localName(const char *n)
{
    return n && (!strcasecmp(n, "localhost") || !strncmp(n, "127.", 4) || !strcmp(n, "::1"));
}

static void offlineRefused(const char *what, const char *name)
{
    static int said;
    if (__atomic_add_fetch(&said, 1, __ATOMIC_RELAXED) <= 20)
        unityLog("offline: %s %s refused\n", what, name ? name : "");
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    static int (*real)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
    if (!real)
        *(void **)&real = dlsym(RTLD_NEXT, "getaddrinfo");
    if (node && offline() && !localName(node))
    {
        offlineRefused("lookup", node);
        return EAI_NONAME;
    }
    return real(node, service, hints, res);
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent *(*real)(const char *);
    if (!real)
        *(void **)&real = dlsym(RTLD_NEXT, "gethostbyname");
    if (offline() && !localName(name))
    {
        offlineRefused("lookup", name);
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    return real(name);
}

int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t len, struct hostent **result, int *err)
{
    static int (*real)(const char *, struct hostent *, char *, size_t, struct hostent **, int *);
    if (!real)
        *(void **)&real = dlsym(RTLD_NEXT, "gethostbyname_r");
    if (offline() && !localName(name))
    {
        offlineRefused("lookup", name);
        *result = NULL;
        *err = HOST_NOT_FOUND;
        return 0;
    }
    return real(name, ret, buf, len, result, err);
}

static int localAddress(const struct sockaddr *a)
{
    static const unsigned char loop6[16] = {[15] = 1};
    if (!a)
        return 0;
    if (a->sa_family == AF_INET)
        return (ntohl(((const struct sockaddr_in *)a)->sin_addr.s_addr) >> 24) == 127;
    if (a->sa_family == AF_INET6)
    {
        const struct in6_addr *v6 = &((const struct sockaddr_in6 *)a)->sin6_addr;
        return !memcmp(v6, loop6, 16) || (IN6_IS_ADDR_V4MAPPED(v6) && v6->s6_addr[12] == 127);
    }
    return 1; // a Unix socket (X11, PulseAudio...): local
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    static int (*real)(int, const struct sockaddr *, socklen_t);
    if (!real)
        *(void **)&real = dlsym(RTLD_NEXT, "connect");
    if (offline() && !localAddress(addr))
    {
        offlineRefused("connection", "");
        errno = ENETUNREACH;
        return -1;
    }
    return real(fd, addr, len);
}

// The cabinet reboots itself (shutdown -r) and its test menu mounts USB
// sticks: neither on the host.
int system(const char *command)
{
    resolve();
    if (active && command && (strstr(command, "shutdown") || strstr(command, "reboot") || strstr(command, "udevadm") ||
                    strstr(command, "mount")))
    {
        unityLog("not run: %s\n", command);
        return 0;
    }
    return realSystem(command);
}

void *unityMonoHandle;

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
    // Loaded locally: its API is reached through this handle (the
    // runtime's, not libmono-native.so's that it loads itself).
    if (handle && file && strstr(file, "libmono") && dlsym(handle, "mono_runtime_invoke"))
        unityMonoHandle = handle;
    return handle;
}

static void makeCabinetFiles(void)
{
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
// game hands it: answered here instead. Nerf must read version 0, and a
// serial number, cabinet type 0 and a template for the game to take its
// settings from it (TestPreferences.BuildDefaultPrefs); Superbikes 3 a
// version up to 2, cabinet type 1 (UnityGame.dongleCabType: the standard
// cabinet, 2 is the motion one) and a color (1 orange .. 4 fuchsia). What the game writes (the
// lifetime coin count, the identity from the factory setup menu) is kept in
// LocalData/system/dongle.
enum
{
    HASP_OK = 0,
};

static struct
{
    int32_t lifetimeCoins;
    int32_t serial;
    uint8_t country, cabType, cabTemplate;
    // Superbikes 3's (the file grew them: an older one ends before)
    uint8_t cabColor;
    int32_t version;
} dongle = {0, 12345, 0, 0, 1, 1, 0};

#define DONGLE_FILE "LocalData/system/dongle"

static void dongleLoad(void)
{
    FILE *f = realFopen(DONGLE_FILE, "rb");
    const char *t = getenv("NERF_CAB_TEMPLATE");
    dongle.cabType = unityGame->dongleCabType;
    if (f)
    {
        if (fread(&dongle, 1, sizeof(dongle), f) < offsetof(typeof(dongle), cabColor))
            unityLog("%s: short read\n", DONGLE_FILE);
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
int HaspReadDongleVersion(int h, int *v) { (void)h; *v = dongle.version; return HASP_OK; }
int HaspReadLifetimeCoinCount(int h, int *v) { (void)h; *v = dongle.lifetimeCoins; return HASP_OK; }
int HaspReadCountryCode(int h, uint8_t *v) { (void)h; *v = dongle.country; return HASP_OK; }
int HaspReadCabType(int h, uint8_t *v) { (void)h; *v = dongle.cabType; return HASP_OK; }
int HaspReadCabTemplate(int h, uint8_t *v) { (void)h; *v = dongle.cabTemplate; return HASP_OK; }
int HaspReadCabSerialNum(int h, int *v) { (void)h; *v = dongle.serial; return HASP_OK; }
int HaspReadCabColor(int h, uint8_t *v) { (void)h; *v = dongle.cabColor; return HASP_OK; }
int HaspWriteLifetimeCoinCount(int h, int *v) { (void)h; dongle.lifetimeCoins = *v; return dongleSave(); }
int HaspWriteCountryCode(int h, uint8_t *v) { (void)h; dongle.country = *v; return dongleSave(); }
int HaspWriteCabType(int h, uint8_t *v) { (void)h; dongle.cabType = *v; return dongleSave(); }
int HaspWriteCabTemplate(int h, uint8_t *v) { (void)h; dongle.cabTemplate = *v; return dongleSave(); }
int HaspWriteCabSerialNum(int h, int *v) { (void)h; dongle.serial = *v; return dongleSave(); }
int HaspWriteCabColor(int h, uint8_t *v) { (void)h; dongle.cabColor = *v; return dongleSave(); }
int HaspWriteDongleVersion(int h, int *v) { (void)h; dongle.version = *v; return dongleSave(); }

// The HASP library itself: the game only calls it once, with dummy
// arguments, to get the delegates' entry points marshalled (a buffer of
// address 999 included), and never reads what it answers.
int hasp_login_scope(int feature, const char *scope, const char *vendor, int *handle)
{
    (void)feature; (void)scope; (void)vendor;
    if (handle) *handle = 1;
    return HASP_OK;
}
// Marvel Contest of Champions' call: hasp_login(0, NULL, NULL).
int hasp_login(int feature, const char *vendor, int *handle)
{
    return hasp_login_scope(feature, NULL, vendor, handle);
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

// The game: the one whose data directory is beside the executable
// (<name>.x86_64 beside <name>_Data), the default one (no dataDir) if
// another is; none if there is no such directory (not a Unity game, or
// what the game starts).
static const UnityGame *findGame(void)
{
    char exe[PATH_MAX], *dot;
    const UnityGame *fallback = NULL;
    ssize_t n;

    if ((n = readlink("/proc/self/exe", exe, sizeof(exe) - 8)) <= 0)
        return NULL;
    exe[n] = 0;
    if ((dot = strrchr(exe, '.')) && !strchr(dot, '/'))
        *dot = 0;
    strcat(exe, "_Data");
    if (realAccess(exe, F_OK) != 0)
        return NULL;
    n = (ssize_t)strlen(exe);
    for (const UnityGame *const *g = unityGames; *g; g++)
    {
        size_t len = (*g)->dataDir ? strlen((*g)->dataDir) : 0;
        if (!(*g)->dataDir)
            fallback = *g;
        else if ((size_t)n >= len && !strcmp(exe + n - len, (*g)->dataDir))
            return *g;
    }
    return fallback;
}

__attribute__((constructor)) static void init(void)
{
    const char *path = getenv("LINUXLOADER_CONFIG");
    const UnityGame *game;

    resolve();
    if (!(game = findGame()))
        return;
    unityGame = game;
    active = 1;
    if (!path || !*path)
        path = realAccess("linuxloader.ini", R_OK) == 0 ? "linuxloader.ini" : NULL;
    if (path && !(ini = iniLoad(path)))
        unityLog("cannot read %s\n", path);
    mkdir("LocalData", 0755);
    mkdir("LocalData/system", 0755);
    if (unityGame->init)
        unityGame->init();
    makeCabinetFiles();
    dongleLoad();
    if (unityGame->video)
        unityVideoInit();
    unityLog("%s, config %s, cabinet template %d\n", unityGame->name, path ? path : "(none)", dongle.cabTemplate);
}
