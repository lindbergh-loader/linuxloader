#ifndef GAME64_H
#define GAME64_H

// The 64-bit games, which the 32-bit loader hands to linuxloader64: what a
// game directory holds, found by both (see game64.c).

#include <limits.h>

typedef enum
{
    GAME64_NONE,
    GAME64_G7,    // a Raw Thrills g7 dump (Halo, Centipede): run by ld.so
    GAME64_UNITY, // a Unity game: <name>_Data, run by Unity's player
} Game64Kind;

typedef struct
{
    Game64Kind kind;
    // GAME64_G7: the executable's directory, relative to the game's, and
    // its name ("game2", the dump, before "game").
    // GAME64_UNITY: "" and the game's name (<name>_Data).
    char dir[PATH_MAX];
    char name[256];
} Game64;

// What gameDir holds; kind GAME64_NONE: not a 64-bit game.
Game64 game64Find(const char *gameDir);

#endif // GAME64_H
