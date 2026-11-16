// Byoyoyo games (see byoyoyo.h): the start-up they need.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glad/gl.h>
#include <X11/Xlib.h>

#include "byoyoyo.h"
#include "../common/desktopInput.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/bezel.h"
#include "../log/log.h"

extern uint32_t gId;

const ByoyoyoGame *byoyoyoCurrentGame(void)
{
    static uint32_t cachedId;
    static const ByoyoyoGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = byoyoyoGetGame(gId);
    }
    return game;
}

int isByoyoyoGame(void)
{
    return byoyoyoCurrentGame() != NULL;
}

// The runner's path: its directory holds assets/. Started through the
// host's dynamic linker (its dump lost the executable mode, see
// hostLinker.c), /proc/self/exe is the linker; the runner is the game
// directory's.
static char runnerPath[PATH_MAX];

__attribute__((force_align_arg_pointer)) static ssize_t gameReadlink(const char *path, char *buf, size_t size)
{
    if (runnerPath[0] && path && !strcmp(path, "/proc/self/exe"))
    {
        size_t length = strlen(runnerPath);
        if (length > size)
            length = size;
        memcpy(buf, runnerPath, length);
        return (ssize_t)length;
    }
    return readlink(path, buf, size);
}

static Window (*realCreateWindow)(Display *, Window, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, Visual *,
                                  unsigned long, XSetWindowAttributes *);

// The runner's first window: the game's, for the quit and the controls. Its
// second (the 480x240 one) takes no keys.
__attribute__((force_align_arg_pointer)) static Window gameCreateWindow(Display *display, Window parent, int x, int y, unsigned int width,
                                                                        unsigned int height, unsigned int border, int depth,
                                                                        unsigned int windowClass, Visual *visual, unsigned long mask,
                                                                        XSetWindowAttributes *attributes)
{
    static Window gameWindow;
    Window window = realCreateWindow(display, parent, x, y, width, height, border, depth, windowClass, visual, mask, attributes);
    if (window && !gameWindow)
    {
        gameWindow = window;
        desktopNoteGameWindow(window);
        byoyoyoInputWindow(window);
    }
    return window;
}

// The bezel ([Display] BEZEL, the game directory's bezel.png by default, see
// bezel.h), drawn over each frame before its swap. The runner fits its 4:3
// picture to the window's height, centred; the bezel (a 16:9 image whose
// hole is that picture) is drawn at the window's height too, centred: its
// sides outside a window narrower than 16:9.
static void (*realSwapBuffers)(Display *, XID);

__attribute__((force_align_arg_pointer)) static void gameSwapBuffers(Display *display, XID drawable)
{
    static int glLoaded;
    Window root;
    int x, y;
    unsigned int width, height, border, depth;

    if (!glLoaded)
    {
        GLADloadfunc getProcAddress = (GLADloadfunc)dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
        glLoaded = getProcAddress && gladLoadGL(getProcAddress) ? 1 : -1;
        if (glLoaded < 0)
            log_warn("Byoyoyo: cannot load the GL entry points, no bezel");
    }
    if (glLoaded > 0 && XGetGeometry(display, drawable, &root, &x, &y, &width, &height, &border, &depth) && height)
    {
        int bezelWidth = (int)height * 16 / 9;
        bezelDraw(((int)width - bezelWidth) / 2, 0, bezelWidth, (int)height);
    }
    realSwapBuffers(display, drawable);
}

static void bezelInstall(const char *gameDir)
{
    const char *bezel = getConfig()->bezel;
    int enabled = getConfig()->bezelEnabled;
    char path[PATH_MAX];

    if (bezel[0] && bezel[0] != '/')
    {
        snprintf(path, sizeof(path), "%s/%s", gameDir, bezel);
        bezel = path;
    }
    // By default (BEZEL_ENABLED -1), there if the image is.
    if (!enabled || !bezel[0] || (enabled < 0 && access(bezel, R_OK) != 0) || !bezelLoad(bezel))
        return;
    if (!(*(void **)&realSwapBuffers = hookExecutableImport("glXSwapBuffers", (void *)gameSwapBuffers)))
        log_warn("Byoyoyo: the runner swaps no GLX buffers, no bezel");
}

int byoyoyoInit(void)
{
    char cwd[PATH_MAX] = "";

    // Started in the game's directory.
    if (getcwd(cwd, sizeof(cwd)) && (size_t)snprintf(runnerPath, sizeof(runnerPath), "%s/runner", cwd) >= sizeof(runnerPath))
        runnerPath[0] = '\0';
    bezelInstall(cwd);
    hookExecutableImport("readlink", (void *)gameReadlink);
    if (!(*(void **)&realCreateWindow = hookExecutableImport("XCreateWindow", (void *)gameCreateWindow)))
        log_warn("Byoyoyo: the runner makes no window of its own");
    byoyoyoInputInstall();
    return 0;
}
