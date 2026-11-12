// Cosmodog games (see cosmodog.h): their cabinet's paths and the start-up
// they need.
//
// The game keeps its cabinet's Gentoo up to date itself, as root: it
// remounts its disks, makes its card reader's device node, writes the
// network's, NTP's and the boot loader's settings, sets the clock and runs
// the tools for all that. None of it reaches the host: the system's files it
// writes are the game directory's env/ (as TeknoParrot's dumps have it), its
// commands and mounts do nothing.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glad/gl.h>

#include "cosmodog.h"
#include "../common/importHook.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const CosmodogGame *cosmodogCurrentGame(void)
{
    static uint32_t cachedId;
    static const CosmodogGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = cosmodogGetGame(gId);
    }
    return game;
}

int isCosmodogGame(void)
{
    return cosmodogCurrentGame() != NULL;
}

static int underDir(const char *path, const char *dir)
{
    size_t n = strlen(dir);

    return !strncmp(path, dir, n) && (path[n] == '/' || path[n] == '\0');
}

static const char *const systemFiles[] = {
    "/boot",           "/etc/conf.d",    "/etc/hosts",       "/etc/hostname",          "/etc/ntp.conf",
    "/etc/resolv.conf", "/etc/sudoers",  "/etc/sysconfig",   "/etc/init.d",            "/etc/mtab",
    "/etc/asound.state", "/etc/modprobe.conf", "/etc/modules.autoload.d", "/etc/ndiswrapper", NULL,
};

const char *cosmodogRedirectPath(const char *path, char *buf, size_t size)
{
    const CosmodogGame *g = cosmodogCurrentGame();

    if (!g || !path || !gameDir[0])
        return path;
    if (underDir(path, g->rootPath))
    {
        snprintf(buf, size, "%s%s", gameDir, path + strlen(g->rootPath));
        return buf;
    }
    if (underDir(path, g->dataPath))
    {
        snprintf(buf, size, "%s/data%s", gameDir, path + strlen(g->dataPath));
        return buf;
    }
    // The system's settings the game writes, and the boot loader's (not the
    // ones the host's libraries read: ALSA's, fonts').
    for (const char *const *dir = systemFiles; *dir; dir++)
        if (underDir(path, *dir))
        {
            snprintf(buf, size, "%s/env%s", gameDir, path);
            return buf;
        }
    return path;
}

__attribute__((force_align_arg_pointer)) static int gameSystem(const char *command)
{
    if (command)
        printf("Cosmodog: not run: %s\n", command);
    return 0;
}

// Its commands' output, read through popen: what the cabinet's would say
// (its parsers take a line for granted: an empty output corrupts the heap).
// The RAM disk is free, the wireless network has a name, the network is
// down (pings get no answer), the interface scripts say nothing.
__attribute__((force_align_arg_pointer)) static FILE *gamePopen(const char *command, const char *mode)
{
    static const struct
    {
        const char *command, *output;
    } outputs[] = {
        {"df ", "Filesystem           1K-blocks      Used Available Use% Mounted on\n"
                "/dev/ram0               750000        16    749984   1% /media/rd0\n"},
        {"iwgetid ", "cosmodog\n"},
        {"ping ", "PING topdog.bowl-o-rama.com: 56 data bytes\n"
                  "--- topdog.bowl-o-rama.com ping statistics ---\n"
                  "3 packets transmitted, 0 received, 100% packet loss\n"},
    };
    const char *output = "\n";

    if (mode && mode[0] == 'w')
        return fopen("/dev/null", "w");
    if (command)
    {
        printf("Cosmodog: not run: %s\n", command);
        for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); i++)
            if (!strncmp(command, outputs[i].command, strlen(outputs[i].command)))
                output = outputs[i].output;
    }
    return fmemopen((void *)output, strlen(output), "r");
}

__attribute__((force_align_arg_pointer)) static int gamePclose(FILE *stream)
{
    return stream ? fclose(stream) : -1;
}

__attribute__((force_align_arg_pointer)) static int gameMount(const char *source, const char *target,
                                                              const char *type, unsigned long flags, const void *data)
{
    (void)source;
    (void)type;
    (void)flags;
    (void)data;
    printf("Cosmodog: not mounted: %s\n", target ? target : "");
    return 0;
}

__attribute__((force_align_arg_pointer)) static int gameUmount(const char *target)
{
    (void)target;
    return 0;
}

__attribute__((force_align_arg_pointer)) static int gameMknod(int ver, const char *path, mode_t mode, dev_t *dev)
{
    (void)ver;
    (void)mode;
    (void)dev;
    printf("Cosmodog: no device node %s\n", path ? path : "");
    return 0;
}

__attribute__((force_align_arg_pointer)) static int gameSetTime(const void *tv, const void *tz)
{
    (void)tv;
    (void)tz;
    return 0;
}

// ALSA's: the cabinet's sound card (plughw:0,0) is the host's sound server's
// today; the game plays through the default device.
static int (*realPcmOpen)(void **, const char *, int, int);

__attribute__((force_align_arg_pointer)) static int gamePcmOpen(void **pcm, const char *name, int stream, int mode)
{
    if (name && (!strncmp(name, "hw:", 3) || !strncmp(name, "plughw:", 7)))
        name = "default";
    return realPcmOpen(pcm, name, stream, mode);
}

// The cabinet image's files (its own and Gentoo's, the wireless card's
// driver, ImageMagick): valid.
__attribute__((force_align_arg_pointer)) static int dataValid(void)
{
    return 1;
}

// COSMODOG_SHOT=<directory> (debugging): a frame every COSMODOG_SHOT_PERIOD
// seconds (4) saved there as a PPM, read back before the swap.
static void (*realSwapBuffers)(void);

__attribute__((force_align_arg_pointer)) static void swapBuffers(void)
{
    static double next = -1;
    static int count;
    const char *dir = getenv("COSMODOG_SHOT");
    struct timespec ts;
    double now;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (dir && now >= next && glad_glReadPixels)
    {
        GLint viewport[4];
        char path[PATH_MAX];
        unsigned char *pixels;
        FILE *f;

        next = now + (getenv("COSMODOG_SHOT_PERIOD") ? atof(getenv("COSMODOG_SHOT_PERIOD")) : 4);
        glad_glGetIntegerv(GL_VIEWPORT, viewport);
        snprintf(path, sizeof(path), "%s/shot_%04d.ppm", dir, count);
        printf("Cosmodog: shot %d\n", count++);
        pixels = malloc((size_t)viewport[2] * viewport[3] * 3);
        if (pixels && (f = fopen(path, "wb")))
        {
            glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glad_glReadBuffer(GL_BACK);
            glad_glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGB, GL_UNSIGNED_BYTE, pixels);
            fprintf(f, "P6\n%d %d\n255\n", viewport[2], viewport[3]);
            for (int y = viewport[3]; y-- > 0;)
                fwrite(pixels + (size_t)y * viewport[2] * 3, 1, (size_t)viewport[2] * 3, f);
            fclose(f);
        }
        free(pixels);
    }
    realSwapBuffers();
}

int cosmodogInit(void)
{
    const CosmodogGame *g = cosmodogCurrentGame();
    char path[PATH_MAX];

    // Started in the game's directory, the cabinet's rootPath.
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    snprintf(path, sizeof(path), "%s/env", gameDir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/env/etc", gameDir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/env/etc/conf.d", gameDir);
    mkdir(path, 0755);

    hookExecutableImport("system", (void *)gameSystem);
    hookExecutableImport("popen", (void *)gamePopen);
    hookExecutableImport("pclose", (void *)gamePclose);
    hookExecutableImport("mount", (void *)gameMount);
    hookExecutableImport("umount", (void *)gameUmount);
    hookExecutableImport("__xmknod", (void *)gameMknod);
    hookExecutableImport("settimeofday", (void *)gameSetTime);
    *(void **)&realPcmOpen = hookExecutableImport("snd_pcm_open", (void *)gamePcmOpen);
    if (!realPcmOpen)
        log_warn("Cosmodog: no snd_pcm_open import");
    // (the swap the game's import reaches is the loader's glut bridge's)
    if (getenv("COSMODOG_SHOT") && hookExecutableImport("glutSwapBuffers", (void *)swapBuffers))
        *(void **)&realSwapBuffers = dlsym(RTLD_DEFAULT, "glutSwapBuffers");
    detourFunction(g->dataValid, dataValid);
    cosmodogIoInstall(g);
    return 0;
}
