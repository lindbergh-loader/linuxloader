// Minimal GLU stub for Counter Strike NEO.
//
// The engine imports only 5 GLU symbols (gluErrorString, gluBuild2DMipmaps,
// gluLookAt, gluOrtho2D, gluPerspective) - it does NOT use GLU for rendering
// or tessellation. But the cabinet's real libGLU.so.1 is built for the old
// glibc and references GL entry points as undefined symbols, so loading it
// grabs an early/broken GL context that breaks SDL3's later GLX visual lookup
// ("Couldn't find matching GLX visual").
//
// This stub provides the needed symbols with no GL dependencies, so the
// cabinet GLU is never loaded and the GLX path is left to Mesa.
//
// Build: gcc -m32 -shared -fPIC -o libGLU.so.1 glu_stub.c

#include <stdint.h>
#include <stdlib.h>

const char *gluErrorString(int error)
{
    (void)error;
    return "glu error";
}

// Mipmap builders: no-op (mipmaps are not essential for the game to render).
int gluBuild2DMipmaps(int target, int internalformat, int width, int height, int format, const void *data)
{
    (void)target;(void)internalformat;(void)width;(void)height;(void)format;(void)data;
    return 0;
}
int gluBuild3DMipmaps(int target, int internalformat, int width, int height, int depth, int format, const void *data)
{
    (void)target;(void)internalformat;(void)width;(void)height;(void)depth;(void)format;(void)data;
    return 0;
}
int gluBuild2DMipmapLevels(const void *data, int format, int internalformat, int width, int height, int base, int levels, int border)
{
    (void)data;(void)format;(void)internalformat;(void)width;(void)height;(void)base;(void)levels;(void)border;
    return 0;
}
int gluBuild2DMipmapCube(const void *data, int format, int internalformat, int width, int height, int border)
{
    (void)data;(void)format;(void)internalformat;(void)width;(void)height;(void)border;
    return 0;
}

// Projection/view helpers referenced by the engine's menu/3D setup. These are
// no-ops: the engine's own GL state (via the Neo renderer) drives the actual
// matrix, so the GLU wrappers here are only needed to satisfy symbol lookup.
void gluLookAt(double eyex, double eyey, double eyez, double centerx, double centery, double centerz, double upx, double upy, double upz)
{
    (void)eyex;(void)eyey;(void)eyez;(void)centerx;(void)centery;(void)centerz;(void)upx;(void)upy;(void)upz;
}
void gluPerspective(double fovy, double aspect, double zNear, double zFar)
{
    (void)fovy;(void)aspect;(void)zNear;(void)zFar;
}
void gluOrtho2D(double left, double right, double bottom, double top)
{
    (void)left;(void)right;(void)bottom;(void)top;
}
void gluOrtho2D2(double left, double right, double bottom, double top)
{
    (void)left;(void)right;(void)bottom;(void)top;
}
void gluOrtho(double left, double right, double bottom, double top, double zNear, double zFar)
{
    (void)left;(void)right;(void)bottom;(void)top;(void)zNear;(void)zFar;
}
