// NVIDIA's program extensions (see nvGl.h), on ARB
// programs: NV_vertex_program (Ring Riders' and Tuning Race's skinned and
// lit objects, VP1.0 and VP1.1), NV_fragment_program (Tuning Race's car
// paint, FP1.0) and NV_occlusion_query.
//
// A program is rewritten into ARB assembly when the game loads it, under
// the game's name (the program namespaces are shared):
//   - vertex attributes v[OPOS], v[NRML], v[COL0], v[TEXn]... are the
//     conventional arrays the game feeds (vertex.position, vertex.normal,
//     ...), results o[HPOS], o[COL0], o[TEXn]... ARB's;
//   - the constants c[] are the vertex program environment, which
//     glProgramParameter4fNV writes; glTrackMatrixNV's matrices are written
//     there before each draw;
//   - fragment programs lose their precision suffixes (R, H, X); their
//     registers f[TEXn], f[COL0]... are ARB's fragment attributes, their
//     named parameters (DECLARE) local parameters.
//
// NV_GL_TRACE=1 prints the programs made; NV_GL_NO_VP=1 leaves the
// vertex programs out (debugging).

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include "nvGl.h"
#include "../log/log.h"

#define GL_VERTEX_PROGRAM_NV 0x8620
#define GL_VERTEX_STATE_PROGRAM_NV 0x8621
#define GL_MODELVIEW_PROJECTION_NV 0x8629
#define GL_IDENTITY_NV 0x862A
#define GL_INVERSE_NV 0x862B
#define GL_TRANSPOSE_NV 0x862C
#define GL_INVERSE_TRANSPOSE_NV 0x862D
#define GL_MATRIX0_NV 0x8630
#define GL_FRAGMENT_PROGRAM_NV 0x8870

#define MAX_TRACKED 24 // c[0..95], four rows each

static struct
{
    void (*GenProgramsARB)(GLsizei, GLuint *);
    void (*BindProgramARB)(GLenum, GLuint);
    void (*ProgramStringARB)(GLenum, GLenum, GLsizei, const void *);
    void (*ProgramEnvParameter4fARB)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*ProgramEnvParameter4fvARB)(GLenum, GLuint, const GLfloat *);
    void (*GenQueriesARB)(GLsizei, GLuint *);
    void (*BeginQueryARB)(GLenum, GLuint);
    void (*EndQueryARB)(GLenum);
    void (*GetQueryObjectuivARB)(GLuint, GLenum, GLuint *);
} gl;

static int trace = -1;

// glTrackMatrixNV's state: the matrix tracked at c[4 * i], and how.
static struct
{
    GLenum matrix, transform;
} tracked[MAX_TRACKED];
static int anyTracked;

static int vertexProgramOn;
static int fragmentProgramOn;
static GLuint fragmentProgram; // the game's bound NV fragment program

static int loadGl(void)
{
    static int loaded;
    void *(*getProcAddress)(const GLubyte *);

    if (loaded)
        return loaded > 0;
    loaded = -1;
    if (!(getProcAddress = (void *(*)(const GLubyte *))nvGlProcAddress()))
        return 0;
#define LOAD(name) *(void **)&gl.name = getProcAddress((const GLubyte *)"gl" #name)
    LOAD(GenProgramsARB);
    LOAD(BindProgramARB);
    LOAD(ProgramStringARB);
    LOAD(ProgramEnvParameter4fARB);
    LOAD(ProgramEnvParameter4fvARB);
    LOAD(GenQueriesARB);
    LOAD(BeginQueryARB);
    LOAD(EndQueryARB);
    LOAD(GetQueryObjectuivARB);
#undef LOAD
    if (!gl.GenProgramsARB || !gl.ProgramStringARB || !gl.GenQueriesARB)
    {
        log_error("NV GL: no ARB programs, the NV programs are not emulated");
        return 0;
    }
    loaded = 1;
    return 1;
}

typedef struct
{
    char text[32768];
    size_t length;
} Out;

static void emit(Out *o, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    if (o->length < sizeof(o->text))
        o->length += vsnprintf(o->text + o->length, sizeof(o->text) - o->length, format, ap);
    va_end(ap);
}

// A vertex attribute's ARB binding: v[OPOS] or v[0]...
static const char *vertexAttribute(const char *name)
{
    static char buf[32];
    static const char *const named[][2] = {
        {"OPOS", "vertex.position"}, {"WGHT", "vertex.attrib[1]"}, {"NRML", "vertex.normal"},
        {"COL0", "vertex.color"},    {"COL1", "vertex.color.secondary"}, {"FOGC", "vertex.fogcoord"},
    };
    int n;

    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
        if (!strcmp(name, named[i][0]))
            return named[i][1];
    if (!strncmp(name, "TEX", 3))
    {
        snprintf(buf, sizeof(buf), "vertex.texcoord[%d]", atoi(name + 3));
        return buf;
    }
    n = atoi(name);
    if (n >= 8)
        snprintf(buf, sizeof(buf), "vertex.texcoord[%d]", n - 8);
    else
    {
        static const char *const numbered[8] = {"vertex.position", "vertex.attrib[1]", "vertex.normal",
                                                "vertex.color",    "vertex.color.secondary", "vertex.fogcoord",
                                                "vertex.attrib[6]", "vertex.attrib[7]"};
        return numbered[n & 7];
    }
    return buf;
}

static const char *vertexResult(const char *name)
{
    static char buf[32];
    static const char *const named[][2] = {
        {"HPOS", "result.position"},           {"COL0", "result.color.primary"},
        {"COL1", "result.color.secondary"},    {"BFC0", "result.color.back.primary"},
        {"BFC1", "result.color.back.secondary"}, {"FOGC", "result.fogcoord"},
        {"PSIZ", "result.pointsize"},
    };

    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
        if (!strcmp(name, named[i][0]))
            return named[i][1];
    snprintf(buf, sizeof(buf), "result.texcoord[%d]", atoi(name + 3));
    return buf;
}

static const char *fragmentAttribute(const char *name)
{
    static char buf[32];

    if (!strcmp(name, "WPOS"))
        return "fragment.position";
    if (!strcmp(name, "COL0"))
        return "fragment.color.primary";
    if (!strcmp(name, "COL1"))
        return "fragment.color.secondary";
    if (!strcmp(name, "FOGC"))
        return "fragment.fogcoord";
    snprintf(buf, sizeof(buf), "fragment.texcoord[%d]", atoi(name + 3));
    return buf;
}

// NV_vertex_program (VP1.0, VP1.1) to ARB_vertex_program.
static void translateVertex(const char *src, Out *o)
{
    const char *p = src + 7; // past "!!VP1.x"
    int maxTemp = -1, address = 0, invariant = 0;
    Out body = {0};

    while (*p)
    {
        if (*p == '#')
        {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if ((p[0] == 'v' || p[0] == 'o') && p[1] == '[')
        {
            char name[16] = {0};
            const char *end = strchr(p, ']');
            if (end && end - p - 2 < (int)sizeof(name))
            {
                memcpy(name, p + 2, end - p - 2);
                emit(&body, "%s", p[0] == 'v' ? vertexAttribute(name) : vertexResult(name));
                p = end + 1;
                continue;
            }
        }
        if (p[0] == 'R' && isdigit((unsigned char)p[1]) && (p == src || !isalnum((unsigned char)p[-1])))
        {
            int n = atoi(p + 1);
            if (n > maxTemp)
                maxTemp = n;
        }
        if (p[0] == 'A' && p[1] == '0' && (p == src || !isalnum((unsigned char)p[-1])))
            address = 1;
        // RCC (VP1.1, the reciprocal clamped away from zero): RCP.
        if (!strncmp(p, "RCC", 3) && (p == src || !isalnum((unsigned char)p[-1])))
        {
            emit(&body, "RCP");
            p += 3;
            continue;
        }
        // An option comes before the declarations in an ARB program.
        if (!strncmp(p, "OPTION NV_position_invariant", 28))
        {
            invariant = 1;
            p += 28;
            if (*p == ';')
                p++;
            continue;
        }
        if (!strncmp(p, "END", 3) && (p == src || !isalnum((unsigned char)p[-1])))
            break;
        emit(&body, "%c", *p++);
    }

    o->length = 0;
    emit(o, "!!ARBvp1.0\n");
    if (invariant)
        emit(o, "OPTION ARB_position_invariant;\n");
    emit(o, "PARAM c[96] = { program.env[0..95] };\n");
    if (maxTemp >= 0)
    {
        emit(o, "TEMP R0");
        for (int i = 1; i <= maxTemp; i++)
            emit(o, ", R%d", i);
        emit(o, ";\n");
    }
    if (address)
        emit(o, "ADDRESS A0;\n");
    // Results the program writes only part of: (0, 0, 0, 1) first, as
    // NVIDIA's had (texture coordinates written .xy, sampled with q).
    for (int i = 0; i < 8; i++)
    {
        char name[24];
        snprintf(name, sizeof(name), "result.texcoord[%d]", i);
        if (strstr(body.text, name))
            emit(o, "MOV %s, {0,0,0,1};\n", name);
    }
    if (strstr(body.text, "result.color.primary"))
        emit(o, "MOV result.color.primary, {0,0,0,1};\n");
    if (strstr(body.text, "result.color.secondary"))
        emit(o, "MOV result.color.secondary, {0,0,0,1};\n");
    emit(o, "%s\nEND\n", body.text);
}

// NV_fragment_program (FP1.0) to ARB_fragment_program.
static void translateFragment(const char *src, Out *o)
{
    const char *p = src + 7; // past "!!FP1.0"
    char declared[16][16];
    int declaredCount = 0, maxR = -1, maxH = -1;
    Out body = {0};

    while (*p)
    {
        if (*p == '#')
        {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (!strncmp(p, "DECLARE", 7))
        {
            // DECLARE name [= {...}];: a local parameter.
            char name[16] = {0};
            int n = 0;
            p += 7;
            while (*p == ' ' || *p == '\t')
                p++;
            while ((isalnum((unsigned char)*p) || *p == '_') && n < 15)
                name[n++] = *p++;
            while (*p && *p != ';')
                p++;
            if (*p)
                p++;
            if (declaredCount < 16)
                strcpy(declared[declaredCount++], name);
            continue;
        }
        if ((p[0] == 'f' || p[0] == 'o') && p[1] == '[')
        {
            char name[16] = {0};
            const char *end = strchr(p, ']');
            if (end && end - p - 2 < (int)sizeof(name))
            {
                memcpy(name, p + 2, end - p - 2);
                if (p[0] == 'f')
                    emit(&body, "%s", fragmentAttribute(name));
                else
                    emit(&body, "%s", !strcmp(name, "DEPR") ? "result.depth" : "result.color");
                p = end + 1;
                continue;
            }
        }
        // An instruction: its opcode without the precision suffix.
        if (isupper((unsigned char)p[0]) && (p == src || p[-1] == ';' || isspace((unsigned char)p[-1])))
        {
            static const char *const ops[] = {"MOV", "MUL", "ADD", "SUB", "MAD", "DP3", "DP4", "DST", "LIT", "MIN",
                                              "MAX", "SLT", "SGE", "RCP", "RSQ", "EX2", "LG2", "FLR", "FRC", "KIL",
                                              "LRP", "POW", "COS", "SIN", "TEX", "TXP", "TXD", "SEQ", "SGT", "SLE",
                                              "SNE", NULL};
            for (const char *const *op = ops; *op; op++)
                if (!strncmp(p, *op, 3) && (p[3] == 'R' || p[3] == 'H' || p[3] == 'X') &&
                    (p[4] == ' ' || p[4] == '_' || p[4] == '\t' || p[4] == 'C'))
                {
                    emit(&body, "%s", *op);
                    p += 4;
                    goto next;
                }
        }
        // Registers: R0-R31, H0-H63 (temporaries); texture units TEXn.
        if ((p[0] == 'R' || p[0] == 'H') && isdigit((unsigned char)p[1]) && !isalnum((unsigned char)p[-1]))
        {
            int n = atoi(p + 1);
            if (p[0] == 'R' && n > maxR)
                maxR = n;
            if (p[0] == 'H' && n > maxH)
                maxH = n;
        }
        if (!strncmp(p, "TEX", 3) && isdigit((unsigned char)p[3]) && !isalnum((unsigned char)p[-1]))
        {
            emit(&body, "texture[%d]", atoi(p + 3));
            p += 3;
            while (isdigit((unsigned char)*p))
                p++;
            continue;
        }
        // A scalar constant {x}: {x, x, x, x}.
        if (*p == '{')
        {
            const char *end = strchr(p, '}');
            if (end && !memchr(p, ',', end - p))
            {
                char value[32] = {0};
                if (end - p - 1 < (int)sizeof(value))
                    memcpy(value, p + 1, end - p - 1);
                emit(&body, "{%s,%s,%s,%s}", value, value, value, value);
                p = end + 1;
                continue;
            }
        }
        if (!strncmp(p, "END", 3) && !isalnum((unsigned char)p[3]))
            break;
        emit(&body, "%c", *p++);
    next:;
    }

    o->length = 0;
    emit(o, "!!ARBfp1.0\n");
    for (int i = 0; i < declaredCount; i++)
        emit(o, "PARAM %s = program.local[%d];\n", declared[i], i);
    for (int i = 0; i <= maxR; i++)
        emit(o, "TEMP R%d;\n", i);
    for (int i = 0; i <= maxH; i++)
        emit(o, "TEMP H%d;\n", i);
    emit(o, "%s\nEND\n", body.text);
}

static void compile(GLenum target, GLuint id, const char *text)
{
    while (glGetError() != GL_NO_ERROR)
        ;
    gl.BindProgramARB(target, id);
    gl.ProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(text), text);
    if (glGetError() != GL_NO_ERROR)
    {
        const char *error = (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
        log_error("NV GL: NV program %u rejected: %s\n%s", id, error ? error : "?", text);
    }
    else if (trace)
        printf("NV GL: NV program %u:\n%s", id, text);
}

__attribute__((force_align_arg_pointer)) static void genProgramsNV(GLsizei n, GLuint *programs)
{
    if (loadGl())
        gl.GenProgramsARB(n, programs);
}

__attribute__((force_align_arg_pointer)) static void loadProgramNV(GLenum target, GLuint id, GLsizei len,
                                                                    const GLubyte *program)
{
    static Out out;
    char *src;

    if (!loadGl() || !(src = malloc(len + 1)))
        return;
    if (trace < 0)
        trace = getenv("NV_GL_TRACE") != NULL;
    memcpy(src, program, len);
    src[len] = '\0';
    if (!strncmp(src, "!!VP1.", 6) && !getenv("NV_GL_NO_VP"))
    {
        translateVertex(src, &out);
        compile(GL_VERTEX_PROGRAM_ARB, id, out.text);
    }
    else if (!strncmp(src, "!!FP1.0", 7))
    {
        translateFragment(src, &out);
        compile(GL_FRAGMENT_PROGRAM_ARB, id, out.text);
    }
    else
        log_warn("NV GL: NV program %u not emulated: %.16s", id, src);
    free(src);
}

static void applyBindFragment(const uint32_t *a)
{
    fragmentProgram = a[0];
}

__attribute__((force_align_arg_pointer)) static void bindProgramNV(GLenum target, GLuint id)
{
    if (!loadGl())
        return;
    if (target == GL_FRAGMENT_PROGRAM_NV)
    {
        const uint32_t args[] = {id};
        if (!nvGlDefer(applyBindFragment, args, 1))
            applyBindFragment(args);
    }
    else
        gl.BindProgramARB(GL_VERTEX_PROGRAM_ARB, id);
}

__attribute__((force_align_arg_pointer)) static void programParameter4fNV(GLenum target, GLuint index, GLfloat x,
                                                                           GLfloat y, GLfloat z, GLfloat w)
{
    (void)target;
    if (loadGl())
        gl.ProgramEnvParameter4fARB(GL_VERTEX_PROGRAM_ARB, index, x, y, z, w);
}

static void applyTrackMatrix(const uint32_t *a)
{
    GLuint address = a[0];

    if (address / 4 >= MAX_TRACKED)
        return;
    tracked[address / 4].matrix = a[1] == GL_NONE ? 0 : a[1];
    tracked[address / 4].transform = a[2];
    anyTracked = 0;
    for (int i = 0; i < MAX_TRACKED; i++)
        anyTracked |= tracked[i].matrix != 0;
}

__attribute__((force_align_arg_pointer)) static void trackMatrixNV(GLenum target, GLuint address, GLenum matrix,
                                                                    GLenum transform)
{
    const uint32_t args[] = {address, matrix, transform};

    (void)target;
    if (!nvGlDefer(applyTrackMatrix, args, 3))
        applyTrackMatrix(args);
}

// m (column-major) inverted into inv; 0 when it is singular.
static int invert(const GLfloat *m, GLfloat *inv)
{
    GLfloat t[16];
    double det;

    t[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    t[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    t[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    t[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    t[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    t[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    t[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    t[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    t[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    t[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    t[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    t[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    t[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    t[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    t[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    t[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    det = m[0] * t[0] + m[1] * t[4] + m[2] * t[8] + m[3] * t[12];
    if (det == 0)
        return 0;
    for (int i = 0; i < 16; i++)
        inv[i] = (GLfloat)(t[i] / det);
    return 1;
}

// a * b, column-major.
static void multiply(const GLfloat *a, const GLfloat *b, GLfloat *r)
{
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] + a[8 + row] * b[c * 4 + 2] +
                             a[12 + row] * b[c * 4 + 3];
}

static void currentMatrix(GLenum matrix, GLfloat *m)
{
    GLfloat a[16], b[16];

    switch (matrix)
    {
    case GL_MODELVIEW:
        glGetFloatv(GL_MODELVIEW_MATRIX, m);
        return;
    case GL_PROJECTION:
        glGetFloatv(GL_PROJECTION_MATRIX, m);
        return;
    case GL_TEXTURE:
        glGetFloatv(GL_TEXTURE_MATRIX, m);
        return;
    case GL_MODELVIEW_PROJECTION_NV:
        glGetFloatv(GL_PROJECTION_MATRIX, a);
        glGetFloatv(GL_MODELVIEW_MATRIX, b);
        multiply(a, b, m);
        return;
    }
    if (matrix >= GL_MATRIX0_NV && matrix < GL_MATRIX0_NV + 8)
    {
        glGetFloatv(GL_CURRENT_MATRIX_ARB, m);
        return;
    }
    memset(m, 0, 16 * sizeof(*m));
    m[0] = m[5] = m[10] = m[15] = 1;
}

// Before a draw with a vertex program on: the tracked matrices into c[].
static void updateTracked(void)
{
    for (int i = 0; i < MAX_TRACKED; i++)
    {
        GLfloat m[16], t[16];
        const GLfloat *src = m;
        int transposed;

        if (!tracked[i].matrix)
            continue;
        currentMatrix(tracked[i].matrix, m);
        if ((tracked[i].transform == GL_INVERSE_NV || tracked[i].transform == GL_INVERSE_TRANSPOSE_NV) &&
            invert(m, t))
            src = t;
        transposed = tracked[i].transform == GL_TRANSPOSE_NV || tracked[i].transform == GL_INVERSE_TRANSPOSE_NV;
        // c[4i + r] is the matrix's row r (its transpose's column r).
        for (int r = 0; r < 4; r++)
        {
            GLfloat row[4];
            for (int c = 0; c < 4; c++)
                row[c] = transposed ? src[r * 4 + c] : src[c * 4 + r];
            gl.ProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB, i * 4 + r, row);
        }
    }
}

int nvVertexProgramOn(void)
{
    return vertexProgramOn;
}

int nvProgramEnable(GLenum cap, int on)
{
    if (cap == GL_VERTEX_PROGRAM_NV)
    {
        vertexProgramOn = on;
        return 0; // the same as ARB's: the GL's own enable
    }
    if (cap == GL_FRAGMENT_PROGRAM_NV)
    {
        fragmentProgramOn = on;
        return 1;
    }
    return 0;
}

int nvProgramBeforeDraw(void)
{
    if (vertexProgramOn && anyTracked && loadGl())
        updateTracked();
    if (fragmentProgramOn && fragmentProgram && loadGl())
    {
        gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, fragmentProgram);
        return 1;
    }
    return 0;
}

// NV_occlusion_query: ARB's queries (its pixel counts' names are ARB's
// results').
__attribute__((force_align_arg_pointer)) static void genOcclusionQueriesNV(GLsizei n, GLuint *ids)
{
    if (loadGl())
        gl.GenQueriesARB(n, ids);
}

__attribute__((force_align_arg_pointer)) static void beginOcclusionQueryNV(GLuint id)
{
    if (loadGl())
        gl.BeginQueryARB(GL_SAMPLES_PASSED_ARB, id);
}

__attribute__((force_align_arg_pointer)) static void endOcclusionQueryNV(void)
{
    if (loadGl())
        gl.EndQueryARB(GL_SAMPLES_PASSED_ARB);
}

__attribute__((force_align_arg_pointer)) static void getOcclusionQueryuivNV(GLuint id, GLenum pname, GLuint *params)
{
    if (loadGl())
        gl.GetQueryObjectuivARB(id, pname, params);
}

void nvProgramInstall(void (*hook)(const char *, void *))
{
    hook("glGenProgramsNV", (void *)genProgramsNV);
    hook("glLoadProgramNV", (void *)loadProgramNV);
    hook("glBindProgramNV", (void *)bindProgramNV);
    hook("glProgramParameter4fNV", (void *)programParameter4fNV);
    hook("glTrackMatrixNV", (void *)trackMatrixNV);
    hook("glGenOcclusionQueriesNV", (void *)genOcclusionQueriesNV);
    hook("glBeginOcclusionQueryNV", (void *)beginOcclusionQueryNV);
    hook("glEndOcclusionQueryNV", (void *)endOcclusionQueryNV);
    hook("glGetOcclusionQueryuivNV", (void *)getOcclusionQueryuivNV);
}
