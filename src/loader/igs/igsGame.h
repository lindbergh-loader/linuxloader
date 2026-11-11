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
    // (MuziBox: a fifth, the wait for the uploaded firmware's start)
    uint32_t a36Loader[5];
    // The library holding the upload and the dongle's API (Crazy Shoot's
    // Unity plugin, dlopen'd): a36Loader's five and haspApi's six found
    // there by name when it loads (0: the addresses above).
    const char *a36Library;
    // The game draws through an imported SDL 1.2 (Fish Hunter SKL 2,
    // MuziBox), not Ogre's GLX window.
    int a36Sdl;
    // Its picture turned for a monitor on its side (MuziBox's portrait
    // cabinet): the quarter turn back (graphics/frameScale.h; 0: none), unless
    // [Display] ROTATE_VERTICAL (the monitor is turned).
    int a36FrameTurn;
    // Its connections out of the machine refused (MuziBox's online service).
    int a36Offline;
    // The E2100's DIP switches (a byte at 0x03900000, a switch ON reading
    // 0): the ones ON, bit n switch n + 1 (IGS_DIPS=hex for tests).
    int a36E2100Dips;
    // The timers' master clock (Hz) when not the E3000's 20 MHz (the
    // E2100's: 65 MHz, as its firmware's baud rates have it). 0: the default.
    uint32_t a36TimerClock;
    // The board's time kept to the wall clock when the emulation falls
    // behind (a firmware polling in its main loop, with no idle loop).
    int a36CatchUp;
    // The RAM at 0x20000000 when more than 512 KiB (Hero of Robots': 16 MiB).
    uint32_t a36RamSize;
    // The external SRAM at 0x50000000 when more than 512 KiB (Hero of
    // Robots': its firmware copies past 1 MiB).
    uint32_t a36ExtSramSize;
    // A byte's time on its control board's USART (cycles; 0: igsA36.c's
    // STM32_BYTE, faster than the line): Hero of Robots' I/O board at its
    // ~116 kbaud, as its firmware polls it as fast as it answers.
    uint32_t a36Stm32ByteCycles;
    // The game asks for the screen's size (SDL_GetVideoInfo's), and draws
    // its frame (frameWidth x frameHeight) at the bottom left of it: given
    // its frame's size.
    int a36AsksScreen;
    // The game's Game::iIgs036_AddSendCmd (Hero of Robots): the swaps
    // unsynced from the display until its first scene's command 2 (its
    // loading screen draws a frame after each step). 0: none.
    uint32_t loadingEnds;
    // GL ES "precision ...;" statements in the game's shaders, which desktop
    // GLSL 1.10 rejects (Scratch's, NVIDIA's driver let pass): blanked. 0:
    // none.
    uint32_t glesPrecision[2];
    // The PC's serial ports (a lamp board the game only writes to) on
    // /dev/zero, not missing.
    int ttyZero;
    // Its card reader (libcasmcard) stood in for: a reader, no card
    // (igsCard.c).
    int casCard;
    // and the operator's chip in it (CardReaderMgr::hasCard,
    // SmartCard::Authentication): there, with credit. 0: none.
    uint32_t casChip[2];
    // Its serial devices (igsCard.c): RS232::initialize stood in for, the
    // device told by its caller, in CardDispenser's code (the card
    // dispenser) or QRCodeReader's (the cards' QR scanner): first and last
    // addresses.
    uint32_t rs232Initialize, rs232Dispenser[2], rs232Qr[2];
    uint8_t a36Dips;
    // A function made to return 0 (MuziBox's "store name is the default
    // MuziBox" check, which sends every boot to the operator's store setting:
    // its store list, TeknoRaven's, offers no store to pick).
    uint32_t returnZero;
    // The board's internal ROM, when the dump has it (Percussion Master
    // 2008): run from reset, in place of vectors to the external ROM's.
    const char *a36InternalRom;
    // Its check of the external ROM (a checksum, a function returning 1
    // when it holds) at this address, passed: another game's internal ROM
    // (Rock Fever 5 runs Percussion Master 2008's). 0: none.
    // The PLX9030 link of the 2008 cabinets (Percussion Master 2008, Rock
    // Fever 5) in place of /dev/pccard0: the game's statically linked PLX
    // SDK (PlxPciDeviceOpen, PlxPciBarMap, then calls answered as done:
    // chip type, interrupts, register reads and writes, close), and its
    // doorbell (the write to BAR3 + 0xe that interrupts the board).
    // BAR2 is the board's data window (0x30000000), BAR3 its command
    // registers (0x40000000). 0: none.
    uint32_t a36PlxOpen, a36PlxBarMap, a36PlxDone[6], a36PlxDoorbell;
    uint8_t a36PlxDoorbellByte; // the byte it writes
    // Rock Fever 5: its own search for the card (its device location
    // filled: a PLX9030, 1 returned), in place of the SDK's.
    uint32_t a36PlxFind;
    int a36Rf5Services;             // Rock Fever 5: its internal ROM's services, here (rf5Service)
    // The SRAM table of internal-ROM services the firmware calls (0: none).
    uint32_t a36Services;
    // The upload's header as the internal ROM's loader leaves it in SRAM
    // (0x0200f000), which the E2200's firmware checks at start: its project
    // name, its area (0x58) and its ASIC type and version (0x9c: type in
    // the low 5 bits). NULL: none.
    const char *a36Project, *a36ProjectArea;
    uint16_t a36ProjectVersion;
    // The cabinet's STM32 I/O controller (igsStm32.c) on this USART + 1
    // (0: none).
    int a36Stm32Usart;
    // Its pots' raw ranges (igsStm32.h's pots; all 0: the full range).
    uint16_t a36Stm32Pots[4][3];
    int a36Stm32Throttle; // the throttle's pot (VR1 + n)
    // In place of the STM32: the E2200 fish tables' control board (1),
    // MuziBox's I/O board (2), Percussion Master 3's (3; igsStm32.c).
    int a36FishBoard;
    // The FPGA cipher's mask for the game's scripts, when it is not the
    // one its key has (all 0: the key's).
    uint8_t a36CipherMask[16];
    // Masks for keys of the game's own (Percussion Master 3's charts: the
    // ten keys its board hands out), by key: {key, mask}; tried first.
    const uint8_t (*a36CipherKeys)[2][16];
    int a36CipherKeyCount;
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

    // The E2200 (Fish Hunter SKL 2, 2012): the A36 board as above, the game
    // drawing through an imported SDL 1.2, and a HASP HL dongle whose API
    // the game links in (Sentinel's, cdecl): login, logout, read, write and
    // the two buffer transforms, replaced by igsHasp.c (0: none). The
    // dongle's file haspFile holds haspMemory, as the game has it after its
    // transform (the transforms leave buffers as they are).
    // Fish Hunter SKL 2's sound loader passes SDL_RWFromMem a size it reads
    // before its file reader writes it (a stale stack word): replaced, with
    // its slot allocator, file reader (path, &size: the decrypted file) and
    // table of 12-byte entries (the Mix_Chunk at +8). 0: none.
    uint32_t soundLoad, soundSlot, soundRead, soundTable;
    uint32_t haspApi[6];
    int haspFile;
    const uint8_t *haspMemory;
    int haspMemorySize;
} IgsGame;

// haspApi's entries
enum
{
    IGS_HASP_LOGIN,
    IGS_HASP_LOGOUT,
    IGS_HASP_READ,
    IGS_HASP_WRITE,
    IGS_HASP_ENCRYPT,
    IGS_HASP_DECRYPT,
};

const IgsGame *igsGetGame(uint32_t crc32);
const IgsGame *igsGetGameByFileCrc(uint32_t fileCrc32);

#endif // IGS_GAME_H
