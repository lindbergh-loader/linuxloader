#ifndef G7_H
#define G7_H

// The 64-bit Raw Thrills g7 games (Halo: Fireteam Raven, Centipede Chaos):
// dumps of HASP Envelope-protected executables, run by the dynamic linker
// with linuxloader64.so preloaded (the 32-bit loader cannot host them; its
// g7 part is in this directory, see loader64/linuxloader64.c). g7_rt.c has
// what they share: the import table rebuilt, the dongle answered, the
// cabinet's /pm mapped onto the install, the window scaled; each game's
// descriptor (g7Games.c) the addresses, and its own hooks.

#include <stddef.h>
#include <stdint.h>

// A slot of the dump's GOT and the symbol it is bound to (tools/g7-imports.py).
typedef struct
{
    unsigned long slot;
    const char *name, *version;
} G7Import;

typedef struct G7Game
{
    const char *name;
    // The executable's dynamic section, which tells the dumps apart.
    uintptr_t dynamic;
    // The game's directory on the cabinet (/pm/g7/<game>), and the prefix of
    // its environment settings (<PREFIX>_CABINET_TYPE, _SERIAL_NO, _REGION).
    const char *cabinetDir;
    const char *envPrefix;
    const G7Import *imports;
    size_t importCount;
    // The linked HASP HL API (0: not hooked).
    struct
    {
        uintptr_t login, logout, encrypt, decrypt, free, sessionInfo, read, write;
    } hasp;
    // The cabinet type in the dongle's identity (file 0xfff4, byte 41), when
    // <PREFIX>_CABINET_TYPE does not set it.
    int defaultCabinet;
    // A dongle that comes with its factory setup done (byte 40): games whose
    // setup asks for the serial number on an on-screen keypad the desktop
    // cannot drive (Centipede). Kept until the game writes its own.
    int factorySetupDone;
    // Video mode: the command-line parser (hooked, its prologue moved) and
    // the settings it fills (+0 window mode, +4 width, +8 height, +0xc
    // aspect), set from [Display] after it (0: left to the command line).
    uintptr_t parseArgs;
    size_t parseArgsPrologue;
    uintptr_t videoSettings;
    // A cabinet path the game's install keeps elsewhere: NULL, or buf.
    const char *(*mapPath)(const char *path, char *buf);
    // The game's own hooks, installed last.
    void (*install)(const struct G7Game *game);
} G7Game;

const G7Game *g7Find(uintptr_t dynamic);

// Shared with the games' own hooks (g7_rt.c).
extern struct G7Config
{
    int width, height, fullscreen, borderEnabled, keepAspectRatio, inputMode;
    float whiteBorder, blackBorder;
} g7Config;

// /pm, and the game's folder (/pm/g7/<game>), here.
extern char g7Root[];
extern char g7GameDir[];

long g7EnvNum(const char *name, long def);
// <PREFIX>_name of the running game, as g7EnvNum.
long g7GameEnv(const char *name, long def);
const char *g7IniValue(const char *section, const char *key);
// The parsed linuxloader.ini (an IniConfig, see iniParser.h), or NULL.
void *g7Ini(void);
// The cabinet type in the dongle's identity (byte 41).
int g7CabinetType(void);
// The drawable the game last presented (0 before its first frame).
unsigned long g7GameWindow(void);

// Gamepads (g7Pads.c): polled once a frame, looked for while there are
// fewer than players; what player's pad holds (G7_PAD_* bits, 0: no pad).
#define G7_MAX_PADS 4
#define G7_PAD_UP 0x01
#define G7_PAD_DOWN 0x02
#define G7_PAD_LEFT 0x04
#define G7_PAD_RIGHT 0x08
#define G7_PAD_BUTTON 0x10
#define G7_PAD_START 0x20
#define G7_PAD_SELECT 0x40
#define G7_PAD_TEST 0x80     // R3: the operator's test (diag) button
#define G7_PAD_SERVICE 0x100 // L3: service
void g7PadsPoll(int players);
unsigned int g7PadHeld(int player);

// Code patches: an absolute jump over a function's entry, and a detour
// through a page near the game whose original first len bytes (whole
// instructions, none RIP-relative) run in the returned trampoline.
void g7Detour(uintptr_t at, void *to);
void *g7HookNear(uintptr_t at, size_t len, void *to);

#define g7Log(...) fprintf(stderr, "g7_rt: " __VA_ARGS__)

#endif // G7_H
