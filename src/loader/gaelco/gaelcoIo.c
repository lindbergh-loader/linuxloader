// Gaelco's I/O board (see gaelco.h), from the loader's JVS state.
//
// The board (PCI, vendor 0xeff0) has two switch bytes, a key code (the
// buttons and the keypad behind an encoder: one key at a time, 0x10-0x1f,
// which the game takes after three equal reads) and five potentiometers'
// ADCs (the wheel, the pedals). Tokyo Cop reads them in the board's PCI
// configuration space (registers 0x40-0x47, iolinux::inport), the later
// games in its memory window (offsets 0-8, the driver's IOMEM_READB). Coins
// are counted by the module (a 20 ms timer on the coin switches): the game
// reads the counts (IOINTF_CMD_GETCURRENTCOINS).
//
// GAELCO_IO_TRACE=1 prints the registers as they change;
// GAELCO_SCRIPT=seconds:switch0:switch1:key[:pot0:pot1:pot2:pot3][,...]
// (hex bytes) sets the switch bytes, the key code and the pots from a time
// on (for finding the cabinet's
// buttons in the game's test menu).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gaelco.h"
#include "../patching/flowControl.h"
#include "../hardware/lindbergh/jvs.h"

// Tokyo Cop's module (2003) has commands 0-11: 8 and 9 are port I/O (inb,
// outb), 11 tells whether the sound commands have been run (gaelcoSound.c);
// the later modules' 8 and 9 are the board's memory window, 11-15 those
// below.
#define IOINTF_CMD_INITTIMER 5
#define IOINTF_CMD_GETCURRENTCOINS 6
#define IOINTF_CMD_GETCURRENTCOINS2 7
#define IOINTF_CMD_IOMEM_READB 8 // Tokyo Cop: inb
#define IOINTF_CMD_IOMEM_WRITEB 9 // Tokyo Cop: outb
#define IOINTF_CMD_GETVSYNC 10
#define IOINTF_CMD_IOMEM_SPEEDDRIVER_WRITEB 11
#define IOINTF_CMD_IOMEM_SPEEDDRIVER_READB 12
#define IOINTF_CMD_MEM_READB 14
#define IOINTF_CMD_MEM_WRITEB 15
#define IOINTF_CMD_STARTSTEERTIMER 16
#define IOINTF_CMD_STOPSTEERTIMER 17
#define IOINTF_CMD_STARTFORCEFEEDBACK 18
#define IOINTF_CMD_STOPFORCEFEEDBACK 19
#define IOINTF_CMD_GETSTEERSPEED 20
#define IOINTF_CMD_CAMERA_STARTCAPTURE 21 // Tuning Race's camera (camera.c)
#define IOINTF_CMD_CAMERA_SETSIZE 22
#define IOINTF_CMD_CAMERA_GETFRAME 23

// The board's inputs, as the cabinet sets them.
typedef struct
{
    uint8_t switches[2]; // active low
    uint8_t key;         // 0x10-0x1f: a key held, 0: none
    uint8_t pots[5];     // the wheel, the pedals
} Inputs;

static int trace = -1;
static int coinBase[2] = {-1, -1};

// A coin switch, closed for a while after each coin.
typedef struct
{
    int count;
    double until;
} CoinPulse;

static CoinPulse coinPulse[2] = {{-1, 0}, {-1, 0}};
static int scriptCoins[1];

static int pulse(CoinPulse *p, int count)
{
    double now = gaelcoVsyncCount() / 60.0;

    if (p->count >= 0 && count != p->count)
        p->until = now + 0.1;
    p->count = count;
    return now < p->until;
}

static double unit(double value)
{
    return value < 0 ? 0 : value > 1 ? 1 : value;
}

// The cabinet's controls, from the loader's JVS state: a driving game's
// (the wheel or handlebar, gas and brake on ANALOGUE_1-3; START, BUTTON_1
// and BUTTON_2, the arrows for a pad without sticks).
typedef struct
{
    int start, view, button1, service, test;
    int gearUp, gearDown;
    int coins[2]; // the coin chutes, closed
    double steer, gas, brake; // 0-1 (steer: left to right)
} Controls;

// The arrows turn the wheel as a hand would, not at once: from the centre
// to a lock in 0.4 s, back to the centre in 0.15 s once let go (a tap a
// small correction, not a full lock). steer: the analogue wheel's, kept
// when no arrow is held and the wheel is back in the centre.
static double keyboardSteer(unsigned player, double steer)
{
    static double keySteer = 0.5, last = -1;
    double now = gaelcoVsyncCount() / 60.0;
    double dt = last < 0 ? 0 : now - last;
    int left = (player & BUTTON_LEFT) != 0, right = (player & BUTTON_RIGHT) != 0;
    double target = left == right ? 0.5 : left ? 0 : 1;
    double rate = target == 0.5 ? 0.5 / 0.15 : 0.5 / 0.4;

    last = now;
    if (dt > 0.1)
        dt = 0.1;
    if (keySteer < target)
        keySteer = keySteer + rate * dt > target ? target : keySteer + rate * dt;
    else if (keySteer > target)
        keySteer = keySteer - rate * dt < target ? target : keySteer - rate * dt;
    return left || right || keySteer != 0.5 ? keySteer : steer;
}

static void readControls(Controls *c)
{
    JVSIO *io = getJVSIO();
    unsigned player = io->state.inputSwitch[PLAYER_1];
    double max = io->analogueMax > 0 ? io->analogueMax : 255;

    c->steer = unit(io->state.analogueChannel[ANALOGUE_1] / max);
    c->gas = unit(io->state.analogueChannel[ANALOGUE_2] / max);
    c->brake = unit(io->state.analogueChannel[ANALOGUE_3] / max);
    c->steer = keyboardSteer(player, c->steer);
    if (player & BUTTON_UP)
        c->gas = 1;
    if (player & BUTTON_DOWN)
        c->brake = 1;
    c->start = (player & BUTTON_START) != 0;
    c->button1 = (player & BUTTON_1) != 0;
    c->view = (player & BUTTON_2) != 0;
    // (the driving bindings' gear up and down: player 2's up and down)
    c->gearUp = (io->state.inputSwitch[PLAYER_2] & BUTTON_UP) != 0;
    c->gearDown = (io->state.inputSwitch[PLAYER_2] & BUTTON_DOWN) != 0;
    c->service = (player & BUTTON_SERVICE) != 0;
    c->test = (io->state.inputSwitch[SYSTEM] & BUTTON_TEST) != 0;
    c->coins[0] = pulse(&coinPulse[0], io->state.coinCount[0]);
    c->coins[1] = pulse(&coinPulse[1], io->state.coinCount[1]);
}

static uint8_t pot(double value)
{
    return (uint8_t)lround(unit(value) * 255);
}

// Tokyo Cop (its test menu's controls page): register 0x40 bit 5 the horn,
// which is the start button, bit 4 the brake pedal (a switch), bit 6 the
// emergency stop button (set: pushed, the game stops); 0x41 bit 0 the view,
// bit 1 the siren, bits 3 and 4 the coin chutes; the key encoder's first
// column the service credit, the test, the volume up and down, the others
// the keypad (1 2 3, 4 5 6, 7 8 9, * 0 #); 0x44 the wheel (the game takes
// 0xff - value), 0x45 the gas pedal.
static void tokyoCop(const Controls *c, Inputs *in)
{
    in->switches[0] = 0xbf;
    in->switches[1] = 0xff;
    // The game toggles the siren at a press of the start button while
    // racing (Juego: estado 0x18, subestado 2), where it has no other use:
    // there, the siren is BUTTON_1 (which closes it too), not START.
    int racing = *(volatile int *)0x087937c8 == 0x18 && *(volatile int *)0x08793c84 == 2;
    if ((c->start && !racing) || c->button1)
        in->switches[0] &= ~0x20;
    if (c->brake > 0.25)
        in->switches[0] &= ~0x10;
    if (c->view)
        in->switches[1] &= ~0x01;
    if (c->button1)
        in->switches[1] &= ~0x02;
    if (c->coins[0])
        in->switches[1] &= ~0x08;
    if (c->coins[1])
        in->switches[1] &= ~0x10;
    in->key = c->test ? 0x14 : c->service ? 0x10 : 0;
    in->pots[0] = 0xff - pot(c->steer);
    in->pots[1] = pot(c->gas);
}

// Ring Riders (its test menu's input/output page): offset 0 bit 0 the
// service credit, 1 the test, 2 and 3 the volume up and down, 4 the brake
// lever's switch, 5 the start button (and its light ring), 6 the emergency
// stop (set: pushed), 7 the footrest detection; offset 1 bit 0 the view
// button, 2 and 3 the coin chutes 2 and 1; offsets 4 and 5 the handlebar
// and the throttle (the game takes 0xff - value), 6 the brake, 8 the
// rider's leaning (the wheel too).
static void ringRiders(const Controls *c, Inputs *in)
{
    in->switches[0] = 0xbf;
    in->switches[1] = 0xff;
    if (c->service)
        in->switches[0] &= ~0x01;
    if (c->test)
        in->switches[0] &= ~0x02;
    if (c->brake > 0.1)
        in->switches[0] &= ~0x10;
    if (c->start)
        in->switches[0] &= ~0x20;
    if (c->view)
        in->switches[1] &= ~0x01;
    if (c->coins[1])
        in->switches[1] &= ~0x04;
    if (c->coins[0])
        in->switches[1] &= ~0x08;
    in->pots[0] = 0xff - pot(c->steer);
    in->pots[1] = 0xff - pot(c->gas);
    in->pots[2] = pot(c->brake);
    in->pots[3] = 0x80;
    in->pots[4] = pot(c->steer);
}

// Gaelco Championship Tuning Race (its test menu's controls page): offset
// 0 bits 0 and 1 the coin chutes 2 and 1, 2 the start button, 3 the view
// button, 4 and 5 the gear up and down, 6 the emergency stop (set:
// pushed); the key encoder as Tokyo Cop's (its first column in the other
// word); offsets 4-6 the wheel (the game takes 0xff - value), the gas and
// brake pedals, 7 and 8 the motion seat's position (centred).
static void tuningRace(const Controls *c, Inputs *in)
{
    in->switches[0] = 0xbf;
    in->switches[1] = 0xff;
    if (c->coins[1])
        in->switches[0] &= ~0x01;
    if (c->coins[0])
        in->switches[0] &= ~0x02;
    if (c->start)
        in->switches[0] &= ~0x04;
    if (c->view)
        in->switches[0] &= ~0x08;
    if (c->gearUp)
        in->switches[0] &= ~0x10;
    if (c->gearDown)
        in->switches[0] &= ~0x20;
    in->key = c->test ? 0x14 : c->service ? 0x10 : 0;
    in->pots[0] = 0xff - pot(c->steer);
    in->pots[1] = pot(c->gas);
    in->pots[2] = pot(c->brake);
    in->pots[3] = 0x80;
    in->pots[4] = 0x80;
}

static void readInputs(Inputs *in)
{
    Controls c;
    const char *env;

    memset(in, 0, sizeof(*in));
    readControls(&c);
    switch (gaelcoCurrentGame()->controls)
    {
    case GAELCO_TOKYO_COP:
        tokyoCop(&c, in);
        break;
    case GAELCO_RING_RIDERS:
        ringRiders(&c, in);
        break;
    case GAELCO_TUNING_RACE:
        tuningRace(&c, in);
        break;
    }

    if ((env = getenv("GAELCO_SCRIPT")))
    {
        double now = gaelcoVsyncCount() / 60.0;
        while (*env)
        {
            char *end;
            double at = strtod(env, &end);
            unsigned s0, s1, key, pots[4];
            int n;
            if (at > now || (n = sscanf(end, ":%x:%x:%x:%x:%x:%x:%x", &s0, &s1, &key, &pots[0], &pots[1], &pots[2],
                                         &pots[3])) < 3)
                break;
            in->switches[0] = (uint8_t)s0;
            in->switches[1] = (uint8_t)s1;
            in->key = (uint8_t)key;
            for (int i = 0; i + 3 < n; i++)
                in->pots[i] = (uint8_t)pots[i];
            env = strchr(end, ',');
            if (!env)
                break;
            env++;
        }
    }

    // (coin chute 1 closed by the script counts a coin)
    if (env)
    {
        static int closed;
        int tuningRace = gaelcoCurrentGame()->controls == GAELCO_TUNING_RACE;
        int now = tuningRace ? !(in->switches[0] & 0x02) : !(in->switches[1] & 0x08);
        if (now && !closed)
            scriptCoins[0]++;
        closed = now;
    }

    if (trace < 0)
        trace = getenv("GAELCO_IO_TRACE") != NULL;
    if (trace)
    {
        static Inputs last;
        if (memcmp(&last, in, sizeof(last)))
            printf("Gaelco I/O: switches %02x %02x key %02x pots %02x %02x %02x %02x %02x\n", in->switches[0],
                   in->switches[1], in->key, in->pots[0], in->pots[1], in->pots[2], in->pots[3], in->pots[4]);
        last = *in;
    }
}

// The board's PCI configuration registers (Tokyo Cop): 0x40 and 0x41 the
// switches, 0x42 the key code, 0x44-0x47 the potentiometers.
int gaelcoIoInport(int port, char *value)
{
    Inputs in;

    readInputs(&in);
    switch (port)
    {
    case 0x40:
    case 0x41:
        *value = (char)in.switches[port - 0x40];
        break;
    case 0x42:
        *value = (char)in.key;
        break;
    case 0x44:
        *value = (char)in.pots[0];
        break;
    case 0x45:
    case 0x46:
    case 0x47:
        *value = (char)in.pots[port - 0x44];
        break;
    default:
        *value = (char)0xff;
        break;
    }
    return 0;
}

// The lamps, the wheel's motor: not emulated.
int gaelcoIoOutport(int port, char *value)
{
    (void)port;
    (void)value;
    return 0;
}

// The memory window (Ring Riders, Tuning Race): offsets 0 and 1 the
// switches, 2 the key code, 4-8 the potentiometers; 0x0c-0x10 the RS485
// port (0x0c bit 7: a byte received, 0x0f: receiving, sending, 0x10 the
// byte).
static int memoryByte(int offset)
{
    Inputs in;

    switch (offset)
    {
    case 0x0c:
        return gaelcoVariadorAvailable() ? 0x80 : 0;
    case 0x0d:
    case 0x0f:
        return 0;
    case 0x10:
        return gaelcoVariadorPeek();
    }
    readInputs(&in);
    switch (offset)
    {
    case 0:
        return in.switches[0];
    case 1:
        return in.switches[1];
    case 2:
        return in.key;
    case 4:
        return in.pots[0];
    case 5:
    case 6:
    case 7:
    case 8:
        return in.pots[offset - 4];
    }
    return 0xff;
}

// The VGA's input status: bit 3 the vertical retrace, at 60 Hz.
static int vgaStatus(void)
{
    struct timespec ts;
    double frame;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    frame = (ts.tv_sec + ts.tv_nsec / 1e9) * 60.0;
    return frame - floor(frame) < 0.08 ? 0x08 : 0x00;
}

static int coins(int slot)
{
    JVSIO *io = getJVSIO();

    if (coinBase[slot] < 0)
        coinBase[slot] = io->state.coinCount[slot];
    return io->state.coinCount[slot] - coinBase[slot] + (slot == 0 ? scriptCoins[0] : 0);
}

int gaelcoIoCommand(int command, int *parms, int *result)
{
    *result = 1;
    switch (command)
    {
    case IOINTF_CMD_INITTIMER:
        return 1;
    case IOINTF_CMD_GETCURRENTCOINS:
        *result = coins(0);
        return 1;
    case IOINTF_CMD_GETCURRENTCOINS2:
        *result = coins(1);
        return 1;
    case IOINTF_CMD_IOMEM_READB:
        if (gaelcoCurrentGame()->io == GAELCO_IO_PCI_CONFIG)
            *result = (parms[0] & 0xffff) == 0x3da ? vgaStatus() : 0xff;
        else
            *result = memoryByte(parms[0]);
        return 1;
    case IOINTF_CMD_IOMEM_WRITEB:
        // The RS485 port: 0x10 a byte to send, 0x0d 2 the byte received
        // taken (the lamps, the motor: not emulated).
        if (gaelcoCurrentGame()->io == GAELCO_IO_MEMORY)
        {
            if (parms[0] == 0x10)
                gaelcoVariadorSend(parms[1]);
            else if (parms[0] == 0x0d && parms[1] == 2)
                gaelcoVariadorNext();
        }
        return 1;
    case IOINTF_CMD_GETVSYNC:
        *result = (int)gaelcoVsyncCount();
        return 1;
    case IOINTF_CMD_IOMEM_SPEEDDRIVER_WRITEB:
        if (gaelcoCurrentGame()->io == GAELCO_IO_PCI_CONFIG)
            return 0;
        *result = 0;
        return 1;
    case IOINTF_CMD_IOMEM_SPEEDDRIVER_READB:
        *result = 0;
        return 1;
    case IOINTF_CMD_MEM_READB:
        *result = (parms[0] & 0xffff) == 0x3da ? vgaStatus() : 0xff;
        return 1;
    case IOINTF_CMD_MEM_WRITEB:
    case IOINTF_CMD_STARTSTEERTIMER:
    case IOINTF_CMD_STOPSTEERTIMER:
    case IOINTF_CMD_STARTFORCEFEEDBACK:
    case IOINTF_CMD_STOPFORCEFEEDBACK:
        return 1;
    case IOINTF_CMD_CAMERA_STARTCAPTURE:
    case IOINTF_CMD_CAMERA_SETSIZE:
        return 1;
    case IOINTF_CMD_CAMERA_GETFRAME:
        // No camera: no frame is ever captured (the player's photo is
        // skipped).
        *result = 0;
        return 1;
    case IOINTF_CMD_GETSTEERSPEED:
        // The wheel's speed (Tuning Race's car physics, 128: still): always
        // still. The wheel's change between the game's reads made its car
        // spin round at a turn of the arrows or a pad's stick.
        *result = 128;
        return 1;
    }
    return 0;
}

// The games' potentiometers: a minimum, the value, a maximum (the range
// their calibration grows), the value scaled to -1..1.
typedef struct
{
    int32_t min, value, max;
    float scaled;
} Potentiometer;

static void (*realIniControllers)(void *, void *, void *);
static void (*realIniPotes)(void);

// Each range as it is once the control has been moved end to end.
static void fullRanges(Potentiometer *pots)
{
    const GaelcoGame *g = gaelcoCurrentGame();

    for (int i = 0; pots && i < g->potentiometerCount; i++)
    {
        pots[i].min = g->potentiometerMargin;
        pots[i].max = 0xff - g->potentiometerMargin;
    }
}

// After the game's set-up of its controls.
__attribute__((force_align_arg_pointer)) static void iniControllers(void *a, void *b, void *c)
{
    const GaelcoGame *g = gaelcoCurrentGame();

    realIniControllers(a, b, c);
    fullRanges(g->potentiometers ? (Potentiometer *)(uintptr_t)g->potentiometers : c);
}

// After Tokyo Cop's second set-up of the same ranges (empty again).
__attribute__((force_align_arg_pointer)) static void iniPotes(void)
{
    realIniPotes();
    fullRanges((Potentiometer *)(uintptr_t)gaelcoCurrentGame()->potentiometers);
}

void gaelcoIoInstall(void)
{
    const GaelcoGame *g = gaelcoCurrentGame();

    if (g->iniControllers)
        *(void **)&realIniControllers =
            trampolineHook((void *)(uintptr_t)g->iniControllers, (void *)iniControllers, g->iniControllersPrologue);
    if (g->iniPotes && g->potentiometers)
        *(void **)&realIniPotes = trampolineHook((void *)(uintptr_t)g->iniPotes, (void *)iniPotes, g->iniPotesPrologue);
}
