#ifndef IGS_H
#define IGS_H

#include <stddef.h>

#include "igsGame.h"

// IGS games (Gold Captain): see igsGame.h. They keep their own window (a
// static SDL 1.2 on X11): none of the Lindbergh path. Their input is the
// loader's (the desktop keyboard, or evdev's JVS state), through the
// IGS036 emulated here.

// Game descriptor of the running game, or NULL if it is not an IGS game.
const IgsGame *igsCurrentGame(void);
int isIgsGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int igsInit(void);

// The cabinet's paths (rootPath, dataDirs, its devices) mapped to the
// game's directory: path, or buf holding the mapped one.
const char *igsRedirectPath(const char *path, char *buf, size_t size);

// Paletted textures, expanded to RGBA (igsPalette.c).
void igsPaletteInstall(void);

// A display's Xlib locks taken away (igsX11.c).
void igsUnlockDisplay(void *display);
// The X screen's size: 0 when there is no display.
int igsScreenSize(int *width, int *height);
// The game's window, larger than width x height, seen at that size by the
// game (XGetWindowAttributes), and its real size (0: as the game sees it).
void igsWindowAttributesInstall(int width, int height);
int igsWindowSize(int *width, int *height);

// The frames the game has shown (its buffer swaps).
unsigned int igsFrames(void);
// Ogre owns SD3's GLX window; capture its frames through the GLX bridge.
void igsBeforeSwap(void);

// The PLX card and the IGS036 behind it (igsIo.c): their functions
// replaced.
void igsIoInstall(const IgsGame *game);

// The A27 board and /dev/pccard0 (igsA27.c), for a game with an a27Rom.
void igsA27Install(const IgsGame *game, const char *gameDir);
// The A36 board and /dev/pccard0 (igsA36.c), for a game with an a36Rom.
void igsA36Install(const IgsGame *game, const char *gameDir);
void igsSd3InputInstall(const IgsGame *game);
void igsSd2Install(const IgsGame *game, const char *gameDir);
void igsSr1Install(const IgsGame *game, const char *gameDir);
// Speed Driver 3's vp40/fp40 programs on hosts without NVIDIA's program
// extensions (igsNvProgram.c).
void igsNvProgramInstall(void);

#endif // IGS_H
