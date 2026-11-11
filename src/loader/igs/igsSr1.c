// Speed Rider M109CNC: original EROM on an IGS036, connected through a
// PLX9030 like Speed Driver 2's (igsSd2.c). The missing mask ROM is
// represented by its service interface, not an authentic ROM dump. The
// cabinet's STM32 I/O controller is emulated on USART0. Firmware owns game
// flow, credits and input calibration.
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "igs.h"
#include "igsArm7.h"
#include "igsStm32.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../patching/flowControl.h"

#define EXT_SIZE 0x1000000
#define SHARED_SIZE 0x20000
#define EROM_SIZE 0x200000

// Board NVRAM: battery-backed internal SRAM from 0x02000300 (bookkeeping,
// operator settings, VR calibration), kept in the game's config/.
#define NVRAM_OFFSET 0x300
#define NVRAM_SIZE 0x1d00

typedef struct
{
    Arm7 cpu;
    uint8_t *rom, *ext, *sram, *shared;
    uint8_t commandOut[0x1000], command[0x1000], gpio[0x1000], control[0x1000];
    uint32_t enabled, pending, handlers[32], random;
    uint64_t timerPeriod[3], timerNext[3];
    int active, inflight;
    // the STM32, on USART0
    IgsStm32 stm32;
} Sr1Board;

static Sr1Board *board;
static int trace;
static char nvramPath[PATH_MAX];
static uint8_t nvramSaved[NVRAM_SIZE];

static void nvramSave(void)
{
    char tmp[PATH_MAX + 8];
    FILE *f;

    if (!board || !nvramPath[0] || !memcmp(nvramSaved, board->sram + NVRAM_OFFSET, NVRAM_SIZE))
        return;
    snprintf(tmp, sizeof(tmp), "%s.tmp", nvramPath);
    if (!(f = fopen(tmp, "wb")))
        return;
    memcpy(nvramSaved, board->sram + NVRAM_OFFSET, NVRAM_SIZE);
    int ok = fwrite(nvramSaved, 1, NVRAM_SIZE, f) == NVRAM_SIZE;
    if (fclose(f) == 0 && ok)
        rename(tmp, nvramPath);
    else
        unlink(tmp);
}

__attribute__((destructor)) static void nvramFlush(void)
{
    nvramSave();
}

static uint64_t sr1Now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

static uint32_t memoryRead(Arm7 *cpu, uint32_t address, int size)
{
    Sr1Board *b = cpu->board;
    (void)size;
    if ((address & ~0xffu) == 0xfffa4000)
        return igsStm32Read(&b->stm32, address & 0xff);
    if (address == 0xfffff214 || address == 0xfffa8014)
        return 0x202;
    return 0;
}

static void memoryWrite(Arm7 *cpu, uint32_t address, uint32_t value, int size)
{
    Sr1Board *b = cpu->board;
    (void)size;
    if ((address & ~0xffu) == 0xfffa4000 && igsStm32Write(&b->stm32, address & 0xff, value) < 0)
        b->cpu.halted = 1;
    if (trace >= 3)
        printf("IGS SR1: MMIO %08x = %08x\n", address, value);
}

static void putWord(uint32_t address, uint32_t value)
{
    Arm7 *cpu = &board->cpu;
    uint8_t *p = cpu->page[address >> ARM7_PAGE_SHIFT];
    if (!p || (address & 3))
    {
        log_error("IGS SR1: invalid service write %08x", address);
        cpu->halted = 1;
        return;
    }
    memcpy(p + (address & cpu->pageMask[address >> ARM7_PAGE_SHIFT]), &value, 4);
}

// The EROM's veneers (0x10008168..0x1000823c) call the missing internal
// ROM: interrupt setup, timers, GPIO, the PC command latches, arithmetic
// and random numbers. Unknown services stop the CPU.
static void maskService(Arm7 *cpu, uint32_t address)
{
    Sr1Board *b = cpu->board;
    uint32_t a = cpu->r[0], v = cpu->r[1], n = cpu->r[2];
    if (trace >= 3)
        printf("IGS SR1: service %04x (%08x %08x %08x) lr=%08x\n", address, a, v, n, cpu->r[14]);
    cpu->r[0] = 0;
    switch (address)
    {
    case 0x3c: // interrupt enable (and unmask the CPU IRQ)
        if (a >= 32) goto invalid;
        b->enabled |= 1u << a;
        cpu->cpsr &= ~0x80u;
        break;
    case 0xbc: // interrupt acknowledge
        b->pending &= ~(1u << (a & 31));
        b->active = -1;
        cpu->irq = 0;
        break;
    case 0xd4: // register interrupt handler (r2)
        if (a >= 32) goto invalid;
        b->handlers[a] = n;
        break;
    case 0x9b0: // read the PC command latch into a two-word buffer
        putWord(a, b->command[6]);
        putWord(a + 4, 0);
        break;
    case 0x9cc: // PC mailbox taken
        b->inflight = 0;
        b->pending &= ~(1u << 3);
        break;
    case 0x9e8: cpu->r[0] = b->inflight; break;
    case 0xa24: // output command byte, port 0..7; port 7 publishes
        if (a >= 8 || v > 255) goto invalid;
        b->commandOut[a] = (uint8_t)v;
        if (a == 7)
            b->shared[0x3000] = b->commandOut[0];
        break;
    case 0xd94: if (!v) goto invalid; cpu->r[0] = a / v; break;
    case 0xe54: if (!v) goto invalid; cpu->r[0] = a % v; break;
    case 0xff4:
        b->random ^= b->random << 13;
        b->random ^= b->random >> 17;
        b->random ^= b->random << 5;
        cpu->r[0] = b->random;
        break;
    case 0x12a0: cpu->r[0] = 100; break;
    case 0x12b0: cpu->r[0] = 1; break;
    case 0x12b8: cpu->r[0] = 0x30001; break;
    case 0x15c0: cpu->r[0] = 0x10; break;
    case 0x1628: // timer r0=channel, r1=period, r2=clock selection
    {
        static const unsigned divisors[] = {2, 8, 32, 128, 1024};
        if (a >= 3 || n >= 5 || !v) goto invalid;
        b->timerPeriod[a] = (uint64_t)((double)v * divisors[n] * 1e9 / 65000000.0);
        b->timerNext[a] = sr1Now() + b->timerPeriod[a];
        break;
    }
    case 0x550: case 0x67c: case 0x998: case 0x9a4: case 0xa08:
    case 0xc9c: case 0xfc8: case 0x1068: case 0x12a8:
    case 0x15ac: case 0x15f4:
        break;
    default:
    invalid:
        log_error("IGS SR1: unsupported mask-ROM service %08x from %08x (%08x %08x %08x)", address, cpu->r[14], a,
                  v, n);
        cpu->halted = 1;
        return;
    }
    cpu->r[15] = cpu->r[14] & ~1u;
    cpu->cpsr = (cpu->cpsr & ~0x20u) | ((cpu->r[14] & 1) ? 0x20 : 0);
}

// Run synchronously at the PLX boundary: no unsynchronised host/ARM writes
// to the shared window. Timers follow monotonic time, independently of PC
// frame rate. IRQ 4 is USART0.
static void boardStep(unsigned instructions)
{
    Sr1Board *b = board;
    uint64_t now = sr1Now(), end = b->cpu.cycles + instructions;
    for (int i = 0; i < 3; i++)
        if (b->timerPeriod[i] && now >= b->timerNext[i])
        {
            b->pending |= 1u << (6 + i);
            b->timerNext[i] = now + b->timerPeriod[i];
        }
    while (b->cpu.cycles < end && !b->cpu.halted)
    {
        if (igsStm32Interrupt(&b->stm32))
            b->pending |= 1u << 4;
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
    igsStm32Sample(&board->stm32);
    board->inflight = 1;
    board->pending |= 1u << 3;
    boardStep(100000);
}

__attribute__((force_align_arg_pointer)) static unsigned commandRead(unsigned char port)
{
    static uint64_t nextSave, nextTrace;
    uint64_t now;

    boardStep(10000);
    now = sr1Now();
    if (now >= nextSave)
    {
        nextSave = now + 2000000000u;
        nvramSave();
    }
    if (trace >= 2 && now >= nextTrace)
    {
        nextTrace = now + 1000000000;
        printf("IGS SR1: pc %08x cpsr %08x enabled %08x pending %08x active %d out %02x shared %02x latch %02x\n",
               board->cpu.r[15], board->cpu.cpsr, board->enabled, board->pending, board->active,
               board->commandOut[0], board->shared[0x3000], board->command[6]);
    }
    return port < 8 ? board->commandOut[port] : 0;
}

static void boardStart(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        log_error("IGS SR1: cannot open EROM %s", path);
        exit(EXIT_FAILURE);
    }
    board = calloc(1, sizeof(*board));
    if (!board)
    {
        log_error("IGS SR1: cannot allocate board state");
        exit(EXIT_FAILURE);
    }
    Sr1Board *b = board;
    b->rom = calloc(1, 0x800000);
    b->ext = calloc(1, EXT_SIZE);
    b->sram = calloc(1, 0x10000);
    b->shared = calloc(1, SHARED_SIZE);
    if (!b->rom || !b->ext || !b->sram || !b->shared)
    {
        log_error("IGS SR1: cannot allocate board memory");
        exit(EXIT_FAILURE);
    }
    if (fread(b->rom, 1, EROM_SIZE, f) != EROM_SIZE || fgetc(f) != EOF)
    {
        log_error("IGS SR1: EROM must be exactly 2 MiB");
        exit(EXIT_FAILURE);
    }
    fclose(f);
    // The decrypted EROM starts with its reset code (push {r4, lr}).
    if (b->rom[0] != 0x10 || b->rom[1] != 0x40 || b->rom[2] != 0x2d || b->rom[3] != 0xe9)
    {
        log_error("IGS SR1: %s is not a decrypted EROM", path);
        exit(EXIT_FAILURE);
    }
    FILE *nv = fopen(nvramPath, "rb");
    if (nv)
    {
        if (fread(b->sram + NVRAM_OFFSET, 1, NVRAM_SIZE, nv) != NVRAM_SIZE)
            memset(b->sram + NVRAM_OFFSET, 0, NVRAM_SIZE);
        fclose(nv);
    }
    memcpy(nvramSaved, b->sram + NVRAM_OFFSET, NVRAM_SIZE);
    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    if (getenv("IGS_SR1_BREAK"))
    {
        // IGS_SR1_BREAK=address[,address...] (up to 8, hexadecimal)
        const char *list = getenv("IGS_SR1_BREAK");
        for (int i = 0; i < 8 && *list; i++)
        {
            char *end;
            b->cpu.breakpoints[i] = (uint32_t)strtoul(list, &end, 16);
            list = *end == ',' ? end + 1 : end;
        }
    }
    b->random = 0x3ac4b691;
    igsStm32Init(&b->stm32, "IGS SR1", trace);
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
    printf("IGS SR1: EROM started, pc=%08x\n", cpu->r[15]);
}

void igsSr1Install(const IgsGame *game, const char *dir)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, game->sr1Rom);
    snprintf(nvramPath, sizeof(nvramPath), "%s/config/nvram_sr1.bin", dir);
    boardStart(path);
    // The PLX9030 library and the game's board interface (SR, file CRC
    // 2770c27e).
    detourFunction(0x082d9a30, plxFind);
    detourFunction(0x0847b960, plxOpen);
    detourFunction(0x0847bda0, plxMap);
    detourFunction(0x0847c340, plxRead);
    detourFunction(0x082da580, doorbell);
    detourFunction(0x082da590, commandRead);
    if (getConfig()->inputMode != 2)
        desktopGamepadEnable();
}
