// The A36 board of IGS's later PC games (Speed Driver 3, 2010) and the
// game's link to it, /dev/pccard0 (see igsGame.h's a36Rom).
//
// The board is IGS's E-3000: an IGS036 (an ARM946E-S with a 16 KiB internal
// ROM, not dumped) running its external ROM, which the game uploads at
// start (the internal ROM's loader) and the dump holds already loaded; and
// an FPGA on the PCI side, with the cipher the game decrypts its scripts
// with. The firmware keeps the game's key, its FRAM (settings, bookkeeping)
// and the cabinet's lock, and answers the game's commands. It is emulated
// as it is: the CPU (igsArm7.c, v5), the IGS036's interrupt controller and
// timers as MAME has them (src/mame/igs/igs036.cpp, atmel_arm_aic.cpp), and
// the board's side of the PCI link, found in the firmware:
//
//   0x00000000  the internal ROM: its vectors go to the external ROM's
//   0x02000000  internal SRAM (64 KiB)
//   0x10000000  the external ROM (the firmware, run from reset)
//   0x30000000  the window the PC maps as BAR0 (data)
//   0x40000000  the PC's BAR1: its command registers (a word each)
//   0x50000000  external SRAM
//   0x60000000  the data window's owner (0: the board, 1: the PC)
//   0x60000004  written 1: the PC's interrupt
//   0xfffa0000  timers; 0xfffff000 the interrupt controller, the PC's
//               interrupt on its line 12
//   0xfffa4000, 0xfffa8000  USARTs (Atmel's): the firmware's console on
//               the first, always ready to send
//
// The game's side (the IGS driver's /dev/pccard0): ioctl 0x40046602 picks
// the BAR mmap then maps, 0x6601 interrupts the board, 0x80046603 reads
// whether the board interrupted the PC (the game's thread polls it every
// millisecond), 0x40046604 acknowledges it, 0x6600 resets; 0xc0106610 is
// the FPGA's cipher. The firmware's upload (the game's P2A_Loader calls) is
// stood in for: the dump's ROM is the board's from the start.
//
// The board runs in a thread of its own, on its clock (A36_CLOCK, an
// instruction a cycle) kept to the wall clock; in the firmware's idle loop
// it sleeps until its next timer or the PC's interrupt.
//
// Speed Rider 2's board differs: its firmware calls seven internal-ROM
// services (romService), takes its USART1 interrupt as the FIQ, and talks
// to the cabinet's STM32 I/O controller there (igsStm32.c); the game's
// scripts take another cipher mask.

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "igs.h"
#include "igsArm7.h"
#include "igsStm32.h"
#include "../common/desktopInput.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../patching/flowControl.h"
#include "../../minhook/include/MinHook.h"

// (the loader logs errors only in the game's process)
#define say(...) (printf("IGS A36: " __VA_ARGS__), putchar('\n'), fflush(stdout))


#define A36_CLOCK 100000000
#define A36_SLICE (A36_CLOCK / 1000)
// Idle: the board checks for its idle loop every A36_IDLE_STEP cycles (it
// still wakes every slice: the PC's writes to the shared memory, which the
// firmware polls, wake nothing).
#define A36_IDLE_STEP 2000
#define TIMER_CLOCK 20000000

#define ROM_SIZE 0x80000
#define SRAM_SIZE 0x10000
#define EXT_SRAM_SIZE 0x80000
#define BAR0_SIZE 0x100000
#define BAR1_SIZE 0x1000

#define AIC_PC 12
#define AIC_TIMER0 6

#define ARM946_ID 0x41059461

// The FPGA's cipher: a 16-byte block's bits moved (bit i to perm[i], LSB
// first in each byte), then a mask, by key, XORed (the masks as Kitsune has
// them).
static const uint8_t cipherPerm[128] = {
    0x5f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
    0x1f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
    0x77, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e,
    0x7d, 0x7e, 0x7f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x50, 0x51, 0x52, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
    0x6d, 0x6e, 0x6f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x78, 0x79, 0x7a, 0x7b, 0x7c,
};

static const struct
{
    uint8_t key[16], mask[16];
} cipherKeys[] = {
    // Speed Driver 3 (the firmware's command 6)
    {{0x4e, 0x67, 0xd5, 0x98, 0xe7, 0x3e, 0x6a, 0x6f, 0x67, 0x1d, 0x87, 0xcd, 0xa7, 0x13, 0xb2, 0x7c},
     {0xf1, 0x87, 0x83, 0x16, 0x41, 0x97, 0x31, 0x71, 0xde, 0x7f, 0x76, 0x2b, 0x95, 0x33, 0x3c, 0x5a}},
};

typedef struct
{
    uint32_t smr[32], svr[32];
    uint32_t enabled, pending, status, coreStatus, spurious, debug, fast;
    int level[9], levelIndex;
} Aic;

typedef struct
{
    Arm7 cpu;
    uint8_t irom[0x4000];
    uint8_t *rom;
    uint8_t sram[SRAM_SIZE];
    uint8_t extSram[EXT_SRAM_SIZE];
    uint8_t *bar[2];
    uint32_t owner;
    Aic aic;
    uint32_t timer[3][16];
    int timerEnabled[3];
    uint64_t timerNext[3]; // in cycles; 0: stopped
    uint32_t idleFrom, idleTo;
    // the cabinet's STM32 (Speed Rider 2), on this USART (stm32Usart - 1)
    int stm32Usart, stm32Line;
    IgsStm32 stm32;

    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    int pcInterrupt;   // the PC's interrupt, to raise
    int boardInterrupt; // the board's to the PC, pending
    int running;
    uint64_t wallStart;
} A36;

static A36 *board;
static int trace;
// Speed Driver 3 (its executable's file CRC): its game's and firmware's
// variables in the traces, at its own addresses
static int sd3;

static uint64_t nowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void unknownAccess(uint32_t address, const char *what, uint32_t value)
{
    static uint32_t seen[64];
    static int count;

    if (trace >= 3)
    {
        say("%s %08x (%08x) at %08x", what, address, value, board->cpu.r[15]);
        return;
    }
    for (int i = 0; i < count; i++)
        if (seen[i] == address)
            return;
    if (count < 64)
        seen[count++] = address;
    say("unknown %s %08x (%08x) at %08x", what, address, value, board->cpu.r[15]);
}

// --- the interrupt controller (MAME's atmel_arm_aic.cpp) ---

static int aicLevel(Aic *a)
{
    return a->level[a->levelIndex];
}

static void aicCheck(A36 *b)
{
    Aic *a = &b->aic;
    uint32_t mask = a->enabled & a->pending & ~a->fast;

    a->coreStatus = 0;
    for (int i = 31; i >= 0 && mask; i--)
        if ((mask & (1u << i)) && (int)(a->smr[i] & 7) > aicLevel(a))
        {
            a->coreStatus |= 2;
            break;
        }
    if (a->enabled & a->pending & a->fast)
        a->coreStatus |= 1;
    b->cpu.irq = (a->coreStatus & ~a->debug & 2) != 0;
    b->cpu.fiq = (a->coreStatus & ~a->debug & 1) != 0;
}

static void aicSetIrq(A36 *b, int line, int state)
{
    if (state)
        b->aic.pending |= 1u << line;
    else if (b->aic.smr[line] & 0x40)
        b->aic.pending &= ~(1u << line);
    aicCheck(b);
}

static uint32_t aicVector(A36 *b)
{
    Aic *a = &b->aic;
    uint32_t mask = a->enabled & a->pending & ~a->fast, vector = a->spurious;

    if (mask)
    {
        int priority = aicLevel(a), found = -1;
        for (int i = 31; i >= 0; i--)
            if ((mask & (1u << i)) && (int)(a->smr[i] & 7) >= priority)
            {
                found = i;
                priority = a->smr[i] & 7;
            }
        if (found > 0)
        {
            a->status = (uint32_t)found;
            vector = a->svr[found];
            if (a->levelIndex < 8)
                a->level[++a->levelIndex] = (int)(a->smr[found] & 7);
            if (a->smr[found] & 0x20)
                a->pending &= ~(1u << found); // edge triggered
        }
    }
    a->coreStatus &= ~2u;
    b->cpu.irq = 0;
    return vector;
}

static uint32_t aicRead(A36 *b, uint32_t offset)
{
    Aic *a = &b->aic;

    if (offset < 0x80)
        return a->smr[offset / 4];
    if (offset < 0x100)
        return a->svr[(offset - 0x80) / 4];
    switch (offset)
    {
    case 0x100:
        return aicVector(b);
    case 0x104:
    {
        uint32_t fast = a->enabled & a->pending & a->fast;
        for (int i = 0; i < 32; i++)
            if ((fast & (1u << i)) && (a->smr[i] & 0x20))
                a->pending &= ~(1u << i); // edge triggered
        aicCheck(b);
        return fast ? a->svr[0] : a->spurious;
    }
    case 0x108:
        return a->status;
    case 0x10c:
        return a->pending;
    case 0x110:
        return a->enabled;
    case 0x114:
        return a->coreStatus;
    case 0x148:
        return a->fast;
    }
    unknownAccess(0xfffff000 + offset, "read", 0);
    return 0;
}

static void aicWrite(A36 *b, uint32_t offset, uint32_t value)
{
    Aic *a = &b->aic;

    if (offset < 0x80)
    {
        a->smr[offset / 4] = value;
        return;
    }
    if (offset < 0x100)
    {
        a->svr[(offset - 0x80) / 4] = value;
        return;
    }
    switch (offset)
    {
    case 0x120:
        a->enabled |= value;
        break;
    case 0x124:
        a->enabled &= ~value;
        break;
    case 0x128:
        a->pending &= ~value;
        break;
    case 0x12c:
        a->pending |= value;
        break;
    case 0x130:
        a->status = 0;
        if (a->levelIndex)
            a->levelIndex--;
        break;
    case 0x100:
        return; // IVR written: the end of a read in protect mode, not used
    case 0x134:
        a->spurious = value;
        return;
    case 0x138:
        a->debug = value;
        break;
    case 0x140:
        a->fast |= value;
        break;
    case 0x144:
        a->fast &= ~value | 1;
        break;
    default:
        unknownAccess(0xfffff000 + offset, "write", value);
        return;
    }
    aicCheck(b);
}

// --- timers (MAME's igs036.cpp) ---

static uint32_t timerClock(A36 *b, int n)
{
    static const uint32_t divisors[4] = {2, 8, 32, 128};
    uint32_t clock = b->timer[n][1] & 7;
    return clock < 4 ? TIMER_CLOCK / divisors[clock] : clock == 4 ? 32768 : 0;
}

static void timerSchedule(A36 *b, int n)
{
    uint32_t clock = timerClock(b, n);
    if (b->timerEnabled[n] && clock)
    {
        uint32_t ticks = b->timer[n][7] ? b->timer[n][7] : 0x10000;
        b->timerNext[n] = b->cpu.cycles + (uint64_t)((double)ticks * A36_CLOCK / clock);
    }
    else
        b->timerNext[n] = 0;
}

static void timerExpired(A36 *b, int n)
{
    b->timer[n][8] |= 0x10;
    if (b->timer[n][11] & 0x10)
        aicSetIrq(b, AIC_TIMER0 + n, 1);
    timerSchedule(b, n);
}

static uint32_t timerRead(A36 *b, uint32_t offset)
{
    uint32_t n = offset / 0x40, reg = (offset % 0x40) / 4;

    if (n >= 3)
        return 0;
    if (reg == 4)
    {
        uint32_t clock = timerClock(b, (int)n), ticks = b->timer[n][7] ? b->timer[n][7] : 0x10000;
        if (!b->timerNext[n] || !clock)
            return 0;
        uint64_t left = b->timerNext[n] > b->cpu.cycles ? b->timerNext[n] - b->cpu.cycles : 0;
        return (ticks - (uint32_t)((double)left * clock / A36_CLOCK)) & 0xffff;
    }
    if (reg == 8)
    {
        uint32_t v = b->timer[n][8] | (b->timerEnabled[n] ? 0x10000 : 0);
        b->timer[n][8] = 0;
        aicSetIrq(b, AIC_TIMER0 + (int)n, 0);
        return v;
    }
    return b->timer[n][reg];
}

static void timerWrite(A36 *b, uint32_t offset, uint32_t value)
{
    uint32_t n = offset / 0x40, reg = (offset % 0x40) / 4;

    if (trace >= 2)
        say("timer %u register %u = %08x", n, reg, value);
    if (n >= 3)
    {
        if (offset == 0xc0 && (value & 1))
            for (int i = 0; i < 3; i++)
                timerSchedule(b, i);
        return;
    }
    switch (reg)
    {
    case 0:
        if (value & 1)
            b->timerEnabled[n] = 1;
        if (value & 2)
            b->timerEnabled[n] = 0;
        timerSchedule(b, (int)n);
        break;
    case 9:
        b->timer[n][11] |= value;
        aicSetIrq(b, AIC_TIMER0 + (int)n, (b->timer[n][8] & b->timer[n][11] & 0x10) != 0);
        break;
    case 10:
        b->timer[n][11] &= ~value;
        aicSetIrq(b, AIC_TIMER0 + (int)n, (b->timer[n][8] & b->timer[n][11] & 0x10) != 0);
        break;
    case 4:
    case 8:
    case 11:
        break;
    default:
        b->timer[n][reg] = value;
        if (reg == 1 || reg == 7)
            timerSchedule(b, (int)n);
        break;
    }
}

// --- the firmware's console (its USARTs, the debug UART) ---

static void console(int port, uint32_t value)
{
    static char line[3][256];
    static int length[3];

    if (value == '\n' || length[port] == (int)sizeof(line[port]) - 1)
    {
        line[port][length[port]] = '\0';
        if (trace)
            say("console %d: %s", port, line[port]);
        length[port] = 0;
    }
    else if (value != '\r')
        line[port][length[port]++] = (char)value;
}

// --- the cabinet's STM32 (Speed Rider 2) ---

// Its USART's interrupt (AIC line 4 + USART, the firmware's fast one),
// edge triggered: taken when the USART starts asking for it. Each byte
// takes STM32_BYTE cycles (115200 baud would be 8700), and ends only out
// of the handler (FIQs masked): its last act clears the interrupt.
#define STM32_BYTE 1000

static void stm32Interrupt(A36 *b)
{
    int line = igsStm32Interrupt(&b->stm32);

    if (line && !b->stm32Line)
        aicSetIrq(b, 3 + b->stm32Usart, 1);
    b->stm32Line = line;
}

static uint32_t stm32Access(A36 *b, uint32_t offset, uint32_t value, int write)
{
    uint32_t result = 0;

    pthread_mutex_lock(&b->lock);
    if (!write)
        result = igsStm32Read(&b->stm32, offset);
    else if (igsStm32Write(&b->stm32, offset, value) < 0)
        b->cpu.halted = 1;
    pthread_mutex_unlock(&b->lock);
    stm32Interrupt(b);
    return result;
}

// --- the board's memory ---

static uint32_t boardRead(Arm7 *cpu, uint32_t address, int size)
{
    A36 *b = cpu->board;

    (void)size;
    if (address >= 0xfffff000 && address < 0xfffff200)
        return aicRead(b, address & 0xffc);
    if (address >= 0xfffa0000 && address < 0xfffa0100)
        return timerRead(b, address & 0xfc);
    if ((address & ~3u) == 0x60000000)
        return b->owner;
    if (address == 0x60000042 || address == 0x60000044)
        return 0; // Speed Rider 2: read at its start, not used
    if (address == 0xfffffa08)
        return 4; // decryption on
    if (address == 0xfffffa0c)
        return 0x180;
    if (address >= 0xfffff400 && address < 0xfffff500)
        return 0; // GPIO
    if (b->stm32Usart && (address & ~0xffu) == 0xfffa0000 + b->stm32Usart * 0x4000u)
        return stm32Access(b, address & 0xff, 0, 0);
    if (address == 0xfffff214 || address == 0xfffa4014 || address == 0xfffa8014)
        return 0x202; // the USARTs, the debug UART: ready to send
    if ((address & 0xffffbfe0) == 0xfffa0000 && (address & 0xc000))
        return 0; // the USARTs' other registers
    unknownAccess(address, "read", 0);
    return 0;
}

static void boardWrite(Arm7 *cpu, uint32_t address, uint32_t value, int size)
{
    A36 *b = cpu->board;

    (void)size;
    if (address >= 0xfffff000 && address < 0xfffff200)
    {
        aicWrite(b, address & 0xffc, value);
        return;
    }
    if (address >= 0xfffa0000 && address < 0xfffa0100)
    {
        timerWrite(b, address & 0xfc, value);
        return;
    }
    if ((address & ~3u) == 0x60000000)
    {
        b->owner = value;
        return;
    }
    if ((address & ~3u) == 0x60000008)
        return; // Speed Rider 2: around its STM32's packets (a transceiver's direction?)
    if ((address & ~3u) == 0x60000004)
    {
        if (value & 1)
        {
            if (trace)
            {
                uint32_t out = arm7Read32(cpu, 0x0200677c), count = arm7Read32(cpu, 0x02000008);
                uint32_t *window = (uint32_t *)b->bar[0];
                say("interrupt to the PC: function %u command %u count %u in %05x out %05x (game %05x) data %08x %08x %08x %08x",
                    arm7Read32(cpu, 0x02000000), arm7Read32(cpu, 0x02000004), count, arm7Read32(cpu, 0x02000030), out,
                    trace >= 2 && sd3 ? *(volatile uint32_t *)0x09755338 : 0, window[out - count], window[out - count + 1],
                    window[out - count + 2], window[out - count + 3]);
            }
            pthread_mutex_lock(&b->lock);
            b->boardInterrupt = 1;
            pthread_mutex_unlock(&b->lock);
        }
        return;
    }
    if (b->stm32Usart && (address & ~0xffu) == 0xfffa0000 + b->stm32Usart * 0x4000u)
    {
        stm32Access(b, address & 0xff, value, 1);
        return;
    }
    if (address == 0xfffff21c || address == 0xfffa401c || address == 0xfffa801c)
    {
        console(address == 0xfffff21c ? 2 : (address >> 15) & 1, value);
        return;
    }
    if ((address & 0xffff3fc0) == 0xfffa0000 && (address & 0xc000))
        return; // the USARTs' set-up
    if (address >= 0xffffec00) // memory controller, bus matrix, UART, GPIO, reset, RTT, watchdog
        return;
    unknownAccess(address, "write", value);
}

// --- the internal ROM's services ---

// Speed Rider 2's and Speed Driver 4's firmware call the internal ROM through a table of seven
// functions in SRAM (romServices), which the internal ROM fills at boot:
// each is here an address in our internal ROM, below 0x4000, where the CPU
// calls romService instead. Their arguments and result go through the
// firmware's own wrappers (0x10004f8c); 4 divides the clock by the baud
// rate, 1 hands the data window over (0 the board, 1 the PC), 0
// interrupts the PC (A2P_Comm_iSendInterrupt). The others are not seen
// called.
#define SERVICE_BASE 0x100
#define SERVICES 7

static uint32_t romServices;

static void romService(Arm7 *cpu, uint32_t address)
{
    A36 *b = cpu->board;
    uint32_t n = (address - SERVICE_BASE) / 4;

    if (address < 0x20)
    {
        // the vectors (an IRQ's too): the external ROM's
        cpu->r[15] = 0x10000000 + address;
        return;
    }
    if (trace >= 3)
        say("service %u (%08x %08x) from %08x", n, cpu->r[0], cpu->r[1], cpu->r[14]);
    switch (address < SERVICE_BASE || (address & 3) ? SERVICES : n)
    {
    case 0:
        pthread_mutex_lock(&b->lock);
        b->boardInterrupt = 1;
        pthread_mutex_unlock(&b->lock);
        cpu->r[0] = 0;
        break;
    case 1:
        b->owner = cpu->r[0];
        break;
    case 3:
        cpu->r[0] = cpu->r[1] ? cpu->r[0] % cpu->r[1] : 0;
        break;
    case 4:
        cpu->r[0] = cpu->r[1] ? cpu->r[0] / cpu->r[1] : 0;
        break;
    case 2: case 5: case 6:
        say("service %u (%08x %08x) from %08x: not known, ignored", n, cpu->r[0], cpu->r[1], cpu->r[14]);
        cpu->r[0] = 0;
        break;
    default:
        log_error("IGS A36: the internal ROM at %08x, from %08x", address, cpu->r[14]);
        cpu->halted = 1;
        return;
    }
    cpu->r[15] = cpu->r[14] & ~1u;
    cpu->cpsr = (cpu->cpsr & ~0x20u) | ((cpu->r[14] & 1) ? 0x20 : 0);
}

// --- the board's thread ---

static uint64_t wallCycles(A36 *b)
{
    return (uint64_t)((double)(nowNs() - b->wallStart) * (A36_CLOCK / 1e9));
}

static uint64_t nextTimer(A36 *b, uint64_t limit)
{
    for (int n = 0; n < 3; n++)
        if (b->timerNext[n] && b->timerNext[n] < limit)
            limit = b->timerNext[n];
    return limit;
}

static int idle(A36 *b)
{
    uint32_t pc = b->cpu.r[15];
    return b->idleFrom && pc >= b->idleFrom && pc <= b->idleTo && !b->cpu.irq;
}

static void *boardThread(void *arg)
{
    A36 *b = arg;
    Arm7 *cpu = &b->cpu;

    uint64_t nextReport = 0;

    b->wallStart = nowNs();
    while (b->running && !cpu->halted)
    {
        uint64_t wall = wallCycles(b), end;

        if (trace >= 2 && cpu->cycles >= nextReport)
        {
            nextReport = cpu->cycles + A36_CLOCK;
            say("pc %08x cpsr %08x irq %d aic enabled %08x pending %08x level %d smr12 %08x svr12 %08x", cpu->r[15],
                cpu->cpsr, cpu->irq, b->aic.enabled, b->aic.pending, b->aic.levelIndex, b->aic.smr[12], b->aic.svr[12]);
        }

        pthread_mutex_lock(&b->lock);
        if (b->pcInterrupt)
        {
            b->pcInterrupt = 0;
            pthread_mutex_unlock(&b->lock);
            if (trace)
                say("interrupt from the PC");
            aicSetIrq(b, AIC_PC, 1);
        }
        else if (cpu->cycles > wall || idle(b))
        {
            // ahead of the clock, or idle: wait for it, the next timer or
            // the PC's interrupt
            uint64_t until = cpu->cycles > wall ? cpu->cycles : nextTimer(b, wall + A36_SLICE);
            uint64_t ns = (uint64_t)((double)(until > wall ? until - wall : 0) * (1e9 / A36_CLOCK));
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            // (no 64-bit division in the loader: libgcc is not linked)
            {
                double t = (double)ts.tv_nsec + (double)ns;
                time_t seconds = (time_t)(t / 1e9);
                ts.tv_sec += seconds;
                ts.tv_nsec = (long)(t - (double)seconds * 1e9);
            }
            if (ns > 1000)
                pthread_cond_timedwait(&b->wake, &b->lock, &ts);
            pthread_mutex_unlock(&b->lock);
            if (idle(b) && !b->pcInterrupt)
            {
                // idle time passes at once, but not over an STM32 exchange
                // (its bytes paced by steps, its timeout a timer): behind
                // the clock (a slow host), the jump would time it out
                uint64_t now = wallCycles(b);
                if (now > cpu->cycles && !(b->stm32Usart && igsStm32Busy(&b->stm32)))
                    cpu->cycles = now;
            }
        }
        else
            pthread_mutex_unlock(&b->lock);

        end = cpu->cycles + A36_SLICE;
        while (cpu->cycles < end && !cpu->halted)
        {
            uint64_t stop = nextTimer(b, end);
            if (b->stm32Usart && stop > cpu->cycles + STM32_BYTE)
                stop = cpu->cycles + STM32_BYTE;
            // in short steps, so the idle loop is seen soon after it starts
            if (b->idleFrom && stop > cpu->cycles + A36_IDLE_STEP)
                stop = cpu->cycles + A36_IDLE_STEP;
            arm7Run(cpu, stop);
            if (b->stm32Usart)
            {
                pthread_mutex_lock(&b->lock);
                if (!(cpu->cpsr & 0x40))
                    igsStm32Tick(&b->stm32);
                pthread_mutex_unlock(&b->lock);
                stm32Interrupt(b);
            }
            for (int n = 0; n < 3; n++)
                if (b->timerNext[n] && cpu->cycles >= b->timerNext[n])
                    timerExpired(b, n);
            if (idle(b))
                break;
        }
    }
    if (cpu->halted)
        log_error("IGS A36: the board stopped");
    return NULL;
}

static int stm32Usart;
static uint16_t stm32Pots[4][3];
static int stm32Throttle;

static int a36Start(const char *romPath, uint32_t idleFrom, uint32_t idleTo)
{
    FILE *f = fopen(romPath, "rb");
    A36 *b;

    if (!f)
    {
        log_error("IGS A36: cannot open the board's ROM %s", romPath);
        return -1;
    }
    b = calloc(1, sizeof(*b));
    b->rom = calloc(1, ROM_SIZE);
    if (fread(b->rom, 1, ROM_SIZE, f) == 0)
        say("%s is empty", romPath);
    fclose(f);
    for (int i = 0; i < 2; i++)
        b->bar[i] = mmap(NULL, i ? BAR1_SIZE : BAR0_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    // The internal ROM's vectors: ldr pc, [pc, #0x18] to the external
    // ROM's (its own vector table at its start).
    for (uint32_t i = 0; i < 8; i++)
    {
        uint32_t ldr = 0xe59ff018, target = 0x10000000 + i * 4;
        memcpy(b->irom + i * 4, &ldr, 4);
        memcpy(b->irom + 0x20 + i * 4, &target, 4);
    }

    b->cpu.board = b;
    b->cpu.read = boardRead;
    b->cpu.write = boardWrite;
    b->cpu.v5 = 1;
    b->cpu.cp15[0] = ARM946_ID;
    arm7Map(&b->cpu, 0x00000000, sizeof(b->irom), b->irom, 1);
    arm7Map(&b->cpu, 0x02000000, SRAM_SIZE, b->sram, 0);
    arm7Map(&b->cpu, 0x10000000, ROM_SIZE, b->rom, 1);
    arm7Map(&b->cpu, 0x30000000, BAR0_SIZE, b->bar[0], 0);
    arm7Map(&b->cpu, 0x40000000, BAR1_SIZE, b->bar[1], 0);
    arm7Map(&b->cpu, 0x50000000, EXT_SRAM_SIZE, b->extSram, 0);
    b->cpu.irqVector = 0x18;
    b->aic.fast = 1;
    b->aic.level[0] = -1;
    b->idleFrom = idleFrom;
    b->idleTo = idleTo;
    b->stm32Usart = stm32Usart;
    igsStm32Init(&b->stm32, "IGS A36", trace);
    if (stm32Usart)
    {
        if (stm32Pots[0][2])
        {
            memcpy(b->stm32.pots, stm32Pots, sizeof(b->stm32.pots));
            b->stm32.throttle = stm32Throttle;
        }
        // the rider on the seat; edges paced for the firmware's fast
        // interrupt
        b->stm32.rider = 1;
        b->stm32.paced = 1;
    }
    arm7Reset(&b->cpu, 0x10000000, 0xd3);
    if (romServices)
    {
        b->cpu.service = romService;
        for (uint32_t i = 0; i < SERVICES; i++)
        {
            uint32_t entry = SERVICE_BASE + i * 4;
            memcpy(b->sram + (romServices - 0x02000000) + i * 4, &entry, 4);
        }
    }
    for (const char *list = getenv("IGS_A27_BREAK"); list && *list;)
    {
        char *end;
        uint32_t address = (uint32_t)strtoul(list, &end, 16);
        for (int i = 0; i < 8; i++)
            if (!b->cpu.breakpoints[i])
            {
                b->cpu.breakpoints[i] = address;
                break;
            }
        list = *end == ',' ? end + 1 : end;
        if (end == list && *end != ',')
            break;
    }

    pthread_mutex_init(&b->lock, NULL);
    pthread_cond_init(&b->wake, NULL);
    b->running = 1;
    board = b;
    if (pthread_create(&b->thread, NULL, boardThread, b) != 0)
    {
        log_error("IGS A36: cannot start the board");
        return -1;
    }
    pthread_setname_np(b->thread, "igs-a36");
    say("board started (%s)", romPath);
    return 0;
}

// --- the FPGA's cipher ---

// The game's own mask (Speed Rider 2: SD3's key, another mask), else the
// key's.
static const uint8_t *gameMask;
// Speed Driver 4: the mask changes every 64 KiB (from the key set, here
// from a file's first request): the game's table, chunk the next one.
static const uint8_t (*chunkMasks)[16];
static int chunkCount, chunk;

static void cipher(const uint8_t *in, uint8_t *out, uint32_t size, const uint8_t *key, int decrypt)
{
    const uint8_t *mask = gameMask;
    uint8_t block[16], m[16];

    for (size_t i = 0; !mask && i < sizeof(cipherKeys) / sizeof(cipherKeys[0]); i++)
        if (!memcmp(cipherKeys[i].key, key, 16))
            mask = cipherKeys[i].mask;
    if (!mask)
    {
        static int warned;
        if (!warned++)
        {
            char hex[33];
            for (int i = 0; i < 16; i++)
                snprintf(hex + i * 2, 3, "%02x", key[i]);
            say("unknown cipher key %s, data left as is", hex);
        }
        memmove(out, in, size);
        return;
    }
    if (decrypt)
    {
        // the mask moved as the bits are
        memset(m, 0, sizeof(m));
        for (int i = 0; i < 128; i++)
            if (mask[cipherPerm[i] >> 3] >> (cipherPerm[i] & 7) & 1)
                m[i >> 3] |= (uint8_t)(1 << (i & 7));
        mask = m;
    }
    for (uint32_t at = 0; at + 16 <= size; at += 16)
    {
        memset(block, 0, sizeof(block));
        for (int i = 0; i < 128; i++)
        {
            int from = decrypt ? cipherPerm[i] : i, to = decrypt ? i : cipherPerm[i];
            if (in[at + (uint32_t)(from >> 3)] >> (from & 7) & 1)
                block[to >> 3] |= (uint8_t)(1 << (to & 7));
        }
        for (int i = 0; i < 16; i++)
            out[at + (uint32_t)i] = block[i] ^ mask[i];
    }
}

// --- /dev/pccard0 ---

static int pccardFds[4] = {-1, -1, -1, -1};
static int selectedBar;
static char romPath[PATH_MAX];
static uint32_t romIdle[2];
static int (*realOpen)(const char *, int, ...);
static int (*realClose)(int);
static int (*realIoctl)(int, unsigned long, ...);
static void *(*realMmap)(void *, size_t, int, int, int, off_t);

static int isPccard(int fd)
{
    for (int i = 0; i < 4; i++)
        if (fd >= 0 && pccardFds[i] == fd)
            return 1;
    return 0;
}

__attribute__((force_align_arg_pointer)) static int pccardOpen(const char *path, int flags, ...)
{
    va_list ap;
    int mode;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    if (path && !strcmp(path, "/dev/pccard0"))
    {
        int fd = realOpen("/dev/null", O_RDWR);
        for (int i = 0; i < 4; i++)
            if (pccardFds[i] < 0)
            {
                pccardFds[i] = fd;
                break;
            }
        if (trace)
            say("/dev/pccard0 opened (%d)", fd);
        return fd;
    }
    // The game's other opens come here too, not to the loader's open: the
    // cabinet's paths mapped (its serial ports: none).
    char buf[PATH_MAX];
    return realOpen(igsRedirectPath(path, buf, sizeof(buf)), flags, mode);
}

__attribute__((force_align_arg_pointer)) static int pccardClose(int fd)
{
    for (int i = 0; i < 4; i++)
        if (fd >= 0 && pccardFds[i] == fd)
            pccardFds[i] = -1;
    return realClose(fd);
}

typedef struct
{
    uint8_t *data;
    uint32_t unused, size, flags;
} CipherBuffer;

typedef struct
{
    int32_t decrypt;
    CipherBuffer *in, *out;
    const uint8_t *key;
} CipherRequest;

__attribute__((force_align_arg_pointer)) static int pccardIoctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (!isPccard(fd))
        return realIoctl(fd, request, arg);
    switch (request & 0xffffffffu)
    {
    case 0x6600: // reset
        if (trace)
            say("reset (ignored)");
        return 0;
    case 0x6601: // the board's interrupt
        if (trace >= 3)
        {
            void *frames[12];
            int n = backtrace(frames, 12);
            say("the PC's command from:");
            backtrace_symbols_fd(frames, n, 1);
        }
        if (board)
        {
            pthread_mutex_lock(&board->lock);
            if (board->stm32Usart)
                igsStm32Sample(&board->stm32);
            board->pcInterrupt = 1;
            pthread_cond_signal(&board->wake);
            pthread_mutex_unlock(&board->lock);
        }
        return 0;
    case 0x40046602: // the BAR mmap maps
        selectedBar = arg ? *(int *)arg : 0;
        return 0;
    case 0x80046603: // the board interrupted the PC
        if (arg)
        {
            int pending = 0;
            if (board)
            {
                pthread_mutex_lock(&board->lock);
                pending = board->boardInterrupt;
                pthread_mutex_unlock(&board->lock);
            }
            *(int *)arg = pending;
            if (pending && trace >= 2 && sd3)
            {
                uint32_t slot = *(volatile uint32_t *)0x097bf58c, *regs = (uint32_t *)board->bar[1];
                say("the PC takes the interrupt: slot %u: %08x %08x %08x %08x, reply offset %05x", slot, regs[slot],
                    regs[slot + 1], regs[slot + 2], regs[slot + 3], *(volatile uint32_t *)0x09755338);
            }
        }
        return 0;
    case 0x40046604: // acknowledged
        if (board)
        {
            pthread_mutex_lock(&board->lock);
            board->boardInterrupt = 0;
            pthread_mutex_unlock(&board->lock);
        }
        return 0;
    case 0x6614: // the cipher's "CBC mode" on and off (Speed Driver 4): its
    case 0x6615: // scripts decrypt block by block all the same
        return 0;
    case 0xc0106610: // the FPGA's cipher
    {
        CipherRequest *r = arg;
        if (!r || !r->in || !r->out || !r->key)
            return -1;
        if (!chunkMasks)
        {
            cipher(r->in->data, r->out->data, r->in->size, r->key, r->decrypt);
            return 0;
        }
        // Speed Driver 4: each 64 KiB takes the next mask; a file comes as
        // 64 KiB requests, its last one shorter (or as one request).
        for (uint32_t at = 0; at < r->in->size; at += 0x10000)
        {
            uint32_t size = r->in->size - at < 0x10000 ? r->in->size - at : 0x10000;
            const uint8_t *saved = gameMask;
            if (chunk >= chunkCount)
            {
                say("cipher chunk %d past the %d masks known, data left as is", chunk, chunkCount);
                memmove(r->out->data + at, r->in->data + at, size);
            }
            else
            {
                gameMask = chunkMasks[chunk];
                cipher(r->in->data + at, r->out->data + at, size, r->key, r->decrypt);
                gameMask = saved;
            }
            chunk = size == 0x10000 ? chunk + 1 : 0;
        }
        if (r->in->size > 0x10000 || r->in->size % 0x10000)
            chunk = 0;
        return 0;
    }
    }
    say("unknown ioctl %08lx", request);
    errno = EINVAL;
    return -1;
}

__attribute__((force_align_arg_pointer)) static void *pccardMmap(void *address, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (!isPccard(fd))
        return realMmap(address, length, prot, flags, fd, offset);
    if (!board || selectedBar < 0 || selectedBar > 1 || length > (size_t)(selectedBar ? BAR1_SIZE : BAR0_SIZE))
    {
        log_error("IGS A36: cannot map BAR%d (%zu bytes)", selectedBar, length);
        return MAP_FAILED;
    }
    if (trace)
        say("BAR%d mapped (%zu bytes)", selectedBar, length);
    return board->bar[selectedBar];
}

// The game's upload of the firmware (P2A_Loader*): the board runs the
// dump's from the first.
__attribute__((force_align_arg_pointer)) static int loaderDone(void)
{
    return 0;
}

__attribute__((force_align_arg_pointer)) static int loaderLoad(void)
{
    say("the firmware not uploaded, the board runs %s", romPath);
    if (!board && a36Start(romPath, romIdle[0], romIdle[1]) != 0)
        return -1;
    return 0;
}

// OIS's joystick scan: an empty std::vector<JoyStickInfo> (three null
// pointers, returned through the caller's pointer as i386 returns structs).
typedef struct
{
    void *begin, *end, *capacity;
} EmptyVector;

__attribute__((force_align_arg_pointer)) static EmptyVector noJoysticks(void)
{
    EmptyVector none = {NULL, NULL, NULL};
    return none;
}

// Power Truck's lamp functions (lamp_control.lua) blink on timer % 2 < 1
// and > 1, and return nothing at exactly 1: the cast to the lamp's byte
// fails, which luabind (built without exceptions) asserts on. The lamp is
// off instead.
__attribute__((force_align_arg_pointer)) static unsigned char lampOff(void *L, const void *type)
{
    (void)L;
    (void)type;
    return 0;
}

// The game's Lua errors (luabind's error callback): the message on top of
// the stack.
static const char *(*luaToLString)(void *L, int index, size_t *length);
static int (*realLuaPCall)(void *, int, int, int);

__attribute__((force_align_arg_pointer)) static void luaError(void *L)
{
    const char *message = luaToLString(L, -1, NULL);
    printf("IGS: Lua error: %s\n", message ? message : "(no message)");
    fflush(stdout);
}

// State::open replaces the error callback for each Lua state. Override the
// getter instead so errors remain visible after those initialisations.
__attribute__((force_align_arg_pointer)) static void (*getLuaErrorCallback(void))(void *)
{
    return luaError;
}

__attribute__((force_align_arg_pointer)) static int luaPCall(void *L, int nargs, int results, int handler)
{
    int status = realLuaPCall(L, nargs, results, handler);
    if (status)
        luaError(L);
    return status;
}

// IGS_TRACE=3: where the game's stack check failed.
__attribute__((force_align_arg_pointer)) static void stackCheckFailed(void)
{
    say("stack check failed, called from %p", __builtin_return_address(0));
    abort();
}

void igsA36Install(const IgsGame *game, const char *gameDir)
{
    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    sd3 = game->fileCrc32 == 0x760014aa;
    snprintf(romPath, sizeof(romPath), "%s/%s", gameDir, game->a36Rom);
    romIdle[0] = game->a36Idle[0];
    romIdle[1] = game->a36Idle[1];
    romServices = game->a36Services;
    stm32Usart = game->a36Stm32Usart;
    memcpy(stm32Pots, game->a36Stm32Pots, sizeof(stm32Pots));
    stm32Throttle = game->a36Stm32Throttle;
    for (int i = 0; i < 16; i++)
        if (game->a36CipherMask[i])
            gameMask = game->a36CipherMask;
    chunkMasks = game->a36CipherChunkMasks;
    chunkCount = game->a36CipherChunks;
    if (stm32Usart && getConfig()->inputMode != 2)
        desktopGamepadEnable();
    *(void **)&realOpen = hookExecutableImport("open", (void *)pccardOpen);
    *(void **)&realClose = hookExecutableImport("close", (void *)pccardClose);
    *(void **)&realIoctl = hookExecutableImport("ioctl", (void *)pccardIoctl);
    *(void **)&realMmap = hookExecutableImport("mmap", (void *)pccardMmap);
    if (trace >= 3)
        hookExecutableImport("__stack_chk_fail", (void *)stackCheckFailed);
    // IGS_PATCH=address:hex bytes[,...] (debugging): the game's code patched
    for (const char *list = getenv("IGS_PATCH"); list && *list;)
    {
        char bytes[256];
        char *end;
        uint32_t address = (uint32_t)strtoul(list, &end, 16);
        size_t n = 0;
        if (*end != ':')
            break;
        for (end++; *end && *end != ',' && n < sizeof(bytes) - 1; end++)
            bytes[n++] = *end;
        bytes[n] = '\0';
        patchMemoryFromString(address, bytes);
        say("patched %08x with %s", address, bytes);
        list = *end == ',' ? end + 1 : end;
    }
    if (!realOpen || !realClose || !realIoctl || !realMmap)
        log_error("IGS A36: the game's open, close, ioctl or mmap import is missing");
    *(void **)&luaToLString = (void *)(uintptr_t)game->luaToLString;
    if (game->luabindGetErrorCallback && luaToLString)
        detourFunction(game->luabindGetErrorCallback, (void *)getLuaErrorCallback);
    if (trace && game->luaPCall && luaToLString &&
        MH_CreateHook((void *)(uintptr_t)game->luaPCall, (void *)luaPCall, (void **)&realLuaPCall) != MH_OK)
        log_error("IGS A36: cannot hook Lua protected calls");
    if (game->oisScanJoys)
        detourFunction(game->oisScanJoys, (void *)noJoysticks);
    if (game->luabindByteCastFailed)
        detourFunction(game->luabindByteCastFailed, (void *)lampOff);
    if (game->a36ReplyPublish)
    {
        // The reply's three loads and stores up to the epilogue (lea
        // -0xc(%ebp),%esp), the wait's movb $0,disp8(%r) among them (SD3
        // and SR2 clear it first, SD4 before the last store): the movb
        // moved after the stores.
        const uint8_t *code = (const uint8_t *)(uintptr_t)game->a36ReplyPublish;
        uint8_t reordered[32];
        size_t end = 0, wait = SIZE_MAX, n = 0;
        while (end + 3 < sizeof(reordered) && !(code[end] == 0x8d && code[end + 1] == 0x65 && code[end + 2] == 0xf4))
            end++;
        for (size_t i = 0; i + 3 < end && wait == SIZE_MAX; i++)
            if (code[i] == 0xc6 && (code[i + 1] & 0xc0) == 0x40 && code[i + 3] == 0)
                wait = i;
        if (end + 3 < sizeof(reordered) && wait != SIZE_MAX)
        {
            char hex[sizeof(reordered) * 2 + 1];
            for (size_t i = 0; i < end; i++)
                if (i < wait || i >= wait + 4)
                    reordered[n++] = code[i];
            memcpy(reordered + n, code + wait, 4);
            for (size_t i = 0; i < end; i++)
                snprintf(hex + i * 2, 3, "%02x", reordered[i]);
            patchMemoryFromString(game->a36ReplyPublish, hex);
        }
        else
            say("dispatchAction's reply at %08x not recognised, left as is", game->a36ReplyPublish);
    }
    for (int i = 0; i < 3; i++)
        if (game->a36Loader[i])
            detourFunction(game->a36Loader[i], (void *)loaderDone);
    if (game->a36Loader[3])
        detourFunction(game->a36Loader[3], (void *)loaderLoad);
    else
        a36Start(romPath, romIdle[0], romIdle[1]);
}
