#ifdef __linux__

#include <dlfcn.h>
#include <stddef.h>

#include "../log/log.h"
#include "glEntryRealign.h"

/*
 * Stack realignment shims for the GLX and GLU entry points the games call
 * directly.
 *
 * The i386 SysV ABI requires %esp to be 16-byte aligned at every call. The
 * Lindbergh binaries predate that guarantee and do not honour it. Modern
 * libglvnd and libGLU are built with SSE codegen that stores xmm registers to
 * their own stack frames with movaps, which raises #GP -- delivered as
 * SIGSEGV -- the moment the incoming stack is misaligned. Every libGLX entry
 * point runs __glDispatchCheckMultithreaded() early, and that function is one
 * of the ones that faults, so any GLX call the game makes straight into the
 * system library is a crash on distributions whose libglvnd was built that way.
 *
 * These wrappers do nothing but realign the stack and forward. They are
 * deliberately pure passthroughs: the entry points below are not interposed by
 * the loader today, so the game already reaches the system library for them and
 * this changes no behaviour. Anything the loader does interpose (glXSwapBuffers,
 * glXCreateContext, the SGIX pbuffer calls, the whole glut* surface) is already
 * covered by its own wrapper and must not be repeated here.
 *
 * glXGetProcAddressARB is the exception: the per-game detours in patch.c send
 * it to gl_XGetProcAddressARB, but a game without one (2 Spicy) binds its PLT
 * import straight to libGLX, so it gets a shim here as well.
 *
 * The loader is built with -mincoming-stack-boundary=2, which already forces
 * this realignment everywhere. The explicit attribute is kept so these shims
 * still do their job if that flag is ever dropped.
 */

#define REALIGN __attribute__((force_align_arg_pointer))

static void *resolveNext(const char *name)
{
    void *fn = dlsym(RTLD_NEXT, name);

    if (fn == NULL)
        log_error("Stack alignment shim: could not resolve %s below the loader", name);

    return fn;
}

/* GLXDrawable, GLXPbuffer and friends are XIDs, which are unsigned long on i386. */

REALIGN void *glXChooseVisual(void *dpy, int screen, int *attribList)
{
    static void *(*real)(void *, int, int *) = NULL;
    if (real == NULL)
        real = resolveNext("glXChooseVisual");
    return real ? real(dpy, screen, attribList) : NULL;
}

REALIGN unsigned long glXCreatePbuffer(void *dpy, void *config, const int *attribList)
{
    static unsigned long (*real)(void *, void *, const int *) = NULL;
    if (real == NULL)
        real = resolveNext("glXCreatePbuffer");
    return real ? real(dpy, config, attribList) : 0;
}

REALIGN void glXDestroyContext(void *dpy, void *ctx)
{
    static void (*real)(void *, void *) = NULL;
    if (real == NULL)
        real = resolveNext("glXDestroyContext");
    if (real)
        real(dpy, ctx);
}

REALIGN void glXDestroyPbuffer(void *dpy, unsigned long pbuf)
{
    static void (*real)(void *, unsigned long) = NULL;
    if (real == NULL)
        real = resolveNext("glXDestroyPbuffer");
    if (real)
        real(dpy, pbuf);
}

REALIGN const char *glXGetClientString(void *dpy, int name)
{
    static const char *(*real)(void *, int) = NULL;
    if (real == NULL)
        real = resolveNext("glXGetClientString");
    return real ? real(dpy, name) : NULL;
}

REALIGN void *glXGetCurrentContext(void)
{
    static void *(*real)(void) = NULL;
    if (real == NULL)
        real = resolveNext("glXGetCurrentContext");
    return real ? real() : NULL;
}

REALIGN unsigned long glXGetCurrentDrawable(void)
{
    static unsigned long (*real)(void) = NULL;
    if (real == NULL)
        real = resolveNext("glXGetCurrentDrawable");
    return real ? real() : 0;
}

REALIGN void *glXGetVisualFromFBConfig(void *dpy, void *config)
{
    static void *(*real)(void *, void *) = NULL;
    if (real == NULL)
        real = resolveNext("glXGetVisualFromFBConfig");
    return real ? real(dpy, config) : NULL;
}

REALIGN int glXIsDirect(void *dpy, void *ctx)
{
    static int (*real)(void *, void *) = NULL;
    if (real == NULL)
        real = resolveNext("glXIsDirect");
    return real ? real(dpy, ctx) : 0;
}

REALIGN int glXMakeCurrent(void *dpy, unsigned long drawable, void *ctx)
{
    static int (*real)(void *, unsigned long, void *) = NULL;
    if (real == NULL)
        real = resolveNext("glXMakeCurrent");
    return real ? real(dpy, drawable, ctx) : 0;
}

REALIGN int glXQueryExtension(void *dpy, int *errorBase, int *eventBase)
{
    static int (*real)(void *, int *, int *) = NULL;
    if (real == NULL)
        real = resolveNext("glXQueryExtension");
    return real ? real(dpy, errorBase, eventBase) : 0;
}

REALIGN const char *glXQueryExtensionsString(void *dpy, int screen)
{
    static const char *(*real)(void *, int) = NULL;
    if (real == NULL)
        real = resolveNext("glXQueryExtensionsString");
    return real ? real(dpy, screen) : NULL;
}

REALIGN const char *glXQueryServerString(void *dpy, int screen, int name)
{
    static const char *(*real)(void *, int, int) = NULL;
    if (real == NULL)
        real = resolveNext("glXQueryServerString");
    return real ? real(dpy, screen, name) : NULL;
}

REALIGN int glXQueryVersion(void *dpy, int *major, int *minor)
{
    static int (*real)(void *, int *, int *) = NULL;
    if (real == NULL)
        real = resolveNext("glXQueryVersion");
    return real ? real(dpy, major, minor) : 0;
}

void *glXGetProcAddressReal(const unsigned char *procName)
{
    static void *(*real)(const unsigned char *) = NULL;
    if (real == NULL)
        real = resolveNext("glXGetProcAddressARB");
    return real ? real(procName) : NULL;
}

/* The pointers handed back are called straight out of the game's frame too,
 * so they go out behind the same trampoline gl_XGetProcAddressARB uses. */
REALIGN void *glXGetProcAddressARB(const unsigned char *procName)
{
    return glRealignWrap(glXGetProcAddressReal(procName));
}

REALIGN void *glXGetProcAddress(const unsigned char *procName)
{
    return glRealignWrap(glXGetProcAddressReal(procName));
}

REALIGN void glXWaitX(void)
{
    static void (*real)(void) = NULL;
    if (real == NULL)
        real = resolveNext("glXWaitX");
    if (real)
        real();
}

/*
 * libGLU carries far more stack SSE than libGLX does, and the projection
 * helpers below are the ones the games reach for, so they get the same
 * treatment.
 */

REALIGN const unsigned char *gluErrorString(unsigned int error)
{
    static const unsigned char *(*real)(unsigned int) = NULL;
    if (real == NULL)
        real = resolveNext("gluErrorString");
    return real ? real(error) : NULL;
}

REALIGN void gluLookAt(double eyeX, double eyeY, double eyeZ, double centerX, double centerY, double centerZ, double upX, double upY,
                       double upZ)
{
    static void (*real)(double, double, double, double, double, double, double, double, double) = NULL;
    if (real == NULL)
        real = resolveNext("gluLookAt");
    if (real)
        real(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
}

REALIGN void gluOrtho2D(double left, double right, double bottom, double top)
{
    static void (*real)(double, double, double, double) = NULL;
    if (real == NULL)
        real = resolveNext("gluOrtho2D");
    if (real)
        real(left, right, bottom, top);
}

REALIGN void gluPerspective(double fovy, double aspect, double zNear, double zFar)
{
    static void (*real)(double, double, double, double) = NULL;
    if (real == NULL)
        real = resolveNext("gluPerspective");
    if (real)
        real(fovy, aspect, zNear, zFar);
}

REALIGN int gluProject(double objX, double objY, double objZ, const double *model, const double *proj, const int *view, double *winX,
                       double *winY, double *winZ)
{
    static int (*real)(double, double, double, const double *, const double *, const int *, double *, double *, double *) = NULL;
    if (real == NULL)
        real = resolveNext("gluProject");
    return real ? real(objX, objY, objZ, model, proj, view, winX, winY, winZ) : 0;
}

REALIGN int gluUnProject(double winX, double winY, double winZ, const double *model, const double *proj, const int *view, double *objX,
                         double *objY, double *objZ)
{
    static int (*real)(double, double, double, const double *, const double *, const int *, double *, double *, double *) = NULL;
    if (real == NULL)
        real = resolveNext("gluUnProject");
    return real ? real(winX, winY, winZ, model, proj, view, objX, objY, objZ) : 0;
}

#endif
