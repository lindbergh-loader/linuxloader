// NVIDIA's extended assembly programs (Tank! Tank! Tank!: Cg's vp40/fp40
// output, "OPTION NV_vertex_program3" and "OPTION NV_fragment_program2")
// rewritten as plain ARB_vertex_program / ARB_fragment_program, which Mesa
// runs: it rejects the NV options, and the game's 3D drew white.
//
// Only what the game's programs use is handled:
// - both: the option, the condition-code registers RC/HC, labels;
// - vertex: _SAT (not in ARB vertex programs: MAX/MIN through a temporary),
//   the four-component address register (ARB has A0.x only: ARL stores the
//   floored value in a temporary, each relative operand reloads A0.x from it
//   and is fetched into a temporary);
// - fragment: the precision suffixes (R, H, X) and SHORT; the condition code
//   (C suffix: a temporary CC), conditional writes (GT/NE/EQ.x) and IF/ENDIF
//   blocks made selects (CMP); a LOOP with a constant count, unrolled.
// A program using anything else is returned as it is (and Mesa rejects it).

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "namcoEs1.h"

typedef struct
{
    char *text;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *fmt, ...)
{
    va_list ap;
    for (;;)
    {
        va_start(ap, fmt);
        int n = vsnprintf(b->text + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n >= 0 && b->len + n < b->cap)
        {
            b->len += n;
            return;
        }
        b->cap = (b->cap + n + 1) * 2;
        b->text = realloc(b->text, b->cap);
    }
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

// Operands split at top-level commas (not inside [] or ()).
static int splitOperands(char *s, char **ops, int max)
{
    int n = 0, depth = 0;
    char *start = s;
    for (char *p = s;; p++)
    {
        if (*p == '[' || *p == '(' || *p == '{')
            depth++;
        else if (*p == ']' || *p == ')' || *p == '}')
            depth--;
        else if ((*p == ',' && depth == 0) || *p == '\0')
        {
            int end = *p == '\0';
            *p = '\0';
            if (n < max)
                ops[n++] = trim(start);
            if (end)
                break;
            start = p + 1;
        }
    }
    return n;
}

// The opcode's parts: base, condition-code update (C), saturation.
typedef struct
{
    char base[16];
    int updateCC, saturate;
} Opcode;

static const char *const bases[] = {"MOV", "ADD", "MAD", "MUL", "DP3", "DP4", "DPH", "RSQ", "RCP", "MAX", "MIN", "SLT",
                                    "SGE", "SIN", "COS", "TEX", "TXP", "TXB", "FLR", "FRC", "ARL", "EX2", "LG2", "POW",
                                    "LRP", "CMP", "ABS", "SUB", "XPD", "LIT", "KIL", "SCS", "SWZ", NULL};

static int parseOpcode(const char *word, Opcode *op)
{
    memset(op, 0, sizeof(*op));
    for (int i = 0; bases[i]; i++)
    {
        size_t n = strlen(bases[i]);
        if (strncmp(word, bases[i], n))
            continue;
        const char *rest = word + n;
        if (*rest == 'R' || *rest == 'H' || *rest == 'X')
            rest++;
        if (*rest == 'C')
        {
            op->updateCC = 1;
            rest++;
        }
        if (!strcmp(rest, "_SAT"))
        {
            op->saturate = 1;
            rest += 4;
        }
        if (*rest)
            continue;
        strcpy(op->base, bases[i]);
        return 1;
    }
    return 0;
}

// A destination: register and write mask, and its condition ("GT.x"...).
static void splitDestination(const char *dst, char *reg, char *mask, char *cond)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", dst);
    cond[0] = mask[0] = '\0';
    char *paren = strchr(tmp, '(');
    if (paren)
    {
        char *close = strchr(paren, ')');
        if (close)
            *close = '\0';
        snprintf(cond, 16, "%s", paren + 1);
        *paren = '\0';
    }
    char *dot = strrchr(tmp, '.');
    // a mask follows the last '.', unless that '.' is part of a binding name
    // (result.texcoord[0] has no mask; result.color.secondary.w has one)
    if (dot && strspn(dot + 1, "xyzw") == strlen(dot + 1) && dot[1])
    {
        snprintf(mask, 8, "%s", dot + 1);
        *dot = '\0';
    }
    snprintf(reg, 96, "%s", trim(tmp));
}

static int isCCRegister(const char *reg)
{
    return !strcmp(reg, "RC") || !strcmp(reg, "HC");
}

// ---------------------------------------------------------------------------
// Vertex programs.

static char *vertexProgram(const char *src)
{
    Buf out = {0};
    char *copy = strdup(src), *save = NULL;
    int declared = 0;

    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *s = trim(line);
        if (!strncmp(s, "OPTION NV_vertex_program", 24) || !strncmp(s, "TEMP RC", 7) || !strcmp(s, "BB1:"))
            continue;
        if (!strncmp(s, "ADDRESS", 7))
        {
            put(&out, "ADDRESS A0;\n");
            continue;
        }
        Opcode op;
        char word[32] = "";
        sscanf(s, "%31s", word);
        if (!parseOpcode(word, &op))
        {
            put(&out, "%s\n", s);
            continue;
        }
        // the loader's temporaries and constants, before the first instruction
        if (!declared)
        {
            put(&out, "TEMP nvAddr, nvRel0, nvRel1, nvRel2, nvSat;\nPARAM nvSat01 = { 0, 1, 0, 0 };\n");
            declared = 1;
        }

        char body[1024];
        snprintf(body, sizeof(body), "%s", s + strlen(word));
        char *semi = strrchr(body, ';');
        if (semi)
            *semi = '\0';
        char *ops[4];
        int n = splitOperands(body, ops, 4);
        if (n < 1 || op.updateCC)
            goto unsupported;

        if (!strcmp(op.base, "ARL"))
        {
            char reg[96], mask[8], cond[16];
            splitDestination(ops[0], reg, mask, cond);
            put(&out, "FLR nvAddr.%s, %s;\n", mask[0] ? mask : "xyzw", ops[1]);
            continue;
        }
        // relative operands: c[A0.k + n] fetched into temporaries
        char fixed[4][160];
        for (int i = 1; i < n; i++)
        {
            char *rel = strstr(ops[i], "[A0.");
            if (!rel)
            {
                snprintf(fixed[i], sizeof(fixed[i]), "%s", ops[i]);
                continue;
            }
            char comp = rel[4];
            const char *close = strchr(rel, ']');
            char inside[64];
            snprintf(inside, sizeof(inside), "%.*s", (int)(close - (rel + 5)), rel + 5);
            // the array name before '['
            const char *name = ops[i];
            int negate = 0;
            if (*name == '-')
            {
                negate = 1;
                name++;
            }
            put(&out, "ARL A0.x, nvAddr.%c;\nMOV nvRel%d, %.*s[A0.x%s];\n", comp, i - 1, (int)(rel - name), name, inside);
            snprintf(fixed[i], sizeof(fixed[i]), "%snvRel%d%s", negate ? "-" : "", i - 1, close + 1);
        }
        char srcs[512] = "";
        for (int i = 1; i < n; i++)
        {
            strcat(srcs, ", ");
            strcat(srcs, fixed[i]);
        }
        if (!op.saturate)
        {
            put(&out, "%s %s%s;\n", op.base, ops[0], srcs);
            continue;
        }
        char reg[96], mask[8], cond[16];
        splitDestination(ops[0], reg, mask, cond);
        const char *m = mask[0] ? mask : "xyzw";
        put(&out, "%s nvSat.%s%s;\nMAX nvSat.%s, nvSat, nvSat01.x;\nMIN %s, nvSat, nvSat01.y;\n", op.base, m, srcs, m, ops[0]);
    }
    free(copy);
    return out.text;

unsupported:
    free(copy);
    free(out.text);
    return NULL;
}

// ---------------------------------------------------------------------------
// Fragment programs.

#define MAX_DEPTH 16

typedef struct
{
    Buf out;
    int depth; // IF nesting: nvMask0..depth-1 hold each level's mask (0/1)
} Fragment;

// cond ("GT.x"...) evaluated into nvCond.x as 0 or 1, combined with the IF
// masks; returns 0 when the write is unconditional.
static int predicate(Fragment *f, const char *cond)
{
    if (cond[0])
    {
        char test[4] = "";
        char comp = 'x';
        sscanf(cond, "%2s", test);
        const char *dot = strchr(cond, '.');
        if (dot)
            comp = dot[1];
        if (!strcmp(test, "GT"))
            put(&f->out, "SLT nvCond.x, nvZero.x, nvCC.%c;\n", comp);
        else if (!strcmp(test, "NE"))
            put(&f->out, "ABS nvCond.x, nvCC.%c;\nSLT nvCond.x, nvZero.x, nvCond.x;\n", comp);
        else if (!strcmp(test, "EQ"))
            put(&f->out, "ABS nvCond.x, nvCC.%c;\nSGE nvCond.x, nvZero.x, nvCond.x;\n", comp);
        else
            return -1;
        if (f->depth)
            put(&f->out, "MUL nvCond.x, nvCond.x, nvMask%d.x;\n", f->depth - 1);
        return 1;
    }
    if (f->depth)
    {
        put(&f->out, "MOV nvCond.x, nvMask%d.x;\n", f->depth - 1);
        return 1;
    }
    return 0;
}

// One instruction (the LOOP body is replayed through it too).
static int fragmentInstruction(Fragment *f, char *s)
{
    char word[32] = "";
    sscanf(s, "%31s", word);

    if (!strcmp(word, "IF"))
    {
        char cond[16] = "";
        sscanf(s + 2, " %15[^;]", cond);
        if (f->depth >= MAX_DEPTH || predicate(f, cond) <= 0)
            return 0;
        put(&f->out, "MOV nvMask%d.x, nvCond.x;\n", f->depth);
        f->depth++;
        return 1;
    }
    if (!strncmp(word, "ENDIF", 5))
    {
        if (!f->depth)
            return 0;
        f->depth--;
        return 1;
    }

    Opcode op;
    if (!parseOpcode(word, &op))
        return 0;
    char body[1024];
    snprintf(body, sizeof(body), "%s", s + strlen(word));
    char *semi = strrchr(body, ';');
    if (semi)
        *semi = '\0';
    char *ops[5];
    int n = splitOperands(body, ops, 5);
    if (n < 1)
        return 0;

    char reg[96], mask[8], cond[16];
    splitDestination(ops[0], reg, mask, cond);
    const char *m = mask[0] ? mask : "xyzw";
    int toCC = isCCRegister(reg);
    if (toCC)
        snprintf(reg, sizeof(reg), "nvCC");

    char srcs[512] = "";
    for (int i = 1; i < n; i++)
    {
        strcat(srcs, ", ");
        strcat(srcs, ops[i]);
    }
    const char *sat = op.saturate ? "_SAT" : "";
    int predicated = predicate(f, cond);
    if (predicated < 0)
        return 0;
    if (!predicated)
        put(&f->out, "%s%s %s.%s%s;\n", op.base, sat, reg, m, srcs);
    else
    {
        // computed apart, then selected into the destination where it holds
        put(&f->out, "%s%s nvTmp.%s%s;\nCMP %s.%s, -nvCond.xxxx, nvTmp, %s;\n", op.base, sat, m, srcs, reg, m, reg);
    }
    // the condition code follows the result
    if (op.updateCC && !toCC)
        put(&f->out, "MOV nvCC.%s, %s;\n", m, reg);
    return 1;
}

// A literal of the program's "PARAM c[N] = { {a, b, c, d}, ... }": c[row].x.
static int paramLiteral(const char *src, int row, float *value)
{
    const char *p = strstr(src, "PARAM c[");
    if (!p || !(p = strchr(p, '{')))
        return 0;
    p++;
    for (int r = 0; r <= row; r++)
    {
        p = strchr(p, '{');
        if (!p)
            return 0;
        if (r < row)
            p = strchr(p, '}');
        if (!p)
            return 0;
        p++;
    }
    return sscanf(p, " %f", value) == 1;
}

static char *fragmentProgram(const char *src)
{
    Fragment f = {0};
    char *copy = strdup(src), *save = NULL;
    int declared = 0;
    char *loopBody[64];
    int loopLines = -1, loopCount = 0;
    char outputName[4][64], outputBinding[4][96];
    int outputs = 0;

    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *s = trim(line);
        if (!strncmp(s, "OPTION NV_fragment_program", 26) || !strcmp(s, "TEMP RC;") || !strcmp(s, "TEMP HC;"))
            continue;
        if (!strncmp(s, "SHORT ", 6) || !strncmp(s, "LONG ", 5))
            s = trim(strchr(s, ' '));
        if (!strncmp(s, "LOOP", 4))
        {
            int row;
            float count;
            if (loopLines >= 0 || sscanf(s, "LOOP c[%d]", &row) != 1 || !paramLiteral(src, row, &count) ||
                count < 1 || count > 64)
                goto unsupported;
            loopLines = 0;
            loopCount = (int)count;
            continue;
        }
        if (!strncmp(s, "ENDLOOP", 7))
        {
            if (loopLines < 0)
                goto unsupported;
            for (int k = 0; k < loopCount; k++)
                for (int i = 0; i < loopLines; i++)
                {
                    char tmp[1024];
                    snprintf(tmp, sizeof(tmp), "%s", loopBody[i]);
                    if (!fragmentInstruction(&f, tmp))
                        goto unsupported;
                }
            loopLines = -1;
            continue;
        }
        if (loopLines >= 0)
        {
            if (loopLines == 64)
                goto unsupported;
            loopBody[loopLines++] = s;
            continue;
        }

        char word[32] = "";
        sscanf(s, "%31s", word);
        // Outputs cannot be read back (the selects do): a temporary each,
        // copied to the output at the end.
        char name[64], binding[96];
        if (sscanf(s, "OUTPUT %63s = %95[^;];", name, binding) == 2)
        {
            if (outputs == 4)
                goto unsupported;
            snprintf(outputName[outputs], sizeof(outputName[0]), "%s", name);
            snprintf(outputBinding[outputs], sizeof(outputBinding[0]), "%s", binding);
            outputs++;
            put(&f.out, "TEMP %s;\n", name);
            continue;
        }
        if (!strcmp(s, "END"))
        {
            for (int i = 0; i < outputs; i++)
                put(&f.out, "MOV %s, %s;\n", outputBinding[i], outputName[i]);
            put(&f.out, "END\n");
            continue;
        }
        Opcode op;
        if (strcmp(word, "IF") && strncmp(word, "ENDIF", 5) && !parseOpcode(word, &op))
        {
            put(&f.out, "%s\n", s);
            continue;
        }
        if (!declared)
        {
            put(&f.out, "TEMP nvCC, nvCond, nvTmp");
            for (int d = 0; d < MAX_DEPTH; d++)
                put(&f.out, ", nvMask%d", d);
            put(&f.out, ";\nPARAM nvZero = { 0, 0, 0, 0 };\n");
            // The output components a program leaves unwritten (blur writes
            // only alpha) are 0, as NVIDIA's: not a temporary's leftovers.
            for (int i = 0; i < outputs; i++)
                put(&f.out, "MOV %s, nvZero;\n", outputName[i]);
            declared = 1;
        }
        if (!fragmentInstruction(&f, s))
            goto unsupported;
    }
    free(copy);
    if (f.depth)
    {
        free(f.out.text);
        return NULL;
    }
    return f.out.text;

unsupported:
    free(copy);
    free(f.out.text);
    return NULL;
}

// ---------------------------------------------------------------------------
// NV_gpu_program4 programs (WMMT4: Cg's gp4vp/gp4fp output, "!!NVvp4.0" and
// "!!NVfp4.0"), which only NVIDIA's driver runs, rewritten as plain ARB ones.
// - The type suffixes (.F, .S, .U) go: everything is a float, which the
//   programs' few integer values (truncations, I2F) are exact in. .SAT is
//   _SAT (fragment) or MAX/MIN through a temporary (vertex); .CC also writes
//   the condition code (nvCC; the RC/HC registers are it).
// - What ARB lacks: DIV (RCP, MUL), DP2, SLE/SGT/SEQ/SNE (SGE/SLT), TRUNC
//   (FLR of the magnitude), |abs| operands (ABS through temporaries).
// - IF/ELSE/ENDIF and conditional writes (dst(NE.x)): every write in them is
//   computed apart then selected into its destination by the masks (CMP;
//   arithmetic in vertex programs, which have none, the computed value
//   clamped first so that a branch not taken cannot bring an infinity in).
// - Inline literals with fewer than four components are spelt out.
// - The array declarations go: the programs name the bindings directly.
//   Generic varyings (result.attrib[n], fragment.attrib[n]) have no ARB
//   binding: 17 and 18 use the texture coordinates the programs leave free
//   (1 and 5); others are dropped (written nowhere, read as 0).
// - The fragment outputs are temporaries (the selects read them), 0 at the
//   start as NVIDIA's are, copied to the outputs at the end.
// - REP/ENDREP loops are unrolled (their count, else 32 times), with a mask
//   of their own that BRK clears; the integer comparisons (.S, .U) give ~0
//   (-1) for true, as the loops' exit tests expect.
// Local arrays and relative addressing are not handled: such a program is
// returned as NULL.

#define GP4_DEPTH 8

typedef struct
{
    Buf out;
    int vertex;
    int depth; // IF/REP nesting: nvMask0..depth-1 hold each level's mask (0/1)
    int loop[GP4_DEPTH], loops; // the mask levels of the REP loops, innermost last
    int declared;
    int failed;
    char outputName[8][64], outputBinding[8][96];
    int outputs;
} Gp4;

// A literal "{a, b}" or "{a}.y", spelt out as four components.
static int gp4Literal(const char *in, char *out, size_t size)
{
    float v[4] = {0, 0, 0, 1};
    int n = 0;
    const char *p = in + 1;

    while (n < 4)
    {
        char *end;
        float f = strtof(p, &end);
        if (end == p)
            return 0;
        v[n++] = f;
        p = end;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == ',')
            p++;
        else
            break;
    }
    if (*p != '}')
        return 0;
    if (n == 1)
        v[1] = v[2] = v[3] = v[0];
    p++;
    float r[4] = {v[0], v[1], v[2], v[3]};
    if (*p == '.')
    {
        const char *swz = p + 1;
        size_t k = strspn(swz, "xyzw");
        if (k == 0 || swz[k])
            return 0;
        for (int i = 0; i < 4; i++)
        {
            char c = swz[i < (int)k ? i : (int)k - 1];
            r[i] = v[c == 'x' ? 0 : c == 'y' ? 1 : c == 'z' ? 2 : 3];
        }
    }
    else if (*p)
        return 0;
    snprintf(out, size, "{%.9g, %.9g, %.9g, %.9g}", r[0], r[1], r[2], r[3]);
    return 1;
}

// result.attrib[n] / fragment.attrib[n]: a texture coordinate, or NULL.
static const char *gp4Varying(int n)
{
    return n == 17 ? "texcoord[1]" : n == 18 ? "texcoord[5]" : NULL;
}

// A register or binding: generic varyings mapped (see gp4Varying).
static void gp4Register(Gp4 *g, const char *in, char *out, size_t size, int destination)
{
    const char *at;
    int n;
    char rest[64];

    if ((at = strstr(in, ".attrib[")) && (!strncmp(in, "result.", 7) || !strncmp(in, "fragment.", 9)) &&
        sscanf(at, ".attrib[%d]", &n) == 1)
    {
        const char *texcoord = gp4Varying(n);
        snprintf(rest, sizeof(rest), "%s", strchr(at, ']') + 1);
        if (texcoord)
            snprintf(out, size, "%.*s.%s%s", (int)(at - in), in, texcoord, rest);
        else
            snprintf(out, size, "%s%s", destination ? "nvDrop" : "nvZero", rest);
        return;
    }
    if (!strcmp(in, "RC") || !strcmp(in, "HC") || !strncmp(in, "RC.", 3) || !strncmp(in, "HC.", 3))
    {
        snprintf(out, size, "nvCC%s", in + 2);
        return;
    }
    if (strstr(in, "[R") || strstr(in, "lmem"))
        g->failed = 1;
    snprintf(out, size, "%s", in);
}

// A source operand: negation, |abs| (into nvAbs<k>), literal, register.
static void gp4Source(Gp4 *g, const char *in, char *out, size_t size, int k)
{
    char inner[160], text[160];
    const char *neg = "";

    if (*in == '-')
    {
        neg = "-";
        for (in++; isspace((unsigned char)*in); in++)
            ;
    }
    size_t n = strlen(in);
    if (*in == '|' && n > 2 && in[n - 1] == '|')
    {
        snprintf(inner, sizeof(inner), "%.*s", (int)n - 2, in + 1);
        gp4Source(g, inner, text, sizeof(text), k);
        put(&g->out, "ABS nvAbs%d, %s;\n", k, text);
        snprintf(out, size, "%snvAbs%d", neg, k);
        return;
    }
    if (*in == '{')
    {
        if (!gp4Literal(in, text, sizeof(text)))
            g->failed = 1;
        snprintf(out, size, "%s%s", neg, text);
        return;
    }
    gp4Register(g, in, text, sizeof(text), 0);
    snprintf(out, size, "%s%s", neg, text);
}

// A scalar operand: given a component when it has none.
static void gp4Scalar(const char *src, char *out, size_t size)
{
    const char *dot = strrchr(src, '.');
    const char *close = strrchr(src, ']');
    const char *brace = strrchr(src, '}');
    int swizzled = dot && (!close || dot > close) && (!brace || dot > brace) && strspn(dot + 1, "xyzw") == strlen(dot + 1);
    snprintf(out, size, "%s%s", src, swizzled ? "" : ".x");
}

// nvCond.x = the condition ("NE.x"...) on nvCC, and with the IF masks.
static int gp4Condition(Gp4 *g, const char *cond)
{
    char test[4] = "";
    char comp = 'x';
    const char *dot = strchr(cond, '.');

    sscanf(cond, "%2s", test);
    if (dot)
        comp = dot[1];
    if (!strcmp(test, "NE"))
        put(&g->out, "ABS nvCond.x, nvCC.%c;\nSLT nvCond.x, nvZero.x, nvCond.x;\n", comp);
    else if (!strcmp(test, "EQ"))
        put(&g->out, "ABS nvCond.x, nvCC.%c;\nSGE nvCond.x, nvZero.x, nvCond.x;\n", comp);
    else if (!strcmp(test, "GT"))
        put(&g->out, "SLT nvCond.x, nvZero.x, nvCC.%c;\n", comp);
    else if (!strcmp(test, "LT"))
        put(&g->out, "SLT nvCond.x, nvCC.%c, nvZero.x;\n", comp);
    else if (!strcmp(test, "GE"))
        put(&g->out, "SGE nvCond.x, nvCC.%c, nvZero.x;\n", comp);
    else if (!strcmp(test, "LE"))
        put(&g->out, "SGE nvCond.x, nvZero.x, nvCC.%c;\n", comp);
    else
        return 0;
    if (g->depth)
        put(&g->out, "MUL nvCond.x, nvCond.x, nvMask%d.x;\n", g->depth - 1);
    return 1;
}

static void gp4Declare(Gp4 *g)
{
    if (g->declared)
        return;
    g->declared = 1;
    put(&g->out, "TEMP nvCC, nvCond, nvTmp, nvT, nvT2, nvSat, nvAbs0, nvAbs1, nvAbs2, nvDrop");
    for (int d = 0; d < GP4_DEPTH; d++)
        put(&g->out, ", nvMask%d", d);
    put(&g->out, ";\nPARAM nvZero = {0, 0, 0, 0};\nPARAM nvOne = {1, 1, 1, 1};\nPARAM nvBig = {1e30, 1e30, 1e30, 1e30};\n");
    for (int i = 0; i < g->outputs; i++)
        put(&g->out, "MOV %s, nvZero;\n", g->outputName[i]);
}

static int gp4Instruction(Gp4 *g, char *s)
{
    char word[32] = "", base[16] = "";
    int saturate = 0, updateCC = 0;

    sscanf(s, "%31s", word);
    char *semi = strrchr(s, ';');
    if (semi)
        *semi = '\0';

    if (!strcmp(word, "IF"))
    {
        char cond[16] = "";
        sscanf(s + 2, " %15s", cond);
        if (g->depth >= GP4_DEPTH || !gp4Condition(g, cond))
            return 0;
        put(&g->out, "MOV nvMask%d.x, nvCond.x;\n", g->depth++);
        return 1;
    }
    if (!strcmp(word, "ELSE") || !strcmp(word, "ELSE;"))
    {
        if (!g->depth)
            return 0;
        int d = g->depth - 1;
        if (d)
            put(&g->out, "ADD nvMask%d.x, nvMask%d.x, -nvMask%d.x;\n", d, d - 1, d);
        else
            put(&g->out, "ADD nvMask0.x, nvOne.x, -nvMask0.x;\n");
        return 1;
    }
    if (!strncmp(word, "ENDIF", 5))
    {
        if (!g->depth)
            return 0;
        g->depth--;
        return 1;
    }
    if (!strncmp(word, "BRK", 3))
    {
        char cond[16] = "";
        const char *open = strchr(s, '(');
        if (!g->loops)
            return 0;
        int level = g->loop[g->loops - 1];
        if (!open)
        {
            put(&g->out, "MOV nvMask%d.x, nvZero.x;\n", level);
            return 1;
        }
        sscanf(open + 1, "%15[^)]", cond);
        if (!gp4Condition(g, cond))
            return 0;
        put(&g->out, "ADD nvMask%d.x, nvMask%d.x, -nvCond.x;\n", level, level);
        return 1;
    }

    // BASE[.F|.S|.U][.CC][.SAT]
    int integer = 0;
    char modifiers[32];
    snprintf(modifiers, sizeof(modifiers), "%s", word);
    char *part = strtok(modifiers, ".");
    snprintf(base, sizeof(base), "%s", part ? part : "");
    while ((part = strtok(NULL, ".")))
    {
        if (!strcmp(part, "SAT"))
            saturate = 1;
        else if (!strcmp(part, "CC") || !strcmp(part, "CC0"))
            updateCC = 1;
        else if (!strcmp(part, "S") || !strcmp(part, "U"))
            integer = 1;
        else if (strcmp(part, "F"))
            return 0;
    }

    char body[1024];
    snprintf(body, sizeof(body), "%s", s + strlen(word));
    char *ops[5];
    int n = splitOperands(body, ops, 5);
    if (n < 2)
        return 0;

    char dstText[160], reg[96], mask[8], cond[16];
    gp4Register(g, ops[0], dstText, sizeof(dstText), 1);
    splitDestination(dstText, reg, mask, cond);
    const char *m = mask[0] ? mask : "xyzw";

    char src[4][160];
    for (int i = 1; i < n && i < 5; i++)
        if (!strcmp(base, "TEX") || !strcmp(base, "TXP") || !strcmp(base, "TXB"))
        {
            // the texture unit and target stay as they are
            if (i >= 2)
                snprintf(src[i - 1], sizeof(src[0]), "%s", ops[i]);
            else
                gp4Source(g, ops[i], src[i - 1], sizeof(src[0]), i - 1);
        }
        else
            gp4Source(g, ops[i], src[i - 1], sizeof(src[0]), i - 1);
    int nsrc = n - 1;
    if (g->failed)
        return 0;

    // where the result goes: the destination, or nvTmp to be selected
    int predicated = cond[0] || g->depth;
    if (predicated && g->vertex && !strncmp(reg, "result.", 7))
        return 0; // cannot read back a vertex output
    char target[96];
    snprintf(target, sizeof(target), "%s", predicated ? "nvTmp" : reg);
    // the last instruction writes final (nvSat for a vertex saturation)
    int vertexSat = saturate && g->vertex;
    const char *final = vertexSat ? "nvSat" : target;
    const char *sat = saturate && !g->vertex ? "_SAT" : "";

    char scalar[160];
    if (!strcmp(base, "DIV"))
    {
        gp4Scalar(src[1], scalar, sizeof(scalar));
        put(&g->out, "RCP nvT.x, %s;\nMUL%s %s.%s, %s, nvT.x;\n", scalar, sat, final, m, src[0]);
    }
    else if (!strcmp(base, "DP2"))
        put(&g->out, "MUL nvT, %s, %s;\nADD%s %s.%s, nvT.x, nvT.y;\n", src[0], src[1], sat, final, m);
    else if (!strcmp(base, "SLE"))
        put(&g->out, "SGE%s %s.%s, %s, %s;\n", sat, final, m, src[1], src[0]);
    else if (!strcmp(base, "SGT"))
        put(&g->out, "SLT%s %s.%s, %s, %s;\n", sat, final, m, src[1], src[0]);
    else if (!strcmp(base, "SEQ") || !strcmp(base, "SNE"))
    {
        put(&g->out, "SGE nvT, %s, %s;\nSGE nvT2, %s, %s;\nMUL nvT, nvT, nvT2;\n", src[0], src[1], src[1], src[0]);
        if (!strcmp(base, "SEQ"))
            put(&g->out, "MOV%s %s.%s, nvT;\n", sat, final, m);
        else
            put(&g->out, "ADD%s %s.%s, nvOne, -nvT;\n", sat, final, m);
    }
    else if (!strcmp(base, "TRUNC"))
    {
        // the magnitude floored, the sign put back (unsigned: from 0 up)
        if (strstr(word, ".U"))
            put(&g->out, "MAX nvT, %s, nvZero;\nFLR%s %s.%s, nvT;\n", src[0], sat, final, m);
        else
            put(&g->out, "ABS nvT, %s;\nFLR nvT, nvT;\nSLT nvT2, %s, nvZero;\nMUL nvT2, nvT2, nvT;\n"
                         "MAD%s %s.%s, nvT2, {-2, -2, -2, -2}, nvT;\n",
                src[0], src[0], sat, final, m);
    }
    else if (!strcmp(base, "I2F") || !strcmp(base, "F2I") || !strcmp(base, "MOV"))
        put(&g->out, "MOV%s %s.%s, %s;\n", sat, final, m, src[0]);
    else
    {
        static const char *const same[] = {"ADD", "MUL", "MAD", "DP3", "DP4", "DPH", "RSQ", "RCP", "MAX", "MIN",
                                            "SLT", "SGE", "TEX", "TXP", "TXB", "FLR", "FRC", "EX2", "LG2", "POW",
                                            "LIT", "XPD", "LRP", "CMP", "ABS", "SUB", "KIL", NULL};
        int known = 0;
        for (int i = 0; same[i]; i++)
            known |= !strcmp(base, same[i]);
        if (!known || (g->vertex && (!strcmp(base, "TEX") || !strcmp(base, "TXP") || !strcmp(base, "TXB") ||
                                     !strcmp(base, "LRP") || !strcmp(base, "CMP") || !strcmp(base, "KIL"))))
            return 0;
        int scalarOp = !strcmp(base, "RSQ") || !strcmp(base, "RCP") || !strcmp(base, "EX2") || !strcmp(base, "LG2");
        put(&g->out, "%s%s %s.%s", base, sat, final, m);
        for (int i = 0; i < nsrc; i++)
        {
            if (scalarOp || (!strcmp(base, "POW") && i < 2))
            {
                gp4Scalar(src[i], scalar, sizeof(scalar));
                put(&g->out, ", %s", scalar);
            }
            else
                put(&g->out, ", %s", src[i]);
        }
        put(&g->out, ";\n");
    }
    if (integer && (!strcmp(base, "SLT") || !strcmp(base, "SGE") || !strcmp(base, "SLE") || !strcmp(base, "SGT") ||
                    !strcmp(base, "SEQ") || !strcmp(base, "SNE")))
        put(&g->out, "MOV %s.%s, -%s;\n", final, m, final);
    if (vertexSat)
        put(&g->out, "MAX nvSat.%s, nvSat, nvZero;\nMIN %s.%s, nvSat, nvOne;\n", m, target, m);

    if (predicated)
    {
        if (cond[0])
        {
            if (!gp4Condition(g, cond))
                return 0;
        }
        else
            put(&g->out, "MOV nvCond.x, nvMask%d.x;\n", g->depth - 1);
        if (g->vertex)
            put(&g->out, "MIN nvTmp.%s, nvTmp, nvBig;\nMAX nvTmp.%s, nvTmp, -nvBig;\nADD nvTmp.%s, nvTmp, -%s;\n"
                         "MAD %s.%s, nvTmp, nvCond.x, %s;\n",
                m, m, m, reg, reg, m, reg);
        else
            put(&g->out, "CMP %s.%s, -nvCond.x, nvTmp, %s;\n", reg, m, reg);
    }
    if (updateCC && strncmp(reg, "nvCC", 4))
        put(&g->out, "MOV nvCC.%s, %s;\n", m, reg);
    return 1;
}

// Instructions, with REP loops unrolled.
static int gp4Lines(Gp4 *g, char **lines, int n)
{
    for (int i = 0; i < n; i++)
    {
        char word[32] = "";
        sscanf(lines[i], "%31s", word);
        if (strncmp(word, "REP", 3))
        {
            char line[1024];
            snprintf(line, sizeof(line), "%s", lines[i]);
            if (!gp4Instruction(g, line))
                return 0;
            continue;
        }
        // the body, to the matching ENDREP
        int end = i + 1, nesting = 1;
        for (; end < n; end++)
        {
            char w[32] = "";
            sscanf(lines[end], "%31s", w);
            if (!strncmp(w, "REP", 3))
                nesting++;
            else if (!strncmp(w, "ENDREP", 6) && --nesting == 0)
                break;
        }
        if (end == n || g->depth >= GP4_DEPTH)
            return 0;
        int count = 32;
        const char *brace = strchr(lines[i], '{');
        if (brace && (sscanf(brace + 1, "%d", &count) != 1 || count < 1 || count > 64))
            return 0;
        int level = g->depth++;
        if (level)
            put(&g->out, "MOV nvMask%d.x, nvMask%d.x;\n", level, level - 1);
        else
            put(&g->out, "MOV nvMask0.x, nvOne.x;\n");
        g->loop[g->loops++] = level;
        for (int k = 0; k < count; k++)
            if (!gp4Lines(g, lines + i + 1, end - i - 1))
                return 0;
        g->loops--;
        g->depth--;
        i = end;
    }
    return 1;
}

char *namcoEs1Gp4Translate(const char *src, int len)
{
    Gp4 g = {0};
    char *copy = malloc(len + 1), *save = NULL;
    char *lines[1024];
    int count = 0;

    if (!copy)
        return NULL;
    memcpy(copy, src, len);
    copy[len] = '\0';
    if (!strncmp(copy, "!!NVvp4.0", 9))
        g.vertex = 1;
    else if (strncmp(copy, "!!NVfp4.0", 9))
    {
        free(copy);
        return NULL;
    }

    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *s = trim(line);
        char name[64], binding[96];

        if (!*s)
            continue;
        if (!strncmp(s, "!!NV", 4))
        {
            put(&g.out, "%s\n", g.vertex ? "!!ARBvp1.0" : "!!ARBfp1.0");
            continue;
        }
        if (*s == '#')
        {
            if (!strncmp(s, "#profile gp4", 12))
                put(&g.out, "#profile %s\n", g.vertex ? "arbvp1" : "arbfp1");
            else
                put(&g.out, "%s\n", s);
            continue;
        }
        if (!strncmp(s, "OPTION", 6))
        {
            if (!g.vertex)
                put(&g.out, "%s\n", s);
            continue;
        }
        if (!strncmp(s, "ATTRIB", 6) || (!strncmp(s, "OUTPUT", 6) && strstr(s, "[] =")) || !strcmp(s, "TEMP RC, HC;") ||
            !strcmp(s, "TEMP RC;") || !strcmp(s, "TEMP HC;"))
            continue;
        if (!strncmp(s, "TEMP lmem", 9))
            goto unsupported;
        if (!g.vertex && sscanf(s, "OUTPUT %63s = %95[^;];", name, binding) == 2)
        {
            if (g.outputs == 8)
                goto unsupported;
            snprintf(g.outputName[g.outputs], sizeof(g.outputName[0]), "%s", name);
            snprintf(g.outputBinding[g.outputs], sizeof(g.outputBinding[0]), "%s", binding);
            g.outputs++;
            put(&g.out, "TEMP %s;\n", name);
            continue;
        }
        if (!strncmp(s, "TEMP", 4) || !strncmp(s, "PARAM", 5) || !strncmp(s, "OUTPUT", 6))
        {
            put(&g.out, "%s\n", s);
            continue;
        }
        if (!strcmp(s, "END"))
        {
            gp4Declare(&g);
            if (!gp4Lines(&g, lines, count) || g.depth)
                goto unsupported;
            for (int i = 0; i < g.outputs; i++)
                put(&g.out, "MOV %s, %s;\n", g.outputBinding[i], g.outputName[i]);
            put(&g.out, "END\n");
            break;
        }
        if (count == (int)(sizeof(lines) / sizeof(lines[0])))
            goto unsupported;
        lines[count++] = s;
    }
    free(copy);
    return g.out.text;

unsupported:
    free(copy);
    free(g.out.text);
    return NULL;
}

char *namcoEs1ArbTranslate(const char *src, int len)
{
    char *text = malloc(len + 1), *out = NULL;
    if (!text)
        return NULL;
    memcpy(text, src, len);
    text[len] = '\0';
    if (strstr(text, "OPTION NV_vertex_program"))
        out = vertexProgram(text);
    else if (strstr(text, "OPTION NV_fragment_program"))
        out = fragmentProgram(text);
    free(text);
    return out;
}
