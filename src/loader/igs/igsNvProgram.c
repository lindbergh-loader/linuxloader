// Speed Driver 3's shaders on hosts without NVIDIA's program extensions
// (see igs.h's igsNvProgramInstall).
//
// The game's Cg programs are written for the vp40 and fp40 profiles: Cg
// compiles them to ARB assembly with NVIDIA's options (NV_vertex_program3,
// NV_fragment_program2), which only NVIDIA's drivers take. The host is told
// those extensions are there (MESA_EXTENSION_OVERRIDE, set before the
// game's GL starts), Ogre and Cg pick the profiles, and each compiled
// program is rewritten on its way to Ogre (cgGetProgramString) into plain
// ARB assembly, which Mesa takes; Ogre binds and feeds it as it would have
// the original (its locals and environment untouched).
//
// The rewrite (TeknoRaven's sd3_nv_program_to_native_arb was the model):
//   - the NV options, SHORT/LONG declarations, unused labels: dropped;
//   - precision suffixes (MULR, MOVH, ...): dropped;
//   - condition codes (an instruction's C suffix): its result also copied
//     to a register of our own, which conditional writes and KILs test;
//   - absolute sources (|R0|), vertex programs' _SAT, SEQ/SNE/SGT/SLE/SFL/
//     STR/SSG, DIV, NRM, DP2: spelled out in ARB instructions;
//   - TXL: TEX (the level of detail lost), DDX/DDY: 0;
//   - vertex SIN/COS: range reduction and a sine polynomial;
//   - scalar instructions' sources: their first component (NVIDIA takes
//     a vector);
//   - the address register's four components (ARL A0.xyz): kept floored in
//     a register of our own, A0.x loaded from it before each use;
//   - vertex.weight: generic attribute 1, its NVIDIA alias (where Ogre
//     puts blend weights).
//   - fragment outputs (OUTPUT oCol = result.color): temporaries, copied
//     to the outputs at the end, as a conditional write reads its
//     destination (Power Truck's light shafts);
//   - IF/ELSE/ENDIF (Speed Rider 2's bike paint and shadows): predicated,
//     both sides run, each write inside a block merged through the block's
//     mask (sd3p, one a level), which its condition and the outer blocks'
//     make; ELSE takes the IF's (sd3q) complement.
// Programs with loops or calls (REP, LOOP, BRA, CAL...) are left as they
// are.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igs.h"
#include "../graphics/shaderPatches.h"

#define say(...) (printf("IGS NV: " __VA_ARGS__), putchar('\n'), fflush(stdout))

#define MAX_OPERANDS 4
#define MAX_BLOCKS 4

static const char *dumpDir;
static unsigned dumped;
static int trace;

typedef struct
{
    char *text;
    size_t length, size;
    int fragment;   // a fragment program (else a vertex program)
    int scratch;    // scratch registers used by this instruction
    int blocks;     // IF blocks open: sd3p<blocks - 1> masks the writes
    // fragment outputs written in temporaries (a conditional write reads
    // its destination), copied out at the end: "result.color, oCol"
    char outputs[4][80];
    int outputCount;
    int failed;
    char error[160];
} Out;

static void emit(Out *o, const char *format, ...)
{
    va_list ap;
    int n;

    for (;;)
    {
        va_start(ap, format);
        n = vsnprintf(o->text + o->length, o->size - o->length, format, ap);
        va_end(ap);
        if (n >= 0 && o->length + (size_t)n < o->size)
            break;
        o->size = o->size * 2 + (size_t)n + 256;
        o->text = realloc(o->text, o->size);
    }
    o->length += (size_t)n;
}

static void fail(Out *o, const char *format, ...)
{
    va_list ap;

    if (o->failed)
        return;
    o->failed = 1;
    va_start(ap, format);
    vsnprintf(o->error, sizeof(o->error), format, ap);
    va_end(ap);
}

// A scratch register of our own (sd3s0..sd3s7), fresh within an
// instruction.
static const char *scratch(Out *o)
{
    static char names[8][8];
    int n = o->scratch++;

    if (n >= 8)
    {
        fail(o, "too many scratch registers");
        n = 7;
    }
    snprintf(names[n], sizeof(names[n]), "sd3s%d", n);
    return names[n];
}

static char *trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == ';'))
        *--end = '\0';
    return s;
}

// Splits operands at the commas outside brackets.
static int splitOperands(char *s, char **operands)
{
    int count = 0, depth = 0;

    if (!*trim(s))
        return 0;
    operands[count++] = s;
    for (char *p = s; *p; p++)
    {
        if (*p == '[' || *p == '{' || *p == '(')
            depth++;
        else if (*p == ']' || *p == '}' || *p == ')')
            depth--;
        else if (*p == ',' && depth == 0 && count < MAX_OPERANDS)
        {
            *p = '\0';
            operands[count++] = p + 1;
        }
    }
    for (int i = 0; i < count; i++)
        operands[i] = trim(operands[i]);
    return count;
}

// A source with an absolute value (|R0.x|, -|R0|): its value moved to a
// scratch register first.
static const char *source(Out *o, const char *src)
{
    static char rewritten[MAX_OPERANDS * 2][128];
    static int next;
    const char *bar = strchr(src, '|');
    char inner[128];
    const char *s;
    int negate;

    if (!bar)
        return src;
    negate = src[0] == '-';
    snprintf(inner, sizeof(inner), "%s", bar + 1);
    if (strchr(inner, '|'))
        *strchr(inner, '|') = '\0';
    s = scratch(o);
    emit(o, "ABS %s, %s;\n", s, inner);
    next = (next + 1) % (MAX_OPERANDS * 2);
    snprintf(rewritten[next], sizeof(rewritten[next]), "%s%s", negate ? "-" : "", s);
    return rewritten[next];
}

// The register part of a destination (R0 of R0.xy), and its mask (.xy or
// "").
static void splitDestination(const char *dst, char *reg, size_t size, const char **mask)
{
    const char *dot = strrchr(dst, '.');

    // (a dot inside an array index is not a mask: result.texcoord[1].xy)
    if (dot && !strchr(dot, ']'))
    {
        snprintf(reg, size, "%.*s", (int)(dot - dst), dst);
        *mask = dot;
    }
    else
    {
        snprintf(reg, size, "%s", dst);
        *mask = "";
    }
}

// A condition (NE.x, GT, ...) on the condition code: 1 or 0 per component
// in a scratch register.
static const char *condition(Out *o, const char *cond)
{
    char rel[4] = "";
    const char *swizzle = strchr(cond, '.');
    char cc[32];
    const char *m = scratch(o), *a;

    snprintf(rel, sizeof(rel), "%.2s", cond);
    snprintf(cc, sizeof(cc), "sd3cc%s", swizzle ? swizzle : "");
    if (!strcmp(rel, "GT"))
        emit(o, "SLT %s, {0,0,0,0}, %s;\n", m, cc);
    else if (!strcmp(rel, "LT"))
        emit(o, "SLT %s, %s, {0,0,0,0};\n", m, cc);
    else if (!strcmp(rel, "GE"))
        emit(o, "SGE %s, %s, {0,0,0,0};\n", m, cc);
    else if (!strcmp(rel, "LE"))
        emit(o, "SGE %s, {0,0,0,0}, %s;\n", m, cc);
    else if (!strcmp(rel, "EQ") || !strcmp(rel, "NE"))
    {
        a = scratch(o);
        emit(o, "ABS %s, %s;\n", a, cc);
        if (!strcmp(rel, "NE"))
            emit(o, "SLT %s, {0,0,0,0}, %s;\n", m, a);
        else
            emit(o, "SGE %s, {0,0,0,0}, %s;\n", m, a);
    }
    else if (!strcmp(rel, "TR"))
        emit(o, "MOV %s, {1,1,1,1};\n", m);
    else if (!strcmp(rel, "FL"))
        emit(o, "MOV %s, {0,0,0,0};\n", m);
    else
        fail(o, "unknown condition %s", cond);
    return m;
}

// A scalar instruction's source: its first component.
static const char *scalarSource(const char *src)
{
    static char rewritten[4][128];
    static int next;
    const char *dot = strrchr(src, '.');

    next = (next + 1) % 4;
    if (dot && !strchr(dot, ']'))
        snprintf(rewritten[next], sizeof(rewritten[next]), "%.*s", (int)(dot - src) + 2, src);
    else
        snprintf(rewritten[next], sizeof(rewritten[next]), "%s.x", src);
    return rewritten[next];
}

static int isScalar(const char *name)
{
    static const char *const scalar[] = {"RCP", "RSQ", "EX2", "LG2", "POW", "EXP", "LOG", "COS", "SIN", "SCS", "ARL"};
    for (size_t i = 0; i < sizeof(scalar) / sizeof(scalar[0]); i++)
        if (!strcmp(name, scalar[i]))
            return 1;
    return 0;
}

typedef struct
{
    const char *name;
    int operands; // sources
    int arb;      // 1: an ARB instruction of both stages, 2: fragment only, 3: vertex only
} Op;

static const Op ops[] = {
    {"ABS", 1, 1}, {"ADD", 2, 1}, {"ARL", 1, 3}, {"CMP", 3, 2}, {"COS", 1, 2}, {"DP3", 2, 1}, {"DP4", 2, 1},
    {"DPH", 2, 1}, {"DST", 2, 1}, {"EX2", 1, 1}, {"EXP", 1, 3}, {"FLR", 1, 1}, {"FRC", 1, 1}, {"KIL", 1, 2},
    {"LG2", 1, 1}, {"LIT", 1, 1}, {"LOG", 1, 3}, {"LRP", 3, 2}, {"MAD", 3, 1}, {"MAX", 2, 1}, {"MIN", 2, 1},
    {"MOV", 1, 1}, {"MUL", 2, 1}, {"POW", 2, 1}, {"RCP", 1, 1}, {"RSQ", 1, 1}, {"SCS", 1, 2}, {"SGE", 2, 1},
    {"SIN", 1, 2}, {"SLT", 2, 1}, {"SUB", 2, 1}, {"SWZ", 1, 1}, {"TEX", 3, 2}, {"TXB", 3, 2}, {"TXP", 3, 2},
    {"XPD", 2, 1},
    // NVIDIA's, spelled out below
    {"SEQ", 2, 0}, {"SNE", 2, 0}, {"SGT", 2, 0}, {"SLE", 2, 0}, {"SFL", 2, 0}, {"STR", 2, 0}, {"SSG", 1, 0},
    {"DIV", 2, 0}, {"NRM", 1, 0}, {"DP2", 2, 0}, {"TXL", 3, 0}, {"DDX", 1, 0}, {"DDY", 1, 0},
};

// The opcode's base and its suffixes: precision (R, H, X), condition code
// update (C), saturation (_SAT, _SSAT).
static const Op *parseOpcode(const char *token, int *setsCc, int *saturate)
{
    const Op *best = NULL;
    size_t bestLength = 0;

    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
    {
        size_t n = strlen(ops[i].name);
        if (n > bestLength && !strncmp(token, ops[i].name, n))
        {
            const char *rest = token + n;
            if (*rest == 'R' || *rest == 'H' || *rest == 'X')
                rest++;
            if (*rest == 'C')
                rest++;
            if (!*rest || !strcmp(rest, "_SAT") || !strcmp(rest, "_SSAT"))
            {
                best = &ops[i];
                bestLength = n;
            }
        }
    }
    if (!best)
        return NULL;
    {
        const char *rest = token + bestLength;
        if (*rest == 'R' || *rest == 'H' || *rest == 'X')
            rest++;
        *setsCc = *rest == 'C';
        if (*rest == 'C')
            rest++;
        *saturate = !strcmp(rest, "_SAT") ? 1 : !strcmp(rest, "_SSAT") ? 2 : 0;
    }
    return best;
}

// The instruction's result into dst (a register with its mask), from base
// with its sources already rewritten.
static void compute(Out *o, const Op *op, const char *dst, const char **s, int count)
{
    const char *t, *u;

    if (!o->fragment && (!strcmp(op->name, "SIN") || !strcmp(op->name, "COS")))
    {
        // ARBvp1.0 has no trigonometric instructions. Reduce to [-pi, pi]
        // and evaluate sin's degree-13 Taylor polynomial (error < 0.000022).
        // COS uses sin(x + pi/2). Keep the input separate for aliased writes.
        t = scratch(o);
        u = scratch(o);
        emit(o, "MAD %s.x, %s, {0.159154943091895}, {%s};\n", t, s[0],
             !strcmp(op->name, "COS") ? "0.75" : "0.5");
        emit(o, "FRC %s.x, %s.x;\n", t, t);
        emit(o, "MAD %s.x, %s.x, {6.283185307179586}, {-3.141592653589793};\n", t, t);
        emit(o, "MUL %s.y, %s.x, %s.x;\n", t, t, t);
        emit(o, "MAD %s.x, %s.y, {1.605904383682161e-10}, {-2.505210838544172e-8};\n", u, t);
        emit(o, "MAD %s.x, %s.x, %s.y, {2.755731922398589e-6};\n", u, u, t);
        emit(o, "MAD %s.x, %s.x, %s.y, {-0.0001984126984126984};\n", u, u, t);
        emit(o, "MAD %s.x, %s.x, %s.y, {0.008333333333333333};\n", u, u, t);
        emit(o, "MAD %s.x, %s.x, %s.y, {-0.1666666666666667};\n", u, u, t);
        emit(o, "MAD %s.x, %s.x, %s.y, {1};\n", u, u, t);
        emit(o, "MUL %s, %s.x, %s.x;\n", dst, t, u);
        return;
    }

    switch (op->arb)
    {
    case 1:
    case 2:
    case 3:
        emit(o, "%s %s", op->name, dst);
        for (int i = 0; i < count; i++)
            emit(o, ", %s", s[i]);
        emit(o, ";\n");
        return;
    }
    if (!strcmp(op->name, "SGT"))
        emit(o, "SLT %s, %s, %s;\n", dst, s[1], s[0]);
    else if (!strcmp(op->name, "SLE"))
        emit(o, "SGE %s, %s, %s;\n", dst, s[1], s[0]);
    else if (!strcmp(op->name, "SEQ") || !strcmp(op->name, "SNE"))
    {
        t = scratch(o);
        u = scratch(o);
        if (!strcmp(op->name, "SEQ"))
        {
            emit(o, "SGE %s, %s, %s;\nSGE %s, %s, %s;\n", t, s[0], s[1], u, s[1], s[0]);
            emit(o, "MUL %s, %s, %s;\n", dst, t, u);
        }
        else
        {
            emit(o, "SLT %s, %s, %s;\nSLT %s, %s, %s;\n", t, s[0], s[1], u, s[1], s[0]);
            emit(o, "ADD %s, %s, %s;\n", dst, t, u);
        }
    }
    else if (!strcmp(op->name, "SFL"))
        emit(o, "MOV %s, {0,0,0,0};\n", dst);
    else if (!strcmp(op->name, "STR"))
        emit(o, "MOV %s, {1,1,1,1};\n", dst);
    else if (!strcmp(op->name, "SSG"))
    {
        t = scratch(o);
        u = scratch(o);
        emit(o, "SLT %s, {0,0,0,0}, %s;\nSLT %s, %s, {0,0,0,0};\n", t, s[0], u, s[0]);
        emit(o, "SUB %s, %s, %s;\n", dst, t, u);
    }
    else if (!strcmp(op->name, "DIV"))
    {
        t = scratch(o);
        emit(o, "RCP %s.x, %s;\nMUL %s, %s, %s.x;\n", t, s[1], dst, s[0], t);
    }
    else if (!strcmp(op->name, "NRM"))
    {
        t = scratch(o);
        emit(o, "DP3 %s.w, %s, %s;\nRSQ %s.w, %s.w;\nMUL %s, %s, %s.w;\n", t, s[0], s[0], t, t, dst, s[0], t);
    }
    else if (!strcmp(op->name, "DP2"))
    {
        t = scratch(o);
        emit(o, "MUL %s, %s, %s;\nADD %s, %s.x, %s.y;\n", t, s[0], s[1], dst, t, t);
    }
    else if (!strcmp(op->name, "TXL"))
    {
        if (!o->fragment)
            fail(o, "vertex texture fetch");
        emit(o, "TEX %s, %s, %s, %s;\n", dst, s[0], s[1], s[2]);
    }
    else if (!strcmp(op->name, "DDX") || !strcmp(op->name, "DDY"))
        emit(o, "MOV %s, {0,0,0,0};\n", dst);
    else
        fail(o, "no rewrite for %s", op->name);
}

// IF cond; ELSE; ENDIF: the masks of the block's writes. A block's mask is
// its condition (its first component) times the outer block's; ELSE's the
// outer block's times its IF's complement.
static void block(Out *o, const char *token, const char *cond)
{
    int n = o->blocks;

    if (!strcmp(token, "IF"))
    {
        char c[24];
        const char *m;
        if (n >= MAX_BLOCKS)
        {
            fail(o, "IF blocks nested too deep");
            return;
        }
        snprintf(c, sizeof(c), "%.*s", (int)strcspn(cond, ";"), cond);
        if (!strchr(c, '.'))
            strcat(c, ".x");
        m = condition(o, c);
        emit(o, "MOV sd3q%d, %s.x;\n", n, m);
        if (n)
            emit(o, "MUL sd3p%d, sd3q%d, sd3p%d;\n", n, n, n - 1);
        else
            emit(o, "MOV sd3p%d, sd3q%d;\n", n, n);
        o->blocks++;
    }
    else if (!n)
        fail(o, "%s outside a block", token);
    else if (!strcmp(token, "ELSE"))
    {
        emit(o, "SUB sd3p%d, {1,1,1,1}, sd3q%d;\n", n - 1, n - 1);
        if (n > 1)
            emit(o, "MUL sd3p%d, sd3p%d, sd3p%d;\n", n - 1, n - 1, n - 2);
    }
    else
        o->blocks--;
}

static void instruction(Out *o, char *line)
{
    char token[32], *rest = line, *operands[MAX_OPERANDS];
    const Op *op;
    int setsCc, saturate, count;
    char *cond = NULL;
    const char *s[MAX_OPERANDS];
    char dstReg[96];
    const char *dstMask;

    o->scratch = 0;
    sscanf(line, "%31s", token);
    if (!strncmp(token, "ARL", 3))
    {
        // the address register's components, floored, kept in sd3a
        char *dst, *src;
        rest = line + strlen(token);
        if (splitOperands(rest, operands) != 2)
        {
            fail(o, "ARL without its operands");
            return;
        }
        dst = operands[0];
        src = operands[1];
        emit(o, "FLR sd3a%s, %s;\n", strchr(dst, '.') ? strchr(dst, '.') : "", source(o, src));
        return;
    }
    {
        // a use of the address register: A0.x loaded from the component
        char *a = strstr(line, "A0.");
        if (a && strchr("xyzw", a[3]))
        {
            char component = a[3];
            emit(o, "ARL A0.x, sd3a.%c;\n", component);
            for (char *p = line; (p = strstr(p, "A0.")); p += 3)
            {
                if (p[3] != component && strchr("xyzw", p[3]))
                    fail(o, "two address components in one instruction");
                p[3] = 'x';
            }
        }
    }
    rest = line + strlen(token);
    token[strcspn(token, ";")] = '\0';
    if (!strcmp(token, "IF") || !strcmp(token, "ELSE") || !strcmp(token, "ENDIF"))
    {
        block(o, token, trim(rest));
        return;
    }
    if (!strcmp(token, "KIL") && strchr(" \t", rest[0]) && (strstr(rest, "NE") || strstr(rest, "EQ") ||
                                                             strstr(rest, "GT") || strstr(rest, "LT") ||
                                                             strstr(rest, "GE") || strstr(rest, "LE") ||
                                                             strstr(rest, "TR") || strstr(rest, "FL")) &&
        !strchr(rest, 'R') && !strchr(rest, '['))
    {
        // KIL on a condition: -1 where it holds
        const char *m = condition(o, trim(rest));
        if (o->blocks)
            emit(o, "MUL %s, %s, sd3p%d;\n", m, m, o->blocks - 1);
        emit(o, "KIL -%s;\n", m);
        return;
    }
    op = parseOpcode(token, &setsCc, &saturate);
    if (!op)
    {
        fail(o, "unknown instruction %s", token);
        return;
    }
    count = splitOperands(rest, operands);
    if (!strcmp(op->name, "KIL"))
    {
        if (o->blocks)
        {
            // only where the block runs (0: no kill)
            const char *k = scratch(o);
            emit(o, "MUL %s, %s, sd3p%d;\nKIL %s;\n", k, source(o, operands[0]), o->blocks - 1, k);
        }
        else
            emit(o, "KIL %s;\n", source(o, operands[0]));
        return;
    }
    if (count < 1)
    {
        fail(o, "%s without operands", token);
        return;
    }
    // the destination's condition: R0.x (NE.x)
    if ((cond = strchr(operands[0], '(')))
    {
        *cond++ = '\0';
        if (strchr(cond, ')'))
            *strchr(cond, ')') = '\0';
        trim(operands[0]);
    }
    for (int i = 1; i < count; i++)
    {
        s[i - 1] = source(o, operands[i]);
        if (isScalar(op->name))
            s[i - 1] = scalarSource(s[i - 1]);
    }
    splitDestination(operands[0], dstReg, sizeof(dstReg), &dstMask);
    if (op->arb == 2 && !o->fragment && strcmp(op->name, "SIN") && strcmp(op->name, "COS"))
        fail(o, "%s in a vertex program", op->name);
    if (op->arb == 3 && o->fragment)
        fail(o, "%s in a fragment program", op->name);

    if (!cond && !saturate && !setsCc && !o->blocks)
    {
        compute(o, op, operands[0], s, count - 1);
        return;
    }
    {
        // into a scratch register, then saturated, condition-code copied,
        // conditionally written
        const char *v = scratch(o);
        char value[16];
        snprintf(value, sizeof(value), "%s", v);
        if (saturate == 1 && o->fragment && op->arb)
        {
            char sat[40];
            Op satOp = *op;
            snprintf(sat, sizeof(sat), "%s_SAT", op->name);
            satOp.name = sat;
            compute(o, &satOp, value, s, count - 1);
        }
        else
        {
            compute(o, op, value, s, count - 1);
            if (saturate)
                emit(o, "MAX %s, %s, {%s,%s,%s,%s};\nMIN %s, %s, {1,1,1,1};\n", value, value,
                     saturate == 2 ? "-1" : "0", saturate == 2 ? "-1" : "0", saturate == 2 ? "-1" : "0",
                     saturate == 2 ? "-1" : "0", value, value);
        }
        if (cond || o->blocks)
        {
            char m[16];
            const char *d = scratch(o);
            if (cond)
            {
                snprintf(m, sizeof(m), "%s", condition(o, cond));
                if (o->blocks)
                    emit(o, "MUL %s, %s, sd3p%d;\n", m, m, o->blocks - 1);
            }
            else
                snprintf(m, sizeof(m), "sd3p%d", o->blocks - 1);
            // fragment: dst = m ? value : dst (CMP, safe from the other
            // side's infinities); vertex: dst += m * (value - dst)
            if (o->fragment)
                emit(o, "CMP %s%s, -%s, %s, %s;\n", dstReg, dstMask, m, value, dstReg);
            else
                emit(o, "SUB %s, %s, %s;\nMAD %s%s, %s, %s, %s;\n", d, value, dstReg, dstReg, dstMask, m, d, dstReg);
            if (setsCc && o->blocks && o->fragment)
                emit(o, "CMP sd3cc%s, -sd3p%d, %s, sd3cc;\n", dstMask, o->blocks - 1, value);
            else if (setsCc && o->blocks)
                emit(o, "SUB %s, %s, sd3cc;\nMAD sd3cc%s, sd3p%d, %s, sd3cc;\n", d, value, dstMask, o->blocks - 1, d);
            else if (setsCc)
                emit(o, "MOV sd3cc%s, %s;\n", dstMask, value);
        }
        else
        {
            emit(o, "MOV %s%s, %s;\n", dstReg, dstMask, value);
            if (setsCc)
                emit(o, "MOV sd3cc%s, %s;\n", dstMask, value);
        }
    }
}

static int braces(const char *s)
{
    int depth = 0;
    for (; *s; s++)
        depth += *s == '{' ? 1 : *s == '}' ? -1 : 0;
    return depth;
}

// Every "from" in s replaced by "to" (malloc'ed).
static char *replaceAll(const char *s, const char *from, const char *to)
{
    size_t count = 0, fromLength = strlen(from), toLength = strlen(to);
    char *out, *w;

    for (const char *p = s; (p = strstr(p, from)); p += fromLength)
        count++;
    out = malloc(strlen(s) + count * (toLength + 1) + 1);
    for (w = out; *s;)
        if (!strncmp(s, from, fromLength))
        {
            memcpy(w, to, toLength);
            w += toLength;
            s += fromLength;
        }
        else
            *w++ = *s++;
    *w = '\0';
    return out;
}

// The program as plain ARB assembly (malloc'ed), or NULL (o->error).
static char *rewrite(const char *program, Out *o)
{
    char *copy = replaceAll(program, "vertex.weight", "vertex.attrib[1]"), *line, *save = NULL;
    int declared = 0;

    memset(o, 0, sizeof(*o));
    o->size = strlen(program) * 3 + 1024;
    o->text = malloc(o->size);
    o->text[0] = '\0';
    o->fragment = !strncmp(program, "!!ARBfp", 7);
    if (strstr(program, "\nREP") || strstr(program, "\nBRA") || strstr(program, "\nCAL ") ||
        strstr(program, "\nLOOP") || strstr(program, "\nRET"))
        fail(o, "loops or calls");

    for (line = strtok_r(copy, "\n", &save); line && !o->failed; line = strtok_r(NULL, "\n", &save))
    {
        char *t = trim(line);
        size_t n = strlen(t);

        if (!n || t[0] == '#')
            continue;
        if (!strncmp(t, "!!", 2))
        {
            emit(o, "%s\n", t);
            continue;
        }
        if (!strncmp(t, "OPTION NV_", 10))
            continue;
        // (SHORT OUTPUT: Speed Rider's fp40 programs)
        if (!strncmp(t, "SHORT ", 6) || !strncmp(t, "LONG ", 5))
            t = strchr(t, ' ') + 1;
        if (o->fragment && !strncmp(t, "OUTPUT ", 7) && o->outputCount < 4)
        {
            char name[32], target[40];
            if (sscanf(t, "OUTPUT %31[^ =] = %39[^ ;]", name, target) == 2)
            {
                emit(o, "TEMP %s;\n", name);
                snprintf(o->outputs[o->outputCount++], sizeof(o->outputs[0]), "%s, %s", target, name);
                continue;
            }
        }
        if (!strncmp(t, "OPTION ", 7) || !strncmp(t, "PARAM ", 6) || !strncmp(t, "ATTRIB ", 7) ||
            !strncmp(t, "OUTPUT ", 7) || !strncmp(t, "ADDRESS ", 8) || !strncmp(t, "ALIAS ", 6))
        {
            // (a PARAM's list spans lines: copied as is up to its closing
            // brace)
            int depth = braces(t);
            emit(o, "%s%s\n", t, depth > 0 ? "" : ";");
            while (depth > 0 && (line = strtok_r(NULL, "\n", &save)))
            {
                t = trim(line);
                depth += braces(t);
                emit(o, "%s%s\n", t, depth > 0 ? "" : ";");
            }
            continue;
        }
        if (!strncmp(t, "TEMP ", 5))
        {
            emit(o, "%s;\n", t);
            continue;
        }
        if (!strcmp(t, "END"))
            break;
        if (t[n - 1] == ':')
            continue; // a label (no branches)
        if (!declared)
        {
            emit(o, "TEMP sd3cc, sd3a, sd3s0, sd3s1, sd3s2, sd3s3, sd3s4, sd3s5, sd3s6, sd3s7;\n");
            if (strstr(program, "\nIF "))
                emit(o, "TEMP sd3p0, sd3p1, sd3p2, sd3p3, sd3q0, sd3q1, sd3q2, sd3q3;\n");
            declared = 1;
        }
        instruction(o, t);
    }
    if (o->blocks)
        fail(o, "IF without ENDIF");
    for (int i = 0; i < o->outputCount; i++)
        emit(o, "MOV %s;\n", o->outputs[i]);
    emit(o, "END\n");
    free(copy);
    if (o->failed)
    {
        free(o->text);
        o->text = NULL;
    }
    return o->text;
}

// IGS_NV_DUMP=<dir> (debugging): each program as Cg compiled it, and its
// rewrite.
static void dump(const char *program, const char *rewritten)
{
    char path[4096];
    FILE *f;

    snprintf(path, sizeof(path), "%s/%04u.txt", dumpDir, dumped);
    if ((f = fopen(path, "w")))
    {
        fputs(program, f);
        fclose(f);
    }
    if (rewritten)
    {
        snprintf(path, sizeof(path), "%s/%04u.arb", dumpDir, dumped);
        if ((f = fopen(path, "w")))
        {
            fputs(rewritten, f);
            fclose(f);
        }
    }
    dumped++;
}

static const char *filter(const char *program)
{
    static char *last;
    Out o;
    char *rewritten;

    if (!strstr(program, "OPTION NV_"))
    {
        if (dumpDir)
            dump(program, NULL);
        return program;
    }
    rewritten = rewrite(program, &o);
    if (dumpDir)
        dump(program, rewritten);
    if (!rewritten)
    {
        const char *name = strstr(program, "#program ");
        say("not rewritten (%s): %.*s", o.error, name ? (int)strcspn(name + 9, "\n") : 0, name ? name + 9 : "");
        return program;
    }
    free(last);
    last = rewritten;
    return rewritten;
}

// Speed Driver 4's car paint (all_purpose_car_ex) is for gp4fp/gp4vp
// (NV_gpu_program4, GeForce 8), its Direct3D twin for ps_3_0: Cg compiles
// it for fp40/vp40 instead, rewritten as the others, with the twin's
// arguments (no centroid sampling, a gp4 one).
#define CG_PROFILE_VP40 7001
#define CG_PROFILE_FP40 6151
#define CG_PROFILE_GP4FP 7010
#define CG_PROFILE_GP4VP 7011

static int nv40Profile(int profile, const char ***args)
{
    static const char *copy[64];
    size_t n = 0;

    if (profile != CG_PROFILE_GP4FP && profile != CG_PROFILE_GP4VP)
        return profile;
    for (const char **a = *args; a && *a && n + 1 < sizeof(copy) / sizeof(copy[0]); a++)
        copy[n++] = strcmp(*a, "-DCENTRIOD=1") ? *a : "-DCENTRIOD=0";
    copy[n] = NULL;
    *args = copy;
    return profile == CG_PROFILE_GP4FP ? CG_PROFILE_FP40 : CG_PROFILE_VP40;
}

void igsNvProgramInstall(void)
{
    // (Mesa appends names it does not know, with a warning)
    setenv("MESA_EXTENSION_OVERRIDE",
           "+GL_NV_fragment_program2 +GL_NV_vertex_program3 +GL_NV_vertex_program2_option +GL_NV_fragment_program "
           "+GL_NV_vertex_program2 +GL_NV_fragment_program_option +GL_NV_gpu_program4",
           1);
    cgCreateProgramFilter = nv40Profile;
    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    dumpDir = getenv("IGS_NV_DUMP");
    cgCompiledProgramFilter = filter;
    if (dumpDir)
        say("programs dumped to %s", dumpDir);
}
