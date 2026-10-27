#ifndef NAMCO_ES1_H
#define NAMCO_ES1_H

#include <stddef.h>
#include <sys/types.h>

#include "namcoEs1Game.h"

// Namco ES1 games (e.g. Nirin) are 32-bit Linux executables for a PC-based
// cabinet: a JVS I/O board on a serial port (n2Jvio), a HASP HL dongle, a
// network for linked play. They use none of the Lindbergh hardware: the I/O
// board and the dongle are emulated here.

// Game descriptor of the running game, or NULL if it is not a Namco game.
const NamcoEs1Game *namcoEs1CurrentGame(void);
int isNamcoEs1Game(void);

// Called from initMain() instead of the Lindbergh hardware setup.
int namcoEs1Init(void);

// The JVS I/O board's serial port: a descriptor of our own, whose writes are
// the game's packets and whose reads its board's replies (see namcoEs1.c).
int namcoEs1JvsIsPath(const char *path);
int namcoEs1JvsOpen(int (*realOpen)(const char *, int, ...));
int namcoEs1JvsIsFd(int fd);
ssize_t namcoEs1JvsRead(void *buf, size_t count);
ssize_t namcoEs1JvsWrite(const void *buf, size_t count);
int namcoEs1JvsIoctl(unsigned long request, void *arg);
void namcoEs1JvsClose(void);

// The steering wheel's force feedback board (kickbackDevice), on a serial
// port of its own: the same, see namcoEs1Kickback.c.
int namcoEs1KickbackIsPath(const char *path);
int namcoEs1KickbackOpen(int (*realOpen)(const char *, int, ...));
int namcoEs1KickbackIsFd(int fd);
ssize_t namcoEs1KickbackRead(void *buf, size_t count);
ssize_t namcoEs1KickbackWrite(const void *buf, size_t count);
int namcoEs1KickbackIoctl(unsigned long request, void *arg);
void namcoEs1KickbackClose(void);
// The board's reports the game waits for, and its power output watched.
void namcoEs1KickbackReportMotorPower(int running);
void namcoEs1KickbackReportSelfCheck(void);
void namcoEs1KickbackReportPoweredSelfCheck(void);
void namcoEs1KickbackInit(const NamcoEs1Game *g);

// Cabinet administration the games run as root (su -c: clock, network,
// kernel settings): not run. Returns 1 when command is dropped.
int namcoEs1DropCommand(const char *command);
// What a dropped command would have printed (the game's commandOutput), or
// NULL.
const char *namcoEs1CommandOutput(const char *command);
// The network ones answered instead (networkCommands, see namcoEs1Network.c).
void namcoEs1NetworkCommand(const char *command);
// WMMT4's shop terminal, announcing itself on the cabinet LAN with the
// drive's serial (namcoEs1Wmmt4Terminal.c).
void namcoEs1Wmmt4StartTerminal(const char *serial);
// The host's first interface with an IPv4 address, as the game's eth0
// (127.0.0.1 without one).
void namcoEs1HostInterface(unsigned char address[4], unsigned char mask[4], unsigned char mac[6]);

// SIOCGIFHWADDR: the games read the MAC address of eth0 (else eth1) and stop
// without one; PCs name their interfaces otherwise. Answered with the first
// network interface's address.
int namcoEs1HwAddr(int fd, void *ifreq, int (*realIoctl)(int, int, void *));
// eth0's flags, address, mask and broadcast address (SIOCGIF*) for a host
// that has no eth0, the game's (eth0): -1 when not answered.
int namcoEs1Eth0(unsigned long request, void *ifreq);

// GLSL shader sources: #version 120 constructs Mesa's compiler refuses
// (NVIDIA's accepted) are rewritten in place. Called from the
// glShaderSource interposer with the (already concatenated) source.
// The game's imports called on a 16-byte-aligned stack (see namcoEs1Align.c).
void namcoEs1AlignImports(void);
// The same for a function the game gets a pointer to.
void *namcoEs1AlignedPointer(void *function);

// The sound driver's entry points (nsAdrv_*), emulated on SDL3 (see
// namcoEs1Sound.c); NULL for other names.
void *namcoEs1SoundSymbol(const char *name);

// Before a swap of a game with its own window: the frame scaled into it
// (Tank! Tank! Tank!).
void namcoEs1BeforeSwap(void *dpy, unsigned long drawable);

// The cabinet's paths (rootPath, the game's directory there) in the game's
// directory here: path, or buf holding the mapped one.
const char *namcoEs1RedirectPath(const char *path, char *buf, size_t size);

// An NVIDIA extended assembly program (NV_vertex_program3,
// NV_fragment_program2) rewritten as a plain ARB one: a malloc'd string, or
// NULL when it is not one or uses what is not handled (namcoEs1ArbProgram.c).
char *namcoEs1ArbTranslate(const char *src, int len);
// The same for an NV_gpu_program4 one (Cg's gp4vp/gp4fp: WMMT4).
char *namcoEs1Gp4Translate(const char *src, int len);

void namcoEs1PatchShader(char *src);

#endif // NAMCO_ES1_H
