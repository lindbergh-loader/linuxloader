#ifndef ROXOR_H
#define ROXOR_H

#include <stddef.h>

#include "roxorGame.h"

// Roxor games (Tux Racer): see roxorGame.h. They keep their own SDL window,
// as America's Army does: none of the Lindbergh path. Their input is the
// loader's (the desktop keyboard, or evdev's JVS state), through the I/O
// board emulated here.

// Game descriptor of the running game, or NULL if it is not a Roxor game.
const RoxorGame *roxorCurrentGame(void);
int isRoxorGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int roxorInit(void);

// The cabinet's paths (rootPath, $HOME/<settings>) in the game's
// directory: path, or buf holding the mapped one.
const char *roxorRedirectPath(const char *path, char *buf, size_t size);

// The parallel port I/O board (roxorIo.c): its functions replaced.
void roxorIoInstall(const RoxorGame *game);

#endif // ROXOR_H
