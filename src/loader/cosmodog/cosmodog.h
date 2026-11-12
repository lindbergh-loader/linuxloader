#ifndef COSMODOG_H
#define COSMODOG_H

#include <stddef.h>

#include "cosmodogGame.h"

// Cosmodog games (Rockin' Bowl-O-Rama): see cosmodogGame.h. They keep their
// own glut window (the loader's glut bridge), as the Teamplay games do: none
// of the Lindbergh path. Their input is the loader's (the desktop keyboard
// and mouse, or evdev's JVS state and mice), through the I/O controller
// emulated here.

// Game descriptor of the running game, or NULL if it is not a Cosmodog game.
const CosmodogGame *cosmodogCurrentGame(void);
int isCosmodogGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int cosmodogInit(void);

// The cabinet's paths (rootPath, dataPath) in the game's directory: path, or
// buf holding the mapped one.
const char *cosmodogRedirectPath(const char *path, char *buf, size_t size);

// The serial I/O controller (cosmodogIo.c): the game's packets answered.
void cosmodogIoInstall(const CosmodogGame *game);

#endif // COSMODOG_H
