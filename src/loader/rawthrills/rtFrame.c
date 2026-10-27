// Video mode and frame hook for Raw Thrills games (kept apart from rawthrills.c, whose GL
// shader wrapper conflicts with glad's gl.h macros).

#include <dlfcn.h>
#include <elf.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#include <glad/gl.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../config/gameData.h"
#include "../graphics/bezel.h"
#include "../graphics/border.h"
#include "../graphics/borderFrame.h"
#include "../graphics/fpsLimiter.h"
#include "../graphics/frameScale.h"
#include "../log/log.h"

// Frame hooks: the games present frames with SDL 1.2's SDL_GL_SwapBuffers,
// or glut's glutSwapBuffers (g3 engine). There the frame is scaled to the
// window once the window no longer matches the game's size (see
// frameScale.h), and the light gun border ([Display] BORDER_ENABLED) drawn,
// as the loader does in its GLX swap hook for Lindbergh games. Under the
// border goes the bezel, if any ([Display] BEZEL, see bezel.h).

// The window being drawn: the games' own idea of its size (SDL 1.2's video
// surface, glut's) does not follow the window manager's resizes. An older
// SDL 2 (The Walking Dead) draws in a child of its window, which keeps its
// size when the window is resized: the loader resizes it along, and until
// that is done what shows of it is the part inside the top-level window (the
// one the window manager manages, with WM_STATE, or the root's child without
// one). area is that part, from the bottom left of the drawable.
static struct
{
    void *(*currentDisplay)(void);
    unsigned long (*currentDrawable)(void);
    int (*getGeometry)(void *, unsigned long, unsigned long *, int *, int *, unsigned int *, unsigned int *,
                       unsigned int *, unsigned int *);
    int (*queryTree)(void *, unsigned long, unsigned long *, unsigned long *, unsigned long **, unsigned int *);
    int (*translateCoordinates)(void *, unsigned long, unsigned long, int, int, int *, int *, unsigned long *);
    unsigned long (*internAtom)(void *, const char *, int);
    int (*getWindowProperty)(void *, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long *,
                             int *, unsigned long *, unsigned long *, unsigned char **);
    int (*freeData)(void *);
    int (*resizeWindow)(void *, unsigned long, unsigned int, unsigned int);
    int (*changeProperty)(void *, unsigned long, unsigned long, unsigned long, int, int, const unsigned char *, int);
} xl;

static unsigned long topLevelWindow(void *display, unsigned long window)
{
    unsigned long wmState = xl.internAtom(display, "WM_STATE", 0);
    for (int depth = 0; depth < 16; depth++)
    {
        unsigned long root, parent, *children, type, nitems, after;
        unsigned char *data = NULL;
        unsigned int nchildren;
        int format;

        if (xl.getWindowProperty(display, window, wmState, 0, 1, 0, 0 /* AnyPropertyType */, &type, &format,
                                 &nitems, &after, &data) == 0 && data)
        {
            xl.freeData(data);
            if (type)
                return window;
        }
        if (!xl.queryTree(display, window, &root, &parent, &children, &nchildren))
            break;
        if (children)
            xl.freeData(children);
        if (parent == root || !parent)
            break;
        window = parent;
    }
    return window;
}

// Esc/Alt+F4 quit only while the focused window is the game's, which it
// tells by its _NET_WM_PID (see rtIo.c): SDL 1.2 linked in (Big Buck HD
// Wild) sets none, so the game's window gets it here.
static void markWindow(void *display, unsigned long window)
{
    unsigned long pidAtom = xl.internAtom(display, "_NET_WM_PID", 0), type, nitems, after;
    unsigned char *data = NULL;
    int format;

    if (xl.getWindowProperty(display, window, pidAtom, 0, 1, 0, 6 /* XA_CARDINAL */, &type, &format, &nitems,
                             &after, &data) == 0 && data)
    {
        xl.freeData(data);
        if (nitems)
            return;
    }
    // Xlib takes 32-bit properties as longs.
    long pid = getpid();
    if (xl.changeProperty)
        xl.changeProperty(display, window, pidAtom, 6 /* XA_CARDINAL */, 32, 0 /* PropModeReplace */,
                          (const unsigned char *)&pid, 1);
}

static int drawableArea(int *width, int *height, int area[4])
{
    static unsigned long lastDrawable, topLevel;
    unsigned long root, drawable, child;
    int x, y, tx, ty;
    unsigned int w, h, tw, th, border, depth;
    void *display;

    if (!xl.getGeometry)
    {
        xl.currentDisplay = rtRealDlsym(RTLD_NEXT, "glXGetCurrentDisplay");
        xl.currentDrawable = rtRealDlsym(RTLD_NEXT, "glXGetCurrentDrawable");
        xl.queryTree = rtRealDlsym(RTLD_NEXT, "XQueryTree");
        xl.translateCoordinates = rtRealDlsym(RTLD_NEXT, "XTranslateCoordinates");
        xl.internAtom = rtRealDlsym(RTLD_NEXT, "XInternAtom");
        xl.getWindowProperty = rtRealDlsym(RTLD_NEXT, "XGetWindowProperty");
        xl.freeData = rtRealDlsym(RTLD_NEXT, "XFree");
        xl.resizeWindow = rtRealDlsym(RTLD_NEXT, "XResizeWindow");
        xl.getGeometry = rtRealDlsym(RTLD_NEXT, "XGetGeometry");
        xl.changeProperty = rtRealDlsym(RTLD_NEXT, "XChangeProperty");
    }
    if (!xl.currentDisplay || !xl.currentDrawable || !xl.getGeometry || !(display = xl.currentDisplay()) ||
        !(drawable = xl.currentDrawable()) ||
        !xl.getGeometry(display, drawable, &root, &x, &y, &w, &h, &border, &depth))
        return 0;
    *width = (int)w;
    *height = (int)h;
    area[0] = area[1] = 0;
    area[2] = (int)w;
    area[3] = (int)h;
    if (!xl.queryTree || !xl.translateCoordinates || !xl.internAtom || !xl.getWindowProperty || !xl.freeData)
        return 1;
    if (drawable != lastDrawable)
    {
        lastDrawable = drawable;
        topLevel = topLevelWindow(display, drawable);
        markWindow(display, topLevel);
    }
    if (topLevel == drawable ||
        !xl.getGeometry(display, topLevel, &root, &x, &y, &tw, &th, &border, &depth) ||
        !xl.translateCoordinates(display, drawable, topLevel, 0, 0, &tx, &ty, &child))
        return 1;
    // The child is made to follow its window, from the next frames on.
    if ((tw != w || th != h) && xl.resizeWindow)
        xl.resizeWindow(display, drawable, tw, th);
    // Meanwhile, the drawable's top left sits at tx, ty in the top-level window.
    int x0 = tx < 0 ? -tx : 0, y0 = ty < 0 ? -ty : 0;
    int x1 = (int)tw - tx < (int)w ? (int)tw - tx : (int)w, y1 = (int)th - ty < (int)h ? (int)th - ty : (int)h;
    if (x1 <= x0 || y1 <= y0)
        return 1;
    area[0] = x0;
    area[1] = (int)h - y1;
    area[2] = x1 - x0;
    area[3] = y1 - y0;
    return 1;
}

// Before the swap; width and height are the game's idea of the window size,
// used if the drawable's cannot be had. The frame is kept clean around the
// swap when only the border is drawn (see borderFrame.h).
static int bordered;

static void frameBegin(int width, int height)
{
    static int glLoaded;
    EmulatorConfig *config = getConfig();

    bordered = 0;
    if (!isRawThrillsGame())
        return;
    // The GL entry points are loaded once a context is current.
    if (!glLoaded)
    {
        GLADloadfunc getProcAddress = (GLADloadfunc)dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
        glLoaded = getProcAddress && gladLoadGL(getProcAddress) ? 1 : -1;
        if (glLoaded < 0)
            log_warn("Raw Thrills: cannot load GL entry points, no border or window scaling");
    }
    if (glLoaded <= 0)
        return;
    int area[4] = {0, 0, width, height};
    drawableArea(&width, &height, area);
    if (rtCurrentGame()->frameDraw && area[2] > 0 && area[3] > 0)
        rtCurrentGame()->frameDraw(area[0], area[1], area[2], area[3]);
    if (width <= 0 || height <= 0 ||
        frameScalePresent(area[0], area[1], area[2], area[3], config->borderEnabled,
                          config->whiteBorderPercentage, config->blackBorderPercentage) ||
        (!config->borderEnabled && !frameOverlaySet()))
        return;
    borderFrameBegin(width, height, config->borderEnabled, config->whiteBorderPercentage, config->blackBorderPercentage);
    bordered = 1;
}

static void frameEnd(void)
{
    if (bordered)
        borderFrameEnd();
    bordered = 0;
}

void SDL_GL_SwapBuffers(void)
{
    static void (*real)(void);
    static int *(*getVideoSurface)(void);

    if (!real)
    {
        real = dlsym(RTLD_NEXT, "SDL_GL_SwapBuffers");
        getVideoSurface = dlsym(RTLD_DEFAULT, "SDL_GetVideoSurface");
    }
    // SDL 1.2 SDL_Surface: flags, format, w, h.
    int *surface = getVideoSurface ? getVideoSurface() : NULL;
    frameBegin(surface ? surface[2] : 0, surface ? surface[3] : 0);
    if (real)
        real();
    frameEnd();
    if (getConfig()->fpsLimiter)
        frameTiming();
}

// Games with SDL linked in get glXSwapBuffers from libGL with dlsym: they are
// handed this one instead (see rtDump.c).
void rtGlxSwapBuffers(void *display, unsigned long drawable)
{
    static void (*real)(void *, unsigned long);

    if (!real)
        real = rtRealDlsym(RTLD_NEXT, "glXSwapBuffers");
    frameBegin(0, 0);
    if (real)
        real(display, drawable);
    frameEnd();
    if (getConfig()->fpsLimiter)
        frameTiming();
}

// SDL 2's swap, for the games whose SDL 2 is the system's (see rtDump.c):
// its own GLX lookups never reach the loader's. real is the system's.
void rtSdlGlSwapWindow(void (*real)(void *), void *window)
{
    frameBegin(0, 0);
    real(window);
    frameEnd();
    if (getConfig()->fpsLimiter)
        frameTiming();
}

#define GLUT_WINDOW_WIDTH 102
#define GLUT_WINDOW_HEIGHT 103

// Bound in place of the game's glutSwapBuffers import (see rtDump.c): the
// loader's own glutSwapBuffers is its Lindbergh glut bridge.
void rtGlutSwapBuffers(void)
{
    static void (*real)(void);
    static int (*glutGet)(int);

    if (!real)
    {
        real = dlsym(RTLD_NEXT, "glutSwapBuffers");
        glutGet = dlsym(RTLD_NEXT, "glutGet");
    }
    frameBegin(glutGet ? glutGet(GLUT_WINDOW_WIDTH) : 0, glutGet ? glutGet(GLUT_WINDOW_HEIGHT) : 0);
    if (real)
        real();
    frameEnd();
    if (getConfig()->fpsLimiter)
        frameTiming();
}

// Video mode: the games render at the cabinet resolution (g5 engine: gCLArgs,
// "-f<w>x<h>" on the command line), set up by their command-line parser. Use
// [Display] WIDTH/HEIGHT instead, which default to that same resolution, and
// [Display] ROTATE_VERTICAL for vertical games.

static void (*parseArgsOrig)(int, char **);
static const RtGame *videoGame;
static void (*orthoOrig)(int w, int h, int rotate);
static void (*realViewport)(int x, int y, int w, int h);

// The game draws in its monitor's pixels, upright or rotated: keep that
// projection and fit it, centred, in the window (in the bezel's hole, with
// one). Pac-Man passes the rotation
// as a third argument, Galaga Assault has only two.
static void setOrtho(int w, int h, int rotate)
{
    const int32_t *monitor = (const int32_t *)(uintptr_t)videoGame->monitorSize;
    int rotated = videoGame->rotateFlag && *(const int32_t *)(uintptr_t)videoGame->rotateFlag;
    int gw = rotated ? monitor[0] : monitor[1], gh = rotated ? monitor[1] : monitor[0];
    int hx, hy, hw, hh;

    orthoOrig(gw, gh, rotate);
    bezelHole(w, h, &hx, &hy, &hw, &hh);
    int vw = hw, vh = hh;
    if ((double)hw * gh > (double)hh * gw)
        vw = (int)((double)hh * gw / gh);
    else
        vh = (int)((double)hw * gh / gw);
    realViewport(hx + (hw - vw) / 2, hy + (hh - vh) / 2, vw, vh);
}

// The game's size when it is not the window's (bezelFrame): the bezel's hole.
static int frameWidth, frameHeight;

static void parseCommandLineArgs(int argc, char **argv)
{
    int width = frameWidth ? frameWidth : getConfig()->width, height = frameWidth ? frameHeight : getConfig()->height;
    int32_t *mode = (int32_t *)videoGame->resolution;

    parseArgsOrig(argc, argv);
    if (width <= 0 || height <= 0)
        return;
    mode[0] = width;
    mode[1] = height;
    if (videoGame->aspect)
        *(float *)(uintptr_t)videoGame->aspect = (float)width / height;
    if (videoGame->rotateFlag)
        *(int32_t *)(uintptr_t)videoGame->rotateFlag = getConfig()->rotateVertical != 0;
    if (videoGame->fullscreenFlag)
        *(int32_t *)(uintptr_t)videoGame->fullscreenFlag = getConfig()->fullscreen != 0;
    printf("Raw Thrills: video mode %dx%d%s\n", width, height,
           videoGame->rotateFlag && getConfig()->rotateVertical ? ", rotated" : "");
}

// g3 engine: mode table entry and glut window.

typedef struct
{
    uint32_t flags;
    uint16_t width;
    uint16_t height;
} RtModeEntry;

static int (*setModeOrig)(int mode);
static int (*windowOpenOrig)(int width, int height, int fullscreen);

// The shape the game is drawn at (frameAspect), unless [Display]
// KEEP_ASPECT_RATIO 0 stretches it: NULL then, and for the other games.
static const uint8_t *g3Aspect;

// The mode is the screen's size, or the game's shape at the screen's height.
static int setMode(int mode)
{
    int ret = setModeOrig(mode);
    RtModeEntry *entry = *(RtModeEntry **)(uintptr_t)videoGame->modePointer;
    int width = getConfig()->width, height = getConfig()->height;

    if (g3Aspect && height > 0 && width * g3Aspect[1] > height * g3Aspect[0])
        width = (height * g3Aspect[0] / g3Aspect[1]) & ~1;
    if (ret >= 0 && entry && width > 0 && height > 0)
    {
        entry->width = width;
        entry->height = height;
        printf("Raw Thrills: video mode %dx%d\n", width, height);
    }
    return ret;
}

static int windowOpen(int width, int height, int fullscreen)
{
    static void (*glutFullScreen)(void);
    int ret = windowOpenOrig(width, height, 0);

    (void)fullscreen;
    if (!glutFullScreen)
        glutFullScreen = dlsym(RTLD_NEXT, "glutFullScreen");
    if (ret >= 0 && getConfig()->fullscreen && !videoGame->glutWindowOnly && glutFullScreen)
        glutFullScreen();
    return ret;
}

static void installG3Video(const RtGame *game)
{
    videoGame = game;
    // It follows its window: its viewports keep its shape (frameScale.h).
    if (game->frameAspect[0] && getConfig()->keepAspectRatio)
    {
        g3Aspect = game->frameAspect;
        frameScaleSetAspect(g3Aspect[0], g3Aspect[1]);
    }
    if (game->setModeSymbol)
    {
        setModeOrig = rtTrampoline(game->setModeSymbol, game->setModePrologue);
        if (!setModeOrig || rtDetour(game->setModeSymbol, setMode) != 0)
            log_warn("Raw Thrills: cannot hook %s, keeping the game's resolution", game->setModeSymbol);
    }
    if (!game->windowOpenSymbol)
        return;
    windowOpenOrig = rtTrampoline(game->windowOpenSymbol, game->windowOpenPrologue);
    if (!windowOpenOrig || rtDetour(game->windowOpenSymbol, windowOpen) != 0)
        log_warn("Raw Thrills: cannot hook %s, the display mode may not be set", game->windowOpenSymbol);
}

#define SDL_FULLSCREEN 0x80000000u

static void *(*setVideoModeOrig)(int width, int height, int bpp, uint32_t flags);

// The game's size (the cabinet's monitor) is replaced by [Display]
// WIDTH/HEIGHT, the screen's.
static void *setVideoMode(int width, int height, int bpp, uint32_t flags)
{
    int w = getConfig()->width, h = getConfig()->height;

    printf("Raw Thrills: video mode %dx%d asked, %dx%d%s\n", width, height, w > 0 ? w : width, h > 0 ? h : height,
           getConfig()->fullscreen ? " fullscreen" : "");
    if (w > 0 && h > 0)
    {
        width = w;
        height = h;
    }
    // As [Display] FULLSCREEN says, whatever the game asks.
    if (getConfig()->fullscreen)
        flags |= SDL_FULLSCREEN;
    else
        flags &= ~SDL_FULLSCREEN;
    return setVideoModeOrig(width, height, bpp, flags);
}

// The window of the glutGameModeWindow games. They ask GLUT's game mode for
// their cabinet's 1366x768, which a PC display may not offer (freeglut:
// "failed to change screen settings", then no window and the game quits):
// they get a window instead, at [Display] WIDTH/HEIGHT, made fullscreen as
// [Display] FULLSCREEN says.
static int enterGameMode(void)
{
    void (*initWindowSize)(int, int) = dlsym(RTLD_NEXT, "glutInitWindowSize");
    int (*createWindow)(const char *) = dlsym(RTLD_NEXT, "glutCreateWindow");
    void (*fullScreen)(void) = dlsym(RTLD_NEXT, "glutFullScreen");
    const GameData *data = getGameData(getConfig()->crc32);
    int w = getConfig()->width, h = getConfig()->height;

    if (!createWindow)
        return 0;
    if (initWindowSize && w > 0 && h > 0)
        initWindowSize(w, h);
    int window = createWindow(data ? data->gameTitle : "Raw Thrills");
    if (getConfig()->fullscreen && fullScreen)
        fullScreen();
    return window;
}

static void gameModeString(const char *mode)
{
    (void)mode;
}

// A turned frame (frameTurn) is the game's own size, whatever its window's:
// its GLUT reshape callback, which sets a viewport of the window's size, is
// given that size.
static void (*gameReshape)(int width, int height);

static void turnedReshape(int width, int height)
{
    const RtGame *g = rtCurrentGame();
    (void)width;
    (void)height;
    if (gameReshape)
        gameReshape(g->frameTurnWidth, g->frameTurnHeight);
}

static void reshapeFunc(void (*callback)(int, int))
{
    static void (*real)(void (*)(int, int));
    if (!real)
        real = dlsym(RTLD_NEXT, "glutReshapeFunc");
    gameReshape = callback;
    if (real)
        real(callback ? turnedReshape : NULL);
}

// A fixedFrame game's window (SDL_SetVideoMode, from its imports) is opened
// at [Display]'s size, whatever size the game draws at (see rtInstallVideo).
void *rtFrameOverride(const char *name)
{
    const RtGame *g = rtCurrentGame();
    if (g && g->glutGameModeWindow && !strcmp(name, "glutEnterGameMode"))
        return (void *)enterGameMode;
    if (g && g->glutGameModeWindow && !strcmp(name, "glutGameModeString"))
        return (void *)gameModeString;
    if (g && g->frameTurn && !getConfig()->rotateVertical && !strcmp(name, "glutReshapeFunc"))
        return (void *)reshapeFunc;
    if (!g || !g->fixedFrame || strcmp(name, "SDL_SetVideoMode") != 0)
        return NULL;
    if (!setVideoModeOrig)
        *(void **)&setVideoModeOrig = dlsym(RTLD_NEXT, name);
    return setVideoModeOrig ? (void *)setVideoMode : NULL;
}

// g5 engine 2D layer (rtGame.h screenSize): the screen size its code sees,
// and the window it projects in, the real one's but for its size.
static uint16_t layoutSize[2];
static struct
{
    void *screen;
    uint8_t ready, quit, fullScreen;
    int32_t w, h;
} layoutWnd;
static void *(*wndCurReal)(void);
static void (*dflt2DCamOrig)(float w, float h);

static void *layoutWndCur(void)
{
    memcpy(&layoutWnd, wndCurReal(), sizeof(layoutWnd));
    layoutWnd.w = layoutSize[0];
    layoutWnd.h = layoutSize[1];
    return &layoutWnd;
}

// g5_Start sets it to the screen's size.
static void dflt2DCam(float w, float h)
{
    (void)w;
    (void)h;
    dflt2DCamOrig(layoutSize[0], layoutSize[1]);
}

// The executable's code segment, the one holding address.
static int codeSegment(uint32_t address, uint8_t **start, uint8_t **end)
{
    const Elf32_Phdr *phdr = (const Elf32_Phdr *)getauxval(AT_PHDR);
    unsigned long count = getauxval(AT_PHNUM);

    for (unsigned long i = 0; phdr && i < count; i++)
        if (phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_X) && address >= phdr[i].p_vaddr &&
            address < phdr[i].p_vaddr + phdr[i].p_filesz)
        {
            *start = (uint8_t *)(uintptr_t)phdr[i].p_vaddr;
            *end = *start + phdr[i].p_filesz;
            return 1;
        }
    return 0;
}

static void installLayout(const RtGame *game)
{
    // A fixedFrame game's own size (see rtInstallVideo), else the window's.
    int width = frameWidth ? frameWidth : getConfig()->width, height = frameWidth ? frameHeight : getConfig()->height;
    uint32_t keep[32][2];
    int nkeep = 0, sizes = 0, windows = 0;
    uint8_t *start, *end;

    wndCurReal = rtSymbol("WndCur");
    if (width <= 0 || height <= 0 || !wndCurReal || !codeSegment((uint32_t)(uintptr_t)wndCurReal, &start, &end))
        return;
    // The cabinet's height at the screen's shape: its width on a screen of
    // (nearly) the cabinet's shape, where its layouts are placed as drawn.
    int layoutW = (int)((double)game->layoutHeight * width / height + 0.5);
    if (abs(layoutW - game->layoutWidth) * 100 <= game->layoutWidth)
        layoutW = game->layoutWidth;
    layoutSize[0] = layoutW;
    layoutSize[1] = game->layoutHeight;
    for (const char *const *name = game->layoutRealSize; name && *name && nkeep < 32; name++)
    {
        void *address = rtSymbol(*name);
        uint32_t size = rtSymbolSize(*name);
        if (!address || !size)
        {
            log_warn("Raw Thrills: 2D layer: %s not found, the HUD keeps the cabinet's pixel size", *name);
            return;
        }
        keep[nkeep][0] = (uint32_t)(uintptr_t)address;
        keep[nkeep++][1] = (uint32_t)(uintptr_t)address + size;
    }
    dflt2DCamOrig = rtTrampoline("VidSetDflt2DCamDim", game->dflt2DCamPrologue);
    if (!dflt2DCamOrig || rtDetour("VidSetDflt2DCamDim", dflt2DCam) != 0)
    {
        log_warn("Raw Thrills: 2D layer: cannot hook VidSetDflt2DCamDim, the HUD keeps the cabinet's pixel size");
        return;
    }
    uintptr_t page = (uintptr_t)start & ~(uintptr_t)0xfff;
    mprotect((void *)page, (uintptr_t)end - page, PROT_READ | PROT_WRITE | PROT_EXEC);
    for (uint8_t *p = start; p + 7 <= end; p++)
    {
        uint32_t at = (uint32_t)(uintptr_t)p;
        int kept = 0;
        for (int i = 0; i < nkeep && !kept; i++)
            kept = at >= keep[i][0] && at < keep[i][1];
        if (kept)
            continue;
        // movzwl screenW or screenH, %reg (absolute address).
        uint32_t operand;
        memcpy(&operand, p + 3, 4);
        if (p[0] == 0x0f && p[1] == 0xb7 && (p[2] & 0xc7) == 0x05 &&
            (operand == game->screenSize || operand == game->screenSize + 2))
        {
            uint32_t shadow = (uint32_t)(uintptr_t)&layoutSize[operand != game->screenSize];
            memcpy(p + 3, &shadow, 4);
            sizes++;
            p += 6;
            continue;
        }
        // call WndCur
        int32_t rel;
        memcpy(&rel, p + 1, 4);
        if (p[0] == 0xe8 && p + 5 + rel == (uint8_t *)wndCurReal)
        {
            rel = (int32_t)((uint8_t *)layoutWndCur - (p + 5));
            memcpy(p + 1, &rel, 4);
            windows++;
            p += 4;
        }
    }
    printf("Raw Thrills: 2D layer laid out at %dx%d (%d screen size reads, %d window lookups)\n", layoutSize[0],
           layoutSize[1], sizes, windows);
}

void rtInstallVideo(const RtGame *game)
{
    // fixedFrame games draw at their own size: [Display]'s, but at the
    // cabinet's shape on a screen narrower than it (a 4:3 one), their 2D
    // being laid out for that shape (Jurassic Park: some of it fell off the
    // right and the bottom at 4:3). Their window is [Display]'s size
    // (setVideoMode) and the frame is fitted in it: with bars above and below
    // for a narrower screen when the aspect is kept, else stretched to it.
    int w = getConfig()->width, h = getConfig()->height;
    if (game->fixedFrame && w > 0 && h > 0)
    {
        frameWidth = w;
        frameHeight = h;
        if (game->layoutWidth && game->layoutHeight &&
            (long)w * game->layoutHeight < (long)h * game->layoutWidth)
            frameHeight = (int)((double)w * game->layoutHeight / game->layoutWidth + 0.5) & ~1;
        frameScaleSetFrame(frameWidth, frameHeight, NULL);
        printf("Raw Thrills: the game drawn at %dx%d in a %dx%d window\n", frameWidth, frameHeight, w, h);
    }
    if (game->screenSize && game->layoutHeight)
        installLayout(game);
    // The bezel, for the vertical games shown upright in the middle of the
    // screen: rotated for a monitor on its side, they fill it. A relative
    // path is from the game's directory: the game runs from one of its own.
    // By default (BEZEL_ENABLED -1), there if the image is.
    const char *bezel = getConfig()->bezel;
    int enabled = getConfig()->bezelEnabled;
    char path[PATH_MAX];
    if (bezel[0] && bezel[0] != '/')
    {
        snprintf(path, sizeof(path), "%s/%s", rtGameDir(), bezel);
        bezel = path;
    }
    // bezelFrame games: on a landscape window (a portrait one is the game's).
    int turned = game->frameTurn && !getConfig()->rotateVertical;
    if (((game->rotateFlag && game->monitorSize && game->orthoSymbol && !getConfig()->rotateVertical) || turned ||
         (game->bezelFrame && getConfig()->width > getConfig()->height)) &&
        enabled && bezel[0] && (enabled > 0 || access(bezel, R_OK) == 0) && bezelLoad(bezel))
        setFrameOverlay(bezelDraw);
    // A turned frame is fitted upright in the window, in the bezel's hole.
    if (turned)
    {
        frameScaleSetFrame(game->frameTurnWidth, game->frameTurnHeight, bezelLoaded() ? bezelHole : NULL);
        frameScaleSetRotation(game->frameTurn);
        printf("Raw Thrills: the %dx%d frame turned upright in the window\n", game->frameTurnWidth,
               game->frameTurnHeight);
    }
    // The game draws its own picture, at the size of the hole: the window,
    // the screen's size, gets it scaled there under the bezel.
    if (game->bezelFrame && bezelLoaded() && getConfig()->width > 0 && getConfig()->height > 0)
    {
        int x, y;
        bezelHole(getConfig()->width, getConfig()->height, &x, &y, &frameWidth, &frameHeight);
        frameScaleSetFrame(frameWidth, frameHeight, bezelHole);
        printf("Raw Thrills: bezel, the game drawn at %dx%d in a %dx%d window\n", frameWidth, frameHeight,
               getConfig()->width, getConfig()->height);
    }
    if (game->videoModeSymbol)
    {
        setVideoModeOrig = rtTrampoline(game->videoModeSymbol, game->videoModePrologue);
        if (!setVideoModeOrig || rtDetour(game->videoModeSymbol, setVideoMode) != 0)
            log_warn("Raw Thrills: cannot hook %s, the game stays windowed", game->videoModeSymbol);
    }
    const char *symbol = game->parseArgsSymbol ? game->parseArgsSymbol : "ParseCommandLineArgs";

    if (game->setModeSymbol || game->windowOpenSymbol)
        installG3Video(game);
    if (!game->resolution || !game->parseArgsPrologue)
        return;
    videoGame = game;
    if (game->monitorSize && game->orthoSymbol)
    {
        // Through the window scaling's wrapper, which then sees the game
        // fit its picture to a resized window itself.
        *(void **)&realViewport = frameScaleWrapper("glViewport");
        if (!realViewport)
            realViewport = dlsym(RTLD_NEXT, "glViewport");
        orthoOrig = rtTrampoline(game->orthoSymbol, game->orthoPrologue);
        if (realViewport && orthoOrig)
            rtDetour(game->orthoSymbol, setOrtho);
    }
    parseArgsOrig = rtTrampoline(symbol, game->parseArgsPrologue);
    if (!parseArgsOrig || rtDetour(symbol, parseCommandLineArgs) != 0)
        log_warn("Raw Thrills: cannot hook ParseCommandLineArgs, keeping the game's resolution");
}
