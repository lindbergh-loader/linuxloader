// Raw Thrills (g5 engine) support: game detection, init and the filesystem
// and GL fixes those games need. See rawthrills.h.

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../log/log.h"

extern uint32_t gId;

static const RtGame *game;
static char gameDir[PATH_MAX];
static char usbDir[PATH_MAX];
static char udevDir[PATH_MAX];

const RtGame *rtCurrentGame(void)
{
    static uint32_t cachedId;
    if (cachedId != gId)
    {
        cachedId = gId;
        game = rtGetGame(gId);
    }
    return game;
}

int isRawThrillsGame(void)
{
    return rtCurrentGame() != NULL;
}

const char *rtGameDir(void)
{
    return gameDir;
}

// ---------------------------------------------------------------------------
// Filesystem

static const char *join(char *buf, size_t size, const char *dir, const char *rest)
{
    snprintf(buf, size, "%s%s", dir, rest);
    return buf;
}

const char *rtRedirectPath(const char *path, char *buf, size_t size)
{
    const RtGame *g = rtCurrentGame();
    size_t rootLen;

    if (!g || !path || !gameDir[0])
        return path;

    // The scripts ship encrypted for the dongle; the dump comes with a
    // decrypted copy, which is what the game must load.
    if (g->encryptedScripts && g->decryptedScripts)
    {
        const char *enc = strstr(path, g->encryptedScripts);
        size_t encLen = strlen(g->encryptedScripts);
        if (enc && (enc[encLen] == '/' || enc[encLen] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, g->decryptedScripts, enc + encLen);
            return buf;
        }
    }

    rootLen = strlen(g->rootPath);
    if (!strncmp(path, g->rootPath, rootLen) && (path[rootLen] == '/' || path[rootLen] == '\0'))
    {
        const char *rest = path + rootLen;
        for (const RtPathAlias *alias = g->pathAliases; alias && alias->from; alias++)
        {
            size_t fromLen = strlen(alias->from);
            if (rest[0] == '/' && !strncmp(rest + 1, alias->from, fromLen) &&
                (rest[1 + fromLen] == '/' || rest[1 + fromLen] == '\0'))
            {
                snprintf(buf, size, "%s/%s%s", gameDir, alias->to, rest + 1 + fromLen);
                return buf;
            }
        }
        return join(buf, size, gameDir, rest);
    }
    for (const RtPathAlias *alias = g->rootAliases; alias && alias->from; alias++)
    {
        size_t fromLen = strlen(alias->from);
        if (!strncmp(path, alias->from, fromLen) && (path[fromLen] == '/' || path[fromLen] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, alias->to, path + fromLen);
            return buf;
        }
    }
    if (!strncmp(path, "/mnt/usbflash", 13))
        return join(buf, size, usbDir, path + 13);
    // The cabinet's serial boards: none (opening a real port, as root on
    // Batocera, leaves the game waiting for replies: Terminator Salvation
    // stalls at the start of a level).
    if (!strncmp(path, "/dev/ttyS", 9) || !strncmp(path, "/dev/ttyUSB", 11))
        return join(buf, size, gameDir, "/rawthrills/no-serial-port");
    // The game installs a USB automount rule at startup.
    if (!strncmp(path, "/etc/udev", 9))
        return join(buf, size, udevDir, path + 9);
    return path;
}

// Commands the games run (sha1sum "/pm/..." for The Walking Dead's file
// check): the cabinet paths in them, as for the game's own file calls.
// Paths start a word (after a space, a quote or '=') and end at a space or
// a quote.
const char *rtRedirectCommand(const char *command, char *buf, size_t size)
{
    size_t out = 0;
    int changed = 0;

    if (!rtCurrentGame() || !command || !gameDir[0])
        return command;
    for (const char *p = command; *p && out + 1 < size;)
    {
        if (*p == '/' && (p == command || strchr(" \t\"'=", p[-1])))
        {
            char path[PATH_MAX], mapped[PATH_MAX];
            size_t n = strcspn(p, " \t\"'");
            const char *r;

            if (n < sizeof(path))
            {
                memcpy(path, p, n);
                path[n] = '\0';
                r = rtRedirectPath(path, mapped, sizeof(mapped));
                if (r != path)
                    changed = 1;
                out += snprintf(buf + out, size - out, "%s", r);
                p += n;
                continue;
            }
        }
        buf[out++] = *p++;
    }
    if (out >= size)
        return command;
    buf[out] = '\0';
    return changed ? buf : command;
}

FILE *popen(const char *command, const char *type)
{
    static FILE *(*real)(const char *, const char *);
    char buf[PATH_MAX * 2];

    if (!real)
        real = dlsym(RTLD_NEXT, "popen");
    return real(rtRedirectCommand(command, buf, sizeof(buf)), type);
}

// Gone from glibc 2.31 but as a versioned compat symbol, which the
// unversioned imports of TeknoParrot's rebuilt executables (MotoGP) cannot
// bind to.
double __pow_finite(double x, double y)
{
    static double (*real)(double, double);
    if (!real)
        real = dlsym(RTLD_NEXT, "pow");
    return real(x, y);
}

// MotoGP's worker threads start "finished" with no thread (handle 0), and
// restarting one joins the old thread first: glibc reads through the null
// handle. There is no such thread.
int pthread_join(pthread_t thread, void **ret)
{
    static int (*real)(pthread_t, void **);
    if (!real)
        real = dlsym(RTLD_NEXT, "pthread_join");
    if (thread == 0 && isRawThrillsGame())
        return ESRCH;
    return real(thread, ret);
}

// ---------------------------------------------------------------------------
// GL: shader sources NVIDIA accepts but not Mesa, applied in place to the
// (already concatenated) source by the glShaderSource interposer
// (graphics/shaderPatches.c).
// - g5 engine: the glow shaders write gl_FragData[] but also pull in a bloom
//   include that declares "out vec4 out_Bloom". GLSL forbids mixing
//   gl_FragData with user-defined outputs; NVIDIA tolerates it, Mesa refuses
//   to link. Those shaders never write out_Bloom, so it is demoted to a
//   plain global.
// - g3 engine: shaders without a #version directive (GLSL 1.10) use GLSL
//   1.20 features (matrix casts, transpose()): they are compiled as 1.20.
//   Some index gl_TexCoord[] with a variable, which needs it redeclared
//   with a size.
// - g6 engine (MotoGP): "#version 150" shaders initialise vectors with an
//   initializer list ("uniform vec2 uPixelSize = { 1.0, 2.0 };"), GLSL 4.20
//   syntax: written as a constructor ("vec2(1.0, 2.0)") instead. Some index
//   a sampler array with a uniform (MotoGP's decals), which GLSL 1.30 on
//   forbids but GL_ARB_gpu_shader5 allows: enabled for shaders declaring
//   one.

#define BLOOM_OUTPUT "out vec4 out_Bloom;"
#define GLSL_120 "#version 120\n"
#define TEXCOORD_SIZED "varying vec4 gl_TexCoord[gl_MaxTextureCoords];\n"

// gl_TexCoord[] indexed by something else than a number.
static int texCoordIndexed(const char *src)
{
    for (const char *p = strstr(src, "gl_TexCoord["); p; p = strstr(p + 1, "gl_TexCoord["))
    {
        const char *i = p + strlen("gl_TexCoord[");
        while (*i == ' ')
            i++;
        if (*i < '0' || *i > '9')
            return 1;
    }
    return 0;
}

// The vector types an initializer list is rewritten for.
static int vectorType(const char *type, size_t n)
{
    static const char *const types[] = {"vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
                                        "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4"};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (strlen(types[i]) == n && strncmp(types[i], type, n) == 0)
            return 1;
    return 0;
}

static int identChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// "<vecN> <name> = { a, b }" -> "<vecN> <name> = vecN( a, b )". The source
// has room for the few bytes each one grows by (see shaderPatches.c).
static void initializerLists(char *src, size_t room)
{
    for (char *brace = strchr(src, '{'); brace; brace = strchr(brace + 1, '{'))
    {
        char *p = brace - 1, *close, *nameEnd, *typeEnd, *type;
        while (p > src && (*p == ' ' || *p == '\t'))
            p--;
        if (p <= src || *p != '=')
            continue;
        p--;
        while (p > src && (*p == ' ' || *p == '\t'))
            p--;
        nameEnd = p + 1;
        while (p > src && identChar(*p))
            p--;
        if (p + 1 == nameEnd)
            continue;
        while (p > src && (*p == ' ' || *p == '\t'))
            p--;
        typeEnd = p + 1;
        while (p > src && identChar(p[-1]))
            p--;
        type = p;
        close = strchr(brace, '}');
        if (!close || memchr(brace + 1, '{', close - brace - 1) || !vectorType(type, typeEnd - type))
            continue;
        size_t typeLen = typeEnd - type, grow = typeLen;
        if (grow > room)
            return;
        room -= grow;
        memmove(brace + 1 + grow, brace + 1, strlen(brace + 1) + 1);
        close += grow;
        memcpy(brace, type, typeLen);
        brace[typeLen] = '(';
        *close = ')';
        brace += typeLen;
    }
}

#define GPU_SHADER5 "#extension GL_ARB_gpu_shader5 : enable\n"

// A sampler array declared: "uniform sampler<type> <name>[".
static int samplerArray(const char *src)
{
    for (const char *p = strstr(src, "uniform sampler"); p; p = strstr(p + 1, "uniform sampler"))
    {
        const char *end = strpbrk(p, ";\n");
        const char *bracket = strchr(p, '[');
        if (bracket && (!end || bracket < end))
            return 1;
    }
    return 0;
}

// The engine's fixes, on the (already concatenated) source.
void rtPatchShaderSource(char *src)
{
    initializerLists(src, 160);
    char *version = strstr(src, "#version");
    char *line = version ? strchr(version, '\n') : NULL;
    if (line && samplerArray(src) && !strstr(src, "GL_ARB_gpu_shader5"))
    {
        size_t n = strlen(GPU_SHADER5);
        memmove(line + 1 + n, line + 1, strlen(line + 1) + 1);
        memcpy(line + 1, GPU_SHADER5, n);
    }
    char *decl = strstr(src, BLOOM_OUTPUT);
    if (decl && strstr(src, "gl_FragData"))
        memset(decl, ' ', 4); // "out " -> a plain global
    if (!strstr(src, "#version"))
    {
        const char *prefix = texCoordIndexed(src) ? GLSL_120 TEXCOORD_SIZED : GLSL_120;
        size_t n = strlen(prefix);
        memmove(src + n, src, strlen(src) + 1);
        memcpy(src, prefix, n);
    }
}

// ---------------------------------------------------------------------------

int rtInit(void)
{
    const RtGame *g = rtCurrentGame();
    if (!g)
        return -1;

    // The launcher starts the game from its directory, which stands in for
    // the cabinet's root (e.g. /pm).
    if (!getcwd(gameDir, sizeof(gameDir)))
        strcpy(gameDir, ".");
    snprintf(usbDir, sizeof(usbDir), "%s/rawthrills/usbflash", gameDir);
    snprintf(udevDir, sizeof(udevDir), "%s/rawthrills/udev", gameDir);
    mkdir(join((char[PATH_MAX]){0}, PATH_MAX, gameDir, "/rawthrills"), 0755);
    mkdir(usbDir, 0755);
    mkdir(udevDir, 0755);
    mkdir(join((char[PATH_MAX]){0}, PATH_MAX, udevDir, "/rules.d"), 0755);
    if (g->workDir)
    {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/%s", gameDir, g->workDir);
        if (chdir(dir) != 0)
            log_warn("Raw Thrills: cannot enter %s", dir);
    }

    // sdl12-compat's GL scaling renders into its own FBO and relies on
    // catching glBindFramebuffer through SDL_GL_GetProcAddress; the game
    // links GL directly, so the swap would blit an empty FBO (black screen).
    setenv("SDL12COMPAT_OPENGL_SCALING", "0", 1);

    // The launcher's stack limit is for the main thread, but glibc gives
    // every thread that much too: games with many threads run out of their
    // 32-bit address space with it.
    if (g->threadStackSize)
    {
        pthread_attr_t attr;
        if (pthread_getattr_default_np(&attr) == 0)
        {
            pthread_attr_setstacksize(&attr, g->threadStackSize);
            pthread_setattr_default_np(&attr);
            pthread_attr_destroy(&attr);
        }
    }

    rtClearBss(g);
    for (const RtHeapPointer *h = g->staleHeapPointers; h && h->address; h++)
        *(void **)(uintptr_t)h->address = calloc(1, h->size);
    rtFixImports(g);
    if (rtReplaceLib(g) != 0)
        return -1;
    rtInstallDongle(g);
    rtInstallIo(g);
    rtInstallInput(g);
    rtInstallJamma(g);
    rtInstallFfb(g);
    rtInstallVideo(g);
    desktopStartQuitWatch();
    return 0;
}
