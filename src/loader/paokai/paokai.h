#ifndef PAOKAI_H
#define PAOKAI_H

#include <stddef.h>

#include "paokaiGame.h"

// Paokai games (Go Go Strike): see paokaiGame.h. They keep their own window (their
// Allegro's X11 one), as the IGS games do: none of the Lindbergh path. Their
// input is the loader's (the desktop keyboard and mouse, or evdev's JVS
// state and mice), through the I/O card emulated here (paokaiIo.c).

// Game descriptor of the running game, or NULL if it is not a Paokai game.
const PaokaiGame *paokaiCurrentGame(void);
int isPaokaiGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int paokaiInit(void);

// The cabinet's devices: the I/O card's ports and control (/dev/null, the reads are
// the loader's) and the SRAM (the game directory's sram.bin): path, or buf
// holding the mapped one.
const char *paokaiRedirectPath(const char *path, char *buf, size_t size);

// The I/O card (paokaiIo.c): the game's port reads answered.
void paokaiIoInstall(const PaokaiGame *game);

#endif // PAOKAI_H
