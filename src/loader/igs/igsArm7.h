#ifndef IGS_ARM7_H
#define IGS_ARM7_H

#include <stdint.h>

// An ARM7TDMI (ARMv4T: ARM and Thumb, no coprocessor), interpreted: the
// IGS027A's core (igsA27.c). With v5 set, the IGS036's ARM946E-S
// (ARMv5TE, igsA36.c): BLX, CLZ, LDRD/STRD, the DSP multiplies, interworking
// loads of the PC and CP15 (its registers kept, the MPU and caches not
// emulated). Its memory is the board's: pages of plain memory (fast) and the
// rest through the board's handlers.

#define ARM7_PAGE_SHIFT 20
#define ARM7_PAGES (1u << (32 - ARM7_PAGE_SHIFT))

typedef struct Arm7 Arm7;

struct Arm7
{
    uint32_t r[16];
    uint32_t cpsr, spsr;
    // Banked registers: r13, r14 and SPSR per mode (user/system, FIQ, IRQ,
    // supervisor, abort, undefined), r8..r12 of FIQ and of the others.
    uint32_t bankR13[6], bankR14[6], bankSpsr[6];
    uint32_t fiqR8[5], usrR8[5];

    // Plain memory: a 1 MiB page's base (NULL: the handlers), and its mask
    // (the memory's size - 1: smaller memories repeat over the page).
    uint8_t *page[ARM7_PAGES];
    uint32_t pageMask[ARM7_PAGES];
    // Pages read-only (ROM): writes ignored.
    uint8_t pageReadOnly[ARM7_PAGES];

    uint32_t (*read)(Arm7 *cpu, uint32_t address, int size);
    void (*write)(Arm7 *cpu, uint32_t address, uint32_t value, int size);
    void *board;
    // Optional missing mask-ROM service implementation. Called only for
    // instruction addresses inside the 16 KiB internal ROM.
    void (*service)(Arm7 *cpu, uint32_t address);

    int v5;             // ARMv5TE (the ARM946E-S)
    uint32_t cp15[16];  // CP15's registers by CRn (opcode2 0), v5
    int irq;            // the IRQ line, level
    uint32_t irqVector; // where an IRQ goes (the internal ROM's handler)
    int fiq;            // the FIQ line, level (to vector 0x1c)
    int halted;         // an exception nothing handles: stopped
    uint64_t cycles;    // instructions run, a cycle each
    uint32_t breakpoints[8]; // debugging: the registers shown there (0: none)
};

void arm7Reset(Arm7 *cpu, uint32_t pc, uint32_t cpsr);
// Maps size bytes of memory (a power of two up to 1 MiB, or a multiple of
// 1 MiB) at address.
void arm7Map(Arm7 *cpu, uint32_t address, uint32_t size, uint8_t *memory, int readOnly);
// Sets the stack pointer of a mode (its CPSR mode bits).
void arm7SetStack(Arm7 *cpu, uint32_t mode, uint32_t sp);
// Runs until cycles reaches end (or the CPU halts).
void arm7Run(Arm7 *cpu, uint64_t end);

uint32_t arm7Read32(Arm7 *cpu, uint32_t address);

#endif // IGS_ARM7_H
