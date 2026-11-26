#ifndef BYOYOYO_H
#define BYOYOYO_H

#include "byoyoyoGame.h"

// Byoyoyo games (Skycurser): see byoyoyoGame.h. They keep their own window
// (the GameMaker runner's X11 one), as the IGS games do: none of the
// Lindbergh path. Their sound is OpenAL Soft's OSS output, the loader's
// /dev/dsp; their controls the X keyboard, to which the loader adds its own
// input (byoyoyoInput.c).

// Game descriptor of the running game, or NULL if it is not a Byoyoyo game.
const ByoyoyoGame *byoyoyoCurrentGame(void);
int isByoyoyoGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int byoyoyoInit(void);

// The cabinet's controls (byoyoyoInput.c): the loader's input (evdev's, or
// the desktop's gamepads) sent to the game's window as the keys they stand
// for.
void byoyoyoInputInstall(void);
// The window the keys go to: the game's, once it has made it.
void byoyoyoInputWindow(unsigned long window);

#endif // BYOYOYO_H
