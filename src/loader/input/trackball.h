#ifndef TRACKBALL_H
#define TRACKBALL_H

// A trackball's motion, for games reading one as counts (Global VR's GFXIO
// board): the relative motion of the SDL mouse (desktop input) and of the
// evdev relative axes (EV_REL, mapped), added up until a reader takes it.
// The mouse buttons are kept too (SDL only), for games whose buttons sit
// by the trackball.

void trackballAdd(int dx, int dy);
// The motion since the last call, then cleared.
void trackballTake(int *dx, int *dy);

// SDL mouse buttons (SDL_BUTTON_LEFT...): held state, and held as a mask
// (1 << (button - 1)).
void trackballSetButton(int button, int held);
unsigned int trackballButtons(void);

// Games wanting the mouse captured (relative mode: unbounded motion, the
// pointer hidden): asked from any thread, applied on the window's
// (trackballApplyCapture, from the SDL event loop).
void trackballRequestCapture(void);
void trackballApplyCapture(void *sdlWindow);

#endif // TRACKBALL_H
