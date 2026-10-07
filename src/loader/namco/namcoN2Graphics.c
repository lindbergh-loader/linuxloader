#define _GNU_SOURCE
// Namco N2 graphics. The cabinet has no X server: NVIDIA's OpenGL driver is
// linked into the game, with its display manager (adm*: modes, a window, a
// context, swaps) talking to the GPU through /dev/nvidiactl. As the Pacloader
// fork does (n2Graphics.cpp), the display manager is the loader's SDL window
// and GL context, and the game's OpenGL entry points (the driver's, filled in
// at start-up: empty in the file) jump to the host's.

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "namcoN2.h"
#include "namcoEs1.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../log/log.h"
#include "../common/desktopInput.h"

#pragma pack(push, 1)
typedef struct
{
    char ident[4];
    uint32_t unknown[5];
    uint32_t width, height;
    uint32_t refreshMilliHz;
} AdmMode;

typedef struct
{
    char ident[4];
    SDL_Window *window;
} AdmWindow;
#pragma pack(pop)

static AdmMode mode;
static AdmMode *modeList[2];
static uint32_t fbConfig;
static AdmWindow admWindow;
static SDL_Window *window;
static SDL_GLContext context;

static int one(void)
{
    return 1;
}

static const char *admGetString(void)
{
    return "Linux Loader Namco N2";
}

// The one mode: the configured size, 60 Hz.
static AdmMode **admChooseModeConfigi(void)
{
    memset(&mode, 0, sizeof(mode));
    memcpy(mode.ident, "MOCF", 4);
    mode.width = getConfig()->width;
    mode.height = getConfig()->height;
    mode.refreshMilliHz = 60000;
    modeList[0] = &mode;
    modeList[1] = NULL;
    return modeList;
}

static uint32_t *admChooseFBConfigi(void)
{
    return &fbConfig;
}

// The assembly programs (Cg's output, for NVIDIA's extended profiles, which
// Mesa refuses) rewritten as plain ARB ones, as Tank! Tank! Tank!'s
// (namcoEs1ArbProgram.c). NAMCO_N2_TRACE set: each program's first line, and
// Mesa's error for any it still refuses.
#define GL_PROGRAM_ERROR_POSITION_ARB 0x864B
#define GL_PROGRAM_ERROR_STRING_ARB 0x8874
static void (*realProgramString)(unsigned, unsigned, int, const void *);

// NVIDIA aliases the generic vertex attributes with the conventional ones
// (2 the normal, 3 the colour, 8-15 the texture coordinates); Mesa only
// attribute 0. The effect programs (glow, blur, flares: Cg's vp40) read
// their texture coordinates as attributes 8-11, which the game feeds with
// glTexCoordPointer: their names are rewritten to the conventional ones,
// else they all read texel (0, 0) and wash the screen grey.
static char *aliasAttributes(const char *src)
{
    static const char *names[16] = {
        NULL, NULL, "vertex.normal", "vertex.color.primary", "vertex.color.secondary", "vertex.fogcoord",
        NULL, NULL, "vertex.texcoord[0]", "vertex.texcoord[1]", "vertex.texcoord[2]", "vertex.texcoord[3]",
        "vertex.texcoord[4]", "vertex.texcoord[5]", "vertex.texcoord[6]", "vertex.texcoord[7]",
    };
    size_t len = strlen(src), cap = len * 2 + 64, n = 0;
    char *out = malloc(cap);
    const char *p = src;
    int changed = 0;

    if (!out || strncmp(src, "!!ARBvp1.0", 10))
    {
        free(out);
        return NULL;
    }
    while (*p)
    {
        int index, used;
        if (!strncmp(p, "vertex.attrib[", 14) && sscanf(p + 14, "%d]%n", &index, &used) == 1 && index >= 0 &&
            index < 16 && names[index])
        {
            n += sprintf(out + n, "%s", names[index]);
            p += 14 + used;
            changed = 1;
            continue;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    if (!changed)
    {
        free(out);
        return NULL;
    }
    return out;
}

static void programString(unsigned target, unsigned format, int len, const void *string)
{
    static int trace = -1;
    char *translated = string && len > 0 ? namcoEs1ArbTranslate(string, len) : NULL;
    if (string && len > 0)
    {
        char *source = translated ? translated : strndup(string, len);
        char *aliased = source ? aliasAttributes(source) : NULL;
        if (aliased)
        {
            free(source);
            translated = aliased;
        }
        else if (source != translated)
            free(source);
    }

    if (trace < 0)
        trace = getenv("NAMCO_N2_TRACE") != NULL;
    if (translated)
        realProgramString(target, format, (int)strlen(translated), translated);
    else
        realProgramString(target, format, len, string);
    if (trace)
    {
        int position = -1;
        ((void (*)(unsigned, int *))SDL_GL_GetProcAddress("glGetIntegerv"))(GL_PROGRAM_ERROR_POSITION_ARB, &position);
        const char *error = ((const char *(*)(unsigned))SDL_GL_GetProcAddress("glGetString"))(GL_PROGRAM_ERROR_STRING_ARB);
        fprintf(stderr, "Namco N2: program %.*s%s%s%s\n", (int)strcspn((const char *)string, "\n"), (const char *)string,
                translated ? " (translated)" : "", position >= 0 ? " ERROR: " : "", position >= 0 && error ? error : "");
    }
    free(translated);
}

// A compressed texture read back (the cars' paint, recoloured): its size, as
// GL reports it, for the image the engine then allocates for it (see
// allocateImageMemory). As the fork's glHooks.cpp.
#define GL_TEXTURE_COMPRESSED_IMAGE_SIZE 0x86A0
static __thread uint32_t lastCompressedSize, pendingCompressedSize;
static void (*realGetTexLevelParameteriv)(unsigned, int, unsigned, int *);
static void (*realGetCompressedTexImage)(unsigned, int, void *);
static void (*realGetCompressedTexImageARB)(unsigned, int, void *);

static void getTexLevelParameteriv(unsigned target, int level, unsigned name, int *params)
{
    realGetTexLevelParameteriv(target, level, name, params);
    if (name == GL_TEXTURE_COMPRESSED_IMAGE_SIZE && params && *params > 0)
        lastCompressedSize = *params;
}

static void getCompressedTexImage(unsigned target, int level, void *image)
{
    realGetCompressedTexImage(target, level, image);
    pendingCompressedSize = lastCompressedSize;
}

static void getCompressedTexImageARB(unsigned target, int level, void *image)
{
    realGetCompressedTexImageARB(target, level, image);
    pendingCompressedSize = lastCompressedSize;
}

// The game renders at 640x480 whatever the window's size (its render
// destination): it draws into a frame of that size (frameScale), which is
// scaled to the window at each swap, keeping its shape when
// KEEP_ASPECT_RATIO is set. The scaler's wrappers stand in for the GL entry
// points that reach the window's framebuffer.
#define N2_FRAME_WIDTH 640
#define N2_FRAME_HEIGHT 480

static void *glProc(const char *name)
{
    void *scaled = frameScaleWrapper(name);
    if (scaled)
        return scaled;
    static const struct
    {
        const char *name;
        void *replacement, **real;
    } wrapped[] = {
        {"glProgramStringARB", programString, (void **)&realProgramString},
        {"glGetTexLevelParameteriv", getTexLevelParameteriv, (void **)&realGetTexLevelParameteriv},
        {"glGetCompressedTexImage", getCompressedTexImage, (void **)&realGetCompressedTexImage},
        {"glGetCompressedTexImageARB", getCompressedTexImageARB, (void **)&realGetCompressedTexImageARB},
    };

    for (size_t i = 0; i < sizeof(wrapped) / sizeof(wrapped[0]); i++)
        if (strcmp(name, wrapped[i].name) == 0)
        {
            *wrapped[i].real = (void *)SDL_GL_GetProcAddress(name);
            return wrapped[i].replacement;
        }
    return (void *)SDL_GL_GetProcAddress(name);
}

// The window, its context current on the calling thread (the game's render
// thread), and the game's OpenGL entry points on the host's.
static AdmWindow *admCreateWindowi(void)
{
    if (!window)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            log_error("Namco N2: SDL video: %s", SDL_GetError());
            return NULL;
        }
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        window = SDL_CreateWindow(getGameName(), getConfig()->width, getConfig()->height,
                                  SDL_WINDOW_OPENGL | (getConfig()->fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
        if (!window || !(context = SDL_GL_CreateContext(window)))
        {
            log_error("Namco N2: window: %s", SDL_GetError());
            return NULL;
        }
        frameScaleInit((void *(*)(const char *))SDL_GL_GetProcAddress, getConfig()->keepAspectRatio);
        frameScaleSetFrame(N2_FRAME_WIDTH, N2_FRAME_HEIGHT, NULL);
        int patched = namcoN2PatchEntryPoints("gl", glProc);
        log_info("Namco N2: %dx%d window, %d OpenGL entry points on the host's (%s)", getConfig()->width,
                 getConfig()->height, patched, (const char *)((const unsigned char *(*)(unsigned))glProc("glGetString"))(0x1F01));
        desktopStartQuitWatch();
    }
    memcpy(admWindow.ident, "WNDW", 4);
    admWindow.window = window;
    return &admWindow;
}

static int admMakeContextCurrent(void)
{
    return window && context && SDL_GL_MakeCurrent(window, context) ? 1 : 0;
}

static int admGetDeviceAttribi(int device, int attribute, int *value)
{
    (void)device;
    (void)attribute;
    if (value)
        *value = 0;
    return 1;
}

static int admSwapBuffers(AdmWindow *w)
{
    SDL_Event e;

    (void)w;
    int width = 0, height = 0;
    if (SDL_GetWindowSizeInPixels(window, &width, &height) && width > 0 && height > 0)
        frameScalePresent(0, 0, width, height, 0, 0, 0);
    SDL_GL_SwapWindow(window);
    while (SDL_PollEvent(&e))
        if (e.type == SDL_EVENT_QUIT)
            exit(0);
    return 1;
}

static int admSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
    return 1;
}

// Alchemy's shader types (igGfxShader..., igTextureSampler..., the pixel and
// vertex shader attributes) are looked up by name before they are
// registered: registered at the first such lookup (the fork's findN2MetaType).
static void *(*realFindType)(const char *);

static void *findType(const char *name)
{
    static int registered, registering;
    static const char *const registrations[] = {
        "_ZN3Gap3Gfx19igGfxShaderConstant11arkRegisterEv",
        "_ZN3Gap3Gfx23igGfxShaderConstantList11arkRegisterEv",
        "_ZN3Gap3Gfx17igGfxShaderDefine11arkRegisterEv",
        "_ZN3Gap3Gfx21igGfxShaderDefineList11arkRegisterEv",
        "_ZN3Gap3Gfx22igTextureSamplerSource11arkRegisterEv",
        "_ZN3Gap3Gfx26igTextureSamplerSourceList11arkRegisterEv",
        "_ZN3Gap5Attrs17igPixelShaderAttr11arkRegisterEv",
        "_ZN3Gap5Attrs21igPixelShaderAttrList11arkRegisterEv",
        "_ZN3Gap5Attrs18igVertexShaderAttr11arkRegisterEv",
        "_ZN3Gap5Attrs22igVertexShaderAttrList11arkRegisterEv",
    };

    if (name && !registered && !registering &&
        (strncmp(name, "igGfxShader", 11) == 0 || strncmp(name, "igTextureSampler", 16) == 0 ||
         strcmp(name, "igPixelShaderAttr") == 0 || strcmp(name, "igPixelShaderAttrList") == 0 ||
         strcmp(name, "igVertexShaderAttr") == 0 || strcmp(name, "igVertexShaderAttrList") == 0))
    {
        registering = 1;
        for (size_t i = 0; i < sizeof(registrations) / sizeof(registrations[0]); i++)
        {
            void (*registration)(void) = (void (*)(void))namcoN2Symbol(registrations[i]);
            if (registration)
                registration();
        }
        registering = 0;
        registered = 1;
        log_info("Namco N2: Alchemy's shader types registered");
    }
    return realFindType(name);
}

// ---------------------------------------------------------------------------
// Alchemy's images (the fork's n2Graphics.cpp): one with no memory pool is
// made in the system pool, and a compressed one read back from GL gets the
// size GL gave (igImage: +0x30 its size, +0x34 its data, +0x3c "owns it").

static void *(*realInstantiateImage)(void *pool);
static void (*realAllocateImageMemory)(void *image);
static void (*autoSetUnfilledParameters)(void *image);
static void *(*realArenaMallocAligned)(void *pool, uint32_t size, uint32_t alignment);

static void *systemPool(void)
{
    void *(*function)(void) = (void *(*)(void))namcoN2Symbol("_ZN3Gap4Core27igMemoryPoolSystem_functionEv");
    void *(*get)(void *) = (void *(*)(void *))namcoN2Symbol("_ZNK3Gap4Core19igMemoryPoolAdaptorptEv");
    void *adaptor = function && get ? function() : NULL;
    return adaptor ? get(adaptor) : NULL;
}

static void *instantiateImage(void *pool)
{
    void *image = realInstantiateImage(pool);
    if (!image && !pool && (pool = systemPool()))
        image = realInstantiateImage(pool);
    return image;
}

static void allocateImageMemory(void *image)
{
    uint8_t *object = image;
    uint32_t compressed = pendingCompressedSize;

    if (!image)
        return;
    void **data = (void **)(object + 0x34);
    uint32_t *size = (uint32_t *)(object + 0x30);
    pendingCompressedSize = 0;
    if (compressed && compressed <= 256u << 20)
    {
        if (autoSetUnfilledParameters)
            autoSetUnfilledParameters(image);
        *size = compressed;
        void *pool = !*data && realArenaMallocAligned ? systemPool() : NULL;
        if (pool && (*data = realArenaMallocAligned(pool, *size, 128)))
        {
            object[0x3c] = 1;
            return;
        }
        realAllocateImageMemory(image);
        *size = compressed;
        return;
    }
    realAllocateImageMemory(image);
    if (!*data && *size && *size <= 64u << 20 && realArenaMallocAligned)
    {
        void *pool = systemPool();
        if (pool && (*data = realArenaMallocAligned(pool, *size, 128)))
            object[0x3c] = 1;
    }
}

static void *arenaMallocAligned(void *pool, uint32_t size, uint32_t alignment)
{
    void *memory = realArenaMallocAligned(pool, size, alignment);
    if (!memory)
        log_error("Namco N2: Alchemy arena allocation failed (pool %s, %u bytes)",
                  pool ? (const char *)pool + 8 : "(null)", size);
    return memory;
}

void namcoN2GraphicsInit(void)
{
    static const struct
    {
        const char *name;
        void *replacement;
    } hooks[] = {
        {"admvt_setup", one},
        {"admShutdown", one},
        {"admGetString", admGetString},
        {"admGetNumDevices", one},
        {"admInitDevicei", one},
        {"admChooseModeConfigi", admChooseModeConfigi},
        {"admModeConfigi", one},
        {"admChooseFBConfigi", admChooseFBConfigi},
        {"admCreateScreeni", one},
        {"admCreateGraphicsContext", one},
        {"admCreateWindowi", admCreateWindowi},
        {"admDisplayScreen", one},
        {"admMakeContextCurrent", admMakeContextCurrent},
        {"admSwapInterval", admSwapInterval},
        {"admCursorAttribi", one},
        {"admGetDeviceAttribi", admGetDeviceAttribi},
        {"admSwapBuffers", admSwapBuffers},
        {"admSetMonitorGamma", one},
    };
    int n = 0;

    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
        n += namcoN2Hook(hooks[i].name, hooks[i].replacement);
    n += namcoN2HookOriginal("_ZN3Gap4Core12igMetaObject8findTypeEPKc", findType, (void **)&realFindType);
    n += namcoN2HookOriginal("_ZN3Gap4Core17igArenaMemoryPool13mallocAlignedEjj", arenaMallocAligned,
                             (void **)&realArenaMallocAligned);
    n += namcoN2HookOriginal("_ZN3Gap3Gfx7igImage20_instantiateFromPoolEPNS_4Core12igMemoryPoolE", instantiateImage,
                             (void **)&realInstantiateImage);
    autoSetUnfilledParameters = (void (*)(void *))namcoN2Symbol("_ZN3Gap3Gfx7igImage25autoSetUnfilledParametersEv");
    n += namcoN2HookOriginal("_ZN3Gap3Gfx7igImage19allocateImageMemoryEv", allocateImageMemory,
                             (void **)&realAllocateImageMemory);
    log_info("Namco N2: %d display manager hooks", n);
}
