#ifndef GVR_H
#define GVR_H

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "gvrGame.h"

// Global VR games (Puck Off): see gvrGame.h. They run on the Lindbergh
// path, their X11/GLX window the loader's (x11Bridge, glxBridge), its JVS
// state their input. Their cabinet's hardware is emulated here.

// Game descriptor of the running game, or NULL if it is not a Global VR game.
const GvrGame *gvrCurrentGame(void);
int isGvrGame(void);

// Called from initMain(), before the hooks are enabled.
int gvrInit(void);

// The cabinet's paths (rootPath) in the game's directory: path, or buf
// holding the mapped one.
const char *gvrRedirectPath(const char *path, char *buf, size_t size);

// The GFXIO board's device (/dev/usb/gfxio0): a descriptor of our own, whose
// reads are the board's reports and whose ioctls its lamps (see
// gvrGfxio.c).
int gvrGfxioIsPath(const char *path);
int gvrGfxioOpen(int (*realOpen)(const char *, int, ...));
int gvrGfxioIsFd(int fd);
ssize_t gvrGfxioRead(void *buf, size_t count);
int gvrGfxioIoctl(unsigned long request, void *arg);
void gvrGfxioClose(void);

// The game's connection to its cabinet's I/O daemon (linkPort): to the
// loader's (see gvrLink.c). Other connections go to realConnect.
int gvrLinkConnect(int fd, const struct sockaddr *addr, socklen_t length,
                   int (*realConnect)(int, const struct sockaddr *, socklen_t));
// Player's gun (0, 1) in pixels of the game's 640x480 picture, as the link
// last sent it: 0 when it is off the screen.
int gvrLinkGun(int player, int *x, int *y);

// The loader's crosshairs over the guns' aim ([CrossHairs] ENABLE_CROSSHAIRS),
// drawn into the game's picture at each SDL_GL_SwapBuffers (gvrCrosshair.c).
void gvrCrosshairInstall(void);

// A game drawing at a fixed size (frameWidth): its picture fitted to the
// loader's window (gvrFrame.c). Before the stack-aligning stubs.
void gvrFrameInstall(const GvrGame *game);

#endif // GVR_H
