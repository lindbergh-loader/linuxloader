#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

#include "bezel.h"

// The image, RGBA from its top row, until it goes to a texture at the first
// draw (a GL context is current only then).
static struct
{
    int width, height;
    unsigned char *pixels;
    GLuint texture;
    int failed;
    // The hole, as fractions of the image from its top left.
    float left, top, right, bottom;
} bezel;

#define TRANSPARENT(p) ((p)[3] < 32)

static void findHole(void)
{
    int w = bezel.width, h = bezel.height, cx = w / 2, cy = h / 2;
    const unsigned char *row = bezel.pixels + (size_t)cy * w * 4;
    int x0 = cx, x1 = cx, y0 = cy, y1 = cy;

    bezel.left = bezel.top = 0.f;
    bezel.right = bezel.bottom = 1.f;
    if (!TRANSPARENT(row + cx * 4))
    {
        printf("Bezel: no transparent middle, the game keeps the whole screen\n");
        return;
    }
    while (x0 > 0 && TRANSPARENT(row + (x0 - 1) * 4))
        x0--;
    while (x1 < w - 1 && TRANSPARENT(row + (x1 + 1) * 4))
        x1++;
    while (y0 > 0 && TRANSPARENT(bezel.pixels + ((size_t)(y0 - 1) * w + cx) * 4))
        y0--;
    while (y1 < h - 1 && TRANSPARENT(bezel.pixels + ((size_t)(y1 + 1) * w + cx) * 4))
        y1++;
    bezel.left = (float)x0 / w;
    bezel.right = (float)(x1 + 1) / w;
    bezel.top = (float)y0 / h;
    bezel.bottom = (float)(y1 + 1) / h;
    printf("Bezel: %dx%d, the game in %d,%d %dx%d of it\n", w, h, x0, y0, x1 + 1 - x0, y1 + 1 - y0);
}

int bezelLoad(const char *path)
{
    int width, height, channels;

    if (!path || !*path)
        return 0;
    bezel.pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!bezel.pixels)
    {
        printf("Bezel: cannot load %s: %s\n", path, stbi_failure_reason());
        return 0;
    }
    bezel.width = width;
    bezel.height = height;
    findHole();
    return 1;
}

int bezelLoaded(void)
{
    return bezel.pixels || bezel.texture;
}

static int shown(void)
{
    return bezelLoaded() && !bezel.failed;
}

void bezelHole(int width, int height, int *x, int *y, int *holeWidth, int *holeHeight)
{
    if (!shown())
    {
        *x = *y = 0;
        *holeWidth = width;
        *holeHeight = height;
        return;
    }
    *x = (int)(bezel.left * width + .5f);
    *holeWidth = (int)(bezel.right * width + .5f) - *x;
    *y = height - (int)(bezel.bottom * height + .5f);
    *holeHeight = height - (int)(bezel.top * height + .5f) - *y;
}

static int upload(void)
{
    static const GLenum store[] = {GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
                                   GL_UNPACK_SKIP_PIXELS};
    GLint unpackBuffer = 0, texture, saved[4];

    if (!glad_glGenTextures)
        return 0;
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (glad_glBindBuffer)
    {
        glad_glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        glad_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    for (int i = 0; i < 4; i++)
    {
        glad_glGetIntegerv(store[i], &saved[i]);
        glad_glPixelStorei(store[i], i ? 0 : 4);
    }
    glad_glGenTextures(1, &bezel.texture);
    glad_glBindTexture(GL_TEXTURE_2D, bezel.texture);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bezel.width, bezel.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      bezel.pixels);
    for (int i = 0; i < 4; i++)
        glad_glPixelStorei(store[i], saved[i]);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    if (glad_glBindBuffer)
        glad_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpackBuffer);
    stbi_image_free(bezel.pixels);
    bezel.pixels = NULL;
    return 1;
}

// A compatibility context (Pac-Man) draws it the fixed-function way, with
// all of its state pushed; a core one (Galaga Assault) has neither, and
// draws it with a shader, what it changes saved by hand.
static int coreProfile(void)
{
    GLint mask = 0;

    if (!glad_glGetString)
        return 0;
    const char *version = (const char *)glad_glGetString(GL_VERSION);
    if (!version || (version[0] < '3' || (version[0] == '3' && version[2] < '2')))
        return 0;
    glad_glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    return (mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
}

static void drawFixed(void)
{
    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();

    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_FOG);
    glad_glDisable(GL_COLOR_MATERIAL);
    glad_glDisable(GL_TEXTURE_1D);
    glad_glDisable(GL_TEXTURE_3D);
    glad_glDisable(GL_TEXTURE_CUBE_MAP);
    glad_glDisable(GL_TEXTURE_GEN_S);
    glad_glDisable(GL_TEXTURE_GEN_T);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColor4f(1.f, 1.f, 1.f, 1.f);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glad_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (glad_glBlendEquation)
        glad_glBlendEquation(GL_FUNC_ADD);
    glad_glBindTexture(GL_TEXTURE_2D, bezel.texture);

    // The image's top row is the texture's first.
    glad_glBegin(GL_QUADS);
    glad_glTexCoord2f(0.f, 1.f);
    glad_glVertex2f(-1.f, -1.f);
    glad_glTexCoord2f(1.f, 1.f);
    glad_glVertex2f(1.f, -1.f);
    glad_glTexCoord2f(1.f, 0.f);
    glad_glVertex2f(1.f, 1.f);
    glad_glTexCoord2f(0.f, 0.f);
    glad_glVertex2f(-1.f, 1.f);
    glad_glEnd();

    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPopMatrix();
    glad_glPopAttrib();
}

static GLuint shader(GLenum type, const char *source)
{
    GLint ok = 0;
    GLuint id = glad_glCreateShader(type);

    glad_glShaderSource(id, 1, &source, NULL);
    glad_glCompileShader(id);
    glad_glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char info[512] = "";
        glad_glGetShaderInfoLog(id, sizeof(info), NULL, info);
        printf("Bezel: shader: %s\n", info);
        glad_glDeleteShader(id);
        return 0;
    }
    return id;
}

// A quad over the viewport, from gl_VertexID: no vertex buffer.
static GLuint coreProgram(void)
{
    static const char *vertex =
        "#version 150\n"
        "out vec2 uv;\n"
        "void main()\n"
        "{\n"
        "    vec2 p = vec2((gl_VertexID & 1) != 0 ? 1.0 : -1.0, (gl_VertexID & 2) != 0 ? 1.0 : -1.0);\n"
        "    uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "}\n";
    static const char *fragment =
        "#version 150\n"
        "uniform sampler2D image;\n"
        "in vec2 uv;\n"
        "out vec4 color;\n"
        "void main()\n"
        "{\n"
        "    color = texture(image, uv);\n"
        "}\n";
    GLint ok = 0;
    GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment), program;

    if (!vs || !fs)
        return 0;
    program = glad_glCreateProgram();
    glad_glAttachShader(program, vs);
    glad_glAttachShader(program, fs);
    glad_glBindFragDataLocation(program, 0, "color");
    glad_glLinkProgram(program);
    glad_glDeleteShader(vs);
    glad_glDeleteShader(fs);
    glad_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        printf("Bezel: cannot link its shader\n");
        glad_glDeleteProgram(program);
        return 0;
    }
    return program;
}

static int drawCore(void)
{
    static GLuint program, vertexArray;
    static const GLenum capabilities[] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST,
                                          GL_CULL_FACE, GL_RASTERIZER_DISCARD};
    GLboolean enabled[6], colorMask[4];
    GLint array, texture, sampler = 0, blend[6], polygonMode[2];

    if (!program)
    {
        if (!glad_glCreateProgram || !glad_glGenVertexArrays || !(program = coreProgram()))
            return 0;
        glad_glGenVertexArrays(1, &vertexArray);
        GLint current;
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        glad_glUseProgram(program);
        glad_glUniform1i(glad_glGetUniformLocation(program, "image"), 0);
        glad_glUseProgram((GLuint)current);
    }

    for (int i = 0; i < 6; i++)
        enabled[i] = glad_glIsEnabled(capabilities[i]);
    glad_glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glad_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (glad_glBindSampler)
        glad_glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    glad_glGetIntegerv(GL_BLEND_SRC_RGB, &blend[0]);
    glad_glGetIntegerv(GL_BLEND_DST_RGB, &blend[1]);
    glad_glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend[2]);
    glad_glGetIntegerv(GL_BLEND_DST_ALPHA, &blend[3]);
    glad_glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend[4]);
    glad_glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend[5]);
    glad_glGetIntegerv(GL_POLYGON_MODE, polygonMode);

    for (int i = 1; i < 6; i++)
        glad_glDisable(capabilities[i]);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glBlendEquation(GL_FUNC_ADD);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glad_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glad_glUseProgram(program);
    glad_glBindVertexArray(vertexArray);
    glad_glBindTexture(GL_TEXTURE_2D, bezel.texture);
    if (glad_glBindSampler)
        glad_glBindSampler(0, 0);
    glad_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (glad_glBindSampler)
        glad_glBindSampler(0, (GLuint)sampler);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    glad_glBindVertexArray((GLuint)array);
    glad_glPolygonMode(GL_FRONT_AND_BACK, (GLenum)polygonMode[0]);
    glad_glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glad_glBlendFuncSeparate((GLenum)blend[0], (GLenum)blend[1], (GLenum)blend[2], (GLenum)blend[3]);
    glad_glBlendEquationSeparate((GLenum)blend[4], (GLenum)blend[5]);
    for (int i = 0; i < 6; i++)
        if (enabled[i])
            glad_glEnable(capabilities[i]);
        else
            glad_glDisable(capabilities[i]);
    return 1;
}

void bezelDraw(int x, int y, int width, int height)
{
    static int core = -1;
    GLint program = 0, activeTexture = GL_TEXTURE0, drawFramebuffer = 0, drawBuffer, viewport[4];

    if (width <= 0 || height <= 0 || !shown())
        return;
    if (core < 0)
        core = coreProfile();
    if ((!core && !glad_glBegin) || (!bezel.texture && !upload()))
    {
        printf("Bezel: no GL texture, not drawn\n");
        bezel.failed = 1;
        return;
    }

    // Onto the window's back buffer, the game's state kept aside.
    if (glad_glUseProgram)
    {
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glad_glUseProgram(0);
    }
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glad_glActiveTexture(GL_TEXTURE0);
    }
    if (glad_glBindFramebuffer)
    {
        glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
    glad_glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    glad_glGetIntegerv(GL_VIEWPORT, viewport);
    glad_glDrawBuffer(GL_BACK);
    glad_glViewport(x, y, width, height);

    if (core ? !drawCore() : (drawFixed(), 0))
    {
        printf("Bezel: no shader for this GL context, not drawn\n");
        bezel.failed = 1;
    }

    glad_glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glad_glDrawBuffer((GLenum)drawBuffer);
    if (glad_glBindFramebuffer)
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFramebuffer);
    if (glad_glActiveTexture)
        glad_glActiveTexture((GLenum)activeTexture);
    if (glad_glUseProgram)
        glad_glUseProgram((GLuint)program);
}
