// Deal or No Deal (Raw Thrills, g3): the cabinet hardware a PC does not have
// (see docs/dond.md).
//
// The button panel. The cabinet's 16 case buttons, DEAL, NO DEAL and the
// panel's start button are switches on the parallel port (0x378), as are the
// lamps in them; the game drives it with in/out instructions of its own (in
// a dozen functions, its attract animation and test threads too), not
// through a driver. Without the I/O permission those fault: the loader's
// SIGSEGV handler hands the ones on the port to rtDondPortIo, which plays
// the panel. The control register (0x37a) selects a bank of 8 switches or
// lamps: switch banks are read with the port in input mode (0x20) and the
// strobe (0x08) up, bank 3 - (control & 3); lamp banks are written with the
// strobe up and latched, bank 2 - (control & 3). A switch's bit is clear
// while it is pressed.
//
// The dongle. A Rockey USB dongle (libusb) checks the game in a thread
// started by BankerOfferInitialize (BankerOfferCalculate, despite its name:
// the offers are the game's own): without the dongle, the game runs it on
// its main thread instead, where it loops forever clearing random bytes of
// its stack. Both are stubbed out (rtGames.c). Deluxe has a HASP HL instead,
// which also holds the cabinet's bonus wheel type (1..5): DongleCheck reads
// it at boot (none: the game only offers its operator setup, whatever is
// pressed), and the setup writes it back (DongleWriteWheel). The check
// succeeds with the wheel of this build, NJS (4), and the setup's choice is
// kept in memory.

#include <dlfcn.h>
#include <glad/gl.h>
#include <SDL3/SDL_gamepad.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "rawthrills.h"
#include "rtGame.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define LPT_DATA 0x378
#define LPT_STATUS 0x379
#define LPT_CONTROL 0x37a
#define LPT_INPUT_MODE 0x20
#define LPT_STROBE 0x08
#define LPT_BANKS 3

// Panel switches (the switch test's "LPT" page).
enum
{
    DOND_CASES = 16, // 0..15: CASE #1..#16
    DOND_DEAL = 16,
    DOND_NO_DEAL = 17,
    DOND_DOUBLE_DEAL = 18, // "LPT START": Double Deal on the title screen
};

// The controls (P1, the evdev input or the game window's):
//  - a pointer (mouse, gun: ANALOGUE_1/2) moves a crosshair, which
//    BUTTON_1 (the trigger, the left button, Enter or Space) presses the
//    case under;
//  - a pad's left stick moves the same crosshair too, at a speed (evdev:
//    ANALOGUE_5/6; the game window's: the SDL gamepad, whose A is BUTTON_1,
//    X DEAL, B NO DEAL, Y DOUBLE DEAL and Start START);
//  - BUTTON_2 is DEAL (D), BUTTON_3 NO DEAL (N), BUTTON_4 DOUBLE DEAL (B);
//  - the volume is the other g3 games' (P1 UP/DOWN, the arrow keys).
#define DOND_PICK BUTTON_1
#define DOND_DEAL_BUTTON BUTTON_2
#define DOND_NO_DEAL_BUTTON BUTTON_3
#define DOND_DOUBLE_DEAL_BUTTON BUTTON_4

// The cases as the game shows them while they are picked and opened, in its
// 1366x768 picture, from case 1: the US build's stage, the numbers the
// models hold up by row from the front; the UK build's board of boxes.
static const float usCases[DOND_CASES][2] = {
    {348, 544}, {528, 544}, {700, 544}, {884, 544}, {452, 444}, {618, 444}, {788, 444}, {958, 444},
    {360, 316}, {526, 316}, {702, 316}, {880, 316}, {460, 206}, {626, 206}, {788, 206}, {940, 206},
};
static const float ukCases[DOND_CASES][2] = {
    {500, 474}, {610, 474}, {724, 474}, {834, 474}, {536, 360}, {640, 360}, {744, 360}, {850, 360},
    {500, 240}, {608, 240}, {714, 240}, {820, 240}, {534, 120}, {640, 120}, {744, 120}, {844, 120},
};
#define PICTURE_WIDTH 1366.f
#define PICTURE_HEIGHT 768.f

// A press further than this from every case (in the picture's height) is
// none's.
#define CASE_REACH 0.15f

static uint8_t control;
static uint8_t lamps[LPT_BANKS];
// The panel's switches held, read by the port emulation (bit set: held).
static volatile uint32_t held;

// The crosshair: where it is (a fraction of the picture from its top left),
// whether it is shown, and when it last moved.
static float aimX = 0.5f, aimY = 0.5f;
static int aimShown;
static struct timespec aimMoved;
#define AIM_HIDE_S 5

static int nearestCase(float x, float y)
{
    const float (*cases)[2] = rtCurrentGame()->crc32 == DEAL_OR_NO_DEAL_UK_RT ? ukCases : usCases;
    int best = -1;
    float bestDistance = CASE_REACH * CASE_REACH;

    for (int i = 0; i < DOND_CASES; i++)
    {
        // Distances in the picture's height.
        float dx = (x - cases[i][0] / PICTURE_WIDTH) * PICTURE_WIDTH / PICTURE_HEIGHT;
        float dy = y - cases[i][1] / PICTURE_HEIGHT;
        if (dx * dx + dy * dy < bestDistance)
        {
            bestDistance = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

static void aimAt(float x, float y)
{
    aimX = x;
    aimY = y;
    aimShown = 1;
    clock_gettime(CLOCK_MONOTONIC, &aimMoved);
}

// The stick: its deflection past the dead zone (a fraction of its travel
// from the centre) moves the crosshair, across the picture's width in
// STICK_CROSS_S at full deflection.
#define STICK_DEAD_ZONE 0.2f
#define STICK_CROSS_S 1.2f

static float stickSpeed(float v)
{
    float a = v < 0 ? -v : v;

    if (a <= STICK_DEAD_ZONE)
        return 0;
    a = (a - STICK_DEAD_ZONE) / (1 - STICK_DEAD_ZONE);
    return v < 0 ? -a : a;
}

// The stick's deflection, -1..1 each way: evdev, ANALOGUE_5/6 once they
// moved (they read 0 until the stick's first event); the game window's, the
// SDL gamepad.
static int stickDeflection(JVSIO *io, float *sx, float *sy)
{
    static int evdevMoved, first[2] = {-1, -1};
    EmulatorConfig *config = getConfig();

    if (config->inputMode != 2)
    {
        desktopGamepadEnable();
        *sx = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.f;
        *sy = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY) / 32768.f;
        return 1;
    }
    if (!config->arcadeInputs.analogue_5[0] || io->analogueMax <= 0)
        return 0;
    int x = io->state.analogueChannel[ANALOGUE_5], y = io->state.analogueChannel[ANALOGUE_6];
    if (first[0] < 0)
    {
        first[0] = x;
        first[1] = y;
    }
    evdevMoved |= x != first[0] || y != first[1];
    if (!evdevMoved)
        return 0;
    *sx = 2.f * x / io->analogueMax - 1;
    *sy = 2.f * y / io->analogueMax - 1;
    return 1;
}

static void stickAim(JVSIO *io)
{
    static struct timespec last;
    struct timespec now;
    float sx, sy;

    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = last.tv_sec ? (now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9f : 0;
    last = now;
    if (dt > 0.1f)
        dt = 0.1f;
    if (!stickDeflection(io, &sx, &sy))
        return;
    sx = stickSpeed(sx);
    sy = stickSpeed(sy);
    if (!sx && !sy)
        return;
    // The same speed both ways, in the picture's width.
    float step = dt / STICK_CROSS_S;
    float x = aimX + sx * step, y = aimY + sy * step * PICTURE_WIDTH / PICTURE_HEIGHT;
    aimAt(x < 0 ? 0 : x > 1 ? 1 : x, y < 0 ? 0 : y > 1 ? 1 : y);
}

// The SDL gamepad's buttons, in the game window's input (see above).
static uint32_t padButtons(void)
{
    static const struct
    {
        int button;
        uint32_t bit;
    } map[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, DOND_PICK},
        {SDL_GAMEPAD_BUTTON_WEST, DOND_DEAL_BUTTON},
        {SDL_GAMEPAD_BUTTON_EAST, DOND_NO_DEAL_BUTTON},
        {SDL_GAMEPAD_BUTTON_NORTH, DOND_DOUBLE_DEAL_BUTTON},
        {SDL_GAMEPAD_BUTTON_START, BUTTON_START},
    };
    static uint32_t last;
    uint32_t held = 0;

    if (getConfig()->inputMode == 2)
        return 0;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (desktopGamepadButton(map[i].button))
            held |= map[i].bit;
    // Start is the board's switch, read before this (next poll's).
    if ((held ^ last) & BUTTON_START)
        rtJammaDesktopSwitch(PLAYER_1, BUTTON_START, (held & BUTTON_START) != 0);
    last = held;
    return held & ~BUTTON_START;
}

// Each frame (the JAMMA board's poll): the crosshair, and the switches the
// input holds. A case is pressed where the crosshair is when BUTTON_1 goes
// down, and held as long as it is.
void rtDondIoFrame(JVSIO *io)
{
    static uint32_t lastButtons;
    static int lastX = -1, lastY = -1, pickedCase = -1;
    uint32_t buttons = io->state.inputSwitch[PLAYER_1] | padButtons();
    uint32_t pressed = buttons & ~lastButtons, now = 0;
    int x = io->state.analogueChannel[ANALOGUE_1], y = io->state.analogueChannel[ANALOGUE_2];
    float px, py;

    // The pointer moved: the crosshair follows it; the stick moves it on.
    if ((x != lastX || y != lastY) && rtJammaPosition(io, ANALOGUE_1, ANALOGUE_2, &px, &py))
        aimAt(px, py);
    lastX = x;
    lastY = y;
    stickAim(io);

    if (pressed & DOND_PICK)
    {
        pickedCase = nearestCase(aimX, aimY);
        if (pickedCase >= 0)
            printf("Deal or No Deal: case %d\n", pickedCase + 1);
    }
    if (!(buttons & DOND_PICK))
        pickedCase = -1;
    if (pickedCase >= 0)
        now |= 1u << pickedCase;
    if (buttons & DOND_DEAL_BUTTON)
        now |= 1u << DOND_DEAL;
    if (buttons & DOND_NO_DEAL_BUTTON)
        now |= 1u << DOND_NO_DEAL;
    if (buttons & DOND_DOUBLE_DEAL_BUTTON)
        now |= 1u << DOND_DOUBLE_DEAL;
    held = now;
    lastButtons = buttons;
}

// The game window's keys, as the pad's buttons (see above).
int rtDondDesktopKey(int key, int special, int held)
{
    int bit = 0;

    if (special)
        return 0;
    switch (key)
    {
    case '\r':
    case ' ':
        bit = DOND_PICK;
        break;
    case 'd':
    case 'D':
        bit = DOND_DEAL_BUTTON;
        break;
    case 'n':
    case 'N':
        bit = DOND_NO_DEAL_BUTTON;
        break;
    case 'b':
    case 'B':
        bit = DOND_DOUBLE_DEAL_BUTTON;
        break;
    }
    if (!bit)
        return 0;
    rtJammaDesktopSwitch(PLAYER_1, bit, held);
    return 1;
}

// The crosshair, a white cross outlined in black, drawn with scissored
// clears (no GL state of the game's to keep but the scissor and the clear
// colour). Hidden a while after it last moved.
static void fillRect(int x, int y, int w, int h, float grey)
{
    glad_glClearColor(grey, grey, grey, 1);
    glad_glScissor(x, y, w, h);
    glad_glClear(GL_COLOR_BUFFER_BIT);
}

void rtDondFrameDraw(int x, int y, int width, int height)
{
    struct timespec now;
    GLfloat clearColour[4];
    GLint scissorBox[4];

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!aimShown || now.tv_sec - aimMoved.tv_sec > AIM_HIDE_S)
        return;
    int cx = x + (int)(aimX * width), cy = y + (int)((1 - aimY) * height);
    int arm = height / 30, thick = height / 300 > 1 ? height / 300 : 1, edge = thick > 1 ? thick : 1;

    glad_glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColour);
    glad_glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    GLboolean scissor = glad_glIsEnabled(GL_SCISSOR_TEST);
    glad_glEnable(GL_SCISSOR_TEST);
    fillRect(cx - arm - edge, cy - thick - edge, 2 * (arm + edge), 2 * (thick + edge), 0);
    fillRect(cx - thick - edge, cy - arm - edge, 2 * (thick + edge), 2 * (arm + edge), 0);
    fillRect(cx - arm, cy - thick, 2 * arm, 2 * thick, 1);
    fillRect(cx - thick, cy - arm, 2 * thick, 2 * arm, 1);
    if (!scissor)
        glad_glDisable(GL_SCISSOR_TEST);
    glad_glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    glad_glClearColor(clearColour[0], clearColour[1], clearColour[2], clearColour[3]);
}

int rtDondPortIo(int in, uint16_t port, uint32_t *eax)
{
    const RtGame *game = rtCurrentGame();

    if (!game || !game->lptPanel || port < LPT_DATA || port > LPT_CONTROL)
        return 0;
    if (!in)
    {
        uint8_t value = *eax & 0xff;
        if (port == LPT_CONTROL)
            control = value;
        else if (port == LPT_DATA && (control & LPT_STROBE) && !(control & LPT_INPUT_MODE) && (control & 3) < LPT_BANKS)
            lamps[2 - (control & 3)] = value;
        return 1;
    }
    uint8_t value = 0xff;
    // A switch's bit is clear while it is held.
    if (port == LPT_DATA && (control & LPT_INPUT_MODE) && (control & 3))
        value = ~(held >> (8 * (3 - (control & 3))));
    else if (port == LPT_CONTROL)
        value = control;
    *eax = (*eax & ~0xffu) | value;
    return 1;
}

// The game's I/O permission requests (ioperm) succeed, and its SIGSEGV
// handler (a crash report) is not installed: the port instructions fault
// into the loader's.
static int ioperm(unsigned long from, unsigned long num, int on)
{
    (void)from;
    (void)num;
    (void)on;
    return 0;
}

typedef void (*SignalHandler)(int);

static SignalHandler gameSignal(int signum, SignalHandler handler)
{
    SignalHandler (*real)(int, SignalHandler) = dlsym(RTLD_NEXT, "signal");
    if (signum == SIGSEGV)
        return SIG_DFL;
    return real(signum, handler);
}

static int gameSigaction(int signum, const struct sigaction *act, struct sigaction *old)
{
    int (*real)(int, const struct sigaction *, struct sigaction *) = dlsym(RTLD_NEXT, "sigaction");
    if (signum == SIGSEGV && act)
        return 0;
    return real(signum, act, old);
}

#define DLX_WHEEL_NJS 4

static int *wheelType;

static int dongleCheck(void)
{
    *wheelType = DLX_WHEEL_NJS;
    return 0;
}

static int dongleWriteWheel(int type)
{
    *wheelType = type;
    return 0;
}

void rtDondInstall(const RtGame *game)
{
    (void)game;
    wheelType = rtSymbol("DongleWheelType");
    if (wheelType && (rtDetour("DongleCheck", dongleCheck) != 0 || rtDetour("DongleWriteWheel", dongleWriteWheel) != 0))
        log_warn("Raw Thrills: Deal or No Deal Deluxe's dongle not installed");
}

void *rtDondOverride(const char *name)
{
    if (!strcmp(name, "ioperm"))
        return (void *)ioperm;
    if (!strcmp(name, "signal"))
        return (void *)gameSignal;
    if (!strcmp(name, "sigaction"))
        return (void *)gameSigaction;
    return NULL;
}
