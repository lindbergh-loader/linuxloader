#ifndef CECROPIA_H
#define CECROPIA_H

#include <stddef.h>

#include "cecropiaGame.h"

// Cecropia games (The Act): see cecropiaGame.h. They keep their own SDL
// window, as the Roxor games do: none of the Lindbergh path. Their input is
// the desktop's (keys and mouse, read by the game itself) with a gamepad,
// or evdev's JVS state, turned into the cabinet's (cecropiaInput.c).

// Game descriptor of the running game, or NULL if it is not a Cecropia game.
const CecropiaGame *cecropiaCurrentGame(void);
int isCecropiaGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int cecropiaInit(void);

// The cabinet's paths (the state database) in the game's directory: path,
// or buf holding the mapped one.
const char *cecropiaRedirectPath(const char *path, char *buf, size_t size);

// The knob, the button and the coins (cecropiaInput.c): the knob's read
// hooked (MinHook, enabled with the others), the events sent from the
// game's event loop.
void cecropiaInputInstall(const CecropiaGame *game);

#endif // CECROPIA_H
