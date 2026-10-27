#ifndef TEAMPLAY_H
#define TEAMPLAY_H

#include <stddef.h>
#include <sys/types.h>

#include "teamplayGame.h"

// Teamplay games (Crossfire Maximum Paintball, Police Trainer 2): see
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

// The MegaJamma board's device (/dev/mjg): a descriptor of our own, whose
// reads are the board's status packets and whose ioctls its registers (see
// teamplayMegaJamma.c).
int teamplayMjIsPath(const char *path);
int teamplayMjOpen(int (*realOpen)(const char *, int, ...));
int teamplayMjIsFd(int fd);
ssize_t teamplayMjRead(void *buf, size_t count);
int teamplayMjIoctl(unsigned long request, void *arg);
void teamplayMjClose(void);

#endif // TEAMPLAY_H
