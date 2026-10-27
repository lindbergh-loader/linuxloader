#pragma once

// Scaling of a game's frame to its window, for games that open and draw into
// their own window at a fixed size (the Raw Thrills games).
//
// Such a game draws into the window's framebuffer at the size it set up, so
// once the window is resized its picture is cropped or left in a corner: the
// window's framebuffer follows the window.  From the first swap where the
// window no longer matches that size, the game's "framebuffer 0" becomes an
// offscreen one of the original size, and each swap scales it into the
// window, letterboxed when keepAspect is set.  Until then nothing changes.
// Games that follow their window themselves (glut's reshape callback: Big
// Buck World) are left alone: they answer the resize with a full-frame
// viewport of the window's size.
//
// The game reaches that framebuffer through the GL entry points
// frameScaleWrapper() hands out in place of the real ones.

// getProcAddress loads the real GL entry points (glXGetProcAddressARB).
void frameScaleInit(void *(*getProcAddress)(const char *), int keepAspect);

// The replacement for a GL entry point, NULL if it is not one the scaler
// needs.
void *frameScaleWrapper(const char *name);

// Right before the swap, with the context current: when scaling, draws the
// frame into the window's back buffer (and the border around the picture,
// if enabled).  x, y, width x height is the part of the drawable the window
// shows, from its bottom left: all of it, unless the game draws in a child
// window that keeps its size when its window is resized (The Walking Dead).
// Returns 1 when it did, in which case the caller must not draw its own
// border.
int frameScalePresent(int x, int y, int width, int height, int border, float whiteBorderPercentage,
                      float blackBorderPercentage);

// Games drawing at a fixed size of their own choosing, not the window's:
// the frame is width x height from the first swap on, and is fitted in the
// part of the window hole() gives (x, y, width, height from the bottom left
// of a window of the size given; NULL: all of it).
void frameScaleSetFrame(int width, int height, void (*hole)(int, int, int *, int *, int *, int *));

// Games drawing for a monitor turned on its side (Tank! Tank! Tank!, its
// picture rotated in its landscape frame): the frame is turned a quarter
// turn back (1: counterclockwise, -1: clockwise) when it is fitted in the
// window, so that the picture is upright. Scaling is on from the first swap.
void frameScaleSetRotation(int quarterTurns);

// Games that follow their window (a full-window viewport at each resize)
// drawn at a shape of their own, width:height: their full-window viewports
// are moved to the middle of the window at that shape.
void frameScaleSetAspect(int width, int height);

// Where the picture sits in the window, as fractions of it: a point x, y of
// the window (0..1, from the top left) to the same point of the picture.
// Returns 0 when the point falls outside the picture.
int frameScaleWindowToGame(float *x, float *y);
