#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#define GL_GLEXT_PROTOTYPES
#include <glad/gl.h>

#ifdef __linux__
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <stdbool.h>
#include <unistd.h>

#include "blitStretching.h"
#include "../config/config.h"
#include "crossHair.h"
#include "overlayMsgs.h"
#include "fpsLimiter.h"
#include "customCursor.h"
#include "../input/sdlInput.h"
#include "../hardware/lindbergh/jvs.h"
#include "../resources/LiberationMono-Regular.h"
#include "../resources/icon.h"
#include "../log/log.h"
#include "sdlCalls.h"
#include "../input/wiimoteEvdev.h"

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;

#ifdef __linux__
Display *x11Display = NULL;
Window x11Window;
#endif

extern SDL_Cursor *customCursor;
extern SDL_Cursor *touchCursor;

bool creatingWindow = false;
SDL_Window *g_SdlWindow = NULL;
SDL_GLContext g_SdlContext = NULL;
bool sdlInputInitialized = false;

int glutInitialized = 0;

bool isFullScreen = false;
bool sdlFontInit = false;
SDL_Renderer *fontRenderer;
TTF_Font *font;

void GLAPIENTRY openglDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message,
                                    const void *userParam)
{
    if (id == 1099)
        return;
    printf("OpenGL Debug Message:\n");
    printf("Source: 0x%x\n", source);
    printf("Type: 0x%x\n", type);
    printf("ID: %u\n", id);
    printf("Severity: 0x%x\n", severity);
    printf("Message: %s\n", message);

    if (severity == GL_DEBUG_SEVERITY_HIGH)
    {
        printf("This is a high severity error!\n");
    }
}

int initSDL()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD))
    {
        log_error("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    if (gGrp == GROUP_OUTRUN_TEST || gGrp == GROUP_ID_SERVERBOX)
    {
        if (!TTF_Init())
        {
            log_error("SDL_ttf could not initialize! SDL_ttf Error: %s\n", SDL_GetError());
            return 1;
        }
        SDL_IOStream *rw = SDL_IOFromConstMem(LiberationMonoRegular_ttf, LiberationMonoRegular_ttf_length);
        float fontSize = 16.0;
        if (gWidth > 640)
            fontSize = fontSize * (((float)gWidth / 640.0f) + ((float)gHeight / 480.0f)) / 2.0f;

        font = TTF_OpenFontIO(rw, 1, fontSize);
        fontRenderer = SDL_CreateRenderer(g_SdlWindow, "");
    }
    return 0;
}

SDL_DisplayID getTargetDisplayID()
{
    int numDisplays = 0;
    SDL_DisplayID *sdlDisplayId = SDL_GetDisplays(&numDisplays);
    if (!sdlDisplayId || numDisplays < 1)
    {
        SDL_free(sdlDisplayId);
        return SDL_GetPrimaryDisplay();
    }

    int index = getConfig()->displayIndex;
    if (index < 0 || index >= numDisplays)
    {
        log_warn("DISPLAY_INDEX %d is out of range (%d display(s) detected), will use the first one.\n", index, numDisplays);
        index = 0;
    }

    SDL_DisplayID displayId = sdlDisplayId[index];
    SDL_free(sdlDisplayId);
    return displayId;
}

void startSDL()
{
    creatingWindow = true;
    SDL_DisplayID targetDisplay = getTargetDisplayID();

    const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode(targetDisplay);
    if (displayMode == NULL)
    {
        log_error("SDL_GetCurrentDisplayMode Error: %s\n", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    if (getConfig()->showDebugMessages)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); // Double buffering
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);  // 24-bit depth buffer
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);   // Set the alpha size to 8 bits
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    if (gGrp == GROUP_ABC || gGrp == GROUP_VF5 || gId == R_TUNED_SBQW || gId == GHOST_SQUAD_EVOLUTION_SBNJ || gId == SEGA_RACE_TV_SBPF ||
        gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA)
    {
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
    }

    int windowWidth = gWidth;
    if (gGrp == GROUP_OUTRUN && getConfig()->width == 640)
    {
        gameIsOutrunChihiroMode = 1;
        gWidth = 800;
    }

    uint32_t windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;

    g_SdlWindow = SDL_CreateWindow(getGameName(), windowWidth, gHeight, windowFlags);

    if (g_SdlWindow)
    {
        SDL_IOStream *io = SDL_IOFromConstMem(icon256x256, sizeof(icon256x256));
        if (io)
        {
            SDL_Surface *iconSurface = IMG_Load_IO(io, true);
            if (iconSurface)
            {
                SDL_SetWindowIcon(g_SdlWindow, iconSurface);
                SDL_DestroySurface(iconSurface);
            }
        }
    }

    // Hacky way to make AxA games render the characters properly
    if (gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE)
        SDL_SetWindowSize(g_SdlWindow, 1024, 768);

    if (!g_SdlWindow)
    {
        SDL_Quit();
        log_error("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ || gId == MJ4_EVO_SBTA || gId == MJ4_SBPN_REVG)
        SDL_SetWindowSize(g_SdlWindow, gWidth, gHeight + 1);

    // Place the window on the selected display, fullscreen is then applied on that display
    SDL_SetWindowPosition(g_SdlWindow, SDL_WINDOWPOS_CENTERED_DISPLAY(targetDisplay), SDL_WINDOWPOS_CENTERED_DISPLAY(targetDisplay));

#ifdef __linux__
    x11Display = (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(g_SdlWindow), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    x11Window = (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(g_SdlWindow), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!x11Display || !x11Window)
    {
        log_error("This program is not running on X11 or failed to get window/display.\n");
    }
#endif

    g_SdlContext = SDL_GL_CreateContext(g_SdlWindow);
    if (!g_SdlContext)
    {
        SDL_DestroyWindow(g_SdlWindow);
        SDL_Quit();
        log_error("OpenGL context could not be created! SDL_Error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // With the FPS limiter on it is the only frame clock; driver vsync on top of it would drift
    // against it and drop frames.
    if (getConfig()->fpsLimiter)
        SDL_GL_SetSwapInterval(0);

    if (getConfig()->showDebugMessages)
    {
        // Enable OpenGL debug output
        glad_glEnable(GL_DEBUG_OUTPUT);
        glad_glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glad_glDebugMessageCallback(openglDebugCallback, NULL);
    }

    SDL_SetWindowMinimumSize(g_SdlWindow, windowWidth, gHeight);

    // Hacky way to make AxA games render the characters properly
    if (gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE)
        SDL_SetWindowPosition(g_SdlWindow, SDL_WINDOWPOS_CENTERED_DISPLAY(targetDisplay), SDL_WINDOWPOS_CENTERED_DISPLAY(targetDisplay));

    if (getConfig()->fullscreen)
    {
        SDL_SetWindowFullscreenMode(g_SdlWindow, NULL);
        SDL_SetWindowFullscreen(g_SdlWindow, true);
        isFullScreen = true;
    }

    initBlitting();

    SDL_SetWindowResizable(g_SdlWindow, true);

    SDL_ShowWindow(g_SdlWindow);

    Uint64 startTime = SDL_GetTicks();
    int running = 1;

    while (running)
    {
        if (SDL_GetTicks() - startTime >= 1000)
            running = 0;
        glad_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glad_glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(g_SdlWindow);
        SDL_PumpEvents();
    }

    creatingWindow = false;

    printf("  RESOLUTION: %dx%d\n", gWidth, gHeight);

    loadCursors();
    if (customCursor)
        setCursor(customCursor);

    if (getConfig()->enableCrosshairs &&
        (gGrp == GROUP_HOD4 || gGrp == GROUP_HOD4_TEST || gGrp == GROUP_HOD4_SP || gGrp == GROUP_HOD4_SP_TEST || gGrp == GROUP_RAMBO ||
         gId == GHOST_SQUAD_EVOLUTION_SBNJ || gId == PRIMEVAL_HUNT_SBPP))
        initCrossHairs();

    if ((gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA || gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE) &&
        strcmp(getConfig()->touchCursor, "") != 0 && getConfig()->emulateTouchscreen)
        setCursor(touchCursor);
    else if (getConfig()->hideCursor)
        SDL_HideCursor();
}

/*
 * Fix for Let's Go Jungle window resize
 */
// The window's own width. In Outrun's Chihiro mode gWidth is the 800 wide render, but the window
// shows the cropped 640.
static int windowedWidth()
{
    return gameIsOutrunChihiroMode ? 640 : gWidth;
}

void refreshResizedDrawable()
{
    if (gGrp != GROUP_LGJ)
        return;

    static int lastWidth = 0;
    static int lastHeight = 0;
    static int pendingClears = 0;

    if (g_SdlWindow == NULL)
        return;

    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(g_SdlWindow, &width, &height);
    if (width <= 0 || height <= 0)
        return;

    if (width != lastWidth || height != lastHeight)
    {
        lastWidth = width;
        lastHeight = height;
        /* The buffers are reallocated lazily, so clear across the swap too. */
        pendingClears = 2;
    }

    if (pendingClears == 0)
        return;

    pendingClears--;

    GLint drawFbo = 0;
    GLboolean depthMask = GL_TRUE;
    GLint stencilMask = 0;
    GLfloat clearDepth = 1.0f;
    GLint clearStencil = 0;
    GLboolean scissor = glad_glIsEnabled(GL_SCISSOR_TEST);

    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glad_glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glad_glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilMask);
    glad_glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    glad_glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clearStencil);

    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    if (scissor)
        glad_glDisable(GL_SCISSOR_TEST);
    glad_glDepthMask(GL_TRUE);
    glad_glStencilMask(0xFFFFFFFFu);
    glad_glClearDepth(1.0);
    glad_glClearStencil(0);
    glad_glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glad_glClearStencil(clearStencil);
    glad_glClearDepth((GLdouble)clearDepth);
    glad_glStencilMask((GLuint)stencilMask);
    glad_glDepthMask(depthMask);
    if (scissor)
        glad_glEnable(GL_SCISSOR_TEST);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
}

SDL_Window *getSDLWindow()
{
    return g_SdlWindow;
}

SDL_GLContext getSDLContext()
{
    return g_SdlContext;
}

int makeSDLCurrent(SDL_Window *win, SDL_GLContext ctx)
{
    return SDL_GL_MakeCurrent(win, ctx);
}

void sdlQuit()
{
    setSwitch(SYSTEM, BUTTON_TEST, 1);
    usleep(50000);
    setSwitch(SYSTEM, BUTTON_TEST, 0);
    usleep(50000);

    if (TTF_WasInit())
    {
        if (font)
        {
            TTF_CloseFont(font);
            font = NULL;
        }
        TTF_Quit();
    }
    if (fontRenderer)
    {
        SDL_DestroyRenderer(fontRenderer);
        fontRenderer = NULL;
    }

    if (g_SdlContext)
    {
        SDL_GL_DestroyContext(g_SdlContext);
        g_SdlContext = NULL;
    }

    if (g_SdlWindow)
    {
        SDL_DestroyWindow(g_SdlWindow);
        g_SdlWindow = NULL;
    }
    SDL_Quit();
    exit(0);
}

void pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
#ifdef __linux__
        if (event.type == SDL_WIIMOTION_EVENT)
            processSdlEvent(&event);
#endif
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
            {
                SDL_Keymod mod = SDL_GetModState();
                if ((event.key.key == SDLK_RETURN && (mod & SDL_KMOD_ALT)) || event.key.key == SDLK_F11)
                {
                    if (isFullScreen)
                    {
                        if (!SDL_SetWindowFullscreen(g_SdlWindow, 0))
                        {
                            log_error("Error setting windowed mode: %s\n", SDL_GetError());
                        }
                        else
                        {
                            int w = windowedWidth();
                            if ((long long)w * 3 == (long long)gHeight * 4)
                                SDL_SetWindowSize(g_SdlWindow, w + 1, gHeight);
                            else
                                SDL_SetWindowSize(g_SdlWindow, w, gHeight + 1);
                            isFullScreen = false;
                            if (getConfig()->fullscreen)
                                SDL_SetWindowBordered(g_SdlWindow, true);
                        }
                    }
                    else
                    {
                        if (!SDL_SetWindowFullscreen(g_SdlWindow, true))
                        {
                            log_error("Error setting fullscreen mode: %s\n", SDL_GetError());
                        }
                        else
                        {
                            isFullScreen = true;
                        }
                    }
                    break;
                }
                if (event.key.key == SDLK_F12)
                {
                    fpsOverlayToggleVisibility();
                    return;
                }
            }
            case SDL_EVENT_KEY_UP:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            case SDL_EVENT_JOYSTICK_HAT_MOTION:
                processSdlEvent(&event);
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                sdlQuit();
                break;
            case SDL_EVENT_WINDOW_RESTORED:
            {
                int w = windowedWidth();
                if (((long long)w * 3 == (long long)gHeight * 4) || gGrp == GROUP_HUMMER)
                    SDL_SetWindowSize(g_SdlWindow, w + 1, gHeight);
                else
                    SDL_SetWindowSize(g_SdlWindow, w, gHeight + 1);
            }
            break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                processSdlEvent(&event);
            default:
                break;
        }
    }
    if (sdlInputInitialized)
    {
        if (gGrp == GROUP_HOD4 || gGrp == GROUP_HOD4_TEST)
            updateGunShake();
        updateCombinedAxes();
        processChangedActions();
    }
}
