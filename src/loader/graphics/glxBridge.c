#include <glad/gl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <stdbool.h>
#include "loader/patching/flowControl.h"
#endif

#include "../elfLoader/glHooks.hpp"
#include "../config/config.h"
#include "border.h"
#include "crossHair.h"
#include "blitStretching.h"
#include "fpsLimiter.h"
#include "overlayMsgs.h"
#include "loader/hardware/lindbergh/touchScreen.h"
#include "sdlCalls.h"
#include "../log/log.h"

#include <math.h>
#include <stdlib.h>
#include <GL/glu.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "glxBridge.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GLX_PBUFFER_HEIGHT 0x8040
#define GLX_PBUFFER_WIDTH 0x8041
#define GLX_LARGEST_PBUFFER 0x801C
#define GLX_PRESERVED_CONTENTS 0x801B

extern uint32_t gId;
extern int gGrp;

SDL_Window *g_pbufWindow = NULL;

#ifdef _WIN32
// --------------------------------------------------------------------------
// Internal Structure Definitions
// --------------------------------------------------------------------------
struct __GLXFBConfigRec
{
    int dummy_attr;
};

// ==========================================================================
//   GLX Implementation
// ==========================================================================

XVisualInfo *bridgeGlxChooseVisual(Display *dpy, int screen, int *attribList)
{
    XVisualInfo *vi = (XVisualInfo *)calloc(1, sizeof(XVisualInfo));
    if (vi)
    {
        vi->visual = (void *)1;
        vi->visualid = 1;
        vi->screen = screen;
        vi->depth = 24;
        vi->class_type = 4; // TrueColor
        vi->bits_per_rgb = 8;
        vi->red_mask = 0xFF0000;
        vi->green_mask = 0x00FF00;
        vi->blue_mask = 0x0000FF;
    }
    return vi;
}

GLXContext bridgeGlxCreateContext(Display *dpy, XVisualInfo *vis, GLXContext shareList, bool direct)
{
    log_debug("glXCreateContext(vis=%p, "
              "share=%p, direct=%d)",
              vis, shareList, direct);

    if (!getSDLWindow())
    {
        startSDL();
    }

    if (!getSDLWindow())
        return NULL;

    // If SDLCalls already manages the context, we return it.
    if (!getSDLContext())
    {
        log_error("glXCreateContext: SDL Context not available from SDLCalls.");
        return NULL;
    }

    log_info("Created OpenGL Context: %p", getSDLContext());
    return (GLXContext)getSDLContext();
}

void bridgeGlxDestroyContext(Display *dpy, GLXContext ctx)
{
    log_debug("glXDestroyContext(%p)", ctx);
    if (ctx)
        SDL_GL_DestroyContext(getSDLContext());
}

bool bridgeGlxMakeContextCurrent(Display *dpy, GLXDrawable drawable, GLXDrawable read, GLXContext ctx)
{
    if (drawable != read)
    {
        printf("Read and Draw drawables are different.\n");
    }

    SDL_Window *target_window = NULL;

    if (drawable == 0)
    { // Unbind context
        if (makeSDLCurrent(NULL, NULL) < 0)
        {
            log_error("glXMakeContextCurrent failed to unbind context: %s\n", SDL_GetError());
            return 0;
        }
        return 1;
    }
    else if (drawable == 1)
    {
        target_window = getSDLWindow();
    }
    else
    {
        target_window = (SDL_Window *)drawable;
    }

    if (!target_window)
    {
        log_error(" drawable %p -> NULL window\n", (void *)drawable);
        return 0;
    }

    if (!SDL_GL_MakeCurrent(target_window, (SDL_GLContext)ctx))
    {
        log_error("glXMakeCurrent failed for window %p context %p: %s\n", (void *)target_window, (void *)ctx, SDL_GetError());
        return 0;
    }

    return 1;
}

bool bridgeGlxMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx)
{
    return bridgeGlxMakeContextCurrent(NULL, drawable, drawable, ctx);
}

int bridgeGlxSwapInterval(int interval)
{
    return SDL_GL_SetSwapInterval(interval) ? 0 : 1;
}

bool bridgeGlxQueryVersion(Display *dpy, int *major, int *minor)
{
    if (major)
        *major = 1;
    if (minor)
        *minor = 4;
    return true;
}

void *bridgeGlxGetProcAddress(const char *procName)
{
    void *proc = NULL;
    proc = GLHooks_GetProcAddress(procName);
    if (proc)
        return proc;

    log_game("GLXBridge::GetProcAddress: Missing wrapper for '%s'", procName);
    return NULL;
}

const char *bridgeGlxQueryExtensionsString(Display *dpy, int screen)
{
    return "";
}
const char *bridgeGlxQueryServerString(Display *dpy, int screen, int name)
{
    return "";
}
const char *bridgeGlxGetClientString(Display *dpy, int name)
{
    return "";
}

static char g_dummy_display_mem[1024] = {0};
Display *bridgeGlxGetCurrentDisplay()
{
    return (Display *)&g_dummy_display_mem;
}

GLXContext bridgeGlxGetCurrentContext()
{
    return (GLXContext)getSDLContext();
}

GLXDrawable bridgeGlxGetCurrentDrawable()
{
    if (SDL_GL_GetCurrentWindow() == getSDLWindow())
        return 1;
    return (GLXDrawable)getSDLWindow();
}

int bridgeGlxIsDirect(Display *dpy, GLXContext ctx)
{
    return 1;
}

int bridgeGlxGetConfig(Display *dpy, XVisualInfo *vis, int attrib, int *value)
{
    if (value)
        *value = 0;
    return 0;
}

GLXFBConfig *bridgeGlxChooseFBConfig(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    static struct __GLXFBConfigRec dummyConfig;

    GLXFBConfig *configs = (GLXFBConfig *)malloc(sizeof(GLXFBConfig) * 2);
    if (configs)
    {
        configs[0] = &dummyConfig;
        configs[1] = NULL;
    }
    if (nelements)
        *nelements = 1;
    return configs;
}

// --- SGIX Extensions ---

GLXFBConfig *bridgeGlxChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    return bridgeGlxChooseFBConfig(dpy, screen, attrib_list, nelements);
}

int bridgeGlxGetFBConfigAttribSGIX(Display *dpy, GLXFBConfig config, int attribute, int *value)
{
    if (value)
        *value = 0;
    return 0;
}

XVisualInfo *bridgeGlxGetVisualFromFBConfig(Display *dpy, GLXFBConfig config)
{
    return bridgeGlxChooseVisual(dpy, 0, NULL);
}

GLXPbuffer bridgeGlxCreatePbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list)
{
    int width = 0;
    int height = 0;

    if (attrib_list)
    {
        for (int i = 0; attrib_list[i] != 0; i += 2)
        {
            if (attrib_list[i] == GLX_PBUFFER_WIDTH)
            {
                width = attrib_list[i + 1];
            }
            if (attrib_list[i] == GLX_PBUFFER_HEIGHT)
            {
                height = attrib_list[i + 1];
            }
        }
    }

    if (width == 0)
        width = 512;
    if (height == 0)
        height = 512;

    log_debug("Creating fake pbuffer (hidden window) of size %dx%d\n", width, height);

    g_pbufWindow = SDL_CreateWindow("Fake Pbuffer", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);

    if (!g_pbufWindow)
    {
        log_error("Failed to create hidden window for Pbuffer: %s\n", SDL_GetError());
        return 0;
    }

    return (GLXPbuffer)g_pbufWindow;
}

void bridgeGlxDestroyPbuffer(Display *dpy, GLXPbuffer pbuf)
{
    if (pbuf)
        SDL_DestroyWindow((SDL_Window *)pbuf);
}

void bridgeGlxDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf)
{
    bridgeGlxDestroyPbuffer(dpy, pbuf);
}

GLXPbuffer bridgeGlxCreateGLXPbufferSGIX(Display *dpy, GLXFBConfig config, int width, int height, int *attrib_list)
{
    int pbufferAttribs[] = {
        GLX_PBUFFER_WIDTH, width, GLX_PBUFFER_HEIGHT, height, GLX_PRESERVED_CONTENTS, true, GLX_LARGEST_PBUFFER, true, 0};
    return bridgeGlxCreatePbuffer(dpy, config, pbufferAttribs);
}

GLXContext bridgeGlxCreateNewContext(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct)
{

    if (!getSDLWindow())
    {
        log_error("No window available for context creation");
        return NULL;
    }

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, share_list != NULL ? 1 : 0);

    SDL_GLContext ctx = SDL_GL_CreateContext(getSDLWindow());
    if (!ctx)
    {
        log_error("Failed to create GL context: %s", SDL_GetError());
        return NULL;
    }
    log_info("Created GL context: %p", ctx);
    return (GLXContext)ctx;
}

GLXContext bridgeGlxCreateContextWithConfigSGIX(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct)
{
    return bridgeGlxCreateNewContext(dpy, config, render_type, share_list, direct);
}

bool bridgeGlxQueryExtension(Display *dpy, const char *extList)
{
    return true;
}

// ==========================================================================
//   GLU Implementation
// ==========================================================================

void bridgegluPerspective(double fovy, double aspect, double zNear, double zFar)
{
    double f = 1.0 / tan(fovy * M_PI / 360.0);
    double m[16] = {0};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0;
    m[14] = (2.0 * zFar * zNear) / (zNear - zFar);
    glad_glMultMatrixd(m);
}

void bridgegluOrtho2D(double left, double right, double bottom, double top)
{
    glad_glOrtho(left, right, bottom, top, -1.0, 1.0);
}

void bridgegluLookAt(double eyeX, double eyeY, double eyeZ, double centerX, double centerY, double centerZ, double upX, double upY,
                     double upZ)
{
    double f[3] = {centerX - eyeX, centerY - eyeY, centerZ - eyeZ};
    double mag = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (mag > 0)
    {
        f[0] /= mag;
        f[1] /= mag;
        f[2] /= mag;
    }
    double u[3] = {upX, upY, upZ};
    mag = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (mag > 0)
    {
        u[0] /= mag;
        u[1] /= mag;
        u[2] /= mag;
    }
    double s[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
    mag = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (mag > 0)
    {
        s[0] /= mag;
        s[1] /= mag;
        s[2] /= mag;
    }
    u[0] = s[1] * f[2] - s[2] * f[1];
    u[1] = s[2] * f[0] - s[0] * f[2];
    u[2] = s[0] * f[1] - s[1] * f[0];
    double mat[16] = {s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1};
    glad_glMultMatrixd(mat);
    glad_glTranslated(-eyeX, -eyeY, -eyeZ);
}

char glubuffer[64];
const unsigned char *bridgegluErrorString(unsigned int error)
{
    switch (error)
    {
        case GLU_INVALID_ENUM:
            return (const GLubyte *)"GLU_INVALID_ENUM";
        case GLU_INVALID_VALUE:
            return (const GLubyte *)"GLU_INVALID_VALUE";
        case GLU_OUT_OF_MEMORY:
            return (const GLubyte *)"GLU_OUT_OF_MEMORY";
        default:
            sprintf(glubuffer, "Unknown GLU error: %d", error);
            return (const GLubyte *)glubuffer;
    }
}

int bridgegluProject(double objX, double objY, double objZ, const double *model, const double *proj, const int *view, double* winX, double* winY, double* winZ)
{
    log_trace("gluProject(objX=%f, objY=%f, objZ=%f) - STUB", objX, objY, objZ);
    if (winX) *winX = 0.0;
    if (winY) *winY = 0.0;
    if (winZ) *winZ = 0.0;
    return 1;
}
int bridgegluUnProject(double winX, double winY, double winZ, const double *model, const double *proj, const int *view, double* objX, double* objY, double* objZ)
{
    log_trace("gluUnProject(winX=%f, winY=%f, winZ=%f) - STUB", winX, winY, winZ);
    if (objX) *objX = 0.0;
    if (objY) *objY = 0.0;
    if (objZ) *objZ = 0.0;
    return 1; 
}

#endif

void bridgeGlxSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    EmulatorConfig *config = getConfig();

    if (config->borderEnabled)
        drawGameBorder(config->width, config->height, config->whiteBorderPercentage, config->blackBorderPercentage);

    if (p1CrossHairInitialized || p2CrossHairInitialized)
        renderCrosshairs();

    blitStretch();
    overlayInit();
    overlayRender();

    pollEvents();

    SDL_GL_SwapWindow(getSDLWindow());

    if (config->fpsLimiter)
        frameTiming();

    static double localFps = 0.0;
    static char windowTitle[256] = {0};
    localFps = calculateFps();
    sprintf(windowTitle, "%s - FPS: %.2f", getGameName(), localFps);
    SDL_SetWindowTitle(getSDLWindow(), windowTitle);
    fpsOverlayUpdateFps(localFps);

    if (gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE)
        mj4TouchHolding();
}

int bridgeGlxGetVideoSyncSGI(unsigned int *count)
{
    static unsigned int frameCount = 0;
    *count = (frameCount++) / 2;
    return 0;
}

int bridgeGlxGetRefreshRateSGI(unsigned int *rate)
{
    *rate = 60;
    return 0;
}

int bridgeGlxSwapIntervalSGI(int interval)
{
    return 0;
}

#ifdef __linux__

extern Display *x11Display;

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    bridgeGlxSwapBuffers(dpy, drawable);
}

GLXContext glXCreateContext(Display *dpy, XVisualInfo *vis, GLXContext shareList, int direct)
{
    GLXContext (*_glXCreateContext)(Display *dpy, XVisualInfo *vis, GLXContext shareList, int direct) =
        dlsym(RTLD_NEXT, "glXCreateContext");

    static int ctxCnt = 0;
    GLXContext ctx = NULL;
    if (ctxCnt == 0)
        ctx = (GLXContext)getSDLContext();
    else
        ctx = _glXCreateContext(dpy, vis, shareList, direct);

    ctxCnt++;
    return ctx;
}

GLXFBConfig *glXChooseFBConfig(Display *dpy, int screen, const int *attrib_list, int *nelements)
{
    GLXFBConfig *(*_glXChooseFBConfig)(Display *dpy, int screen, const int *attrib_list, int *nelements) =
        dlsym(RTLD_NEXT, "glXChooseFBConfig");

    char *__GLX_VENDOR_LIBRARY_NAME = getenv("__GLX_VENDOR_LIBRARY_NAME");
    char *__NV_PRIME_RENDER_OFFLOAD = getenv("__NV_PRIME_RENDER_OFFLOAD");
    if (__GLX_VENDOR_LIBRARY_NAME == NULL)
        __GLX_VENDOR_LIBRARY_NAME = " ";
    if (__NV_PRIME_RENDER_OFFLOAD == NULL)
        __NV_PRIME_RENDER_OFFLOAD = " ";

    if (gGrp == GROUP_HOD4 || gGrp == GROUP_HOD4_SP || gId == THE_HOUSE_OF_THE_DEAD_EX_SBRC || gId == TOO_SPICY_SBMV ||
        gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE)
    {
        if (strcmp(__GLX_VENDOR_LIBRARY_NAME, "nvidia") == 0 && strcmp(__NV_PRIME_RENDER_OFFLOAD, "1") == 0)
        {
            for (int i = 0; attrib_list[i] != None; i += 2)
            {
                if (attrib_list[i] == GLX_DOUBLEBUFFER || attrib_list[i] == GLX_DRAWABLE_TYPE)
                {
                    int *ptr = (int *)&attrib_list[i + 1];
                    patchMemoryFromString((size_t)ptr, "01");
                }
            }
        }
    }
    return _glXChooseFBConfig(dpy, screen, attrib_list, nelements);
}

GLXPbuffer glXCreateGLXPbufferSGIX(Display *dpy, GLXFBConfigSGIX config, unsigned int width, unsigned int height, int *attrib_list)
{
    int pbufferAttribs[] = {
        GLX_PBUFFER_WIDTH, width, GLX_PBUFFER_HEIGHT, height, GLX_PRESERVED_CONTENTS, true, GLX_LARGEST_PBUFFER, true, None};
    return glXCreatePbuffer(dpy, config, pbufferAttribs);
}

void glXDestroyGLXPbufferSGIX(Display *dpy, GLXPbufferSGIX pbuf)
{
    glXDestroyPbuffer(dpy, pbuf);
}

GLXFBConfigSGIX *glXChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    char *__GLX_VENDOR_LIBRARY_NAME = getenv("__GLX_VENDOR_LIBRARY_NAME");
    char *__NV_PRIME_RENDER_OFFLOAD = getenv("__NV_PRIME_RENDER_OFFLOAD");
    if (__GLX_VENDOR_LIBRARY_NAME == NULL)
        __GLX_VENDOR_LIBRARY_NAME = " ";
    if (__NV_PRIME_RENDER_OFFLOAD == NULL)
        __NV_PRIME_RENDER_OFFLOAD = " ";

    if (gGrp == GROUP_ID4_EXP || gGrp == GROUP_ID4_JAP)
    {
        if (strcmp(__GLX_VENDOR_LIBRARY_NAME, "nvidia") == 0 && strcmp(__NV_PRIME_RENDER_OFFLOAD, "1") == 0)
        {
            for (int i = 0; attrib_list[i] != None; i += 2)
            {
                if (attrib_list[i] == GLX_DOUBLEBUFFER)
                {
                    int *ptr = (int *)&attrib_list[i + 1];
                    patchMemoryFromString((size_t)ptr, "01");
                }
            }
        }
    }
    return glXChooseFBConfig(dpy, screen, attrib_list, nelements);
}

int glXGetFBConfigAttribSGIX(Display *dpy, GLXFBConfigSGIX config, int attribute, int *value)
{
    return glXGetFBConfigAttrib(dpy, config, attribute, value);
}

GLXContext glXCreateContextWithConfigSGIX(Display *dpy, GLXFBConfigSGIX config, int render_type, GLXContext share_list, Bool direct)
{
    return glXCreateNewContext(dpy, config, render_type, share_list, direct);
}

Display *glXGetCurrentDisplay(void)
{
    return x11Display;
}

int glXGetVideoSyncSGI(unsigned int *count)
{
    return bridgeGlxGetVideoSyncSGI(count);
}

int glXGetRefreshRateSGI(unsigned int *rate)
{
    return bridgeGlxGetRefreshRateSGI(rate);
}

int glXSwapIntervalSGI(int interval)
{
    return bridgeGlxSwapIntervalSGI(interval);
}

#endif

/*
#ifdef __cplusplus
extern "C"
{
#endif

    // GLX & GLU Wrappers
    XVisualInfo *glXChooseVisual(Display *dpy, int screen, int *attribList)
    {
        return bridgeGlxChooseVisual(dpy, screen, attribList);
    }

    GLXContext glXCreateContext(Display *dpy, XVisualInfo *vis, GLXContext shareList, bool direct)
    {
        return bridgeGlxCreateContext(dpy, vis, shareList, direct);
    }

    void glXDestroyContext(Display *dpy, GLXContext ctx)
    {
        bridgeGlxDestroyContext(dpy, ctx);
    }

    bool glXMakeContextCurrent(Display *dpy, GLXDrawable drawable, GLXDrawable read, GLXContext ctx)
    {
        return bridgeGlxMakeContextCurrent(dpy, drawable, read, ctx);
    }

    bool glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx)
    {
        return bridgeGlxMakeCurrent(dpy, drawable, ctx);
    }

    void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
    {
        bridgeGlxSwapBuffers(dpy, drawable);
    }

    int glXSwapInterval(int interval)
    {
        return bridgeGlxSwapInterval(interval);
    }

    bool glXQueryVersion(Display *dpy, int *major, int *minor)
    {
        return bridgeGlxQueryVersion(dpy, major, minor);
    }

    void *glXGetProcAddress(const char *procName)
    {
        return bridgeGlxGetProcAddress(procName);
    }

    const char *glXQueryExtensionsString(Display *dpy, int screen)
    {
        return bridgeGlxQueryExtensionsString(dpy, screen);
    }

    const char *glXQueryServerString(Display *dpy, int screen, int name)
    {
        return bridgeGlxQueryServerString(dpy, screen, name);
    }

    const char *glXGetClientString(Display *dpy, int name)
    {
        return bridgeGlxGetClientString(dpy, name);
    }

    Display *glXGetCurrentDisplay()
    {
        return bridgeGlxGetCurrentDisplay();
    }

    GLXContext glXGetCurrentContext()
    {
        return bridgeGlxGetCurrentContext();
    }

    GLXDrawable glXGetCurrentDrawable()
    {
        return bridgeGlxGetCurrentDrawable();
    }

    int glXIsDirect(Display *dpy, GLXContext ctx)
    {
        return bridgeGlxIsDirect(dpy, ctx);
    }

    int glXGetConfig(Display *dpy, XVisualInfo *vis, int attrib, int *value)
    {
        return bridgeGlxGetConfig(dpy, vis, attrib, value);
    }

    GLXFBConfig *glXChooseFBConfig(Display *dpy, int screen, int *attrib_list, int *nelements)
    {
        return bridgeGlxChooseFBConfig(dpy, screen, attrib_list, nelements);
    }

    GLXFBConfig *glXChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements)
    {
        return bridgeGlxChooseFBConfigSGIX(dpy, screen, attrib_list, nelements);
    }

    int glXGetFBConfigAttribSGIX(Display *dpy, GLXFBConfig config, int attribute, int *value)
    {
        return bridgeGlxGetFBConfigAttribSGIX(dpy, config, attribute, value);
    }

    XVisualInfo *glXGetVisualFromFBConfig(Display *dpy, GLXFBConfig config)
    {
        return bridgeGlxGetVisualFromFBConfig(dpy, config);
    }

    GLXPbuffer glXCreatePbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list)
    {
        return bridgeGlxCreatePbuffer(dpy, config, attrib_list);
    }

    void glXDestroyPbuffer(Display *dpy, GLXPbuffer pbuf)
    {
        bridgeGlxDestroyPbuffer(dpy, pbuf);
    }

    void glXDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf)
    {
        bridgeGlxDestroyGLXPbufferSGIX(dpy, pbuf);
    }

    GLXPbuffer glXCreateGLXPbufferSGIX(Display *dpy, GLXFBConfig config, int width, int height, int *attrib_list)
    {
        return bridgeGlxCreateGLXPbufferSGIX(dpy, config, width, height, attrib_list);
    }

    GLXContext glXCreateNewContext(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct)
    {
        return bridgeGlxCreateNewContext(dpy, config, render_type, share_list, direct);
    }

    GLXContext glXCreateContextWithConfigSGIX(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct)
    {
        return bridgeGlxCreateContextWithConfigSGIX(dpy, config, render_type, share_list, direct);
    }

    bool glXQueryExtension(Display *dpy, const char *extList)
    {
        return bridgeGlxQueryExtension(dpy, extList);
    }

#ifdef __linux__
    void gluPerspective(double fovy, double aspect, double zNear, double zFar)
    {
        bridgegluPerspective(fovy, aspect, zNear, zFar);
    }

    void gluOrtho2D(double left, double right, double bottom, double top)
    {
        bridgegluOrtho2D(left, right, bottom, top);
    }

    void gluLookAt(double eyeX, double eyeY, double eyeZ, double centerX, double centerY, double centerZ, double upX, double upY,
                   double upZ)
    {
        bridgegluLookAt(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
    }

    const GLubyte *gluErrorString(unsigned int error)
    {
        return bridgegluErrorString(error);
    }
#endif

#ifdef __cplusplus
}
#endif
*/
