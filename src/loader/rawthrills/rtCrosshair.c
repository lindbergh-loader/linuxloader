// The guns' crosshairs of the Raw Thrills IR gun games that opt in (their
// descriptor's frameDraw), which the cabinets have none of, with
// [CrossHairs] ENABLE_CROSSHAIRS, as Lindbergh's and Teamplay's:
// P1_CROSSHAIR_PATH and P2_CROSSHAIR_PATH (from the game's directory when
// relative), else the game's directory's P1.png and P2.png, else a ring and
// a cross drawn as windowsarcadeloader draws them (player 1 blue, player 2
// red). CUSTOM_CROSSHAIRS_WIDTH x HEIGHT pixels of a 1280x960 picture (the
// default 64: a 15th of the screen's height), centred on each gun's aim
// (rtIo.c's, the one the game is given) while it is on the screen. Drawn
// over the frame before the swap, the fixed-function way, the game's state
// put back.

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glad/gl.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../log/log.h"

#define CROSSHAIR_GUNS 2
// The drawn crosshair's picture, scaled down to its size on the screen.
#define CROSSHAIR_SHAPE 128

#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#define GL_VERTEX_PROGRAM_ARB 0x8620
#define GL_FRAGMENT_PROGRAM_ARB 0x8804
#define GL_FRAGMENT_PROGRAM_NV 0x8870

static struct
{
    GLuint texture;
    int failed;
} crosshair[CROSSHAIR_GUNS];

// Signed distance from a box of half sizes hx, hy centred on the origin.
static float boxDistance(float x, float y, float hx, float hy)
{
    float qx = fabsf(x) - hx, qy = fabsf(y) - hy;
    float ox = fmaxf(qx, 0.f), oy = fmaxf(qy, 0.f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.f);
}

// The drawn crosshair, windowsarcadeloader's: a ring, a cross with a gap and
// a dot, white strokes in the player's colour (1 blue, 2 red) with a black
// outline, their edges antialiased from each pixel's distance to them.
static SDL_Surface *drawnCrosshair(int gun)
{
    static const uint8_t colours[CROSSHAIR_GUNS][3] = {{0x20, 0x80, 0xff}, {0xff, 0x30, 0x30}};
    SDL_Surface *s = SDL_CreateSurface(CROSSHAIR_SHAPE, CROSSHAIR_SHAPE, SDL_PIXELFORMAT_RGBA32);
    const float c = CROSSHAIR_SHAPE / 2.f, gap = c * .28f, end = c * .92f;
    const float mid = (gap + end) / 2.f, half = (end - gap) / 2.f;

    if (!s)
        return NULL;
    for (int y = 0; y < CROSSHAIR_SHAPE; y++)
    {
        uint8_t *row = (uint8_t *)s->pixels + y * s->pitch;
        for (int x = 0; x < CROSSHAIR_SHAPE; x++)
        {
            float dx = x + .5f - c, dy = y + .5f - c, r = sqrtf(dx * dx + dy * dy);
            float d = fabsf(r - c * .62f) - 6.f;
            d = fminf(d, boxDistance(dx - mid, dy, half, 6.f));
            d = fminf(d, boxDistance(dx + mid, dy, half, 6.f));
            d = fminf(d, boxDistance(dx, dy - mid, 6.f, half));
            d = fminf(d, boxDistance(dx, dy + mid, 6.f, half));
            d = fminf(d, r - 7.f);
            // The stroke white (times the colour), then a 4.5 pixel outline.
            float white = fminf(fmaxf(.5f - d, 0.f), 1.f), alpha = fminf(fmaxf(5.f - d, 0.f), 1.f);
            for (int i = 0; i < 3; i++)
                row[4 * x + i] = (uint8_t)(white * colours[gun][i]);
            row[4 * x + 3] = (uint8_t)(alpha * 255.f);
        }
    }
    return s;
}

static int loadCrosshair(int gun)
{
    const char *configured = gun ? getConfig()->p2CrossHairPath : getConfig()->p1CrossHairPath;
    char path[PATH_MAX];
    GLint bound = 0, alignment = 4;
    SDL_Surface *loaded;

    if (crosshair[gun].texture || crosshair[gun].failed)
        return crosshair[gun].texture != 0;
    if (configured[0] == '/')
        snprintf(path, sizeof(path), "%s", configured);
    else if (configured[0])
        snprintf(path, sizeof(path), "%s/%s", rtGameDir(), configured);
    else
        snprintf(path, sizeof(path), "%s/P%d.png", rtGameDir(), gun + 1);
    loaded = IMG_Load(path);
    if (!loaded && configured[0])
        log_warn("Raw Thrills: no crosshair for player %d (%s)", gun + 1, path);
    SDL_Surface *rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : drawnCrosshair(gun);
    if (loaded)
        SDL_DestroySurface(loaded);
    if (!rgba)
    {
        crosshair[gun].failed = 1;
        return 0;
    }
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glad_glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glad_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glad_glGenTextures(1, &crosshair[gun].texture);
    glad_glBindTexture(GL_TEXTURE_2D, crosshair[gun].texture);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glad_glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    SDL_DestroySurface(rgba);
    return 1;
}

// x, y, width, height: the window's drawable area, from its bottom left.
void rtCrosshairFrameDraw(int x, int y, int width, int height)
{
    float aimX[CROSSHAIR_GUNS], aimY[CROSSHAIR_GUNS];
    int shown[CROSSHAIR_GUNS], any = 0;
    GLint activeTexture = GL_TEXTURE0, program = 0, drawFbo = 0;

    if (!getConfig()->enableCrosshairs)
        return;
    for (int g = 0; g < CROSSHAIR_GUNS; g++)
        any |= shown[g] = rtGunOnScreen(g, &aimX[g], &aimY[g]);
    if (!any)
        return;
    float halfW = getConfig()->customCrossHairWidth * width / (2.f * 1280.f);
    float halfH = getConfig()->customCrossHairHeight * height / (2.f * 960.f);

    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glad_glDrawBuffer(GL_BACK);
    if (glad_glUseProgram)
    {
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glad_glUseProgram(0);
    }
    // Fixed function on texture unit 0 alone: the game's other units and its
    // ARB / NV programs (Cg's) off too, their enables pushed with the rest.
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        for (int unit = 1; unit < 4; unit++)
        {
            glad_glActiveTexture(GL_TEXTURE0 + unit);
            glad_glDisable(GL_TEXTURE_1D);
            glad_glDisable(GL_TEXTURE_2D);
            glad_glDisable(GL_TEXTURE_3D);
            glad_glDisable(GL_TEXTURE_CUBE_MAP);
        }
        glad_glActiveTexture(GL_TEXTURE0);
    }
    glad_glDisable(GL_VERTEX_PROGRAM_ARB);
    glad_glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glad_glDisable(GL_FRAGMENT_PROGRAM_NV);
    glad_glDisable(GL_TEXTURE_1D);
    glad_glDisable(GL_TEXTURE_3D);
    glad_glDisable(GL_TEXTURE_CUBE_MAP);
    glad_glDisable(GL_COLOR_LOGIC_OP);
    glad_glDisable(GL_TEXTURE_GEN_S);
    glad_glDisable(GL_TEXTURE_GEN_T);
    glad_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glOrtho(0, width, 0, height, -1, 1);
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glViewport(x, y, width, height);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_FOG);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glBlendEquation(GL_FUNC_ADD);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glad_glDepthMask(GL_FALSE);
    for (int g = 0; g < CROSSHAIR_GUNS; g++)
    {
        if (!shown[g] || !loadCrosshair(g))
            continue;
        float cx = aimX[g] * width, cy = (1.f - aimY[g]) * height;
        glad_glBindTexture(GL_TEXTURE_2D, crosshair[g].texture);
        glad_glBegin(GL_QUADS);
        glad_glTexCoord2f(0, 1);
        glad_glVertex2f(cx - halfW, cy - halfH);
        glad_glTexCoord2f(1, 1);
        glad_glVertex2f(cx + halfW, cy - halfH);
        glad_glTexCoord2f(1, 0);
        glad_glVertex2f(cx + halfW, cy + halfH);
        glad_glTexCoord2f(0, 0);
        glad_glVertex2f(cx - halfW, cy + halfH);
        glad_glEnd();
    }
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPopMatrix();
    if (glad_glActiveTexture)
        glad_glActiveTexture((GLenum)activeTexture);
    if (glad_glUseProgram)
        glad_glUseProgram((GLuint)program);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFbo);
    glad_glPopAttrib();
}
