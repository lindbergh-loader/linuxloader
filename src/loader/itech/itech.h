#ifndef ITECH_H
#define ITECH_H

#include <stddef.h>

#include "itechGame.h"

// Incredible Technologies games (Orange County Choppers Pinball): see
// itechGame.h. They keep their own window (their X11 + GLX one), as the IGS
// games do: none of the Lindbergh path. Their input is the loader's, through
// the I/O board emulated here (itechIo.c).

// Game descriptor of the running game, or NULL if it is not an IT game.
const ItechGame *itechCurrentGame(void);
int isItechGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int itechInit(void);

// The cabinet's paths: /game/static (the game directory), /game/dynamic (its
// dynamic/, lib/ the dump's own), the devices. Returns path, or buf holding
// the mapped one.
const char *itechRedirectPath(const char *path, char *buf, size_t size);

// The cabinet's devices (itechIo.c): the IT layer's system calls detoured.
void itechIoInstall(void);

// The window and its picture (itechFrame.c): the frame turned upright into
// the window; itechBeforeSwap() at each swap, from glXSwapBuffers.
void itechFrameInstall(void);
void itechBeforeSwap(void);

// The executable's function name (it exports them all) detoured to
// replacement.
void itechDetour(const char *name, void *replacement);

#endif // ITECH_H
