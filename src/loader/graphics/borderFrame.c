#include <glad/gl.h>

#include "border.h"
#include "borderFrame.h"

static struct
{
    int width, height;
    GLuint framebuffer, renderbuffer;
    int saved;
} clean;

void borderFrameBegin(int width, int height, int border, float whiteBorderPercentage, float blackBorderPercentage)
{
    clean.saved = 0;
    if (width <= 0 || height <= 0)
        return;

    // Drawn on the window, leaving the game's framebuffer and scissor state
    // as they were.
    GLint drawFramebuffer = 0, readFramebuffer = 0, readBuffer = GL_BACK, scissorBox[4];
    GLboolean scissor = glad_glIsEnabled(GL_SCISSOR_TEST);
    glad_glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    int blit = glad_glBindFramebuffer && glad_glBlitFramebuffer && glad_glGenRenderbuffers;
    if (blit)
    {
        glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glad_glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        if (clean.width != width || clean.height != height)
        {
            if (!clean.framebuffer)
            {
                glad_glGenFramebuffers(1, &clean.framebuffer);
                glad_glGenRenderbuffers(1, &clean.renderbuffer);
            }
            glad_glBindRenderbuffer(GL_RENDERBUFFER, clean.renderbuffer);
            glad_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, clean.framebuffer);
            glad_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                           clean.renderbuffer);
            clean.width = width;
            clean.height = height;
        }
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, clean.framebuffer);
        glad_glReadBuffer(GL_BACK);
        glad_glDisable(GL_SCISSOR_TEST);
        glad_glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        clean.saved = 1;
    }
    drawFrameOverlay(0, 0, width, height);
    if (border)
        drawGameBorder(width, height, whiteBorderPercentage, blackBorderPercentage);
    if (blit)
    {
        glad_glReadBuffer((GLenum)readBuffer);
        glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFramebuffer);
        glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFramebuffer);
    }
    glad_glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    if (scissor)
        glad_glEnable(GL_SCISSOR_TEST);
}

void borderFrameEnd(void)
{
    GLint drawFramebuffer, readFramebuffer;
    GLboolean scissor;

    if (!clean.saved)
        return;
    clean.saved = 0;
    scissor = glad_glIsEnabled(GL_SCISSOR_TEST);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, clean.framebuffer);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glBlitFramebuffer(0, 0, clean.width, clean.height, 0, 0, clean.width, clean.height, GL_COLOR_BUFFER_BIT,
                           GL_NEAREST);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFramebuffer);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFramebuffer);
    if (scissor)
        glad_glEnable(GL_SCISSOR_TEST);
}
