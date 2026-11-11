#define GL_GLEXT_PROTOTYPES
#include <glad/gl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3_image/SDL_image.h>

#include "crossHair.h"
#include "border.h"
#include "../config/config.h"
#include "../patching/patchResolution.h"
#include "blitStretching.h"

#ifdef __linux__
#include <pthread.h>
static pthread_t pollingThreadId = 0;
#else
#include "loader/elfLoader/pthread/pthreadEmu.hpp"
static uint32_t pollingThreadId = 0;
#endif

#define MAX_PLAYERS 2
#define INACTIVITY_TIMEOUT 3

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;

extern int phX, phY, phW, phH;
extern int phX2, phY2, phW2, phH2;

static Crosshair crossHair[MAX_PLAYERS];
static GLuint gShaderProgram = 0;
static GLint gUProjectionLoc = -1;
static GLint gUTextureLoc = -1;

#ifndef _WIN32
#define __stdcall
#endif

bool p1CrossHairInitialized = false;
bool p2CrossHairInitialized = false;
bool testMode = false;

int textureIdIdxAdjust = 0;

static const char *vertex_shader_source = "#version 120\n"
                                          "attribute vec2 a_pos;\n"
                                          "attribute vec2 a_tex_coord;\n"
                                          "varying vec2 v_tex_coord;\n"
                                          "uniform mat4 u_projection;\n"
                                          "void main() {\n"
                                          "    gl_Position = u_projection * vec4(a_pos.x, a_pos.y, 0.0, 1.0);\n"
                                          "    v_tex_coord = a_tex_coord;\n"
                                          "}\n";
static const char *fragment_shader_source = "#version 120\n"
                                            "varying vec2 v_tex_coord;\n"
                                            "uniform sampler2D u_texture;\n"
                                            "void main() {\n"
                                            "    gl_FragColor = texture2D(u_texture, v_tex_coord);\n"
                                            "}\n";

GLuint compileShader(GLenum type, const char *source)
{
    GLuint shader = glad_glCreateShader(type);
    glad_glShaderSource(shader, 1, &source, NULL);
    glad_glCompileShader(shader);
    GLint success;
    glad_glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glad_glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "ERROR: Shader compilation failed\n%s\n", infoLog);
        glad_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createShaderProgram()
{
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertex_shader_source);
    if (vert == 0)
        return 0;
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragment_shader_source);
    if (frag == 0)
    {
        glad_glDeleteShader(vert);
        return 0;
    }
    GLuint program = glad_glCreateProgram();
    glad_glAttachShader(program, vert);
    glad_glAttachShader(program, frag);
    glad_glBindAttribLocation(program, 0, "a_pos");
    glad_glBindAttribLocation(program, 1, "a_tex_coord");
    glad_glLinkProgram(program);
    GLint success;
    glad_glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glad_glGetProgramInfoLog(program, 512, NULL, infoLog);
        fprintf(stderr, "ERROR: Shader linking failed\n%s\n", infoLog);
    }
    glad_glDeleteShader(vert);
    glad_glDeleteShader(frag);
    return program;
}

void createCrosshairGeometry(Crosshair *ch)
{
    float w = (float)ch->width;
    float h = (float)ch->height;
    float vertices[] = {0.0f, h, 0.0f, 1.0f, w, h,    1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        w,    h, 1.0f, 1.0f, w, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    glad_glGenVertexArrays(1, &ch->vao);
    glad_glGenBuffers(1, &ch->vbo);
    glad_glBindVertexArray(ch->vao);
    glad_glBindBuffer(GL_ARRAY_BUFFER, ch->vbo);
    glad_glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glad_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glad_glEnableVertexAttribArray(0);
    glad_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glad_glEnableVertexAttribArray(1);
    glad_glBindBuffer(GL_ARRAY_BUFFER, 0);
    glad_glBindVertexArray(0);
}

void initCrossHairs()
{
    if (gShaderProgram == 0)
    {
        gShaderProgram = createShaderProgram();
        gUProjectionLoc = glad_glGetUniformLocation(gShaderProgram, "u_projection");
        gUTextureLoc = glad_glGetUniformLocation(gShaderProgram, "u_texture");
    }

    if (gShaderProgram == 0)
        return;

    if (loadCrosshairImage(0, getConfig()->p1CrossHairPath))
        p1CrossHairInitialized = true;

    if (loadCrosshairImage(1, getConfig()->p2CrossHairPath))
        p2CrossHairInitialized = true;

    if (isTestMode() || gGrp == GROUP_HOD4_TEST || gGrp == GROUP_HOD4_SP_TEST)
        testMode = true;

    if (!testMode && gId != PRIMEVAL_HUNT_SBPP)
        startPollingThread();

    textureIdIdxAdjust = (crossHair[0].texture != 0) + (crossHair[1].texture != 0);
}

int loadCrosshairImage(int player, const char *filepath)
{
    if (player < 0 || player >= MAX_PLAYERS)
        return 0;

    if (crossHair[player].surface)
    {
        SDL_DestroySurface(crossHair[player].surface);
        crossHair[player].surface = NULL;
    }
    if (crossHair[player].texture)
    {
        glad_glDeleteTextures(1, &crossHair[player].texture);
        crossHair[player].texture = 0;
    }
    if (crossHair[player].vao)
    {
        glad_glDeleteVertexArrays(1, &crossHair[player].vao);
        crossHair[player].vao = 0;
    }
    if (crossHair[player].vbo)
    {
        glad_glDeleteBuffers(1, &crossHair[player].vbo);
        crossHair[player].vbo = 0;
    }

    SDL_Surface *surface = IMG_Load(filepath);
    if (!surface)
    {
        fprintf(stderr, "Failed to load PNG for player %d: %s\n", player + 1, SDL_GetError());
        return 0;
    }
    crossHair[player].surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);

    crossHair[player].x = 0;
    crossHair[player].y = 0;
    crossHair[player].visible = false;
    crossHair[player].texture = 0;
    crossHair[player].vao = 0;
    crossHair[player].vbo = 0;

    /* Take the texture id now, not on first draw: textureIdIdxAdjust is counted once at init,
     * so the loader's ids have to be taken before the game creates its own. */
    GLuint tex = 0;
    glad_glGenTextures(1, &tex);
    glad_glBindTexture(GL_TEXTURE_2D, tex);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, crossHair[player].surface->w, crossHair[player].surface->h, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, crossHair[player].surface->pixels);

    SDL_DestroySurface(crossHair[player].surface);
    crossHair[player].surface = NULL;

    crossHair[player].width = getConfig()->customCrossHairWidth;
    crossHair[player].height = getConfig()->customCrossHairHeight;
    crossHair[player].texture = tex;

    /* Create VAO/VBO geometry — needed for shader-based rendering */
    createCrosshairGeometry(&crossHair[player]);

    return 1;
}

void updateCrosshairPosition(int player, float normX, float normY)
{
    if (player < 0 || player >= MAX_PLAYERS)
        return;

    crossHair[player].x = normX * renderWidth;
    crossHair[player].y = normY * renderHeight;
    if (testMode || gId == PRIMEVAL_HUNT_SBPP)
        crossHair[player].visible = true;
}

void renderCrosshairs(void)
{
    /* Save per-game viewport overrides before our save (these don't need restoring) */
    if (gId == PRIMEVAL_HUNT_SBPP)
        glad_glViewport(phX, phY, phW, phH);
    else if (gGrp == GROUP_HOD4_SP)
        glad_glViewport(0, 0, gWidth, gHeight);

    /* Clear any accumulated GL errors before we start */
    while (glad_glGetError() != GL_NO_ERROR)
        ;

    /* ---- Surgical save of game state ---- */
    GLint lastProgram, lastVao, lastVbo, lastActiveTex, lastTexBind, lastDepthFunc;
    GLboolean lastBlendEnabled, lastDepthEnabled;
    GLint lastBlendSrc, lastBlendDst;
    GLint lastViewport[4];
    GLint lastDrawFbo, lastReadFbo;
    GLboolean lastDepthMask;
    GLboolean lastScissorEnabled;
    GLboolean lastColorMask[4];
    GLfloat lastClearColor[4];
    GLint lastDrawBuffer = GL_BACK, lastReadBuffer = GL_BACK;

    glad_glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
    glad_glGetIntegerv(GL_DRAW_BUFFER, &lastDrawBuffer);
    glad_glGetIntegerv(GL_READ_BUFFER, &lastReadBuffer);
    glad_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVao);
    glad_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastVbo);
    glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &lastActiveTex);
    glad_glGetIntegerv(GL_VIEWPORT, lastViewport);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &lastDrawFbo);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &lastReadFbo);
    glad_glGetIntegerv(GL_DEPTH_FUNC, &lastDepthFunc);
    glad_glGetBooleanv(GL_DEPTH_WRITEMASK, &lastDepthMask);
    glad_glGetBooleanv(GL_COLOR_WRITEMASK, lastColorMask);
    glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, lastClearColor);

    /* Save texture bound to unit 0 */
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexBind);

    lastBlendEnabled = glad_glIsEnabled(GL_BLEND);
    lastDepthEnabled = glad_glIsEnabled(GL_DEPTH_TEST);
    lastScissorEnabled = glad_glIsEnabled(GL_SCISSOR_TEST);
    GLboolean lastStencilEnabled = glad_glIsEnabled(GL_STENCIL_TEST);
    GLboolean lastAlphaTestEnabled = glad_glIsEnabled(GL_ALPHA_TEST);
    GLboolean lastCullFaceEnabled = glad_glIsEnabled(GL_CULL_FACE);
    glad_glGetIntegerv(GL_BLEND_SRC_RGB, &lastBlendSrc);
    glad_glGetIntegerv(GL_BLEND_DST_RGB, &lastBlendDst);
    GLint lastBlendEq = GL_FUNC_ADD;
    glad_glGetIntegerv(GL_BLEND_EQUATION_RGB, &lastBlendEq);

    glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // glad_glViewport(0, 0, renderWidth, renderHeight);

    glad_glDisable(GL_DEPTH_TEST);
    glad_glDepthFunc(GL_ALWAYS);
    glad_glDepthMask(GL_FALSE);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glBlendEquation(GL_FUNC_ADD);
    glad_glUseProgram(gShaderProgram);
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glUniform1i(gUTextureLoc, 0);

    /* Force all color channels writable */
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* Disable scissor so it doesn't clip our rendering */
    glad_glDisable(GL_SCISSOR_TEST);

    /* Disable stencil and alpha test */
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);

    /* Force draw/read buffer to GL_BACK — game may have set GL_FRONT or GL_NONE */
    glad_glDrawBuffer(GL_BACK);
    glad_glReadBuffer(GL_BACK);

    if(gId == GHOST_SQUAD_EVOLUTION_SBNJ && getConfig()->borderEnabled)
        drawGameBorder(640, 480, getConfig()->whiteBorderPercentage, getConfig()->blackBorderPercentage);

    /* ---- Render crosshairs ---- */
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (!crossHair[i].visible)
            continue;

        if (crossHair[i].texture == 0 && crossHair[i].surface != NULL)
        {
            glad_glGenTextures(1, &crossHair[i].texture);
            glad_glBindTexture(GL_TEXTURE_2D, crossHair[i].texture);
            glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, crossHair[i].surface->w, crossHair[i].surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              crossHair[i].surface->pixels);

            crossHair[i].width = getConfig()->customCrossHairWidth;
            crossHair[i].height = getConfig()->customCrossHairHeight;
            createCrosshairGeometry(&crossHair[i]);

            SDL_DestroySurface(crossHair[i].surface);
            crossHair[i].surface = NULL;
        }

        if (crossHair[i].texture != 0 && crossHair[i].vao != 0)
        {
            float x = crossHair[i].x - (crossHair[i].width / 2.0f);
            float y = crossHair[i].y - (crossHair[i].height / 2.0f);
            float projection[16] = {0.0f};
            projection[0] = 2.0f / renderWidth;
            projection[5] = -2.0f / renderHeight;
            projection[10] = -1.0f;
            projection[12] = -1.0f + (x * projection[0]);
            projection[13] = 1.0f + (y * projection[5]);
            projection[15] = 1.0f;
            glad_glUniformMatrix4fv(gUProjectionLoc, 1, GL_FALSE, projection);

            glad_glBindTexture(GL_TEXTURE_2D, crossHair[i].texture);
            glad_glBindVertexArray(crossHair[i].vao);
            glad_glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    /* Clear any GL errors our rendering may have generated */
    while (glad_glGetError() != GL_NO_ERROR)
        ;

    /* ---- Restore game state ---- */
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)lastTexBind);
    glad_glBindVertexArray((GLuint)lastVao);
    glad_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)lastVbo);
    glad_glUseProgram((GLuint)lastProgram);
    glad_glActiveTexture((GLenum)lastActiveTex);
    glad_glBlendFunc((GLenum)lastBlendSrc, (GLenum)lastBlendDst);
    glad_glBlendEquation((GLenum)lastBlendEq);
    if (lastBlendEnabled)
        glad_glEnable(GL_BLEND);
    else
        glad_glDisable(GL_BLEND);
    if (lastDepthEnabled)
        glad_glEnable(GL_DEPTH_TEST);
    else
        glad_glDisable(GL_DEPTH_TEST);
    glad_glDepthFunc((GLenum)lastDepthFunc);
    glad_glDepthMask(lastDepthMask);
    if (lastScissorEnabled)
        glad_glEnable(GL_SCISSOR_TEST);
    else
        glad_glDisable(GL_SCISSOR_TEST);
    if (lastStencilEnabled)
        glad_glEnable(GL_STENCIL_TEST);
    else
        glad_glDisable(GL_STENCIL_TEST);
    if (lastAlphaTestEnabled)
        glad_glEnable(GL_ALPHA_TEST);
    else
        glad_glDisable(GL_ALPHA_TEST);
    if (lastCullFaceEnabled)
        glad_glEnable(GL_CULL_FACE);
    else
        glad_glDisable(GL_CULL_FACE);
    glad_glClearColor(lastClearColor[0], lastClearColor[1],
                      lastClearColor[2], lastClearColor[3]);
    glad_glColorMask(lastColorMask[0], lastColorMask[1],
                     lastColorMask[2], lastColorMask[3]);
    glad_glDrawBuffer((GLenum)lastDrawBuffer);
    glad_glReadBuffer((GLenum)lastReadBuffer);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)lastReadFbo);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)lastDrawFbo);
    glad_glViewport(lastViewport[0], lastViewport[1],
                    lastViewport[2], lastViewport[3]);

    /* Final error clear — restore calls may also generate errors */
    while (glad_glGetError() != GL_NO_ERROR)
        ;
}

void destroyCrosshairs(void)
{
    stopPollingThread();
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (crossHair[i].texture)
            glad_glDeleteTextures(1, &crossHair[i].texture);
        if (crossHair[i].vao)
            glad_glDeleteVertexArrays(1, &crossHair[i].vao);
        if (crossHair[i].vbo)
            glad_glDeleteBuffers(1, &crossHair[i].vbo);
        if (crossHair[i].surface)
            SDL_DestroySurface(crossHair[i].surface);
    }
    if (gShaderProgram)
        glad_glDeleteProgram(gShaderProgram);
}

typedef struct
{
    bool keepRunning;
} PollingArgs;

PollingArgs gPollingArgs;

static void *gsevoPollingThreadFunc(void *arg)
{
    PollingArgs *args = (PollingArgs *)arg;

    while (args->keepRunning)
    {
        uint8_t p1Mode = *(uint8_t *)0x086617E8;
        uint8_t p2Mode = *(uint8_t *)0x08661994;
        if (p1Mode == 0x2)
            crossHair[0].visible = true;
        else
            crossHair[0].visible = false;

        if (p2Mode == 0x2)
            crossHair[1].visible = true;
        else
            crossHair[1].visible = false;

        SDL_Delay(10);
    }
    return NULL;
}

// Player modes (CPlayer +0x38): 3 game, 4 hold (cutscenes), 5 select, 6 event.
// Rambo's scripted aiming (the bow shot at the helicopter) runs in event mode.
static bool playerModeShowsCrosshair(uint8_t mode)
{
    return mode == 3 || mode == 5 || (gGrp == GROUP_RAMBO && mode == 6);
}

static void *pollingThreadFunc(void *arg)
{
    PollingArgs *args = (PollingArgs *)arg;
    uint32_t *pGunMgr = NULL;
    uint32_t *pPlayerMgr = NULL;

    if (gId == THE_HOUSE_OF_THE_DEAD_4_SBLC_REVA)
    {
        pGunMgr = (uint32_t *)0x0a711758;
        pPlayerMgr = (uint32_t *)0x0a7117a8;
    }
    else if (gId == THE_HOUSE_OF_THE_DEAD_4_SBLC_REVB || gId == THE_HOUSE_OF_THE_DEAD_4_SBLC_REVC)
    {
        pGunMgr = (uint32_t *)0x0a6f27a8;
        pPlayerMgr = (uint32_t *)0x0a6f27f8;
    }
    else if (gId == RAMBO_SBQL)
        pPlayerMgr = (uint32_t *)0x0842fe9c;
    else if (gId == RAMBO_SBSS_CHINA)
        pPlayerMgr = (uint32_t *)0x084304fc;
    else if (gId == THE_HOUSE_OF_THE_DEAD_4_SPECIAL_SBLS_REVB)
        pPlayerMgr = (uint32_t *)0x0A69F92C;
    else
        args->keepRunning = false;

    while (args->keepRunning)
    {
        if (gGrp != GROUP_HOD4 || *pGunMgr != 0x0)
        {
            uint8_t *gameMode;
            if (gGrp == GROUP_HOD4)
            {
                uint32_t *gameModeAddress = *(void **)pGunMgr + 0x2c;
                gameMode = *(uint8_t **)gameModeAddress + 0x38;
            }
            if (gGrp != GROUP_HOD4 || *gameMode == 8)
            {
                if (*pPlayerMgr != 0x0)
                {
                    uint32_t *p1ModeAddress = *(void **)pPlayerMgr + 0x34;
                    uint32_t *p2ModeAddress = *(void **)pPlayerMgr + 0x38;

                    if (*p1ModeAddress != 0x0)
                    {
                        uint8_t *p1Mode = *(uint8_t **)p1ModeAddress + 0x38;
                        crossHair[0].visible = playerModeShowsCrosshair(*p1Mode);
                    }

                    if (*p2ModeAddress != 0x0)
                    {
                        uint8_t *p2Mode = *(uint8_t **)p2ModeAddress + 0x38;
                        crossHair[1].visible = playerModeShowsCrosshair(*p2Mode);
                    }
                }
            }
        }
        SDL_Delay(10);
    }
    return NULL;
}

void startPollingThread()
{
    if (gPollingArgs.keepRunning)
        return;

    gPollingArgs.keepRunning = true;
    if (gId == GHOST_SQUAD_EVOLUTION_SBNJ)
    {
#ifdef __linux__
        pthread_create(&pollingThreadId, NULL, gsevoPollingThreadFunc, &gPollingArgs);
#else
        emuPthreadCreate(&pollingThreadId, NULL, gsevoPollingThreadFunc, &gPollingArgs);
#endif
    }
    else
    {
#ifdef __linux__
        pthread_create(&pollingThreadId, NULL, pollingThreadFunc, &gPollingArgs);
#else
        emuPthreadCreate(&pollingThreadId, NULL, pollingThreadFunc, &gPollingArgs);
#endif
    }
}

void stopPollingThread()
{
    if (gPollingArgs.keepRunning)
    {
        gPollingArgs.keepRunning = false;
        if (pollingThreadId)
        {
#ifdef __linux__
            pthread_join(pollingThreadId, NULL);
#else
            emuPthreadJoin(pollingThreadId, NULL);
#endif
            pollingThreadId = 0;
        }
    }
}
