#ifndef NAMCO_N2_H
#define NAMCO_N2_H

#include <stddef.h>
#include <sys/types.h>

#include "namcoN2Game.h"

// Namco System N2 games (Wangan Midnight Maximum Tune 3, 3DX+): see
// namcoN2Game.h. The port follows the Pacloader fork's (hardware/namco/n2).

// Game descriptor of the running game, or NULL if it is not an N2 game.
const NamcoN2Game *namcoN2CurrentGame(void);
int isNamcoN2Game(void);

// Called from initMain() instead of the Lindbergh hardware setup.
int namcoN2Init(void);

// Launcher side (mainShared.c, namcoN2Launch.c), for the N2 games run with
// the cabinet's command line (their descriptor's arguments): appends it.
// Returns 1 when the command was changed.
int namcoN2PrepareCommand(char *command, size_t size, uint32_t crc32);

// Counter Strike NEO's cabinet free disk (namcoN2Csneo.c): the engine's raw
// openat(AT_FDCWD, ...) opens of /freespace/... (its setting.ini, CloseTime.
// ini, the player data) are remapped onto the dump's TeknoParrot/ copy by
// the loader's openat funnel. 1 when the path is a freespace one (the caller
// opens buf instead), 0 otherwise.
int namcoN2Openat(int dirfd, const char *path, int flags, char *buf, size_t size);

// The cabinet's shell commands (system()), none of which runs: the ones the
// game needs are answered here (namcoN2Command.c). Returns 1 when handled,
// with the command's exit status in *status.
int namcoN2Command(const char *command, int *status);

// The cabinet's work disk is /tmp (tmpfs and a USB disk): it lives in the
// game's directory, as tmp/. path, or buf holding the mapped one.
const char *namcoN2RedirectPath(const char *path, char *buf, size_t size);

// The game's directory (its working directory at start-up).
const char *namcoN2GameDir(void);

// Hooks by name (namcoN2Hook.c). The functions are the game's own, found in
// its dynamic symbols. namcoN2HookOriginal also gives a pointer that runs the
// original function (a trampoline); 0 when the name is not the game's or
// its first instructions cannot be moved.
void *namcoN2Symbol(const char *name);
int namcoN2Hook(const char *name, void *replacement);
int namcoN2HookOriginal(const char *name, void *replacement, void **original);
// For each of the game's exported symbols starting with prefix and placed
// in its writable code (.writetext: NVIDIA's OpenGL entry points, which its
// driver fills in at start-up), a jump to resolve(name) when not NULL.
int namcoN2PatchEntryPoints(const char *prefix, void *(*resolve)(const char *));

// The display manager (adm*) on the loader's SDL window and GL context, and
// the game's OpenGL entry points on the host's (namcoN2Graphics.c).
void namcoN2GraphicsInit(void);

// The steering wheel's force feedback board's port (namcoN2Kickback.c).
void namcoN2KickbackInit(void);
int namcoN2KickbackIsPath(const char *path);
int namcoN2KickbackOpen(int (*realOpen)(const char *, int, ...));
int namcoN2KickbackIsFd(int fd);
ssize_t namcoN2KickbackRead(void *buf, size_t count);
ssize_t namcoN2KickbackWrite(const void *buf, size_t count);
int namcoN2KickbackIoctl(unsigned long request, void *arg);
void namcoN2KickbackClose(void);

// Sound: the game's linked-in nForce OpenAL on the host's (namcoN2Audio.c).
void namcoN2AudioInit(void);

// The HASP dongles (namcoN2Hasp.c).
void namcoN2HaspInit(void);

// The JVS board's port (the n2Jvio library, as on ES1): served by the
// Namco JVS emulation in namcoEs1.c. Its analog inputs in the cabinet's raw
// counts (the fork's n2AnalogueCount: wheel 0..65535, accelerator
// 35000..55480, brake 29000..49480).
void namcoN2Calibrate(int *channel);

#endif // NAMCO_N2_H
