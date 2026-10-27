// Roxor games (see roxor.h): their cabinet's paths and the start-up they
// need.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glad/gl.h>

#include "roxor.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../namco/namcoEs1.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const RoxorGame *roxorCurrentGame(void)
{
    static uint32_t cachedId;
    static const RoxorGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = roxorGetGame(gId);
    }
    return game;
}

int isRoxorGame(void)
{
    return roxorCurrentGame() != NULL;
}

const char *roxorRedirectPath(const char *path, char *buf, size_t size)
{
    const RoxorGame *g = roxorCurrentGame();
    const char *settings;
    size_t n;

    if (!g || !path || !gameDir[0])
        return path;
    n = strlen(g->rootPath);
    if (!strncmp(path, g->rootPath, n) && (path[n] == '/' || path[n] == '\0'))
    {
        snprintf(buf, size, "%s%s", gameDir, path + n);
        return buf;
    }
    // $HOME/<settings>[/...], wherever HOME is.
    n = strlen(g->settings);
    for (settings = strstr(path, g->settings); settings; settings = strstr(settings + 1, g->settings))
        if (settings > path && settings[-1] == '/' && (settings[n] == '/' || settings[n] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, g->saveDir, settings + n);
            return buf;
        }
    return path;
}

// The options file (Tcl, "set <name> <value>" lines): data_dir is the
// cabinet's data directory (the dumps' is TeknoParrot's Windows one), and
// the game is fullscreen (sdl12-compat scales its 640x480 to the screen) as
// the loader's config says, or when it asks for a picture larger than the
// game's (Batocera's: the screen's size, not fullscreen).
static void setOptions(const RoxorGame *g)
{
    char path[PATH_MAX], tmp[PATH_MAX + 8], line[1024];
    int fullscreen = getConfig()->fullscreen || getConfig()->width > 640 || getConfig()->height > 480;
    FILE *in, *out;

    snprintf(path, sizeof(path), "%s/%s/options", gameDir, g->saveDir);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if (!(in = fopen(path, "r")))
        return;
    if (!(out = fopen(tmp, "w")))
    {
        fclose(in);
        return;
    }
    while (fgets(line, sizeof(line), in))
    {
        if (!strncmp(line, "set data_dir ", 13))
            fprintf(out, "set data_dir \"%s/%s\"\n", g->rootPath, g->dataDir);
        else if (!strncmp(line, "set fullscreen ", 15))
            fprintf(out, "set fullscreen %s\n", fullscreen ? "true" : "false");
        else
            fputs(line, out);
    }
    fclose(in);
    if (fclose(out) == 0)
        rename(tmp, path);
    else
        unlink(tmp);
}

// The game links libGL: its GL calls (and libGLU's) reach the loader's GL
// wrappers, which call through glad's entry points. The Lindbergh path loads
// them once its window's context is current; this game's is SDL's, made by
// SDL_SetVideoMode: loaded right after it.
static void *(*realSetVideoMode)(int, int, int, unsigned int);

__attribute__((force_align_arg_pointer)) static void *setVideoMode(int width, int height, int bpp, unsigned int flags)
{
    static int glLoaded;
    void *surface = realSetVideoMode(width, height, bpp, flags);

    if (surface && !glLoaded)
    {
        GLADloadfunc getProcAddress = (GLADloadfunc)dlsym(RTLD_NEXT, "glXGetProcAddressARB");
        glLoaded = 1;
        if (!getProcAddress || !gladLoadGL(getProcAddress))
            log_error("Roxor: cannot load the GL entry points");
    }
    return surface;
}

int roxorInit(void)
{
    const RoxorGame *g = roxorCurrentGame();
    char data[PATH_MAX];

    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    setOptions(g);
    // The game runs from its data directory (its Tcl scripts are "./").
    snprintf(data, sizeof(data), "%s/%s", gameDir, g->dataDir);
    if (chdir(data) != 0)
        fprintf(stderr, "Roxor: cannot enter %s\n", data);
    *(void **)&realSetVideoMode = hookExecutableImport("SDL_SetVideoMode", (void *)setVideoMode);
    if (!realSetVideoMode)
        log_warn("Roxor: no SDL_SetVideoMode import, GL is not loaded");
    roxorIoInstall(g);
    // Built by gcc 2.95, for a 4-byte-aligned stack: the host's libraries
    // fault on it (libvorbisfile's SSE moves in the music's Mix_LoadMUS). Its
    // imports go through stubs aligning it, as Dead Heat's (see
    // namco/namcoEs1Align.c); after the hooks on them above.
    if (!getenv("ROXOR_NOALIGN"))
        namcoEs1AlignImports();
    return 0;
}
