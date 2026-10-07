#include "rtGame.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "cruisnblast/cbImports.h"
#include "galagaassault/gaImports.h"
#include "pacman/pmImports.h"
#include "walkingdead/twdImports.h"
#include "jurassicpark/jpImports.h"

// SDL 1.2 key codes (engine input ids of the keyboard).
enum
{
    KEY_BACKSPACE = 8,
    KEY_RETURN = 13,
    KEY_UP = 273,
    KEY_DOWN = 274,
    KEY_F1 = 282,
    KEY_F2 = 283,
    KEY_KP0 = 256, // to KP9 = 265
    KEY_KP_PERIOD = 266,
    KEY_KP_MULTIPLY = 268,
    KEY_KP_ENTER = 271,
};

// ---------------------------------------------------------------------------
// Jurassic Park. Engine ids from g5engine/src/g5/core_input_enums.h.

static const char *const jpExtraLibs[] = {"libNxCooking.so", NULL};

enum
{
    JP_MOUSE_X = 0x144,
    JP_MOUSE_Y = 0x145,
    JP_MOUSE_RIGHT = 0x147,
    JP_GUN0_X = 0x14b,
    JP_GUN0_Y = 0x14c,
    JP_GUN0_TRIGGER0 = 0x14d,
    JP_GUN0_TRIGGER1 = 0x14e,
    JP_GUN1_X = 0x155,
    JP_GUN1_Y = 0x156,
    JP_GUN1_TRIGGER0 = 0x157,
    JP_GUN1_TRIGGER1 = 0x158,
    JP_GUN3_TRIGGER7 = 0x172,
    JP_START0 = 0x173,
    JP_START1 = 0x174,
    JP_SERVICE = 0x179,
    JP_DIAG = 0x17a,
    JP_COIN0 = 0x17b,
    JP_COIN1 = 0x17c,
    JP_MENU_UP = 0x17f,
    JP_MENU_DOWN = 0x180,
    JP_MENU_SELECT = 0x181,
    JP_MENU_CANCEL = 0x182,
    JP_HOVER_X_P1 = 0x183,
    JP_HOVER_Y_P1 = 0x184,
    JP_HOVER_X_P2 = 0x185,
    JP_HOVER_Y_P2 = 0x186,
    JP_ANALOG_SELECT = 0x187,
    JP_ANALOG_SELECT_P1 = 0x188,
    JP_ANALOG_SELECT_P2 = 0x189,
};

// I/O slots: the cabinet board's switches (routed by the game's cabinet map)
// and free ones (hardware not present here) for the guns.
enum
{
    JP_IO_COIN0 = 0x14c,
    JP_IO_COIN1 = 0x14d,
    JP_IO_DIAG = 0x150,
    JP_IO_SERVICE = 0x151,
    JP_IO_START0 = 0x154,
    JP_IO_START1 = 0x155,
    JP_IO_GUN0_X = 0x15f,
    JP_IO_GUN0_Y = 0x160,
    JP_IO_GUN0_TRIGGER0 = 0x163,
    JP_IO_GUN0_TRIGGER1 = 0x164,
    JP_IO_GUN1_TRIGGER0 = 0x166,
    JP_IO_GUN1_TRIGGER1 = 0x167,
    JP_IO_GUN1_X = 0x169,
    JP_IO_GUN1_Y = 0x16a,
};

// The developer map drives all four guns (and P2's trigger) from the mouse:
// keep it on P1.
static const RtMapFilter jpDevMapFilters[] = {
    {JP_MOUSE_X, JP_MOUSE_Y, JP_GUN1_X, JP_GUN3_TRIGGER7},
    {JP_MOUSE_RIGHT, JP_MOUSE_RIGHT, JP_GUN1_X, JP_GUN3_TRIGGER7},
    {JP_MOUSE_RIGHT, JP_MOUSE_RIGHT, JP_ANALOG_SELECT_P2, JP_ANALOG_SELECT_P2},
    {0, 0, 0, 0},
};

static const RtMap jpExtraMaps[] = {
    {JP_MOUSE_RIGHT, JP_GUN0_TRIGGER1},
    {'1', JP_START0},
    {'2', JP_START1},
    {'5', JP_COIN0},
    {'6', JP_COIN1},
    {KEY_F1, JP_SERVICE},
    {KEY_F2, JP_DIAG},
    {KEY_UP, JP_MENU_UP},
    {KEY_DOWN, JP_MENU_DOWN},
    {KEY_RETURN, JP_MENU_SELECT},
    {KEY_BACKSPACE, JP_MENU_CANCEL},
    {0, 0},
};

// Guns come from evdev: the desktop pointer must not drive them too.
static const RtMapFilter jpEvdevMapFilters[] = {{JP_MOUSE_X, JP_MOUSE_RIGHT, 0, 0xffff}, {0, 0, 0, 0}};

static const RtMap jpEvdevMaps[] = {
    {JP_IO_GUN0_X, JP_GUN0_X},
    {JP_IO_GUN0_Y, JP_GUN0_Y},
    {JP_IO_GUN0_X, JP_HOVER_X_P1},
    {JP_IO_GUN0_Y, JP_HOVER_Y_P1},
    {JP_IO_GUN0_TRIGGER0, JP_GUN0_TRIGGER0},
    {JP_IO_GUN0_TRIGGER0, JP_ANALOG_SELECT_P1},
    {JP_IO_GUN0_TRIGGER0, JP_ANALOG_SELECT},
    {JP_IO_GUN0_TRIGGER1, JP_GUN0_TRIGGER1},
    {JP_IO_GUN1_X, JP_GUN1_X},
    {JP_IO_GUN1_Y, JP_GUN1_Y},
    {JP_IO_GUN1_X, JP_HOVER_X_P2},
    {JP_IO_GUN1_Y, JP_HOVER_Y_P2},
    {JP_IO_GUN1_TRIGGER0, JP_GUN1_TRIGGER0},
    {JP_IO_GUN1_TRIGGER0, JP_ANALOG_SELECT_P2},
    {JP_IO_GUN1_TRIGGER0, JP_ANALOG_SELECT},
    {JP_IO_GUN1_TRIGGER1, JP_GUN1_TRIGGER1},
    {0, 0},
};

// ANALOGUE_1/2 and 3/4 are the P1/P2 guns (the cabinet guns report Y from
// the bottom of the screen), PLAYER_n_BUTTON_1/2 their triggers.
static const RtIoInput jpIoInputs[] = {
    {RT_IO_ANALOG, 0, JP_IO_GUN0_X, ANALOGUE_1},
    {RT_IO_ANALOG_INVERTED, 0, JP_IO_GUN0_Y, ANALOGUE_2},
    {RT_IO_ANALOG, 0, JP_IO_GUN1_X, ANALOGUE_3},
    {RT_IO_ANALOG_INVERTED, 0, JP_IO_GUN1_Y, ANALOGUE_4},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_GUN0_TRIGGER0, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_GUN0_TRIGGER1, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_GUN1_TRIGGER0, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_GUN1_TRIGGER1, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, JP_IO_DIAG, BUTTON_TEST},
    {RT_IO_COIN, 0, JP_IO_COIN0, 0},
    {RT_IO_COIN, 1, JP_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};
static const char *const cbExtraLibs[] = {"libNxCooking.so", NULL};

// ---------------------------------------------------------------------------
// Cruis'n Blast. Its core_input_enums.h adds VIEW and TUNES after START3, so
// the cabinet ids differ from Jurassic Park's; the wheel and gas pedal are
// the GUN0 X/Y inputs.

enum
{
    CB_START0 = 0x173,
    CB_VIEW = 0x177,
    CB_TUNES = 0x178,
    CB_VOL_UP = 0x179,
    CB_VOL_DOWN = 0x17a,
    CB_SERVICE = 0x17b,
    CB_DIAG = 0x17c,
    CB_COIN0 = 0x17d,
    CB_COIN1 = 0x17e,
    CB_MENU_UP = 0x19d,
    CB_MENU_DOWN = 0x19e,
    CB_MENU_SELECT = 0x19f,
    CB_MENU_CANCEL = 0x1a0,
};

// The cabinet board's inputs, as routed by the game's cabinet map (the brake
// pedal is a switch, read through START1).
enum
{
    CB_IO_COIN0 = 0x14c,
    CB_IO_COIN1 = 0x14d,
    CB_IO_DIAG = 0x150,
    CB_IO_SERVICE = 0x151,
    CB_IO_VOL_UP = 0x152,   // -> 0x179, 0x19d (also menu up)
    CB_IO_VOL_DOWN = 0x153, // -> 0x17a, 0x19e (also menu down)
    CB_IO_START0 = 0x154,
    CB_IO_BRAKE = 0x155,
    CB_IO_VIEW = 0x156,
    CB_IO_TUNES = 0x159,
    CB_IO_WHEEL = 0x173,
    CB_IO_GAS = 0x174,
};

// The cabinet's keypad, engine ids from the switch test's table of {id,
// name} pairs (SwitchTestKP0 at 0x8a59e48): keys 0..9, then * and #.  The
// game feeds them from the keypad's I/O slots (0x167 on); the desktop numpad
// is mapped to them as for Big Buck HD Wild, with # on the numpad's Enter
// and on its '.' (for keypads without Enter).
enum
{
    CB_NUMPAD0 = 0x191,
    CB_NUMPAD_STAR = 0x19b,
    CB_NUMPAD_HASH = 0x19c,
};

static const RtMap cbExtraMaps[] = {
    {'1', CB_START0},
    {'5', CB_COIN0},
    {'6', CB_COIN1},
    {'v', CB_VIEW},
    {'m', CB_TUNES},
    {KEY_F1, CB_SERVICE},
    {KEY_F2, CB_DIAG},
    // The volume buttons, which also move in the menus, as the cabinet's.
    {KEY_UP, CB_MENU_UP},
    {KEY_UP, CB_VOL_UP},
    {KEY_DOWN, CB_MENU_DOWN},
    {KEY_DOWN, CB_VOL_DOWN},
    {KEY_RETURN, CB_MENU_SELECT},
    {KEY_BACKSPACE, CB_MENU_CANCEL},
    {KEY_KP0 + 0, CB_NUMPAD0 + 0}, {KEY_KP0 + 1, CB_NUMPAD0 + 1}, {KEY_KP0 + 2, CB_NUMPAD0 + 2},
    {KEY_KP0 + 3, CB_NUMPAD0 + 3}, {KEY_KP0 + 4, CB_NUMPAD0 + 4}, {KEY_KP0 + 5, CB_NUMPAD0 + 5},
    {KEY_KP0 + 6, CB_NUMPAD0 + 6}, {KEY_KP0 + 7, CB_NUMPAD0 + 7}, {KEY_KP0 + 8, CB_NUMPAD0 + 8},
    {KEY_KP0 + 9, CB_NUMPAD0 + 9}, {KEY_KP_MULTIPLY, CB_NUMPAD_STAR}, {KEY_KP_ENTER, CB_NUMPAD_HASH},
    {KEY_KP_PERIOD, CB_NUMPAD_HASH},
    {0, 0},
};

// ANALOGUE_1 steers, ANALOGUE_2 is the gas pedal and ANALOGUE_3 the brake
// pedal (or PLAYER_1_BUTTON_1); BUTTON_2 changes the view, BUTTON_3 the music,
// BUTTON_UP/DOWN the volume (the menus' up and down).
static const RtIoInput cbIoInputs[] = {
    {RT_IO_ANALOG, 0, CB_IO_WHEEL, ANALOGUE_1},
    {RT_IO_ANALOG, 0, CB_IO_GAS, ANALOGUE_2},
    {RT_IO_ANALOG_SWITCH, 0, CB_IO_BRAKE, ANALOGUE_3},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_BRAKE, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_VIEW, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_TUNES, BUTTON_3},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, CB_IO_DIAG, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_VOL_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_VOL_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, CB_IO_COIN0, 0},
    {RT_IO_COIN, 1, CB_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

// On the cabinet the game's user data directory links to /pm/pmuser.
static const RtPathAlias cbPathAliases[] = {{"g5/race/pmuser", "pmuser"}, {NULL, NULL}};

// Force feedback wheel driver board on the parallel port (its motor,
// Wheel_Set, goes to the loader's force feedback: see rtFfb.c).
static const RtStub cbStubs[] = {
    {"Wheel_Init", 0},       {"Wheel_EnablePWM", 0},   {"Wheel_DisablePWM", 0}, {"Wheel_Shutdown", 0},
    {"Wheel_EnableWDT", 0},  {"Wheel_DisableWDT", 0},  {"Wheel_TwiddleWDT", 0},
    {"Wheel_SetData", 0},    {"Wheel_SetControl", 0},  {NULL, 0},
};

// The wheel's state (g_ffwheel, 0x8babba0): the force the menus ask for,
// and whether a race is on; and the flag turning the wheel effects on
// (bumps, crashes, off-road), which the game only clears.
enum
{
    CB_WHEEL_MENU_FORCE = 0x08babbac,
    CB_WHEEL_IN_RACE = 0x08babbc0,
    CB_WHEEL_EFFECTS = 0x09c28504,
};

// ---------------------------------------------------------------------------
// Galaga Assault (statically linked SDL 2.0.3, not the g5 engine).

// RIO switch numbers (from the switch test).
enum
{
    GA_SW_COIN1 = 0x00,
    GA_SW_COIN2 = 0x01,
    GA_SW_TEST = 0x03,
    GA_SW_SERVICE = 0x04,
    GA_SW_VOL_UP = 0x05,
    GA_SW_VOL_DOWN = 0x06,
    GA_SW_START_FIRE = 0x07,
    GA_SW_UP = 0x0d,
    GA_SW_DOWN = 0x0e,
    GA_SW_LEFT = 0x0f,
    GA_SW_RIGHT = 0x10,
};

// One button starts and fires: START or BUTTON_1.
static const RtIoInput gaRioSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, GA_SW_START_FIRE, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_START_FIRE, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_DOWN, BUTTON_DOWN},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_LEFT, BUTTON_LEFT},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_RIGHT, BUTTON_RIGHT},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, GA_SW_TEST, BUTTON_TEST},
    {RT_IO_COIN, 0, GA_SW_COIN1, 0},
    {RT_IO_COIN, 1, GA_SW_COIN2, 0},
    {RT_IO_END, 0, 0, 0},
};

// The RIO board is reported connected.
static const RtStub gaStubs[] = {{"RIO_Connected", 0}, {NULL, 0}};

// ---------------------------------------------------------------------------
// The Walking Dead (g6 engine; the dump exports no game function), v1.05.
// Found from the HASP library's trace strings, by matching the dongle and
// RIO layers against the g5 games and Galaga Assault, and from the engine's
// I/O backends. Its dongle secrets (four calls at startup) are recorded in
// its "hasp" folder; the RIO board's switches arrive as the engine's switch
// reports, and the mounted crossbows through its UMC board backend.

static const RtSymbol twdSymbols[] = {
    {"hasp_login", 0x0837bbc0},
    {"hasp_logout", 0x0837bc60},
    {"hasp_read", 0x0837c9b8},
    {"hasp_write", 0x0837ca84},
    {"hasp_get_sessioninfo", 0x0837c740},
    {"hasp_encrypt", 0x0837bd4c},
    {"hasp_decrypt", 0x0837be38},
    {"DongleEncrypt", 0x081d7200},
    {"DongleDecrypt", 0x081d7380},
    // The RIO API, matched against Galaga Assault's (19 switches).
    {"RIO_Connected", 0x081cf02e},
    {"RIO_SW_State", 0x081cf3a9},
    {"RIO_SW_Count", 0x081cf428},
    // The engine's board backend: its loop, and its switch report handler.
    {"io_rio_loop", 0x081ca37d},
    {"io_rio_switch_report", 0x081ca608},
    // The engine's UMC backend (mounted crossbows): connection check (0:
    // connected) and report read.
    {"umc_check_connection", 0x081cb73b},
    {"umc_device_status", 0x081cb78f},
    {"umc_read_report", 0x081cb7ef},
    // Linked-in SDL 1.2.
    {"SDL_SetVideoMode", 0x081f80b0},
    // The g5 engine's command line parser (gCLArgs: 1360x768 by default).
    {"ParseCommandLineArgs", 0x0807d8c0},
    {NULL, 0},
};

static const RtHeapPointer twdStaleHeapPointers[] = {{0x0900efcc, 64}, {0, 0}};

// Crossbows: P1 on ANALOGUE_1/2, P2 on ANALOGUE_3/4. The game's input maps
// for the mounted guns: UMC pots 4/5 P1 x/y, 6/7 P2, 2/3 the reload levers,
// switches 4/6 the triggers.
static const RtUmcGun twdGuns[] = {
    {PLAYER_1, ANALOGUE_1, ANALOGUE_2, 4, 5, 2, 4},
    {PLAYER_2, ANALOGUE_3, ANALOGUE_4, 6, 7, 3, 6},
};

// The file check hashes the executable too: the original, not the dump.
static const RtPathAlias twdPathAliases[] = {{"game", "game_ori"}, {NULL, NULL}};

// Galaga Assault's switch numbers, and player 2's start after player 1's.
static const RtIoInput twdRioSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, GA_SW_START_FIRE, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, GA_SW_START_FIRE + 1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, GA_SW_TEST, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_VOL_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_VOL_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, GA_SW_COIN1, 0},
    {RT_IO_COIN, 1, GA_SW_COIN2, 0},
    {RT_IO_END, 0, 0, 0},
};

// ---------------------------------------------------------------------------
// Terminator Salvation (g3 engine), v01.25.00. Not a dump: a stripped but
// normally linked binary, with the HASP HL library linked in statically.
// Functions found from the game's call sites and the HASP API's argument
// checks. The data files are encrypted with keys the dongle encrypts: its
// answers are read from TeknoParrot's recording ("hasp" folder).

static const RtSymbol t4Symbols[] = {
    {"hasp_login", 0x083bbe60},
    {"hasp_logout", 0x083bbe00},
    {"hasp_read", 0x083bac80},
    {"hasp_write", 0x083bad30},
    {"hasp_get_sessioninfo", 0x083bae80},
    {"hasp_free", 0x083baab0},
    {"hasp_encrypt", 0x083bbd60},
    {"hasp_decrypt", 0x083bbce0},
    // Thread keeping the dongle busy with random encryptions.
    {"DongleNoise", 0x080ac230},
    // Forks a tracer: the game runs as a child the parent ptraces, so that
    // no debugger can attach. Returns 1 in that child.
    {"TracerGuard", 0x082f2eda},
    // Boot-time check of the data files against their stored checksums.
    // Game copies ship a re-encrypted, fixed eshaders/include/frag_shadmap.gls
    // (the original does not compile on current drivers), which fails it
    // and stops the game on "Game file errors detected".
    {"DiagCheckAllFiles", 0x08190960},
    // Online licensing: sets a record's lockout reason (offline too long,
    // clock error, account delinquent, on too long without connection, not
    // registered) from the operator and unit registration the network
    // provides (op.aud, GameUnit.aud); a lockout stops the game on "Please
    // stand by". Returns 1 when the record has none.
    {"LicenseLockout", 0x081adad0},
    // Whether a player's light gun is connected (the IR gun manager found
    // it on a USB serial port): players join only then.
    {"GunConnected", 0x08159f00},
    // The IR gun manager's aim of a player's gun (reticle, ...), and whether
    // it lost the gun's signal (the aim is then off the screen).
    {"GunAim", 0x0842e488},
    {"GunNoSignal", 0x0842e476},
    // Its count of a gun's own button (gun, button 0..4), read each frame
    // by the gun input (0x815b0b0), which posts an event when one changes
    // (gun 0: 0x12 to 0x15 for buttons 1 to 4, gun 1: 0x2a to 0x2d).
    {"GunButton", 0x0843a180},
    // Input: the JAMMA board's poll, run each frame, the board API
    // (request, ...) and the input event queue (event, data).
    {"JammaPoll", 0x08063630},
    {"JammaOp", 0x081d70a3},
    {"PostInputEvent", 0x08062320},
    // Video: select the mode table entry, open the glut window.
    {"SetVideoMode", 0x080c6fc0},
    {"OpenWindow", 0x080c48b0},
    {NULL, 0},
};

// JAMMA board switches, numbered as in the games' switch table (which maps
// them to input events; the same in Big Buck World), and gun events.
enum
{
    T4_GUN0_TRIGGER = 1,
    T4_GUN1_TRIGGER = 2,
    T4_GUN0_RELOAD = 3,
    T4_GUN1_RELOAD = 4,
    T4_START0 = 5,
    T4_START1 = 6,
    T4_COIN0 = 7,
    T4_COIN1 = 8,
    T4_SERVICE = 10,
    T4_TEST = 11,
    T4_VOLUME_UP = 12, // also moves in the test menus
    T4_VOLUME_DOWN = 13,
    // Light gun board messages: a shot.
    T4_GUN0_SHOT = 0x10,
    T4_GUN1_SHOT = 0x28,
};

static const RtIoInput jammaSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_TRIGGER, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_TRIGGER, BUTTON_3}, // a shot off the screen
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_TRIGGER, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_TRIGGER, BUTTON_3},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, T4_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, T4_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, T4_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, T4_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, T4_TEST, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, T4_VOLUME_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, T4_VOLUME_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, T4_COIN0, 0},
    {RT_IO_COIN, 1, T4_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

// Terminator Salvation's guns have a third button of their own, the
// grenade, which the board does not carry: the IR gun manager counts the
// guns' buttons (GunButton), a count each for a button's presses and its
// releases, and the game posts an event when one changes: buttons 1 and 2
// the pump's press and release (events 0x12 and 0x13, as the board's pump
// switch), 3 and 4 the grenade's (0x14, 0x15). So BUTTON_3 is the grenade
// here, not a shot off the screen.
static const RtIoInput t4Switches[] = {
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_TRIGGER, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_TRIGGER, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, T4_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, T4_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, T4_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, T4_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, T4_TEST, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, T4_VOLUME_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, T4_VOLUME_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, T4_COIN0, 0},
    {RT_IO_COIN, 1, T4_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

static const RtJammaGun t4Guns[] = {
    {PLAYER_1, ANALOGUE_1, ANALOGUE_2, T4_GUN0_SHOT, 0, T4_GUN0_SHOT + 1},
    {PLAYER_2, ANALOGUE_3, ANALOGUE_4, T4_GUN1_SHOT, 0, T4_GUN1_SHOT + 1},
};

static const RtStub t4Stubs[] = {{"TracerGuard", 1}, {"DiagCheckAllFiles", 0}, {"LicenseLockout", 1}, {"GunConnected", 1}, {"GunNoSignal", 0}, {NULL, 0}};

static const RtPathAlias t4RootAliases[] = {{"/T4User", "T4User"}, {NULL, NULL}};

// ---------------------------------------------------------------------------
// Big Buck World (g3 engine), v1.20: built like Terminator Salvation, with
// its HASP library's trace strings naming the API functions. Its recorded
// dongle answers are named by the first 32 input bytes.

static const RtSymbol bbwSymbols[] = {
    {"hasp_login", 0x0839d3d0},
    {"hasp_logout", 0x0839d470},
    {"hasp_encrypt", 0x0839d55c},
    {"hasp_decrypt", 0x0839d648},
    {"hasp_free", 0x0839dc8c},
    {"hasp_get_sessioninfo", 0x0839df50},
    {"hasp_read", 0x0839e1c8},
    {"hasp_write", 0x0839e294},
    {"TracerGuard", 0x082dea6a},
    // Checked from the main loop: the tracer parent is alive and tracing.
    {"TracerCheck", 0x082ded09},
    {"DiagCheckAllFiles", 0x080ff8c0},
    // Opens the glut window: (width, height, fullscreen).
    {"OpenWindow", 0x0807fa00},
    // Selects the video mode table entry (as in Terminator Salvation).
    {"SetVideoMode", 0x0808a970},
    // Input: the JAMMA board's poll, run each frame, the board API and the
    // input event queue (event, data).
    {"JammaPoll", 0x080587d0},
    {"JammaOp", 0x08209462},
    {"PostInputEvent", 0x08058cd0},
    // IR gun manager: gun connected, no signal, aim (as Terminator
    // Salvation's).
    {"GunConnected", 0x083d6333},
    {"GunNoSignal", 0x083d861f},
    {"GunAim", 0x083d862f},
    // Its count of a gun's trigger pulls, read with IR guns only: the game
    // shoots at the aim (and posts the board's events for it) when it
    // changes. Not the cabinet type here: kept for reference.
    {"GunTrigger", 0x083d8c58},
    {NULL, 0},
};

// Board guns (the cabinet type the dongle selects): the board reports the
// position (event 0x3f, the gun's number as its id) and the shot, as the
// game's own IR gun path (0x8160d59) turns an IR shot into them: 0x12 then
// 0x0e for the first gun, 0x14 then 0x24 for the second (the board's
// handler, 0x80597f0, takes its shot events from 0x8529e18: 0x0e, 0x10,
// 0x24, 0x26). Nothing on the trigger's release: 0x0f is the first gun's
// reload.
static const RtJammaGun bbwGuns[] = {
    {PLAYER_1, ANALOGUE_1, ANALOGUE_2, 0x0e, 0x12, 0},
    {PLAYER_2, ANALOGUE_3, ANALOGUE_4, 0x24, 0x14, 0},
};

static const RtStub bbwStubs[] = {
    {"TracerGuard", 1},
    {"TracerCheck", 0},
    {"DiagCheckAllFiles", 0},
    {"GunConnected", 1},
    {"GunNoSignal", 0},
    {NULL, 0},
};

static const RtPathAlias bbwRootAliases[] = {{"/bbwuser", "bbwuser"}, {NULL, NULL}};

// ---------------------------------------------------------------------------
// Wheel of Fortune (g3 engine): like Big Buck World, a stripped but normally
// linked binary, so its imports need no rebuilding. Its data is plaintext
// (no hasp answers). Its cabinet has two USB devices: the RIO board (0c70:
// 0780, the switches) and the spinner (1241:1111, the wheel, see rtWof.c).
// The RIO API was found by matching Galaga Assault's: the game registers a
// callback per switch (RIO_Uses), fed by RIO_ProcessCallbacks each frame.

static const RtSymbol wofSymbols[] = {
    {"hasp_login", 0x08206800},
    {"hasp_logout", 0x082068a0},
    {"hasp_encrypt", 0x0820698c},
    {"hasp_decrypt", 0x08206a78},
    {"hasp_read", 0x082075f8},            // after the previous function's nop pad
    {"hasp_write", 0x082076c4},
    {"hasp_get_sessioninfo", 0x08207380}, // (as hasp_read: its calls go here)
    {"DongleEncrypt", 0x082056b0},
    {"DongleDecrypt", 0x08205640},
    // The dongle memory read its boot reads a record with, and the
    // record's checksum (see rtWof.c).
    {"DongleMemRead", 0x08206270},
    {"DongleChecksum", 0x081e9fa4},
    // Forks a tracer: the game runs as a child the parent ptraces, so that
    // no debugger can attach. Returns 1 in that child.
    {"TracerGuard", 0x081e3ef8},
    // Input: the event queue's ingress (event, data).
    {"PostInputEvent", 0x080500a0},
    // The RIO API (matched against Galaga Assault's), and the game's switch
    // callback (switch, 3, transition count, time), the same for every
    // switch.
    {"RIO_Connect", 0x083235a3},
    {"RIO_ConnectEx", 0x083236b4},
    {"RIO_Connected", 0x0832384a},
    {"RIO_SendReport", 0x08323864},
    {"RIO_SampleInput", 0x08323972},
    {"RIO_ProcessCallbacks", 0x08323a00},
    {"RIO_SW_State", 0x08323ba6},
    {"RIO_SW_Count", 0x08323c2e},
    {"wof_switch_event", 0x08074b50},
    // The spinner: its open (0: connected), close, and its libusb-0.1
    // usb_interrupt_read (the spinner's only user).
    {"SpinnerOpen", 0x0811f500},
    {"SpinnerClose", 0x0811f410},
    {"SpinnerUsbRead", 0x0812eec0},
    {NULL, 0},
};

// The RIO board is connected (RIO_Connected: 0), and needs no transport.
static const RtStub wofStubs[] = {
    {"TracerGuard", 1},
    {"RIO_Connect", 0},
    {"RIO_ConnectEx", 0},
    {"RIO_Connected", 0},
    {"RIO_SendReport", 0},
    {NULL, 0},
};

// RIO switch numbers (the game's switch table at 0x83cb480; names from the
// switch test).
enum
{
    WOF_SW_COIN1 = 0x00,
    WOF_SW_COIN2 = 0x01,
    WOF_SW_BILL = 0x02,
    WOF_SW_TEST = 0x03,
    WOF_SW_SERVICE = 0x04,
    WOF_SW_VOL_UP = 0x05,
    WOF_SW_VOL_DOWN = 0x06,
    WOF_SW_PUSH = 0x07,
};

// The "PUSH" button starts and plays: START or BUTTON_1.
static const RtIoInput wofRioSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, WOF_SW_PUSH, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, WOF_SW_PUSH, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, WOF_SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, WOF_SW_TEST, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, WOF_SW_VOL_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, WOF_SW_VOL_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, WOF_SW_COIN1, 0},
    {RT_IO_COIN, 1, WOF_SW_COIN2, 0},
    {RT_IO_END, 0, 0, 0},
};

// The cabinet's writable directories: its settings (/wofuser) and the online
// module's downloads, query cache and messages (/wofvuser, made at boot: the
// game takes its shutdown path when it cannot make it).
static const RtPathAlias wofRootAliases[] = {{"/wofuser", "wofuser"}, {"/wofvuser", "wofvuser"}, {NULL, NULL}};

// ---------------------------------------------------------------------------
// Big Buck HD Wild (g5 engine): like Terminator Salvation a stripped but
// normally linked binary, not a dump, so its imports need no rebuilding.
// The HASP HL library is linked in statically and sits at a constant offset
// from The Walking Dead's copy of it (+0x1b4300): each function below was
// checked by the trace string it pushes ("enter hasp_login" and friends), so
// the addresses are matched, not guessed.
//
// It draws and reads its inputs through the same g5 io layer as Jurassic
// Park, not through glut: the binary carries io_sdl, io_rio, io_irtrack and
// the rest (their error strings name them), and SDL 1.2 is linked in
// statically -- io_sdl:create_window() at 0x825ad8b asks SDL_SetVideoMode
// for a window (SDL_HWSURFACE|SDL_OPENGL) of the size two engine getters
// return.  Its input map, the RIO board and the IR guns are still to be
// found.

static const RtSymbol bbhdSymbols[] = {
    {"hasp_login", 0x0852fec0},
    {"hasp_logout", 0x0852ff60},
    {"hasp_encrypt", 0x0853004c},
    {"hasp_decrypt", 0x08530138},
    {"hasp_get_sessioninfo", 0x08530a40},
    {"hasp_read", 0x08530cb8},
    {"hasp_write", 0x08530d84},
    // The game's own layer over them: each calls the hasp_decrypt or
    // hasp_encrypt above and leaves its status in the dongle object at
    // +0x10, which is where the loader's stand-in writes its success.
    {"DongleDecrypt", 0x08266c40},
    {"DongleEncrypt", 0x08266cb0},
    // The anti-debug guard, the first thing main (0x811e830) calls: it
    // forks, the child PTRACE_TRACEMEs itself and returns 1 to carry on as
    // the game while the parent watches it and kills it if anything else
    // attaches.  Stubbed to 1 the fork never happens.
    {"TracerGuard", 0x084338e6},
    {"SDL_SetVideoMode", 0x08288c60},
    // The g5 io layer (io.c), in its source order: found by the constants
    // of their Jurassic Park counterparts (whose dump exports them), such as
    // the accessors' id ranges (digital: <= 0x142, then 0x146 on; analog:
    // 0x144 on).  Its digital slots are 0x2c bytes, Jurassic Park's 0x20:
    // the first 0x20 are laid out the same.
    {"io_new_data_present", 0x0825618f},
    {"io_input_analog_update", 0x08256210},
    {"io_set_input_raw_range", 0x082565c3},
    {"io_get_input_digital", 0x08256804},
    {"io_get_input_analog", 0x08256cb8},
    // The backends' loops are called by io_loop (0x8255f80) from a table at
    // 0x8c57340 ({mask, init, quit, loop, state}, 0x54 bytes each): entry 4
    // is io_sdl, whose create_window() follows it.
    {"io_sdl_loop", 0x0825a4eb},
    // Caps the map at 0x400 entries, InputMap at 0x8cc6234, count at
    // 0x8cc8234; main (0x811e830) calls it itself, 69 times.
    {"InputAddMap", 0x0815a1d0},
    // The camera manager, as io_irtrack's loop (0x8259b23) calls it for each
    // gun: whether the gun is active, then its position (0x85cda1a: -1 off
    // the screen). Inactive, the loop flags the gun's slots offscreen
    // (0x10000 in their first word) every frame, and attract mode says "gun
    // not connected". The position is the camera's raw coordinates, filtered
    // (0x85cd0f9) or not (0x85cd081, which the calibration reads), mapped
    // through the gun's calibration: the raw ones are answered, so that the
    // calibration and everything built on it work as on the cabinet.
    {"IrGunActive", 0x085cc328},
    {"IrGunRaw", 0x085cd081},
    {"IrGunRawFiltered", 0x085cd0f9},
    {"IrGunAim", 0x085cda1a},
    // Its buttons (gun, button): a count of transitions, odd while held,
    // which the loop turns into presses of the gun's trigger (button 0) and
    // pump (1) slots.
    {"IrGunButton", 0x085cd35e},
    {NULL, 0},
};

// Big Buck HD Wild v08.40 (2018, from the cabinet's restore discs): the
// same code with new content, each entry above found in it by its bytes
// (addresses masked).  The HASP library and the camera manager moved by
// +0xd8a0, the dongle layer and SDL by +0xd710, the io layer by +0xd530.
// main's 69 InputAddMap calls are unchanged, so are the slot ids below.
static const RtSymbol bbhd840Symbols[] = {
    {"hasp_login", 0x0853d760},
    {"hasp_logout", 0x0853d800},
    {"hasp_encrypt", 0x0853d8ec},
    {"hasp_decrypt", 0x0853d9d8},
    {"hasp_get_sessioninfo", 0x0853e2e0},
    {"hasp_read", 0x0853e558},
    {"hasp_write", 0x0853e624},
    {"DongleDecrypt", 0x08274350},
    {"DongleEncrypt", 0x082743c0},
    // main (0x8123540)'s first call, as in v08.29.
    {"TracerGuard", 0x08441126},
    {"SDL_SetVideoMode", 0x08296370},
    {"io_new_data_present", 0x082636bf},
    {"io_input_analog_update", 0x08263740},
    {"io_set_input_raw_range", 0x08263af3},
    {"io_get_input_digital", 0x08263d34},
    {"io_get_input_analog", 0x082641e8},
    {"io_sdl_loop", 0x08267a1b},
    {"InputAddMap", 0x08166130},
    {"IrGunActive", 0x085d9bc8},
    {"IrGunRaw", 0x085da921},
    {"IrGunRawFiltered", 0x085da999},
    {"IrGunAim", 0x085db2ba},
    {"IrGunButton", 0x085dabfe},
    {NULL, 0},
};

// Its input map, as main registers it: engine input ids (the game's) fed
// by I/O slot ids (where the cabinet's boards write).  main maps either the
// cabinet guns (IR tracking) or a developer mouse map, on a developer flag
// (0x80ae610) the release build keeps at 0: the cabinet map is used, and
// the loader writes the cabinet's own slots, from evdev or from the desktop.
enum
{
    BBHD_IO_COIN0 = 0x14c,    // -> 0x17f
    BBHD_IO_COIN1 = 0x14d,    // -> 0x180
    BBHD_IO_DIAG = 0x150,     // -> 0x17e
    BBHD_IO_SERVICE = 0x151,  // -> 0x17d (a service credit)
    BBHD_IO_VOL_UP = 0x152,   // -> 0x17b, 0x183 (also menu up)
    BBHD_IO_VOL_DOWN = 0x153, // -> 0x17c, 0x184 (also menu down)
    BBHD_IO_START0 = 0x154,   // -> 0x177
    BBHD_IO_START1 = 0x155,   // -> 0x178
    BBHD_IO_GUN0_TRIGGER = 0x17b, // -> 0x14e, and the P1 menu select 0x18b
    BBHD_IO_GUN0_PUMP = 0x17c,    // -> 0x14f
    BBHD_IO_GUN1_TRIGGER = 0x17e, // -> 0x159, and the P2 menu select 0x18c
    BBHD_IO_GUN1_PUMP = 0x17f,    // -> 0x15a
    BBHD_IO_GUN0_X = 0x181,   // -> 0x14b, and the P1 menu pointer 0x187
    BBHD_IO_GUN0_Y = 0x182,   // -> 0x14c, 0x188
    BBHD_IO_GUN1_X = 0x183,   // -> 0x156, 0x189
    BBHD_IO_GUN1_Y = 0x184,   // -> 0x157, 0x18a
};

// The cabinet's keypad, engine ids from the switch test's table of {id,
// name} pairs (SwitchTestNumpad0 at 0x8c57270): keys 0..9, then * and #.  main feeds them
// from the keypad's I/O slots (0x163 on, and again 0x16f on); the desktop
// numpad is mapped to them as well (SDL 1.2 key codes: KP0 is 256), with
// # on the numpad's Enter, the keypad's confirm key.
enum
{
    BBHD_NUMPAD0 = 0x18e,
    BBHD_NUMPAD_STAR = 0x198,
    BBHD_NUMPAD_HASH = 0x199,
};

static const RtMap bbhdExtraMaps[] = {
    {KEY_KP0 + 0, BBHD_NUMPAD0 + 0}, {KEY_KP0 + 1, BBHD_NUMPAD0 + 1}, {KEY_KP0 + 2, BBHD_NUMPAD0 + 2},
    {KEY_KP0 + 3, BBHD_NUMPAD0 + 3}, {KEY_KP0 + 4, BBHD_NUMPAD0 + 4}, {KEY_KP0 + 5, BBHD_NUMPAD0 + 5},
    {KEY_KP0 + 6, BBHD_NUMPAD0 + 6}, {KEY_KP0 + 7, BBHD_NUMPAD0 + 7}, {KEY_KP0 + 8, BBHD_NUMPAD0 + 8},
    {KEY_KP0 + 9, BBHD_NUMPAD0 + 9}, {KEY_KP_MULTIPLY, BBHD_NUMPAD_STAR}, {KEY_KP_ENTER, BBHD_NUMPAD_HASH},
    {0, 0},
};

// ANALOGUE_1/2 and 3/4 are the P1/P2 guns, PLAYER_n_BUTTON_1 their
// trigger and BUTTON_2 the pump, PLAYER_1_BUTTON_UP/DOWN the volume (the
// menus' up and down); from the desktop, the mouse is P1's gun (left button
// the trigger, right the pump), and the keys are rtIo.c's desktop keys.
// The guns reach the game through the camera manager's answers (IrGun*),
// which its calibration and io_irtrack's loop turn into the gun slots.
static const RtIoInput bbhdIoInputs[] = {
    {RT_IO_SWITCH, PLAYER_1, BBHD_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, BBHD_IO_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, BBHD_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, BBHD_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, BBHD_IO_DIAG, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, BBHD_IO_VOL_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, BBHD_IO_VOL_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, BBHD_IO_COIN0, 0},
    {RT_IO_COIN, 1, BBHD_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

static const RtStub bbhdStubs[] = {
    {"TracerGuard", 1},
    {NULL, 0},
};

// ---------------------------------------------------------------------------
// Pink Panther Jewel Heist (g6 engine), a ticket redemption game on a
// portrait monitor. Like Big Buck HD Wild a stripped but normally linked
// binary, except that SDL 1.2 is the system's. Its HASP HL library is Big
// Buck HD Wild's at -0x225050, each function checked by the trace string it
// pushes; its four dongle answers come from TeknoParrot's recording.

static const RtSymbol ppSymbols[] = {
    {"hasp_login", 0x0830ae70},
    {"hasp_logout", 0x0830af10},
    {"hasp_encrypt", 0x0830affc},
    {"hasp_decrypt", 0x0830b0e8},
    {"hasp_get_sessioninfo", 0x0830b9f0},
    {"hasp_read", 0x0830bc68},
    {"hasp_write", 0x0830bd34},
    // The game's own layer over them, as in Big Buck HD Wild.
    {"DongleEncrypt", 0x0819eda0},
    {"DongleDecrypt", 0x0819ef20},
    // Big Buck HD Wild's anti-debug guard, the same code (nothing seems to
    // call it in this build).
    {"TracerGuard", 0x081e4dbb},
    // gCLArgs at 0x8d2cc00: width, height (1280x720 by default), aspect;
    // "-f<w>x<h>" sets the size and fullscreen (+0x1c).
    {"ParseCommandLineArgs", 0x08066c50},
    // Its PLT entry: SDL 1.2 is the system's.
    {"SDL_SetVideoMode", 0x08050c90},
    // The I/O layer (io.c, 0x81855e2 on). Unlike Big Buck HD Wild's, the
    // engine keeps its own copy of the slots (0x8a128ac, accessor
    // 0x80aaec0): its input update (0x80ab1c0) runs io_loop (0x81857b0),
    // then copies io.c's slots over it. So io.c's slots are written: its
    // accessor (<= 0x142, then 0x146 on through a table; 0x2c byte slots
    // from 0x8a72f60 + 0x1a28), the getter of the flag io_loop sets, and
    // the loop of io_rio (entry 0x2 of the backends' table at 0x89e6060),
    // the last backend to run.
    {"io_get_input_digital", 0x0818604f},
    {"io_new_data_present", 0x08185a18},
    {"io_rio_loop", 0x08184e83},
    {NULL, 0},
};

// Its input map, as GameInputMaps (0x8068000) registers it: Big Buck HD
// Wild's cabinet switches, and six extra switches (ExtSwitch0..5 of the
// switch test, 0x19c..0x1a1). The game plays with a single button: the
// input it reads (0x185) is fed by any of the extra switches, the starts
// and the test switch alike.
enum
{
    PP_IO_COIN0 = 0x14c,    // -> 0x17f
    PP_IO_COIN1 = 0x14d,    // -> 0x180
    PP_IO_DIAG = 0x150,     // -> 0x17e, 0x185
    PP_IO_SERVICE = 0x151,  // -> 0x17d, 0x186
    PP_IO_VOL_UP = 0x152,   // -> 0x17b, 0x183 (also menu up)
    PP_IO_VOL_DOWN = 0x153, // -> 0x17c, 0x184 (also menu down)
    PP_IO_START0 = 0x154,   // -> 0x177, 0x185: the left button
    PP_IO_START1 = 0x155,   // -> 0x178, 0x185: the right button
    PP_IO_EXT0 = 0x15b,     // -> 0x19c, 0x185
    PP_IO_EXT1 = 0x15a,     // -> 0x19d, 0x185
};

// The two start switches are the cabinet's left and right buttons, which
// move the Panther: fed by P1's left and right too (the arrow keys, a pad's
// d-pad). BUTTON_1 is the play button (from the desktop, the left mouse
// button).
static const RtIoInput ppIoInputs[] = {
    {RT_IO_SWITCH, PLAYER_1, PP_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, PP_IO_START0, BUTTON_LEFT},
    {RT_IO_SWITCH, PLAYER_2, PP_IO_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, PP_IO_START1, BUTTON_RIGHT},
    {RT_IO_SWITCH, PLAYER_1, PP_IO_EXT0, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_2, PP_IO_EXT1, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, PP_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, PP_IO_DIAG, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, PP_IO_VOL_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, PP_IO_VOL_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, PP_IO_COIN0, 0},
    {RT_IO_COIN, 1, PP_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

// ---------------------------------------------------------------------------
// Aliens Armageddon (g6 engine, /pm/g6/aa): a light gun game like Big Buck
// HD Wild -- stripped, normally linked, SDL 1.2 linked in and GL through
// dlsym, IR guns seen by the cmgr camera manager -- with Pink Panther Jewel
// Heist's io layer, whose slots the engine copies after io_loop. Its HASP
// HL library is Big Buck World's at +0xa5fb0, each function checked by the
// trace string it pushes; the camera manager is Big Buck HD Wild's at
// -0xecc00. Its dongle answers come from TeknoParrot's recording, named by
// their first 16 bytes as Terminator Salvation's.

static const RtSymbol aaSymbols[] = {
    {"hasp_login", 0x08443380},
    {"hasp_logout", 0x08443420},
    {"hasp_encrypt", 0x0844350c},
    {"hasp_decrypt", 0x084435f8},
    {"hasp_free", 0x08443c3c},
    {"hasp_get_sessioninfo", 0x08443f00},
    {"hasp_read", 0x08444178},
    {"hasp_write", 0x08444244},
    // The game's own layer over them, as in Big Buck HD Wild.
    {"DongleDecrypt", 0x081bda90},
    {"DongleEncrypt", 0x081bdb00},
    // Big Buck HD Wild's anti-debug guard, the same code, main's first call.
    {"TracerGuard", 0x08347966},
    // gCLArgs at 0x890b540: width, height (1360x768 by default), aspect;
    // "-f<w>x<h>" sets the size and the fullscreen byte (0x890b551).
    {"ParseCommandLineArgs", 0x08083b40},
    // Linked-in SDL 1.2, called by io_sdl's create_window (0x81b58bb).
    {"SDL_SetVideoMode", 0x081dfab0},
    // The io layer (io.c, io_init at 0x81b0df0, io_loop 0x81b0fa4). As in
    // Pink Panther Jewel Heist, the engine keeps its own copy of the slots
    // (accessor 0x80b9200): its input update (0x80b9920) runs io_loop, then
    // copies io.c's slots over it. So io.c's slots are written: its accessor
    // (<= 0x142, then 0x146 on through a table; 0x2c byte slots from
    // 0x893caa0 + 0x19fc), the getter of the flag io_loop sets, and the loop
    // of io_rio, the last backend enabled in the table at 0x88b1040 (after
    // io_irtrack and io_sdl).
    {"io_get_input_digital", 0x081b148c},
    {"io_new_data_present", 0x081b11b3},
    {"io_rio_loop", 0x081b5c74},
    // The camera manager, as io_irtrack's loop (0x81b4653) calls it for each
    // gun (see Big Buck HD Wild's): whether the gun is active, its raw
    // coordinates (not filtered, and filtered) and its buttons. The loop
    // reads buttons 0, 1 and 3: the trigger, the pump and the grenade.
    {"io_get_input_analog", 0x081b18d8},
    {"IrGunActive", 0x084df728},
    {"IrGunRaw", 0x084e0481},
    {"IrGunRawFiltered", 0x084e04f9},
    {"IrGunAim", 0x084e0e1a},
    {"IrGunButton", 0x084e075e},
    {NULL, 0},
};

// Its input map, as GameInputMaps (0x8081180) registers it: with the IR guns
// (the dongle's byte 0x3a is not 4, which would select other guns), Big Buck
// HD Wild's cabinet switches and gun slots, and a third button on each gun:
// the switch test names them trigger, pump and grenade (slots 0x17b..0x17d
// -> 0x14d..0x14f for gun 1, 0x17e..0x180 -> 0x157..0x159 for gun 2).
enum
{
    AA_IO_COIN0 = 0x14c,    // -> 0x17b
    AA_IO_COIN1 = 0x14d,    // -> 0x17c
    AA_IO_DIAG = 0x150,     // -> 0x17a, 0x181
    AA_IO_SERVICE = 0x151,  // -> 0x179, 0x182 (a service credit)
    AA_IO_VOL_UP = 0x152,   // -> 0x177, 0x17f (also menu up)
    AA_IO_VOL_DOWN = 0x153, // -> 0x178, 0x180 (also menu down)
    AA_IO_START0 = 0x154,   // -> 0x173, 0x181
    AA_IO_START1 = 0x155,   // -> 0x174, 0x181
};

// The guns reach the game through the camera manager's answers (IrGun*):
// ANALOGUE_1/2 and 3/4 the P1/P2 guns, PLAYER_n_BUTTON_1 the trigger,
// BUTTON_2 the pump and BUTTON_3 the grenade, as in Terminator Salvation
// (from the desktop, the mouse is P1's gun: left, right and middle buttons).
static const RtIoInput aaIoInputs[] = {
    {RT_IO_SWITCH, PLAYER_1, AA_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, AA_IO_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, AA_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, AA_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, AA_IO_DIAG, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, AA_IO_VOL_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, AA_IO_VOL_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, AA_IO_COIN0, 0},
    {RT_IO_COIN, 1, AA_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

// Pac-Man Chomp Mania (statically linked SDL 1.2), v1.28C: Galaga Assault's
// RIO layer and switch numbers, one player.

static const RtIoInput pmRioSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, GA_SW_START_FIRE, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_DOWN, BUTTON_DOWN},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_LEFT, BUTTON_LEFT},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_RIGHT, BUTTON_RIGHT},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, GA_SW_TEST, BUTTON_TEST},
    {RT_IO_COIN, 0, GA_SW_COIN1, 0},
    {RT_IO_COIN, 1, GA_SW_COIN2, 0},
    {RT_IO_END, 0, 0, 0},
};

// Jurassic Park's renderer: its viewports, render targets, frame grabs and
// screen-space shaders are sized from the real screen (see layoutRealSize).
static const char *const jpLayoutRealSize[] = {
    "RenderInit", "RenderStateStart", "RenderState_SetRenderTarget", "RenderLoop", "RenderLoop_RenderLayer",
    "FrameFlip", "VidStart", "RenderRecordFrame", "CheckScreenGrab", "CamBuildReflectionMatrices",
    "shader113SetupFunc", "ShaderSetupEnvMapFunc", NULL,
};

static const RtGame rtGames[] = {
    {
        .crc32 = JURASSIC_PARK_RT,
        .fileCrc32 = 0xf4a8eca8,
        .envelopeGot = JP_ENVELOPE_GOT,
        .envelopeImports = jpEnvelopeImports,
        .envelopeImportCount = sizeof(jpEnvelopeImports) / sizeof(jpEnvelopeImports[0]),
        .envelopeSelfSlot = 38,
        .envelopeSelfTarget = 0x09048d30,
        .gameImports = jpGameImports,
        .gameImportCount = sizeof(jpGameImports) / sizeof(jpGameImports[0]),
        .extraLibs = jpExtraLibs,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .rootPath = "/pm",
        .encryptedScripts = "g5/dino/data/programs_enc",
        .decryptedScripts = "programs_dec",
        .inputMap = 0x08db6e80,
        .inputMapCount = 0x08b505f8,
        .devInputFlag = 0x08db4e38,
        // "sub $0x2c,%esp; mov 0x8db4e38,%edx"
        .gameInputMapsPrologue = 9,
        // io_sdl_loop: "push %ebp; mov %esp,%ebp; push %esi; push %ebx; sub $0x2aa40,%esp"
        .ioLoopPrologue = 11,
        .devMapFilters = jpDevMapFilters,
        .extraMaps = jpExtraMaps,
        .evdevMapFilters = jpEvdevMapFilters,
        .evdevMaps = jpEvdevMaps,
        .ioInputs = jpIoInputs,
        .ioRawRange = 1,
        .resolution = 0x08db4e20,
        .aspect = 0x08db4e28,
        // "push %ebp; xor %ebp,%ebp; push %edi; push %esi; push %ebx; mov $0x1,%ebx"
        .parseArgsPrologue = 11,
        // VidDisp.screenW
        .screenSize = 0x08e7ad44,
        .layoutWidth = 1360,
        .layoutHeight = 768,
        .layoutRealSize = jpLayoutRealSize,
        // "flds 0x4(%esp); flds 0x8(%esp)"
        .dflt2DCamPrologue = 8,
        .fixedFrame = 1,
    },
    {
        .crc32 = CRUISN_BLAST_RT,
        .fileCrc32 = 0xb919dbd6,
        .envelopeGot = CB_ENVELOPE_GOT,
        .envelopeImports = cbEnvelopeImports,
        .envelopeImportCount = sizeof(cbEnvelopeImports) / sizeof(cbEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = cbGameImports,
        .gameImportCount = sizeof(cbGameImports) / sizeof(cbGameImports[0]),
        .extraLibs = cbExtraLibs,
        .haspFeature = 0,
        .haspMemoryFileId = 0xfff4,
        .haspReadPatch = 0x0804829a,
        .haspWritePatch = 0x0804833c,
        .haspSessionInfoPatch = 0x0804842a,
        .stubs = cbStubs,
        .rootPath = "/pm",
        .encryptedScripts = "g5/race/data/programs_enc",
        .decryptedScripts = "programs_dec",
        .pathAliases = cbPathAliases,
        .gameInputMapsSymbol = "_Z13GameInputMapsv",
        .inputMap = 0x09d45ce0,
        .inputMapCount = 0x09c28544,
        // "push %ebx; sub $0x28,%esp; mov 0x8bdd9c8,%edx"
        .gameInputMapsPrologue = 10,
        // The cabinet board backend runs after the SDL one and would clear
        // the switch edges: "push %ebp; push %edi; push %esi; push %ebx; sub $0x1c,%esp"
        .ioLoopSymbol = "io_rio_loop",
        .ioLoopPrologue = 7,
        .extraMaps = cbExtraMaps,
        .ioInputs = cbIoInputs,
        .wheelSetSymbol = "Wheel_Set",
        .wheelInRace = CB_WHEEL_IN_RACE,
        .wheelMenuForce = CB_WHEEL_MENU_FORCE,
        .wheelEffectsFlag = CB_WHEEL_EFFECTS,
        .parseArgsSymbol = "_Z20ParseCommandLineArgsiPPc",
        .resolution = 0x08bdd960,
        .aspect = 0x08bdd974,
        // "push %ebp; push %edi; push %esi; push %ebx; mov $0x1,%ebx"
        .parseArgsPrologue = 9,
    },
    {
        .crc32 = GALAGA_ASSAULT_RT,
        .fileCrc32 = 0xb2452857,
        .envelopeGot = GA_ENVELOPE_GOT,
        .envelopeImports = gaEnvelopeImports,
        .envelopeImportCount = sizeof(gaEnvelopeImports) / sizeof(gaEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = gaGameImports,
        .gameImportCount = sizeof(gaGameImports) / sizeof(gaGameImports[0]),
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff0,
        .bssStart = 0x083484bc,
        .bssEnd = 0x08633cfc,
        .replacedLib = "libSDL2-2.0.so.0",
        .replacedLibPrefix = "SDL_",
        .workDir = "data",
        // Game_PreInit sets the defaults (1920x1080, rotated), then the
        // command line: "push %ebp; mov %esp,%ebp; sub $0x28,%esp"
        .parseArgsSymbol = "_Z19Main_ProcessCmdLineiPPc",
        .parseArgsPrologue = 6,
        .resolution = 0x083313e0,
        .rotateFlag = 0x085a4f74,
        .monitorSize = 0x083313e8,
        .rioSwitches = gaRioSwitches,
        .stubs = gaStubs,
        // "push %ebp; mov %esp,%ebp; sub $0x58,%esp"
        .orthoSymbol = "set_ortho",
        .orthoPrologue = 6,
        .rootPath = "/pm",
    },
    {
        .crc32 = BIG_BUCK_HD_WILD_RT,
        // The game links libcsv by absolute path (/usr/lib/libcsv.so), which
        // no library path can redirect, so it runs from a patched copy.
        .fileCrc32 = 0xa982eef0,
        .envelopeSelfSlot = -1,
        .symbols = bbhdSymbols,
        // Both read off the binary.  hasp_login is called at 0x8267ad8 with
        // 0xffff0000 as a literal, the legacy program-number feature every
        // other Raw Thrills game uses (a second login at 0x8267b75 asks for
        // the default feature 0).  The memory reads at 0x826757e and
        // 0x826762e pass 0xfff2 as a literal.
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        // Recorded answers of its four dongle calls at startup: the secrets
        // its file keys derive from, and the efilemaps' AES key and IV.
        .haspAnswers = "hasp",
        // The cabinet mounts the game at /pm (utils/go.sh, and the data paths
        // under /pm/g5/bbhd).
        .rootPath = "/pm",
        .stubs = bbhdStubs,
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx"
        .videoModeSymbol = "SDL_SetVideoMode",
        .videoModePrologue = 6,
        // main keeps its render size in locals, 1360x768 unless its
        // command line says otherwise: "-f<w>x<h>" (sscanf "%dx%d", which
        // also asks for fullscreen, left to [Display] FULLSCREEN).
        .sizeArgument = "-f%dx%d",
        // "push %ebp; mov %esp,%ebp; sub $0x18,%esp"
        .inputAddMapPrologue = 6,
        // "push %ebp; mov %esp,%ebp; push %ebx; sub $0x6584,%esp"
        .ioLoopPrologue = 10,
        .extraMaps = bbhdExtraMaps,
        .ioInputs = bbhdIoInputs,
        .ioDesktop = 1,
        .irGunActiveSymbol = "IrGunActive",
        .irGunRawSymbols = {"IrGunRaw", "IrGunRawFiltered"},
        .irGunButtonSymbol = "IrGunButton",
        .irGunAimSymbol = "IrGunAim",
        .irGunSlots = 0x08d200e4,
        .irGunSlotStride = 0xec,
        .irGunButtonSlot = 0x68,
    },
    {
        // v08.40, run as v08.29 above (same dongle answers, prologues and
        // input map).
        .crc32 = BIG_BUCK_HD_WILD_840_RT,
        .fileCrc32 = 0x41e3ddb1,
        .envelopeSelfSlot = -1,
        .symbols = bbhd840Symbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        // FactorySetupProc (0x80a3391) writes 1 at 0x39 (0x8274b70) only
        // after registering the cabinet online, which offline never
        // succeeds; the attract mode reads it (0x8274ee0) and says "factory
        // setup incomplete" while it is 0.
        .haspFactorySetupOffset = 0x39,
        // v08.40 always runs as an online cabinet: unregistered, it opens a
        // registration wizard at boot with a 30-day grace of limited play.
        // At 1 (or 2) byte 4 (read at 0x80b9cc0) makes it an offline one:
        // no wizard, no grace period, the permission check (0x80e2750)
        // passes basic play without the server.  The DLCs are unlocked by
        // the gameOptions buyouts (zombieBuyout, ...) in pmuser, but for
        // the two Monster Island upgrades, which read 0 from there: owned
        // by the dongle (bytes 6..0xd, one per upgrade, 0x80b9400 on).  Its
        // "Animal" one (0xc, owned bit 0x40) is the Buckzilla adventure in
        // Big Buck HD; its "Mini-Game" (0xd) is left out, as it takes Duck
        // Dynasty's place on the game select for a second Zombies card.
        .haspSetOffsets = {0x04, 0x0c},
        .rootPath = "/pm",
        .stubs = bbhdStubs,
        .videoModeSymbol = "SDL_SetVideoMode",
        .videoModePrologue = 6,
        .sizeArgument = "-f%dx%d",
        .inputAddMapPrologue = 6,
        .ioLoopPrologue = 10,
        .extraMaps = bbhdExtraMaps,
        .ioInputs = bbhdIoInputs,
        .ioDesktop = 1,
        .irGunActiveSymbol = "IrGunActive",
        .irGunRawSymbols = {"IrGunRaw", "IrGunRawFiltered"},
        .irGunButtonSymbol = "IrGunButton",
        .irGunAimSymbol = "IrGunAim",
        // io_irtrack's slot table (0x8266e1e stores it: base + 0x1850).
        .irGunSlots = 0x08d398e4,
        .irGunSlotStride = 0xec,
        .irGunButtonSlot = 0x68,
        // [CrossHairs] ENABLE_CROSSHAIRS (rtCrosshair.c).
        .frameDraw = rtCrosshairFrameDraw,
    },
    {
        .crc32 = PACMAN_CHOMP_MANIA_RT,
        .fileCrc32 = 0x19c3fef7,
        .envelopeGot = PM_ENVELOPE_GOT,
        .envelopeImports = pmEnvelopeImports,
        .envelopeImportCount = sizeof(pmEnvelopeImports) / sizeof(pmEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = pmGameImports,
        .gameImportCount = sizeof(pmGameImports) / sizeof(pmGameImports[0]),
        .symbols = pmSymbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff0,
        .bssStart = 0x08283bbc,
        .bssEnd = 0x0835fd5c,
        // Game_PreInit sets the defaults (1920x1080, rotated), then the
        // command line: "push %ebp; mov %esp,%ebp; sub $0x28,%esp"
        .parseArgsSymbol = "_Z19Main_ProcessCmdLineiPPc",
        .parseArgsPrologue = 6,
        .resolution = 0x08270c80,
        .rotateFlag = 0x082d4390,
        .fullscreenFlag = 0x082d4384,
        // gameh, gamew: the portrait game, 1080x1920
        .monitorSize = 0x08270c88,
        .rioSwitches = pmRioSwitches,
        .rioDesktopKeys = 1,
        .stubs = gaStubs,
        // "push %ebp; mov %esp,%ebp; sub $0x48,%esp"
        .orthoSymbol = "OGL_resize_window_ortho",
        .orthoPrologue = 6,
        .rootPath = "/pm",
    },
    {
        .crc32 = PINK_PANTHER_RT,
        // libcsv by absolute path, as Big Buck HD Wild: a patched copy.
        .fileCrc32 = 0xd394437b,
        .envelopeSelfSlot = -1,
        .symbols = ppSymbols,
        // hasp_login at 0x819dfb0 with 0xffff0000 as a literal, the memory
        // reads at 0x819e615 and 0x819e705 with 0xfff2.
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        .rootPath = "/pm",
        .stubs = bbhdStubs,
        .resolution = 0x08d2cc00,
        .aspect = 0x08d2cc08,
        .fullscreenFlag = 0x08d2cc1c,
        // "jmp *0x89ac324", the PLT entry: the window at [Display]
        // WIDTH/HEIGHT, the game at the bezel's hole with one.
        .videoModeSymbol = "SDL_SetVideoMode",
        .videoModePrologue = 6,
        .bezelFrame = 1,
        .exeGlxGetProcAddress = 1,
        // "push %edi; push %esi; push %ebx; mov $0x1,%ebx"
        .parseArgsPrologue = 8,
        // "push %ebp; mov %esp,%ebp; sub $0x38,%esp"
        .ioLoopSymbol = "io_rio_loop",
        .ioLoopPrologue = 6,
        .ioInputs = ppIoInputs,
        .ioDesktop = 1,
    },
    {
        .crc32 = ALIENS_ARMAGEDDON_RT,
        // libcsv by absolute path, as Big Buck HD Wild: a patched copy. The
        // untouched file, or TeknoParrot's 1920x1080 resolution patch (only
        // main's default size differs, which the loader sets anyway).
        .fileCrc32 = 0xbc531e68,
        .altFileCrc32 = 0xc6b8dfa2,
        .envelopeSelfSlot = -1,
        .symbols = aaSymbols,
        // hasp_login at 0x81be928 with 0xffff0000 as a literal, the memory
        // reads at 0x81be47e and the writes at 0x81be09b with 0xfff2.
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        // Its dozen threads at 64 MB of stack each, its heap and 32-bit Mesa
        // fill its address space: Mesa then fails to map a shader, and the
        // GPU context is lost.
        .threadStackSize = 8 << 20,
        .rootPath = "/pm",
        .stubs = bbhdStubs,
        .resolution = 0x0890b540,
        .aspect = 0x0890b548,
        // Its fullscreen flag is a byte (0x890b551), next to the flag that
        // selects the guns: fullscreen is left to SDL_SetVideoMode's hook.
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx"
        .videoModeSymbol = "SDL_SetVideoMode",
        .videoModePrologue = 6,
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx"
        .parseArgsPrologue = 6,
        // "push %ebp; mov %esp,%ebp; sub $0x38,%esp"
        .ioLoopSymbol = "io_rio_loop",
        .ioLoopPrologue = 6,
        .ioInputs = aaIoInputs,
        .ioDesktop = 1,
        .irGunActiveSymbol = "IrGunActive",
        .irGunRawSymbols = {"IrGunRaw", "IrGunRawFiltered"},
        .irGunButtonSymbol = "IrGunButton",
        .irGunAimSymbol = "IrGunAim",
        .irGunSlots = 0x08945ca4,
        .irGunSlotStride = 0xdc,
        .irGunButtonSlot = 0x58,
    },
    {
        .crc32 = WALKING_DEAD_RT,
        .fileCrc32 = 0x3fcf1642,
        .envelopeGot = TWD_ENVELOPE_GOT,
        .envelopeImports = twdEnvelopeImports,
        .envelopeImportCount = sizeof(twdEnvelopeImports) / sizeof(twdEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = twdGameImports,
        .gameImportCount = sizeof(twdGameImports) / sizeof(twdGameImports[0]),
        .symbols = twdSymbols,
        // Heap pointers of the cabinet's run left in the dump: Bullet's
        // profile clock data (its start time, a struct timeval).
        .staleHeapPointers = twdStaleHeapPointers,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        // Recorded answers of its four dongle calls at startup: the secrets
        // its file keys derive from, and the efilemaps' AES key and IV.
        .haspAnswers = "hasp",
        .rioSwitches = twdRioSwitches,
        .rioDesktopKeys = 1,
        // "push %ebp; mov %esp,%ebp; sub $0x38,%esp"
        .ioLoopSymbol = "io_rio_loop",
        .ioLoopPrologue = 6,
        .rioEventSymbol = "io_rio_switch_report",
        .umcConnectedSymbol = "umc_check_connection",
        .umcStatusSymbol = "umc_device_status",
        .umcReadSymbol = "umc_read_report",
        .umcGuns = twdGuns,
        .umcGunCount = 2,
        // "push %ebp; push %edi; push %esi; push %ebx; sub $0x7c,%esp"
        .videoModeSymbol = "SDL_SetVideoMode",
        .videoModePrologue = 7,
        .resolution = 0x08ac0400,
        .aspect = 0x08ac0408,
        // "push %ebp; push %edi; push %esi; push %ebx; mov $0x1,%ebx"
        .parseArgsPrologue = 9,
        .stubs = gaStubs,
        .rootPath = "/pm",
        .pathAliases = twdPathAliases,
    },
    {
        .crc32 = TERMINATOR_SALVATION_RT,
        .envelopeSelfSlot = -1,
        .symbols = t4Symbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        .stubs = t4Stubs,
        .rootPath = "/g3",
        .rootAliases = t4RootAliases,
        // Both "push %ebp; mov %esp,%ebp; sub $imm8,%esp"
        .setModeSymbol = "SetVideoMode",
        .setModePrologue = 6,
        .modePointer = 0x088b5e70,
        .windowOpenSymbol = "OpenWindow",
        .windowOpenPrologue = 6,
        .glutWindowOnly = 1,
        .jammaPollSymbol = "JammaPoll",
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx; xor %ebx,%ebx"
        .jammaPollPrologue = 8,
        .jammaOpSymbol = "JammaOp",
        // "push %ebp; mov %esp,%ebp; push %ebx; sub $0x74,%esp"
        .jammaOpPrologue = 7,
        .postEventSymbol = "PostInputEvent",
        .jammaSwitches = t4Switches,
        .jammaGuns = t4Guns,
        .jammaGunCount = sizeof(t4Guns) / sizeof(t4Guns[0]),
        .gunAimSymbol = "GunAim",
        .gunAimWidth = 800,
        .gunAimHeight = 600,
        .gunButtonSymbol = "GunButton",
        .gunButtonPresses = {[3] = BUTTON_3},
        .gunButtonReleases = {[4] = BUTTON_3},
    },
    {
        .crc32 = BIG_BUCK_WORLD_RT,
        .envelopeSelfSlot = -1,
        .symbols = bbwSymbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        .haspAnswerKeySize = 32,
        .stubs = bbwStubs,
        .rootPath = "/g3",
        .rootAliases = bbwRootAliases,
        // "push %ebp; mov %esp,%ebp; sub $0x8,%esp"
        .setModeSymbol = "SetVideoMode",
        .setModePrologue = 6,
        .modePointer = 0x08bbcccc,
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx"
        .windowOpenSymbol = "OpenWindow",
        .windowOpenPrologue = 6,
        .jammaPollSymbol = "JammaPoll",
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx; mov $0x8baba40,%edi"
        .jammaPollPrologue = 11,
        .jammaOpSymbol = "JammaOp",
        // "push %ebp; mov %esp,%ebp; push %esi; push %ebx; sub $0x70,%esp"
        .jammaOpPrologue = 8,
        .jammaSwitches = jammaSwitches,
        .postEventSymbol = "PostInputEvent",
        .jammaGuns = bbwGuns,
        .jammaGunCount = sizeof(bbwGuns) / sizeof(bbwGuns[0]),
        .gunPositionEvent = 0x3f,
        .gunWidth = 640,
        .gunHeight = 480,
        // The calibration's requests (0x8108b8e), with its targets at 0x8b56560.
        .jammaGunCalibrateOp = 0x16,
        .jammaGunCalibrationModeOp = 0x19,
        .gunAimSymbol = "GunAim",
        .gunAimWidth = 800,
        .gunAimHeight = 600,
        .offScreenButton = 1,
        // A 4:3 game: with black bars on a wide screen (see RtGame).
        .frameAspect = {4, 3},
    },
    {
        .crc32 = WHEEL_OF_FORTUNE_RT,
        .envelopeSelfSlot = -1,
        .symbols = wofSymbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .stubs = wofStubs,
        .install = rtWofInstall,
        .glutGameModeWindow = 1,
        .rioSwitches = wofRioSwitches,
        .rioDesktopKeys = 1,
        .rioEventSymbol = "wof_switch_event",
        .ioLoopSymbol = "RIO_ProcessCallbacks",
        .ioLoopPrologue = 6,
        .rootPath = "/g3",
        .rootAliases = wofRootAliases,
        // Its default (1366x768) otherwise, parsed from its command line.
        .sizeArgument = "-r%dx%d",
    },
};

const RtGame *rtGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(rtGames) / sizeof(rtGames[0]); i++)
        if (rtGames[i].crc32 == crc32)
            return &rtGames[i];
    return NULL;
}

const RtGame *rtGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(rtGames) / sizeof(rtGames[0]); i++)
        if ((rtGames[i].fileCrc32 && rtGames[i].fileCrc32 == fileCrc32) ||
            (rtGames[i].altFileCrc32 && rtGames[i].altFileCrc32 == fileCrc32))
            return &rtGames[i];
    return NULL;
}
