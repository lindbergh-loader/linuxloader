// The guns' crosshairs of the Teamplay glut games (Crossfire Maximum
// Paintball, Star Trek: Voyager), which the cabinets have none of, with
// [CrossHairs] ENABLE_CROSSHAIRS: P1_CROSSHAIR_PATH and P2_CROSSHAIR_PATH
// (from the game's directory when relative), else the game's directory's
// P1.png and P2.png, else a ring and a cross (player 1 red, player 2 blue).
// CUSTOM_CROSSHAIRS_WIDTH x HEIGHT pixels of a 1280x960 picture (the default
// 64: a 15th of the screen's height), centred on each gun's aim while it is
// on the screen. Drawn into the game's frame before the loader scales it,
// the fixed-function way, the game's state put back.

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glad/gl.h>

#include "teamplay.h"
#include "../config/config.h"
#include "../log/log.h"

#define CROSSHAIR_GUNS 2
#define CROSSHAIR_SIZE 64

static struct
{
    GLuint texture;
    int failed;
} crosshair[CROSSHAIR_GUNS];

// The drawn crosshair: a ring and a cross with a gap, outlined in black.
static SDL_Surface *drawnCrosshair(int gun)
{
    static const uint32_t colours[CROSSHAIR_GUNS] = {0xff3030, 0x3080ff};
    SDL_Surface *s = SDL_CreateSurface(CROSSHAIR_SIZE, CROSSHAIR_SIZE, SDL_PIXELFORMAT_RGBA32);
    const float c = (CROSSHAIR_SIZE - 1) / 2.f, radius = 20.f, gap = 7.f;

    if (!s)
        return NULL;
    for (int y = 0; y < CROSSHAIR_SIZE; y++)
    {
        uint8_t *row = (uint8_t *)s->pixels + y * s->pitch;
        for (int x = 0; x < CROSSHAIR_SIZE; x++)
        {
            float dx = x - c, dy = y - c, d = sqrtf(dx * dx + dy * dy);
            float ring = fabsf(d - radius);
            float arm = fminf(fabsf(dx), fabsf(dy));
            int onArm = d > gap && d < CROSSHAIR_SIZE / 2.f;
            // Inside: the colour; around it, the outline.
            int inside = ring < 1.5f || (onArm && arm < 1.5f);
            int outline = ring < 3.f || (onArm && arm < 3.f);
            uint32_t rgb = inside ? colours[gun] : 0;
            row[4 * x + 0] = rgb >> 16;
            row[4 * x + 1] = rgb >> 8;
            row[4 * x + 2] = rgb;
            row[4 * x + 3] = inside ? 255 : outline ? 160 : 0;
        }
    }
    return s;
}

static int loadCrosshair(int gun)
{
    const char *configured = gun ? getConfig()->p2CrossHairPath : getConfig()->p1CrossHairPath;
    char path[64];
    GLint bound = 0, alignment = 4;
    SDL_Surface *loaded;

    if (crosshair[gun].texture || crosshair[gun].failed)
        return crosshair[gun].texture != 0;
    snprintf(path, sizeof(path), "P%d.png", gun + 1);
    if (configured[0])
    {
        loaded = IMG_Load(configured);
        if (!loaded)
            log_warn("Teamplay: no crosshair for player %d (%s)", gun + 1, configured);
    }
    else
        loaded = IMG_Load(path);
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

void teamplayDrawCrosshairs(int width, int height)
{
    float aimX[CROSSHAIR_GUNS], aimY[CROSSHAIR_GUNS];
    int shown[CROSSHAIR_GUNS], any = 0;
    GLint activeTexture = GL_TEXTURE0, program = 0, drawFbo = 0;

    if (!isTeamplayGame() || !getConfig()->enableCrosshairs)
        return;
    for (int g = 0; g < CROSSHAIR_GUNS; g++)
        any |= shown[g] = teamplayGunAim(g, &aimX[g], &aimY[g]);
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
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glad_glActiveTexture(GL_TEXTURE0);
    }
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
    glad_glViewport(0, 0, width, height);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_FOG);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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
