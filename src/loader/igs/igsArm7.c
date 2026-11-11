// The IGS027A's ARM7TDMI core, interpreted (see igsArm7.h). ARMv4T: the
// ARM and Thumb instruction sets, without coprocessors (with v5, ARMv5TE's
// additions and CP15, MAME's arm7ops.cpp the reference); a cycle per
// instruction. An exception other than the IRQ (undefined instruction, SWI,
// aborts: the firmware's error paths only) halts the CPU.

#include <stdio.h>
#include <string.h>

#include "igsArm7.h"
#include "../log/log.h"

#define FLAG_N 0x80000000u
#define FLAG_Z 0x40000000u
#define FLAG_C 0x20000000u
#define FLAG_V 0x10000000u
#define FLAG_Q 0x08000000u
#define FLAG_I 0x80u
#define FLAG_F 0x40u
#define FLAG_T 0x20u

#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SYS 0x1f

static int modeIndex(uint32_t mode)
{
    switch (mode & 0x1f)
    {
    case 0x11:
        return 1;
    case 0x12:
        return 2;
    case 0x13:
        return 3;
    case 0x17:
        return 4;
    case 0x1b:
        return 5;
    default:
        return 0; // user, system
    }
}

static void switchMode(Arm7 *c, uint32_t mode)
{
    int from = modeIndex(c->cpsr), to = modeIndex(mode);

    if (from != to)
    {
        c->bankR13[from] = c->r[13];
        c->bankR14[from] = c->r[14];
        c->bankSpsr[from] = c->spsr;
        if ((from == 1) != (to == 1))
        {
            if (from == 1)
            {
                memcpy(c->fiqR8, &c->r[8], sizeof(c->fiqR8));
                memcpy(&c->r[8], c->usrR8, sizeof(c->usrR8));
            }
            else
            {
                memcpy(c->usrR8, &c->r[8], sizeof(c->usrR8));
                memcpy(&c->r[8], c->fiqR8, sizeof(c->fiqR8));
            }
        }
        c->r[13] = c->bankR13[to];
        c->r[14] = c->bankR14[to];
        c->spsr = c->bankSpsr[to];
    }
    c->cpsr = (c->cpsr & ~0x1fu) | (mode & 0x1f);
}

static void setCpsr(Arm7 *c, uint32_t value)
{
    if ((value & 0x1f) != (c->cpsr & 0x1f))
        switchMode(c, value);
    c->cpsr = value;
}

void arm7Reset(Arm7 *cpu, uint32_t pc, uint32_t cpsr)
{
    memset(cpu->r, 0, sizeof(cpu->r));
    memset(cpu->bankR13, 0, sizeof(cpu->bankR13));
    memset(cpu->bankR14, 0, sizeof(cpu->bankR14));
    memset(cpu->bankSpsr, 0, sizeof(cpu->bankSpsr));
    cpu->cpsr = MODE_USR;
    cpu->spsr = 0;
    switchMode(cpu, cpsr);
    cpu->cpsr = cpsr & ~FLAG_T;
    if (pc & 1)
        cpu->cpsr |= FLAG_T;
    cpu->r[15] = pc & ~1u;
    cpu->halted = 0;
}

void arm7SetStack(Arm7 *cpu, uint32_t mode, uint32_t sp)
{
    if (modeIndex(mode) == modeIndex(cpu->cpsr))
        cpu->r[13] = sp;
    else
        cpu->bankR13[modeIndex(mode)] = sp;
}

void arm7Map(Arm7 *cpu, uint32_t address, uint32_t size, uint8_t *memory, int readOnly)
{
    uint32_t pageSize = 1u << ARM7_PAGE_SHIFT;

    if (size <= pageSize)
    {
        cpu->page[address >> ARM7_PAGE_SHIFT] = memory;
        cpu->pageMask[address >> ARM7_PAGE_SHIFT] = size - 1;
        cpu->pageReadOnly[address >> ARM7_PAGE_SHIFT] = (uint8_t)readOnly;
        return;
    }
    for (uint32_t offset = 0; offset < size; offset += pageSize)
    {
        cpu->page[(address + offset) >> ARM7_PAGE_SHIFT] = memory + offset;
        cpu->pageMask[(address + offset) >> ARM7_PAGE_SHIFT] = pageSize - 1;
        cpu->pageReadOnly[(address + offset) >> ARM7_PAGE_SHIFT] = (uint8_t)readOnly;
    }
}

// --- memory ---

static inline uint32_t read8(Arm7 *c, uint32_t a)
{
    uint8_t *p = c->page[a >> ARM7_PAGE_SHIFT];

    if (p)
        return p[a & c->pageMask[a >> ARM7_PAGE_SHIFT]];
    return c->read(c, a, 1) & 0xff;
}

static inline uint32_t read16(Arm7 *c, uint32_t a)
{
    uint8_t *p = c->page[a >> ARM7_PAGE_SHIFT];

    a &= ~1u;
    if (p)
    {
        uint16_t v;
        memcpy(&v, p + (a & c->pageMask[a >> ARM7_PAGE_SHIFT]), 2);
        return v;
    }
    return c->read(c, a, 2) & 0xffff;
}

static inline uint32_t read32(Arm7 *c, uint32_t a)
{
    uint8_t *p = c->page[a >> ARM7_PAGE_SHIFT];

    a &= ~3u;
    if (p)
    {
        uint32_t v;
        memcpy(&v, p + (a & c->pageMask[a >> ARM7_PAGE_SHIFT]), 4);
        return v;
    }
    return c->read(c, a, 4);
}

uint32_t arm7Read32(Arm7 *cpu, uint32_t address)
{
    return read32(cpu, address);
}

static inline void write8(Arm7 *c, uint32_t a, uint32_t v)
{
    uint8_t *p = c->page[a >> ARM7_PAGE_SHIFT];

    if (p)
    {
        if (!c->pageReadOnly[a >> ARM7_PAGE_SHIFT])
            p[a & c->pageMask[a >> ARM7_PAGE_SHIFT]] = (uint8_t)v;
        return;
    }
    c->write(c, a, v & 0xff, 1);
}

static inline void write16(Arm7 *c, uint32_t a, uint32_t v)
{
    uint8_t *p = c->page[a >> ARM7_PAGE_SHIFT];

    a &= ~1u;
    if (p)
    {
        uint16_t h = (uint16_t)v;
        if (!c->pageReadOnly[a >> ARM7_PAGE_SHIFT])
            memcpy(p + (a & c->pageMask[a >> ARM7_PAGE_SHIFT]), &h, 2);
        return;
    }
    c->write(c, a, v & 0xffff, 2);
}

static inline void write32(Arm7 *c, uint32_t a, uint32_t v)
{
    uint8_t *p = c->page[a >> ARM7_PAGE_SHIFT];

    a &= ~3u;
    if (p)
    {
        if (!c->pageReadOnly[a >> ARM7_PAGE_SHIFT])
            memcpy(p + (a & c->pageMask[a >> ARM7_PAGE_SHIFT]), &v, 4);
        return;
    }
    c->write(c, a, v, 4);
}

// LDR of an unaligned address: the word rotated.
static inline uint32_t readRotated(Arm7 *c, uint32_t a)
{
    uint32_t v = read32(c, a), s = (a & 3) * 8;

    return s ? (v >> s) | (v << (32 - s)) : v;
}

// --- flags ---

static inline void setNZ(Arm7 *c, uint32_t v)
{
    c->cpsr = (c->cpsr & ~(FLAG_N | FLAG_Z)) | (v & FLAG_N) | (v ? 0 : FLAG_Z);
}

static inline void setC(Arm7 *c, int carry)
{
    c->cpsr = carry ? c->cpsr | FLAG_C : c->cpsr & ~FLAG_C;
}

static inline uint32_t addFlags(Arm7 *c, uint32_t a, uint32_t b, uint32_t carry)
{
    uint64_t wide = (uint64_t)a + b + carry;
    uint32_t r = (uint32_t)wide;

    setNZ(c, r);
    setC(c, wide >> 32 != 0);
    c->cpsr = ((~(a ^ b) & (a ^ r)) >> 31) ? c->cpsr | FLAG_V : c->cpsr & ~FLAG_V;
    return r;
}

// a - b - !carry, as a + ~b + carry.
static inline uint32_t subFlags(Arm7 *c, uint32_t a, uint32_t b, uint32_t carry)
{
    return addFlags(c, a, ~b, carry);
}

static int condition(Arm7 *c, uint32_t cond)
{
    uint32_t f = c->cpsr;
    int n = !!(f & FLAG_N), z = !!(f & FLAG_Z), cy = !!(f & FLAG_C), v = !!(f & FLAG_V);

    switch (cond)
    {
    case 0x0:
        return z;
    case 0x1:
        return !z;
    case 0x2:
        return cy;
    case 0x3:
        return !cy;
    case 0x4:
        return n;
    case 0x5:
        return !n;
    case 0x6:
        return v;
    case 0x7:
        return !v;
    case 0x8:
        return cy && !z;
    case 0x9:
        return !cy || z;
    case 0xa:
        return n == v;
    case 0xb:
        return n != v;
    case 0xc:
        return !z && n == v;
    case 0xd:
        return z || n != v;
    case 0xe:
        return 1;
    default:
        return 0;
    }
}

// --- barrel shifter: the shifted value, and its carry out in *carry ---

static uint32_t shift(uint32_t type, uint32_t v, uint32_t amount, int byRegister, int *carry)
{
    switch (type)
    {
    case 0: // LSL
        if (amount == 0)
            return v;
        if (amount < 32)
        {
            *carry = (v >> (32 - amount)) & 1;
            return v << amount;
        }
        *carry = amount == 32 ? (int)(v & 1) : 0;
        return 0;
    case 1: // LSR (#0: #32)
        if (amount == 0)
        {
            if (byRegister)
                return v;
            amount = 32;
        }
        if (amount < 32)
        {
            *carry = (v >> (amount - 1)) & 1;
            return v >> amount;
        }
        *carry = amount == 32 ? (int)(v >> 31) : 0;
        return 0;
    case 2: // ASR (#0: #32)
        if (amount == 0)
        {
            if (byRegister)
                return v;
            amount = 32;
        }
        if (amount < 32)
        {
            *carry = (v >> (amount - 1)) & 1;
            return (uint32_t)((int32_t)v >> amount);
        }
        *carry = (int)(v >> 31);
        return (int32_t)v < 0 ? 0xffffffffu : 0;
    default: // ROR (#0: RRX)
        if (amount == 0)
        {
            if (byRegister)
                return v;
            {
                uint32_t r = (v >> 1) | ((uint32_t)*carry << 31);
                *carry = v & 1;
                return r;
            }
        }
        amount &= 31;
        if (amount == 0)
        {
            *carry = (int)(v >> 31);
            return v;
        }
        *carry = (v >> (amount - 1)) & 1;
        return (v >> amount) | (v << (32 - amount));
    }
}

// --- exceptions ---

static void enterIrq(Arm7 *c, uint32_t returnAddress)
{
    uint32_t old = c->cpsr;

    switchMode(c, MODE_IRQ);
    c->spsr = old;
    c->r[14] = returnAddress + 4;
    c->cpsr = (c->cpsr & ~FLAG_T) | FLAG_I;
    c->r[15] = c->irqVector;
}

static void enterFiq(Arm7 *c, uint32_t returnAddress)
{
    uint32_t old = c->cpsr;

    switchMode(c, MODE_FIQ);
    c->spsr = old;
    c->r[14] = returnAddress + 4;
    c->cpsr = (c->cpsr & ~FLAG_T) | FLAG_I | FLAG_F;
    c->r[15] = 0x1c;
}

static void unhandled(Arm7 *c, const char *what, uint32_t pc, uint32_t instruction)
{
    log_error("IGS ARM: %s at %08x (%08x), CPU stopped", what, pc, instruction);
    c->halted = 1;
}

// --- ARMv5TE ---

static uint32_t saturate(Arm7 *c, int64_t v)
{
    if (v > INT32_MAX)
    {
        c->cpsr |= FLAG_Q;
        return INT32_MAX;
    }
    if (v < INT32_MIN)
    {
        c->cpsr |= FLAG_Q;
        return (uint32_t)INT32_MIN;
    }
    return (uint32_t)(int32_t)v;
}

// A load to the PC: ARMv5 changes state on its bit 0, ARMv4T does not.
static void loadPc(Arm7 *c, uint32_t v)
{
    if (c->v5 && (v & 1))
    {
        c->cpsr |= FLAG_T;
        c->r[15] = v & ~1u;
    }
    else if (c->v5)
    {
        c->cpsr &= ~FLAG_T;
        c->r[15] = v & ~3u;
    }
    else
        c->r[15] = v & ~3u;
}

// ARMv5TE's instructions among ARMv4T's undefined and unconditional ones:
// 1, done.
static int executeV5(Arm7 *c, uint32_t pc, uint32_t op)
{
    uint32_t rd = (op >> 12) & 15, rn = (op >> 16) & 15, rs = (op >> 8) & 15, rm = op & 15;

    if ((op >> 28) == 0xf)
    {
        if ((op & 0x0e000000) == 0x0a000000)
        {
            // BLX immediate: to Thumb, H the halfword
            int32_t offset = ((int32_t)(op << 8) >> 6) | (int32_t)((op >> 23) & 2);
            c->r[14] = pc + 4;
            c->cpsr |= FLAG_T;
            c->r[15] = pc + 8 + (uint32_t)offset;
            return 1;
        }
        if ((op & 0x0d70f000) == 0x0550f000)
            return 1; // PLD
        return 0;
    }
    if (!condition(c, op >> 28))
        return 1;
    if ((op & 0x0ffffff0) == 0x012fff30)
    {
        // BLX register
        uint32_t target = c->r[rm];
        c->r[14] = pc + 4;
        if (target & 1)
            c->cpsr |= FLAG_T;
        else
            c->cpsr &= ~FLAG_T;
        c->r[15] = target & ~1u;
        return 1;
    }
    if ((op & 0x0fff0ff0) == 0x016f0f10)
    {
        // CLZ
        uint32_t v = c->r[rm];
        c->r[rd] = v ? (uint32_t)__builtin_clz(v) : 32;
        return 1;
    }
    if ((op & 0x0f9000f0) == 0x01000050)
    {
        // QADD, QSUB, QDADD, QDSUB
        int64_t b = (int32_t)c->r[rn];
        if (op & (1 << 22))
            b = (int32_t)saturate(c, b * 2);
        c->r[rd] = saturate(c, (op & (1 << 21)) ? (int64_t)(int32_t)c->r[rm] - b : (int64_t)(int32_t)c->r[rm] + b);
        return 1;
    }
    if ((op & 0x0f900090) == 0x01000080)
    {
        // SMLAxy, SMLAWy, SMULWy, SMLALxy, SMULxy (rd at 16, rn at 12)
        int32_t x = (op & 0x20) ? (int32_t)c->r[rm] >> 16 : (int16_t)c->r[rm];
        int32_t y = (op & 0x40) ? (int32_t)c->r[rs] >> 16 : (int16_t)c->r[rs];
        switch ((op >> 21) & 3)
        {
        case 0: // SMLAxy
            {
                int64_t r = (int64_t)(x * y) + (int32_t)c->r[rd];
                if (r != (int32_t)r)
                    c->cpsr |= FLAG_Q;
                c->r[rn] = (uint32_t)r;
            }
            break;
        case 1: // SMLAWy, SMULWy
        {
            int64_t r = ((int64_t)(int32_t)c->r[rm] * y) >> 16;
            if (!(op & 0x20))
            {
                int64_t sum = r + (int32_t)c->r[rd];
                if (sum != (int32_t)sum)
                    c->cpsr |= FLAG_Q;
                r = sum;
            }
            c->r[rn] = (uint32_t)r;
            break;
        }
        case 2: // SMLALxy
        {
            int64_t r = (int64_t)(((uint64_t)c->r[rn] << 32) | c->r[rd]) + (int64_t)x * y;
            c->r[rd] = (uint32_t)r;
            c->r[rn] = (uint32_t)((uint64_t)r >> 32);
            break;
        }
        default: // SMULxy
            c->r[rn] = (uint32_t)(x * y);
            break;
        }
        return 1;
    }
    if ((op & 0x0e1000d0) == 0x000000d0)
    {
        // LDRD, STRD (rd even, its pair rd + 1)
        uint32_t offset = (op & (1 << 22)) ? ((op >> 4) & 0xf0) | (op & 15) : c->r[rm];
        uint32_t base = rn == 15 ? pc + 8 : c->r[rn], address;
        int pre = !!(op & (1 << 24)), up = !!(op & (1 << 23)), writeBack = !!(op & (1 << 21));
        uint32_t final = up ? base + offset : base - offset;

        address = pre ? final : base;
        if (op & 0x20)
        {
            write32(c, address, c->r[rd]);
            write32(c, address + 4, c->r[rd + 1]);
            if (!pre || writeBack)
                c->r[rn] = final;
        }
        else
        {
            uint32_t lo = read32(c, address), hi = read32(c, address + 4);
            if (!pre || writeBack)
                c->r[rn] = final;
            c->r[rd] = lo;
            c->r[rd + 1] = hi;
        }
        return 1;
    }
    if ((op & 0x0f000f10) == 0x0e000f10)
    {
        // MRC, MCR p15 (CRn's register, opcode2 0)
        uint32_t crn = (op >> 16) & 15;
        if (op & (1 << 20))
        {
            uint32_t v = c->cp15[crn];
            if (rd == 15)
                c->cpsr = (c->cpsr & 0x0fffffff) | (v & 0xf0000000);
            else
                c->r[rd] = v;
        }
        else if (crn != 0)
            c->cp15[crn] = rd == 15 ? pc + 12 : c->r[rd];
        return 1;
    }
    if ((op & 0x0f000010) == 0x0e000000)
        return 1; // CDP (no coprocessor work)
    return 0;
}

// --- ARM ---

// The register r as an operand: the PC reads 8 ahead.
#define REG(n) ((n) == 15 ? pc + 8 : c->r[n])

static void executeArm(Arm7 *c, uint32_t pc, uint32_t op)
{
    uint32_t next = pc + 4;

    c->r[15] = next;
    if (c->v5 && executeV5(c, pc, op))
        return;
    if (!condition(c, op >> 28))
        return;

    switch ((op >> 25) & 7)
    {
    case 0:
    case 1:
    {
        // BX
        if ((op & 0x0ffffff0) == 0x012fff10)
        {
            uint32_t target = REG(op & 15);
            if (target & 1)
                c->cpsr |= FLAG_T;
            else
                c->cpsr &= ~FLAG_T;
            c->r[15] = target & ~1u;
            return;
        }
        if (!(op & 0x02000000) && (op & 0x90) == 0x90)
        {
            uint32_t rn = (op >> 16) & 15, rd = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;

            if ((op & 0x60) == 0)
            {
                if ((op & 0x0fc000f0) == 0x00000090)
                {
                    // MUL, MLA (rd at 16, rn at 12)
                    uint32_t r = c->r[rm] * c->r[rs];
                    if (op & (1 << 21))
                        r += c->r[rd];
                    c->r[rn] = r;
                    if (op & (1 << 20))
                        setNZ(c, r);
                    return;
                }
                if ((op & 0x0f8000f0) == 0x00800090)
                {
                    // UMULL, UMLAL, SMULL, SMLAL (hi at 16, lo at 12)
                    uint64_t r;
                    if (op & (1 << 22))
                        r = (uint64_t)((int64_t)(int32_t)c->r[rm] * (int32_t)c->r[rs]);
                    else
                        r = (uint64_t)c->r[rm] * c->r[rs];
                    if (op & (1 << 21))
                        r += ((uint64_t)c->r[rn] << 32) | c->r[rd];
                    c->r[rd] = (uint32_t)r;
                    c->r[rn] = (uint32_t)(r >> 32);
                    if (op & (1 << 20))
                        c->cpsr = (c->cpsr & ~(FLAG_N | FLAG_Z)) | ((uint32_t)(r >> 32) & FLAG_N) | (r ? 0 : FLAG_Z);
                    return;
                }
                if ((op & 0x0fb00ff0) == 0x01000090)
                {
                    // SWP, SWPB
                    uint32_t address = c->r[rn];
                    if (op & (1 << 22))
                    {
                        uint32_t v = read8(c, address);
                        write8(c, address, c->r[rm]);
                        c->r[rd] = v;
                    }
                    else
                    {
                        uint32_t v = readRotated(c, address);
                        write32(c, address, c->r[rm]);
                        c->r[rd] = v;
                    }
                    return;
                }
                unhandled(c, "undefined instruction", pc, op);
                return;
            }
            // LDRH, STRH, LDRSB, LDRSH
            {
                uint32_t offset = (op & (1 << 22)) ? ((op >> 4) & 0xf0) | (op & 15) : c->r[rm];
                uint32_t base = REG(rn), address;
                int pre = !!(op & (1 << 24)), up = !!(op & (1 << 23)), writeBack = !!(op & (1 << 21));
                int load = !!(op & (1 << 20));

                address = pre ? (up ? base + offset : base - offset) : base;
                if (load)
                {
                    uint32_t v;
                    switch ((op >> 5) & 3)
                    {
                    case 1:
                        v = read16(c, address);
                        break;
                    case 2:
                        v = (uint32_t)(int32_t)(int8_t)read8(c, address);
                        break;
                    default:
                        v = (uint32_t)(int32_t)(int16_t)read16(c, address);
                        break;
                    }
                    if (!pre || writeBack)
                        c->r[rn] = pre ? address : (up ? base + offset : base - offset);
                    if (rd == 15)
                        c->r[15] = v & ~3u;
                    else
                        c->r[rd] = v;
                }
                else
                {
                    write16(c, address, rd == 15 ? pc + 12 : c->r[rd]);
                    if (!pre || writeBack)
                        c->r[rn] = pre ? address : (up ? base + offset : base - offset);
                }
                return;
            }
        }
        // MRS, MSR
        if ((op & 0x0fbf0fff) == 0x010f0000)
        {
            c->r[(op >> 12) & 15] = (op & (1 << 22)) ? c->spsr : c->cpsr;
            return;
        }
        if ((op & 0x0db0f000) == 0x0120f000)
        {
            uint32_t value, mask = 0;
            if (op & 0x02000000)
            {
                uint32_t imm = op & 0xff, rot = ((op >> 8) & 15) * 2;
                value = rot ? (imm >> rot) | (imm << (32 - rot)) : imm;
            }
            else
                value = c->r[op & 15];
            if (op & (1 << 16))
                mask |= 0xff;
            if (op & (1 << 19))
                mask |= 0xff000000;
            if (op & (1 << 22))
                c->spsr = (c->spsr & ~mask) | (value & mask);
            else
            {
                if ((c->cpsr & 0x1f) == MODE_USR)
                    mask &= 0xff000000;
                setCpsr(c, (c->cpsr & ~mask) | (value & mask));
            }
            return;
        }
        // data processing
        {
            uint32_t opcode = (op >> 21) & 15, rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            int setFlags = !!(op & (1 << 20));
            int carry = !!(c->cpsr & FLAG_C);
            uint32_t operand, a, r = 0;

            if (op & 0x02000000)
            {
                uint32_t imm = op & 0xff, rot = ((op >> 8) & 15) * 2;
                operand = rot ? (imm >> rot) | (imm << (32 - rot)) : imm;
                if (rot)
                    carry = (int)(operand >> 31);
                a = REG(rn);
            }
            else if (op & 0x10)
            {
                // shift by register: the PC reads 12 ahead
                uint32_t rm = op & 15, rs = (op >> 8) & 15;
                uint32_t v = rm == 15 ? pc + 12 : c->r[rm];
                operand = shift((op >> 5) & 3, v, c->r[rs] & 0xff, 1, &carry);
                a = rn == 15 ? pc + 12 : c->r[rn];
            }
            else
            {
                operand = shift((op >> 5) & 3, REG(op & 15), (op >> 7) & 31, 0, &carry);
                a = REG(rn);
            }

            switch (opcode)
            {
            case 0x0: // AND
            case 0x8: // TST
                r = a & operand;
                break;
            case 0x1: // EOR
            case 0x9: // TEQ
                r = a ^ operand;
                break;
            case 0x2: // SUB
            case 0xa: // CMP
                r = setFlags ? subFlags(c, a, operand, 1) : a - operand;
                break;
            case 0x3: // RSB
                r = setFlags ? subFlags(c, operand, a, 1) : operand - a;
                break;
            case 0x4: // ADD
            case 0xb: // CMN
                r = setFlags ? addFlags(c, a, operand, 0) : a + operand;
                break;
            case 0x5: // ADC
                r = setFlags ? addFlags(c, a, operand, !!(c->cpsr & FLAG_C)) : a + operand + !!(c->cpsr & FLAG_C);
                break;
            case 0x6: // SBC
                r = setFlags ? subFlags(c, a, operand, !!(c->cpsr & FLAG_C)) : a - operand - !(c->cpsr & FLAG_C);
                break;
            case 0x7: // RSC
                r = setFlags ? subFlags(c, operand, a, !!(c->cpsr & FLAG_C)) : operand - a - !(c->cpsr & FLAG_C);
                break;
            case 0xc: // ORR
                r = a | operand;
                break;
            case 0xd: // MOV
                r = operand;
                break;
            case 0xe: // BIC
                r = a & ~operand;
                break;
            case 0xf: // MVN
                r = ~operand;
                break;
            }
            if (setFlags)
            {
                switch (opcode)
                {
                case 0x0:
                case 0x1:
                case 0x8:
                case 0x9:
                case 0xc:
                case 0xd:
                case 0xe:
                case 0xf:
                    setNZ(c, r);
                    setC(c, carry);
                    break;
                }
            }
            if (opcode >= 0x8 && opcode <= 0xb)
                return; // TST, TEQ, CMP, CMN: no result
            if (rd == 15)
            {
                if (setFlags)
                {
                    // the exception's return: CPSR from SPSR
                    setCpsr(c, c->spsr);
                    c->r[15] = r & ((c->cpsr & FLAG_T) ? ~1u : ~3u);
                }
                else
                    c->r[15] = r & ~3u;
            }
            else
                c->r[rd] = r;
            return;
        }
    }
    case 2:
    case 3:
    {
        // LDR, STR, LDRB, STRB
        uint32_t rn = (op >> 16) & 15, rd = (op >> 12) & 15, offset, base = REG(rn), address;
        int pre = !!(op & (1 << 24)), up = !!(op & (1 << 23)), byte = !!(op & (1 << 22));
        int writeBack = !!(op & (1 << 21)), load = !!(op & (1 << 20));

        if (op & 0x02000000)
        {
            int carry = !!(c->cpsr & FLAG_C);
            if (op & 0x10)
            {
                unhandled(c, "undefined instruction", pc, op);
                return;
            }
            offset = shift((op >> 5) & 3, REG(op & 15), (op >> 7) & 31, 0, &carry);
        }
        else
            offset = op & 0xfff;
        address = pre ? (up ? base + offset : base - offset) : base;
        if (load)
        {
            uint32_t v = byte ? read8(c, address) : readRotated(c, address);
            if (!pre || writeBack)
                c->r[rn] = pre ? address : (up ? base + offset : base - offset);
            if (rd == 15)
                loadPc(c, v); // ARMv4T: no interworking on LDR PC
            else
                c->r[rd] = v;
        }
        else
        {
            uint32_t v = rd == 15 ? pc + 12 : c->r[rd];
            if (byte)
                write8(c, address, v);
            else
                write32(c, address, v);
            if (!pre || writeBack)
                c->r[rn] = pre ? address : (up ? base + offset : base - offset);
        }
        return;
    }
    case 4:
    {
        // LDM, STM
        uint32_t rn = (op >> 16) & 15, list = op & 0xffff, count = 0, base = c->r[rn], address, end;
        int pre = !!(op & (1 << 24)), up = !!(op & (1 << 23)), psr = !!(op & (1 << 22));
        int writeBack = !!(op & (1 << 21)), load = !!(op & (1 << 20));
        int userBank = psr && !(load && (list & 0x8000));
        uint32_t savedMode = c->cpsr & 0x1f;

        for (int i = 0; i < 16; i++)
            count += (list >> i) & 1;
        if (up)
        {
            address = pre ? base + 4 : base;
            end = base + count * 4;
        }
        else
        {
            address = pre ? base - count * 4 : base - count * 4 + 4;
            end = base - count * 4;
        }
        if (userBank)
            switchMode(c, MODE_USR);
        if (load)
        {
            if (writeBack && !(list & (1u << rn)))
                c->r[rn] = end;
            for (int i = 0; i < 16; i++)
                if (list & (1u << i))
                {
                    uint32_t v = read32(c, address);
                    address += 4;
                    if (i == 15)
                    {
                        if (psr)
                        {
                            setCpsr(c, c->spsr);
                            c->r[15] = v & ((c->cpsr & FLAG_T) ? ~1u : ~3u);
                        }
                        else
                            loadPc(c, v);
                    }
                    else
                        c->r[i] = v;
                }
        }
        else
        {
            for (int i = 0; i < 16; i++)
                if (list & (1u << i))
                {
                    write32(c, address, i == 15 ? pc + 12 : c->r[i]);
                    address += 4;
                }
            if (writeBack)
                c->r[rn] = end;
        }
        if (userBank)
            switchMode(c, savedMode);
        return;
    }
    case 5:
    {
        // B, BL
        int32_t offset = (int32_t)(op << 8) >> 6;
        if (op & (1 << 24))
            c->r[14] = next;
        c->r[15] = pc + 8 + (uint32_t)offset;
        return;
    }
    case 7:
        if ((op & 0x0f000000) == 0x0f000000)
        {
            unhandled(c, "SWI", pc, op);
            return;
        }
        // fall through
    default:
        unhandled(c, "coprocessor instruction", pc, op);
        return;
    }
}

#undef REG

// --- Thumb ---

static void executeThumb(Arm7 *c, uint32_t pc, uint32_t op)
{
    uint32_t next = pc + 2;

    c->r[15] = next;
    switch (op >> 13)
    {
    case 0:
        if ((op & 0x1800) == 0x1800)
        {
            // ADD, SUB: register or 3-bit immediate
            uint32_t rd = op & 7, a = c->r[(op >> 3) & 7];
            uint32_t b = (op & 0x400) ? (op >> 6) & 7 : c->r[(op >> 6) & 7];
            c->r[rd] = (op & 0x200) ? subFlags(c, a, b, 1) : addFlags(c, a, b, 0);
        }
        else
        {
            // LSL, LSR, ASR by immediate
            int carry = !!(c->cpsr & FLAG_C);
            uint32_t r = shift((op >> 11) & 3, c->r[(op >> 3) & 7], (op >> 6) & 31, 0, &carry);
            c->r[op & 7] = r;
            setNZ(c, r);
            setC(c, carry);
        }
        return;
    case 1:
    {
        // MOV, CMP, ADD, SUB with an 8-bit immediate
        uint32_t rd = (op >> 8) & 7, imm = op & 0xff;
        switch ((op >> 11) & 3)
        {
        case 0:
            c->r[rd] = imm;
            setNZ(c, imm);
            break;
        case 1:
            subFlags(c, c->r[rd], imm, 1);
            break;
        case 2:
            c->r[rd] = addFlags(c, c->r[rd], imm, 0);
            break;
        default:
            c->r[rd] = subFlags(c, c->r[rd], imm, 1);
            break;
        }
        return;
    }
    case 2:
        if ((op & 0xfc00) == 0x4000)
        {
            // ALU operations
            uint32_t rd = op & 7, rs = (op >> 3) & 7, a = c->r[rd], b = c->r[rs], r;
            int carry = !!(c->cpsr & FLAG_C);
            switch ((op >> 6) & 15)
            {
            case 0x0: // AND
                r = c->r[rd] = a & b;
                setNZ(c, r);
                break;
            case 0x1: // EOR
                r = c->r[rd] = a ^ b;
                setNZ(c, r);
                break;
            case 0x2: // LSL
            case 0x3: // LSR
            case 0x4: // ASR
            case 0x7: // ROR
            {
                static const uint32_t types[8] = {0, 0, 0, 1, 2, 0, 0, 3};
                r = c->r[rd] = shift(types[(op >> 6) & 7], a, b & 0xff, 1, &carry);
                setNZ(c, r);
                setC(c, carry);
                break;
            }
            case 0x5: // ADC
                c->r[rd] = addFlags(c, a, b, !!(c->cpsr & FLAG_C));
                break;
            case 0x6: // SBC
                c->r[rd] = subFlags(c, a, b, !!(c->cpsr & FLAG_C));
                break;
            case 0x8: // TST
                setNZ(c, a & b);
                break;
            case 0x9: // NEG
                c->r[rd] = subFlags(c, 0, b, 1);
                break;
            case 0xa: // CMP
                subFlags(c, a, b, 1);
                break;
            case 0xb: // CMN
                addFlags(c, a, b, 0);
                break;
            case 0xc: // ORR
                r = c->r[rd] = a | b;
                setNZ(c, r);
                break;
            case 0xd: // MUL
                r = c->r[rd] = a * b;
                setNZ(c, r);
                break;
            case 0xe: // BIC
                r = c->r[rd] = a & ~b;
                setNZ(c, r);
                break;
            default: // MVN
                r = c->r[rd] = ~b;
                setNZ(c, r);
                break;
            }
            return;
        }
        if ((op & 0xfc00) == 0x4400)
        {
            // high registers: ADD, CMP, MOV, BX
            uint32_t rd = (op & 7) | ((op >> 4) & 8), rs = (op >> 3) & 15;
            uint32_t b = rs == 15 ? pc + 4 : c->r[rs];
            uint32_t a = rd == 15 ? pc + 4 : c->r[rd];
            switch ((op >> 8) & 3)
            {
            case 0:
                if (rd == 15)
                    c->r[15] = (a + b) & ~1u;
                else
                    c->r[rd] = a + b;
                break;
            case 1:
                subFlags(c, a, b, 1);
                break;
            case 2:
                if (rd == 15)
                    c->r[15] = b & ~1u;
                else
                    c->r[rd] = b;
                break;
            default:
                // BX; ARMv5: BLX with H1
                if (c->v5 && (op & 0x80))
                    c->r[14] = next | 1;
                if (!(b & 1))
                {
                    c->cpsr &= ~FLAG_T;
                    c->r[15] = b & ~3u;
                }
                else
                    c->r[15] = b & ~1u;
                break;
            }
            return;
        }
        if ((op & 0xf800) == 0x4800)
        {
            // LDR PC-relative
            c->r[(op >> 8) & 7] = read32(c, ((pc + 4) & ~3u) + (op & 0xff) * 4);
            return;
        }
        {
            // load/store with a register offset
            uint32_t rd = op & 7, address = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
            switch ((op >> 9) & 7)
            {
            case 0:
                write32(c, address, c->r[rd]);
                break;
            case 1:
                write16(c, address, c->r[rd]);
                break;
            case 2:
                write8(c, address, c->r[rd]);
                break;
            case 3:
                c->r[rd] = (uint32_t)(int32_t)(int8_t)read8(c, address);
                break;
            case 4:
                c->r[rd] = readRotated(c, address);
                break;
            case 5:
                c->r[rd] = read16(c, address);
                break;
            case 6:
                c->r[rd] = read8(c, address);
                break;
            default:
                c->r[rd] = (uint32_t)(int32_t)(int16_t)read16(c, address);
                break;
            }
            return;
        }
    case 3:
    {
        // LDR, STR, LDRB, STRB with a 5-bit immediate offset
        uint32_t rd = op & 7, base = c->r[(op >> 3) & 7], imm = (op >> 6) & 31;
        switch ((op >> 11) & 3)
        {
        case 0:
            write32(c, base + imm * 4, c->r[rd]);
            break;
        case 1:
            c->r[rd] = readRotated(c, base + imm * 4);
            break;
        case 2:
            write8(c, base + imm, c->r[rd]);
            break;
        default:
            c->r[rd] = read8(c, base + imm);
            break;
        }
        return;
    }
    case 4:
        if (!(op & 0x1000))
        {
            // LDRH, STRH with an immediate offset
            uint32_t rd = op & 7, address = c->r[(op >> 3) & 7] + ((op >> 6) & 31) * 2;
            if (op & 0x800)
                c->r[rd] = read16(c, address);
            else
                write16(c, address, c->r[rd]);
        }
        else
        {
            // SP-relative LDR, STR
            uint32_t rd = (op >> 8) & 7, address = c->r[13] + (op & 0xff) * 4;
            if (op & 0x800)
                c->r[rd] = readRotated(c, address);
            else
                write32(c, address, c->r[rd]);
        }
        return;
    case 5:
        if (!(op & 0x1000))
        {
            // ADD rd, PC/SP, #imm
            uint32_t rd = (op >> 8) & 7, imm = (op & 0xff) * 4;
            c->r[rd] = (op & 0x800) ? c->r[13] + imm : ((pc + 4) & ~3u) + imm;
            return;
        }
        if ((op & 0xff00) == 0xb000)
        {
            // ADD SP, #+-imm
            uint32_t imm = (op & 0x7f) * 4;
            c->r[13] = (op & 0x80) ? c->r[13] - imm : c->r[13] + imm;
            return;
        }
        if ((op & 0xf600) == 0xb400)
        {
            // PUSH, POP (with LR, PC)
            uint32_t list = op & 0xff, sp = c->r[13];
            int extra = !!(op & 0x100);
            if (op & 0x800)
            {
                for (int i = 0; i < 8; i++)
                    if (list & (1u << i))
                    {
                        c->r[i] = read32(c, sp);
                        sp += 4;
                    }
                if (extra)
                {
                    // ARMv4T: POP {pc} stays in Thumb (ARMv5: interworking)
                    uint32_t v = read32(c, sp);
                    if (c->v5 && !(v & 1))
                    {
                        c->cpsr &= ~FLAG_T;
                        c->r[15] = v & ~3u;
                    }
                    else
                        c->r[15] = v & ~1u;
                    sp += 4;
                }
            }
            else
            {
                uint32_t count = extra;
                for (int i = 0; i < 8; i++)
                    count += (list >> i) & 1;
                sp -= count * 4;
                c->r[13] = sp;
                for (int i = 0; i < 8; i++)
                    if (list & (1u << i))
                    {
                        write32(c, sp, c->r[i]);
                        sp += 4;
                    }
                if (extra)
                    write32(c, sp, c->r[14]);
                return;
            }
            c->r[13] = sp;
            return;
        }
        unhandled(c, "undefined Thumb instruction", pc, op);
        return;
    case 6:
        if (!(op & 0x1000))
        {
            // STMIA, LDMIA
            uint32_t rb = (op >> 8) & 7, list = op & 0xff, address = c->r[rb];
            for (int i = 0; i < 8; i++)
                if (list & (1u << i))
                {
                    if (op & 0x800)
                        c->r[i] = read32(c, address);
                    else
                        write32(c, address, c->r[i]);
                    address += 4;
                }
            if (!(op & 0x800) || !(list & (1u << rb)))
                c->r[rb] = address;
            return;
        }
        if ((op & 0x0f00) == 0x0f00)
        {
            unhandled(c, "SWI", pc, op);
            return;
        }
        // conditional branch
        if (condition(c, (op >> 8) & 15))
            c->r[15] = pc + 4 + (uint32_t)((int32_t)(int8_t)(op & 0xff) * 2);
        return;
    default:
        if ((op & 0xf800) == 0xe000)
        {
            // B
            c->r[15] = pc + 4 + (uint32_t)(((int32_t)(op << 21) >> 20));
            return;
        }
        if ((op & 0xf800) == 0xf000)
        {
            // BL, first half: LR = PC + offset high
            c->r[14] = pc + 4 + (uint32_t)(((int32_t)(op << 21) >> 9));
            return;
        }
        if ((op & 0xf800) == 0xe800 && c->v5)
        {
            // BLX, second half: to ARM
            uint32_t target = (c->r[14] + (op & 0x7ff) * 2) & ~3u;
            c->r[14] = next | 1;
            c->cpsr &= ~FLAG_T;
            c->r[15] = target;
            return;
        }
        if ((op & 0xf800) == 0xf800)
        {
            uint32_t target = c->r[14] + (op & 0x7ff) * 2;
            c->r[14] = next | 1;
            c->r[15] = target & ~1u;
            return;
        }
        unhandled(c, "undefined Thumb instruction", pc, op);
        return;
    }
}

void arm7Run(Arm7 *cpu, uint64_t end)
{
    while (cpu->cycles < end && !cpu->halted)
    {
        uint32_t pc = cpu->r[15];

        if (cpu->fiq && !(cpu->cpsr & FLAG_F))
        {
            enterFiq(cpu, pc);
            continue;
        }
        if (cpu->irq && !(cpu->cpsr & FLAG_I))
        {
            enterIrq(cpu, pc);
            continue;
        }
        if (pc < 0x4000 && cpu->service)
        {
            cpu->service(cpu, pc);
            cpu->cycles++;
            continue;
        }
        if (cpu->breakpoints[0])
            for (int i = 0; i < 8 && cpu->breakpoints[i]; i++)
                if (pc == cpu->breakpoints[i])
                    printf("IGS ARM: %llu %08x r0 %08x r1 %08x r2 %08x r3 %08x r4 %08x r5 %08x r6 %08x r7 %08x sp %08x lr %08x cpsr %08x\n",
                   (unsigned long long)cpu->cycles, pc,
                   cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], cpu->r[4], cpu->r[5], cpu->r[6], cpu->r[7], cpu->r[13],
                   cpu->r[14], cpu->cpsr);
        if (cpu->cpsr & FLAG_T)
            executeThumb(cpu, pc, read16(cpu, pc));
        else
            executeArm(cpu, pc, read32(cpu, pc));
        cpu->cycles++;
    }
}
