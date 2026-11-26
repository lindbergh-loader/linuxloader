// The IT games' window (see itech.h). Orange County Choppers Pinball draws
// 640x480 for a monitor on its side, its picture turned a quarter turn
// counterclockwise: the frame is drawn offscreen and, at each swap, turned
// back upright into the window (graphics/frameScale.h). The window is portrait,
// 600x800, or [Display] WIDTH x HEIGHT when set, the screen's size with
// FULLSCREEN. [Display] ROTATE_VERTICAL 1 (a monitor on its side, as the
// cabinet's) leaves the picture as the game draws it.
//
// ITECH_SHOT=<dir> saves the window's picture there every ITECH_SHOT_PERIOD
// seconds (4), as PPM files (a capture of the window comes out black under
// gamescope).

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <glad/gl.h>

#include "itech.h"
#include "../common/desktopInput.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 800

typedef unsigned long XID;

static void *gameDisplay;
static XID gameWindow;
static int scaling;

static void *glProcAddress(const char *name)
{
    static void *(*get)(const char *);

    if (!get)
        *(void **)&get = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    return get ? get(name) : NULL;
}

// The window the game makes for its screen: at the window's size, full
// screen if asked.
__attribute__((force_align_arg_pointer)) static XID createWindow(void *display, XID parent, int x, int y,
                                                                 unsigned int width, unsigned int height,
                                                                 unsigned int border, int depth, unsigned int class,
                                                                 void *visual, unsigned long mask, void *attributes)
{
    static XID (*real)(void *, XID, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, void *,
                       unsigned long, void *);
    int (*displayWidth)(void *, int) = dlsym(RTLD_DEFAULT, "XDisplayWidth");
    int (*displayHeight)(void *, int) = dlsym(RTLD_DEFAULT, "XDisplayHeight");
    int (*defaultScreen)(void *) = dlsym(RTLD_DEFAULT, "XDefaultScreen");
    EmulatorConfig *config = getConfig();
    unsigned int w = config->rotateVertical ? FRAME_WIDTH : WINDOW_WIDTH;
    unsigned int h = config->rotateVertical ? FRAME_HEIGHT : WINDOW_HEIGHT;
    XID window;

    if (!real)
        *(void **)&real = importTarget("XCreateWindow");
    if (width != FRAME_WIDTH || height != FRAME_HEIGHT)
        return real(display, parent, x, y, width, height, border, depth, class, visual, mask, attributes);
    if (config->fullscreen && displayWidth && displayHeight && defaultScreen)
    {
        w = displayWidth(display, defaultScreen(display));
        h = displayHeight(display, defaultScreen(display));
    }
    else if (config->width != FRAME_WIDTH || config->height != FRAME_HEIGHT)
    {
        w = config->width;
        h = config->height;
    }
    window = real(display, parent, x, y, w, h, border, depth, class, visual, mask, attributes);
    if (!window)
        return window;
    gameDisplay = display;
    gameWindow = window;
    desktopNoteGameWindow(window);
    if (config->fullscreen)
    {
        unsigned long (*internAtom)(void *, const char *, int) = dlsym(RTLD_DEFAULT, "XInternAtom");
        int (*changeProperty)(void *, XID, unsigned long, unsigned long, int, int, const unsigned char *, int) =
            dlsym(RTLD_DEFAULT, "XChangeProperty");
        if (internAtom && changeProperty)
        {
            unsigned long state = internAtom(display, "_NET_WM_STATE_FULLSCREEN", 0);
            changeProperty(display, window, internAtom(display, "_NET_WM_STATE", 0), 4 /* XA_ATOM */, 32,
                           0 /* PropModeReplace */, (const unsigned char *)&state, 1);
        }
    }
    printf("IT: %ux%u window%s\n", w, h, config->fullscreen ? ", full screen" : "");
    return window;
}

static void saveShot(int width, int height)
{
    static double next = -1;
    static int count;
    const char *dir = getenv("ITECH_SHOT");
    struct timespec ts;
    double now;

    if (!dir || !glad_glReadPixels)
        return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (now < next)
        return;
    next = now + (getenv("ITECH_SHOT_PERIOD") ? atof(getenv("ITECH_SHOT_PERIOD")) : 4);

    char path[PATH_MAX];
    unsigned char *pixels = malloc((size_t)width * height * 3);
    FILE *f;

    snprintf(path, sizeof(path), "%s/itech%04d.ppm", dir, count++);
    if (!pixels || !(f = fopen(path, "wb")))
    {
        free(pixels);
        return;
    }
    // The window's picture, not the game's offscreen frame.
    GLint read = 0;
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glad_glReadBuffer(GL_BACK);
    glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glad_glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; y--)
        fwrite(pixels + (size_t)y * width * 3, 1, (size_t)width * 3, f);
    fclose(f);
    free(pixels);
}

void itechBeforeSwap(void)
{
    int (*getGeometry)(void *, XID, XID *, int *, int *, unsigned int *, unsigned int *, unsigned int *,
                       unsigned int *) = dlsym(RTLD_DEFAULT, "XGetGeometry");
    unsigned int width = FRAME_WIDTH, height = FRAME_HEIGHT, border, depth;
    XID root;
    int x, y;
    EmulatorConfig *config = getConfig();

    if (!isItechGame())
        return;
    if (gameWindow && getGeometry)
        getGeometry(gameDisplay, gameWindow, &root, &x, &y, &width, &height, &border, &depth);
    if (scaling)
        frameScalePresent(0, 0, (int)width, (int)height, config->borderEnabled, config->whiteBorderPercentage,
                          config->blackBorderPercentage);
    saveShot((int)width, (int)height);
}

void itechFrameInstall(void)
{
    static const char *const wrapped[] = {"glViewport", "glDrawBuffer", "glReadBuffer"};

    hookExecutableImport("XCreateWindow", (void *)createWindow);
    frameScaleInit(glProcAddress, getConfig()->keepAspectRatio);
    for (size_t i = 0; i < sizeof(wrapped) / sizeof(wrapped[0]); i++)
    {
        void *wrapper = frameScaleWrapper(wrapped[i]);
        if (wrapper)
            hookExecutableImport(wrapped[i], wrapper);
    }
    frameScaleSetFrame(FRAME_WIDTH, FRAME_HEIGHT, NULL);
    if (!getConfig()->rotateVertical)
        frameScaleSetRotation(-1);
    scaling = 1;
}
