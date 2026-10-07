#ifndef TEAMPLAY_H
#define TEAMPLAY_H

#include <stddef.h>
#include <sys/types.h>

#include "teamplayGame.h"

// Teamplay games (Crossfire Maximum Paintball, Police Trainer 2, Star Trek:
// Voyager): see
// teamplayGame.h. They are glut or X11/GLX games like the Lindbergh ones,
// and run on the same path: the loader's glut or X11 bridge is their
// window, its JVS state their input. Their cabinet's hardware is emulated
// here.

// Game descriptor of the running game (or of the game whose sound daemon
// this is), or NULL if it is not a Teamplay game.
const TeamplayGame *teamplayCurrentGame(void);
int isTeamplayGame(void);

// Called from initMain(), before the hooks are enabled.
int teamplayInit(void);

// The cabinet's paths (rootPath, dataPath, rootAliases) in the game's
// directory, and its /proc/version and /proc/cpuinfo: path, or buf holding
// the mapped one.
const char *teamplayRedirectPath(const char *path, char *buf, size_t size);

// The OSS mixer settings a sound daemon makes ("mixer vol 100"; it exits if
// there is no mixer command): not run, the volume is the host's. Returns 1
// when command is dropped.
int teamplayDropCommand(const char *command);

// A command the game starts (its sound daemon, "./stvsnd ..."): a program
// of the dump that lost its executable mode runs through the host's dynamic
// linker. Returns command, or buf holding the new one.
const char *teamplayRedirectCommand(const char *command, char *buf, size_t size);

// The MegaJamma board's device (/dev/mjg, /dev/mjf): a descriptor of our own, whose
// reads are the board's status packets and whose ioctls its registers (see
// teamplayMegaJamma.c).
int teamplayMjIsPath(const char *path);
int teamplayMjOpen(int (*realOpen)(const char *, int, ...));
int teamplayMjIsFd(int fd);
ssize_t teamplayMjRead(void *buf, size_t count);
int teamplayMjIoctl(unsigned long request, void *arg);
void teamplayMjClose(void);

// Gun gun's aim (0: player 1) in the game's gun space, from its top left, 0
// to 1; 0 when it is off the screen.
int teamplayGunAim(int gun, float *x, float *y);

// The guns' crosshairs ([CrossHairs] ENABLE_CROSSHAIRS), drawn into the glut
// games' frame, width x height from the bottom left of the back buffer,
// before the loader scales it (teamplayCrosshair.c).
void teamplayDrawCrosshairs(int width, int height);

#endif // TEAMPLAY_H
