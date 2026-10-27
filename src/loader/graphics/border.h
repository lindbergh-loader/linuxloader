#pragma once

void drawGameBorder(int width, int height, float whiteBorderPercentage, float blackBorderPercentage);
void drawGameBorderAt(int x, int y, int width, int height, float whiteBorderPercentage, float blackBorderPercentage);

// Something drawn over the whole window before the border (the bezel):
// draw(x, y, width, height), the window's part from its bottom left.
void setFrameOverlay(void (*draw)(int x, int y, int width, int height));
int frameOverlaySet(void);
void drawFrameOverlay(int x, int y, int width, int height);
