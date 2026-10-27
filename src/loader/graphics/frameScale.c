#include <stdio.h>
#include <string.h>
#include <glad/gl.h>

#include "border.h"
#include "frameScale.h"

static GLADloadfunc loader;
static int keepAspectRatio = 1;
static int glLoaded;

static int renderWidth, renderHeight;   // the game's size: the window's at the first swap
static int active, failed;
static int viewportWidth, viewportHeight;   // the game's last full-frame viewport
static int mismatches;                      // swaps in a row the window has not had the game's size
static GLuint gameFramebuffer;          // what the game gets for framebuffer 0
static GLuint resolveFramebuffer;       // single-sampled copy, when the game's is multisampled
static GLuint renderbuffers[3];
static struct { int x, y, w, h, windowWidth, windowHeight; } dest;
static void (*frameHole)(int, int, int *, int *, int *, int *);
// Games following their window drawn at a shape of their own (aspect): the
// box their full-window viewports are moved to, as fractions of the window
// (width 0: none).
static int aspectWidth, aspectHeight;
static float boxLeft, boxWidth;

void frameScaleInit(void *(*getProcAddress)(const char *), int keepAspect)
{
    loader = (GLADloadfunc)getProcAddress;
    keepAspectRatio = keepAspect;
}

void frameScaleSetAspect(int width, int height)
{
    aspectWidth = width;
    aspectHeight = height;
}

static int quarterTurns;

void frameScaleSetRotation(int turns)
{
    quarterTurns = turns;
}

void frameScaleSetFrame(int width, int height, void (*hole)(int, int, int *, int *, int *, int *))
{
    renderWidth = width;
    renderHeight = height;
    frameHole = hole;
}

static int gl(void)
{
    if (!glLoaded)
        glLoaded = loader && gladLoadGL(loader) ? 1 : -1;
    return glLoaded > 0;
}

// The window's own buffers name the offscreen framebuffer's single one.
static GLenum windowBuffer(GLenum buffer)
{
    switch (buffer)
    {
    case GL_FRONT:
    case GL_BACK:
    case GL_LEFT:
    case GL_FRONT_LEFT:
    case GL_BACK_LEFT:
    case GL_FRONT_AND_BACK:
        return GL_COLOR_ATTACHMENT0;
    }
    return buffer;
}

static int boundToGame(GLenum binding)
{
    GLint framebuffer = 0;
    if (!active)
        return 0;
    glad_glGetIntegerv(binding, &framebuffer);
    return (GLuint)framebuffer == gameFramebuffer;
}

static void GLAD_API_PTR bindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (!gl())
        return;
    glad_glBindFramebuffer(target, framebuffer == 0 && active ? gameFramebuffer : framebuffer);
}

static void GLAD_API_PTR bindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    if (!gl())
        return;
    framebuffer = framebuffer == 0 && active ? gameFramebuffer : framebuffer;
    if (glad_glBindFramebufferEXT)
        glad_glBindFramebufferEXT(target, framebuffer);
    else
        glad_glBindFramebuffer(target, framebuffer);
}

static void GLAD_API_PTR drawBuffer(GLenum buffer)
{
    if (!gl())
        return;
    glad_glDrawBuffer(boundToGame(GL_DRAW_FRAMEBUFFER_BINDING) ? windowBuffer(buffer) : buffer);
}

static void GLAD_API_PTR drawBuffers(GLsizei n, const GLenum *buffers)
{
    GLenum mapped[16];
    if (!gl())
        return;
    if (n > 0 && n <= 16 && boundToGame(GL_DRAW_FRAMEBUFFER_BINDING))
    {
        for (GLsizei i = 0; i < n; i++)
            mapped[i] = windowBuffer(buffers[i]);
        buffers = mapped;
    }
    glad_glDrawBuffers(n, buffers);
}

static void GLAD_API_PTR readBuffer(GLenum buffer)
{
    if (!gl())
        return;
    glad_glReadBuffer(boundToGame(GL_READ_FRAMEBUFFER_BINDING) ? windowBuffer(buffer) : buffer);
}

// Games that follow their window (glut's reshape callback, Big Buck World)
// set a viewport of the window's new size: they scale themselves.
static void GLAD_API_PTR viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (!gl())
        return;
    if (x == 0 && y == 0)
    {
        viewportWidth = width;
        viewportHeight = height;
        // Wider than the game's shape: the middle of it, black bars aside
        // (what the game clears the window with).
        if (aspectWidth && height > 0 && (long)width * aspectHeight > (long)height * aspectWidth)
        {
            int w = (int)((long)height * aspectWidth / aspectHeight);
            x = (width - w) / 2;
            boxLeft = (float)x / width;
            boxWidth = (float)w / width;
            width = w;
        }
    }
    glad_glViewport(x, y, width, height);
}

// The game still sees framebuffer 0 and the window's back buffer.
static void GLAD_API_PTR getIntegerv(GLenum name, GLint *data)
{
    if (!gl())
        return;
    glad_glGetIntegerv(name, data);
    if (!active || !data)
        return;
    switch (name)
    {
    case GL_DRAW_FRAMEBUFFER_BINDING:
    case GL_READ_FRAMEBUFFER_BINDING:
        if ((GLuint)data[0] == gameFramebuffer)
            data[0] = 0;
        break;
    case GL_DRAW_BUFFER:
        if (data[0] == GL_COLOR_ATTACHMENT0 && boundToGame(GL_DRAW_FRAMEBUFFER_BINDING))
            data[0] = GL_BACK;
        break;
    case GL_READ_BUFFER:
        if (data[0] == GL_COLOR_ATTACHMENT0 && boundToGame(GL_READ_FRAMEBUFFER_BINDING))
            data[0] = GL_BACK;
        break;
    }
}

void *frameScaleWrapper(const char *name)
{
    static const struct { const char *name; void *fn; } wrappers[] = {
        {"glBindFramebuffer", (void *)bindFramebuffer},
        {"glBindFramebufferEXT", (void *)bindFramebufferEXT},
        {"glDrawBuffer", (void *)drawBuffer},
        {"glDrawBuffers", (void *)drawBuffers},
        {"glDrawBuffersARB", (void *)drawBuffers},
        {"glReadBuffer", (void *)readBuffer},
        {"glGetIntegerv", (void *)getIntegerv},
        {"glViewport", (void *)viewport},
    };
    if (!loader)
        return NULL;
    for (size_t i = 0; name && i < sizeof(wrappers) / sizeof(wrappers[0]); i++)
        if (!strcmp(name, wrappers[i].name))
            return wrappers[i].fn;
    return NULL;
}

static GLuint framebufferWith(GLuint color, GLuint depthStencil)
{
    GLuint framebuffer;
    glad_glGenFramebuffers(1, &framebuffer);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glad_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (depthStencil)
        glad_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencil);
    if (glad_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glad_glDeleteFramebuffers(1, &framebuffer);
        return 0;
    }
    glad_glClearColor(0.f, 0.f, 0.f, 1.f);
    glad_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    return framebuffer;
}

static void storage(GLuint renderbuffer, GLint samples, GLenum format)
{
    glad_glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    if (samples > 1)
        glad_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, renderWidth, renderHeight);
    else
        glad_glRenderbufferStorage(GL_RENDERBUFFER, format, renderWidth, renderHeight);
}

// The offscreen framebuffer, as multisampled as the window's; the game's
// bindings of framebuffer 0 move over to it.
static int activate(void)
{
    GLint draw, read, renderbuffer, samples = 0, scissor = glad_glIsEnabled(GL_SCISSOR_TEST);
    GLfloat clear[4];

    if (!glad_glGenFramebuffers || !glad_glBlitFramebuffer || !glad_glGenRenderbuffers)
        return 0;
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glad_glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glad_glGetIntegerv(GL_SAMPLES, &samples);
    glad_glDisable(GL_SCISSOR_TEST);

    glad_glGenRenderbuffers(3, renderbuffers);
    storage(renderbuffers[0], samples, GL_RGBA8);
    storage(renderbuffers[1], samples, GL_DEPTH24_STENCIL8);
    gameFramebuffer = framebufferWith(renderbuffers[0], renderbuffers[1]);
    if (gameFramebuffer && samples > 1)
    {
        storage(renderbuffers[2], 0, GL_RGBA8);
        resolveFramebuffer = framebufferWith(renderbuffers[2], 0);
    }

    glad_glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)renderbuffer);
    glad_glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (scissor)
        glad_glEnable(GL_SCISSOR_TEST);
    if (!gameFramebuffer || (samples > 1 && !resolveFramebuffer))
    {
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
        glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
        return 0;
    }
    active = 1;
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw ? (GLuint)draw : gameFramebuffer);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, read ? (GLuint)read : gameFramebuffer);
    return 1;
}

// The frame (source, a framebuffer), turned a quarter turn, drawn into the
// window's back buffer at x, y, w x h: copied to a texture, drawn as a quad
// with the fixed pipeline, the game's state put back after.
static void drawTurned(GLuint source, int x, int y, int w, int h)
{
    static GLuint texture, framebuffer;
    GLint program = 0, activeTexture = GL_TEXTURE0, bound = 0;

    if (!texture)
    {
        glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        glad_glGenTextures(1, &texture);
        glad_glBindTexture(GL_TEXTURE_2D, texture);
        glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, renderWidth, renderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glad_glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
        glad_glGenFramebuffers(1, &framebuffer);
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glad_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glad_glBlitFramebuffer(0, 0, renderWidth, renderHeight, 0, 0, renderWidth, renderHeight, GL_COLOR_BUFFER_BIT,
                           GL_NEAREST);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glad_glDrawBuffer(GL_BACK);

    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (glad_glUseProgram)
    {
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glad_glUseProgram(0);
    }
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int unit = 3; unit >= 0; unit--)
        {
            glad_glActiveTexture(GL_TEXTURE0 + unit);
            glad_glDisable(GL_TEXTURE_2D);
        }
    }
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();

    glad_glViewport(x, y, w, h);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_BLEND);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_FOG);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glBindTexture(GL_TEXTURE_2D, texture);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColor4f(1.f, 1.f, 1.f, 1.f);
    // The window's corners from its bottom left, counterclockwise, and the
    // frame's points shown there.
    static const float corner[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
    static const float frameCorner[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
    int shift = quarterTurns > 0 ? 1 : 3;
    glad_glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        glad_glTexCoord2f(frameCorner[(i + shift) % 4][0], frameCorner[(i + shift) % 4][1]);
        glad_glVertex2f(corner[i][0], corner[i][1]);
    }
    glad_glEnd();

    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPopMatrix();
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (glad_glActiveTexture)
        glad_glActiveTexture((GLenum)activeTexture);
    if (glad_glUseProgram)
        glad_glUseProgram((GLuint)program);
    glad_glPopAttrib();
}

int frameScalePresent(int areaX, int areaY, int windowWidth, int windowHeight, int border,
                      float whiteBorderPercentage, float blackBorderPercentage)
{
    if (failed || windowWidth <= 0 || windowHeight <= 0 || !gl())
        return 0;
    if (!renderWidth)
    {
        renderWidth = windowWidth;
        renderHeight = windowHeight;
        return 0;
    }
    if (!active && quarterTurns)
    {
        // A turned frame never fits the window as it is.
        if (++mismatches >= 2)
        {
            failed = !activate();
            printf("Frame scaling: %dx%d frame turned into a %dx%d window%s\n", renderWidth, renderHeight, windowWidth,
                   windowHeight, failed ? " failed, no offscreen framebuffer" : "");
        }
        return 0;
    }
    if (!active)
    {
        if (windowWidth == renderWidth && windowHeight == renderHeight)
        {
            mismatches = 0;
            return 0;
        }
        // A game that answered the resize with a viewport of the window's
        // size scales itself: its size is now the window's.
        if (viewportWidth == windowWidth && viewportHeight == windowHeight)
        {
            renderWidth = windowWidth;
            renderHeight = windowHeight;
            mismatches = 0;
            return 0;
        }
        // Give the game a swap to answer the resize; then this frame went to
        // the window, and the offscreen framebuffer takes the next ones.
        if (++mismatches >= 2)
        {
            failed = !activate();
            printf("Frame scaling: %dx%d frame to a %dx%d window%s\n", renderWidth, renderHeight, windowWidth,
                   windowHeight, failed ? " failed, no offscreen framebuffer" : "");
        }
        return 0;
    }

    GLint draw, read, scissorBox[4];
    GLboolean scissor = glad_glIsEnabled(GL_SCISSOR_TEST), colorMask[4];
    GLfloat clear[4];
    GLuint source = gameFramebuffer;

    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glad_glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    glad_glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (resolveFramebuffer)
    {
        glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, gameFramebuffer);
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer);
        glad_glBlitFramebuffer(0, 0, renderWidth, renderHeight, 0, 0, renderWidth, renderHeight,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
        source = resolveFramebuffer;
    }

    int hx = 0, hy = 0, hw = windowWidth, hh = windowHeight;
    if (frameHole)
        frameHole(windowWidth, windowHeight, &hx, &hy, &hw, &hh);
    dest.x = hx;
    dest.y = hy;
    dest.w = hw;
    dest.h = hh;
    // A turned frame's sides are swapped in the window.
    int frameW = quarterTurns ? renderHeight : renderWidth, frameH = quarterTurns ? renderWidth : renderHeight;
    if (keepAspectRatio)
    {
        if ((double)hw * frameH > (double)hh * frameW)
            dest.w = (int)((double)hh * frameW / frameH);
        else
            dest.h = (int)((double)hw * frameH / frameW);
        dest.x = hx + (hw - dest.w) / 2;
        dest.y = hy + (hh - dest.h) / 2;
    }
    dest.windowWidth = windowWidth;
    dest.windowHeight = windowHeight;

    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glad_glDrawBuffer(GL_BACK);
    glad_glClearColor(0.f, 0.f, 0.f, 1.f);
    glad_glClear(GL_COLOR_BUFFER_BIT);
    if (quarterTurns)
        drawTurned(source, areaX + dest.x, areaY + dest.y, dest.w, dest.h);
    else
        glad_glBlitFramebuffer(0, 0, renderWidth, renderHeight, areaX + dest.x, areaY + dest.y,
                               areaX + dest.x + dest.w, areaY + dest.y + dest.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    drawFrameOverlay(areaX, areaY, windowWidth, windowHeight);
    if (border)
        drawGameBorderAt(areaX + dest.x, areaY + dest.y, dest.w, dest.h, whiteBorderPercentage, blackBorderPercentage);

    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glad_glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glad_glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glad_glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    if (scissor)
        glad_glEnable(GL_SCISSOR_TEST);
    else
        glad_glDisable(GL_SCISSOR_TEST);
    return 1;
}

int frameScaleWindowToGame(float *x, float *y)
{
    float px, py;
    if (!active && boxWidth > 0.f)
    {
        *x = (*x - boxLeft) / boxWidth;
        return *x >= 0.f && *x <= 1.f && *y >= 0.f && *y <= 1.f;
    }
    if (!active || dest.w <= 0 || dest.h <= 0)
        return *x >= 0.f && *x <= 1.f && *y >= 0.f && *y <= 1.f;
    // dest is from the bottom left, x and y from the top left.
    px = (*x * dest.windowWidth - dest.x) / dest.w;
    py = (*y * dest.windowHeight - (dest.windowHeight - dest.y - dest.h)) / dest.h;
    *x = px;
    *y = py;
    return px >= 0.f && px <= 1.f && py >= 0.f && py <= 1.f;
}
