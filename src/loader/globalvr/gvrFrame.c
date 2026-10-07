// The picture of a Global VR game drawing at a fixed size (frameWidth x
// frameHeight) in the loader's window, which is [Display]'s size: Puck Off
// sets a 640x480 viewport once, as on its cabinet, whose X server ran at
// the monitor's resolution, and its frame was left in the window's bottom
// left corner. The window scaling (graphics/frameScale.h) draws it into an
// offscreen frame and fits that to the window at each swap, letterboxed
// with KEEP_ASPECT_RATIO.
//
// The game's own GL imports the scaling redirects go to its wrappers, and
// its glXSwapBuffers to gvrSwapBuffers. Hooked before the stack-aligning
// stubs (namcoEs1AlignImports) are put in front of the imports: the game's
// calls reach these through them, on an aligned stack.

#include <GL/glx.h>
#include <SDL3/SDL_video.h>
#include <stdio.h>

#include "gvr.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../graphics/sdlCalls.h"
#include "../log/log.h"

static void gvrSwapBuffers(Display *display, GLXDrawable drawable)
{
    EmulatorConfig *config = getConfig();
    int width = 0, height = 0;

    if (getSDLWindow() && SDL_GetWindowSizeInPixels(getSDLWindow(), &width, &height) && width > 0 && height > 0)
        frameScalePresent(0, 0, width, height, config->borderEnabled, config->whiteBorderPercentage,
                          config->blackBorderPercentage);
    // The loader's own, the Lindbergh path's swap.
    glXSwapBuffers(display, drawable);
}

void gvrFrameInstall(const GvrGame *game)
{
    static const char *const redirected[] = {"glViewport", "glDrawBuffer", "glReadBuffer", "glGetIntegerv"};

    frameScaleInit((void *(*)(const char *))glXGetProcAddressARB, getConfig()->keepAspectRatio);
    frameScaleSetFrame(game->frameWidth, game->frameHeight, NULL);
    for (size_t i = 0; i < sizeof(redirected) / sizeof(redirected[0]); i++)
    {
        void *wrapper = frameScaleWrapper(redirected[i]);
        if (wrapper)
            hookExecutableImport(redirected[i], wrapper);
    }
    if (!hookExecutableImport("glXSwapBuffers", (void *)gvrSwapBuffers))
        log_warn("Global VR: no glXSwapBuffers import, the picture is not scaled to the window");
    printf("Global VR: the game drawn at %dx%d, fitted to the window\n", game->frameWidth, game->frameHeight);
}
