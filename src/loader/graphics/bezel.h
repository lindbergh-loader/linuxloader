#pragma once

// A bezel: a PNG drawn over the whole window, the game's picture showing
// through its transparent middle ([Display] BEZEL, the path of the image,
// bezel.png by default). [Display] BEZEL_ENABLED: 0 never, and by default
// (-1) when the image exists.
//
// The hole is found in the image itself: the transparent run of pixels
// through its centre, across and down. Games that fit their picture in the
// window themselves (Pac-Man, Galaga Assault) fit it in that hole instead.

// Loads the image, 0 if it cannot be (no bezel then).
int bezelLoad(const char *path);

// 1 once an image is loaded.
int bezelLoaded(void);

// The hole of a width x height window, from its bottom left: all of it
// without a bezel.
void bezelHole(int width, int height, int *x, int *y, int *holeWidth, int *holeHeight);

// Draws the bezel over x, y, width x height of the window's back buffer
// (framebuffer 0), from the bottom left; with the context current, leaving
// the GL state as it was. Nothing without a bezel.
void bezelDraw(int x, int y, int width, int height);
