#ifndef NAMCO_N2_GAME_H
#define NAMCO_N2_GAME_H

#include <stdint.h>

#include "namcoFfb.h"

// Everything the loader needs to know about one Namco System N2 game. They
// are 32-bit Linux executables for an nForce2 PC with no X server: NVIDIA's
// OpenGL driver (with its display manager, adm*) and its nForce OpenAL are
// linked in. Every function is exported (.dynsym), so the hooks go by name
// and hold for every build; a descriptor only says which ones apply.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // gRomInfo's revision name starts with this (checked at start-up: the
    // descriptor's hooks were made for that game).
    const char *revision;

    // Serial ports: the JVS I/O board (n2Jvio), the steering wheel's force
    // feedback board (clKickback) and the magnetic card reader.
    const char *jvsDevice;
    const char *kickbackDevice;
    const char *cardDevice;
    // The wheel's force feedback, from clKickback (namcoFfb.c; ffb.instance
    // unused: clKickback::sm_instance is found by name).
    NamcoFfb ffb;

    // The boot's search for linked cabinets (clSeqBootNetThread::run): its
    // "movl $18000,0xc(%reg)", the frames (300 s) a lone cabinet waits before
    // it plays alone ("data merge"); made 60 (1 s).
    uint32_t linkSearchMov;

    // Games run with the cabinet's command line: its arguments, appended by
    // the launcher (namcoN2PrepareCommand), and the directory, relative to
    // the game's root, the game runs from.
    const char *arguments;
    const char *workDir;
} NamcoN2Game;

const NamcoN2Game *namcoN2GetGame(uint32_t crc32);
const NamcoN2Game *namcoN2GetGameByFileCrc(uint32_t fileCrc32);

#endif // NAMCO_N2_GAME_H
