#pragma once
#include "x11types.h"

#include <stdbool.h>

#ifdef _WIN32
// --------------------------------------------------------------------------
// GLX Types & Constants
// --------------------------------------------------------------------------
typedef XID GLXContextID;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef XID GLXPbuffer;
typedef XID GLXWindow;
typedef XID GLXFBConfigID;

typedef struct __GLXcontextRec *GLXContext;
typedef struct __GLXFBConfigRec *GLXFBConfig;

// Visual Info Structure (Compatible with X11)
typedef struct
{
    void *visual;
    VisualID visualid;
    int screen;
    int depth;
    int class_type;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
} XVisualInfo;
#else
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif
    XVisualInfo *bridgeGlxChooseVisual(Display *dpy, int screen, int *attribList);
    GLXContext bridgeGlxCreateContext(Display *dpy, XVisualInfo *vis, GLXContext shareList, bool direct);
    void bridgeGlxDestroyContext(Display *dpy, GLXContext ctx);
    void bridgeGlxSwapBuffers(Display *dpy, GLXDrawable drawable);
    int bridgeGlxSwapInterval(int interval);
    void *bridgeGlxGetProcAddress(const char *procName);
    bool bridgeGlxQueryVersion(Display *dpy, int *major, int *minor);
    const char *bridgeGlxQueryExtensionsString(Display *dpy, int screen);
    const char *bridgeGlxQueryServerString(Display *dpy, int screen, int name);
    const char *bridgeGlxGetClientString(Display *dpy, int name);
    Display *bridgeGlxGetCurrentDisplay();
    GLXContext bridgeGlxGetCurrentContext();
    GLXDrawable bridgeGlxGetCurrentDrawable();
    bool bridgeGlxMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx);
    bool bridgeGlxMakeContextCurrent(Display *dpy, GLXDrawable drawable, GLXDrawable read, GLXContext ctx);
    int bridgeGlxIsDirect(Display *dpy, GLXContext ctx);
    int bridgeGlxGetConfig(Display *dpy, XVisualInfo *vis, int attrib, int *value);
    GLXFBConfig *bridgeGlxChooseFBConfig(Display *dpy, int screen, int *attrib_list, int *nelements);
    GLXFBConfig *bridgeGlxChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements);
    int bridgeGlxGetFBConfigAttribSGIX(Display *dpy, GLXFBConfig config, int attribute, int *value);
    XVisualInfo *bridgeGlxGetVisualFromFBConfig(Display *dpy, GLXFBConfig config);
    GLXContext bridgeGlxCreateContextWithConfigSGIX(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct);
    int bridgeGlxGetVideoSyncSGI(unsigned int *count);
    int bridgeGlxGetRefreshRateSGI(unsigned int *rate);
    int bridgeGlxSwapIntervalSGI(int interval);
    GLXPbuffer bridgeGlxCreatePbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list);
    void bridgeGlxDestroyPbuffer(Display *dpy, GLXPbuffer pbuf);
    GLXPbuffer bridgeGlxCreateGLXPbufferSGIX(Display *dpy, GLXFBConfig config, int width, int height, int *attrib_list);
    void bridgeGlxDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf);
    GLXContext bridgeGlxCreateNewContext(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct);
    bool bridgeGlxQueryExtension(Display *dpy, const char *extList);

    // --- GLU Utilities ---
    void bridgegluPerspective(double fovy, double aspect, double zNear, double zFar);
    void bridgegluLookAt(double eyeX, double eyeY, double eyeZ, double centerX, double centerY, double centerZ, double upX, double upY,
                         double upZ);
    void bridgegluOrtho2D(double left, double right, double bottom, double top);
    const unsigned char *bridgegluErrorString(unsigned int error);
    int bridgegluProject(double objX, double objY, double objZ, const double *model, const double *proj, const int *view, double* winX, double* winY, double* winZ);
    int bridgegluUnProject(double winX, double winY, double winZ, const double *model, const double *proj, const int *view, double* objX, double* objY, double* objZ);

#ifdef __cplusplus
}
#endif