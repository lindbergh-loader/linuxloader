// Global VR games (see gvr.h): their cabinet's paths and the start-up they
// need.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gvr.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../namco/namcoEs1.h"
#include "../patching/flowControl.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const GvrGame *gvrCurrentGame(void)
{
    static uint32_t cachedId;
    static const GvrGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = gvrGetGame(gId);
    }
    return game;
}

int isGvrGame(void)
{
    return gvrCurrentGame() != NULL;
}

const char *gvrRedirectPath(const char *path, char *buf, size_t size)
{
    const GvrGame *g = gvrCurrentGame();
    size_t n;

    if (!g || !path || !gameDir[0])
        return path;
    n = strlen(g->rootPath);
    if (strncmp(path, g->rootPath, n) || (path[n] != '/' && path[n] != '\0'))
        return path;
    snprintf(buf, size, "%s%s", gameDir, path + n);
    return buf;
}

// One ini setting: section's key set to value, in lines (a file read whole,
// rewritten in place). The key is added at the section's end if missing.
static char *iniSet(char *text, const char *section, const char *key, const char *value)
{
    char header[128], *out;
    size_t keyLength = strlen(key), size = strlen(text) + strlen(section) + keyLength + strlen(value) + 16;
    int inSection = 0, done = 0;

    snprintf(header, sizeof(header), "[%s]", section);
    if (!(out = malloc(size)))
        return text;
    out[0] = '\0';
    for (char *line = text, *next; line && *line; line = next)
    {
        char *end = strchr(line, '\n');
        next = end ? end + 1 : NULL;
        size_t length = end ? (size_t)(next - line) : strlen(line);
        if (line[0] == '[')
        {
            if (inSection && !done)
            {
                sprintf(out + strlen(out), "%s=%s\n", key, value);
                done = 1;
            }
            inSection = !strncmp(line, header, strlen(header));
        }
        else if (inSection && !done && !strncmp(line, key, keyLength) && line[keyLength] == '=')
        {
            sprintf(out + strlen(out), "%s=%s%s\n", key, value, end && end > line && end[-1] == '\r' ? "\r" : "");
            done = 1;
            continue;
        }
        strncat(out, line, length);
    }
    if (!done)
        sprintf(out + strlen(out), "%s%s=%s\n", inSection ? "" : header, key, value);
    free(text);
    return out;
}

static char *readText(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    long size;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (text = malloc(size + 1)))
    {
        size = fread(text, 1, size, f);
        text[size] = '\0';
    }
    fclose(f);
    return text;
}

static void writeText(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "Global VR: cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    fputs(text, f);
    fclose(f);
}

// America's Army keeps its settings in $HOME/.armyops260/System: home/ in
// the game's directory (the System directory's parent). ArmyOps.ini is made
// from Default.ini (the Windows one: its viewport WinDrv) if missing, and is
// set to SDL's at 640x480, the mouse not captured (the desktop's pointer is
// player 1's gun, gvrLink.c). Fullscreen (scaled to the screen by
// sdl12-compat) as the loader's config says, or when it asks for a picture
// larger than the game's (Batocera's: the screen's size, not fullscreen).
// User.ini (from DefUser.ini) loses the cabinet's keys (F1, F2 start, C coin,
// O, T test): the I/O link has them, the loader's keys (1, 2, 5, F2) too.
// OpenAL's backend is OSS: the loader's /dev/dsp.
static void gvrSetUpHome(void)
{
    char home[PATH_MAX], path[PATH_MAX + 64], source[PATH_MAX + 64];
    char *text;

    snprintf(home, sizeof(home), "%s", gameDir);
    char *slash = strrchr(home, '/');
    if (slash && slash != home)
        *slash = '\0';
    strncat(home, "/home", sizeof(home) - strlen(home) - 1);
    setenv("HOME", home, 1);
    mkdir(home, 0755);
    snprintf(path, sizeof(path), "%s/.armyops260", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.armyops260/System", home);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s/.armyops260/System/ArmyOps.ini", home);
    snprintf(source, sizeof(source), "%s/Default.ini", gameDir);
    if ((text = readText(path)) || (text = readText(source)))
    {
        text = iniSet(text, "Engine.Engine", "ViewportManager", "SDLDrv.SDLClient");
        text = iniSet(text, "SDLDrv.SDLClient", "WindowedViewportX", "640");
        text = iniSet(text, "SDLDrv.SDLClient", "WindowedViewportY", "480");
        text = iniSet(text, "SDLDrv.SDLClient", "FullscreenViewportX", "640");
        text = iniSet(text, "SDLDrv.SDLClient", "FullscreenViewportY", "480");
        text = iniSet(text, "SDLDrv.SDLClient", "MenuViewportX", "640");
        text = iniSet(text, "SDLDrv.SDLClient", "MenuViewportY", "480");
        int fullscreen = getConfig()->fullscreen || getConfig()->width > 640 || getConfig()->height > 480;
        text = iniSet(text, "SDLDrv.SDLClient", "StartupFullscreen", fullscreen ? "True" : "False");
        text = iniSet(text, "SDLDrv.SDLClient", "CaptureMouse", "False");
        writeText(path, text);
        free(text);
    }

    snprintf(path, sizeof(path), "%s/.armyops260/System/User.ini", home);
    snprintf(source, sizeof(source), "%s/DefUser.ini", gameDir);
    if ((text = readText(path)) || (text = readText(source)))
    {
        static const char *const cabinetKeys[] = {"F1", "F2", "C", "O", "T"};
        for (size_t i = 0; i < sizeof(cabinetKeys) / sizeof(cabinetKeys[0]); i++)
            text = iniSet(text, "Engine.Input", cabinetKeys[i], "");
        writeText(path, text);
        free(text);
    }

    snprintf(path, sizeof(path), "%s/.openalrc", home);
    writeText(path, "(define devices '(oss))\n");
}

int gvrInit(void)
{
    const GvrGame *g = gvrCurrentGame();

    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';

    if (g->ownWindow)
    {
        gvrSetUpHome();
        gvrCrosshairInstall();
        return 0;
    }

    // Esc (Batocera's hotkey exit sends it) and Alt+F4 quit in every input
    // mode: the Lindbergh path's own Esc is an SDL input binding, and
    // evdev input (INPUT_MODE 2) has none.
    desktopStartQuitWatch();

    // SELECT starts a game only on the cabinet's own disk (see gvrGame.h).
    if (g->diskSerialCheck)
        detourFunction(g->diskSerialCheck, stubRetOne);

    // Built for a 4-byte-aligned stack: the host's libraries fault on it
    // (libX11's SSE moves in XOpenDisplay). Its imports go through stubs
    // aligning it, as Dead Heat's (see namco/namcoEs1Align.c).
    if (g->frameWidth && g->frameHeight)
        gvrFrameInstall(g);
    namcoEs1AlignImports();
    return 0;
}
