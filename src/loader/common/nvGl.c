// NVIDIA's GeForce extensions (see nvGl.h), for the games that drew with
// them: the Gaelco games, Incredible Technologies'.
//
// The games draw for a GeForce 3/4/FX with NVIDIA's own extensions, which
// today's Mesa drivers do not have. They are emulated over what those have:
//   - NV_register_combiners (every lit or lightmapped surface): the
//     combiners' state is kept here, and at each draw while they are on, an
//     ARB fragment program doing the same (generated once per state, then
//     cached) is bound in their place;
//   - NV_vertex_program, NV_fragment_program, NV_occlusion_query: on ARB
//     programs and queries (nvProgram.c);
//   - NV_vertex_array_range: the game's "video memory" is plain memory;
//   - EXT_vertex_weighting (Tokyo Cop's skinned characters): the meshes
//     are blended between the two modelview matrices on the CPU at the
//     draw.
//
// NV_GL_TRACE=1 prints each fragment program made;
// NV_GL_NO_COMBINERS=1 leaves the combiners out (debugging).

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include "nvGl.h"
#include "importHook.h"
#include "../log/log.h"

// NV_register_combiners
#define GL_REGISTER_COMBINERS_NV 0x8522
#define GL_VARIABLE_A_NV 0x8523
#define GL_VARIABLE_G_NV 0x8529
#define GL_CONSTANT_COLOR0_NV 0x852A
#define GL_CONSTANT_COLOR1_NV 0x852B
#define GL_PRIMARY_COLOR_NV 0x852C
#define GL_SECONDARY_COLOR_NV 0x852D
#define GL_SPARE0_NV 0x852E
#define GL_SPARE1_NV 0x852F
#define GL_DISCARD_NV 0x8530
#define GL_E_TIMES_F_NV 0x8531
#define GL_SPARE0_PLUS_SECONDARY_COLOR_NV 0x8532
#define GL_PER_STAGE_CONSTANTS_NV 0x8535
#define GL_UNSIGNED_IDENTITY_NV 0x8536
#define GL_UNSIGNED_INVERT_NV 0x8537
#define GL_EXPAND_NORMAL_NV 0x8538
#define GL_EXPAND_NEGATE_NV 0x8539
#define GL_HALF_BIAS_NORMAL_NV 0x853A
#define GL_HALF_BIAS_NEGATE_NV 0x853B
#define GL_SIGNED_IDENTITY_NV 0x853C
#define GL_SIGNED_NEGATE_NV 0x853D
#define GL_SCALE_BY_TWO_NV 0x853E
#define GL_SCALE_BY_FOUR_NV 0x853F
#define GL_SCALE_BY_ONE_HALF_NV 0x8540
#define GL_BIAS_BY_NEGATIVE_ONE_HALF_NV 0x8541
#define GL_MAX_GENERAL_COMBINERS_NV 0x854D
#define GL_NUM_GENERAL_COMBINERS_NV 0x854E
#define GL_COLOR_SUM_CLAMP_NV 0x854F
#define GL_COMBINER0_NV 0x8550

#define GL_VERTEX_ARRAY_RANGE_NV 0x851D

#define MAX_STAGES 8
#define MAX_UNITS 4

typedef struct
{
    GLenum input, mapping, usage;
} CombinerInput;

typedef struct
{
    GLenum ab, cd, sum, scale, bias;
    GLboolean abDot, cdDot, mux;
} CombinerOutput;

// The combiners' state (NV_register_combiners' initial values).
static struct
{
    int enabled, perStageConstants;
    int stages, colorSumClamp;
    float constants[2][4];
    float stageConstants[MAX_STAGES][2][4];
    CombinerInput general[MAX_STAGES][2][4]; // stage, RGB/alpha, A-D
    CombinerOutput output[MAX_STAGES][2];
    CombinerInput final[7]; // A-G
} rc;

// The texture units' enabled targets, the fog's state: what the generated
// programs read.
static GLenum activeUnit;
static unsigned textureTargets[MAX_UNITS]; // TARGET_* bits
static int fogEnabled;
static GLenum fogMode = GL_EXP;

#define TARGET_1D 1
#define TARGET_2D 2
#define TARGET_3D 4
#define TARGET_CUBE 8

static int trace = -1;

// The program bound for the current state, 0: none made yet.
static int dirty = 1;
static int programOn;

// EXT_vertex_weighting's state (see drawBlended).
#define GL_MODELVIEW1_EXT 0x850A
#define GL_VERTEX_WEIGHTING_EXT 0x8509
#define GL_VERTEX_WEIGHT_ARRAY_EXT 0x850C

static struct
{
    int on, arrayOn, modelview1; // modelview1: the matrix mode is M1
    GLfloat m1[16];
    GLsizei stride;
    const GLfloat *weights;
    GLfloat *vertices, *normals;
    GLuint capacity;
} vw = {.m1 = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};

static struct
{
    void (*GenProgramsARB)(GLsizei, GLuint *);
    void (*BindProgramARB)(GLenum, GLuint);
    void (*ProgramStringARB)(GLenum, GLenum, GLsizei, const void *);
    void (*ProgramEnvParameter4fvARB)(GLenum, GLuint, const GLfloat *);
} gl;

static void (*realEnable)(GLenum);
static void (*realDisable)(GLenum);
static void (*realEnableClientState)(GLenum);
static void (*realDisableClientState)(GLenum);
static void (*realActiveTextureARB)(GLenum);
static void (*realFogi)(GLenum, GLint);
static void (*realFogf)(GLenum, GLfloat);
static void (*realDrawElements)(GLenum, GLsizei, GLenum, const void *);
static void (*realDrawArrays)(GLenum, GLint, GLsizei);
static void (*realDrawPixels)(GLsizei, GLsizei, GLenum, GLenum, const void *);
static void (*realCallList)(GLuint);
static void (*realNewList)(GLuint, GLenum);
static void (*realEndList)(void);
static void (*realGetIntegerv)(GLenum, GLint *);
static void (*realSwapBuffers)(void *, unsigned long);

void *(*nvGlProcAddress(void))(const unsigned char *)
{
    return (void *(*)(const unsigned char *))importTarget("glXGetProcAddressARB");
}

static int loadGl(void)
{
    static int loaded;
    void *(*getProcAddress)(const GLubyte *);

    if (loaded)
        return loaded > 0;
    getProcAddress = nvGlProcAddress();
    loaded = -1;
    if (!getProcAddress)
        return 0;
#define LOAD(name) *(void **)&gl.name = getProcAddress((const GLubyte *)"gl" #name)
    LOAD(GenProgramsARB);
    LOAD(BindProgramARB);
    LOAD(ProgramStringARB);
    LOAD(ProgramEnvParameter4fvARB);
#undef LOAD
    if (!gl.GenProgramsARB || !gl.BindProgramARB || !gl.ProgramStringARB || !gl.ProgramEnvParameter4fvARB)
    {
        log_error("NV GL: no ARB_fragment_program, the register combiners are not emulated");
        return 0;
    }
    loaded = 1;
    return 1;
}

static void resetCombiners(void)
{
    rc.stages = 1;
    rc.colorSumClamp = 1;
    for (int s = 0; s < MAX_STAGES; s++)
        for (int p = 0; p < 2; p++)
        {
            GLenum usage = p ? GL_ALPHA : GL_RGB;
            // A = primary color, B = 1 (zero inverted), C = D = 0: spare0
            // = the primary color.
            rc.general[s][p][0] = (CombinerInput){GL_PRIMARY_COLOR_NV, GL_UNSIGNED_IDENTITY_NV, usage};
            rc.general[s][p][1] = (CombinerInput){GL_ZERO, GL_UNSIGNED_INVERT_NV, usage};
            rc.general[s][p][2] = (CombinerInput){GL_ZERO, GL_UNSIGNED_IDENTITY_NV, usage};
            rc.general[s][p][3] = (CombinerInput){GL_ZERO, GL_UNSIGNED_IDENTITY_NV, usage};
            rc.output[s][p] = (CombinerOutput){GL_DISCARD_NV, GL_DISCARD_NV, GL_SPARE0_NV, GL_NONE, GL_NONE, 0, 0, 0};
        }
    // A = fog factor, B = spare0 + secondary color, C = fog color, D = 0,
    // G = spare0's alpha.
    rc.final[0] = (CombinerInput){GL_FOG, GL_UNSIGNED_IDENTITY_NV, GL_ALPHA};
    rc.final[1] = (CombinerInput){GL_SPARE0_PLUS_SECONDARY_COLOR_NV, GL_UNSIGNED_IDENTITY_NV, GL_RGB};
    rc.final[2] = (CombinerInput){GL_FOG, GL_UNSIGNED_IDENTITY_NV, GL_RGB};
    rc.final[3] = (CombinerInput){GL_ZERO, GL_UNSIGNED_IDENTITY_NV, GL_RGB};
    rc.final[4] = (CombinerInput){GL_ZERO, GL_UNSIGNED_IDENTITY_NV, GL_RGB};
    rc.final[5] = (CombinerInput){GL_ZERO, GL_UNSIGNED_IDENTITY_NV, GL_RGB};
    rc.final[6] = (CombinerInput){GL_SPARE0_NV, GL_UNSIGNED_IDENTITY_NV, GL_ALPHA};
}

// The program text being made.
typedef struct
{
    char text[16384];
    size_t length;
    unsigned textures; // units read
    int fog;
} Program;

static void emit(Program *p, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    if (p->length < sizeof(p->text))
        p->length += vsnprintf(p->text + p->length, sizeof(p->text) - p->length, format, ap);
    va_end(ap);
}

// A combiner register's value in the program: a temporary of ours, or a
// parameter.
static const char *registerName(Program *p, GLenum input, int stage)
{
    static char name[16];

    switch (input)
    {
    case GL_ZERO:
        return "{0,0,0,0}";
    case GL_CONSTANT_COLOR0_NV:
    case GL_CONSTANT_COLOR1_NV:
        if (rc.perStageConstants && stage >= 0)
            snprintf(name, sizeof(name), "sc%d%d", stage, input - GL_CONSTANT_COLOR0_NV);
        else
            snprintf(name, sizeof(name), "c%d", input - GL_CONSTANT_COLOR0_NV);
        return name;
    case GL_FOG:
        p->fog = 1;
        return "fog";
    case GL_PRIMARY_COLOR_NV:
        return "col0";
    case GL_SECONDARY_COLOR_NV:
        return "col1";
    case GL_SPARE0_NV:
        return "spare0";
    case GL_SPARE1_NV:
        return "spare1";
    case GL_E_TIMES_F_NV:
        return "ef";
    case GL_SPARE0_PLUS_SECONDARY_COLOR_NV:
        return "sps";
    }
    if (input >= GL_TEXTURE0_ARB && input < GL_TEXTURE0_ARB + MAX_UNITS)
    {
        p->textures |= 1u << (input - GL_TEXTURE0_ARB);
        snprintf(name, sizeof(name), "tex%d", input - GL_TEXTURE0_ARB);
        return name;
    }
    return "{0,0,0,0}";
}

// A variable's value (its register's component, mapped) into dst's mask
// (".xyz": an RGB portion's, ".w": an alpha portion's).
static void mapInput(Program *p, const CombinerInput *in, const char *dst, const char *mask, int stage)
{
    const char *reg = registerName(p, in->input, stage);
    const char *swizzle = in->usage == GL_ALPHA ? ".wwww" : in->usage == GL_BLUE ? ".zzzz" : "";

    // Zero, mapped: a constant (no swizzles or signs on those).
    if (in->input == GL_ZERO)
    {
        const char *value = in->mapping == GL_UNSIGNED_INVERT_NV || in->mapping == GL_EXPAND_NEGATE_NV ? "1"
                            : in->mapping == GL_EXPAND_NORMAL_NV                                    ? "-1"
                            : in->mapping == GL_HALF_BIAS_NORMAL_NV                                 ? "-0.5"
                            : in->mapping == GL_HALF_BIAS_NEGATE_NV                                 ? "0.5"
                                                                                                    : "0";
        emit(p, "MOV %s%s, {%s,%s,%s,%s};\n", dst, mask, value, value, value, value);
        return;
    }
    switch (in->mapping)
    {
    case GL_UNSIGNED_INVERT_NV:
        emit(p, "MOV_SAT %s%s, %s%s;\nSUB %s%s, {1,1,1,1}, %s;\n", dst, mask, reg, swizzle, dst, mask, dst);
        break;
    case GL_EXPAND_NORMAL_NV:
        emit(p, "MAX %s%s, %s%s, {0,0,0,0};\nMAD %s%s, %s, {2,2,2,2}, {-1,-1,-1,-1};\n", dst, mask, reg, swizzle, dst,
             mask, dst);
        break;
    case GL_EXPAND_NEGATE_NV:
        emit(p, "MAX %s%s, %s%s, {0,0,0,0};\nMAD %s%s, %s, {-2,-2,-2,-2}, {1,1,1,1};\n", dst, mask, reg, swizzle, dst,
             mask, dst);
        break;
    case GL_HALF_BIAS_NORMAL_NV:
        emit(p, "MAX %s%s, %s%s, {0,0,0,0};\nADD %s%s, %s, {-0.5,-0.5,-0.5,-0.5};\n", dst, mask, reg, swizzle, dst,
             mask, dst);
        break;
    case GL_HALF_BIAS_NEGATE_NV:
        emit(p, "MAX %s%s, %s%s, {0,0,0,0};\nSUB %s%s, {0.5,0.5,0.5,0.5}, %s;\n", dst, mask, reg, swizzle, dst, mask,
             dst);
        break;
    case GL_SIGNED_IDENTITY_NV:
        emit(p, "MOV %s%s, %s%s;\n", dst, mask, reg, swizzle);
        break;
    case GL_SIGNED_NEGATE_NV:
        emit(p, "MOV %s%s, -%s%s;\n", dst, mask, reg, swizzle);
        break;
    default: // GL_UNSIGNED_IDENTITY_NV
        emit(p, "MAX %s%s, %s%s, {0,0,0,0};\n", dst, mask, reg, swizzle);
        break;
    }
}

// An output of a stage's portion: (value + bias) * scale, clamped to
// [-1, 1], into its register.
static void writeOutput(Program *p, const CombinerOutput *out, GLenum reg, const char *value, const char *mask)
{
    const char *bias = out->bias == GL_BIAS_BY_NEGATIVE_ONE_HALF_NV ? "{-0.5,-0.5,-0.5,-0.5}" : "{0,0,0,0}";
    const char *scale = out->scale == GL_SCALE_BY_TWO_NV       ? "{2,2,2,2}"
                        : out->scale == GL_SCALE_BY_FOUR_NV    ? "{4,4,4,4}"
                        : out->scale == GL_SCALE_BY_ONE_HALF_NV ? "{0.5,0.5,0.5,0.5}"
                                                                : "{1,1,1,1}";

    if (reg == GL_DISCARD_NV)
        return;
    emit(p, "ADD r%s, %s, %s;\nMUL r%s, r, %s;\nMIN r%s, r, {1,1,1,1};\nMAX %s%s, r, {-1,-1,-1,-1};\n", mask, value,
         bias, mask, scale, mask, registerName(p, reg, -1), mask);
}

static void generalStage(Program *p, int s)
{
    static const char *const vars[4] = {"va", "vb", "vc", "vd"};
    static const char *const masks[2] = {".xyz", ".w"};

    // Every input of the stage is read before any of its outputs is written.
    for (int portion = 0; portion < 2; portion++)
        for (int v = 0; v < 4; v++)
            mapInput(p, &rc.general[s][portion][v], vars[v], masks[portion], s);
    for (int portion = 0; portion < 2; portion++)
    {
        const CombinerOutput *out = &rc.output[s][portion];
        const char *m = masks[portion];
        int rgb = portion == 0;

        emit(p, "%s ab%s, va, vb;\n", rgb && out->abDot ? "DP3" : "MUL", m);
        emit(p, "%s cd%s, vc, vd;\n", rgb && out->cdDot ? "DP3" : "MUL", m);
        if (out->mux)
            // spare0's alpha at least 0.5: CD, else AB.
            emit(p, "SUB r.x, {0.5,0.5,0.5,0.5}, spare0.w;\nCMP sum%s, r.x, cd, ab;\n", m);
        else
            emit(p, "ADD sum%s, ab, cd;\n", m);
    }
    for (int portion = 0; portion < 2; portion++)
    {
        const CombinerOutput *out = &rc.output[s][portion];
        writeOutput(p, out, out->ab, "ab", masks[portion]);
        writeOutput(p, out, out->cd, "cd", masks[portion]);
        writeOutput(p, out, out->sum, "sum", masks[portion]);
    }
}

// The texture target a unit samples with, NULL when none is enabled.
static const char *unitTarget(int unit)
{
    unsigned t = textureTargets[unit];

    return t & TARGET_CUBE ? "CUBE" : t & TARGET_3D ? "3D" : t & TARGET_2D ? "2D" : t & TARGET_1D ? "1D" : NULL;
}

static void generate(Program *p)
{
    Program body = {0};
    static const char *const finalVars[7] = {"fa", "fb", "fc", "fd", "fe", "ff", "fg"};

    // The combiners themselves first: they tell which units and registers
    // are read.
    for (int s = 0; s < rc.stages && s < MAX_STAGES; s++)
        generalStage(&body, s);
    mapInput(&body, &rc.final[4], "fe", ".xyz", -1);
    mapInput(&body, &rc.final[5], "ff", ".xyz", -1);
    emit(&body, "MUL ef.xyz, fe, ff;\n");
    emit(&body, "ADD%s sps.xyz, spare0, col1;\n", rc.colorSumClamp ? "_SAT" : "");
    for (int v = 0; v < 4; v++)
        mapInput(&body, &rc.final[v], finalVars[v], ".xyz", -1);
    mapInput(&body, &rc.final[6], "fg", ".w", -1);
    emit(&body, "LRP r.xyz, fa, fb, fc;\nADD r.xyz, r, fd;\nMOV r.w, fg.w;\nMOV_SAT result.color, r;\nEND\n");

    p->length = 0;
    emit(p, "!!ARBfp1.0\n");
    emit(p, "PARAM c0 = program.env[0];\nPARAM c1 = program.env[1];\n");
    if (rc.perStageConstants)
        for (int s = 0; s < MAX_STAGES; s++)
            emit(p, "PARAM sc%d0 = program.env[%d];\nPARAM sc%d1 = program.env[%d];\n", s, 2 + s * 2, s, 3 + s * 2);
    emit(p, "TEMP col0, col1, spare0, spare1, fog, tex0, tex1, tex2, tex3;\n");
    emit(p, "TEMP va, vb, vc, vd, ab, cd, sum, r, fa, fb, fc, fd, fe, ff, fg, ef, sps;\n");
    emit(p, "MOV col0, fragment.color.primary;\nMOV col1, fragment.color.secondary;\n");
    for (int u = 0; u < MAX_UNITS; u++)
    {
        const char *target = unitTarget(u);
        if (!(body.textures & (1u << u)) && !(u == 0 && target))
            continue;
        if (!target)
            emit(p, "MOV tex%d, {0,0,0,0};\n", u);
        else
            emit(p, "%s tex%d, fragment.texcoord[%d], texture[%d], %s;\n", !strcmp(target, "CUBE") ? "TEX" : "TXP", u,
                 u, u, target);
    }
    // spare0's alpha starts as texture 0's.
    emit(p, "MOV spare0, {0,0,0,0};\nMOV spare1, {0,0,0,0};\n");
    if (unitTarget(0))
        emit(p, "MOV spare0.w, tex0.w;\n");
    if (body.fog)
    {
        // The fog register: its color, and the fog factor of the
        // fragment's fog coordinate (1 with the fog off).
        emit(p, "MOV fog.xyz, state.fog.color;\n");
        if (!fogEnabled)
            emit(p, "MOV fog.w, {1,1,1,1};\n");
        else if (fogMode == GL_LINEAR)
            emit(p, "SUB r.x, state.fog.params.z, fragment.fogcoord.x;\nMUL_SAT fog.w, r.x, state.fog.params.w;\n");
        else if (fogMode == GL_EXP2)
            emit(p, "MUL r.x, state.fog.params.x, fragment.fogcoord.x;\nMUL r.x, r.x, r.x;\n"
                    "MUL r.x, r.x, {-1.442695,-1.442695,-1.442695,-1.442695};\nEX2_SAT fog.w, r.x;\n");
        else
            emit(p, "MUL r.x, state.fog.params.x, fragment.fogcoord.x;\n"
                    "MUL r.x, r.x, {-1.442695,-1.442695,-1.442695,-1.442695};\nEX2_SAT fog.w, r.x;\n");
    }
    emit(p, "%s", body.text);
}

// The programs made, by their text.
typedef struct CachedProgram
{
    char *text;
    GLuint id;
    struct CachedProgram *next;
} CachedProgram;

static CachedProgram *programs;

static GLuint programFor(const char *text)
{
    CachedProgram *c;

    for (c = programs; c; c = c->next)
        if (!strcmp(c->text, text))
            return c->id;
    c = calloc(1, sizeof(*c));
    if (!c)
        return 0;
    c->text = strdup(text);
    while (glGetError() != GL_NO_ERROR)
        ;
    gl.GenProgramsARB(1, &c->id);
    gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, c->id);
    gl.ProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(text), text);
    if (glGetError() != GL_NO_ERROR)
    {
        const char *error = (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
        log_error("NV GL: combiner program rejected: %s\n%s", error ? error : "?", text);
    }
    else if (trace)
        printf("NV GL: combiner program %u:\n%s", c->id, text);
    c->next = programs;
    programs = c;
    return c->id;
}

// Before a draw: the program standing for the combiners, or none.
static void applyCombiners(void)
{
    static Program program;

    if (trace < 0)
        trace = getenv("NV_GL_TRACE") != NULL;
    // The game's NV fragment program, over the combiners.
    if (nvProgramBeforeDraw())
    {
        if (!programOn)
        {
            realEnable(GL_FRAGMENT_PROGRAM_ARB);
            programOn = 1;
        }
        dirty = 1;
        return;
    }
    if (!rc.enabled || !loadGl() || getenv("NV_GL_NO_COMBINERS"))
    {
        if (programOn)
        {
            realDisable(GL_FRAGMENT_PROGRAM_ARB);
            programOn = 0;
        }
        return;
    }
    if (dirty)
    {
        generate(&program);
        gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, programFor(program.text));
        dirty = 0;
    }
    if (!programOn)
    {
        realEnable(GL_FRAGMENT_PROGRAM_ARB);
        programOn = 1;
    }
}

static unsigned targetBit(GLenum cap)
{
    switch (cap)
    {
    case GL_TEXTURE_1D:
        return TARGET_1D;
    case GL_TEXTURE_2D:
        return TARGET_2D;
    case GL_TEXTURE_3D:
        return TARGET_3D;
    case GL_TEXTURE_CUBE_MAP_ARB:
        return TARGET_CUBE;
    }
    return 0;
}

// Display lists: the game's shaders keep their state (combiners, enables,
// programs) in lists they call before drawing. Our part of what a list
// holds is kept here while it is compiled, and done again each time it is
// called; the GL's own part is the GL's.
typedef struct
{
    void (*apply)(const uint32_t *);
    uint32_t args[10];
} Recorded;

typedef struct List
{
    GLuint id;
    Recorded *calls;
    int count, size;
    struct List *next;
} List;

static List *lists;
static List *compiling;
static int compileOnly; // GL_COMPILE: nothing is done now

static List *findList(GLuint id, int create)
{
    for (List *l = lists; l; l = l->next)
        if (l->id == id)
            return l;
    if (!create)
        return NULL;
    List *l = calloc(1, sizeof(*l));
    if (l)
    {
        l->id = id;
        l->next = lists;
        lists = l;
    }
    return l;
}

int nvGlDefer(void (*apply)(const uint32_t *), const uint32_t *args, int count)
{
    if (!compiling)
        return 0;
    if (compiling->count == compiling->size)
    {
        int size = compiling->size ? compiling->size * 2 : 16;
        Recorded *calls = realloc(compiling->calls, size * sizeof(*calls));
        if (!calls)
            return compileOnly;
        compiling->calls = calls;
        compiling->size = size;
    }
    compiling->calls[compiling->count].apply = apply;
    memcpy(compiling->calls[compiling->count].args, args, count * sizeof(*args));
    compiling->count++;
    return compileOnly;
}

// A call of ours: kept in the list being compiled, done unless the list is
// only compiled.
#define DEFER(apply, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        const uint32_t args_[] = {__VA_ARGS__};                                                                        \
        if (!nvGlDefer(apply, args_, sizeof(args_) / sizeof(args_[0])))                                            \
            apply(args_);                                                                                              \
    } while (0)

static uint32_t floatBits(GLfloat f)
{
    uint32_t u;

    memcpy(&u, &f, sizeof(u));
    return u;
}

static GLfloat bitsFloat(uint32_t u)
{
    GLfloat f;

    memcpy(&f, &u, sizeof(f));
    return f;
}

// cap enabled (on) or disabled: our state of it.
static void applyEnable(const uint32_t *a)
{
    GLenum cap = a[0];
    int on = (int)a[1];
    unsigned bit = targetBit(cap);

    if (cap == GL_REGISTER_COMBINERS_NV)
        rc.enabled = on;
    else if (cap == GL_PER_STAGE_CONSTANTS_NV)
        rc.perStageConstants = on;
    else if (cap == GL_FOG)
        fogEnabled = on;
    else if (bit && activeUnit < MAX_UNITS)
        textureTargets[activeUnit] = on ? textureTargets[activeUnit] | bit : textureTargets[activeUnit] & ~bit;
    else
    {
        nvProgramEnable(cap, on);
        return;
    }
    dirty = 1;
}

// Our caps, not the GL's: register combiners, per-stage constants, NV
// fragment programs.
static int ourCap(GLenum cap)
{
    return cap == GL_REGISTER_COMBINERS_NV || cap == GL_PER_STAGE_CONSTANTS_NV || cap == 0x8870;
}

__attribute__((force_align_arg_pointer)) static void enable(GLenum cap)
{
    if (cap == GL_VERTEX_WEIGHTING_EXT)
    {
        vw.on = 1;
        return;
    }
    DEFER(applyEnable, cap, 1);
    if (!ourCap(cap))
        realEnable(cap);
}

__attribute__((force_align_arg_pointer)) static void disable(GLenum cap)
{
    if (cap == GL_VERTEX_WEIGHTING_EXT)
    {
        vw.on = 0;
        return;
    }
    DEFER(applyEnable, cap, 0);
    if (!ourCap(cap))
        realDisable(cap);
}

static void applyActiveTexture(const uint32_t *a)
{
    activeUnit = a[0] - GL_TEXTURE0_ARB;
}

__attribute__((force_align_arg_pointer)) static void activeTextureARB(GLenum texture)
{
    DEFER(applyActiveTexture, texture);
    realActiveTextureARB(texture);
}

static void applyFogMode(const uint32_t *a)
{
    fogMode = a[0];
    dirty = 1;
}

__attribute__((force_align_arg_pointer)) static void fogi(GLenum pname, GLint param)
{
    if (pname == GL_FOG_MODE)
        DEFER(applyFogMode, (uint32_t)param);
    realFogi(pname, param);
}

__attribute__((force_align_arg_pointer)) static void fogf(GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_MODE)
        DEFER(applyFogMode, (uint32_t)param);
    realFogf(pname, param);
}

static void applyCombinerParameter(const uint32_t *a)
{
    switch (a[0])
    {
    case GL_CONSTANT_COLOR0_NV:
    case GL_CONSTANT_COLOR1_NV:
        for (int i = 0; i < 4; i++)
            rc.constants[a[0] - GL_CONSTANT_COLOR0_NV][i] = bitsFloat(a[1 + i]);
        break;
    case GL_NUM_GENERAL_COMBINERS_NV:
        rc.stages = (int)bitsFloat(a[1]);
        dirty = 1;
        break;
    case GL_COLOR_SUM_CLAMP_NV:
        rc.colorSumClamp = bitsFloat(a[1]) != 0;
        dirty = 1;
        break;
    }
}

__attribute__((force_align_arg_pointer)) static void combinerParameterfvNV(GLenum pname, const GLfloat *params)
{
    int vector = pname == GL_CONSTANT_COLOR0_NV || pname == GL_CONSTANT_COLOR1_NV;

    DEFER(applyCombinerParameter, pname, floatBits(params[0]), floatBits(vector ? params[1] : 0),
          floatBits(vector ? params[2] : 0), floatBits(vector ? params[3] : 0));
    // The constants are the programs' environment (which the GL keeps in
    // a list).
    if (vector && loadGl())
        gl.ProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, pname - GL_CONSTANT_COLOR0_NV, params);
}

__attribute__((force_align_arg_pointer)) static void combinerParameteriNV(GLenum pname, GLint param)
{
    GLfloat value = (GLfloat)param;

    if (pname == GL_NUM_GENERAL_COMBINERS_NV || pname == GL_COLOR_SUM_CLAMP_NV)
        combinerParameterfvNV(pname, &value);
}

__attribute__((force_align_arg_pointer)) static void combinerStageParameterfvNV(GLenum stage, GLenum pname,
                                                                                 const GLfloat *params)
{
    int s = stage - GL_COMBINER0_NV, i = pname - GL_CONSTANT_COLOR0_NV;

    if (s < 0 || s >= MAX_STAGES || i < 0 || i > 1)
        return;
    if (loadGl())
        gl.ProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 2 + s * 2 + i, params);
}

static void applyCombinerInput(const uint32_t *a)
{
    int s = a[0] - GL_COMBINER0_NV, v = a[2] - GL_VARIABLE_A_NV;

    if (s < 0 || s >= MAX_STAGES || v < 0 || v > 3)
        return;
    rc.general[s][a[1] == GL_ALPHA][v] = (CombinerInput){a[3], a[4], a[5]};
    dirty = 1;
}

__attribute__((force_align_arg_pointer)) static void combinerInputNV(GLenum stage, GLenum portion, GLenum variable,
                                                                      GLenum input, GLenum mapping, GLenum usage)
{
    DEFER(applyCombinerInput, stage, portion, variable, input, mapping, usage);
}

static void applyCombinerOutput(const uint32_t *a)
{
    int s = a[0] - GL_COMBINER0_NV;

    if (s < 0 || s >= MAX_STAGES)
        return;
    rc.output[s][a[1] == GL_ALPHA] = (CombinerOutput){a[2], a[3], a[4], a[5], a[6], (GLboolean)a[7],
                                                       (GLboolean)a[8], (GLboolean)a[9]};
    dirty = 1;
}

__attribute__((force_align_arg_pointer)) static void combinerOutputNV(GLenum stage, GLenum portion, GLenum abOutput,
                                                                       GLenum cdOutput, GLenum sumOutput, GLenum scale,
                                                                       GLenum bias, GLboolean abDot, GLboolean cdDot,
                                                                       GLboolean mux)
{
    DEFER(applyCombinerOutput, stage, portion, abOutput, cdOutput, sumOutput, scale, bias, abDot, cdDot, mux);
}

static void applyFinalInput(const uint32_t *a)
{
    int v = a[0] - GL_VARIABLE_A_NV;

    if (v < 0 || v > 6)
        return;
    rc.final[v] = (CombinerInput){a[1], a[2], a[3]};
    dirty = 1;
}

__attribute__((force_align_arg_pointer)) static void finalCombinerInputNV(GLenum variable, GLenum input,
                                                                           GLenum mapping, GLenum usage)
{
    DEFER(applyFinalInput, variable, input, mapping, usage);
}

__attribute__((force_align_arg_pointer)) static void newList(GLuint id, GLenum mode)
{
    compiling = findList(id, 1);
    if (compiling)
        compiling->count = 0;
    compileOnly = mode == GL_COMPILE;
    realNewList(id, mode);
}

__attribute__((force_align_arg_pointer)) static void endList(void)
{
    compiling = NULL;
    realEndList();
}

// EXT_vertex_weighting: a vertex's eye position is w * M0 * v + (1 - w) *
// M1 * v, M0 the modelview, M1 the second modelview (GL_MODELVIEW1_EXT),
// w its weight; its normal the same with the matrices' inverse transposes.
// Mesa has no such thing: at a draw with the weighting on, the vertices
// and normals are blended into M0's object space, v' = w * v + (1 - w) *
// B * v (B = M0^-1 * M1), n' = w * n + (1 - w) * B^-T * n, and drawn with
// M0. The game sets M1 with glLoadMatrixf and glMultMatrixf only.
static void (*realMatrixMode)(GLenum);
static void (*realLoadMatrixf)(const GLfloat *);
static void (*realMultMatrixf)(const GLfloat *);
static void (*realLoadIdentity)(void);

// Column-major 4x4: r = a * b.
static void multiply(GLfloat *r, const GLfloat *a, const GLfloat *b)
{
    GLfloat t[16];

    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            t[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] + a[12 + i] * b[c * 4 + 3];
    memcpy(r, t, sizeof(t));
}

// An affine matrix's inverse (the modelviews are).
static int invertAffine(GLfloat *r, const GLfloat *m)
{
    double det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
                 m[8] * (m[1] * m[6] - m[5] * m[2]);

    if (det == 0)
        return 0;
    double d = 1 / det;
    r[0] = (GLfloat)((m[5] * m[10] - m[9] * m[6]) * d);
    r[1] = (GLfloat)((m[9] * m[2] - m[1] * m[10]) * d);
    r[2] = (GLfloat)((m[1] * m[6] - m[5] * m[2]) * d);
    r[4] = (GLfloat)((m[8] * m[6] - m[4] * m[10]) * d);
    r[5] = (GLfloat)((m[0] * m[10] - m[8] * m[2]) * d);
    r[6] = (GLfloat)((m[4] * m[2] - m[0] * m[6]) * d);
    r[8] = (GLfloat)((m[4] * m[9] - m[8] * m[5]) * d);
    r[9] = (GLfloat)((m[8] * m[1] - m[0] * m[9]) * d);
    r[10] = (GLfloat)((m[0] * m[5] - m[4] * m[1]) * d);
    r[3] = r[7] = r[11] = 0;
    r[12] = -(r[0] * m[12] + r[4] * m[13] + r[8] * m[14]);
    r[13] = -(r[1] * m[12] + r[5] * m[13] + r[9] * m[14]);
    r[14] = -(r[2] * m[12] + r[6] * m[13] + r[10] * m[14]);
    r[15] = 1;
    return 1;
}

__attribute__((force_align_arg_pointer)) static void matrixMode(GLenum mode)
{
    vw.modelview1 = mode == GL_MODELVIEW1_EXT;
    realMatrixMode(vw.modelview1 ? GL_MODELVIEW : mode);
}

__attribute__((force_align_arg_pointer)) static void loadMatrixf(const GLfloat *m)
{
    if (vw.modelview1)
        memcpy(vw.m1, m, sizeof(vw.m1));
    else
        realLoadMatrixf(m);
}

__attribute__((force_align_arg_pointer)) static void multMatrixf(const GLfloat *m)
{
    if (vw.modelview1)
        multiply(vw.m1, vw.m1, m);
    else
        realMultMatrixf(m);
}

__attribute__((force_align_arg_pointer)) static void loadIdentity(void)
{
    static const GLfloat identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    if (vw.modelview1)
        memcpy(vw.m1, identity, sizeof(vw.m1));
    else
        realLoadIdentity();
}

// The draw with its vertices blended (see above) into our arrays, bound in
// place of the game's for it: 1, or 0 when it cannot be done (the caller
// draws it as it is).
static int drawBlended(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    GLint size = 0, arrayType = 0, stride = 0, normalType = 0, normalStride = 0;
    const char *vertices = NULL, *normals = NULL;
    GLuint first = ~0u, last = 0;

    if (!vw.on || !vw.arrayOn || !vw.weights || count <= 0 || !glIsEnabled(GL_VERTEX_ARRAY))
        return 0;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &size);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE, &arrayType);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &stride);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, (void **)&vertices);
    if (size != 3 || arrayType != GL_FLOAT || !vertices)
        return 0;
    if (!stride)
        stride = 3 * sizeof(GLfloat);
    if (glIsEnabled(GL_NORMAL_ARRAY))
    {
        glGetIntegerv(GL_NORMAL_ARRAY_TYPE, &normalType);
        glGetIntegerv(GL_NORMAL_ARRAY_STRIDE, &normalStride);
        glGetPointerv(GL_NORMAL_ARRAY_POINTER, (void **)&normals);
        if (normalType != GL_FLOAT)
            normals = NULL;
        if (!normalStride)
            normalStride = 3 * sizeof(GLfloat);
    }

    for (GLsizei i = 0; i < count; i++)
    {
        GLuint index = type == GL_UNSIGNED_SHORT  ? ((const GLushort *)indices)[i]
                       : type == GL_UNSIGNED_INT ? ((const GLuint *)indices)[i]
                                                 : ((const GLubyte *)indices)[i];
        first = index < first ? index : first;
        last = index > last ? index : last;
    }
    if (last + 1 > vw.capacity)
    {
        GLfloat *v = realloc(vw.vertices, (last + 1) * 3 * sizeof(GLfloat));
        GLfloat *n = v ? realloc(vw.normals, (last + 1) * 3 * sizeof(GLfloat)) : NULL;

        if (v)
            vw.vertices = v;
        if (n)
            vw.normals = n;
        if (!v || !n)
            return 0;
        vw.capacity = last + 1;
    }

    // B = M0^-1 * M1, and B^-T for the normals.
    GLfloat m0[16], inverse[16], b[16], bInverse[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m0);
    if (!invertAffine(inverse, m0))
        return 0;
    multiply(b, inverse, vw.m1);
    if (!invertAffine(bInverse, b))
        return 0;

    for (GLuint i = first; i <= last; i++)
    {
        const GLfloat *v = (const GLfloat *)(vertices + (size_t)i * stride);
        GLfloat w = *(const GLfloat *)((const char *)vw.weights + (size_t)i * vw.stride), u = 1 - w;
        GLfloat *out = vw.vertices + i * 3;

        for (int r = 0; r < 3; r++)
            out[r] = w * v[r] + u * (b[r] * v[0] + b[4 + r] * v[1] + b[8 + r] * v[2] + b[12 + r]);
        if (normals)
        {
            const GLfloat *n = (const GLfloat *)(normals + (size_t)i * normalStride);
            out = vw.normals + i * 3;
            // (B^-T * n: the rows of B^-1)
            for (int r = 0; r < 3; r++)
                out[r] = w * n[r] + u * (bInverse[r * 4] * n[0] + bInverse[r * 4 + 1] * n[1] + bInverse[r * 4 + 2] * n[2]);
        }
    }
    glVertexPointer(3, GL_FLOAT, 0, vw.vertices);
    if (normals)
        glNormalPointer(GL_FLOAT, 0, vw.normals);
    realDrawElements(mode, count, type, indices);
    glVertexPointer(3, GL_FLOAT, stride, vertices);
    if (normals)
        glNormalPointer(GL_FLOAT, normalStride, normals);
    return 1;
}

__attribute__((force_align_arg_pointer)) static void drawElements(GLenum mode, GLsizei count, GLenum type,
                                                                   const void *indices)
{
    applyCombiners();
    if (!drawBlended(mode, count, type, indices))
        realDrawElements(mode, count, type, indices);
}

__attribute__((force_align_arg_pointer)) static void drawArrays(GLenum mode, GLint first, GLsizei count)
{
    applyCombiners();
    realDrawArrays(mode, first, count);
}

__attribute__((force_align_arg_pointer)) static void drawPixels(GLsizei width, GLsizei height, GLenum format,
                                                                 GLenum type, const void *pixels)
{
    applyCombiners();
    realDrawPixels(width, height, format, type, pixels);
}

__attribute__((force_align_arg_pointer)) static void callList(GLuint list)
{
    List *l = compiling ? NULL : findList(list, 0);

    for (int i = 0; l && i < l->count; i++)
        l->calls[i].apply(l->calls[i].args);
    applyCombiners();
    realCallList(list);
}

__attribute__((force_align_arg_pointer)) static void getIntegerv(GLenum pname, GLint *params)
{
    if (pname == GL_MAX_GENERAL_COMBINERS_NV)
    {
        *params = MAX_STAGES;
        return;
    }
    realGetIntegerv(pname, params);
}

// The loader draws its own frame at the swap: no program of ours then.
__attribute__((force_align_arg_pointer)) static void swapBuffers(void *display, unsigned long drawable)
{
    int vertexProgram = nvVertexProgramOn();

    if (programOn)
    {
        realDisable(GL_FRAGMENT_PROGRAM_ARB);
        programOn = 0;
    }
    if (vertexProgram)
        realDisable(GL_VERTEX_PROGRAM_ARB);
    realSwapBuffers(display, drawable);
    if (vertexProgram)
        realEnable(GL_VERTEX_PROGRAM_ARB);
}

// NV_vertex_array_range: the game's vertex memory (AGP or video memory,
// glXAllocateMemoryNV) is plain memory; its range is no more than a hint.
__attribute__((force_align_arg_pointer)) static void *allocateMemoryNV(GLsizei size, GLfloat readFrequency,
                                                                        GLfloat writeFrequency, GLfloat priority)
{
    void *memory = NULL;

    (void)readFrequency;
    (void)writeFrequency;
    (void)priority;
    if (posix_memalign(&memory, 4096, size))
        return NULL;
    return memory;
}

__attribute__((force_align_arg_pointer)) static void freeMemoryNV(void *pointer)
{
    free(pointer);
}

__attribute__((force_align_arg_pointer)) static void vertexArrayRangeNV(GLsizei length, const void *pointer)
{
    (void)length;
    (void)pointer;
}

__attribute__((force_align_arg_pointer)) static void enableClientState(GLenum array)
{
    if (array == GL_VERTEX_WEIGHT_ARRAY_EXT)
        vw.arrayOn = 1;
    else if (array != GL_VERTEX_ARRAY_RANGE_NV)
        realEnableClientState(array);
}

__attribute__((force_align_arg_pointer)) static void disableClientState(GLenum array)
{
    if (array == GL_VERTEX_WEIGHT_ARRAY_EXT)
        vw.arrayOn = 0;
    else if (array != GL_VERTEX_ARRAY_RANGE_NV)
        realDisableClientState(array);
}

__attribute__((force_align_arg_pointer)) static void vertexWeightPointerEXT(GLint size, GLenum type, GLsizei stride,
                                                                             const void *pointer)
{
    // (Tokyo Cop's: one float)
    vw.weights = size == 1 && type == GL_FLOAT ? pointer : NULL;
    vw.stride = stride ? stride : sizeof(GLfloat);
}

static void hookImport(const char *name, void *replacement)
{
    hookExecutableImport(name, replacement);
}

// An import of the game's, replaced: real is what it reached, the loader's
// own function of that name when there is one (glEnable, glXSwapBuffers...).
#define HOOK(name, replacement, real)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (hookExecutableImport(name, (void *)(replacement)))                                                         \
            *(void **)&(real) = importTarget(name);                                                                    \
    } while (0)

void nvGlInstall(void)
{
    resetCombiners();

    hookExecutableImport("glXAllocateMemoryNV", (void *)allocateMemoryNV);
    hookExecutableImport("glXFreeMemoryNV", (void *)freeMemoryNV);
    hookExecutableImport("glVertexArrayRangeNV", (void *)vertexArrayRangeNV);
    hookExecutableImport("glVertexWeightPointerEXT", (void *)vertexWeightPointerEXT);
    HOOK("glEnableClientState", enableClientState, realEnableClientState);
    HOOK("glDisableClientState", disableClientState, realDisableClientState);
    HOOK("glMatrixMode", matrixMode, realMatrixMode);
    HOOK("glLoadMatrixf", loadMatrixf, realLoadMatrixf);
    HOOK("glMultMatrixf", multMatrixf, realMultMatrixf);
    HOOK("glLoadIdentity", loadIdentity, realLoadIdentity);

    hookExecutableImport("glCombinerInputNV", (void *)combinerInputNV);
    hookExecutableImport("glCombinerOutputNV", (void *)combinerOutputNV);
    hookExecutableImport("glFinalCombinerInputNV", (void *)finalCombinerInputNV);
    hookExecutableImport("glCombinerParameterfvNV", (void *)combinerParameterfvNV);
    hookExecutableImport("glCombinerParameteriNV", (void *)combinerParameteriNV);
    hookExecutableImport("glCombinerStageParameterfvNV", (void *)combinerStageParameterfvNV);
    HOOK("glEnable", enable, realEnable);
    HOOK("glDisable", disable, realDisable);
    HOOK("glActiveTextureARB", activeTextureARB, realActiveTextureARB);
    HOOK("glFogi", fogi, realFogi);
    HOOK("glFogf", fogf, realFogf);
    HOOK("glDrawElements", drawElements, realDrawElements);
    HOOK("glDrawArrays", drawArrays, realDrawArrays);
    HOOK("glDrawPixels", drawPixels, realDrawPixels);
    HOOK("glCallList", callList, realCallList);
    HOOK("glNewList", newList, realNewList);
    HOOK("glEndList", endList, realEndList);
    HOOK("glGetIntegerv", getIntegerv, realGetIntegerv);
    HOOK("glXSwapBuffers", swapBuffers, realSwapBuffers);
    nvProgramInstall(hookImport);
}
