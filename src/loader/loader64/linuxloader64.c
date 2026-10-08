// linuxloader64: the 64-bit games' launcher, which the 32-bit linuxloader
// hands them to (game64.h), with its arguments:
//   -g <dir>   the game's directory (else the current one)
//   -c <ini>   the configuration (else linuxloader.ini here, if any)
// It starts the game with linuxloader64.so, from its own directory, and
// the configuration named by LINUXLOADER_CONFIG:
//
// - a g7 dump (Halo, Centipede): by the dynamic linker, which preloads the
//   library before anything in the dump runs (see rawthrills/g7/g7.h), from
//   the executable's directory, lib/ there holding the libraries the system
//   lacks;
// - a Unity game: its dumps carry the Windows player, so the game runs under
//   Unity's Linux player of the version it was built with, from a tree
//   made under /tmp at each start (see docs/nerf-arcade.md): the player, the
//   game's <name>_Data linked file by file, the player's Mono, the library
//   under the names of the cabinet plugins it stands in for, and the rest
//   of the game's directory (LocalData, its state) linked.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "game64.h"
#include "../config/iniParser.h"

#define LIBRARY "linuxloader64.so"
#define MAX_ARGS 32

static char loaderDir[PATH_MAX], library[PATH_MAX + 32];
static IniConfig *ini;

int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    va_list ap;
    (void)level; (void)file; (void)line;
    fprintf(stderr, "linuxloader64: ");
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    return 0;
}

#define say(...) fprintf(stderr, "linuxloader64: " __VA_ARGS__)

static int iniInt(const char *section, const char *key, int def)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    if (!v || !*v || !strcasecmp(v, "auto")) return def;
    if (!strcasecmp(v, "true")) return 1;
    if (!strcasecmp(v, "false")) return 0;
    return atoi(v);
}

// The 32-bit loader's environment, made for its games (the generator sets
// /lib32 first in these): their 64-bit entries only.
static void drop32(const char *name)
{
    const char *v = getenv(name);
    char out[8192] = "", copy[8192], *save, *tok;
    size_t o = 0;
    if (!v)
        return;
    snprintf(copy, sizeof(copy), "%s", v);
    for (tok = strtok_r(copy, ":", &save); tok; tok = strtok_r(NULL, ":", &save))
    {
        if (!strncmp(tok, "/lib32", 6) || !strncmp(tok, "/usr/lib32", 10) || strstr(tok, "i386") ||
            strstr(tok, "i686"))
            continue;
        o += snprintf(out + o, sizeof(out) - o, "%s%s", o ? ":" : "", tok);
        if (o >= sizeof(out))
            break;
    }
    setenv(name, out, 1);
}

static void prependEnv(const char *name, const char *dir)
{
    const char *v = getenv(name);
    char buf[8192];
    snprintf(buf, sizeof(buf), "%s%s%s", dir, v && *v ? ":" : "", v && *v ? v : "");
    setenv(name, buf, 1);
}

static void appendTunable(const char *tunable)
{
    const char *v = getenv("GLIBC_TUNABLES");
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s%s%s", v && *v ? v : "", v && *v ? ":" : "", tunable);
    setenv("GLIBC_TUNABLES", buf, 1);
}

// ---------------------------------------------------------------------------
// A g7 dump.

static void runG7(const char *gameDir, const Game64 *g)
{
    char *argv[MAX_ARGS], exe[300], size[64];
    int argc = 0, width = iniInt("Display", "WIDTH", 0), height = iniInt("Display", "HEIGHT", 0);

    if (*g->dir && chdir(g->dir) != 0)
    {
        say("cannot enter %s: %s\n", g->dir, strerror(errno));
        exit(EXIT_FAILURE);
    }
    snprintf(exe, sizeof(exe), "./%s", g->name);
    argv[argc++] = "/lib64/ld-linux-x86-64.so.2";
    argv[argc++] = "--preload";
    argv[argc++] = library;
    argv[argc++] = exe;
    // Centipede takes its size on its command line, as the cabinet gives it.
    // Its folder names it: pm/g7/centipede, or the game's own folder when
    // the dump is at its root (a flattened copy).
    if ((strcasestr(g->dir, "centipede") || strcasestr(gameDir, "centipede")) && width > 0 && height > 0)
    {
        snprintf(size, sizeof(size), "-f%dx%d", width, height);
        argv[argc++] = size;
    }
    argv[argc] = NULL;
    prependEnv("LD_LIBRARY_PATH", "lib");
    // The import table is rebuilt up front: nothing may be left for a lazy
    // resolution that would run against it.
    setenv("LD_BIND_NOW", "1", 1);
    say("g7 game %s/%s\n", *g->dir ? g->dir : ".", g->name);
    execv(argv[0], argv);
    say("cannot start %s: %s\n", argv[0], strerror(errno));
    exit(EXIT_FAILURE);
}

// ---------------------------------------------------------------------------
// A Unity game.

// The Unity version the game was built with: the serialized file header of
// <name>_Data/globalgamemanagers holds it ("2018.2.21f1").
static int unityVersion(const char *dataDir, char *out, size_t size)
{
    char path[PATH_MAX], head[64];
    ssize_t n;
    int fd;
    snprintf(path, sizeof(path), "%s/globalgamemanagers", dataDir);
    if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0)
        return 0;
    n = read(fd, head, sizeof(head) - 1);
    close(fd);
    for (ssize_t i = 0; i + 4 < n; i++)
        if (head[i] == '2' && head[i + 1] == '0' && head[i + 4] == '.')
        {
            snprintf(out, size, "%.*s", (int)strnlen(head + i, n - i), head + i);
            return 1;
        }
    return 0;
}

// Unity's Linux player of that version: a folder with LinuxPlayer and its
// Data/ (the linux64_withgfx_nondevelopment_mono variation of Unity's Linux
// build support; tools/unity-player.sh fetches it), kept with the game:
// looked for in $LINUXLOADER_UNITY, then the game's unity/<version> and
// unity/, then unity/<version> beside the loader.
static int findPlayer(const char *gameDir, const char *version, char *out, size_t size)
{
    const char *env = getenv("LINUXLOADER_UNITY");
    char candidates[4][PATH_MAX];
    int n = 0;
    if (env && *env)
        snprintf(candidates[n++], PATH_MAX, "%s", env);
    snprintf(candidates[n++], PATH_MAX, "%s/unity/%s", gameDir, version);
    snprintf(candidates[n++], PATH_MAX, "%s/unity", gameDir);
    snprintf(candidates[n++], PATH_MAX, "%s/unity/%s", loaderDir, version);
    for (int i = 0; i < n; i++)
    {
        char player[PATH_MAX + 16];
        snprintf(player, sizeof(player), "%s/LinuxPlayer", candidates[i]);
        if (access(player, R_OK) == 0)
        {
            snprintf(out, size, "%s", candidates[i]);
            return 1;
        }
    }
    say("no Unity %s Linux player: put it in %s/unity/%s (tools/unity-player.sh %s <changeset> <game>/unity)\n",
        version, gameDir, version, version);
    return 0;
}

static int removeEntry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st; (void)flag; (void)ftw;
    return remove(path);
}

static void makeLink(const char *target, const char *link)
{
    if (symlink(target, link) != 0)
        say("cannot link %s: %s\n", link, strerror(errno));
}

static int copyFile(const char *from, const char *to, mode_t mode)
{
    char buf[1 << 16];
    ssize_t n;
    int in = open(from, O_RDONLY | O_CLOEXEC), out, ok = 1;
    if (in < 0)
        return 0;
    if ((out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode)) < 0)
    {
        close(in);
        return 0;
    }
    while ((n = read(in, buf, sizeof(buf))) > 0)
        if (write(out, buf, n) != n)
        {
            ok = 0;
            break;
        }
    close(in);
    close(out);
    return ok && n == 0;
}

// The cabinet's native plugins the library stands in for (rawthrills/nerf):
// the RIO board, the dongle and its glue; the RIO2 the game never calls.
static const char *pluginStandIn(const char *name)
{
    if (!strcmp(name, "librio.so") || !strcmp(name, "libUnityNatives.so") || !strncmp(name, "libhasp_linux_", 14))
        return library;
    return NULL;
}

// The plugins a game's dumps lack, the cabinet's Linux ones gone with their
// player: the library under their names in <name>_Data/Plugins/x86_64.
// Superbikes 3 (rawthrills/sb3): the RIO boards, the dongle, the handlebar's
// motor, the camera.
static void addStandIns(const char *dataDir, const Game64 *g)
{
    static const char *const sb3[] = {"librio.so",     "libRIO2.so",  "libUnityNatives.so", "libhasp_linux_23557.so",
                                      "libwheel.so", "libWebCamCV.so", NULL};
    char path[PATH_MAX * 2];
    if (strcmp(g->name, "fnfmega"))
        return;
    snprintf(path, sizeof(path), "%s/Plugins/x86_64", dataDir);
    mkdir(path, 0755);
    for (const char *const *n = sb3; *n; n++)
    {
        snprintf(path, sizeof(path), "%s/Plugins/x86_64/%s", dataDir, *n);
        if (access(path, F_OK) != 0)
            makeLink(library, path);
    }
}

// <name>_Data/Plugins: the game's, the stand-ins in place of the cabinet's,
// the player's own (ScreenSelector.so) before the game's copy.
static void linkPlugins(const char *from, const char *to, const char *playerPlugins)
{
    DIR *d = opendir(from);
    struct dirent *e;
    mkdir(to, 0755);
    while (d && (e = readdir(d)))
    {
        char src[PATH_MAX * 2], dst[PATH_MAX * 2], own[PATH_MAX * 2];
        struct stat st;
        if (e->d_name[0] == '.')
            continue;
        snprintf(src, sizeof(src), "%s/%s", from, e->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", to, e->d_name);
        snprintf(own, sizeof(own), "%s/%s", playerPlugins, e->d_name);
        if (stat(src, &st) == 0 && S_ISDIR(st.st_mode))
        {
            char ownSub[PATH_MAX * 2];
            snprintf(ownSub, sizeof(ownSub), "%s/%s", playerPlugins, e->d_name);
            linkPlugins(src, dst, ownSub);
        }
        else if (!strcmp(e->d_name, "libRIO2.so"))
            continue;
        else if (pluginStandIn(e->d_name))
            makeLink(pluginStandIn(e->d_name), dst);
        else
            makeLink(access(own, R_OK) == 0 ? own : src, dst);
    }
    if (d)
        closedir(d);
}

static void runUnity(const char *gameDir, const Game64 *g)
{
    char data[PATH_MAX], version[32], player[PATH_MAX], tree[PATH_MAX], path[PATH_MAX * 2], from[PATH_MAX * 2];
    char exe[PATH_MAX], width[16], height[16], fullscreen[4];
    char *argv[MAX_ARGS];
    int argc = 0, w = iniInt("Display", "WIDTH", 0), h = iniInt("Display", "HEIGHT", 0),
        fs = iniInt("Display", "FULLSCREEN", 0);
    DIR *d;
    struct dirent *e;

    snprintf(data, sizeof(data), "%s/%s_Data", gameDir, g->name);
    if (!unityVersion(data, version, sizeof(version)))
    {
        say("%s: no Unity version found\n", data);
        exit(EXIT_FAILURE);
    }
    if (!findPlayer(gameDir, version, player, sizeof(player)))
        exit(EXIT_FAILURE);
    say("Unity %s game %s, player %s\n", version, g->name, player);

    // The tree, made again at each start.
    snprintf(tree, sizeof(tree), "/tmp/linuxloader64-%u", (unsigned)getuid());
    mkdir(tree, 0700);
    snprintf(tree + strlen(tree), sizeof(tree) - strlen(tree), "/%s", g->name);
    nftw(tree, removeEntry, 16, FTW_DEPTH | FTW_PHYS);
    if (mkdir(tree, 0755) != 0)
    {
        say("cannot make %s: %s\n", tree, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // The player, under the game's name: it finds <name>_Data beside its
    // own executable (/proc/self/exe), so it is a file of the tree.
    snprintf(exe, sizeof(exe), "%s/%s.x86_64", tree, g->name);
    snprintf(from, sizeof(from), "%s/LinuxPlayer", player);
    if (link(from, exe) != 0 && !copyFile(from, exe, 0755))
    {
        say("cannot copy %s: %s\n", from, strerror(errno));
        exit(EXIT_FAILURE);
    }
    chmod(exe, 0755);

    // <name>_Data: the game's, Mono and the plugins from the player.
    snprintf(path, sizeof(path), "%s/%s_Data", tree, g->name);
    mkdir(path, 0755);
    if ((d = opendir(data)))
    {
        while ((e = readdir(d)))
        {
            char dst[PATH_MAX * 3];
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "MonoBleedingEdge") || !strcmp(e->d_name, "Mono") ||
                !strcmp(e->d_name, "Plugins"))
                continue;
            snprintf(from, sizeof(from), "%s/%s", data, e->d_name);
            snprintf(dst, sizeof(dst), "%s/%s", path, e->d_name);
            makeLink(from, dst);
        }
        closedir(d);
    }
    {
        char dst[PATH_MAX * 3], ownPlugins[PATH_MAX * 2];
        snprintf(from, sizeof(from), "%s/Data/MonoBleedingEdge", player);
        snprintf(dst, sizeof(dst), "%s/MonoBleedingEdge", path);
        if (access(from, F_OK) == 0)
            makeLink(from, dst);
        snprintf(from, sizeof(from), "%s/Data/Mono", player);
        snprintf(dst, sizeof(dst), "%s/Mono", path);
        if (access(from, F_OK) == 0)
            makeLink(from, dst);
        snprintf(from, sizeof(from), "%s/Plugins", data);
        snprintf(dst, sizeof(dst), "%s/Plugins", path);
        snprintf(ownPlugins, sizeof(ownPlugins), "%s/Data/Plugins", player);
        linkPlugins(from, dst, ownPlugins);
        addStandIns(path, g);
        // The game's plugins find each other there (one needing another,
        // as FMOD's studio library its core one).
        snprintf(dst, sizeof(dst), "%s/Plugins/x86_64", path);
        prependEnv("LD_LIBRARY_PATH", dst);
        // Plugins built asking for an executable stack (FMOD 1.10's), which
        // glibc 2.41 and later refuse to dlopen unless told (older ones
        // ignore the tunable).
        appendTunable("glibc.rtld.execstack=2");
    }

    // The rest of the game's directory, but the Windows player's files:
    // the game reads and writes it from its current directory (LocalData).
    snprintf(from, sizeof(from), "%s/LocalData", gameDir);
    mkdir(from, 0755);
    if ((d = opendir(gameDir)))
    {
        size_t dataLen = strlen(g->name) + 5;
        while ((e = readdir(d)))
        {
            const char *dot = strrchr(e->d_name, '.');
            char dst[PATH_MAX * 3];
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "MonoBleedingEdge") || !strcmp(e->d_name, "Plugins") ||
                !strcmp(e->d_name, "unity") || (dot && (!strcasecmp(dot, ".exe") || !strcasecmp(dot, ".dll"))) ||
                (strlen(e->d_name) == dataLen && !strncmp(e->d_name, g->name, dataLen - 5)))
                continue;
            snprintf(from, sizeof(from), "%s/%s", gameDir, e->d_name);
            snprintf(dst, sizeof(dst), "%s/%s", tree, e->d_name);
            makeLink(from, dst);
        }
        closedir(d);
    }

    // [Display]: the size (else a 1280x720 window), FULLSCREEN, or a
    // borderless window of the size given (the screen's, from the
    // generator), as the other games have. Nerf Arcade only renders at its
    // cabinet's 1920x1080: its shots go through the reticle's canvas
    // position taken as screen pixels.
    if (!strcmp(g->name, "Nerf"))
    {
        w = 1920;
        h = 1080;
    }
    snprintf(width, sizeof(width), "%d", w > 0 ? w : 1280);
    snprintf(height, sizeof(height), "%d", h > 0 ? h : 720);
    snprintf(fullscreen, sizeof(fullscreen), "%d", fs ? 1 : 0);
    argv[argc++] = exe;
    argv[argc++] = "-screen-width";
    argv[argc++] = width;
    argv[argc++] = "-screen-height";
    argv[argc++] = height;
    argv[argc++] = "-screen-fullscreen";
    argv[argc++] = fullscreen;
    if (!fs && w > 0 && h > 0)
        argv[argc++] = "-popupwindow";
    argv[argc] = NULL;

    if (chdir(tree) != 0)
    {
        say("cannot enter %s: %s\n", tree, strerror(errno));
        exit(EXIT_FAILURE);
    }
    setenv("LD_PRELOAD", library, 1);
    // Mono takes its culture from the locale: the games parse their data
    // (numbers in their text files) as on the cabinet's English system,
    // with a decimal point.
    setenv("LC_ALL", "C", 1);
    execv(exe, argv);
    say("cannot start %s: %s\n", exe, strerror(errno));
    exit(EXIT_FAILURE);
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    char gameDir[PATH_MAX] = ".", config[PATH_MAX] = "", cwd[PATH_MAX], *slash;
    ssize_t n;
    Game64 g;

    for (int i = 1; i < argc; i++)
    {
        if ((!strcmp(argv[i], "-g") || !strcmp(argv[i], "--gamepath")) && i + 1 < argc)
            snprintf(gameDir, sizeof(gameDir), "%s", argv[++i]);
        else if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")) && i + 1 < argc)
            snprintf(config, sizeof(config), "%s", argv[++i]);
        else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--test"))
            say("-t: the 64-bit games open their test menu from their own button\n");
    }

    if ((n = readlink("/proc/self/exe", loaderDir, sizeof(loaderDir) - 1)) <= 0)
    {
        say("cannot find myself\n");
        return EXIT_FAILURE;
    }
    loaderDir[n] = 0;
    if ((slash = strrchr(loaderDir, '/')))
        *slash = 0;
    snprintf(library, sizeof(library), "%s/%s", loaderDir, LIBRARY);
    if (access(library, R_OK) != 0)
    {
        say("%s not found\n", library);
        return EXIT_FAILURE;
    }

    // The configuration, an absolute path: the game runs elsewhere.
    if (!getcwd(cwd, sizeof(cwd)))
        return EXIT_FAILURE;
    if (!*config && access("linuxloader.ini", R_OK) == 0)
        snprintf(config, sizeof(config), "linuxloader.ini");
    if (*config)
    {
        char abs[PATH_MAX * 2];
        snprintf(abs, sizeof(abs), "%s%s%s", *config == '/' ? "" : cwd, *config == '/' ? "" : "/", config);
        if (!(ini = iniLoad(abs)))
            say("cannot read %s\n", abs);
        else
            setenv("LINUXLOADER_CONFIG", abs, 1);
    }

    if (!realpath(gameDir, cwd))
    {
        say("no game directory %s\n", gameDir);
        return EXIT_FAILURE;
    }
    if (chdir(cwd) != 0)
        return EXIT_FAILURE;
    drop32("LD_LIBRARY_PATH");
    drop32("LIBGL_DRIVERS_PATH");
    drop32("SPA_PLUGIN_DIR");
    drop32("PIPEWIRE_MODULE_DIR");
    unsetenv("LD_PRELOAD");

    g = game64Find(".");
    switch (g.kind)
    {
        case GAME64_G7:
            runG7(cwd, &g);
            break;
        case GAME64_UNITY:
            runUnity(cwd, &g);
            break;
        default:
            break;
    }
    say("no 64-bit game in %s\n", cwd);
    return EXIT_FAILURE;
}
