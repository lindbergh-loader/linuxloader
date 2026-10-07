// The loader's crosshairs for America's Army, whose arcade HUD shows none:
// a ring and a cross over each gun's aim (player 1 red, player 2 blue), drawn
// into the game's 640x480 picture before each of its SDL_GL_SwapBuffers
// (with sdl12-compat, the frame it scales to the window afterwards). Legacy
// GL, as the game's renderer, every state touched put back. The GL entry
// points are libGL's own (the loader's gl* interposers are the Lindbergh
// games'), looked up at the first frame: the engine loads libGL itself.
// The light gun border ([Display] BORDER_ENABLED) goes around the same
// picture, after the crosshairs.

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>

#include <glad/gl.h>

#include "gvr.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/border.h"

#define CROSSHAIR_WIDTH 640
#define CROSSHAIR_HEIGHT 480
#define CROSSHAIR_RADIUS 9.f
#define CROSSHAIR_ARM 15.f
#define CROSSHAIR_GAP 4.f
#define CROSSHAIR_SEGMENTS 24

#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_MAX_TEXTURE_UNITS 0x84E2
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#define GL_VERTEX_PROGRAM_ARB 0x8620
#define GL_FRAGMENT_PROGRAM_ARB 0x8804
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6

static void (*realSwapBuffers)(void);

static struct
{
    void (*PushAttrib)(GLbitfield);
    void (*PopAttrib)(void);
    void (*GetIntegerv)(GLenum, GLint *);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFunc)(GLenum, GLenum);
    void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*MatrixMode)(GLenum);
    void (*PushMatrix)(void);
    void (*PopMatrix)(void);
    void (*LoadIdentity)(void);
    void (*Ortho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
    void (*LineWidth)(GLfloat);
    void (*Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Begin)(GLenum);
    void (*End)(void);
    void (*Vertex2f)(GLfloat, GLfloat);
    void (*ActiveTexture)(GLenum); // optional
    void (*UseProgram)(GLuint);    // optional
} gl;

// 1 once the entry points are there, -1 if they cannot be.
static int loadGl(void)
{
    static int loaded;
    static const char *const names[] = {"glPushAttrib", "glPopAttrib", "glGetIntegerv", "glEnable", "glDisable",
                                        "glBlendFunc",  "glColorMask", "glViewport",    "glMatrixMode", "glPushMatrix",
                                        "glPopMatrix",  "glLoadIdentity", "glOrtho",    "glLineWidth", "glColor4f",
                                        "glBegin",      "glEnd",       "glVertex2f"};
    void **slots = (void **)&gl;

    if (loaded)
        return loaded;
    void *libGl = dlopen("libGL.so.1", RTLD_NOW | RTLD_NOLOAD);
    void *(*getProcAddress)(const char *) = libGl ? (void *(*)(const char *))dlsym(libGl, "glXGetProcAddressARB") : NULL;
    loaded = 1;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!libGl || !(slots[i] = dlsym(libGl, names[i])))
        {
            fprintf(stderr, "Global VR: crosshairs: no %s in libGL\n", names[i]);
            loaded = -1;
            break;
        }
    if (getProcAddress)
    {
        gl.ActiveTexture = (void (*)(GLenum))getProcAddress("glActiveTexture");
        gl.UseProgram = (void (*)(GLuint))getProcAddress("glUseProgram");
    }
    return loaded;
}

static void drawCrosshair(float x, float y, float r, float g, float b, float width)
{
    gl.LineWidth(width);
    gl.Color4f(r, g, b, 1.f);
    gl.Begin(GL_LINE_LOOP);
    for (int i = 0; i < CROSSHAIR_SEGMENTS; i++)
    {
        float a = 2.f * (float)M_PI * i / CROSSHAIR_SEGMENTS;
        gl.Vertex2f(x + CROSSHAIR_RADIUS * cosf(a), y + CROSSHAIR_RADIUS * sinf(a));
    }
    gl.End();
    gl.Begin(GL_LINES);
    gl.Vertex2f(x - CROSSHAIR_ARM, y);
    gl.Vertex2f(x - CROSSHAIR_GAP, y);
    gl.Vertex2f(x + CROSSHAIR_GAP, y);
    gl.Vertex2f(x + CROSSHAIR_ARM, y);
    gl.Vertex2f(x, y - CROSSHAIR_ARM);
    gl.Vertex2f(x, y - CROSSHAIR_GAP);
    gl.Vertex2f(x, y + CROSSHAIR_GAP);
    gl.Vertex2f(x, y + CROSSHAIR_ARM);
    gl.End();
}

static void drawCrosshairs(void)
{
    static const float colours[2][3] = {{1.f, .2f, .2f}, {.3f, .55f, 1.f}};
    GLint program = 0, active = GL_TEXTURE0, units = 1;
    int x[2], y[2], shown[2];

    shown[0] = gvrLinkGun(0, &x[0], &y[0]);
    shown[1] = gvrLinkGun(1, &x[1], &y[1]);
    if ((!shown[0] && !shown[1]) || loadGl() < 0)
        return;

    gl.PushAttrib(GL_ALL_ATTRIB_BITS);
    if (gl.UseProgram)
    {
        gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
        if (program)
            gl.UseProgram(0);
    }
    gl.Disable(GL_VERTEX_PROGRAM_ARB);
    gl.Disable(GL_FRAGMENT_PROGRAM_ARB);
    if (gl.ActiveTexture)
    {
        gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
        gl.GetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
    }
    for (int unit = units - 1; unit >= 0; unit--)
    {
        if (gl.ActiveTexture)
            gl.ActiveTexture(GL_TEXTURE0 + unit);
        gl.Disable(GL_TEXTURE_1D);
        gl.Disable(GL_TEXTURE_2D);
        gl.Disable(GL_TEXTURE_3D);
        gl.Disable(GL_TEXTURE_CUBE_MAP);
    }
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_ALPHA_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_LIGHTING);
    gl.Disable(GL_FOG);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.Enable(GL_LINE_SMOOTH);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.Viewport(0, 0, CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT);

    gl.MatrixMode(GL_TEXTURE);
    gl.PushMatrix();
    gl.LoadIdentity();
    gl.MatrixMode(GL_PROJECTION);
    gl.PushMatrix();
    gl.LoadIdentity();
    gl.Ortho(0, CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT, 0, -1, 1);
    gl.MatrixMode(GL_MODELVIEW);
    gl.PushMatrix();
    gl.LoadIdentity();

    for (int p = 0; p < 2; p++)
    {
        if (!shown[p])
            continue;
        // A dark outline under the colour, seen on any background.
        drawCrosshair(x[p] + .5f, y[p] + .5f, 0.f, 0.f, 0.f, 4.f);
        drawCrosshair(x[p] + .5f, y[p] + .5f, colours[p][0], colours[p][1], colours[p][2], 2.f);
    }

    gl.MatrixMode(GL_MODELVIEW);
    gl.PopMatrix();
    gl.MatrixMode(GL_PROJECTION);
    gl.PopMatrix();
    gl.MatrixMode(GL_TEXTURE);
    gl.PopMatrix();
    if (gl.ActiveTexture)
        gl.ActiveTexture(active);
    if (program && gl.UseProgram)
        gl.UseProgram(program);
    gl.PopAttrib();
}

// The border's clears, through glad: loaded from libGL at the first frame.
static void drawBorder(void)
{
    static int loaded;
    GLboolean mask[4], scissor;
    GLint box[4];

    if (!loaded)
    {
        void *libGl = dlopen("libGL.so.1", RTLD_NOW | RTLD_NOLOAD);
        GLADloadfunc getProcAddress = libGl ? (GLADloadfunc)dlsym(libGl, "glXGetProcAddressARB") : NULL;
        loaded = getProcAddress && gladLoadGL(getProcAddress) ? 1 : -1;
        if (loaded < 0)
            fprintf(stderr, "Global VR: cannot load the GL entry points, no gun border\n");
    }
    if (loaded < 0)
        return;
    glad_glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    scissor = glad_glIsEnabled(GL_SCISSOR_TEST);
    glad_glGetIntegerv(GL_SCISSOR_BOX, box);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    drawGameBorder(CROSSHAIR_WIDTH, CROSSHAIR_HEIGHT, getConfig()->whiteBorderPercentage,
                   getConfig()->blackBorderPercentage);
    glad_glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glad_glScissor(box[0], box[1], box[2], box[3]);
    if (scissor)
        glad_glEnable(GL_SCISSOR_TEST);
}

static void gameSwapBuffers(void)
{
    if (getConfig()->enableCrosshairs)
        drawCrosshairs();
    if (getConfig()->borderEnabled)
        drawBorder();
    realSwapBuffers();
}

void gvrCrosshairInstall(void)
{
    if (!getConfig()->enableCrosshairs && !getConfig()->borderEnabled)
        return;
    *(void **)&realSwapBuffers = hookExecutableImport("SDL_GL_SwapBuffers", (void *)gameSwapBuffers);
    if (!realSwapBuffers)
        fprintf(stderr, "Global VR: no SDL_GL_SwapBuffers to draw the crosshairs and border at\n");
    else
        printf("Global VR:%s%s\n", getConfig()->enableCrosshairs ? " crosshairs" : "",
               getConfig()->borderEnabled ? " gun border" : "");
}
