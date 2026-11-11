// Speed Driver 2 M207CNC: original decrypted EROM on an IGS036, connected
// through a PLX9030. The missing mask ROM is represented by its service
// interface, not an authentic ROM dump. Firmware owns game flow and physics.
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL_gamepad.h>

#include "igs.h"
#include "igsArm7.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../input/evdevFfb.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

#define EXT_SIZE 0x1000000
#define SHARED_SIZE 0x20000

typedef struct
{
    Arm7 cpu;
    uint8_t *rom, *ext, *sram, *shared;
    uint8_t commandOut[0x1000], command[0x1000], gpio[0x1000], control[0x1000];
    uint32_t enabled, pending, handlers[32], random;
    uint64_t timerPeriod[2], timerNext[2];
    int active, inflight;
} Sd2Board;

static Sd2Board *board;
static int trace;
static int ffbOpen = -1, ffbTrace;

__attribute__((destructor)) static void stopSd2Motor(void)
{
    if (ffbOpen > 0)
        evdevFfbConstant(0);
}

// The I/O board motor byte has enable in bit 7, direction in bit 6, and
// a six-bit magnitude with its bits reversed on the wire. The firmware
// already mixes the steering and road forces before building this packet.
static void motorPacket(Arm7 *cpu)
{
    if (arm7Read32(cpu, 0x20006b60) != 1 || arm7Read32(cpu, 0x20006b64) != 9)
        return;
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += arm7Read32(cpu, 0x20006b68 + i * 4);
    if (sum != arm7Read32(cpu, 0x20006b84))
        return;
    uint8_t raw = arm7Read32(cpu, 0x20006b68);
    unsigned strength = 0;
    for (int i = 0; i < 6; i++)
        strength |= ((raw >> i) & 1) << (5 - i);
    float force = (raw & 0x80) ? ((raw & 0x40) ? 1.f : -1.f) * strength / 63.f : 0;
    static int last = -1;
    if (ffbTrace && raw != last)
    {
        printf("IGS SD2 FFB: motor %02x magnitude %u force %.3f\n", raw, strength, force);
        fflush(stdout);
        last = raw;
    }
    if (ffbOpen < 0)
        ffbOpen = evdevFfbOpen("IGS SD2");
    if (ffbOpen > 0)
        evdevFfbConstant(force);
}

static uint64_t sd2Now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

static uint32_t memoryRead(Arm7 *cpu, uint32_t address, int size)
{
    (void)cpu;
    (void)size;
    if (address == 0xfffff214 || address == 0xfffa4014 || address == 0xfffa8014)
        return 0x202;
    return 0;
}

static void memoryWrite(Arm7 *cpu, uint32_t address, uint32_t value, int size)
{
    (void)size;
    // USART0 TX enable follows construction of the complete eight-byte
    // output packet. Read the queued frame, before emulating TX completion.
    if (address == 0xfffa4000 && (value & 0x40))
        motorPacket(cpu);
    if (trace >= 3)
        printf("IGS SD2: MMIO %08x = %08x\n", address, value);
}

static void putWord(uint32_t address, uint32_t value)
{
    Arm7 *cpu = &board->cpu;
    uint8_t *p = cpu->page[address >> ARM7_PAGE_SHIFT];
    if (!p || (address & 3))
    {
        log_error("IGS SD2: invalid service write %08x", address);
        cpu->halted = 1;
        return;
    }
    memcpy(p + (address & cpu->pageMask[address >> ARM7_PAGE_SHIFT]), &value, 4);
}

// Service addresses come from the EROM's veneers at 0x1003050c..0x100305fc.
// Kitsune's Sd2EromBoard and TeknoRaven's SD2 IROM compatibility layer supply
// the reference ABI. Unimplemented service entry points stop the CPU.
static void maskService(Arm7 *cpu, uint32_t address)
{
    Sd2Board *b = cpu->board;
    uint32_t a = cpu->r[0], v = cpu->r[1], n = cpu->r[2];
    if (trace >= 3)
        printf("IGS SD2: service %04x (%08x %08x %08x) lr=%08x\n", address, a, v, n, cpu->r[14]);
    switch (address)
    {
    case 0xbc: case 0x144: // interrupt acknowledge
        b->pending &= ~(1u << (a & 31));
        b->active = -1;
        cpu->irq = 0;
        break;
    case 0xc4: // interrupt enable (and unmask the CPU IRQ)
        b->enabled |= 1u << (a & 31);
        cpu->cpsr &= ~0x80u;
        putWord(0x02000000, b->enabled);
        break;
    case 0x15c: // register interrupt handler (r2)
        if (a < 32) b->handlers[a] = n;
        else cpu->halted = 1;
        break;
    case 0x200: cpu->r[0] = !b->inflight; break; // PC mailbox idle
    case 0x20c: // read the PC command latch into a two-word buffer
        putWord(a, b->command[6]);
        putWord(a + 4, 0);
        cpu->r[0] = b->command[6];
        break;
    case 0x264: // watchdog kick
        cpu->r[0] = 0;
        break;
    case 0x280: // output command byte, port 0..3 in reverse order
        if (a < 4)
        {
            b->commandOut[(3 - a) * 2] = (uint8_t)v;
            if (a == 0)
            {
                b->shared[0x3000] = (uint8_t)v;
                b->inflight = 0;
            }
        }
        cpu->r[0] = 0;
        break;
    case 0x4b0: cpu->r[0] = arm7Read32(cpu, 0x0200002c); break;
    case 0x500: putWord(0x0200002c, v); cpu->r[0] = 0; break;
    case 0xbd0: cpu->r[0] = v ? a % v : 0; break;
    case 0xd70:
        b->random ^= b->random << 13;
        b->random ^= b->random >> 17;
        b->random ^= b->random << 5;
        cpu->r[0] = b->random;
        break;
    case 0x134c: // timer r0=channel, r1=period, r2=clock selection
    {
        static const unsigned divisors[] = {2, 8, 32, 128, 1024};
        if (a >= 2 || n >= 5 || !v)
        {
            log_error("IGS SD2: invalid timer %u/%u/%u", a, v, n);
            cpu->halted = 1;
            break;
        }
        b->timerPeriod[a] = (uint64_t)((double)v * divisors[n] * 1e9 / 65000000.0);
        b->timerNext[a] = sd2Now() + b->timerPeriod[a];
        cpu->r[0] = 0;
        break;
    }
    case 0x14e0: cpu->r[0] = n; break;
    case 0x150c: cpu->r[0] = 0xcf; break;
    case 0x1514: case 0x151c: cpu->r[0] = 1; break;
    case 0x1524: cpu->r[0] = 200; break;
    case 0x1f4: case 0x228: case 0x244: case 0xb10: case 0xd44:
    case 0xde4: case 0x12d0: case 0x12e4: case 0x1318: case 0x13ec:
    case 0x1410: case 0x142c: case 0x1448: case 0x152c:
        cpu->r[0] = 0;
        break;
    default:
        log_error("IGS SD2: unsupported mask-ROM service %08x from %08x", address, cpu->r[14]);
        cpu->halted = 1;
        return;
    }
    cpu->r[15] = cpu->r[14] & ~1u;
    cpu->cpsr = (cpu->cpsr & ~0x20u) | ((cpu->r[14] & 1) ? 0x20 : 0);
}

static double unit(double value)
{
    return value < 0 ? 0 : value > 1 ? 1 : value;
}

// The I/O board's incoming serial packet: active-low switches (LE), then
// steering, brake and throttle (BE), and the byte-sum checksum. Feed the
// firmware's USART receive buffer; its debounce/calibration remains active.
static void sampleInputs(void)
{
    static int lastCoins = -1;
    static uint64_t coinUntil;
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopKeyboardState();
    unsigned player = io->state.inputSwitch[PLAYER_1];
    uint64_t switches = 0;
    double turn = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;
    double gas = desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0;
    double brake = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0;
    uint64_t now = sd2Now();
    if (getConfig()->inputMode == 2 && io->analogueMax > 0)
    {
        turn = io->state.analogueChannel[ANALOGUE_1] * 2.0 / io->analogueMax - 1;
        gas = io->state.analogueChannel[ANALOGUE_2] / (double)io->analogueMax;
        brake = io->state.analogueChannel[ANALOGUE_3] / (double)io->analogueMax;
    }
    if (player & BUTTON_LEFT) turn = -1;
    if (player & BUTTON_RIGHT) turn = 1;
    if (player & BUTTON_UP) gas = 1;
    if (player & BUTTON_DOWN) brake = 1;
    if (lastCoins >= 0 && io->state.coinCount[0] != lastCoins)
        coinUntil = now + 250000000;
    lastCoins = io->state.coinCount[0];
    // SD2's host switch words use the cabinet's io.inf indices. The
    // firmware combines these with the serial ADCs and owns all key edges.
    if (now < coinUntil) switches |= 1ull << 5;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST) switches |= 1ull << 37;
    if (player & BUTTON_SERVICE) switches |= 1ull << 36;
    if (player & BUTTON_START) switches |= 1ull << 52;
    if (player & BUTTON_2) switches |= 1ull << 49;
    if (player & BUTTON_1) switches |= 1ull << 53;
    if (player & BUTTON_5) switches |= 1ull << 11;
    if (player & BUTTON_4) switches |= 1ull << 12;
    // Firmware keymap 0x1003235c: logical wheel-left/right (17/20)
    // use io.inf A/F (58/22), independently of the analogue steering ADC.
    if (player & BUTTON_LEFT) switches |= 1ull << 58;
    if (player & BUTTON_RIGHT) switches |= 1ull << 22;
    if (trace)
    {
        static uint64_t last;
        if (switches != last)
            printf("IGS SD2: switches %016llx\n", (unsigned long long)switches);
        last = switches;
    }
    memcpy(board->shared + 8, &switches, sizeof(switches));
    uint16_t adc[] = {(uint16_t)(unit((turn + 1) / 2) * 1024),
                      (uint16_t)(4 + unit(brake) * 1020), (uint16_t)(4 + unit(gas) * 1020)};
    uint8_t packet[9] = {0xff, 0xff};
    for (int i = 0; i < 3; i++)
    {
        packet[2 + i * 2] = (uint8_t)(adc[i] >> 8);
        packet[3 + i * 2] = (uint8_t)adc[i];
    }
    for (int i = 0; i < 8; i++) packet[8] += packet[i];
    putWord(0x20006d68, 1);
    putWord(0x20006d6c, 9);
    putWord(0x20006b5c, 1);
    for (int i = 0; i < 9; i++) putWord(0x20006d70 + i * 4, packet[i]);
}

// Run synchronously at the PLX boundary: no unsynchronised host/ARM writes
// to the shared window. Timers follow monotonic time, independently of PC
// frame rate. The game waits for the previous sequence before using it.
static void boardStep(unsigned instructions)
{
    Sd2Board *b = board;
    uint64_t now = sd2Now(), end = b->cpu.cycles + instructions;
    for (int i = 0; i < 2; i++)
        if (b->timerPeriod[i] && now >= b->timerNext[i])
        {
            b->pending |= 1u << (6 + i);
            b->timerNext[i] = now + b->timerPeriod[i];
        }
    while (b->cpu.cycles < end && !b->cpu.halted)
    {
        if (b->active < 0 && (b->cpu.cpsr & 0x1f) != 0x12)
        {
            uint32_t pending = b->enabled & b->pending;
            b->cpu.irq = 0;
            for (int i = 0; i < 32; i++)
                if ((pending & (1u << i)) && b->handlers[i])
                {
                    b->active = i;
                    b->cpu.irqVector = b->handlers[i] & ~1u;
                    b->cpu.irq = 1;
                    break;
                }
        }
        arm7Run(&b->cpu, b->cpu.cycles + 128);
        if (b->active >= 0 && (b->cpu.cpsr & 0x1f) == 0x12)
            b->cpu.irq = 0;
    }
}

__attribute__((force_align_arg_pointer)) static int plxFind(unsigned char *device)
{
    device[2] = 0x30; device[3] = 0x90;
    device[4] = 0xb5; device[5] = 0x10;
    return 1;
}

__attribute__((force_align_arg_pointer)) static int plxOpen(void *device, int *handle)
{
    (void)device;
    *handle = open("/dev/null", O_RDWR);
    return 0x200;
}

__attribute__((force_align_arg_pointer)) static int plxMap(int handle, unsigned char bar, void **address)
{
    (void)handle;
    *address = bar == 2 ? board->shared : board->command;
    return 0x200;
}

__attribute__((force_align_arg_pointer)) static unsigned plxRead(int handle, unsigned offset, unsigned *status)
{
    (void)handle; (void)offset;
    if (status) *status = 0x200;
    return 0;
}

__attribute__((force_align_arg_pointer)) static void doorbell(void)
{
    sampleInputs();
    board->inflight = 1;
    board->pending |= 1u << 3;
    boardStep(100000);
}

__attribute__((force_align_arg_pointer)) static unsigned commandRead(unsigned char port)
{
    boardStep(10000);
    if (trace >= 2)
    {
        static uint64_t next;
        uint64_t now = sd2Now();
        if (now >= next)
        {
            next = now + 1000000000;
            printf("IGS SD2: pc %08x cpsr %08x enabled %08x pending %08x active %d ticks %u/%u seq %02x shared %02x PC %02x latch %02x\n",
                   board->cpu.r[15], board->cpu.cpsr, board->enabled, board->pending, board->active,
                   arm7Read32(&board->cpu, 0x20000150), arm7Read32(&board->cpu, 0x20000154),
                   board->commandOut[6], board->shared[0x3000], *(unsigned char *)0x08c20844, board->command[6]);
        }
    }
    return port < 8 ? board->commandOut[port < 4 ? (3 - port) * 2 : (7 - port) * 2 + 8] : 0;
}

static void boardStart(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        log_error("IGS SD2: cannot open EROM %s", path);
        exit(EXIT_FAILURE);
    }
    board = calloc(1, sizeof(*board));
    if (!board)
    {
        log_error("IGS SD2: cannot allocate board state");
        exit(EXIT_FAILURE);
    }
    Sd2Board *b = board;
    b->rom = calloc(1, 0x800000);
    b->ext = calloc(1, EXT_SIZE);
    b->sram = calloc(1, 0x10000);
    b->shared = calloc(1, SHARED_SIZE);
    if (!b->rom || !b->ext || !b->sram || !b->shared)
    {
        log_error("IGS SD2: cannot allocate board memory");
        exit(EXIT_FAILURE);
    }
    if (fread(b->rom, 1, 0x80000, f) != 0x80000 || fgetc(f) != EOF)
    {
        log_error("IGS SD2: EROM must be exactly 512 KiB");
        exit(EXIT_FAILURE);
    }
    fclose(f);
    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    ffbTrace = getenv("IGS_FFB_TRACE") != NULL;
    b->random = 0xa36d2036;
    b->active = -1;
    Arm7 *cpu = &b->cpu;
    cpu->board = b;
    cpu->read = memoryRead;
    cpu->write = memoryWrite;
    cpu->service = maskService;
    cpu->v5 = 1;
    cpu->cp15[0] = 0x41059461;
    arm7Map(cpu, 0x02000000, 0x10000, b->sram, 0);
    arm7Map(cpu, 0x10000000, 0x800000, b->rom, 0);
    arm7Map(cpu, 0x20000000, EXT_SIZE, b->ext, 0);
    arm7Map(cpu, 0x30000000, 0x800000, b->rom, 0);
    arm7Map(cpu, 0x40000000, SHARED_SIZE, b->shared, 0);
    arm7Map(cpu, 0x50000000, sizeof(b->gpio), b->gpio, 0);
    arm7Map(cpu, 0x60000000, sizeof(b->control), b->control, 0);
    arm7Reset(cpu, 0x10000000, 0x53);
    arm7SetStack(cpu, 0x13, 0x2001f600);
    arm7SetStack(cpu, 0x12, 0x2001f800);
    arm7SetStack(cpu, 0x11, 0x2001fa00);
    arm7SetStack(cpu, 0x17, 0x2001fc00);
    arm7SetStack(cpu, 0x1b, 0x2001fe00);
    // Initialisation advances to the firmware's first frame boundary.
    boardStep(2000000);
    printf("IGS SD2: EROM started, pc=%08x\n", cpu->r[15]);
}

void igsSd2Install(const IgsGame *game, const char *dir)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, game->sd2Rom);
    boardStart(path);
    detourFunction(0x0810fd76, plxFind);
    detourFunction(0x08337e10, plxOpen);
    detourFunction(0x08338250, plxMap);
    detourFunction(0x083387f0, plxRead);
    detourFunction(0x0811012a, doorbell);
    detourFunction(0x0811013c, commandRead);
    if (getConfig()->inputMode != 2)
        desktopGamepadEnable();
}
