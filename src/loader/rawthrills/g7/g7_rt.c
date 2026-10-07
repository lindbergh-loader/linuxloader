// The 64-bit Raw Thrills g7 games' preload (see g7.h): what they share.
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glad/gl.h>
#include "g7.h"
#include "../../config/iniParser.h"
#include "../../graphics/borderFrame.h"
#include "../../graphics/frameScale.h"

static const G7Game *game;

// ---------------------------------------------------------------------------
// Code patches.

void g7Detour(uintptr_t at, void *to)
{
    uint8_t *p = (uint8_t *)at;
    if (!at)
        return;
    p[0] = 0xff; p[1] = 0x25; memset(p + 2, 0, 4);   // jmp *0(%rip)
    memcpy(p + 6, &to, 8);
}

// Hooks reach the game through a page mapped near it (rel32 jumps).
static uint8_t *near;
static size_t nearUsed;
static void *nearAlloc(size_t n)
{
    if (!near)
    {
        near = mmap((void *)0x10000000, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (near == MAP_FAILED) { g7Log("no near page\n"); near = NULL; return NULL; }
    }
    void *p = near + nearUsed;
    nearUsed += (n + 15) & ~15u;
    return p;
}
static void absJump(uint8_t *at, void *to)
{
    at[0] = 0xff; at[1] = 0x25; memset(at + 2, 0, 4);
    memcpy(at + 6, &to, 8);
}
static void relJump(uintptr_t at, void *to)
{
    uint8_t *p = (uint8_t *)at;
    int32_t rel = (int32_t)((intptr_t)to - (intptr_t)(at + 5));
    p[0] = 0xe9; memcpy(p + 1, &rel, 4);
}
void *g7HookNear(uintptr_t at, size_t len, void *to)
{
    uint8_t *tramp = nearAlloc(len + 14), *thunk = nearAlloc(14);
    memcpy(tramp, (void *)at, len);
    absJump(tramp + len, (void *)(at + len));
    absJump(thunk, to);
    relJump(at, thunk);
    return tramp;
}

// ---------------------------------------------------------------------------
// Settings.

long g7EnvNum(const char *name, long def)
{
    const char *v = getenv(name);
    return v && *v ? strtol(v, NULL, 0) : def;
}

long g7GameEnv(const char *name, long def)
{
    char full[128];
    snprintf(full, sizeof(full), "%s_%s", game->envPrefix, name);
    return g7EnvNum(full, def);
}

// The loader's configuration: the linuxloader.ini the 32-bit loader reads for
// the other games, named by LINUXLOADER_CONFIG (else linuxloader.ini in the
// game directory, if there is one).  The g7 games take from it:
//   [Display] WIDTH, HEIGHT, FULLSCREEN, KEEP_ASPECT_RATIO, BORDER_ENABLED,
//             WHITE_BORDER_PERCENTAGE, BLACK_BORDER_PERCENTAGE
//   [Input]   INPUT_MODE (2: the controls come from [EVDEV])
//   [EVDEV]   the evdev input, read by the games' own hooks
static IniConfig *ini;
struct G7Config g7Config = {0, 0, 1, 0, 1, 1, 0.02f, 0.0f};

void *g7Ini(void)
{
    return ini;
}

const char *g7IniValue(const char *section, const char *key)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    return v && *v ? v : NULL;
}
static int iniInt(const char *section, const char *key, int def)
{
    const char *v = g7IniValue(section, key);
    if (!v) return def;
    if (!strcasecmp(v, "true")) return 1;
    if (!strcasecmp(v, "false")) return 0;
    if (!strcasecmp(v, "auto")) return def;
    return atoi(v);
}
static float iniFloat(const char *section, const char *key, float def)
{
    const char *v = g7IniValue(section, key);
    return v ? (float)atof(v) : def;
}

static void loadConfig(void)
{
    const char *path = getenv("LINUXLOADER_CONFIG");
    if (!path || !*path)
    {
        if (access("linuxloader.ini", R_OK) != 0)
            return;
        path = "linuxloader.ini";
    }
    if (!(ini = iniLoad(path)))
        return;
    g7Config.width = iniInt("Display", "WIDTH", g7Config.width);
    g7Config.height = iniInt("Display", "HEIGHT", g7Config.height);
    g7Config.fullscreen = iniInt("Display", "FULLSCREEN", g7Config.fullscreen);
    g7Config.borderEnabled = iniInt("Display", "BORDER_ENABLED", g7Config.borderEnabled);
    g7Config.whiteBorder = iniFloat("Display", "WHITE_BORDER_PERCENTAGE", g7Config.whiteBorder * 100.f) / 100.f;
    g7Config.blackBorder = iniFloat("Display", "BLACK_BORDER_PERCENTAGE", g7Config.blackBorder * 100.f) / 100.f;
    g7Config.keepAspectRatio = iniInt("Display", "KEEP_ASPECT_RATIO", g7Config.keepAspectRatio);
    g7Config.inputMode = iniInt("Input", "INPUT_MODE", g7Config.inputMode);
    g7Log("config %s: %dx%d%s, border %s, %s input\n", path, g7Config.width, g7Config.height,
          g7Config.fullscreen ? " fullscreen" : "", g7Config.borderEnabled ? "on" : "off",
          g7Config.inputMode == 2 ? "evdev" : "keyboard/mouse");
}

// ---------------------------------------------------------------------------
// The dongle (HASP HL, linked in).

static int haspOk(void) { return 0; }
static int haspLogin(uint32_t feature, const void *vc, uint32_t *handle)
{
    (void)vc;
    g7Log("hasp_login feature %#x\n", feature);
    *handle = 1;
    return 0;
}
static int haspSessionInfo(uint32_t h, const char *fmt, char **info)
{
    (void)h;
    g7Log("hasp_get_sessioninfo %.60s\n", fmt);
    *info = strdup("<?xml version=\"1.0\" encoding=\"UTF-8\" ?><hasp_info><hasp id=\"1234567\"><haspid>1234567</haspid></hasp></hasp_info>");
    return 0;
}
static int haspFree(void *p) { free(p); return 0; }

// The cabinet's identity lives in the dongle's memory file 0xfff4, and the g7
// runtime reads it before anything else: byte 40 is the factory setup flag
// (set by the test menu's factory setup, checked by the attract mode), 41 the
// cabinet type, 42 the region, 43..46 the serial number.  A dongle that reads
// back as zeroes leaves the cabinet type at 0 ("Unknown"): the game skips
// the configuration its cabinet type selects, and stops (Halo).
// <PREFIX>_FACTORY_SETUP, _CABINET_TYPE, _REGION and _SERIAL_NO set them.
static uint8_t dongleMem[64];
static char dongleMemPath[PATH_MAX];

int g7CabinetType(void)
{
    return dongleMem[41];
}

static void dongleMemLoad(void)
{
    uint32_t serial = (uint32_t)g7GameEnv("SERIAL_NO", 1234567);
    FILE *f;
    dongleMem[40] = (uint8_t)g7GameEnv("FACTORY_SETUP", game->factorySetupDone);
    dongleMem[41] = (uint8_t)g7GameEnv("CABINET_TYPE", game->defaultCabinet);
    dongleMem[42] = (uint8_t)g7GameEnv("REGION", 0);
    memcpy(dongleMem + 43, &serial, sizeof(serial));
    if ((f = fopen(dongleMemPath, "rb")))
    {
        uint8_t saved[sizeof(dongleMem)];
        if (fread(saved, 1, sizeof(saved), f) == sizeof(saved))
        {
            // The cabinet stays the one asked for.
            saved[41] = dongleMem[41];
            memcpy(dongleMem, saved, sizeof(saved));
        }
        fclose(f);
    }
}
static int haspRead(uint32_t h, uint32_t file, uint32_t off, uint32_t len, void *buf)
{
    (void)h;
    memset(buf, 0, len);
    if (file == 0xfff4)
    {
        if (off < sizeof(dongleMem))
            memcpy(buf, dongleMem + off, len < sizeof(dongleMem) - off ? len : sizeof(dongleMem) - off);
    }
    g7Log("hasp_read file %#x off %u len %u -> %#x\n", file, off, len, *(const uint8_t *)buf);
    return 0;
}
// What the game writes there is kept, and saved in the game's folder
// (dongle_memory.dmp, loaded at start) for the next runs, as the loader does
// for the other games (rtDongle.c).
static int haspWrite(uint32_t h, uint32_t file, uint32_t off, uint32_t len, const void *buf)
{
    FILE *f;
    uint32_t n = len < sizeof(dongleMem) - off ? len : sizeof(dongleMem) - off;
    (void)h;
    g7Log("hasp_write file %#x off %u len %u\n", file, off, len);
    if (file != 0xfff4 || off >= sizeof(dongleMem) || !memcmp(dongleMem + off, buf, n))
        return 0;
    memcpy(dongleMem + off, buf, n);
    if (!(f = fopen(dongleMemPath, "wb")))
        return 0;
    fwrite(dongleMem, 1, sizeof(dongleMem), f);
    fclose(f);
    return 0;
}

// The recorded answers of the dongle's AES (dump/, as TeknoParrot keeps
// them): a file named by the SHA-256 of the input, else the only one of the
// size asked for (the games encrypt a fresh challenge at each start).
static unsigned char *(*sha256)(const unsigned char *, size_t, unsigned char *);
static char dumpDir[PATH_MAX];
static int answerBySize(uint32_t len, char *name, size_t size)
{
    DIR *d = opendir(dumpDir);
    struct dirent *e;
    int found = 0;
    if (!d)
        return 0;
    while ((e = readdir(d)))
    {
        struct stat st;
        char path[PATH_MAX];
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dumpDir, e->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == (off_t)len)
        {
            if (found++)
                break;
            snprintf(name, size, "%s", path);
        }
    }
    closedir(d);
    return found == 1;
}
static int haspCrypt(const char *what, uint32_t h, uint8_t *buf, uint32_t len)
{
    unsigned char md[32];
    char name[PATH_MAX], hex[65];
    (void)h;
    if (!sha256)
    {
        void *c = dlopen("libcrypto.so.3", RTLD_NOW);
        if (c) *(void **)&sha256 = dlsym(c, "SHA256");
    }
    hex[0] = 0;
    if (sha256)
    {
        sha256(buf, len, md);
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", md[i]);
    }
    snprintf(name, sizeof(name), "%s/%s", dumpDir, hex);
    if (!hex[0] || access(name, R_OK) != 0)
        answerBySize(len, name, sizeof(name));
    FILE *f = fopen(name, "rb");
    size_t got = 0;
    if (f) { got = fread(buf, 1, len, f); fclose(f); }
    g7Log("%s len %u sha256 %s -> %s\n", what, len, hex, f ? (got == len ? "answered" : "size mismatch") : "no answer");
    return 0;
}
static int haspEncrypt(uint32_t h, uint8_t *b, uint32_t l) { return haspCrypt("encrypt", h, b, l); }
static int haspDecrypt(uint32_t h, uint8_t *b, uint32_t l) { return haspCrypt("decrypt", h, b, l); }

// ---------------------------------------------------------------------------
// Cabinet paths: /pm is the game's pm folder, and the game lives in
// /pm/g7/<game>. Either the install keeps that layout (the game started from
// pm/g7/<game>, /pm two levels up), or it is flattened: the game's folder is
// both /pm/g7/<game> and /pm (see init).
char g7Root[PATH_MAX], g7GameDir[PATH_MAX];

static const char *mapPath(const char *p, char *buf)
{
    const char *mapped;
    size_t n = strlen(game->cabinetDir);
    if (p && game->mapPath && (mapped = game->mapPath(p, buf)))
        return mapped;
    if (p && !strncmp(p, game->cabinetDir, n) && (p[n] == '/' || !p[n]))
    {
        snprintf(buf, PATH_MAX, "%s%s", g7GameDir, p + n);
        return buf;
    }
    if (p && !strncmp(p, "/pm", 3) && (p[3] == '/' || !p[3]))
    {
        snprintf(buf, PATH_MAX, "%s%s", g7Root, p + 3);
        return buf;
    }
    return p;
}
static char *mapCommand(const char *cmd, char *buf, size_t size)
{
    size_t o = 0, n = strlen(game->cabinetDir);
    for (const char *c = cmd; *c && o + 1 < size;)
    {
        if (!strncmp(c, "/pm/", 4) && (c == cmd || strchr(" \t\"'=", c[-1])))
        {
            int inGame = !strncmp(c, game->cabinetDir, n) && c[n] == '/';
            o += snprintf(buf + o, size - o, "%s", inGame ? g7GameDir : g7Root);
            c += inGame ? n : 3;
            continue;
        }
        buf[o++] = *c++;
    }
    buf[o] = 0;
    return buf;
}
static FILE *gamePopen(const char *cmd, const char *type)
{
    char b[PATH_MAX * 2];
    if (strstr(cmd, "/dev/video"))  // the room camera (player photos): none
        return popen("true", type);
    return popen(mapCommand(cmd, b, sizeof(b)), type);
}
static int gameSystem(const char *cmd) { char b[PATH_MAX * 2]; return system(mapCommand(cmd, b, sizeof(b))); }
static int gameOpen(const char *path, int flags, ...)
{
    char b[PATH_MAX];
    va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
    if (!strncmp(path, "/dev/video", 10)) { errno = ENOENT; return -1; }
    return open(mapPath(path, b), flags, mode);
}
static int gameOpen2(const char *path, int flags) { return gameOpen(path, flags, 0); }
static FILE *gameFopen(const char *path, const char *m) { char b[PATH_MAX]; return fopen(mapPath(path, b), m); }
static DIR *gameOpendir(const char *path) { char b[PATH_MAX]; return opendir(mapPath(path, b)); }
static int gameAccess(const char *path, int m) { char b[PATH_MAX]; return access(mapPath(path, b), m); }
static int gameChdir(const char *path) { char b[PATH_MAX]; return chdir(mapPath(path, b)); }
static int gameMkdir(const char *path, mode_t m) { char b[PATH_MAX]; return mkdir(mapPath(path, b), m); }
static int gameRmdir(const char *path) { char b[PATH_MAX]; return rmdir(mapPath(path, b)); }
static int gameUnlink(const char *path) { char b[PATH_MAX]; return unlink(mapPath(path, b)); }
static int gameRename(const char *a, const char *z) { char b[PATH_MAX], c[PATH_MAX]; return rename(mapPath(a, b), mapPath(z, c)); }
static ssize_t gameReadlink(const char *path, char *o, size_t n) { char b[PATH_MAX]; return readlink(mapPath(path, b), o, n); }
static int gameSymlink(const char *t, const char *path) { char b[PATH_MAX]; return symlink(t, mapPath(path, b)); }
static char *gameRealpath(const char *path, char *o, size_t n) { char b[PATH_MAX]; (void)n; return realpath(mapPath(path, b), o); }
// FMOD Studio opens the sound banks itself, on its loading thread, from the
// path the game gives it (FMOD::Studio::System::loadBankFile(path, flags,
// bank)): mapped here, since FMOD's own opens are not the game's.
#define FMOD_LOAD_BANK_FILE "_ZN4FMOD6Studio6System12loadBankFileEPKcjPPNS0_4BankE"
static int gameLoadBankFile(void *system, const char *path, unsigned flags, void **bank)
{
    static int (*real)(void *, const char *, unsigned, void **);
    char b[PATH_MAX];
    if (!real) *(void **)&real = dlsym(RTLD_DEFAULT, FMOD_LOAD_BANK_FILE);
    return real(system, mapPath(path, b), flags, bank);
}
static int (*xstat)(int, const char *, struct stat *), (*lxstat)(int, const char *, struct stat *);
static int gameXstat(int v, const char *path, struct stat *st) { char b[PATH_MAX]; return xstat(v, mapPath(path, b), st); }
static int gameLxstat(int v, const char *path, struct stat *st) { char b[PATH_MAX]; return lxstat(v, mapPath(path, b), st); }

// ---------------------------------------------------------------------------
// Video mode: the settings the game's command-line parser fills ("-f<w>x<h>"
// a fullscreen w x h; without it a 1280x720 window), set to [Display]
// WIDTH/HEIGHT instead, which Batocera sets to the screen's, so the game
// renders at the monitor's own resolution and fills it. FULLSCREEN 0 gives a
// borderless window of that size, as the other games get.
//   +0x0 window mode   0 windowed, 1 borderless, 2 fullscreen
//   +0x4 width, +0x8 height, +0xc aspect (float)
static void (*parseArgsOrig)(int, char **);
static void parseArgs(int argc, char **argv)
{
    uint8_t *video = (uint8_t *)game->videoSettings;
    parseArgsOrig(argc, argv);
    if (g7Config.width <= 0 || g7Config.height <= 0)
        return;
    *(int32_t *)video = g7Config.fullscreen ? 2 : 1;
    *(int32_t *)(video + 4) = g7Config.width;
    *(int32_t *)(video + 8) = g7Config.height;
    *(float *)(video + 12) = (float)g7Config.width / (float)g7Config.height;
    g7Log("video mode %dx%d%s\n", g7Config.width, g7Config.height, g7Config.fullscreen ? " fullscreen" : " borderless");
}

// The frame on its way to the screen: scaled to the window once the window
// no longer matches the game's size (see frameScale.h), and the light gun
// border ([Display] BORDER_ENABLED) drawn around it.  The game's statically
// linked SDL looks glXSwapBuffers up through glXGetProcAddressARB (and could
// through dlsym), and each X function through dlsym: both hand it the
// loader's swap instead, and the GL entry points the scaler redirects.
static void *(*realGetProcAddress)(const char *);
static void (*realSwap)(void *, unsigned long);
static int (*swapGetGeometry)(void *, unsigned long, unsigned long *, int *, int *,
                              unsigned int *, unsigned int *, unsigned int *, unsigned int *);
static unsigned long gameWindow;   // the drawable last presented

unsigned long g7GameWindow(void)
{
    return gameWindow;
}

static void gameSwap(void *display, unsigned long drawable)
{
    static int glLoaded;
    unsigned long root;
    int x, y, bordered = 0;
    unsigned int w, h, bw, depth;

    if (!glLoaded)
    {
        void *x11 = dlopen("libX11.so.6", RTLD_NOW);
        *(void **)&swapGetGeometry = x11 ? dlsym(x11, "XGetGeometry") : NULL;
        glLoaded = realGetProcAddress && swapGetGeometry && gladLoadGL((GLADloadfunc)realGetProcAddress) ? 1 : -1;
        if (glLoaded < 0)
            g7Log("cannot load the GL entry points, no border or scaling\n");
    }
    gameWindow = drawable;
    if (glLoaded > 0 && swapGetGeometry(display, drawable, &root, &x, &y, &w, &h, &bw, &depth) &&
        !frameScalePresent(0, 0, (int)w, (int)h, g7Config.borderEnabled, g7Config.whiteBorder, g7Config.blackBorder) &&
        g7Config.borderEnabled)
    {
        borderFrameBegin((int)w, (int)h, 1, g7Config.whiteBorder, g7Config.blackBorder);
        bordered = 1;
    }
    realSwap(display, drawable);
    if (bordered)
        borderFrameEnd();
}

static void *glEntry(const char *name, void *real)
{
    void *wrapper;
    if (!real || !name)
        return real;
    if (!strcmp(name, "glXSwapBuffers"))
    {
        realSwap = real;
        return (void *)gameSwap;
    }
    wrapper = frameScaleWrapper(name);
    return wrapper ? wrapper : real;
}

static void *gameGetProcAddress(const char *name)
{
    return glEntry(name, realGetProcAddress(name));
}

// SDL pins a window it was not asked to make resizable at its size, with
// equal minimum and maximum size hints (XSetWMProperties, then
// XSetWMNormalHints): drop them, so that the window can be resized and the
// frame scales with it.
static int (*realSetWMNormalHints)(void *, unsigned long, long *);
static int setWMNormalHints(void *display, unsigned long window, long *hints)
{
    if (hints)
        hints[0] &= ~((1L << 4) | (1L << 5));   // XSizeHints.flags: PMinSize, PMaxSize
    return realSetWMNormalHints(display, window, hints);
}
// SDL sets them first with the other properties.
static void (*realSetWMProperties)(void *, unsigned long, void *, void *, char **, int, long *, void *, void *);
static void setWMProperties(void *display, unsigned long window, void *name, void *iconName, char **argv, int argc,
                            long *hints, void *wmHints, void *classHints)
{
    if (hints)
        hints[0] &= ~((1L << 4) | (1L << 5));
    realSetWMProperties(display, window, name, iconName, argv, argc, hints, wmHints, classHints);
}

static void *gameDlsym(void *handle, const char *name)
{
    void *p = dlsym(handle, name);
    if (p && name && !strcmp(name, "XSetWMNormalHints"))
    {
        realSetWMNormalHints = p;
        return (void *)setWMNormalHints;
    }
    if (p && name && !strcmp(name, "XSetWMProperties"))
    {
        realSetWMProperties = p;
        return (void *)setWMProperties;
    }
    if (p && name && (!strcmp(name, "glXGetProcAddressARB") || !strcmp(name, "glXGetProcAddress")))
    {
        realGetProcAddress = p;
        return (void *)gameGetProcAddress;
    }
    return glEntry(name, p);
}

static const struct { const char *name; void *fn; } wrappers[] = {
    {"dlsym", gameDlsym},
    {"popen", gamePopen}, {"system", gameSystem}, {"open", gameOpen}, {"open64", gameOpen}, {"__open_2", gameOpen2},
    {"fopen", gameFopen}, {"fopen64", gameFopen}, {"opendir", gameOpendir}, {"access", gameAccess},
    {"chdir", gameChdir}, {"mkdir", gameMkdir}, {"rmdir", gameRmdir}, {"unlink", gameUnlink},
    {"rename", gameRename}, {"readlink", gameReadlink}, {"symlink", gameSymlink}, {"__realpath_chk", gameRealpath},
    {"__xstat", gameXstat}, {"__xstat64", gameXstat}, {"__lxstat", gameLxstat}, {"__lxstat64", gameLxstat},
    {FMOD_LOAD_BANK_FILE, gameLoadBankFile},
};

// ---------------------------------------------------------------------------
// Esc or Alt+F4 quits, as in the other games: a thread watches the
// keyboard with its own X connection.  The X server reports keys pressed in
// any window, so only while the focused window, or one of its parents,
// belongs to the game (_NET_WM_PID, which SDL sets).
static struct
{
    void *(*openDisplay)(const char *);
    int (*queryKeymap)(void *, char[32]);
    unsigned char (*keysymToKeycode)(void *, unsigned long);
    int (*getInputFocus)(void *, unsigned long *, int *);
    int (*queryTree)(void *, unsigned long, unsigned long *, unsigned long *, unsigned long **, unsigned int *);
    unsigned long (*internAtom)(void *, const char *, int);
    int (*getWindowProperty)(void *, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long *,
                             int *, unsigned long *, unsigned long *, unsigned char **);
    int (*freeData)(void *);
} qx;

static int gameFocused(void *display, unsigned long pidAtom)
{
    unsigned long window, root, parent, *children;
    unsigned int nchildren;
    int revert;

    qx.getInputFocus(display, &window, &revert);
    // None or PointerRoot (gamescope): no other X window has the keyboard.
    if (window <= 1)
        return 1;
    for (int depth = 0; window > 1 && depth < 16; depth++)
    {
        unsigned long type, nitems, after;
        unsigned char *data = NULL;
        int format;

        if (qx.getWindowProperty(display, window, pidAtom, 0, 1, 0, 6 /* XA_CARDINAL */, &type, &format,
                                 &nitems, &after, &data) == 0 && data)
        {
            // Xlib returns 32-bit properties as longs.
            int ours = nitems == 1 && (pid_t)*(unsigned long *)data == getpid();
            qx.freeData(data);
            return ours;
        }
        if (!qx.queryTree(display, window, &root, &parent, &children, &nchildren))
            break;
        if (children)
            qx.freeData(children);
        if (parent == root)
            break;
        window = parent;
    }
    return 0;
}

static int keyDown(const char keys[32], unsigned char code)
{
    return code && (keys[code / 8] & (1 << (code % 8)));
}

static void *quitWatch(void *arg)
{
    void *x11 = dlopen("libX11.so.6", RTLD_NOW), *display;
    (void)arg;
    if (x11)
    {
        *(void **)&qx.openDisplay = dlsym(x11, "XOpenDisplay");
        *(void **)&qx.queryKeymap = dlsym(x11, "XQueryKeymap");
        *(void **)&qx.keysymToKeycode = dlsym(x11, "XKeysymToKeycode");
        *(void **)&qx.getInputFocus = dlsym(x11, "XGetInputFocus");
        *(void **)&qx.queryTree = dlsym(x11, "XQueryTree");
        *(void **)&qx.internAtom = dlsym(x11, "XInternAtom");
        *(void **)&qx.getWindowProperty = dlsym(x11, "XGetWindowProperty");
        *(void **)&qx.freeData = dlsym(x11, "XFree");
    }
    if (!qx.openDisplay || !qx.queryKeymap || !qx.keysymToKeycode || !qx.getInputFocus || !qx.queryTree ||
        !qx.internAtom || !qx.getWindowProperty || !qx.freeData || !(display = qx.openDisplay(NULL)))
    {
        g7Log("no X display, Esc/Alt+F4 quit disabled\n");
        return NULL;
    }
    unsigned char escape = qx.keysymToKeycode(display, 0xff1b), f4 = qx.keysymToKeycode(display, 0xffc1),
                  altL = qx.keysymToKeycode(display, 0xffe9), altR = qx.keysymToKeycode(display, 0xffea);
    unsigned long pidAtom = qx.internAtom(display, "_NET_WM_PID", 0);

    for (;;)
    {
        char keys[32];

        usleep(50000);
        qx.queryKeymap(display, keys);
        if (!keyDown(keys, escape) && !(keyDown(keys, f4) && (keyDown(keys, altL) || keyDown(keys, altR))))
            continue;
        if (!gameFocused(display, pidAtom))
            continue;
        g7Log("quit from the keyboard\n");
        _exit(0);
    }
    return NULL;
}

// ---------------------------------------------------------------------------

static uintptr_t executableDynamic(void)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    return exe ? (uintptr_t)exe->l_ld : 0;
}

__attribute__((constructor)) static void init(void)
{
    int missing = 0;
    uintptr_t dynamic = executableDynamic();
    char cabinetGame[PATH_MAX];
    struct stat here, there;

    // Not a g7 game: one of the other 64-bit games (linuxloader64.so is
    // theirs too), or what a game starts.
    if (!(game = g7Find(dynamic)))
        return;
    // The cabinet's layout: the game is in g7/<game> two levels under /pm.
    // Otherwise the game's folder stands for all of /pm, with the recorded
    // answers in dump/ beside the game (or left in pm/g7/dump).
    snprintf(cabinetGame, sizeof(cabinetGame), "../..%s/game", game->cabinetDir + 3);
    if (stat("game", &here) == 0 && stat(cabinetGame, &there) == 0 && here.st_ino == there.st_ino &&
        here.st_dev == there.st_dev)
    {
        if (!realpath("../..", g7Root))
            strcpy(g7Root, "../..");
        snprintf(g7GameDir, sizeof(g7GameDir), "%s%s", g7Root, game->cabinetDir + 3);
        snprintf(dumpDir, sizeof(dumpDir), "%s/g7/dump", g7Root);
    }
    else
    {
        if (!realpath(".", g7Root))
            strcpy(g7Root, ".");
        strcpy(g7GameDir, g7Root);
        snprintf(dumpDir, sizeof(dumpDir), "%s",
                 access("dump", F_OK) == 0 || access("pm/g7/dump", F_OK) != 0 ? "dump" : "pm/g7/dump");
    }
    // The cabinet layout without its game's twin (Centipede's dump in
    // g7/centipede, answers in g7/dump): those answers.
    if (access(dumpDir, F_OK) != 0 && access("../dump", F_OK) == 0)
        strcpy(dumpDir, "../dump");
    *(void **)&xstat = dlvsym(RTLD_DEFAULT, "__xstat", "GLIBC_2.2.5");
    *(void **)&lxstat = dlvsym(RTLD_DEFAULT, "__lxstat", "GLIBC_2.2.5");
    snprintf(dongleMemPath, sizeof(dongleMemPath), "%s/dongle_memory.dmp", g7GameDir);
    dongleMemLoad();
    g7Log("%s: /pm is %s, the game's folder %s, answers in %s\n", game->name, g7Root, g7GameDir, dumpDir);
    loadConfig();
    for (size_t i = 0; i < game->importCount; i++)
    {
        const G7Import *imp = &game->imports[i];
        void *p = imp->version[0] ? dlvsym(RTLD_DEFAULT, imp->name, imp->version) : NULL;
        if (!imp->name[0])
            continue;
        for (size_t w = 0; w < sizeof(wrappers) / sizeof(wrappers[0]); w++)
            if (!strcmp(imp->name, wrappers[w].name))
                p = wrappers[w].fn;
        if (!p)
            p = dlsym(RTLD_DEFAULT, imp->name);
        if (p && !strcmp(imp->name, "glXGetProcAddressARB"))
        {
            realGetProcAddress = p;
            p = gameGetProcAddress;
        }
        else if (p && frameScaleWrapper(imp->name))
            p = frameScaleWrapper(imp->name);
        if (!p) { missing++; g7Log("missing import %s@%s (slot %#lx)\n", imp->name, imp->version, imp->slot); continue; }
        *(void **)imp->slot = p;
    }
    g7Log("imports rebuilt, %d missing\n", missing);
    frameScaleInit(realGetProcAddress, g7Config.keepAspectRatio);
    g7Detour(game->hasp.login, haspLogin);
    g7Detour(game->hasp.logout, haspOk);
    g7Detour(game->hasp.encrypt, haspEncrypt);
    g7Detour(game->hasp.decrypt, haspDecrypt);
    g7Detour(game->hasp.free, haspFree);
    g7Detour(game->hasp.sessionInfo, haspSessionInfo);
    g7Detour(game->hasp.read, haspRead);
    g7Detour(game->hasp.write, haspWrite);
    if (game->parseArgs && game->videoSettings)
        *(void **)&parseArgsOrig = g7HookNear(game->parseArgs, game->parseArgsPrologue, parseArgs);
    if (game->install)
        game->install(game);

    pthread_t quitThread;
    if (pthread_create(&quitThread, NULL, quitWatch, NULL) == 0)
        pthread_detach(quitThread);
}
