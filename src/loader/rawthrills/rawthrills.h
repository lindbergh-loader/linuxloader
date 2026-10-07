#ifndef RAWTHRILLS_H
#define RAWTHRILLS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "rtGame.h"

// Raw Thrills games (g5 engine, e.g. Jurassic Park Arcade) are dumps of HASP
// Envelope-protected Linux binaries. They do not use any Lindbergh hardware:
// the dongle, the RIO I/O board and the sound device are emulated here.

// Game descriptor of the running game, or NULL if it is not a Raw Thrills game.
const RtGame *rtCurrentGame(void);
int isRawThrillsGame(void);
// Directory the game was started from, standing in for the cabinet's root.
const char *rtGameDir(void);

// Called from initMain() instead of the Lindbergh hardware setup.
int rtInit(void);

// Filesystem: map cabinet paths (/pm, /mnt/usbflash, /etc/udev, encrypted
// scripts) to their host location (through common/gamePath.c). Returns path
// or buf.
const char *rtRedirectPath(const char *path, char *buf, size_t size);
const char *rtRedirectCommand(const char *command, char *buf, size_t size);

// Helpers shared by the rawthrills modules.
// Game function by name: exported, or listed in the game's descriptor.
void *rtSymbol(const char *name);
// Size of an exported game function (its dynamic symbol's), 0 if unknown.
uint32_t rtSymbolSize(const char *name);
int rtDetour(const char *name, void *replacement);
void rtDetourAddress(uint32_t address, void *replacement);
void *rtTrampoline(const char *name, size_t prologueLength);
int rtFixImports(const RtGame *game);
void rtClearBss(const RtGame *game);
int rtReplaceLib(const RtGame *game);
void rtInstallDongle(const RtGame *game);
void rtInstallIo(const RtGame *game);
void rtInstallInput(const RtGame *game);
void rtInstallJamma(const RtGame *game);
void rtInstallFfb(const RtGame *game);
// Evdev input (the loader's JVS state): whether a switch input is held.
struct JVSIO;
int rtIoSwitchState(const RtIoInput *in, struct JVSIO *io);
// Presses of a player's switch (bit) the evdev input counted since *last.
int rtSwitchTaps(struct JVSIO *io, int player, uint32_t bit, unsigned int *last);
// A board's count of a switch's transitions (odd while held, or even when
// inverted), read once a frame: the presses since (taps) are all in it.
void rtSwitchCount(uint32_t *count, int *wasHeld, int held, int taps, int inverted);
// GLSL shader sources: the engine's fixes, applied in place to the (already
// concatenated) source by the glShaderSource interposer
// (graphics/shaderPatches.c).
void rtPatchShaderSource(char *src);

// Imports a game gets from the frame code instead of the real ones (a
// fixedFrame game's SDL_SetVideoMode), NULL for the others (rtFrame.c).
void *rtFrameOverride(const char *name);
// The JAMMA board's input (g3 games): the loader's JVS state with evdev
// input, else the game window's keys and mouse (see rtJamma.c).
struct JVSIO *rtJammaInput(void);
// Holds or releases a switch of the game window's input (rtJamma.c).
void rtJammaDesktopSwitch(int player, int bit, int held);
// A position of the input (two analogue channels) as a fraction of the
// picture from its top left: 0 when it is off it (see rtJamma.c).
int rtJammaPosition(struct JVSIO *io, int xChannel, int yChannel, float *x, float *y);
// Deal or No Deal's parallel port panel: an in (in = 1) or out instruction
// on a port, emulated (1) or not the panel's (0). eax: the register.
int rtDondPortIo(int in, uint16_t port, uint32_t *eax);
// A gun's position from the loader's input (evdev or the desktop pointer),
// 0..1 from the picture's top left: 0 when it is off the screen.
int rtGunOnScreen(int gun, float *x, float *y);
void rtGlutSwapBuffers(void);
void rtSdlGlSwapWindow(void (*real)(void *), void *window);
void rtGlxSwapBuffers(void *display, unsigned long drawable);
void *rtRealDlsym(void *handle, const char *name);
void rtInstallVideo(const RtGame *game);

#endif // RAWTHRILLS_H
