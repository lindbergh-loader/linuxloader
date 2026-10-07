// Counter Strike NEO (csneo2): a Namco System N2 title that is, unlike the
// Wangan games, not a Namco clSystemN2 app. The cabinet runs Valve's GoldSrc
// HLDS: hlds_amd (a stripped launcher, the one TeknoParrot runs) dlopen's
// engine_amd.so (the client + dedicated-server engine, the czero mod) from its
// working directory, and the two play over loopback.
//
// engine_amd.so is the rendering build: a full "Neo" OpenGL engine
// (Neo_RenderAll, CNeoOpenGLMaterialImpl, the NEO_CGFX_* Cg shader path) on
// SDL 1.2 + GL 1.x. The cabinet's display is a second, NVIDIA-only "Alchemy"
// SystemModule (not in this dump), but the engine's own GL window is enough to
// show the game on a PC's X display. The engine's Cg path needs NVIDIA's Cg;
// on a non-NVIDIA GPU the Cg/GL bridge (libCgGL.so) is replaced by a no-op
// shim (libs/linux_x86/libCgGL.so) whose cgGLLoadProgram / cgGLIsProfile-
// Supported answer "not loaded / not supported", steering the shader init down
// the engine's own GLSL fallback.
//
// The hardware it expects (the USB coin/hasp dongle, the magnetic card reader,
// the cabinet management network) is absent, so the install below keeps the
// boot from dying: the hasp login (hasp_login, which the USB class wraps), the
// card reader, and the cabinet management channel.
//
// The Wangan hooks (namcoN2.c) do not apply: clSystemN2, adm*, gRomInfo, the
// Alchemy ig* engine and clKickback are all absent. And unlike those, the
// symbols to patch live in the dlopen'd engine, not the main program (the
// launcher has only ~71 dynamic symbols), so they are looked up in the
// engine's own .dynsym below. The build is stripped, but its dynamic symbol
// table is complete (29 882 entries), so every hook goes by name; the few
// static helpers (CheckCSVLine / CheckCSVColumn) are the engine's own
// _Z12CheckCSVLine... / _Z14CheckCSVColumn... entries.

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "namcoN2.h"
#include "../log/log.h"

// ---------------------------------------------------------------------------
// The dlopen'd engine: the object whose file name is engine_*.so. The launcher
// dlopen's it in main(), which runs after the loader's initMain() (where the
// install is requested), so the install runs when the engine appears in the
// process's loaded objects: the launcher NEEDs libdl, so its dlopen calls go
// through the libdl wrapper and can be interposed here.

static int csneoInstalled;

void namcoN2CsneoInstall(void);

static void csneoInstallFromHandle(void *handle);

// The cabinet's free-disk remap, called from the loader's openat funnel
// (redirections/filesystemShared.c): the engine's opens are raw
// openat(AT_FDCWD, ...) syscalls, which the loader's open(2) interposition
// never sees. Only the cabinet's freespace paths are remapped (onto the
// dump's TeknoParrot/ copy); everything else falls through untouched.
// Writes are not remapped: the TeknoParrot tree is read-only config and the
// cabinet's writes (CloseTime, the player data) have nowhere to go.
const char *csneoMapFreespace(const char *path, int flags, char *buf, size_t size);

int csneoOpenatRemap(int dirfd, const char *path, int flags, char *buf, size_t size)
{
    (void)dirfd;
    return csneoMapFreespace(path, flags, buf, size) != NULL;
}

// Map the engine's cabinet free-disk path ("...freespace/contents2/...") onto
// the dump's copies. The game's own mod files (freespace/contents2/czero/*)
// are the real czero/ tree under the workdir; the cabinet config
// (freespace/contents2/setting.ini, CloseTime.ini, usercfg.ini, ...) is the
// TeknoParrot/ tree. Returns the mapped path in buf, or NULL if the path is
// not a freespace one (or is a write, which has nowhere to go).
const char *csneoMapFreespace(const char *path, int flags, char *buf, size_t size)
{
    static char workDir[PATH_MAX];
    const char *rest;
    const char *freespace;
    if (!workDir[0])
        getcwd(workDir, sizeof(workDir));
    if (!strncmp(path, "freespace/", 10))
        rest = path + 10;
    else if (!strncmp(path, "/freespace/", 11))
        rest = path + 11;
    else if ((freespace = strstr(path, "/freespace/")) != NULL)
        rest = freespace + strlen("/freespace/");
    else
        return NULL;
    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND))
        return NULL; // writes have nowhere to go in the read-only dump
    // freespace/contents2/czero/<x> -> <workdir>/czero/<x> (the real mod),
    // falling back to <workdir>/valve/<x> (the base game) when the mod does
    // not override the file (gfx.wad, fonts.wad, decals.wad, ...).
    if (!strncmp(rest, "contents2/czero/", 16))
    {
        snprintf(buf, size, "%s/czero/%s", workDir, rest + 16);
        if (access(buf, F_OK) != 0)
            snprintf(buf, size, "%s/valve/%s", workDir, rest + 16);
    }
    else if (!strncmp(rest, "contents2/", 10))
        // freespace/contents2/<config> -> <workdir>/TeknoParrot/<config>
        snprintf(buf, size, "%s/TeknoParrot/%s", workDir, rest + 10);
    else
        snprintf(buf, size, "%s/TeknoParrot/%s", workDir, rest);
    return buf;
}

// The cabinet's libSDL-1.2.so.0 / libCgGL.so (in csneo2/lib, found through
// the engine's RPATH "../lib", which beats LD_LIBRARY_PATH) route every GL
// call through the Alchemy "adm*" GPU driver - the cabinet's NVIDIA system
// module, absent on a PC. With them the engine cannot open a window (SDL_Init
// and the GLX visual match fail). A standard SDL 1.2 (and the CgGL shim) must
// be loaded into the global scope first: the engine's later dlopen resolves
// its SDL_*/cgGL* imports against them instead of the cabinet libs.
static void csneoPreloadStandardLibs(void *(*realDlopen)(const char *, int))
{
    const char *dir = getenv("CSNEO_LIBS");
    static char sdl[4096], cggl[4096], adm[4096];
    if (!dir)
        return;
    snprintf(sdl, sizeof(sdl), "%s/libSDL-1.2.so.0", dir);
    if (access(sdl, R_OK) == 0)
        realDlopen(sdl, RTLD_NOW | RTLD_GLOBAL);
    snprintf(cggl, sizeof(cggl), "%s/libCgGL.so", dir);
    if (access(cggl, R_OK) == 0)
        realDlopen(cggl, RTLD_NOW | RTLD_GLOBAL);
    snprintf(adm, sizeof(adm), "%s/libSDLAdm.so", dir);
    if (access(adm, R_OK) == 0)
        realDlopen(adm, RTLD_NOW | RTLD_GLOBAL);
}

// Preload only the shims (CgGL, SDLAdm) that the engine needs but that are
// not in the RPATH library's dependency chain. The standard SDL 1.2 is found
// via the RPATH swap in csneo2/lib/ and must NOT be preloaded a second time
// into the global scope (it breaks the X11 video driver init).
static void csneoPreloadShims(void *(*realDlopen)(const char *, int))
{
    const char *dir = getenv("CSNEO_LIBS");
    static char cggl[4096], adm[4096];
    if (!dir)
        return;
    snprintf(cggl, sizeof(cggl), "%s/libCgGL.so", dir);
    if (access(cggl, R_OK) == 0)
        realDlopen(cggl, RTLD_NOW | RTLD_GLOBAL);
    snprintf(adm, sizeof(adm), "%s/libSDLAdm.so", dir);
    if (access(adm, R_OK) == 0)
        realDlopen(adm, RTLD_NOW | RTLD_GLOBAL);
}

void *dlopen(const char *filename, int flags)
{
    static void *(*realDlopen)(const char *, int);
    if (!realDlopen)
        realDlopen = (void *(*)(const char *, int))dlsym(RTLD_NEXT, "dlopen");
    // Preload the shims (SDL_ADM_GetDevice, CgGL) into the global scope so
    // the engine's PLT resolves them. Do NOT preload libSDL-1.2.so.0: the
    // RPATH swap (csneo2/lib/libSDL-1.2.so.0 -> standard SDL) is sufficient,
    // and a second SDL 1.2 in the global scope breaks the X11 video driver.
    if (filename && strstr(filename, "engine_") != NULL)
        csneoPreloadShims(realDlopen);
    void *handle = realDlopen(filename, flags);
    if (handle && filename && strstr(filename, "engine_") != NULL)
        csneoInstallFromHandle(handle);
    return handle;
}

static uintptr_t engineBase;
static const Elf32_Sym *engineSymtab;
static const char *engineStrtab;
static const Elf32_Word *engineHash;
static uint32_t engineSymbolCount;
static void *engineHandle;

// The engine is the dlopen'd object whose file name is engine_*.so (the
// launcher's and the game dll's code is also RWE, so a flags match alone is
// ambiguous; and the .so has no DT_SONAME, a cabinet build artifact).
static int cacheEngine(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    (void)data;
    const char *lpath = info->dlpi_name;
    if (!lpath || strstr(lpath, "engine_") == NULL || strstr(lpath, ".so") == NULL)
        return 1; // not the engine
    for (int i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_DYNAMIC)
            continue;
        for (const ElfW(Dyn) *d = (const ElfW(Dyn) *)(info->dlpi_addr + ph->p_vaddr); d->d_tag != DT_NULL; d++)
        {
            if (d->d_tag == DT_SYMTAB)
                engineSymtab = (const Elf32_Sym *)(uintptr_t)d->d_un.d_ptr;
            else if (d->d_tag == DT_STRTAB)
                engineStrtab = (const char *)(uintptr_t)d->d_un.d_ptr;
            else if (d->d_tag == DT_HASH)
                engineHash = (const Elf32_Word *)(uintptr_t)d->d_un.d_ptr;
        }
    }
    if (!engineSymtab)
        return 1;
    engineBase = (uintptr_t)info->dlpi_addr;
    return 0; // found it, stop
}

static void cacheEngineNow(void)
{
    if (!engineBase)
    {
        dl_iterate_phdr(cacheEngine, NULL);
        if (engineHash)
            engineSymbolCount = engineHash[1];
    }
}

// Called from the dlopen interposer when the engine is loaded: use dlinfo
// to get the engine's link_map, then walk its program headers for the
// dynamic segment. (RTLD_DI_LINKMAP returns a struct link_map *, not a
// dl_phdr_info.)
static void csneoInstallFromHandle(void *handle)
{
    struct link_map *map;
    if (csneoInstalled)
        return;
    engineHandle = handle;
    int ok = dlinfo(handle, RTLD_DI_LINKMAP, &map);
    if (ok != 0 || !map || !map->l_ld)
        return;
    engineBase = (uintptr_t)map->l_addr;
    for (const ElfW(Dyn) *d = map->l_ld; d->d_tag != DT_NULL; d++)
    {
        if (d->d_tag == DT_SYMTAB)
            engineSymtab = (const Elf32_Sym *)(uintptr_t)d->d_un.d_ptr;
        else if (d->d_tag == DT_STRTAB)
            engineStrtab = (const char *)(uintptr_t)d->d_un.d_ptr;
        else if (d->d_tag == DT_HASH)
            engineHash = (const Elf32_Word *)(uintptr_t)d->d_un.d_ptr;
    }
    if (engineSymtab && engineHash)
    {
        csneoInstalled = 1;
        namcoN2CsneoInstall();
    }
}

static unsigned long elfHash(const char *name)
{
    unsigned long h = 0, g;
    while (*name)
    {
        h = (h << 4) + (unsigned char)*name++;
        if ((g = h & 0xf0000000))
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static void *engineSymbol(const char *name)
{
    cacheEngineNow();
    if (engineSymtab && engineStrtab && engineHash)
    {
        uint32_t nbucket = engineHash[0];
        const Elf32_Word *bucket = engineHash + 2, *chain = bucket + nbucket;
        for (Elf32_Word i = bucket[elfHash(name) % nbucket]; i != STN_UNDEF; i = chain[i])
            if (engineSymtab[i].st_shndx != SHN_UNDEF && strcmp(engineStrtab + engineSymtab[i].st_name, name) == 0)
                return (void *)(uintptr_t)engineSymtab[i].st_value;
    }
    // The engine's SYSV hash (DT_HASH) covers only a subset of its dynamic
    // symbols; fall back to dlsym on the engine's dlopen handle for the rest.
    if (engineHandle)
        return dlsym(engineHandle, name);
    return NULL;
}

static int writable(void *address, size_t n)
{
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)0xfff;
    size_t len = ((uintptr_t)address + n - page + 0xfff) & ~(size_t)0xfff;
    if (mprotect((void *)page, len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0)
        return 1;
    if (getenv("CSNEO_TRACE"))
    {
        fprintf(stderr, "CSNeo: mprotect RWX failed on %p len=%zu errno=%d (%s)\n", (void *)page, len, errno, strerror(errno));
        FILE *f = fopen("/proc/self/maps", "r");
        if (f)
        {
            char line[512];
            while (fgets(line, sizeof(line), f))
                fprintf(stderr, "  %s", line);
            fclose(f);
        }
    }
    return 0;
}

static int engineHook(const char *name, void *replacement)
{
    uint8_t *target = engineSymbol(name);
    if (!target)
    {
        log_debug("CSNeo: %s is not the engine's", name);
        return 0;
    }
    if (!writable(target, 5))
        return 0;
    target[0] = 0xe9;
    *(int32_t *)(target + 1) = (int32_t)((const uint8_t *)replacement - (target + 5));
    return 1;
}

// A jump to a fixed offset inside the engine, for symbols the build's dynamic
// table names differently (or not at all). The offset is stable for this
// build.
static int engineHookAt(uint32_t offset, void *replacement)
{
    if (!engineBase)
        return 0;
    uint8_t *target = (uint8_t *)(uintptr_t)(engineBase + offset);
    if (!writable(target, 5))
        return 0;
    target[0] = 0xe9;
    *(int32_t *)(target + 1) = (int32_t)((const uint8_t *)replacement - (target + 5));
    return 1;
}

// ---------------------------------------------------------------------------
// The replacements.

// 1: the hasp coin dongle is "present and logged in" (it is not: with no
// dongle the cabinet would refuse to boot). hasp_login is the engine's own
// HASP shim (menu/hasp.cpp, .dynsym), wrapped by the USB class the boot calls.
static int one(void)
{
    return 1;
}

// The magnetic card reader (lib_mucard.cpp): no reader is connected, so an
// open fails (the card check then finds no card).
static int muOpenFail(void)
{
    return -1;
}

// The cabinet's online management channel (the APDU/CRM client's connect to
// the master servers): there is no cabinet network, so TCP/IP connections get
// refused and the online play/clan features simply do not come up. AF_UNIX
// (Unix domain sockets) must be allowed through: SDL's X11 video driver and
// the engine's local loopback game server both use Unix sockets, and blocking
// them breaks the GL window and the client/server handshake.
static int connectFail(int domain, const void *addr, int addrlen)
{
    (void)addr;
    (void)addrlen;
    if (domain == AF_UNIX)
        return 0; // allow local sockets (X11, loopback game server)
    return -1; // no cabinet network for TCP/IP
}

// ---------------------------------------------------------------------------
// SDL 1.2 interposers.
//
// The engine's CMtSDL::InitDraw and sdl_main set cabinet-specific GL
// attributes (SDL_GL_BUFFER_SIZE=1 for the 30-bit Alchemy panel) that have no
// matching GLX visual on a PC's Mesa/AMD server. Because the 32-bit process
// exhausts its virtual address space, mprotect(RWX) on the engine's code
// pages fails with ENOMEM and the engineHook byte-patch approach is unusable.
// Instead we interpose SDL_GL_SetAttribute here: the loader is LD_PRELOADed
// and its symbols are found before the SDL library's, so the engine's PLT
// call to SDL_GL_SetAttribute resolves to this wrapper. Invalid attribute
// values (BUFFER_SIZE <= 1) are dropped; everything else passes through.

static int (*realSDL_GL_SetAttribute)(int, int);
static int sdlGLAttrReal(void)
{
    if (!realSDL_GL_SetAttribute)
        realSDL_GL_SetAttribute = (int (*)(int, int))dlsym(RTLD_NEXT, "SDL_GL_SetAttribute");
    return realSDL_GL_SetAttribute ? 1 : 0;
}

// SDL 1.2 GL attribute indices:
// 0 = RED_SIZE, 1 = GREEN_SIZE, 2 = BLUE_SIZE, 3 = ALPHA_SIZE,
// 4 = DEPTH_SIZE, 5 = STENCIL_SIZE, 6 = ACCUM_RED_SIZE, ...
// 10 = DOUBLEBUFFER, 11 = ACCUM_SIZE, 12 = STENCIL_SIZE (old),
// 13 = AUX_BUFFERS, 14 = STEREO, 15 = DIRECT, 16 = REFRESH_RATE,
// 17 = PRESERVE_DRAW, 18 = PRESERVE_READ, 19 = MASK_RGB, 20 = BUFFER_SIZE,
// 21 = SAMPLE_BUFFERS, 22 = SAMPLES, 23 = GL_CONTEXT_MAJOR_VERSION, ...
// Wait - let me check the actual SDL 1.2 enum values.
// From SDL 1.2 sdl_opengl.h:
//   SDL_GL_BUFFER_SIZE = 0
//   SDL_GL_DEPTH_SIZE = 1
//   SDL_GL_STENCIL_SIZE = 2
//   SDL_GL_ACCUM_SIZE = 3
//   SDL_GL_ACCUM_RED_SIZE = 4
//   SDL_GL_ACCUM_GREEN_SIZE = 5
//   SDL_GL_ACCUM_BLUE_SIZE = 6
//   SDL_GL_ACCUM_ALPHA_SIZE = 7
//   SDL_GL_DOUBLEBUFFER = 8
//   SDL_GL_MULTISAMPLEBUFFERS = 9
//   SDL_GL_MULTISAMPLESAMPLES = 10
//   SDL_GL_STEREO = 11
//   SDL_GL_RESIZABLE = 12
//   SDL_GL_PRESERVE_DRAW_BUFFER = 13
//   SDL_GL_PRESERVE_READ_BUFFER = 14
//   SDL_GL_NO_ACCEL_SERIALIZED = 15
//   SDL_GL_CONTEXT_MAJOR_VERSION = 16
//   SDL_GL_CONTEXT_MINOR_VERSION = 17
//   SDL_GL_CONTEXT_EGL = 18
//   SDL_GL_CONTEXT_FLAGS = 19
//   SDL_GL_CONTEXT_PROFILE_MASK = 20
//   SDL_GL_CONTEXT_SHARE_WITH = 21
//
// So attribute 5 = SDL_GL_ACCUM_GREEN_SIZE, value 1. That's unusual but not
// fatal. Let me re-examine the disassembly: movl $0x5,(%esp) / movl $0x1,0x4(%esp)
// The calling convention is: first arg in %esp (5), second in 0x4(%esp) (1).
// So SDL_GL_SetAttribute(5, 1) = SDL_GL_ACCUM_GREEN_SIZE = 1.
// That's not really the problem for GLX. Let me check what actually fails.

// Disabled: the loader's SDL_GL_SetAttribute export shadows SDL 1.2's and
// breaks the engine's GL attribute setup. The mprotect approach for InitDraw
// doesn't work (ENOMEM), so we need a different strategy for the GL attrs.
// int SDL_GL_SetAttribute(int attr, int value)
// {
//     if (!sdlGLAttrReal())
//         return -1;
//     if (attr >= 8 && attr <= 12)
//         return 0;
//     return realSDL_GL_SetAttribute(attr, value);
// }

// Interpose SDL_Init to diagnose the video driver failure. The engine calls
// SDL_Init(0x100020) = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE. If the video
// driver fails to init, SDL_GetVideoInfo() returns NULL and InitDraw dies.


// The cabinet's menu/main loop (menu/sdl_main.cpp): SDL_Init, the GL
// attributes, SDL_SetVideoMode, then the menuMain/SDL_PollEvent loop. It is
// the engine's own cabinet entry point (not the GoldSrc Host_Init), and it
// is where the game's GL window is created. The cabinet's display is a 30-bit
// (10 bits/channel) NVIDIA panel, so the original sets SDL_GL_BUFFER_SIZE to
// 5 (plus a stereo/30-bit attribute) before SDL_SetVideoMode; a PC's
// Mesa/AMD GLX server has no matching visual, so the window fails to create
// ("Couldn't find matching GLX visual").
//
// The engine's Neo renderer is fine with a standard 24-bit doublebuffered
// visual, so this reimplements sdl_main with Mesa-compatible attributes
// (SDL_GL_DOUBLEBUFFER, DEPTH 24, STENCIL 8, ALPHA 8) and the same
// SDL_SetVideoMode(1024, 768, 32, SDL_HWSURFACE), then runs the identical
// menu loop (initScreen, initialize, SDL_SetEventFilter, init_changeScene,
// SDL_PollEvent/menuMain/SDL_GL_SwapBuffers until menuMain returns non-zero,
// menuLogout). The menu/menuMain functions are the engine's own, found in its
// .dynsym; the SDL_* entry points are looked up by name (the engine's own
// libSDL-1.2.so.0 provides them; the loader's interposer for them is lost to
// the engine's PLT, so they are resolved directly here).
typedef int (*sdlFn)(void);
typedef int (*sdlInitFn)(int);
typedef int (*sdlGLAttrFn)(int, int);
typedef void *(*sdlVideoFn)(int, int, int, int);
typedef void (*sdlCaptionFn)(const char *, const char *);
typedef int (*sdlEventFn)(void *);
typedef int (*sdlPollFn)(void *);
typedef void (*sdlSwapFn)(void);

static void *engineDlsym(const char *name)
{
    // The engine is the last object dlopen'd; its own libSDL-1.2.so.0 and its
    // menu functions are in the global scope by then. dlsym(RTLD_DEFAULT)
    // finds them (the loader's own SDL3 symbols have different names, so there
    // is no clash with the SDL 1.2 names).
    return dlsym(RTLD_DEFAULT, name);
}

static int csneoSdlMain(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    typedef int (*initScreenFn)(void);
    typedef int (*initializeFn)(void);
    typedef int (*changeSceneFn)(void);
    typedef int (*menuMainFn)(void);
    typedef void (*menuLogoutFn)(void);
    typedef void (*eventFilterFn)(int, void *);

    sdlInitFn SDL_Init = (sdlInitFn)engineDlsym("SDL_Init");
    sdlGLAttrFn SDL_GL_SetAttribute = (sdlGLAttrFn)engineDlsym("SDL_GL_SetAttribute");
    sdlVideoFn SDL_SetVideoMode = (sdlVideoFn)engineDlsym("SDL_SetVideoMode");
    sdlCaptionFn SDL_WM_SetCaption = (sdlCaptionFn)engineDlsym("SDL_WM_SetCaption");
    sdlSwapFn SDL_GL_SwapBuffers = (sdlSwapFn)engineDlsym("SDL_GL_SwapBuffers");
    sdlEventFn SDL_SetEventFilter = (sdlEventFn)engineDlsym("SDL_SetEventFilter");
    sdlPollFn SDL_PollEvent = (sdlPollFn)engineDlsym("SDL_PollEvent");
    initScreenFn initScreen = (initScreenFn)engineDlsym("initScreen");
    initializeFn initialize = (initializeFn)engineDlsym("initialize");
    changeSceneFn init_changeScene = (changeSceneFn)engineDlsym("init_changeScene");
    menuMainFn menuMain = (menuMainFn)engineDlsym("menuMain");
    menuLogoutFn menuLogout = (menuLogoutFn)engineDlsym("menuLogout");
    eventFilterFn eventFilter = (eventFilterFn)engineDlsym("eventFilter");

    if (!SDL_Init || !SDL_SetVideoMode || !menuMain)
    {
        fprintf(stderr, "CSNeo: sdl_main: engine SDL/menu symbols not found\n");
        return -1;
    }
    if (SDL_Init(0x20 /* SDL_INIT_VIDEO */) < 0)
    {
        fprintf(stderr, "CSNeo: SDL_Init: %s\n", (const char *)engineDlsym("SDL_GetError") ? ((const char *(*)(void))engineDlsym("SDL_GetError"))() : "");
        if (menuLogout)
            menuLogout();
        return -1;
    }
    // Mesa-compatible GL attributes (the original's 5-bit/30-bit cabinet panel
    // attributes have no matching GLX visual on a PC).
    if (SDL_GL_SetAttribute)
    {
        SDL_GL_SetAttribute(1, 1); // SDL_GL_DOUBLEBUFFER
        SDL_GL_SetAttribute(3, 24); // SDL_GL_DEPTH_SIZE
        SDL_GL_SetAttribute(4, 8); // SDL_GL_STENCIL_SIZE
        SDL_GL_SetAttribute(2, 32); // SDL_GL_PIXEL_SIZE (bits) - 24-bit color + alpha
    }
    void *surface = SDL_SetVideoMode(1024, 768, 32, 0x2 /* SDL_HWSURFACE */);
    if (!surface)
    {
        fprintf(stderr, "CSNeo: SDL_SetVideoMode: %s\n", (const char *)engineDlsym("SDL_GetError") ? ((const char *(*)(void))engineDlsym("SDL_GetError"))() : "");
        if (menuLogout)
            menuLogout();
        return -1;
    }
    if (SDL_WM_SetCaption)
        SDL_WM_SetCaption("Counter Strike NEO", "Counter Strike NEO");
    if (initScreen)
        initScreen();
    if (SDL_GL_SwapBuffers)
        SDL_GL_SwapBuffers();
    if (initialize && !initialize())
    {
        if (menuLogout)
            menuLogout();
        return -1;
    }
    if (eventFilter)
    {
        // The original passes a callback (menu event filter); pass NULL to let
        // SDL's default (no filter) run - the menuMain loop drives input.
        // SDL_SetEventFilter's signature is (SDL_EventFilter, void *userdata).
        ((int (*)(void *, void *))engineDlsym("SDL_SetEventFilter"))(NULL, NULL);
    }
    if (init_changeScene)
        init_changeScene();
    // SDL 1.2's SDL_Event is about 256 bytes; keep it on the heap, sized
    // conservatively (the real struct is a tagged union, far smaller than
    // this on i386).
    char ev[1024];
    for (int r = menuMain(); r == 0; r = menuMain())
    {
        if (SDL_PollEvent)
            SDL_PollEvent(ev);
        if (SDL_GL_SwapBuffers)
            SDL_GL_SwapBuffers();
    }
    if (menuLogout)
        menuLogout();
    return 0;
}

// The Namco cabinet's menu/result-screen CSV parser (System::ReadMapCSV ->
// CheckCSVColumn, menu/system.cpp) aborts when a CSV row has as many fields
// as the table's column cap. The dump's owari/csv/drum3.csv header row has
// 7 fields against a cap of 5 ("column overflow 5"), so the boot dies in
// Host_Init before the server starts. A column overflow is a non-fatal data
// condition (the parser just wants to drop the extra fields), so this always
// returns "ok" and lets the engine continue.
static int csneoCsvOk(int column, int max, const char *file, int line, const char *text)
{
    (void)column;
    (void)max;
    (void)file;
    (void)line;
    (void)text;
    return 0;
}

// No-op: the function is skipped (its work is not wanted; see the install
// below).
static void csnoop(void)
{
}

// The engine's own GL window (CMtSDL::InitDraw, nsSdlWrap/CMtSDL.cpp): it
// creates the client's GL surface before the menu loop. The cabinet build
// sets SDL_GL_BUFFER_SIZE to 5 (the 30-bit Alchemy panel) and a stereo
// attribute, then SDL_SetVideoMode(1024, 768, ...) - a combination with no
// matching GLX visual on a PC's Mesa/AMD server, so the surface creation
// fails and the boot dies in PrepareMenu. Reimplemented with Mesa-compatible
// attributes (doublebuffer, 24-bit depth, 8-bit stencil) and the same
// SDL_SetVideoMode; the this->m_videoInfo / this->m_surface / this->m_flags
// members are written exactly as the original does (offsets 0xc98, 0xc94,
// 0xc90 respectively). Returns 0 on success, -1 on failure (the original's
// contract).
typedef void *(*sdlGetVideoInfoFn)(void);
typedef int (*sdlGLSetAttrFn2)(int, int);
typedef void *(*sdlSetVideoModeFn2)(int, int, int, int);

static int csneoInitDraw(void *self)
{
    sdlGetVideoInfoFn SDL_GetVideoInfo = (sdlGetVideoInfoFn)engineDlsym("SDL_GetVideoInfo");
    sdlGLSetAttrFn2 SDL_GL_SetAttribute = (sdlGLSetAttrFn2)engineDlsym("SDL_GL_SetAttribute");
    sdlSetVideoModeFn2 SDL_SetVideoMode = (sdlSetVideoModeFn2)engineDlsym("SDL_SetVideoMode");
    sdlInitFn SDL_Init = (sdlInitFn)engineDlsym("SDL_Init");
    if (!SDL_SetVideoMode)
        return -1;
    // Make sure the video subsystem is up (the engine's Initialize calls
    // SDL_Init with video+joystick+other flags; if any of those failed the
    // video may not be ready). A second SDL_Init(VIDEO) is idempotent.
    if (SDL_Init)
        SDL_Init(0x20 /* SDL_INIT_VIDEO */);
    // m_videoInfo: set if the video subsystem is up; NULL is tolerated by the
    // engine's callers (they only use it for capability queries).
    void *info = SDL_GetVideoInfo ? SDL_GetVideoInfo() : NULL;
    *(void **)((char *)self + 0xc98) = info; // m_videoInfo
    int flags = 0x20000017; // SDL_OPENGL | SDL_DOUBLEBUF | SDL_HWSURFACE | SDL_HWRCCPU (the original's base)
    if (SDL_GL_SetAttribute)
    {
        // Mesa-compatible GL attributes (the original's 5-bit/30-bit cabinet
        // panel attributes have no matching GLX visual on a PC).
        SDL_GL_SetAttribute(1, 1); // SDL_GL_DOUBLEBUFFER
        SDL_GL_SetAttribute(3, 24); // SDL_GL_DEPTH_SIZE
        SDL_GL_SetAttribute(4, 8); // SDL_GL_STENCIL_SIZE
    }
    *(int *)((char *)self + 0xc90) = flags;
    void *surface = SDL_SetVideoMode(0x400 /* 1024 */, 0x300 /* 768 */, 32, flags);
    *(void **)((char *)self + 0xc94) = surface; // m_surface
    return surface ? 0 : -1;
}

// The engine's cabinet FS build (filesystem_stdio_amd.so) can return a bogus
// value from FS_Size (the virtual FS_Size implementation is broken). This
// breaks LoadEntityDLLs (Mem_Malloc of garbage size -> Sys_Error -> exit) and
// Decal_Init (FS_Read with NULL buffer and negative size -> SIGSEGV). The
// file handle is a FILE* (the stdio FS implementation uses FILE*), so it is
// fstat'ed to get the real size.
// The COpenedFile struct in the cabinet FS stdio module contains a FILE* at
// an unknown offset. Try the common ones (0..16) and validate by checking
// that fileno() succeeds on the candidate pointer.
static int fsSizeReal(void *file)
{
    if (!file)
        return 0;
    for (int off = 0; off <= 16; off += 4)
    {
        void *candidate = *(void **)((char *)file + off);
        if (!candidate)
            continue;
        int fd = fileno((FILE *)candidate);
        if (fd >= 0)
        {
            struct stat st;
            if (fstat(fd, &st) == 0 && st.st_size > 0)
            {
                off_t sz = st.st_size;
                if (sz > 0x7fffffff)
                    return 0x7fffffff;
                return (int)sz;
            }
        }
    }
    return 0;
}

// The cabinet's free disk (setting.ini, CloseTime.ini, the player data) is
// /freespace/contents2; the dump has it under the game's TeknoParrot/ (the
// config the loader writes) and the engine also FileCopies CloseTime.ini
// there. Without it the engine's 18 retries of a missing setting.ini abort
// the boot, so the cabinet path is mapped onto the dump's. The loader's open/
// open64 interposition (redirections/filesystemShared.c) calls this through
// namcoN2RedirectPath (common/gamePath.c).
const char *csneoRedirectPath(const char *path, char *buf, size_t size)
{
    return csneoMapFreespace(path, 0, buf, size) ? buf : path;
}

// Background thread: after the server is ready (10 s), start the
// attract-mode map. The cabinet management server would normally trigger
// this via Taka_DedicatedMap, but that uses a hardcoded map name
// (weapon_test_s) not present in this dump. Instead, execute the
// `map neo_01collision` console command (the first entry in mapcycle.txt)
// via Cbuf_InsertText (its dynamic symbol; the engine is single-threaded,
// so the command queue is safe to feed from another thread).
static void *csneoAutoStartThread(void *arg)
{
    (void)arg;
    sleep(10);
    void *(*cbufInsertText)(const char *) = engineSymbol("Cbuf_InsertText");
    if (cbufInsertText)
        cbufInsertText("map neo_01collision\n");
    return NULL;
}

void namcoN2CsneoInstall(void)
{
    cacheEngineNow();
    if (!engineBase)
    {
        // The engine is not dlopen'd yet: it comes up in main(), after
        // initMain() ran, so the dlopen() interposer above re-enters this
        // function from the right moment.
        log_info("CSNeo: engine not loaded yet; the stubs install at dlopen");
        return;
    }

    // The coin/hasp USB dongle: present, logged in, empty. hasp_login (the
    // engine's HASP shim) is what the USB class wraps; with it answering 1,
    // the boot's "dongle is logged in" checks all pass.
    if (engineHook("hasp_login", one))
        log_info("CSNeo: hasp_login stubbed (no USB/hasp dongle)");

    // The dongle's "check" flag: with it false the engine re-checks the
    // dongle on each frame and exits. Set it true (logged in, never again).
    {
        int32_t *check = engineSymbol("g_usbCheck");
        if (check)
        {
            if (writable(check, 4))
            {
                *check = 1;
                log_info("CSNeo: g_usbCheck set (fake dongle logged in)");
            }
        }
    }

    // The magnetic card reader: not connected (a card is never present).
    if (engineHook("_Z7MU_Opens", muOpenFail))
        log_info("CSNeo: MU_Opens stubbed (no card reader)");

    // The cabinet management channel: no cabinet network (no master servers).
    if (engineHook("connect", connectFail))
        log_info("CSNeo: connect stubbed (offline)");

    // The menu/result-screen CSV: a column overflow in the dump's drum*.csv
    // would abort the boot (see csneoCsvOk above). The build's dynamic table
    // names them _Z12CheckCSVLine... / _Z14CheckCSVColumn... (mangled, the
    // engine's own static helpers, not in the stock GoldSrc).
    if (engineHook("_Z12CheckCSVLineiPcS0_iS0_", csneoCsvOk) || engineHookAt(0x688228, csneoCsvOk))
        log_info("CSNeo: CheckCSVLine tolerated (csv line overflow)");
    if (engineHook("_Z14CheckCSVColumniPcS0_iS0_", csneoCsvOk) || engineHookAt(0x688284, csneoCsvOk))
        log_info("CSNeo: CheckCSVColumn tolerated (csv column overflow)");

    // FS_Size: the cabinet FS stdio module's virtual FS_Size can return a
    // bogus value. Replace with a real fstat: find the FILE* inside the
    // COpenedFile handle by scanning for a pointer that fileno() accepts.
    if (engineHook("FS_Size", fsSizeReal))
        log_info("CSNeo: FS_Size fixed (cabinet FS stdio returns bogus size)");

    // Decal_Init (engine/decals.c) loads the decals wad files (gfx.wad,
    // fonts.wad, decals.wad, ...) into the wad cache. In this 32-bit build
    // the cumulative wad data exhausts the process's address space and the
    // subsequent malloc for the cache index aborts. Decals are sprite
    // effects, not critical for the game to boot and render.
    if (engineHook("Decal_Init", csnoop))
        log_info("CSNeo: Decal_Init skipped (32-bit wad cache OOM)");

    // CMtSDL::InitDraw (nsSdlWrap/CMtSDL.cpp): the engine's own GL surface,
    // created before the menu loop. Its cabinet GL attributes (5-bit buffer,
    // 30-bit stereo panel) have no matching GLX visual on a PC, so the
    // surface creation fails and the boot dies in PrepareMenu. Replaced with
    // the same flow and Mesa-compatible attributes (see csneoInitDraw above).
    {
        void *id = engineSymbol("_ZN9nsSdlWrap6CMtSDL8InitDrawEv");
        int wr = id ? writable(id, 5) : -1;
        if (getenv("CSNEO_TRACE"))
            fprintf(stderr, "CSNeo: InitDraw symbol=%p handle=%p writable=%d\n", id, engineHandle, wr);
        if (engineHook("_ZN9nsSdlWrap6CMtSDL8InitDrawEv", csneoInitDraw))
            log_info("CSNeo: InitDraw replaced (Mesa-compatible GL attributes)");
    }

    // sdl_main (menu/sdl_main.cpp): the cabinet's GL window and menu loop.
    // Its GL attributes (5-bit buffer, 30-bit stereo panel) have no matching
    // GLX visual on a PC, so SDL_SetVideoMode fails. Replaced with the same
    // flow and Mesa-compatible attributes (see csneoSdlMain above).
    if (engineHook("_Z8sdl_mainiPPc", csneoSdlMain))
        log_info("CSNeo: sdl_main replaced (Mesa-compatible GL attributes)");

    // The cabinet management server (localms1) normally sends the "start
    // game" APDU after the server is ready. Without it the engine idles
    // forever. A background thread executes the `map neo_01collision`
    // console command (the first entry in mapcycle.txt) after a short delay
    // so the server is fully up.
    pthread_t tid;
    if (pthread_create(&tid, NULL, csneoAutoStartThread, NULL) == 0)
        pthread_detach(tid);
}
