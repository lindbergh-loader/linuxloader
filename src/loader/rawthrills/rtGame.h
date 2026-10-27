#ifndef RT_GAME_H
#define RT_GAME_H

#include <stddef.h>
#include <stdint.h>

struct JVSIO;

// One slot of an envelope runtime GOT: slot index -> symbol name.
typedef struct
{
    int slot;
    const char *name;
} RtEnvelopeImport;

// One entry of the game's own GOT: absolute slot address -> symbol@version.
typedef struct
{
    uint32_t slot;
    const char *name;
    const char *version;
} RtGameImport;

// An entry of the engine's input map: source id -> game input id.
typedef struct
{
    uint16_t src;
    uint16_t dst;
} RtMap;

// Input map entries to drop: source and destination id ranges.
typedef struct
{
    uint16_t srcMin, srcMax;
    uint16_t dstMin, dstMax;
} RtMapFilter;

// Evdev input (the loader's JVS state) fed to an engine I/O slot each frame.
typedef enum
{
    RT_IO_END,
    RT_IO_ANALOG,          // source: JVS analogue channel
    RT_IO_ANALOG_INVERTED, // same, reversed
    RT_IO_ANALOG_SWITCH,   // source: JVS analogue channel, pressed past half travel
    RT_IO_SWITCH,          // source: JVS switch bits of player (SYSTEM for TEST)
    RT_IO_COIN,            // player: coin slot (0, 1)
    // Desktop keys only (the slot is left to its other inputs with evdev):
    // an analog slot from two switches of the player, source the "low"
    // bit, then the "high" one shifted by 16 (RT_KEYS_ANALOG): at the end
    // of the range while one is held, else in the middle; with no low
    // switch, at 0 until the high one is held (a pedal).
    RT_IO_SWITCH_ANALOG,
} RtIoInputType;

#define RT_KEYS_ANALOG(low, high) ((uint32_t)(low) | ((uint32_t)(high) << 16))

typedef struct
{
    RtIoInputType type;
    int player;
    uint16_t io;
    uint32_t source;
} RtIoInput;

// A light gun of a JAMMA board (g3 engine): evdev position (JVS analogue
// channels) of a player, whose BUTTON_1 pulls the trigger and BUTTON_3
// reloads (a shot off the screen), and the game's event for a shot.
// A mounted gun of the UMC board: evdev position (JVS analogue channels) of
// a player, whose BUTTON_1 is the trigger and BUTTON_2 pulls the reload
// lever, and its UMC channels.
typedef struct RtUmcGun
{
    int player;
    int xChannel, yChannel;
    int umcX, umcY, umcLever; // analog
    int umcTrigger;           // switch
} RtUmcGun;

typedef struct RtHeapPointer
{
    uint32_t address;
    uint32_t size;
} RtHeapPointer;

typedef struct
{
    int player;
    int xChannel, yChannel;
    uint16_t shotEvent; // 0: shots come from the IR gun manager's trigger
    uint16_t triggerEvent; // posted just before the shot (0: none)
    uint16_t releaseEvent; // posted when the trigger is let go (0: none)
} RtJammaGun;

// A directory the cabinet links elsewhere (symlinks that copies of the game
// may have lost): paths relative to rootPath.
typedef struct
{
    const char *from;
    const char *to;
} RtPathAlias;

// A function of a dump that does not export it: name -> address.
typedef struct
{
    const char *name;
    uint32_t address;
} RtSymbol;

// A game function replaced by one returning a constant (0 or 1).
typedef struct
{
    const char *name;
    int value;
} RtStub;

// Everything the loader needs to know about one Raw Thrills game dump.
typedef struct RtGame
{
    uint32_t crc32;    // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the dumped ELF file, as the launcher sees it (0: run as is)
    uint32_t altFileCrc32; // another copy of the same ELF (a resolution patch), run the same way (0: none)

    // Rockey boot envelope: XOR-encrypted code beginning at the real entry.
    // Decrypted in the launcher's cached copy; the original remains intact.
    uint32_t rockeyTextStart, rockeyTextSize, rockeyTextXor;

    // Import tables to rebuild (see rtDump.c).
    uint32_t envelopeGot;
    const RtEnvelopeImport *envelopeImports;
    size_t envelopeImportCount;
    int envelopeSelfSlot; // slot pointing back into the envelope, or -1
    uint32_t envelopeSelfTarget;
    const RtGameImport *gameImports;
    size_t gameImportCount;
    const char *const *extraLibs; // libraries the game uses but does not list in DT_NEEDED

    // Functions hooked by name that the dump does not export (terminated by
    // a NULL name).
    const RtSymbol *symbols;

    // HASP feature the game logs in to (HASP_DEFAULT_FID 0, or the legacy
    // program-number feature 0xffff0000); every other login fails.
    uint32_t haspFeature;
    uint32_t haspMemoryFileId; // HASP file holding the dongle memory the game reads
    // Factory setup flag to seed to 1 in synthesized memory (0: none).
    // Recorded and saved dongle memory take precedence over this default.
    uint32_t haspFactorySetupOffset;
    // Some dumps call a file-backed replacement of hasp_read, hasp_write and
    // hasp_get_sessioninfo (dongle memory under /pp) instead of the HASP
    // API: their addresses, emulated like the API functions (0: none).
    uint32_t haspReadPatch;
    uint32_t haspWritePatch;
    uint32_t haspSessionInfoPatch;
    // Directory, relative to the game directory, of recorded dongle answers
    // (TeknoParrot's "hasp" folder), for games whose data needs the dongle's
    // AES: hasp_encrypt/hasp_decrypt results, each in a file named by the
    // first haspAnswerKeySize bytes of the input in hex (0: 16), and the
    // dongle memory the game reads (hhl_mem.dmp). NULL: none.
    const char *haspAnswers;
    int haspAnswerKeySize;

    // A library statically linked into the game whose dumped state is
    // unusable: the game's exported functions with this prefix are sent to
    // the system's copy instead (NULL: none).
    const char *replacedLib;
    const char *replacedLibPrefix;
    // The game's .bss, captured with its runtime state (the envelope's own
    // data follows it): cleared, except what ld.so copies in (R_386_COPY).
    uint32_t bssStart, bssEnd;
    // Pointers to the cabinet's heap left in the dump's data, given a new
    // zeroed block (size bytes) instead.
    const struct RtHeapPointer *staleHeapPointers;

    // Stack size of the game's threads (0: the launcher's stack limit, 64
    // MB, as for the main thread).
    uint32_t threadStackSize;

    // Cabinet hardware without an emulation: functions stubbed out.
    const RtStub *stubs;
    // Game-specific hooks, installed with the I/O ones (rtInstallIo), and
    // imports the game gets in place of the real ones (NULL for the others).
    void (*install)(const struct RtGame *game);
    void *(*override)(const char *name);

    // Cabinet file layout: the game expects to live in this directory.
    const char *rootPath;
    // Directory holding encrypted scripts, relative to rootPath, and its
    // decrypted copy relative to the game directory (NULL if none).
    const char *encryptedScripts;
    const char *decryptedScripts;
    const RtPathAlias *pathAliases;
    // Cabinet directories outside rootPath: absolute path -> path relative
    // to the game directory.
    const RtPathAlias *rootAliases;
    const char *workDir; // working directory the game expects, relative to the game directory (NULL: itself)

    // Engine input (g5 engine). No GameInputMaps prologue: input hooks off.
    // After GameInputMaps, devMapFilters entries are dropped and extraMaps
    // added; with evdev input, evdevMapFilters entries are dropped too,
    // evdevMaps added, and ioInputs fed to the engine's I/O slots each frame.
    const char *gameInputMapsSymbol; // NULL: "GameInputMaps" (C games; C++ ones are mangled)
    uint32_t inputMap;           // InputMap[][2] (src, dst)
    uint32_t inputMapCount;      // number of entries
    uint32_t devInputFlag;       // gCLArgs field enabling the developer mouse/keyboard map, 0: off
    size_t gameInputMapsPrologue; // bytes of GameInputMaps to relocate into a trampoline
    // Games that register their maps inline (in main: Big Buck HD Wild)
    // rather than in a GameInputMaps: InputAddMap itself is hooked instead,
    // dropping the filtered entries as they come and adding the extra ones
    // at its first call (0: GameInputMaps is hooked).
    size_t inputAddMapPrologue;
    // I/O backend loop the evdev input is fed after: it must be the last one
    // to update the slots used (NULL: "io_sdl_loop").
    const char *ioLoopSymbol;
    size_t ioLoopPrologue; // bytes of it to relocate into a trampoline
    const RtMapFilter *devMapFilters;
    const RtMap *extraMaps;
    const RtMapFilter *evdevMapFilters;
    const RtMap *evdevMaps;
    const RtIoInput *ioInputs;
    // Set the analog slots' raw range to the evdev one (else the game's
    // calibration): 1 once, 2 every frame (games reapplying a saved
    // calibration over it, MotoGP's bike).
    int ioRawRange;
    // Feed ioInputs outside evdev mode too, from the desktop (mouse on P1's
    // gun, keys on the buttons, see rtIo.c): games without a developer map.
    int ioDesktop;
    // IR tracked guns (the cmgr camera manager): without the camera the
    // guns are inactive, and the tracking backend flags their slots
    // offscreen every frame. Answered from the loader's gun input instead
    // (guns 0 and 1: ANALOGUE_1/2 and 3/4): whether a gun is active (gun);
    // the camera's raw coordinates of the gun (gun, float *x, float *y),
    // from which the game's calibration makes the aim: in a 800x600 camera
    // space, -1 off the screen (two getters: filtered and not); and its
    // buttons (gun, button): a count of the button's transitions, odd while
    // held (0: the trigger, BUTTON_1; 1: the pump, BUTTON_2; 3: a third
    // button, BUTTON_3). NULL: none.
    const char *irGunActiveSymbol;
    const char *irGunRawSymbols[2];
    const char *irGunButtonSymbol;
    // Its calibrated aim of a gun (gun, float *x, float *y), which io_irtrack
    // puts in the gun's X and Y slots (0x181.. , 2 per gun), in their raw
    // range: answered from the loader's input, the game's calibration
    // bypassed (NULL: its calibration applies to the raw coordinates).
    const char *irGunAimSymbol;
    // Games whose IR raw X range runs from right to left.
    int irGunInvertX;
    // Absolute lightgun axes needing the opposite orientation to desktop
    // and relative mouse input. Applies only to evdev ABS mappings.
    int irAbsGunInvertX;
    int irAbsGunInvertY;
    // io_irtrack's gun slots: the address of its pointer to them, the bytes
    // a gun's take and the offset of its first button's (0x2c bytes each:
    // trigger, pump, then the third button). The games invert their buttons
    // there (bit 0 of a slot's flags): the counts follow (0: none).
    uint32_t irGunSlots;
    uint32_t irGunSlotStride;
    uint32_t irGunButtonSlot;
    // Games reading the RIO board through its API (RIO_SW_State/RIO_SW_Count)
    // instead of the g5 engine: evdev input by switch number (RtIoInput.io).
    const RtIoInput *rioSwitches;
    // RIO games without keys of their own: desktop keyboard on the switches
    // when not in evdev mode (see rtIo.c).
    int rioDesktopKeys;
    // RIO_SW_Count is a transition count (odd while held), rather than
    // a count of presses, for games reading its parity as switch state.
    int rioTransitionCounts;
    // Games that take the board's switch reports themselves (g6 engine):
    // its report handler, (switch, 3, transition count, time), called from
    // a hook of ioLoopSymbol (the engine's io_rio_loop(dt)) for each switch
    // that changed.
    const char *rioEventSymbol;
    // The board's analog inputs read through RIO_ADC_Val(adc) (RtIoInput.io):
    // RT_IO_ANALOG, a JVS analogue channel (evdev, or the desktop mouse);
    // RT_IO_SWITCH_ANALOG, the player's two switches (RT_KEYS_ANALOG(low,
    // high)) moving it towards either end while held (desktop keys).
    const RtIoInput *rioAnalogs;
    // UMC board (g6 engine, mounted crossbows): its connection check and
    // report read (int[16]: 8 switch transition counts, then 8 analog
    // values), answered for the umcGuns.
    const char *umcConnectedSymbol;
    const char *umcStatusSymbol; // device status (0: connected), for the attract mode's check
    const char *umcReadSymbol;
    const struct RtUmcGun *umcGuns;
    int umcGunCount;

    // Force feedback wheel (driving games, see rtFfb.c): the game's motor
    // call Wheel_Set(float force), and its wheel state: whether a race is
    // on (int32, 1), the force the menus ask for (float), and the flag
    // turning its wheel effects on (byte). NULL: none.
    const char *wheelSetSymbol;
    uint32_t wheelInRace;
    uint32_t wheelMenuForce;
    uint32_t wheelEffectsFlag;

    // Video mode (g5 engine): gCLArgs width, height (int) and aspect (float),
    // set from the command line by ParseCommandLineArgs.
    const char *parseArgsSymbol; // NULL: "ParseCommandLineArgs"
    uint32_t resolution;          // width, then height
    uint32_t aspect;              // 0: none
    // Vertical games: flag drawing the picture rotated for a monitor turned
    // on its side, set from [Display] ROTATE_VERTICAL (0: none).
    uint32_t rotateFlag;
    // Flag opening the window fullscreen, set from [Display] FULLSCREEN (0: none).
    uint32_t fullscreenFlag;
    // Games drawing in their monitor's pixels (landscape size, width then
    // height, 0: none) through a set_ortho(w, h) setting viewport and
    // projection for the window: the picture is scaled to fit the window.
    uint32_t monitorSize;
    // Games drawing upright at the size they are given, portrait (Pink
    // Panther Jewel Heist): with a bezel, they are given its hole's size and
    // the window the screen's ([Display] WIDTH/HEIGHT), and the frame is
    // fitted in the hole (see frameScale.h).
    int bezelFrame;
    // The executable's own glXGetProcAddress imports get the window
    // scaling's GL wrappers (games linking SDL 1.2 dynamically, see rtDump.c).
    int exeGlxGetProcAddress;
    // Games rendering at their own size whatever their window's (Jurassic
    // Park): [Display] WIDTH/HEIGHT, or the cabinet's shape (layoutWidth x
    // layoutHeight) at that width on a narrower screen, fitted in the window
    // (frameScaleSetFrame: stretched; KEEP_ASPECT_RATIO is only for the
    // frameAspect games).
    int fixedFrame;
    // Games drawing their picture turned for a monitor on its side, in a
    // landscape frame of their own size (Tippin' Bloks): the frame is turned
    // upright in the window (1: a quarter turn counterclockwise, -1:
    // clockwise), the bezel around it, unless [Display] ROTATE_VERTICAL says
    // the monitor is on its side (0: none). The game is given that size
    // (sizeArgument), its only one, whatever [Display] says.
    int frameTurn;
    uint16_t frameTurnWidth, frameTurnHeight;
    // Games opening their window in GLUT's game mode at the cabinet's
    // resolution: a window at [Display]'s size instead (see rtFrame.c).
    int glutGameModeWindow;
    // Games made for a 4:3 monitor (g3 engine, width then height, 0:
    // none): their mode at that shape, the screen's height, and their
    // full-window viewports in the middle of the window at it (black bars
    // at the sides); the guns aim in the picture. [Display]
    // KEEP_ASPECT_RATIO 0 stretches them to the screen instead.
    uint8_t frameAspect[2];
    // Sets the projection: (width, height[, rotate]).
    // Linked-in SDL 1.2's SDL_SetVideoMode: fullscreen with [Display]
    // FULLSCREEN (the game asks for a window).
    const char *videoModeSymbol;
    size_t videoModePrologue;
    // Games whose render size is a local of main, set from the command line
    // only (Big Buck HD Wild): the argument the launcher adds, a printf format
    // taking [Display] WIDTH and HEIGHT (NULL: none).
    const char *sizeArgument;
    const char *orthoSymbol;
    size_t orthoPrologue;
    size_t parseArgsPrologue; // bytes of ParseCommandLineArgs to relocate into a trampoline
    // g5 engine 2D layer (HUD, menus): laid out in the cabinet's pixels,
    // placed from the screen's size (VidDisp's 16-bit screenW, screenH at
    // screenSize) and projected at the window's (WndCur), so on a bigger
    // screen it keeps the cabinet's pixel size. Those reads are answered
    // with the cabinet's height (layoutHeight) at the screen's shape, but
    // in the functions sizing viewports and render targets (layoutRealSize,
    // the renderer's), and the default 2D camera (VidSetDflt2DCamDim) gets
    // that size: the 2D layer then spans the screen. 0: none.
    uint32_t screenSize;
    uint16_t layoutWidth, layoutHeight;
    const char *const *layoutRealSize;
    size_t dflt2DCamPrologue; // bytes of VidSetDflt2DCamDim to relocate into a trampoline

    // Video mode (g3 engine): the mode is an entry of a table, selected by
    // index (setMode), and the selected entry is pointed to by modePointer;
    // its 16-bit width and height (+4, +6) are set from [Display]. The
    // window is opened by windowOpen(width, height, fullscreen) with glut:
    // its fullscreen switches the display mode, which is replaced by a
    // window, made fullscreen with [Display] FULLSCREEN.
    const char *setModeSymbol;
    size_t setModePrologue;
    uint32_t modePointer;
    const char *windowOpenSymbol;
    size_t windowOpenPrologue;
    // Keep the game window as it is with [Display] FULLSCREEN (glut's
    // fullscreen stalls Terminator Salvation's frame timers at the start of a
    // level on NVIDIA): a window of the screen's size fills it anyway on a
    // desktop without window manager (Batocera).
    int glutWindowOnly;

    // JAMMA I/O board (g3 engine): its switch counters are emulated and
    // handed to the game's requests for them (jammaOpSymbol, the board API);
    // the game's poll (jammaPollSymbol, run each frame) turns them into
    // events. Switches: io is the board's switch number. Guns: their shots
    // are posted (postEventSymbol(event, data)) as the board's messages
    // would.
    const char *jammaPollSymbol;
    size_t jammaPollPrologue;
    const char *jammaOpSymbol;
    size_t jammaOpPrologue;
    const char *postEventSymbol;
    const RtIoInput *jammaSwitches;
    const RtJammaGun *jammaGuns;
    int jammaGunCount;
    // The IR gun manager's aim of a player's gun (player, float *x,
    // float *y, int), in its gunAimWidth x gunAimHeight camera space (the
    // reticle follows it): answered from the same gun positions.
    // Games reading the guns' position from the board: the board's position
    // event, posted each frame, with the position in its gunWidth x
    // gunHeight space (0: none).
    uint16_t gunPositionEvent;
    int gunWidth, gunHeight;
    // The board's calibration of a gun, which the game's calibration sends
    // it, (gun, x1, y1, x2, y2, tx1, ty1, tx2, ty2): the points shot at two
    // targets and the targets, in the board's space, which the board applies
    // to its positions (0: none); and its mode request, (gun, mode): 2 applies
    // it, 0 (while the game calibrates) gives the raw positions. The game
    // keeps the calibration and sends it at each start.
    uint16_t jammaGunCalibrateOp;
    uint16_t jammaGunCalibrationModeOp;
    const char *gunAimSymbol;
    int gunAimWidth, gunAimHeight;
    // Games reading the trigger from the IR gun manager (gun, a count of
    // trigger pulls: the game shoots at the aim when it changes) rather than
    // from the board: answered with the guns' pulls (NULL: none).
    const char *gunTriggerSymbol;
    // The IR guns' own buttons, as the IR gun manager counts them (gun,
    // button 0..4): the game posts an event each time a count changes, one
    // count for a button's presses and another for its releases. The JVS
    // switch of the gun's player whose presses (gunButtonPresses) or
    // releases (gunButtonReleases) each count follows (0: none, the board
    // has it). NULL: none.
    const char *gunButtonSymbol;
    int gunButtonPresses[5];
    int gunButtonReleases[5];
    // BUTTON_3 fires off the screen (a reload), for guns without a third
    // button of their own.
    int offScreenButton;
    // Deal or No Deal's button panel on the parallel port (see rtDond.c).
    int lptPanel;
    // The game window's keys (glut: key, special for glut's special keys,
    // held), seen before the JAMMA board's (rtJamma.c): 1 when taken.
    int (*desktopKey)(int key, int special, int held);
    // Drawn on each frame before the swap, in the window's drawable area (x,
    // y from its bottom left, width, height), with the GL entry points
    // loaded (NULL: nothing).
    void (*frameDraw)(int x, int y, int width, int height);
    // Called each frame once the I/O slots are fed (evdev input), with the
    // input they came from: game-specific devices (NULL: none).
    void (*ioFrame)(struct JVSIO *io);
} RtGame;

// Wheel of Fortune's own hooks (rtWof.c), its descriptor's install. Weak:
// the launcher links the descriptors without them (NULL there, never called).
void rtWofInstall(const RtGame *game) __attribute__((weak));
// Tippin' Bloks' controller (rtTb.c), the same way.
void rtTbInstall(const RtGame *game) __attribute__((weak));
// Target: Terror's files, JAMMA board, window, clock and crosshairs (rtTt.c),
// the same way.
void rtTtInstall(const RtGame *game) __attribute__((weak));
void *rtTtOverride(const char *name) __attribute__((weak));
void rtTtFrameDraw(int x, int y, int width, int height) __attribute__((weak));
// Angry Birds Arcade's touch frame (rtAb.c), the same way.
void rtAbInstall(const RtGame *game) __attribute__((weak));
void rtAbIoFrame(struct JVSIO *io) __attribute__((weak));
// Ghostbusters' IR backend and optional shader backup (rtGb.c).
void rtGbInstall(const RtGame *game) __attribute__((weak));
void rtSifInstall(const RtGame *game) __attribute__((weak));
void rtWlpInstall(const RtGame *game) __attribute__((weak));

// Deal or No Deal's panel and dongle (rtDond.c), the same way.
void *rtDondOverride(const char *name) __attribute__((weak));
void rtDondInstall(const RtGame *game) __attribute__((weak));
void rtDondIoFrame(struct JVSIO *io) __attribute__((weak));
int rtDondDesktopKey(int key, int special, int held) __attribute__((weak));
void rtDondFrameDraw(int x, int y, int width, int height) __attribute__((weak));

// MotoGP's wheel force feedback (rtMgpFfb.c), the same way.
void rtMgpFfbInstall(void) __attribute__((weak));

const RtGame *rtGetGame(uint32_t crc32);
const RtGame *rtGetGameByFileCrc(uint32_t fileCrc32);

// Launcher: run a patched copy of a Raw Thrills dump (see rtLaunch.c).
// configPath: the loader's configuration, for the games taking their video
// mode on the command line (sizeArgument).
void rtPrepareCommand(char *command, size_t size, uint32_t fileCrc32, const char *configPath);

#endif // RT_GAME_H
