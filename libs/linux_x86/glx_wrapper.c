// GLX wrapper for Counter Strike NEO.
//
// SDL 1.2's X11 video driver dlopens libGLX.so.6 and resolves glXChooseFBConfig
// from that handle. LD_PRELOAD interposers are invisible to these calls.
// This wrapper is placed at csneo2/lib/libGLX.so.6 (the engine's RPATH finds
// it first). It wraps the real libGLX.so.6 and relaxes glXChooseFBConfig to
// retry with minimal attributes when the full list finds no visual.
//
// Build:
//   cp <system-libGLX> csneo2/lib/libGLX.real.so.6
//   gcc -m32 -shared -fPIC -o csneo2/lib/libGLX.so.6 glx_wrapper.c -ldl
//   ln -sf libGLX.real.so.6 csneo2/lib/libGLX.real.so

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// The real GLX, loaded at first use.
static void *realGLX;

static void *getRealGLX(void)
{
    if (!realGLX)
    {
        Dl_info info;
        char path[4096];
        if (dladdr((void *)getRealGLX, &info) && info.dli_fname)
        {
            strncpy(path, info.dli_fname, sizeof(path) - 20);
            char *lib = strrchr(path, '/');
            if (lib)
                strcpy(lib, "/libGLX.real.so.6");
            else
                strcpy(path, "libGLX.real.so.6");
        }
        else
            strcpy(path, "libGLX.real.so.6");
        realGLX = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    }
    return realGLX;
}

// Opaque types (we don't need the real definitions).
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef struct _XDisplay *Display;

// glXChooseFBConfig: retry with minimal attrs if the full list fails.
typedef GLXFBConfig *(*ChooseFBConfigFn)(Display *, int, const int *, int *);

// GLX attribute constants.
#define GLX_DOUBLEBUFFER 0x005
#define GLX_NONE 0x0000

GLXFBConfig *glXChooseFBConfig(Display *dpy, int screen, const int *attrib_list, int *nelements)
{
    static ChooseFBConfigFn real;
    if (!real)
        real = (ChooseFBConfigFn)dlsym(getRealGLX(), "glXChooseFBConfig");
    if (!real)
        return NULL;

    GLXFBConfig *configs = real(dpy, screen, attrib_list, nelements);
    if (getenv("CSNEO_TRACE"))
    {
        fprintf(stderr, "GLXwrap: glXChooseFBConfig full result=%p\n", (void *)configs);
        for (int i = 0; attrib_list && attrib_list[i] != 0; i += 2)
            fprintf(stderr, "  attr[%d]=%d val=%d\n", i / 2, attrib_list[i], attrib_list[i + 1]);
    }
    if (configs)
        return configs;

    // Retry with just doublebuffer.
    int minimal[] = {GLX_DOUBLEBUFFER, 1, GLX_NONE};
    configs = real(dpy, screen, minimal, nelements);
    if (getenv("CSNEO_TRACE"))
        fprintf(stderr, "GLXwrap: glXChooseFBConfig minimal result=%p\n", (void *)configs);
    if (configs && nelements)
        *nelements = 1;
    return configs;
}

// glXChooseFBConfigSGIX: same.
typedef GLXFBConfig *(*ChooseFBConfigSGIXFn)(Display *, int, int *, int *);
GLXFBConfig *glXChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    static ChooseFBConfigSGIXFn real;
    if (!real)
        real = (ChooseFBConfigSGIXFn)dlsym(getRealGLX(), "glXChooseFBConfigSGIX");
    if (!real)
        return NULL;

    GLXFBConfig *configs = real(dpy, screen, attrib_list, nelements);
    if (configs)
        return configs;
    int minimal[] = {GLX_DOUBLEBUFFER, 1, GLX_NONE};
    configs = real(dpy, screen, minimal, nelements);
    if (configs && nelements)
        *nelements = 1;
    return configs;
}
