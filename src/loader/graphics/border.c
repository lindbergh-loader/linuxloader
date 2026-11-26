#include <glad/gl.h>

#include "border.h"

static void drawBorderWithOffsetAt(int x, int y, int width, int height, float borderPercentage, float offsetPercentage,
                                   GLfloat *color)
{
    // Border thickness based on the percentage of the width/height
    int borderWidth = (int)(width * borderPercentage); // Border width as a percentage of the screen width
    int borderHeight = borderWidth;

    // Offset based on the percentage of the width/height
    int offsetX = (int)(width * offsetPercentage); // Horizontal offset from the screen edge
    int offsetY = offsetX;

    // Set the clear color based on the color parameter
    glad_glClearColor(color[0], color[1], color[2], color[3]);

    // Left side (borderWidth wide from top to bottom, starting from the left edge with offset)
    glad_glScissor(x + offsetX, y + offsetY, borderWidth,
                   height - 2 * offsetY); // X = offsetX, Y = offsetY, Width = borderWidth, Height = (height - 2 * offsetY)
    glad_glClear(GL_COLOR_BUFFER_BIT);

    // Right side (borderWidth wide from top to bottom, starting from the right edge with offset)
    glad_glScissor(
        x + width - borderWidth - offsetX, y + offsetY, borderWidth,
        height - 2 * offsetY); // X = (width - borderWidth - offsetX), Y = offsetY, Width = borderWidth, Height = (height - 2 * offsetY)
    glad_glClear(GL_COLOR_BUFFER_BIT);

    // Top side (borderHeight wide from left to right, starting from the top edge with offset)
    glad_glScissor(x + offsetX, y + offsetY, width - 2 * offsetX,
                   borderHeight); // X = offsetX, Y = offsetY, Width = (width - 2 * offsetX), Height = borderHeight
    glad_glClear(GL_COLOR_BUFFER_BIT);

    // Bottom side (borderHeight wide from left to right, starting from the bottom edge with offset)
    glad_glScissor(
        x + offsetX, y + height - borderHeight - offsetY, width - 2 * offsetX,
        borderHeight); // X = offsetX, Y = (height - borderHeight - offsetY), Width = (width - 2 * offsetX), Height = borderHeight
    glad_glClear(GL_COLOR_BUFFER_BIT);
}

void drawBorderWithOffset(int width, int height, float borderPercentage, float offsetPercentage, GLfloat *color)
{
    drawBorderWithOffsetAt(0, 0, width, height, borderPercentage, offsetPercentage, color);
}

void drawGameBorder(int width, int height, float whiteBorderPercentage, float blackBorderPercentage)
{
    drawGameBorderAt(0, 0, width, height, whiteBorderPercentage, blackBorderPercentage);
}

// The border around a width x height picture whose bottom-left corner is at x, y.
void drawGameBorderAt(int x, int y, int width, int height, float whiteBorderPercentage, float blackBorderPercentage)
{
    // Store the old clear colour
    GLfloat originalClearColour[4];
    glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, originalClearColour);

    GLfloat blackColour[4] = {0.0, 0.0, 0.0, 0.0};
    GLfloat whiteColour[4] = {1.0, 1.0, 1.0, 0.0};

    glad_glEnable(GL_SCISSOR_TEST);

    drawBorderWithOffsetAt(x, y, width, height, whiteBorderPercentage, blackBorderPercentage, whiteColour);
    drawBorderWithOffsetAt(x, y, width, height, blackBorderPercentage, 0, blackColour);

    glad_glDisable(GL_SCISSOR_TEST);

    glad_glClearColor(originalClearColour[0], originalClearColour[1], originalClearColour[2], originalClearColour[3]);
}
static void (*frameOverlay)(int x, int y, int width, int height);

void setFrameOverlay(void (*draw)(int x, int y, int width, int height))
{
    frameOverlay = draw;
}

int frameOverlaySet(void)
{
    return frameOverlay != 0;
}

void drawFrameOverlay(int x, int y, int width, int height)
{
    if (frameOverlay)
        frameOverlay(x, y, width, height);
}
