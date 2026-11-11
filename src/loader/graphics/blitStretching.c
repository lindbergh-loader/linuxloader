#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "blitStretching.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../patching/patchResolution.h"
#include "sdlCalls.h"
// #include "glInitFunctions.h"

#define CHECK_GL(msg)                                                                                                                      \
    do                                                                                                                                     \
    {                                                                                                                                      \
        GLenum err = glad_glGetError();                                                                                                    \
        if (err != GL_NO_ERROR)                                                                                                            \
        {                                                                                                                                  \
            log_error("OpenGL Error in " msg ": 0x%04X", err);                                                                             \
        }                                                                                                                                  \
    } while (0)

// #ifdef _WIN32
// #include "../elfLoader/glhooks.hpp"
// #endif

extern uint32_t gId;
extern int gGrp;
extern int gWidth;
extern int gHeight;
extern SDL_Window *g_SdlWindow;

int blitWidth = 0;
int blitHeight = 0;
int renderWidth = 0;
int renderHeight = 0;

int gameIsOutrunChihiroMode = 0;

int fboInitialized = false;
GLuint fboId = 0;
GLuint fboTextureId = 0;

int drawableW = 1;
int drawableH = 1;

Dest dest;

/*
 * The core FBO calls are used by default. NVIDIA 341.92 (9600GT) rejects glBindFramebuffer on our
 * FBO with GL_INVALID_OPERATION every frame in After Burner Climax, which drives its own render
 * targets through EXT_framebuffer_object. When the core bind fails we rebuild the FBO with the EXT
 * calls and stay on them.
 */
static int fboUseExt = 0;
static int fboExtTried = 0;

static void fboBind(GLenum target, GLuint framebuffer)
{
    if (fboUseExt)
        glad_glBindFramebufferEXT(target, framebuffer);
    else
        glad_glBindFramebuffer(target, framebuffer);
}

static void fboBlit(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                    GLenum filter)
{
    if (fboUseExt)
        glad_glBlitFramebufferEXT(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, filter);
    else
        glad_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, filter);
}

static int fboIsFramebuffer(GLuint framebuffer)
{
    if (fboUseExt)
        return glad_glIsFramebufferEXT ? glad_glIsFramebufferEXT(framebuffer) : 1;
    return glad_glIsFramebuffer ? glad_glIsFramebuffer(framebuffer) : 1;
}

static GLuint fboCreate()
{
    GLuint id = 0;
    GLenum status;

    if (fboUseExt)
    {
        glad_glGenFramebuffersEXT(1, &id);
        if (id == 0)
            return 0;
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, id);
        glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTextureId, 0);
        status = glad_glCheckFramebufferStatusEXT(GL_FRAMEBUFFER);
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
    }
    else
    {
        glad_glGenFramebuffers(1, &id);
        if (id == 0)
            return 0;
        glad_glBindFramebuffer(GL_FRAMEBUFFER, id);
        glad_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTextureId, 0);
        status = glad_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (status != GL_FRAMEBUFFER_COMPLETE)
        log_error("FBO %u is not complete! Status: 0x%x\n", id, status);

    log_info("blitStretch: created fboId %u with the %s calls, context %p (main %p)", id, fboUseExt ? "EXT" : "core",
             SDL_GL_GetCurrentContext(), getSDLContext());
    return id;
}

/*
 * Binds fboId to GL_FRAMEBUFFER. The first time the core bind fails with GL_INVALID_OPERATION it logs
 * what the driver thinks of the FBO, then switches to the EXT calls and retries. The old name is not
 * deleted, it may be one the game's EXT FBOs answer to as well.
 */
static void fboBindChecked(const char *where)
{
    fboBind(GL_FRAMEBUFFER, fboId);
    GLenum err = glad_glGetError();

    if (err == GL_INVALID_OPERATION && !fboUseExt && !fboExtTried)
    {
        fboExtTried = 1;

        GLint boundFbo = 0;
        glad_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFbo);
        log_warn("blitStretch: glBindFramebuffer(GL_FRAMEBUFFER, %u) failed with 0x%04X in %s, glIsFramebuffer=%d, bound FBO %d, "
                 "context %p (main %p)",
                 fboId, err, where, fboIsFramebuffer(fboId), boundFbo, SDL_GL_GetCurrentContext(), getSDLContext());

        if (!glad_glGenFramebuffersEXT || !glad_glBindFramebufferEXT || !glad_glFramebufferTexture2DEXT ||
            !glad_glCheckFramebufferStatusEXT || !glad_glBlitFramebufferEXT)
        {
            log_warn("blitStretch: EXT framebuffer calls not available (EXT_framebuffer_object %d, EXT_framebuffer_blit %d), "
                     "staying on the core calls",
                     GLAD_GL_EXT_framebuffer_object, GLAD_GL_EXT_framebuffer_blit);
            return;
        }

        fboUseExt = 1;
        fboId = fboCreate();
        fboBind(GL_FRAMEBUFFER, fboId);
        err = glad_glGetError();
        log_warn("blitStretch: switched to the EXT framebuffer calls, fboId %u, bind result 0x%04X", fboId, err);
    }

    if (err != GL_NO_ERROR)
        log_error("OpenGL Error in bind GL_FRAMEBUFFER fboId %s: 0x%04X", where, err);
}

void initBlitting()
{
    blitSetWidthandHeightSize();
    blitInitializeFbo();
}

/*
 * Outrun at 640x480 (Chihiro mode) renders 800x480 and shows the centre 640. The window is only
 * 640 wide, so when the game binds the default framebuffer it gets our 800x480 FBO instead and
 * blitStretch() crops from there. Called from the glBindFramebufferEXT wrappers; returns 1 when the
 * bind was redirected.
 */
int blitRedirectDefaultFramebuffer(unsigned int target, unsigned int framebuffer)
{
    if (!gameIsOutrunChihiroMode || target != GL_FRAMEBUFFER || framebuffer != 0 || !fboInitialized || fboId == 0)
        return 0;

    fboBind(GL_FRAMEBUFFER, fboId);
    glad_glViewport(0, 0, 800, 480);
    glad_glClear(GL_COLOR_BUFFER_BIT);
    return 1;
}

void blitSetWidthandHeightSize()
{
    if (gGrp == GROUP_ID4_EXP || gGrp == GROUP_ID4_JAP || gGrp == GROUP_ID5)
    {
        if (isTestMode())
        {
            blitWidth = 1360;
            blitHeight = 768;
        }
        else
        {
            blitWidth = gWidth;
            blitHeight = gHeight;
        }
    }
    else if (gGrp == GROUP_VT3_TEST)
    {
        blitWidth = 640;
        blitHeight = 480;
    }
    else if (gId == QUIZ_AXA_SBMS || gId == QUIZ_AXA_SBUR_LIVE || gId == MJ4_SBPN_REVG || gId == MJ4_EVO_SBTA)
    {
        blitWidth = 1024;
        blitHeight = 768;
    }
    else
    {
        blitWidth = gWidth;
        blitHeight = gHeight;
    }

    renderWidth = blitWidth;
    renderHeight = blitHeight;
}

int blitInitializeFbo()
{
    fboId = 0; // Will be lazily initialized in blitStretch on the correct context

    glad_glGenTextures(1, &fboTextureId);
    if (fboTextureId == 0)
    {
        log_error("Failed to generate FBO texture.\n");
        return 0;
    }

    GLint maxTexSize = 0;
    glad_glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    if (blitWidth > maxTexSize || blitHeight > maxTexSize)
    {
        log_error("FBO texture size %dx%d exceeds GL_MAX_TEXTURE_SIZE %d! SSAA may be silently clamped.",
                  blitWidth, blitHeight, maxTexSize);
    }

    glad_glBindTexture(GL_TEXTURE_2D, fboTextureId);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, blitWidth, blitHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glad_glBindTexture(GL_TEXTURE_2D, 0);

    fboInitialized = true;
    return 1;
}
/*
 * Fixes Primeval Hunt aim if the window is resize
 */
void blitWindowToRender(float windowX, float windowY, float *renderX, float *renderY)
{
    float x = windowX;
    float y = windowY;

    if (dest.W > 0 && dest.H > 0 && renderWidth > 0 && renderHeight > 0)
    {
        x = (windowX - (float)dest.X) * (float)renderWidth / (float)dest.W;
        y = (windowY - (float)dest.Y) * (float)renderHeight / (float)dest.H;
    }

    if (renderX != NULL)
        *renderX = x;
    if (renderY != NULL)
        *renderY = y;
}

/*
 * Inverse of blitWindowToRender: takes a point in the game's render space and returns
 * where it lands on screen once blitStretch has scaled and letterboxed the frame.
 */
void blitRenderToWindow(float renderX, float renderY, float *windowX, float *windowY)
{
    float x = renderX;
    float y = renderY;

    if (dest.W > 0 && dest.H > 0 && renderWidth > 0 && renderHeight > 0)
    {
        x = (float)dest.X + renderX * (float)dest.W / (float)renderWidth;
        y = (float)dest.Y + renderY * (float)dest.H / (float)renderHeight;
    }

    if (windowX != NULL)
        *windowX = x;
    if (windowY != NULL)
        *windowY = y;
}

void blitStretch()
{
    if (!fboInitialized)
        return;

    // Lazily initialize FBO on the correct thread/context
    if (fboId == 0 && fboTextureId > 0)
        fboId = fboCreate();

    if (fboInitialized && fboId > 0 && fboTextureId > 0 && g_SdlWindow)
    {
        while (glad_glGetError() != GL_NO_ERROR)
            ; // clear previous errors

        static int firstTime = 1;
        if (firstTime)
        {
            // For Chihiro mode, don't resize window (blitRedirectDefaultFramebuffer redirects FB0 to the FBO)
            if (!gameIsOutrunChihiroMode)
                SDL_SetWindowSize(g_SdlWindow, gWidth, gHeight);
            firstTime = 0;
        }
        SDL_GetWindowSizeInPixels(g_SdlWindow, &drawableW, &drawableH);

        // SAVE STATE
        GLint oldScissorTest = 0;
        GLint oldScissorBox[4];
        GLboolean oldColorMask[4];
        GLint oldDrawFbo = 0, oldReadFbo = 0;
        GLfloat oldClearColor[4];
        GLint oldReadBuffer = GL_BACK, oldDrawBuffer = GL_BACK;

        glad_glGetIntegerv(GL_SCISSOR_TEST, &oldScissorTest);
        CHECK_GL("glGetIntegerv GL_SCISSOR_TEST");
        glad_glGetIntegerv(GL_SCISSOR_BOX, oldScissorBox);
        CHECK_GL("glGetIntegerv GL_SCISSOR_BOX");
        glad_glGetBooleanv(GL_COLOR_WRITEMASK, oldColorMask);
        CHECK_GL("glGetBooleanv GL_COLOR_WRITEMASK");
        glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFbo);
        CHECK_GL("glGetIntegerv GL_DRAW_FRAMEBUFFER_BINDING");
        glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFbo);
        CHECK_GL("glGetIntegerv GL_READ_FRAMEBUFFER_BINDING");
        glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClearColor);
        CHECK_GL("glGetFloatv GL_COLOR_CLEAR_VALUE");

        // FORCE STATE FOR FULLSCREEN BLITS
        glad_glDisable(GL_SCISSOR_TEST);
        CHECK_GL("glDisable GL_SCISSOR_TEST");
        glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        CHECK_GL("glColorMask");

        if (!fboIsFramebuffer(fboId))
        {
            log_error("fboId %u is NOT a valid framebuffer! Context changed?", fboId);
            fboId = fboCreate();
        }

        // First blit: copy from framebuffer to FBO
        // In Chihiro mode the game rendered to our FBO (blitRedirectDefaultFramebuffer), so skip
        if (!gameIsOutrunChihiroMode)
        {
                // Non-MSAA path: direct blit from FB0 → fboId
                fboBindChecked("first blit");
                fboBind(GL_READ_FRAMEBUFFER, 0);
                CHECK_GL("bind GL_READ_FRAMEBUFFER 0 first blit");

                glad_glReadBuffer(GL_BACK);
                CHECK_GL("readbuffer GL_BACK");

                // Source: read from FB0 at game render resolution (pre-SSAA).
                // Destination: write to FBO at SSAA resolution (upscale with GL_LINEAR).
                // When SSAA is off, renderWidth==blitWidth and this is a 1:1 copy.
                fboBlit(0, 0, renderWidth, renderHeight, 0, 0, blitWidth, blitHeight, GL_LINEAR);
                CHECK_GL("first blit to fboId");
        }

        float gameAspect;
        if (gameIsOutrunChihiroMode)
            gameAspect = 640.0f / 480.0f; // post-crop aspect (center 640px of 800-wide game)
        else
            gameAspect = (float)blitWidth / (float)blitHeight;
        float windowAspect = (float)drawableW / (float)drawableH;

        // Reset dest each frame to prevent stale offsets after window resize / fullscreen toggle
        dest.X = 0;
        dest.Y = 0;
        dest.W = drawableW;
        dest.H = drawableH;

        if (windowAspect > gameAspect)
        {
            dest.W = (GLsizei)(drawableH * gameAspect);
            dest.X = (drawableW - dest.W) / 2;
        }
        else if (windowAspect < gameAspect)
        {
            dest.H = (GLsizei)(drawableW / gameAspect);
            dest.Y = (drawableH - dest.H) / 2;
        }

        fboBind(GL_FRAMEBUFFER, 0);
        CHECK_GL("bind default framebuffer");

        glad_glDrawBuffer(GL_BACK); // Default FBO usually draws to GL_BACK
        CHECK_GL("drawbuffer GL_BACK");

        glad_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glad_glClear(GL_COLOR_BUFFER_BIT);
        CHECK_GL("glClear on default framebuffer");

            // Non-MSAA path: direct scaled blit fboId → FB 0
            fboBindChecked("second blit");
            fboBind(GL_DRAW_FRAMEBUFFER, 0); // We want to draw to 0
            CHECK_GL("bind DRAW 0 second blit");

            GLenum scalingFilter = getConfig()->scalingFilterNearest ? GL_NEAREST : GL_LINEAR;
            if(gameIsOutrunChihiroMode)
            {
                // Crop center 640px from 800-wide FBO: remove 80px left, 80px right
                fboBlit(80, 0, 720, blitHeight, dest.X, dest.Y, dest.X + dest.W, dest.Y + dest.H, scalingFilter);
            }
            else
            {
                fboBlit(0, 0, blitWidth, blitHeight, dest.X, dest.Y, dest.X + dest.W, dest.Y + dest.H, scalingFilter);
            }
            CHECK_GL("second blit (scaled) to default framebuffer");

        // RESTORE STATE
        if (oldScissorTest)
            glad_glEnable(GL_SCISSOR_TEST);
        glad_glScissor(oldScissorBox[0], oldScissorBox[1], oldScissorBox[2], oldScissorBox[3]);
        glad_glColorMask(oldColorMask[0], oldColorMask[1], oldColorMask[2], oldColorMask[3]);

        fboBind(GL_READ_FRAMEBUFFER, oldReadFbo);
        if (oldReadFbo == 0)
        {
            glad_glReadBuffer(oldReadBuffer);
        } // Can only restore safely if it's default

        fboBind(GL_DRAW_FRAMEBUFFER, oldDrawFbo);
        if (oldDrawFbo == 0)
        {
            glad_glDrawBuffer(oldDrawBuffer);
        }

        glad_glClearColor(oldClearColor[0], oldClearColor[1], oldClearColor[2], oldClearColor[3]);
    }
}