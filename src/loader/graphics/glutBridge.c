#include "glad/gl.h"
#define GL_GLEXT_PROTOTYPES
#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#ifdef __linux__
#include <GL/freeglut.h>
#endif
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "blitStretching.h"
#include "border.h"
#include "../config/config.h"
#include "crossHair.h"
#include "fpsLimiter.h"
#include "overlayMsgs.h"
#include "../graphics/sdlCalls.h"
#include "../hardware/lindbergh/touchScreen.h"
#ifdef __linux__
#include "../teamplay/teamplay.h"
#endif

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;
extern SDL_Window *g_SdlWindow;
extern char current_text[256];
extern double localFps;
bool winHidden = true;

static void *g_displayFunc = NULL;
static void *reshapeFunc = NULL;
static void *g_visibilityFunc = NULL;
static void *g_idleFunc = NULL;
static bool g_redisplayNeeded = true;

extern int drawableW;
extern int drawableH;
extern int blitWidth;
extern int gameIsOutrunChihiroMode;
extern SDL_Renderer *fontRenderer;
extern TTF_Font *font;

void bridgeGlutInit(int *argcp, char **argv)
{
    // void (*_glutInit)(int *argcp, char **argv) = dlsym(RTLD_NEXT, "glutInit");
    if (getConfig()->gameGroup == GROUP_OUTRUN_TEST)
        startSDL();
}

void bridgeGlutMainLoop(void)
{

    bool quit = false;
    while (!quit)
    {
        pollEvents();
        int w, h;
        SDL_GetWindowSize(g_SdlWindow, &w, &h);
        if (g_displayFunc && g_redisplayNeeded)
        {
            (((void *(*)(void))g_displayFunc)());
            g_redisplayNeeded = false;
        }
        if (reshapeFunc)
            (((void *(*)(int, int))reshapeFunc)(w, h));
        if (g_visibilityFunc)
            (((void *(*)(int))g_visibilityFunc)(1));
        if (g_idleFunc)
            (((void *(*)(void))g_idleFunc)());
        SDL_Delay(1);
    }
    SDL_DestroyWindow(g_SdlWindow);
    SDL_Quit();
}

void bridgeGlutMainLoopEvent(void)
{
    pollEvents();
    return;
}

void bridgeGlutSwapBuffers(void)
{
    EmulatorConfig *config = getConfig();

    if (gGrp == GROUP_OUTRUN || gGrp == GROUP_OUTRUN_TEST)
        pollEvents();

    if ((p1CrossHairInitialized || p2CrossHairInitialized) || (gId == GHOST_SQUAD_EVOLUTION_SBNJ && config->borderEnabled))
        renderCrosshairs();

#ifdef __linux__
    // Into the Teamplay games' frame, before it is scaled.
    teamplayDrawCrosshairs(blitWidth, blitHeight);
#endif

    blitStretch();

    if (config->borderEnabled && gId != GHOST_SQUAD_EVOLUTION_SBNJ)
        drawGameBorder(drawableW, drawableH, config->whiteBorderPercentage, config->blackBorderPercentage);

    overlayInit();
    overlayRender();

    SDL_GL_SwapWindow(g_SdlWindow);

    if (config->fpsLimiter)
        frameTiming();

    double fps_val = calculateFps();
    static char windowTitle[256] = {0};
    sprintf(windowTitle, "%s - FPS: %.2f", getGameName(), fps_val);
    SDL_SetWindowTitle(g_SdlWindow, windowTitle);
    fpsOverlayUpdateFps(fps_val);

    if (gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA)
        mj4TouchHolding();
}

int bridgeGlutEnterGameMode(void)
{
    if (!g_SdlWindow)
        startSDL();
    return 0;
}

void bridgeGlutFullScreen()
{
}

int bridgeGlutCreateWindow(const char *title)
{
    if (!g_SdlWindow)
        startSDL();
    return 1;
}

int bridgeGlutGet(GLenum type)
{
    if (type == 0x66)
        return gWidth;
    if (type == 0x67)
        return gHeight;

    return 0;
}

int bridgeGlutExtensionSupported(const char *extension)
{
    return SDL_GL_ExtensionSupported(extension);
}

void bridgeGlutSetCursor(int glutCursor)
{
    if (strcmp(getConfig()->customCursor, "") == 0 && strcmp(getConfig()->touchCursor, "") == 0)
    {
        SDL_Cursor *cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        SDL_SetCursor(cursor);
    }
}

void bridgeGlutInitWindowSize(int width, int height)
{
}

void bridgeGlutInitWindowPosition(int x, int y)
{
}

void bridgeGlutDisplayFunc(void (*callback)(void))
{
    g_displayFunc = callback;
}

void bridgeGlutReshapeFunc(void (*callback)(int, int))
{
    // Star Trek: Voyager (and its service mode) sets its viewport and
    // projection there: once, at its monitor's size (the frame scaled), which
    // it also gives the gun board (240 or 384 lines).
    if (gId == STAR_TREK_VOYAGER_TEAMPLAY || gId == STAR_TREK_VOYAGER_SERVICE_TEAMPLAY)
        callback(blitWidth, blitHeight);
}

void bridgeGlutVisibilityFunc(void (*callback)(int))
{
    g_visibilityFunc = callback;
}

void bridgeGlutIdleFunc(void (*callback)(void))
{
    g_idleFunc = callback;
}

void bridgeGlutInitDisplayMode(unsigned int mode)
{
}

void bridgeGlutGameModeString(const char *string)
{
}

void bridgeGlutJoystickFunc(void (*callback)(unsigned int, int, int, int), int pollInterval)
{
}

void bridgeGlutPostRedisplay(void)
{
    g_redisplayNeeded = true;
}

void bridgeGlutKeyboardFunc(void (*callback)(unsigned char, int, int))
{
}

void bridgeGlutKeyboardUpFunc(void (*callback)(unsigned char, int, int))
{
}

void bridgeGlutMouseFunc(void (*callback)(int, int, int, int))
{
}

void bridgeGlutMotionFunc(void (*callback)(int, int))
{
}

void bridgeGlutSpecialFunc(void (*callback)(int, int, int))
{
}

void bridgeGlutSpecialUpFunc(void (*callback)(int, int, int))
{
}

void bridgeGlutPassiveMotionFunc(void (*callback)(int, int))
{
}

void bridgeGlutEntryFunc(void (*callback)(int))
{
}

void bridgeGlutLeaveGameMode()
{
}

void bridgeGlutSolidTeapot(double size)
{
}

void bridgeGlutWireTeapot(double size)
{
}

void bridgeGlutSolidSphere(double radius, GLint slices, GLint stacks)
{
}

void bridgeGlutWireSphere(double radius, GLint slices, GLint stacks)
{
}

void bridgeGlutWireCone(double base, double height, GLint slices, GLint stacks)
{
}

void bridgeGlutSolidCone(double base, double height, GLint slices, GLint stacks)
{
}

void bridgeGlutWireCube(double dSize)
{
}

void bridgeGlutSolidCube(double dSize)
{
}

int bridgeGlutBitmapWidth(void *fontID, int character)
{
    return 9;
}

void convertSurfaceTo1Bit(SDL_Surface *surface, uint8_t *outBitmap, int pitch)
{
    SDL_LockSurface(surface);
    for (int y = 0; y < surface->h; ++y)
    {
        uint8_t byte = 0;
        int bit = 0;
        int sdlY = surface->h - 1 - y;

        Uint8 *row = (Uint8 *)surface->pixels + sdlY * surface->pitch;

        for (int x = 0; x < surface->w; ++x)
        {
            if (row[x])
                byte |= (1 << (7 - bit));

            bit++;
            if (bit == 8 || x == surface->w - 1)
            {
                outBitmap[y * pitch + x / 8] = byte;
                byte = 0;
                bit = 0;
            }
        }
    }
    SDL_UnlockSurface(surface);
}

void bridgeGlutBitmapCharacter(void *fontID, int character)
{
    static int penX = 0;
    GLint glColor[4];
    glGetIntegerv(GL_CURRENT_COLOR, glColor);

    SDL_Color sdlColor;
    sdlColor.r = (Uint8)(glColor[0] * 255);
    sdlColor.g = (Uint8)(glColor[1] * 255);
    sdlColor.b = (Uint8)(glColor[2] * 255);
    sdlColor.a = (Uint8)(glColor[3] * 255);

    GLint glPos[4];
    glGetIntegerv(GL_CURRENT_RASTER_POSITION, glPos);

    if (penX != glPos[0])
        penX = glPos[0];

    SDL_Surface *glyph = TTF_RenderGlyph_Solid(font, character, sdlColor);
    if (!glyph)
    {
        printf("Failed to render glyph: %s\n", SDL_GetError());
        return;
    }

    int pitch = (glyph->w + 7) / 8;
    uint8_t *bitmap = (uint8_t *)malloc(pitch * glyph->h);
    if (!bitmap)
    {
        printf("Failed to allocate memory for bitmap\n");
        SDL_DestroySurface(glyph);
        return;
    }
    convertSurfaceTo1Bit(glyph, bitmap, pitch);

    GLint swbytes, lsbfirst, rowlen, skiprows, skippix, align;

    glGetIntegerv(GL_UNPACK_SWAP_BYTES, &swbytes);
    glGetIntegerv(GL_UNPACK_LSB_FIRST, &lsbfirst);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlen);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skiprows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skippix);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBitmap(glyph->w, glyph->h, 0, 0, (float)(glyph->w), 0.0, bitmap);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION, glPos);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, swbytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, lsbfirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlen);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skiprows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skippix);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);

    free(bitmap);
    SDL_DestroySurface(glyph);
}

#ifdef __cplusplus
extern "C"
{
#endif

    void glutInit(int *argcp, char **argv)
    {
        bridgeGlutInit(argcp, argv);
    }
    void glutMainLoop(void)
    {
        bridgeGlutMainLoop();
    }
    void glutMainLoopEvent(void)
    {
        bridgeGlutMainLoopEvent();
    }
    void glutSwapBuffers(void)
    {
        bridgeGlutSwapBuffers();
    }
    int glutEnterGameMode(void)
    {
        return bridgeGlutEnterGameMode();
    }
    void glutFullScreen(void)
    {
        bridgeGlutFullScreen();
    }
    int glutCreateWindow(const char *title)
    {
        return bridgeGlutCreateWindow(title);
    }
    int glutGet(GLenum type)
    {
        return bridgeGlutGet(type);
    }
    int glutExtensionSupported(const char *extension)
    {
        return bridgeGlutExtensionSupported(extension);
    }
    void glutSetCursor(int glutCursor)
    {
        bridgeGlutSetCursor(glutCursor);
    }
    void glutInitWindowSize(int width, int height)
    {
        bridgeGlutInitWindowSize(width, height);
    }
    void glutInitWindowPosition(int x, int y)
    {
        bridgeGlutInitWindowPosition(x, y);
    }
    void glutDisplayFunc(void (*callback)(void))
    {
        bridgeGlutDisplayFunc(callback);
    }
    void glutReshapeFunc(void (*callback)(int, int))
    {
        bridgeGlutReshapeFunc(callback);
    }
    void glutVisibilityFunc(void (*callback)(int))
    {
        bridgeGlutVisibilityFunc(callback);
    }
    void glutIdleFunc(void (*callback)(void))
    {
        bridgeGlutIdleFunc(callback);
    }
    void glutInitDisplayMode(unsigned int mode)
    {
        bridgeGlutInitDisplayMode(mode);
    }
    void glutGameModeString(const char *string)
    {
        bridgeGlutGameModeString(string);
    }
    void glutJoystickFunc(void (*callback)(unsigned int, int, int, int), int pollInterval)
    {
        bridgeGlutJoystickFunc(callback, pollInterval);
    }
    void glutPostRedisplay(void)
    {
        bridgeGlutPostRedisplay();
    }
    void glutKeyboardFunc(void (*callback)(unsigned char, int, int))
    {
        bridgeGlutKeyboardFunc(callback);
    }
    void glutKeyboardUpFunc(void (*callback)(unsigned char, int, int))
    {
        bridgeGlutKeyboardUpFunc(callback);
    }
    void glutMouseFunc(void (*callback)(int, int, int, int))
    {
        bridgeGlutMouseFunc(callback);
    }
    void glutMotionFunc(void (*callback)(int, int))
    {
        bridgeGlutMotionFunc(callback);
    }
    void glutSpecialFunc(void (*callback)(int, int, int))
    {
        bridgeGlutSpecialFunc(callback);
    }
    void glutSpecialUpFunc(void (*callback)(int, int, int))
    {
        bridgeGlutSpecialUpFunc(callback);
    }
    void glutPassiveMotionFunc(void (*callback)(int, int))
    {
        bridgeGlutPassiveMotionFunc(callback);
    }
    void glutEntryFunc(void (*callback)(int))
    {
        bridgeGlutEntryFunc(callback);
    }
    void glutLeaveGameMode()
    {
        bridgeGlutLeaveGameMode();
    }
    void glutSolidTeapot(double size)
    {
        bridgeGlutSolidTeapot(size);
    }
    void glutWireTeapot(double size)
    {
        bridgeGlutWireTeapot(size);
    }
    void glutSolidSphere(double radius, GLint slices, GLint stacks)
    {
        bridgeGlutSolidSphere(radius, slices, stacks);
    }
    void glutWireSphere(double radius, GLint slices, GLint stacks)
    {
        bridgeGlutWireSphere(radius, slices, stacks);
    }
    void glutWireCone(double base, double height, GLint slices, GLint stacks)
    {
        bridgeGlutWireCone(base, height, slices, stacks);
    }
    void glutSolidCone(double base, double height, GLint slices, GLint stacks)
    {
        bridgeGlutSolidCone(base, height, slices, stacks);
    }
    void glutWireCube(double dSize)
    {
        bridgeGlutWireCube(dSize);
    }
    void glutSolidCube(double dSize)
    {
        bridgeGlutSolidCube(dSize);
    }
    int glutBitmapWidth(void *fontID, int character)
    {
        return bridgeGlutBitmapWidth(fontID, character);
    }
    void glutBitmapCharacter(void *fontID, int character)
    {
        bridgeGlutBitmapCharacter(fontID, character);
    }

#ifdef __cplusplus
}
#endif
