// The A27 protection board of IGS's early PC games (Speed Driver, 2003) and
// the game's link to it, /dev/pccard0 (see igsGame.h's a27Rom).
//
// The board is an IGS027A, an ARM7TDMI with an internal ROM, running its
// external ROM (the game's a27Rom); much of the game runs there, the cars
// among it, so the board is emulated as it is: the CPU (igsArm7.c) and the
// IGS027A's peripherals as MAME has them (src/mame/igs/igs027a.cpp), plus
// the board's own side of the PCI link, found in the firmware:
//
//   0x00000000  the internal ROM (16 KiB): not dumped; it sets the stacks up
//               and starts the external ROM's main (the third word of the
//               table the ROM's first instruction loads), and its IRQ vector
//               goes to the external ROM's handler (0x0800002c)
//   0x08000000  the external ROM
//   0x10000000  internal RAM
//   0x18000000  external RAM
//   0x28200000, 0x28300000, 0x28400000  output latches (the board's lamps, its link's
//               handshake lines): kept
//   0x38000000  the PC's command byte (read), the board's answer (write:
//               0xf1, the reply is ready)
//   0x40000018  the IGS027A's output port; 0x4000000c its input port
//   0x48000000  the RAM shared with the PC (128 KiB): the PC's request at 0
//               (0x230 bytes), the reply at 8 + word 0 * 1024 + word 1
//               (0x23c bytes, its place random: words 0 and 1)
//   0x58000000  read: the PC's interrupt acknowledged
//   0x70000100  the IGS027A's timer rates (timer 0, 1)
//   0x70000200  its interrupts: read, pending (a 0 bit each: timers 0, 1,
//               bit 3 the PC's command), and cleared; written, their mask
//
// The game's side (the IGS driver's /dev/pccard0): lseek sets the command
// byte of the next write, which hands the request over (copied to the shared RAM, the command
// raising the board's interrupt), read waits for the board's answer and
// returns the reply (whatever length it is asked); both return the time
// waited, which the game checks against its timeouts.
//
// The board runs on its own clock (MAME's 22 MHz, an instruction a cycle):
// at each request it catches up with the wall clock (up to a frame's worth),
// then runs on until it answers, ahead of the clock if need be.

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "igs.h"
#include "igsArm7.h"
#include <SDL3/SDL_gamepad.h>
#include "../common/desktopInput.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../input/evdevFfb.h"
#include "../log/log.h"

#define A27_CLOCK 22000000
#define A27_TIMER_DIVISOR 4263
#define A27_CATCH_UP (A27_CLOCK / 20)
#define A27_BOOT (A27_CLOCK / 2)
#define A27_STEP (A27_CLOCK / 500)
#define A27_ANSWER_LIMIT ((uint64_t)A27_CLOCK * 2)

#define ROM_SIZE 0x400000
#define INTERNAL_RAM_SIZE 0x10000
#define EXTERNAL_RAM_SIZE 0x100000
#define SHARED_RAM_SIZE 0x20000

#define REQUEST_SIZE 0x230
#define REPLY_SIZE 0x23c
#define ANSWER_READY 0xf1

#define IRQ_EXTERNAL 3

typedef struct
{
    Arm7 cpu;
    uint8_t *rom;
    uint8_t internalRam[INTERNAL_RAM_SIZE];
    uint8_t externalRam[EXTERNAL_RAM_SIZE];
    uint8_t sharedRam[SHARED_RAM_SIZE];

    uint8_t command; // the PC's command byte
    int answered;    // the board wrote its answer
    int externalLine;
    uint8_t irqEnable, irqPending;
    uint64_t timerPeriod[2], timerNext[2];
    uint8_t latch[3], outPort;

    // The cabinet's controls, as the board reads them: per seat, 16
    // switches (a bit each, 1: pressed) and 3 potentiometers (10 bits).
    uint16_t switches[2];
    uint16_t analog[2][3];

    uint64_t wallStart; // the wall clock at the board's start (ns)
} A27;

static A27 *board;
static const char *debugAnalog;
static int trace;

static uint64_t nowNs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void updateIrq(A27 *b)
{
    b->cpu.irq = b->irqPending != 0xff;
}

static void raiseIrq(A27 *b, int line)
{
    if (!(b->irqEnable & (1u << line)))
    {
        b->irqPending &= (uint8_t) ~(1u << line);
        updateIrq(b);
    }
}

static void unknownAccess(uint32_t address, const char *what, uint32_t value)
{
    if (trace > 1)
        printf("IGS A27: %s %08x (%08x)\n", what, address, value);
    static uint32_t seen[64];
    static int count;

    for (int i = 0; i < count; i++)
        if (seen[i] == address)
            return;
    if (count < 64)
        seen[count++] = address;
    log_warn("IGS A27: %s %08x (%08x) unknown", what, address, value);
}

// The controls' bus: 0x28200000 selects a seat's switches ('v', 0x76) or
// its potentiometers' converter ('s', 0x73), the seat's nibbles swapped for
// the second (0x67, 0x37); 0x28300000's bits 3-4 pick the potentiometer.
// 0x28000000 and 0x28100000 read the selected 16 bits, high and low byte,
// inverted. A potentiometer reads as the PC decodes it: its low 8 bits
// first, then its top 2.
static uint8_t inputRead(A27 *b, int low)
{
    uint8_t select = b->latch[2] & 0x77;
    int seat;
    uint16_t value;

    switch (select)
    {
    case 0x76:
    case 0x73:
        seat = 0;
        break;
    case 0x67:
    case 0x37:
        seat = 1;
        break;
    default:
        return 0xff;
    }
    if (select == 0x76 || select == 0x67)
        value = b->switches[seat];
    else
    {
        int channel = (b->latch[0] >> 3) & 3;
        uint16_t v = channel < 3 ? b->analog[seat][channel] & 0x3ff : 0;
        value = (uint16_t)((v & 0xff) << 8 | v >> 8);
    }
    return (uint8_t)~(low ? value : value >> 8);
}

static uint32_t boardRead(Arm7 *cpu, uint32_t address, int size)
{
    A27 *b = cpu->board;

    (void)size;
    if (trace > 3)
        printf("IGS A27: read %08x at %08x\n", address, cpu->r[15]);
    switch (address)
    {
    case 0x38000000:
        if (trace > 2)
            printf("IGS A27: command read at %08x\n", cpu->r[15]);
        return b->command;
    case 0x58000000:
        if (trace > 2)
            printf("IGS A27: command acknowledged at %08x\n", cpu->r[15]);
        b->externalLine = 0;
        return 0xff;
    case 0x70000200:
    {
        uint8_t pending = b->irqPending;
        if (trace > 1 && pending != 0xff)
            printf("IGS A27: interrupts %02x (cycle %llu)\n", pending, (unsigned long long)cpu->cycles);
        b->irqPending = 0xff;
        updateIrq(b);
        return pending;
    }
    case 0x4000000c:
        return 0xff800007 | (0xfffff << 3);
    case 0x28000000:
    case 0x28100000:
        return inputRead(b, address == 0x28100000);
    case 0x28300000:
        return b->latch[0];
    case 0x28400000:
        return b->latch[1];
    case 0x28200000:
        return b->latch[2];
    }
    unknownAccess(address, "read", 0);
    return 0;
}

static void boardWrite(Arm7 *cpu, uint32_t address, uint32_t value, int size)
{
    A27 *b = cpu->board;

    (void)size;
    switch (address)
    {
    case 0x38000000:
        if (trace)
            printf("IGS A27: answer %02x\n", value & 0xff);
        if ((value & 0xff) == ANSWER_READY)
            b->answered = 1;
        return;
    case 0x70000100:
    case 0x70000104:
    {
        int n = (address >> 2) & 1;
        value &= 0xff;
        b->timerPeriod[n] = value ? (uint64_t)A27_TIMER_DIVISOR * (value + 1) : 0;
        b->timerNext[n] = cpu->cycles + b->timerPeriod[n];
        return;
    }
    case 0x70000200:
        if (trace > 2 && b->irqEnable != (uint8_t)value)
            printf("IGS A27: interrupt mask %02x (line %d)\n", value & 0xff, b->externalLine);
        b->irqEnable = (uint8_t)value;
        // The PC's command stays raised until acknowledged: masked while it
        // came, it is pending once unmasked.
        if (b->externalLine)
            raiseIrq(b, IRQ_EXTERNAL);
        return;
    case 0x40000018:
        if (getenv("IGS_A27_OUTPUTS") && b->outPort != (value & 0x1f))
            printf("IGS A27: %.2f s out port %02x\n", (nowNs() - b->wallStart) / 1e9, value & 0x1f);
        b->outPort = value & 0x1f;
        return;
    case 0x40000014:
    case 0xf0000008:
        return;
    case 0x28300000:
    case 0x28400000:
    case 0x28200000:
    {
        int n = address == 0x28300000 ? 0 : address == 0x28400000 ? 1 : 2;
        if (getenv("IGS_A27_OUTPUTS") && b->latch[n] != (uint8_t)value)
            printf("IGS A27: %.2f s latch %08x %02x\n", (nowNs() - b->wallStart) / 1e9, address, value & 0xff);
        b->latch[n] = (uint8_t)value;
        return;
    }
    }
    // The internal ROM: the firmware's first request copy goes there,
    // before it has set its buffers up.
    if (address < 0x4000)
        return;
    unknownAccess(address, "write", value);
}

// Runs the board to cycle end, its timers firing on the way; stops early
// once it has answered when untilAnswer.
static void runBoard(A27 *b, uint64_t end, int untilAnswer)
{
    Arm7 *cpu = &b->cpu;

    while (cpu->cycles < end && !cpu->halted && !(untilAnswer && b->answered))
    {
        uint64_t stop = end;

        for (int n = 0; n < 2; n++)
            if (b->timerPeriod[n] && b->timerNext[n] < stop)
                stop = b->timerNext[n];
        if (untilAnswer && stop > cpu->cycles + 1000)
            stop = cpu->cycles + 1000; // checks the answer now and then
        arm7Run(cpu, stop);
        for (int n = 0; n < 2; n++)
            if (b->timerPeriod[n] && cpu->cycles >= b->timerNext[n])
            {
                b->timerNext[n] += b->timerPeriod[n];
                raiseIrq(b, n);
            }
    }
}

static uint64_t wallCycles(A27 *b)
{
    // (no 64-bit division in the loader: libgcc is not linked)
    return (uint64_t)((double)(nowNs() - b->wallStart) * (A27_CLOCK / 1e9));
}

static void catchUp(A27 *b)
{
    uint64_t wall = wallCycles(b);

    if (wall > b->cpu.cycles + A27_CATCH_UP)
        b->cpu.cycles = wall - A27_CATCH_UP; // a long stall is not made up
    if (wall > b->cpu.cycles)
        runBoard(b, wall, 0);
}

static int a27Start(const char *romPath)
{
    FILE *f = fopen(romPath, "rb");
    A27 *b;
    uint32_t table;

    if (!f)
    {
        log_error("IGS A27: cannot open the board's ROM %s", romPath);
        return -1;
    }
    b = calloc(1, sizeof(*b));
    b->rom = calloc(1, ROM_SIZE);
    if (fread(b->rom, 1, ROM_SIZE, f) < 0x8000)
    {
        log_error("IGS A27: %s is too short", romPath);
        fclose(f);
        return -1;
    }
    fclose(f);

    b->cpu.board = b;
    b->cpu.read = boardRead;
    b->cpu.write = boardWrite;
    arm7Map(&b->cpu, 0x08000000, ROM_SIZE, b->rom, 1);
    arm7Map(&b->cpu, 0x10000000, INTERNAL_RAM_SIZE, b->internalRam, 0);
    arm7Map(&b->cpu, 0x18000000, EXTERNAL_RAM_SIZE, b->externalRam, 0);
    arm7Map(&b->cpu, 0x48000000, SHARED_RAM_SIZE, b->sharedRam, 0);
    b->irqEnable = 0xff;
    b->irqPending = 0xff;
    for (int seat = 0; seat < 2; seat++)
        b->analog[seat][0] = 0x200;
    debugAnalog = getenv("IGS_A27_ANALOG");
    // IGS_A27_ANALOG=seat:channel:value[,...] (debugging; hexadecimal): in
    // place of the controls'
    for (const char *list = debugAnalog; list && *list;)
    {
        int seat, channel;
        unsigned int value;
        if (sscanf(list, "%d:%d:%x", &seat, &channel, &value) == 3 && seat >= 0 && seat < 2 && channel >= 0 &&
            channel < 3)
            b->analog[seat][channel] = (uint16_t)value;
        list = strchr(list, ',');
        list = list ? list + 1 : NULL;
    }

    // The internal ROM's part: the stacks (the external RAM's end, past what
    // the firmware uses), IRQs on, the external ROM's main in system mode.
    memcpy(&table, b->rom + 0xc, 4);
    if ((table >> 24) != 0x08)
    {
        log_error("IGS A27: %s is not an IGS027A external ROM", romPath);
        return -1;
    }
    {
        uint32_t entry;
        memcpy(&entry, b->rom + (table - 0x08000000) + 8, 4);
        arm7Reset(&b->cpu, entry, 0x1f | 0x40); // system mode, FIQ off
    }
    arm7SetStack(&b->cpu, 0x1f, 0x18100000);
    arm7SetStack(&b->cpu, 0x12, 0x180f0000);
    arm7SetStack(&b->cpu, 0x13, 0x180e0000);
    arm7SetStack(&b->cpu, 0x11, 0x180d0000);
    b->cpu.irqVector = 0x0800002c;
    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    if (getenv("IGS_A27_BREAK"))
    {
        // IGS_A27_BREAK=address[,address...] (up to 8, hexadecimal)
        const char *list = getenv("IGS_A27_BREAK");
        for (int i = 0; i < 8 && *list; i++)
        {
            char *end;
            b->cpu.breakpoints[i] = (uint32_t)strtoul(list, &end, 16);
            list = *end == ',' ? end + 1 : end;
        }
    }
    // The board was on long before the game: its start-up done (it clears
    // any command pending), it waits for the game's first.
    runBoard(b, A27_BOOT, 0);
    b->wallStart = nowNs() - (uint64_t)(A27_BOOT * (1e9 / A27_CLOCK));
    board = b;
    return 0;
}

// --- /dev/pccard0 ---

static int pccardFd = -1;
static uint8_t position; // lseek's: the next write's command byte
static int (*realOpen)(const char *, int, ...);
static int (*realClose)(int);
static off_t (*realLseek)(int, off_t, int);
static ssize_t (*realRead)(int, void *, size_t);
static ssize_t (*realWrite)(int, const void *, size_t);
static char romPath[PATH_MAX];
static uint8_t request[REQUEST_SIZE]; // the last one (tracing)

__attribute__((force_align_arg_pointer)) static int pccardOpen(const char *path, int flags, ...)
{
    va_list ap;
    int mode;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    if (path && !strcmp(path, "/dev/pccard0"))
    {
        if (!board && a27Start(romPath) != 0)
            return -1;
        if (pccardFd < 0)
            pccardFd = realOpen("/dev/null", O_RDWR);
        return pccardFd;
    }
    return realOpen(path, flags, mode);
}

__attribute__((force_align_arg_pointer)) static int pccardClose(int fd)
{
    if (fd >= 0 && fd == pccardFd)
        pccardFd = -1;
    return realClose(fd);
}

__attribute__((force_align_arg_pointer)) static off_t pccardLseek(int fd, off_t offset, int whence)
{
    if (fd >= 0 && fd == pccardFd)
    {
        // The game's sequence number goes on past 0xff (it skips 0xfe, its
        // start command): the board takes 0xf0 to 0xff as its own commands
        // (0xf0 to 0xf4, 0xff stop it), the driver kept the others below.
        position = offset == 0xfe ? 0xfe : (uint8_t)(offset % 0xf0);
        return offset;
    }
    return realLseek(fd, offset, whence);
}

// The cabinet's controls (its KEY TEST and driving calibration), per seat:
// the steering wheel, the gas and brake pedals (potentiometers 0, 2, 1),
// START, VIEW, TURBO and the gear shift. Seat 1's switches also carry the
// cabinet's: test, both seats' coins and service buttons.
#define SW_COIN1 0x0001
#define SW_TEST 0x0002
#define SW_SERVICE1 0x0004
#define SW_START 0x0100
#define SW_VIEW 0x0200
#define SW_TURBO 0x0400
#define SW_GEAR_DOWN 0x0800
#define SW_GEAR_UP 0x1000
#define SW_SERVICE2 0x2000
#define SW_COIN2 0x4000
// The pots' ends, past the calibration the cabinet's settings keep (wheel
// left 1023, centre 575, right 167; gas 1023 to 556, brake 956 to 524): the
// game clamps them.
#define WHEEL_LEFT 1023
#define WHEEL_CENTRE 575
#define WHEEL_RIGHT 150
#define PEDAL_RELEASED 1023
#define PEDAL_PRESSED 500
#define STICK_DEADZONE 0.12
// A coin: its switch held, then released, for that long of the board's
// time (the board reads its switches 20 times a second and takes a coin held
// for 2 to 13 of them; its time runs ahead of the wall clock when the game
// asks faster than its timer).
#define COIN_PULSE (A27_CLOCK * 3ull / 10)

static JVSIO *inputState(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
}

// IGS_A27_COINS=seconds:seat[,...] (debugging): a coin at that time.
static int debugCoins(A27 *b, int seat)
{
    double now = (nowNs() - b->wallStart) / 1e9;
    int count = 0;

    for (const char *list = getenv("IGS_A27_COINS"); list && *list;)
    {
        double at;
        int s;
        if (sscanf(list, "%lf:%d", &at, &s) == 2 && s == seat && now >= at)
            count++;
        list = strchr(list, ',');
        list = list ? list + 1 : NULL;
    }
    return count;
}

static double clampUnit(double v)
{
    return v > 1 ? 1 : v < -1 ? -1 : v;
}

// The keyboard and pads (desktopInput.h: 1 / 2 start, 5 / 6 coins, F1
// service, F2 test, the arrows steer, Up is the gas and Down the brake,
// space TURBO, z VIEW, c / v gear down / up; a pad: left stick, R2 gas, L2
// brake, A TURBO, Y VIEW, L1 / R1 gear down / up, Start, Back coin), or
// evdev's JVS state (ANALOGUE_1..3 seat 1's wheel, gas, brake, 4..6 seat
// 2's; BUTTON_1 TURBO, 2 VIEW, 4 / 5 gear down / up).
static void controls(A27 *b)
{
    static int seenCoins[2] = {-1, -1}, pendingCoins[2];
    static uint64_t coinUntil[2];
    JVSIO *io = inputState();
    int evdev = getConfig()->inputMode == 2 && io->analogueMax > 0;

    b->switches[0] = b->switches[1] = 0;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        b->switches[0] |= SW_TEST;
    for (int seat = 0; seat < 2; seat++)
    {
        unsigned int in = io->state.inputSwitch[PLAYER_1 + seat];
        double wheel, gas, brake;
        uint16_t sw = 0;

        if (in & BUTTON_START || desktopPlayerGamepadButton(seat, SDL_GAMEPAD_BUTTON_START))
            sw |= SW_START;
        if (in & BUTTON_2 || desktopPlayerGamepadButton(seat, SDL_GAMEPAD_BUTTON_NORTH))
            sw |= SW_VIEW;
        if (in & BUTTON_1)
            sw |= SW_TURBO;
        if (in & BUTTON_4 || desktopPlayerGamepadButton(seat, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
            sw |= SW_GEAR_DOWN;
        if (in & BUTTON_5 || desktopPlayerGamepadButton(seat, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
            sw |= SW_GEAR_UP;
        if (in & BUTTON_SERVICE)
            b->switches[0] |= seat ? SW_SERVICE2 : SW_SERVICE1;
        b->switches[seat] |= sw;

        wheel = desktopPlayerGamepadAxis(seat, SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;
        if (wheel > -STICK_DEADZONE && wheel < STICK_DEADZONE)
            wheel = 0;
        gas = desktopPlayerGamepadAxis(seat, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0;
        brake = desktopPlayerGamepadAxis(seat, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0;
        if (evdev)
        {
            wheel += io->state.analogueChannel[ANALOGUE_1 + seat * 3] * 2.0 / io->analogueMax - 1;
            gas += io->state.analogueChannel[ANALOGUE_2 + seat * 3] / (double)io->analogueMax;
            brake += io->state.analogueChannel[ANALOGUE_3 + seat * 3] / (double)io->analogueMax;
        }
        wheel = clampUnit(wheel + !!(in & BUTTON_RIGHT) - !!(in & BUTTON_LEFT));
        gas = clampUnit(gas + !!(in & BUTTON_UP));
        brake = clampUnit(brake + !!(in & BUTTON_DOWN));
        if (gas < 0)
            gas = 0;
        if (brake < 0)
            brake = 0;
        if (!debugAnalog)
        {
            b->analog[seat][0] = (uint16_t)(wheel < 0 ? WHEEL_CENTRE - wheel * (WHEEL_LEFT - WHEEL_CENTRE)
                                                      : WHEEL_CENTRE - wheel * (WHEEL_CENTRE - WHEEL_RIGHT));
            b->analog[seat][1] = (uint16_t)(PEDAL_RELEASED - brake * (PEDAL_RELEASED - PEDAL_PRESSED));
            b->analog[seat][2] = (uint16_t)(PEDAL_RELEASED - gas * (PEDAL_RELEASED - PEDAL_PRESSED));
        }

        // Coins: each inserted one pulses the seat's coin switch.
        {
            int count = io->state.coinCount[seat] + debugCoins(b, seat);
            if (seenCoins[seat] >= 0 && count > seenCoins[seat])
                pendingCoins[seat] += count - seenCoins[seat];
            seenCoins[seat] = count;
            if (b->cpu.cycles >= coinUntil[seat] + COIN_PULSE && pendingCoins[seat] > 0)
            {
                pendingCoins[seat]--;
                coinUntil[seat] = b->cpu.cycles + COIN_PULSE;
            }
            if (b->cpu.cycles < coinUntil[seat])
                b->switches[0] |= seat ? SW_COIN2 : SW_COIN1;
        }
    }
}

// The wheels' motors, as the game asks the board for them: request byte
// 0x10 (seat 1), 0x12 (seat 2): bit 7 on, bit 6 the direction (set: to the
// left, as the MOTOR TEST's +31), bits 1-5 the strength (0-31). Seat 1's
// is the loader's force feedback device's constant force (a pad rumbles as
// hard).
#define MOTOR_SEAT1 0x10

static int ffbOpen = -1;

static void motor(const uint8_t *request, size_t count)
{
    static int last = -1;
    int value, strength;

    if (ffbOpen < 0)
        ffbOpen = evdevFfbOpen("IGS A27");
    if (!ffbOpen || count <= MOTOR_SEAT1)
        return;
    value = request[MOTOR_SEAT1];
    if (value == last)
        return;
    last = value;
    strength = (value & 0x80) ? (value >> 1) & 0x1f : 0;
    evdevFfbConstant((value & 0x40 ? 1.f : -1.f) * strength / 31.f);
}

// IGS_A27_SWITCHES=seconds:seat:mask[,...] (debugging): from that many
// seconds after the board's start, the seat's switches are mask (hex).
static void debugSwitches(A27 *b)
{
    const char *list = getenv("IGS_A27_SWITCHES");
    double now = (nowNs() - b->wallStart) / 1e9;
    int masks[2] = {-1, -1};

    while (list && *list)
    {
        char *end;
        double at = strtod(list, &end);
        int seat;
        unsigned int mask;

        if (sscanf(end, ":%d:%x", &seat, &mask) != 2 || seat < 0 || seat > 1)
            break;
        if (now >= at)
            masks[seat] = (int)mask;
        list = strchr(end, ',');
        list = list ? list + 1 : NULL;
    }
    for (int seat = 0; seat < 2; seat++)
        if (masks[seat] >= 0)
        {
            static int shown[2] = {-1, -1};
            if (shown[seat] != masks[seat])
                printf("IGS A27: %.1f s: seat %d switches %04x\n", now, seat + 1, masks[seat]);
            shown[seat] = masks[seat];
            b->switches[seat] = (uint16_t)masks[seat];
        }
}

__attribute__((force_align_arg_pointer)) static ssize_t pccardWrite(int fd, const void *buf, size_t count)
{
    A27 *b = board;

    if (fd < 0 || fd != pccardFd)
        return realWrite(fd, buf, count);
    catchUp(b);
    controls(b);
    debugSwitches(b);
    memcpy(b->sharedRam, buf, count < REQUEST_SIZE ? count : REQUEST_SIZE);
    motor(buf, count);
    if (getenv("IGS_A27_REQDIFF"))
    {
        const uint8_t *now = buf;
        for (size_t i = 0; i < REQUEST_SIZE && i < count; i++)
            if (now[i] != request[i] && i != 0x18 && i != 0x19)
                printf("IGS A27: %.2f s request[%03zx] %02x -> %02x\n", (nowNs() - b->wallStart) / 1e9, i, request[i],
                       now[i]);
    }
    memcpy(request, buf, count < REQUEST_SIZE ? count : REQUEST_SIZE);
    b->command = position;
    if (trace)
        printf("IGS A27: command %02x e/f %08x 308b %02x pc %08x\n", b->command, arm7Read32(&b->cpu, 0x180030b0),
               arm7Read32(&b->cpu, 0x18003088) >> 24, b->cpu.r[15]);
    b->answered = 0;
    b->externalLine = 1;
    raiseIrq(b, IRQ_EXTERNAL);
    return 1;
}

__attribute__((force_align_arg_pointer)) static ssize_t pccardRead(int fd, void *buf, size_t count)
{
    A27 *b = board;
    uint32_t place[2], offset;

    (void)count;
    if (fd < 0 || fd != pccardFd)
        return realRead(fd, buf, count);
    if (!b->answered)
        runBoard(b, b->cpu.cycles + A27_ANSWER_LIMIT, 1);
    if (!b->answered)
    {
        log_error("IGS A27: the board does not answer (pc %08x)", b->cpu.r[15]);
        if (trace > 2)
        {
            for (int i = 0; i < 0x40; i += 4)
                printf("%08x%c", arm7Read32(&b->cpu, 0x180030a4 + i), i % 32 == 28 ? '\n' : ' ');
            printf("3088: %08x %08x\n", arm7Read32(&b->cpu, 0x18003088), arm7Read32(&b->cpu, 0x1800308c));
        }
        return 1000; // the game's own timeout
    }
    memcpy(place, b->sharedRam, sizeof(place));
    offset = 8 + place[0] * 1024 + place[1];
    if (offset + REPLY_SIZE > SHARED_RAM_SIZE)
    {
        log_error("IGS A27: reply out of the shared RAM (%x)", offset);
        offset = 8;
    }
    memcpy(buf, b->sharedRam + offset, REPLY_SIZE);
    // The board goes on with its step (it answers first): a frame passes
    // before the game's next command, which the board drops unless that
    // step is done.
    runBoard(b, b->cpu.cycles + A27_STEP, 0);
    if (trace == 2)
    {
        printf("IGS A27: %02x pc %08x copy %08x request state %02x %02x, reply", b->command, b->cpu.r[15],
               arm7Read32(&b->cpu, 0x180049a0), request[0x18], request[0x19]);
        for (int i = 0; i < 0x28; i++)
            printf(" %02x", ((uint8_t *)buf)[i]);
        printf("\n");
    }
    return 1;
}

void igsA27Install(const IgsGame *game, const char *gameDir)
{
    // With evdev input the pads are evdev's (read twice otherwise).
    if (getConfig()->inputMode != 2)
    {
        desktopGamepadEnableTwoPlayers();
        desktopKeyboardEnableTwoPlayers();
    }
    snprintf(romPath, sizeof(romPath), "%s/%s", gameDir, game->a27Rom);
    *(void **)&realOpen = hookExecutableImport("open", (void *)pccardOpen);
    *(void **)&realClose = hookExecutableImport("close", (void *)pccardClose);
    *(void **)&realLseek = hookExecutableImport("lseek", (void *)pccardLseek);
    *(void **)&realRead = hookExecutableImport("read", (void *)pccardRead);
    *(void **)&realWrite = hookExecutableImport("write", (void *)pccardWrite);
    if (!realOpen || !realClose || !realLseek || !realRead || !realWrite)
        log_error("IGS A27: the game does not import its device's calls");
}
