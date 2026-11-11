#ifndef IGS_GAME_H
#define IGS_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one IGS (International Games
// System) PC game. Gold Captain (2009) and Pirate Revenge (2008): 32-bit
// Linux games on IGS's own distribution (IGS-Linux, kernel 2.4), drawing
// through a static SDL 1.2 on X11 and GL. Their cabinet: a PLX PCI9030 card
// whose shared RAM talks to the IGS036, the board's protected CPU (the
// cabinet's switches, coins, lamps, the operator's settings and the game's
// stage flow), a BIOS id check through /dev/roio and a security device the
// game only opens (/dev/pccard0).
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // The cabinet's game directory (rootPath, NULL: none) is the dump's;
    // its other absolute directories (dataDirs, NULL-terminated) go there
    // too.
    const char *rootPath;
    const char *dataDirs[4];
    // Firmware-resident Lua script sizes passed by the cabinet launcher.
    const char *arguments;

    // The motherboard id the BIOS check answers (its table's ids), and the
    // check itself: int (table, id).
    int biosBoard;
    uint32_t biosCheck;

    // The static SDL's SDL_SetVideoMode (the game asks for full screen), and
    // the size the game draws at (its cabinet's monitor).
    uint32_t setVideoMode;
    int frameWidth, frameHeight;

    // The cabinet's switches' bits in the IGS036's block (word 5): the left
    // and right buttons, the bomb button (0: none), test and service.
    uint8_t keyLeft, keyRight, keyBomb, keyTest, keyService;
    // The helm's potentiometer range (IGS036 word 6: 0..helmMax - 1); the
    // game reads it as 100 - value / 5.12 percent. Reversed: 0 is the right.
    int helmMax, helmReversed;
    // The helm's fastest turn (its full range is 2: per frame the game
    // shows), as the game takes a faster one for a dodge (then on buttons of
    // their own). 0: as fast as the controls.
    double helmSpeed;
    // The helm's calibration (VR Setting: its two ends and its centre, 3
    // ints) in the game's settings (an address) and in the operator's
    // settings it hands the IGS036 (a word), which the game's defaults
    // leave at 0 (no turn at all): it is the full range then. 0: none.
    uint32_t helmCalibration;
    int helmCalibrationWord;

    // The game's reads of the desktop keyboard (its debug keys, and its
    // controls' keys besides the cabinet's: KeyDown, KeyPress, KeyUp), none
    // pressed: the controls are the loader's. 0: left as they are.
    uint32_t keyboardReads[3];

    // The PLX SDK's device find, open and BAR map (PlxPciDeviceFind,
    // PlxPciDeviceOpen, PlxPciBarMap).
    uint32_t plxDeviceFind, plxDeviceOpen, plxBarMap;

    // The IGS036 exchange (one round of the shared RAM's mailbox, returning
    // the command the IGS036 acknowledged), the game's handling of the
    // IGS036's block, and their variables: the IGS036's block as read
    // (decrypted, 32-bit words), the PC's as sent, the pending command and
    // its argument.
    uint32_t mcuExchange, mcuProcess;
    uint32_t mcuIn, pcOut, pendingCommand, pendingArgument;

    // The stage's segments the IGS036 answers (command 5: 10 of start,
    // length, attribute), and the stage's objects as loaded from its SOD
    // file (their depth, an int, then the next object's).
    uint32_t sodObjectDepth;
    int sodObjects, sodObjectSize;

    // The game's stage (an int, the stage switch's index), the fight stage
    // whose loop leaves on its time up without naming the next stage, and
    // that fight's time left (frames), segment and count of segments: the
    // time up ends the segments, as its debug key does, so the stage ends.
    // fightTime 0: none (each fight ends and frees itself).
    uint32_t stage;
    int segmentFight;
    uint32_t fightTime, segment, segmentCount;

    // The fights' setups (each loads its fight; the segment fight's first),
    // the segment fight's teardown, which frees it only when its flag (an
    // int) is 1 (else the boss's fight goes on with it), and the weapons'
    // release every fight's teardown calls. 0: none (each fight frees what it
    // loaded).
    uint32_t fightSetups[3];
    uint32_t fightRelease, fightReleaseFlag, weaponsRelease;

    // Speed Driver (2003) is of an earlier cabinet: no PLX card nor IGS036,
    // but the A27 board (an IGS027A, igsA27.c) behind /dev/pccard0, running
    // this ROM (in the game's directory). None of the fields above but the
    // paths then, and the video mode (0: SDL_SetVideoMode, imported).
    const char *a27Rom;

    // Speed Driver 2: A36 with missing mask-ROM services and the older
    // PLX9030 shared-memory protocol (igsSd2.c).
    const char *sd2Rom;

    // Speed Rider M109CNC: its decrypted EROM on an IGS036 behind a PLX9030,
    // with an STM32 cabinet controller (igsSr1.c).
    const char *sr1Rom;

    // Speed Driver 3 (2010) has the A36 board (an IGS036, igsA36.c) behind
    // /dev/pccard0, running this ROM (in the game's directory); a36Idle,
    // its main loop's first and last addresses (the board sleeps there);
    // a36Loader, the game's upload of it (P2A_Loader iInit, iReset,
    // iUnInit, LoaderAdv iLoadData), stood in for.
    const char *a36Rom;
    uint32_t a36Idle[2];
    uint32_t a36Loader[4];
    // The SRAM table of internal-ROM services the firmware calls (0: none).
    uint32_t a36Services;
    // The cabinet's STM32 I/O controller (igsStm32.c) on this USART + 1
    // (0: none).
    int a36Stm32Usart;
    // Its pots' raw ranges (igsStm32.h's pots; all 0: the full range).
    uint16_t a36Stm32Pots[4][3];
    int a36Stm32Throttle; // the throttle's pot (VR1 + n)
    // The FPGA cipher's mask for the game's scripts, when it is not the
    // one its key has (all 0: the key's).
    uint8_t a36CipherMask[16];
    // Speed Driver 4: a mask for each 64 KiB of a file, from its first
    // (the first is a36CipherMask's); NULL: one mask for any length.
    const uint8_t (*a36CipherChunkMasks)[16];
    int a36CipherChunks;
    // Where A36Interface::dispatchAction hands a reply to the waiting
    // command: it clears the wait first, then writes the reply's function,
    // count and result, which a waiter on another core can read before
    // they are written (a reply's data then left unread, and the replies
    // after it read at the wrong place). Its seven instructions reordered:
    // the reply, then the wait cleared.
    uint32_t a36ReplyPublish;
    // OIS's LinuxJoyStick::_scanJoys: its evdev capability buffers are
    // smaller than today's kernels fill (the stack smashed). The cabinet's
    // controls are its serial I/O board's: no joystick found.
    uint32_t oisScanJoys;
    // luabind::get_error_callback and lua_tolstring: the game's Lua errors
    // shown (luabind otherwise only asserts).
    uint32_t luabindGetErrorCallback, luaToLString, luaPCall;
    // luabind's throw_error_policy<unsigned char>::handle_error: a failed
    // cast to a byte gives 0 (igsA36.c's lampOff), not luabind's assert.
    uint32_t luabindByteCastFailed;
    // SD3 cabinet: GameIO initialise/read, VR setFFB, GameIO setBound.
    uint32_t serialIo[4];
    // Power Truck: a four-position gear lever (G1-G4, held) in place of
    // SD3's gear up and down buttons, which here move it.
    int serialIoGearLever;
    // Ogre's plugin list as the game opens it, naming the cabinet's
    // directory: the game reads a copy naming libso/ (NULL: as it is).
    const char *ogrePlugins;
    // RenderSystem_GL 1.6.1: uninitialised auto-texture flag and matrix.
    // Byte flag at this offset, followed by an aligned float[16] at +4.
    uint32_t ogreAutoTextureMatrix;
    // The game's ArchiveManager::unload calls left out (igs.c's
    // ogreKeepArchive).
    int ogreKeepArchives;
} IgsGame;

const IgsGame *igsGetGame(uint32_t crc32);
const IgsGame *igsGetGameByFileCrc(uint32_t fileCrc32);

#endif // IGS_GAME_H
