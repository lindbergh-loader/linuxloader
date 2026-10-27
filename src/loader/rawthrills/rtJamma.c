// JAMMA I/O board of g3 engine games (Terminator Salvation).
//
// The cabinet's board counts the transitions of its switches (a count is
// odd while a switch is held) and sends the light guns' shots. The game
// reads the switch counters through its board API (JammaOp) and turns their
// changes into input events, in its poll run each frame and in its test
// menus. The board is not there: its switch counters are kept here and
// handed to the game's requests for them, the gun shots are posted as the
// input events the board's messages would have caused, and the IR gun
// manager's aim of each gun (from which the game places the reticle and
// the shots) is answered with its position. Inputs:
//  - with evdev input (INPUT_MODE 2), the loader's JVS state: the [EVDEV]
//    mappings as for the other games (guns: ANALOGUE_1/2 and 3/4, BUTTON_1
//    fires, BUTTON_2 the reload button, BUTTON_3 the gun's third button --
//    Terminator Salvation's grenade -- or, for guns without one, a reload
//    by firing off the screen, as does firing at its edge; player 1's
//    UP/DOWN the volume);
//  - otherwise, the game window: the mouse is player 1's gun (left fires,
//    right is the reload button, middle is BUTTON_3); keys work on
//    QWERTY and AZERTY keyboards: 1 or I start player 1, 2 or O player 2,
//    5 or U coin, 6 coin 2, P or F1 service, F2 test, up/down arrows volume.
// The keys also work with evdev input. Esc or Alt+F4 in the game window
// quits, with either input.

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define MAX_BOARD_SWITCHES 32
#define MAX_GUNS 2
#define DESKTOP_ANALOGUE_MAX 0xffff

// JammaOp requests: switch counters (with a flag telling whether they
// changed) and the first read of them.
#define JAMMAOP_GET_SWITCHES 0x112
#define JAMMAOP_GET_SWITCHES_FIRST 0x312

// glut constants (freeglut).
#define GLUT_KEY_F1 1
#define GLUT_KEY_F2 2
#define GLUT_KEY_F4 4
#define GLUT_ACTIVE_ALT 4
#define KEY_ESCAPE 27
#define GLUT_KEY_UP 101
#define GLUT_KEY_DOWN 103
#define GLUT_LEFT_BUTTON 0
#define GLUT_MIDDLE_BUTTON 1
#define GLUT_RIGHT_BUTTON 2
#define GLUT_DOWN 0
#define GLUT_WINDOW_WIDTH 102
#define GLUT_WINDOW_HEIGHT 103

// Data of the board's gun position event: the gun's number, the position,
// and two values the game's IR gun path sets to 1.
typedef struct
{
    int id;
    int x, y;
    int extra[2];
} GunPosition;

static const RtGame *jammaGame;
static int (*postEvent)(int event, const void *data);
static int (*jammaPollOrig)(void);
static int (*jammaOpOrig)(int op, int a, int b, int c, int d, int e);

// The board's calibration of each gun, per axis: board = scale * raw + offset
// (see RtGame). The game keeps it and sends it at each start, and switches
// a gun to raw positions (mode 0) while it calibrates it: applied in mode 2.
#define GUN_CALIBRATED_MODE 2
static struct
{
    double scale[2], offset[2];
    int mode;
} gunCalibration[MAX_GUNS] = {{{1, 1}, {0, 0}, 0}, {{1, 1}, {0, 0}, 0}};

// A new calibration, from the points shot at two targets: the gun's
// previous one is replaced (none if a point is missing). The first
// calibration after the start is shot without the game switching the gun to
// raw positions: points shot with the calibration applied are taken back to
// raw ones first.
static void calibrateGun(int g, const int *measured, const int *targets)
{
    for (int axis = 0; axis < 2; axis++)
    {
        double m1 = measured[axis], m2 = measured[2 + axis];
        int t1 = targets[axis], t2 = targets[2 + axis], valid = m1 >= 0 && m2 >= 0;
        double scale = 1, offset = 0;
        if (valid && gunCalibration[g].mode == GUN_CALIBRATED_MODE && gunCalibration[g].scale[axis] != 0)
        {
            m1 = (m1 - gunCalibration[g].offset[axis]) / gunCalibration[g].scale[axis];
            m2 = (m2 - gunCalibration[g].offset[axis]) / gunCalibration[g].scale[axis];
        }
        if (valid && m1 != m2)
        {
            scale = (double)(t2 - t1) / (m2 - m1);
            offset = t1 - scale * m1;
        }
        gunCalibration[g].scale[axis] = scale;
        gunCalibration[g].offset[axis] = offset;
    }
    printf("Raw Thrills: gun %d calibrated: x * %f + %f, y * %f + %f\n", g, gunCalibration[g].scale[0],
           gunCalibration[g].offset[0], gunCalibration[g].scale[1], gunCalibration[g].offset[1]);
}
static uint32_t boardSwitches[MAX_BOARD_SWITCHES];
static int gunHeld[MAX_GUNS];
static int evdevInput;

// ---------------------------------------------------------------------------
// Desktop input, kept as a JVS state so that both inputs read the same way.

static JVSIO desktop = {.analogueMax = DESKTOP_ANALOGUE_MAX};
// The coin keys hold the coin switches, as the other keys do theirs.
static int desktopCoinHeld[2];
static int (*glutGet)(int what);
static int (*glutGetModifiers)(void);

// The games have no quit key; their threads can keep exit() from returning.
static void quit(void)
{
    log_info("Raw Thrills: quit from the keyboard");
    _exit(0);
}

static void desktopSwitch(int player, int bit, int held)
{
    if (held)
        desktop.state.inputSwitch[player] |= bit;
    else
        desktop.state.inputSwitch[player] &= ~bit;
}

// glut reports characters, which depend on the keyboard layout: the digit
// keys are also taken as their AZERTY characters (& é ( -, é in Latin-1),
// and letters placed alike on QWERTY and AZERTY do the same.
static void keyboard(unsigned char key, int held)
{
    switch (key)
    {
    case '1':
    case '&':
    case 'i':
    case 'I':
        desktopSwitch(PLAYER_1, BUTTON_START, held);
        break;
    case '2':
    case 0xe9:
    case 'o':
    case 'O':
        desktopSwitch(PLAYER_2, BUTTON_START, held);
        break;
    case '5':
    case '(':
    case 'u':
    case 'U':
        desktopCoinHeld[0] = held;
        break;
    case '6':
    case '-':
        desktopCoinHeld[1] = held;
        break;
    case 'p':
    case 'P':
        desktopSwitch(PLAYER_1, BUTTON_SERVICE, held);
        break;
    }
}

static int gameKey(int key, int special, int held)
{
    return jammaGame->desktopKey && jammaGame->desktopKey(key, special, held);
}

static void keyDown(unsigned char key, int x, int y)
{
    (void)x;
    (void)y;
    if (key == KEY_ESCAPE)
        quit();
    if (!gameKey(key, 0, 1))
        keyboard(key, 1);
}

static void keyUp(unsigned char key, int x, int y)
{
    (void)x;
    (void)y;
    if (!gameKey(key, 0, 0))
        keyboard(key, 0);
}

static void special(int key, int held)
{
    if (key == GLUT_KEY_F1)
        desktopSwitch(PLAYER_1, BUTTON_SERVICE, held);
    else if (key == GLUT_KEY_F2)
        desktopSwitch(SYSTEM, BUTTON_TEST, held);
    else if (key == GLUT_KEY_UP)
        desktopSwitch(PLAYER_1, BUTTON_UP, held);
    else if (key == GLUT_KEY_DOWN)
        desktopSwitch(PLAYER_1, BUTTON_DOWN, held);
}

static void specialDown(int key, int x, int y)
{
    (void)x;
    (void)y;
    if (key == GLUT_KEY_F4 && glutGetModifiers && (glutGetModifiers() & GLUT_ACTIVE_ALT))
        quit();
    if (!gameKey(key, 1, 1))
        special(key, 1);
}

static void specialUp(int key, int x, int y)
{
    (void)x;
    (void)y;
    if (!gameKey(key, 1, 0))
        special(key, 0);
}

// Player 1's gun: its position as JVS analogue values (y upwards).
static void motion(int x, int y)
{
    int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
    if (w <= 1 || h <= 1)
        return;
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    // Keep off the extremes, which mean off the screen.
    desktop.state.analogueChannel[ANALOGUE_1] = 1 + (int)((double)x * (DESKTOP_ANALOGUE_MAX - 2) / (w - 1));
    desktop.state.analogueChannel[ANALOGUE_2] = 1 + (int)((double)y * (DESKTOP_ANALOGUE_MAX - 2) / (h - 1));
}

static void mouse(int button, int state, int x, int y)
{
    static const int bits[] = {[GLUT_LEFT_BUTTON] = BUTTON_1, [GLUT_MIDDLE_BUTTON] = BUTTON_3,
                               [GLUT_RIGHT_BUTTON] = BUTTON_2};
    motion(x, y);
    if (button >= 0 && button < (int)(sizeof(bits) / sizeof(bits[0])))
        desktopSwitch(PLAYER_1, bits[button], state == GLUT_DOWN);
}

// The callbacks go to the window the game opened, current by now. With
// evdev input, only the quit keys.
static void installWindowInput(void)
{
    void (*keyboardFunc)(void (*)(unsigned char, int, int)) = dlsym(RTLD_NEXT, "glutKeyboardFunc");
    void (*keyboardUpFunc)(void (*)(unsigned char, int, int)) = dlsym(RTLD_NEXT, "glutKeyboardUpFunc");
    void (*specialFunc)(void (*)(int, int, int)) = dlsym(RTLD_NEXT, "glutSpecialFunc");
    void (*specialUpFunc)(void (*)(int, int, int)) = dlsym(RTLD_NEXT, "glutSpecialUpFunc");
    void (*mouseFunc)(void (*)(int, int, int, int)) = dlsym(RTLD_NEXT, "glutMouseFunc");
    void (*motionFunc)(void (*)(int, int)) = dlsym(RTLD_NEXT, "glutMotionFunc");
    void (*passiveMotionFunc)(void (*)(int, int)) = dlsym(RTLD_NEXT, "glutPassiveMotionFunc");
    void (*ignoreKeyRepeat)(int) = dlsym(RTLD_NEXT, "glutIgnoreKeyRepeat");

    glutGet = dlsym(RTLD_NEXT, "glutGet");
    glutGetModifiers = dlsym(RTLD_NEXT, "glutGetModifiers");
    if (!keyboardFunc || !keyboardUpFunc || !specialFunc || !specialUpFunc || !mouseFunc || !motionFunc ||
        !passiveMotionFunc || !glutGet)
    {
        log_error("Raw Thrills: glut input functions not found, no keyboard and mouse input");
        return;
    }
    if (ignoreKeyRepeat)
        ignoreKeyRepeat(1);
    keyboardFunc(keyDown);
    keyboardUpFunc(keyUp);
    specialFunc(specialDown);
    specialUpFunc(specialUp);
    if (evdevInput)
        return;
    mouseFunc(mouse);
    motionFunc(motion);
    passiveMotionFunc(motion);
}

// ---------------------------------------------------------------------------
// The board: switch counters and gun shots.

static int buttonHeld(JVSIO *io, int player, int bit)
{
    return (io->state.inputSwitch[player] & bit) != 0;
}

// Count a transition of each board switch whose state changed. Inputs
// sharing a switch are or-ed.
// With evdev input, the game window's keys still work (the operator's test
// and service keys on a gun-only setup).
static int desktopKeyState(const RtIoInput *in)
{
    return in->type == RT_IO_SWITCH && (desktop.state.inputSwitch[in->player] & in->source) != 0;
}

// A coin of the evdev input (a count) holds its switch for a few polls, as
// a coin mechanism does, then leaves it up as long before the next one: a
// one-poll press is counted by the game but never shows in its switch test.
#define COIN_POLLS 4

static int coinHeld(int slot, int count)
{
    static int seen[2], phase[2];

    if (slot < 0 || slot > 1)
        return 0;
    if (count < seen[slot])
        seen[slot] = count;
    if (!phase[slot] && count > seen[slot])
    {
        seen[slot]++;
        phase[slot] = 2 * COIN_POLLS;
    }
    if (!phase[slot])
        return 0;
    return phase[slot]-- > COIN_POLLS;
}

// The board counts a switch's transitions, which the game reads once a
// poll: the presses the evdev input counted since (a pump pressed and
// released between two polls) are all in it (see rtSwitchCount).
#define MAX_JAMMA_SWITCH_INPUTS 64
static void updateSwitches(JVSIO *io)
{
    static unsigned int lastPresses[MAX_JAMMA_SWITCH_INPUTS];
    static int wasHeld[MAX_BOARD_SWITCHES];
    int held[MAX_BOARD_SWITCHES] = {0}, taps[MAX_BOARD_SWITCHES] = {0};
    int n = 0;

    for (const RtIoInput *in = jammaGame->jammaSwitches; in->type != RT_IO_END; in++, n++)
    {
        if (in->io >= MAX_BOARD_SWITCHES)
            continue;
        if (in->type == RT_IO_SWITCH && n < MAX_JAMMA_SWITCH_INPUTS && in->source && in->player <= PLAYER_4)
            taps[in->io] += rtSwitchTaps(io, in->player, in->source, &lastPresses[n]);
        if (in->type == RT_IO_COIN)
        {
            // The desktop's keys work with evdev too.
            if (in->player < 2)
                held[in->io] |= desktopCoinHeld[in->player] ||
                                (io != &desktop && coinHeld(in->player, io->state.coinCount[in->player]));
            continue;
        }
        held[in->io] |= rtIoSwitchState(in, io) | (io != &desktop && desktopKeyState(in));
    }
    for (int i = 0; i < MAX_BOARD_SWITCHES; i++)
        rtSwitchCount(&boardSwitches[i], &wasHeld[i], held[i], taps[i], 0);
}

// A gun's position as a fraction of the screen (from its top left); off
// the screen when the gun points at its edge (where light guns report what
// is outside).
int rtJammaPosition(JVSIO *io, int xChannel, int yChannel, float *x, float *y)
{
    int max = io->analogueMax, ax = io->state.analogueChannel[xChannel];
    int ay = io->state.analogueChannel[yChannel];
    float px, py;

    if (max <= 0 || ax <= 0 || ax >= max || ay <= 0 || ay >= max)
        return 0;
    // A point of the screen (or the window) to the same point of the
    // picture, when that does not fill it (frameAspect): off it, off screen.
    px = (float)ax / (max + 1);
    py = (float)ay / (max + 1);
    if (!frameScaleWindowToGame(&px, &py))
        return 0;
    *x = px;
    *y = py;
    return 1;
}

static int gunPosition(const RtJammaGun *gun, JVSIO *io, double *x, double *y)
{
    float px, py;

    if (!rtJammaPosition(io, gun->xChannel, gun->yChannel, &px, &py))
        return 0;
    *x = px;
    *y = py;
    return 1;
}

// Last position of each gun, for the IR gun manager's aim, and its trigger
// pulls.
static double aimX[MAX_GUNS], aimY[MAX_GUNS];
static int aimValid[MAX_GUNS];
static int triggerPulls[MAX_GUNS];

// A board gun's events for a shot, and for the trigger's release.
static void gunShot(const RtJammaGun *gun)
{
    if (gun->triggerEvent)
        postEvent(gun->triggerEvent, NULL);
    if (gun->shotEvent)
        postEvent(gun->shotEvent, NULL);
}

static void gunRelease(const RtJammaGun *gun)
{
    if (gun->releaseEvent)
        postEvent(gun->releaseEvent, NULL);
}

// The shot's position comes from the IR gun manager's aim (the game turns
// it into the board's sensor data itself); the trigger is reported by the
// board or by the IR gun manager. A shot off the screen reloads: the aim is
// then missing.
static void updateGun(int g, JVSIO *io)
{
    const RtJammaGun *gun = &jammaGame->jammaGuns[g];
    int offScreen = jammaGame->offScreenButton && buttonHeld(io, gun->player, BUTTON_3);
    int fire = buttonHeld(io, gun->player, BUTTON_1) || offScreen;
    double x, y;

    aimValid[g] = !offScreen && gunPosition(gun, io, &x, &y);
    if (aimValid[g])
    {
        aimX[g] = x;
        aimY[g] = y;
    }
    // Board guns: the position, before a press (the gun's number as its id,
    // as the game's own IR gun path posts it).
    if (jammaGame->gunPositionEvent)
    {
        GunPosition pos = {.id = g, .x = -1, .y = -1, .extra = {1, 1}};
        if (aimValid[g])
        {
            pos.x = (int)(x * jammaGame->gunWidth);
            pos.y = (int)(y * jammaGame->gunHeight);
            if (gunCalibration[g].mode == GUN_CALIBRATED_MODE)
            {
                pos.x = (int)(gunCalibration[g].scale[0] * pos.x + gunCalibration[g].offset[0]);
                pos.y = (int)(gunCalibration[g].scale[1] * pos.y + gunCalibration[g].offset[1]);
            }
        }
        postEvent(jammaGame->gunPositionEvent, &pos);
    }
    // Trigger pulls that began and ended between two polls (the evdev
    // input counts the presses): each one a whole shot, which the trigger's
    // state alone would lose. A release and a new pull within one poll end
    // the shot held before.
    static unsigned int lastPresses[MAX_GUNS];
    int taps = rtSwitchTaps(io, gun->player, BUTTON_1, &lastPresses[g]);
    if (taps > 0 && gunHeld[g])
    {
        gunRelease(gun);
        gunHeld[g] = 0;
    }
    for (taps -= fire; taps > 0; taps--)
    {
        triggerPulls[g]++;
        gunShot(gun);
        gunRelease(gun);
    }
    if (fire && !gunHeld[g])
    {
        triggerPulls[g]++;
        gunShot(gun);
    }
    else if (!fire && gunHeld[g])
        gunRelease(gun);
    gunHeld[g] = fire;
}

// The IR gun manager's count of a gun's trigger pulls.
static int gunTrigger(int gun)
{
    return gun >= 0 && gun < MAX_GUNS ? triggerPulls[gun] : 0;
}

// The IR gun manager's count of a gun's own button's presses or releases
// (see RtGame).
static int gunButton(int gun, int button)
{
    static int count[MAX_GUNS][5], wasHeld[MAX_GUNS][5];
    JVSIO *io = evdevInput ? getJVSIO() : &desktop;
    int press, release, bit, held;

    if (gun < 0 || gun >= MAX_GUNS || gun >= jammaGame->jammaGunCount || button < 0 || button >= 5)
        return 0;
    press = jammaGame->gunButtonPresses[button];
    release = jammaGame->gunButtonReleases[button];
    if (!(bit = press | release))
        return 0;
    held = buttonHeld(io, jammaGame->jammaGuns[gun].player, bit) ||
           (evdevInput && buttonHeld(&desktop, jammaGame->jammaGuns[gun].player, bit));
    if (held != wasHeld[gun][button])
    {
        if (held ? press : release)
            count[gun][button]++;
        wasHeld[gun][button] = held;
    }
    return count[gun][button];
}

// The IR gun manager's aim of a player's gun, in its camera space.
static int gunAim(int player, float *x, float *y, int unused)
{
    (void)unused;
    if (player < 0 || player >= MAX_GUNS)
        return -1;
    if (x)
        *x = aimValid[player] ? (float)(aimX[player] * jammaGame->gunAimWidth) : -1.0f;
    if (y)
        *y = aimValid[player] ? (float)(aimY[player] * jammaGame->gunAimHeight) : -1.0f;
    return 0;
}

// The game's requests to the board: switch counters from here, the others
// to its board API (which fails without a board).
static int jammaOp(int op, int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
    // The board's calibration: applied here (see RtGame).
    if (op && op == jammaGame->jammaGunCalibrateOp)
    {
        int measured[4] = {b, c, d, e}, targets[4] = {f, g, h, i};
        if (a >= 0 && a < MAX_GUNS)
            calibrateGun(a, measured, targets);
        return 0;
    }
    if (op && op == jammaGame->jammaGunCalibrationModeOp)
    {
        if (a >= 0 && a < MAX_GUNS)
            gunCalibration[a].mode = b;
        return 0;
    }
    if (op == JAMMAOP_GET_SWITCHES || op == JAMMAOP_GET_SWITCHES_FIRST)
    {
        if (op == JAMMAOP_GET_SWITCHES && a)
            *(int *)(uintptr_t)a = 1;
        if (b)
            *(uint32_t **)(uintptr_t)b = boardSwitches;
        return 0;
    }
    return jammaOpOrig(op, a, b, c, d, e);
}

// Each frame: update the board, then run the game's poll, which reads the
// switch counters and posts their events.
static int jammaPoll(void)
{
    static int started;
    JVSIO *io = evdevInput ? getJVSIO() : &desktop;

    if (!started)
    {
        started = 1;
        installWindowInput();
    }
    updateSwitches(io);
    for (int g = 0; g < jammaGame->jammaGunCount && g < MAX_GUNS; g++)
        updateGun(g, io);
    if (jammaGame->ioFrame)
        jammaGame->ioFrame(io);
    return jammaPollOrig();
}

void rtJammaDesktopSwitch(int player, int bit, int held)
{
    desktopSwitch(player, bit, held);
}

JVSIO *rtJammaInput(void)
{
    if (!jammaGame)
        return NULL;
    return evdevInput ? getJVSIO() : &desktop;
}

void rtInstallJamma(const RtGame *game)
{
    if (!game->jammaPollSymbol)
        return;
    jammaGame = game;
    evdevInput = getConfig()->inputMode == 2;
    if (game->jammaGunCount)
        *(void **)&postEvent = rtSymbol(game->postEventSymbol);
    jammaPollOrig = rtTrampoline(game->jammaPollSymbol, game->jammaPollPrologue);
    jammaOpOrig = rtTrampoline(game->jammaOpSymbol, game->jammaOpPrologue);
    if ((game->jammaGunCount && !postEvent) || !jammaPollOrig || !jammaOpOrig || rtDetour(game->jammaOpSymbol, jammaOp) != 0 ||
        rtDetour(game->jammaPollSymbol, jammaPoll) != 0)
        log_error("Raw Thrills: JAMMA board input not installed");
    if (game->gunAimSymbol && rtDetour(game->gunAimSymbol, gunAim) != 0)
        log_error("Raw Thrills: gun aim not installed");
    if (game->gunTriggerSymbol && rtDetour(game->gunTriggerSymbol, gunTrigger) != 0)
        log_error("Raw Thrills: gun trigger not installed");
    if (game->gunButtonSymbol && rtDetour(game->gunButtonSymbol, gunButton) != 0)
        log_error("Raw Thrills: gun buttons not installed");
}
