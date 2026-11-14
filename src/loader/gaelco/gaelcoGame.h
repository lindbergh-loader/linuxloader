#ifndef GAELCO_GAME_H
#define GAELCO_GAME_H

#include <stdint.h>

// A cabinet directory, in the game's directory.
typedef struct
{
    const char *from; // cabinet path, e.g. "/home/save0"
    const char *to;   // under the game's directory, e.g. "save0"
} GaelcoPathAlias;

// How the game reads its I/O board (gaelcoIo.c).
typedef enum
{
    // Tokyo Cop: the board's registers 0x40-0x47 in its PCI configuration
    // space (iolinux::inport), port I/O through the driver's commands 8 and
    // 9 (the VGA's retrace and CRTC registers).
    GAELCO_IO_PCI_CONFIG,
    // Ring Riders, Gaelco Championship Tuning Race: the board's memory
    // window (the driver's commands 8 and 9, IOMEM_READB and IOMEM_WRITEB).
    GAELCO_IO_MEMORY,
} GaelcoIoKind;

// Which cabinet's controls are wired to the board, and how.
typedef enum
{
    GAELCO_TOKYO_COP,   // the car: wheel, gas, brake switch, horn, siren, view
    GAELCO_RING_RIDERS, // the motorbike: handlebar, throttle, brake, leaning
    GAELCO_TUNING_RACE, // the car: wheel, pedals, gear shift
} GaelcoControls;

// Everything the loader needs to know about one Gaelco game. They are
// 32-bit Linux 2.4 executables (X11/GLX on a GeForce, its NV extensions)
// on Gaelco's PC (Debian, kernel 2.4.16), started by the cabinet's scripts
// from /home/joc2001/<game>/salidas. Their hardware is behind Gaelco's
// kernel module, sndgaelco.o: system calls of its own (222-233 on the
// cabinet's kernel, xattr calls on today's) for the sound (a sampler of the
// game's sample ROM, mixed by the module onto the AC97 codec), the I/O
// board (switches, potentiometers, coin counters, lamps, the wheel's
// motor) and the vertical blank count. Their sources' names are in the
// ELF: the system calls' wrappers are found by name (gaelco.c).
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // The cabinet's directories, NULL-terminated, relative to the game's
    // directory (where the game runs from: salidas).
    const GaelcoPathAlias *paths;
    // The directory the cabinet runs the game in, relative to the game's
    // directory (its relative paths: records, settings); NULL: the game's.
    const char *workDir;

    // The size the game draws at, whatever its window's (scaled to it).
    int frameWidth, frameHeight;

    GaelcoIoKind io;
    // The cabinet's controls on the board (gaelcoIo.c).
    GaelcoControls controls;
    // The game's controls' set-up (IniControllers), which empties its
    // potentiometers' ranges (they then grow as the controls move: the
    // wheel is off-centre until it has been turned both ways): the
    // function, the length of its first instructions (moved out for the
    // hook), its potentiometers (0: the function's third argument), their
    // count and the margin its calibration keeps from the ends.
    uint32_t iniControllers;
    int iniControllersPrologue;
    uint32_t potentiometers;
    int potentiometerCount, potentiometerMargin;
    // Tokyo Cop's second set-up of the same ranges (IniPotes, at the
    // software's start, after IniControllers' at the hardware's), which
    // empties them again: the wheel was off-centre (the car pulled to a
    // side) until turned to both ends. Its length of first instructions,
    // as above. 0: none.
    uint32_t iniPotes;
    int iniPotesPrologue;
    // Ring Riders' RS485 string read (G3Dintf_ports::RS485getString(char *,
    // int)), which waits 200 ms whatever comes: the drive's answers are
    // there at once. 0: none.
    uint32_t rs485GetString;
    // Ring Riders' platform set-up (iniPlataforma), which programs the
    // drive and reads its parameters back, with waits between them: done
    // here without the drive's part. 0: none.
    uint32_t platformInit;
    // Tokyo Cop's motion platform loop (ProcesaPlataforma): its jump past
    // the "no motion" setting's state, made 2 bytes of nops, so that the
    // platform is always off, whatever the saved setting (there is no
    // platform: its self-test fails, "MOTION SYSTEM ERROR!" stays on
    // screen). 0: none.
    uint32_t noMotionJump;
    // Built by Intel's compiler, which keeps the stack 4-byte aligned:
    // the imports go through stubs that align it (importHook.h).
    int alignStack;
} GaelcoGame;

const GaelcoGame *gaelcoGetGame(uint32_t crc32);
const GaelcoGame *gaelcoGetGameByFileCrc(uint32_t fileCrc32);

#endif // GAELCO_GAME_H
