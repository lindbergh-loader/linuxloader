#ifndef GAELCO_H
#define GAELCO_H

#include <stddef.h>
#include <stdint.h>

#include "gaelcoGame.h"

// Gaelco games (Tokyo Cop, Ring Riders, Gaelco Championship Tuning Race):
// see gaelcoGame.h. They make their own X11/GLX window, which the loader's
// X11 and GLX bridges stand in for, as for the Teamplay games; their input
// is the loader's JVS state (a driving game's: the wheel, the pedals). Their
// kernel module (sound, I/O board) and the GeForce's extensions are
// emulated here.

// Game descriptor of the running game, or NULL if it is not a Gaelco game.
const GaelcoGame *gaelcoCurrentGame(void);
int isGaelcoGame(void);

// Called from initMain(), before the hooks are enabled.
int gaelcoInit(void);

// The cabinet's paths in the game's directory: path, or buf holding the
// mapped one.
const char *gaelcoRedirectPath(const char *path, char *buf, size_t size);

// A 60 Hz count from the game's start: the module's vertical blank count.
uint32_t gaelcoVsyncCount(void);

// The sound module (gaelcoSound.c): the sampler fed the game's sample ROM
// and commands, mixed into an SDL3 stream.
int gaelcoSoundStart(void);
int gaelcoSoundStop(void);
int gaelcoSoundLoad(const void *samples, int size);
int gaelcoSoundAddCommand(int command, int channel, int p0p1, int p2p3);
// The interface commands of the sampler (IOINTF_CMD_MASTERVOL...); 1 when
// command is one, its result in *result.
int gaelcoSoundCommand(int command, int *parms, int *result);

// The I/O board (gaelcoIo.c): its registers read and written.
int gaelcoIoInport(int port, char *value);
int gaelcoIoOutport(int port, char *value);
// The board's commands (IOINTF_CMD_GETCURRENTCOINS, IOMEM_READB...); 1 when
// command is one, its result in *result.
int gaelcoIoCommand(int command, int *parms, int *result);
// The game's controls' set-up hooked: its potentiometers calibrated.
void gaelcoIoInstall(void);

// Ring Riders' motion drive on the board's RS485 port (gaelcoVariador.c):
// a byte the game sends, the answer's bytes.
void gaelcoVariadorSend(int byte);
int gaelcoVariadorAvailable(void);
int gaelcoVariadorPeek(void);
void gaelcoVariadorNext(void);
void gaelcoVariadorInstall(void);

#endif // GAELCO_H
