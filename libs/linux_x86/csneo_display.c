// Minimal display shim for Counter Strike NEO.
//
// Provides the symbols the engine needs that the standard libs don't have
// (SDL_ADM_GetDevice + the adm* API over GLX), and intercepts glXChooseFBConfig to
// relax the GL attribute matching so Mesa/GLX finds a visual.
//
// This is used INSTEAD of the full linuxloader.so when the goal is just to
// display the game (the full loader's libc interposers break the X11
// connection in the engine's SDL 1.2).
//
// Build: gcc -m32 -shared -fPIC -o libcsneo_display.so csneo_display.c -ldl -lGL -lX11
// (-lGL is load-bearing: it puts the system libGL in the global scope so the
// engine's gl* bind to it instead of the embedded NVIDIA driver.)

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <GL/glx.h>
#include <stdlib.h>
#include <math.h>

// --- ADM (NVIDIA "Alchemy Display Manager") over GLX.
//
// engine_amd.so statically contains the cabinet's whole NVIDIA userspace GL
// driver (the _nv*adm functions, the gl* entry points in .writetext, the adm*
// API). The cabinet SDL had an ADM video driver; SDL_ADM_GetDevice handed the
// engine its device. Running that embedded driver on a PC is fatal: it builds
// an LDT entry (modify_ldt 666) and loads it into %gs, which is glibc's TLS
// register, so the next malloc/calloc faults.
//
// So the embedded driver must never run. Two halves:
//  - gl*: the engine reaches its GL through symbol relocations (PLT, PC32 and
//    data R_386_32 against its own exported gl*). This shim is linked against
//    the system libGL, which then sits in the global scope ahead of the
//    dlopen'ed engine, so every engine gl* binds to Mesa and works on the GLX
//    context the real SDL created. The few the engine names that libGL lacks
//    (SGIS multitexture etc.) are provided below.
//  - adm*: the only ADM users outside the driver are SDL_ADM_GetDevice, the
//    video memory queries, CMtSDL::SwapBuffer (admSwapInterval) and the Neo
//    renderer's CNeoPixelBufferADMImpl (render-to-texture). Those adm* calls
//    go through the engine's PLT, so the definitions here interpose them and
//    map them onto GLX: handles are plain GLX values (GLXContext pointers,
//    GLXDrawable XIDs, a malloc'ed GLXFBConfig array).
//
// ADM API as used by the engine (from CNeoPixelBufferADMImpl, 0x3b3000):
//   SDL_ADM_GetDevice(0) -> struct; +4 = device, +8 -> struct whose +0x18 is
//       the window's graphics context
//   admChooseFBConfigi(dev, n, attribs, max, int *count) -> config array
//   admCreatePbufferi(dev, config, attribs{0x802 w, 0x803 h, 0x900 ?, 0})
//   admCreateGraphicsContext(dev, config, windowCtx, shareCtx)
//   admMakeContextCurrent(ctx, draw, read) -> 1 on success
//   admGetDrawableAttribi(draw, 0x802/0x803, int *) width/height
//   admGetDeviceAttribi(dev, 0x200/0x207, int *) video memory total/used
//   admFree(config array)
#define ADM_WIDTH 0x802
#define ADM_HEIGHT 0x803

static int adm_trace(void)
{
    static int t = -1;
    if (t < 0)
        t = getenv("CSNEO_TRACE") != NULL;
    return t;
}

static Display *adm_dpy;    // SDL's display, captured from the current context
static GLXContext adm_wctx; // the SDL window's context

// Pick up SDL's display + window context the first time one is current.
static void adm_capture(void)
{
    if (adm_wctx)
        return;
    GLXContext c = glXGetCurrentContext();
    if (!c)
        return;
    adm_wctx = c;
    adm_dpy = glXGetCurrentDisplay();
    if (adm_trace())
        fprintf(stderr, "CSNeo ADM: window ctx=%p dpy=%p drawable=0x%lx\n",
                (void *)c, (void *)adm_dpy, (unsigned long)glXGetCurrentDrawable());
}

struct adm_window
{
    int pad[6];
    GLXContext ctx; // +0x18
};
static struct adm_window adm_window;
static struct
{
    int magic;
    void *device;               // +4
    struct adm_window *window;  // +8
} adm_sdl_device = {0x49564544, &adm_sdl_device, &adm_window};

void *SDL_ADM_GetDevice(int index)
{
    (void)index;
    adm_capture();
    adm_window.ctx = adm_wctx;
    return &adm_sdl_device;
}

void *admChooseFBConfigi(void *dev, int n, const int *attribs, int max, int *count)
{
    (void)dev;
    (void)n;
    (void)attribs;
    (void)max;
    adm_capture();
    *count = 0;
    if (!adm_dpy)
        return NULL;
    int screen = DefaultScreen(adm_dpy);
    GLXFBConfig *cfgs = NULL;
    int nc = 0;
    // Same config as the window, so the window context can also be made
    // current on the pbuffer (bind() copies from it with that context).
    int id = 0;
    if (adm_wctx && glXQueryContext(adm_dpy, adm_wctx, GLX_FBCONFIG_ID, &id) == Success)
    {
        int want[] = {GLX_FBCONFIG_ID, id, None};
        cfgs = glXChooseFBConfig(adm_dpy, screen, want, &nc);
    }
    int types = 0;
    if (cfgs && nc > 0)
        glXGetFBConfigAttrib(adm_dpy, cfgs[0], GLX_DRAWABLE_TYPE, &types);
    if (!(types & GLX_PBUFFER_BIT))
    {
        if (cfgs)
            XFree(cfgs);
        int want[] = {GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT | GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
                      GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
                      GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, True, None};
        cfgs = glXChooseFBConfig(adm_dpy, screen, want, &nc);
    }
    if (!cfgs || nc <= 0)
        return NULL;
    GLXFBConfig *out = malloc(sizeof(*out));
    out[0] = cfgs[0];
    XFree(cfgs);
    *count = 1;
    if (adm_trace())
        fprintf(stderr, "CSNeo ADM: ChooseFBConfig -> %p (window cfg id 0x%x)\n", (void *)out[0], id);
    return out;
}

int admFree(void *p)
{
    free(p);
    return 1;
}

GLXPbuffer admCreatePbufferi(void *dev, GLXFBConfig cfg, const int *attribs)
{
    (void)dev;
    int w = 1024, h = 1024;
    for (int i = 0; attribs && attribs[i]; i += 2)
    {
        if (attribs[i] == ADM_WIDTH)
            w = attribs[i + 1];
        else if (attribs[i] == ADM_HEIGHT)
            h = attribs[i + 1];
    }
    if (!adm_dpy || !cfg)
        return 0;
    int pa[] = {GLX_PBUFFER_WIDTH, w, GLX_PBUFFER_HEIGHT, h, GLX_PRESERVED_CONTENTS, True, None};
    GLXPbuffer pb = glXCreatePbuffer(adm_dpy, cfg, pa);
    if (adm_trace())
        fprintf(stderr, "CSNeo ADM: CreatePbuffer %dx%d -> 0x%lx\n", w, h, (unsigned long)pb);
    return pb;
}

int admDestroyPbuffer(GLXPbuffer pb)
{
    if (adm_dpy && pb)
        glXDestroyPbuffer(adm_dpy, pb);
    return 1;
}

GLXContext admCreateGraphicsContext(void *dev, GLXFBConfig cfg, GLXContext window, GLXContext share)
{
    (void)dev;
    if (!adm_dpy || !cfg)
        return NULL;
    if (!share)
        share = window ? window : adm_wctx;
    GLXContext c = glXCreateNewContext(adm_dpy, cfg, GLX_RGBA_TYPE, share, True);
    if (adm_trace())
        fprintf(stderr, "CSNeo ADM: CreateGraphicsContext share=%p -> %p\n", (void *)share, (void *)c);
    return c;
}

int admDestroyContext(GLXContext c)
{
    if (adm_dpy && c && c != adm_wctx)
        glXDestroyContext(adm_dpy, c);
    return 1;
}

int admMakeContextCurrent(GLXContext ctx, GLXDrawable draw, GLXDrawable read)
{
    adm_capture();
    if (!adm_dpy)
        return 0;
    if (!ctx)
        draw = read = None;
    return glXMakeContextCurrent(adm_dpy, draw, read, ctx) ? 1 : 0;
}

GLXContext admGetCurrentContext(void)
{
    return glXGetCurrentContext();
}

GLXDrawable admGetCurrentDrawDrawable(void)
{
    return glXGetCurrentDrawable();
}

GLXDrawable admGetCurrentReadDrawable(void)
{
    return glXGetCurrentReadDrawable();
}

int admGetDrawableAttribi(GLXDrawable d, int attr, int *value)
{
    unsigned int v = 0;
    *value = 0;
    if (!adm_dpy || !d)
        return 0;
    if (attr == ADM_WIDTH)
        glXQueryDrawable(adm_dpy, d, GLX_WIDTH, &v);
    else if (attr == ADM_HEIGHT)
        glXQueryDrawable(adm_dpy, d, GLX_HEIGHT, &v);
    else
        return 0;
    *value = (int)v;
    return 1;
}

int admGetDeviceAttribi(void *dev, int attr, int *value)
{
    (void)dev;
    // 0x200 = video memory size, 0x207 = free video memory (in KB on the
    // cabinet); only used for the "video memory usage" debug readouts.
    switch (attr)
    {
    case 0x200:
        *value = 256 * 1024;
        return 1;
    case 0x207:
        *value = 128 * 1024;
        return 1;
    }
    *value = 0;
    return 0;
}

int admSwapInterval(int interval)
{
    typedef int (*SwapIntervalFn)(unsigned int);
    static SwapIntervalFn fn;
    static int looked;
    if (!looked)
    {
        looked = 1;
        fn = (SwapIntervalFn)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA");
    }
    static int last = -1;
    if (fn && interval != last)
    {
        last = interval;
        fn((unsigned int)interval);
    }
    return 1;
}

// --- gl* names the engine's GL table points at that the system libGL does
// not export (otherwise they would bind to the dead embedded driver).
// SGIS_multitexture is mapped onto ARB_multitexture; the rest are no-ops.
#define SGIS_TEXTURE0 0x835F
typedef void (*ActiveTextureFn)(GLenum);
typedef void (*MultiTexCoord2fFn)(GLenum, GLfloat, GLfloat);

void glSelectTextureSGIS(GLenum target)
{
    static ActiveTextureFn fn;
    if (!fn)
        fn = (ActiveTextureFn)glXGetProcAddressARB((const GLubyte *)"glActiveTextureARB");
    fn(target - SGIS_TEXTURE0 + GL_TEXTURE0);
}

void glSelectTextureCoordSetSGIS(GLenum target)
{
    static ActiveTextureFn fn;
    if (!fn)
        fn = (ActiveTextureFn)glXGetProcAddressARB((const GLubyte *)"glClientActiveTextureARB");
    fn(target - SGIS_TEXTURE0 + GL_TEXTURE0);
}

void glMultiTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
    static MultiTexCoord2fFn fn;
    if (!fn)
        fn = (MultiTexCoord2fFn)glXGetProcAddressARB((const GLubyte *)"glMultiTexCoord2fARB");
    fn(target - SGIS_TEXTURE0 + GL_TEXTURE0, s, t);
}

void glAddSwapHintRectWIN(GLint x, GLint y, GLsizei w, GLsizei h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

// --- glGetTexLevelParameteriv: CNeoGLTextureContextManager::getTextureLevelDetail
// (0x3aa310) reads GL_TEXTURE_INTERNAL_FORMAT back and only knows the sized
// formats NVIDIA reports (RGB8, RGBA8, LUMINANCE8, 0x803E for alpha); anything
// else is a Sys_Error. Mesa returns the unsized format the texture was created
// with, so map those to the sized ones.
typedef void (*GetTexLevelParameterivFn)(GLenum, GLint, GLenum, GLint *);

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    static GetTexLevelParameterivFn real;
    if (!real)
        real = (GetTexLevelParameterivFn)dlsym(RTLD_NEXT, "glGetTexLevelParameteriv");
    real(target, level, pname, params);
    if (getenv("CSNEO_TRACE_TEX"))
        fprintf(stderr, "CSNeo tex: target 0x%x level %d pname 0x%x -> 0x%x\n", target, level, pname, *params);
    if (pname != GL_TEXTURE_INTERNAL_FORMAT)
        return;
    switch (*params)
    {
    case GL_RGB:
    case 3:
        *params = GL_RGB8;
        break;
    case GL_RGBA:
    case 4:
        *params = GL_RGBA8;
        break;
    case GL_LUMINANCE:
    case 1:
        *params = GL_LUMINANCE8;
        break;
    case GL_ALPHA:
    case GL_ALPHA8:
        *params = GL_ALPHA16;
        break;
    }
}

// --- Cg profiles: the Neo materials (.teq) and some precompiled programs ask
// for NVIDIA-only profiles (vp40/fp40, NV_vertex_program3/NV_fragment_program2
// assembly). Mesa has ARB_vertex_program/ARB_fragment_program, so remap those
// to arbvp1/arbfp1 everywhere a profile crosses the Cg API. CGprofile/CGenum
// are ints; handles are pointers.
typedef int CGprofile;
typedef void *CGcontext;
typedef void *CGprogram;

static void *cg_sym(const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (!p)
        fprintf(stderr, "CSNeo: Cg symbol %s missing\n", name);
    return p;
}

static CGprofile cg_map(CGprofile prof)
{
    static CGprofile vp40, vp30, fp40, fp30, arbvp1, arbfp1;
    if (!arbvp1)
    {
        CGprofile (*get)(const char *) = (CGprofile(*)(const char *))cg_sym("cgGetProfile");
        vp40 = get("vp40");
        vp30 = get("vp30");
        fp40 = get("fp40");
        fp30 = get("fp30");
        arbvp1 = get("arbvp1");
        arbfp1 = get("arbfp1");
    }
    if (prof == vp40 || prof == vp30)
        return arbvp1;
    if (prof == fp40 || prof == fp30)
        return arbfp1;
    return prof;
}

int cgGLIsProfileSupported(CGprofile prof)
{
    static int (*real)(CGprofile);
    if (!real)
        real = cg_sym("cgGLIsProfileSupported");
    return real(cg_map(prof));
}

void cgGLEnableProfile(CGprofile prof)
{
    static void (*real)(CGprofile);
    if (!real)
        real = cg_sym("cgGLEnableProfile");
    real(cg_map(prof));
}

void cgGLDisableProfile(CGprofile prof)
{
    static void (*real)(CGprofile);
    if (!real)
        real = cg_sym("cgGLDisableProfile");
    real(cg_map(prof));
}

void cgGLSetOptimalOptions(CGprofile prof)
{
    static void (*real)(CGprofile);
    if (!real)
        real = cg_sym("cgGLSetOptimalOptions");
    real(cg_map(prof));
}

CGprogram cgCreateProgramFromFile(CGcontext ctx, int type, const char *file, CGprofile prof,
                                  const char *entry, const char **args)
{
    static CGprogram (*real)(CGcontext, int, const char *, CGprofile, const char *, const char **);
    if (!real)
        real = cg_sym("cgCreateProgramFromFile");
    CGprofile mapped = cg_map(prof);
    CGprogram p = real(ctx, type, file, mapped, entry, mapped == prof ? args : NULL);
    if (!p || getenv("CSNEO_TRACE"))
    {
        const char *(*lastListing)(CGcontext) = (const char *(*)(CGcontext))cg_sym("cgGetLastListing");
        const char *l = lastListing ? lastListing(ctx) : NULL;
        fprintf(stderr, "CSNeo Cg: %s %s:%s profile %d->%d -> %p%s%s\n", type == 4113 ? "obj" : "src",
                file, entry ? entry : "-", prof, mapped, p, l && !p ? "\n" : "", l && !p ? l : "");
    }
    return p;
}

// --- powf: cs_amd.so (the game DLL) imports powf@GLIBCXX_3.4, which the
// cabinet's GCC 3.4-era libstdc++ exported and today's does not. An
// unversioned definition satisfies the versioned reference.
float powf(float base, float exponent)
{
    return (float)pow((double)base, (double)exponent);
}

// --- Cabinet store/DB server: every dbcommand (reset, getAreaList, ...) ends
// in DB_CONNECT_ERROR without the shop's management server, and Reset then
// shows "PC RESET ERROR" and parks the game. Commands post their result with
// DB::PushResult (result at cmd+0x14, name at cmd+4); answer a connect error
// as DB_COMPLETE, as if the server had acknowledged.
typedef void (*PushResultFn)(void *db, void *cmd);

void _ZN2DB10PushResultEPN9dbcommand9DBCommandE(void *db, void *cmd)
{
    static PushResultFn real;
    static int *connectError, *complete;
    if (!real)
    {
        Dl_info info;
        void *engine = NULL;
        if (dladdr(__builtin_return_address(0), &info) && info.dli_fname)
            engine = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
        if (!engine)
        {
            fprintf(stderr, "CSNeo: DB::PushResult: engine handle not found\n");
            return;
        }
        real = (PushResultFn)dlsym(engine, "_ZN2DB10PushResultEPN9dbcommand9DBCommandE");
        connectError = dlsym(engine, "_ZN9dbcommand16DB_CONNECT_ERRORE");
        complete = dlsym(engine, "_ZN9dbcommand11DB_COMPLETEE");
    }
    int *result = (int *)((char *)cmd + 0x14);
    if (connectError && complete && *result == *connectError)
    {
        if (getenv("CSNEO_TRACE"))
            fprintf(stderr, "CSNeo DB: %s: CONNECT_ERROR -> COMPLETE\n", *(const char **)((char *)cmd + 4));
        *result = *complete;
    }
    real(db, cmd);
}

// --- I/O board (PCB) on /dev/ttyM0. PCB::PCB opens the serial port and a
// thread exchanges packets every 100 ms: the game writes 12 bytes (lamps,
// coin lockouts) and reads 8-byte input packets, byte 7 being the sum of
// bytes 0-6 & 0x7f. test_start (polled every frame) reads:
//   byte 0: error flags (bit 0 = "COIN0 choke"), kept 0
//   byte 1: bit 2 = TEST switch, bit 3 = SERVICE (both active low)
//   bytes 3-4 / 5-6: coin 1 / coin 2 counters, 7 bits per byte (lo, hi);
//   the game credits the increase.
// open("/dev/ttyM0") gets a pty whose master end is answered by
// pcb_thread; the keys come from the SDL event hooks below.
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>
#include <SDL/SDL_keysym.h>

static volatile int pcb_coin[2], pcb_test, pcb_service;

static void *pcb_thread(void *arg)
{
    int master = (int)(intptr_t)arg;
    unsigned char in[64], out[8];
    for (;;)
    {
        // The game's own tcsetattr may turn on XON/XOFF; a 0x13 byte in a
        // packet would then stop its output (write() -> EAGAIN). Keep raw.
        struct termios t;
        if (tcgetattr(master, &t) == 0 && (t.c_iflag & (IXON | IXOFF) || t.c_lflag & (ICANON | ECHO)))
        {
            cfmakeraw(&t);
            tcsetattr(master, TCSANOW, &t);
            tcflow(master, TCOON);
        }
        while (read(master, in, sizeof(in)) > 0)
            ;
        memset(out, 0, sizeof(out));
        out[1] = (pcb_test ? 0 : 0x04) | (pcb_service ? 0 : 0x08);
        out[3] = pcb_coin[0] & 0x7f;
        out[4] = (pcb_coin[0] >> 7) & 0x7f;
        out[5] = pcb_coin[1] & 0x7f;
        out[6] = (pcb_coin[1] >> 7) & 0x7f;
        int sum = 0;
        for (int i = 0; i < 7; i++)
            sum += out[i];
        out[7] = sum & 0x7f;
        if (write(master, out, sizeof(out)) < 0)
            break;
        usleep(30000);
    }
    return NULL;
}

static int pcb_open(int flags)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master))
        return -1;
    int slave = open(ptsname(master), (flags & ~O_CREAT) | O_NOCTTY);
    if (slave < 0)
        return -1;
    struct termios t;
    if (tcgetattr(slave, &t) == 0)
    {
        cfmakeraw(&t);
        tcsetattr(slave, TCSANOW, &t);
    }
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    pthread_t th;
    pthread_create(&th, NULL, pcb_thread, (void *)(intptr_t)master);
    pthread_detach(th);
    fprintf(stderr, "CSNeo: /dev/ttyM0 -> emulated I/O board (%s)\n", ptsname(master));
    return slave;
}

int open(const char *path, int flags, ...)
{
    static int (*real)(const char *, int, ...);
    if (!real)
        real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (!strcmp(path, "/dev/ttyM0"))
        return pcb_open(flags);
    return real(path, flags, mode);
}

// --- Keys: the cabinet's SDL reported Linux evdev key codes as the scancode
// (key_mapSDL's table is evdev); rebuild it from the keysym. Also the
// board's keys: F5/F6 coin 1/2, F2 test (held), F3 service (held); F7/F8
// console and map (below).
#define SDL12_KEYDOWN 2
#define SDL12_KEYUP 3

// SDL 1.2 keysym -> Linux evdev code (sdl12-compat leaves the scancode 0
// for most keys).
static int sdl_to_evdev(int sym)
{
    // Printable keys: the physical key, from the X keymap (X keycode =
    // evdev + 8), so the layout (AZERTY...) does not move the bindings.
    if (sym > ' ' && sym < 0x7f)
    {
        static Display *dpy;
        if (!dpy)
            dpy = XOpenDisplay(NULL);
        KeyCode kc = dpy ? XKeysymToKeycode(dpy, (KeySym)sym) : 0;
        if (kc >= 8)
            return kc - 8;
    }
    if (sym >= SDLK_F1 && sym <= SDLK_F10)
        return 59 + sym - SDLK_F1;
    switch (sym)
    {
    case '0': return 11;
    case SDLK_ESCAPE: return 1;
    case SDLK_MINUS: return 12;
    case SDLK_EQUALS: return 13;
    case SDLK_BACKSPACE: return 14;
    case SDLK_TAB: return 15;
    case SDLK_LEFTBRACKET: return 26;
    case SDLK_RIGHTBRACKET: return 27;
    case SDLK_RETURN: return 28;
    case SDLK_LCTRL: return 29;
    case SDLK_SEMICOLON: return 39;
    case SDLK_QUOTE: return 40;
    case SDLK_BACKQUOTE: return 41;
    case SDLK_LSHIFT: return 42;
    case SDLK_BACKSLASH: return 43;
    case SDLK_COMMA: return 51;
    case SDLK_PERIOD: return 52;
    case SDLK_SLASH: return 53;
    case SDLK_RSHIFT: return 54;
    case SDLK_LALT: return 56;
    case SDLK_SPACE: return 57;
    case SDLK_CAPSLOCK: return 58;
    case SDLK_F11: return 87;
    case SDLK_F12: return 88;
    case SDLK_RCTRL: return 97;
    case SDLK_RALT: return 100;
    case SDLK_HOME: return 102;
    case SDLK_UP: return 103;
    case SDLK_PAGEUP: return 104;
    case SDLK_LEFT: return 105;
    case SDLK_RIGHT: return 106;
    case SDLK_END: return 107;
    case SDLK_DOWN: return 108;
    case SDLK_PAGEDOWN: return 109;
    case SDLK_INSERT: return 110;
    case SDLK_DELETE: return 111;
    }
    return 0;
}

// Console commands into the engine (Cbuf_AddText). The console's own key
// (ALT+F7) is GNOME's "move window", and display mode has no auto-start of
// a match map (the cabinet's management server, or the full loader's
// thread, does that): F7 toggles the console, F8 loads $CSNEO_MAP
// (default neo_01collision, the first gameplay map).
static void *csneo_engine;

static void csneo_command(const char *cmd)
{
    static void (*addText)(const char *);
    if (!addText && csneo_engine)
        addText = (void (*)(const char *))dlsym(csneo_engine, "Cbuf_AddText");
    if (addText)
        addText(cmd);
    if (getenv("CSNEO_TRACE"))
        fprintf(stderr, "CSNeo: command %s", cmd);
}

static void csneo_key_event(unsigned char *e)
{
    if (e[0] != SDL12_KEYDOWN && e[0] != SDL12_KEYUP)
        return;
    // Seen by the filter already (it hands the same event to the queue).
    if (e[1] == 0x5a)
        return;
    e[1] = 0x5a;
    int down = e[0] == SDL12_KEYDOWN;
    int sym = *(int *)(e + 8);
    int code = sdl_to_evdev(sym);
    if (getenv("CSNEO_TRACE"))
        fprintf(stderr, "CSNeo key: %s sym %d scancode %d -> %d\n", down ? "down" : "up", sym, e[4], code);
    e[4] = code;
    switch (sym)
    {
    case SDLK_F5:
        if (down)
            pcb_coin[0]++;
        break;
    case SDLK_F6:
        if (down)
            pcb_coin[1]++;
        break;
    case SDLK_F2:
        pcb_test = down;
        break;
    case SDLK_F3:
        pcb_service = down;
        break;
    case SDLK_F7:
        if (down)
            csneo_command("toggleconsole\n");
        break;
    case SDLK_F8:
        if (down)
        {
            char cmd[128];
            const char *map = getenv("CSNEO_MAP");
            snprintf(cmd, sizeof(cmd), "map %s\n", map && *map ? map : "neo_01collision");
            csneo_command(cmd);
        }
        break;
    }
}

// The game handles its input in an SDL event filter (HandleEvent, set by
// sdl_main), which sees each event before the queue and swallows most keys,
// so the fix-up has to run in front of it.
typedef int (*EventFilter)(const void *event);
static EventFilter game_filter;

static int csneo_filter(const void *event)
{
    csneo_key_event((unsigned char *)event);
    return game_filter ? game_filter(event) : 1;
}

void SDL_SetEventFilter(EventFilter filter)
{
    static void (*real)(EventFilter);
    if (!real)
    {
        void *sdl = dlopen("libSDL-1.2.so.0", RTLD_LAZY | RTLD_NOLOAD);
        real = (void (*)(EventFilter))dlsym(sdl ? sdl : RTLD_NEXT, "SDL_SetEventFilter");
    }
    game_filter = filter;
    if (getenv("CSNEO_TRACE"))
        fprintf(stderr, "CSNeo: SDL_SetEventFilter(%p) wrapped\n", (void *)filter);
    real(csneo_filter);
}

int SDL_PollEvent(void *event)
{
    static int (*real)(void *);
    if (!real)
    {
        void *sdl = dlopen("libSDL-1.2.so.0", RTLD_LAZY | RTLD_NOLOAD);
        real = (int (*)(void *))dlsym(sdl ? sdl : RTLD_NEXT, "SDL_PollEvent");
    }
    if (!csneo_engine)
    {
        Dl_info info;
        if (dladdr(__builtin_return_address(0), &info) && info.dli_fname)
            csneo_engine = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
    }
    int r = real(event);
    if (r && event)
        csneo_key_event(event);
    return r;
}

// --- glXChooseFBConfig: relax the attribute matching for Mesa.
// SDL 1.2's X11 driver calls this with the GL attributes set by the engine.
// If the exact match fails, retry with a minimal attribute list (just
// doublebuffer) so Mesa finds a visual.
typedef GLXFBConfig *(*ChooseFBConfigFn)(Display *, int, const int *, int *);

GLXFBConfig *glXChooseFBConfig(Display *dpy, int screen, const int *attrib_list, int *nelements)
{
    static ChooseFBConfigFn real;
    if (!real)
        real = (ChooseFBConfigFn)dlsym(RTLD_NEXT, "glXChooseFBConfig");
    if (!real)
        return NULL;

    GLXFBConfig *configs = real(dpy, screen, attrib_list, nelements);
    if (getenv("CSNEO_TRACE"))
    {
        fprintf(stderr, "CSNeo: glXChooseFBConfig full attrs result=%p nelements=%d\n", (void *)configs, nelements ? *nelements : 0);
        for (int i = 0; attrib_list && attrib_list[i] != None; i += 2)
            fprintf(stderr, "  attr[%d]=%d val=%d\n", i / 2, attrib_list[i], attrib_list[i + 1]);
    }
    if (configs)
        return configs;

    // No match with the full attribute list. Retry with just doublebuffer
    // so Mesa finds a standard 24-bit visual.
    int minimal[] = {GLX_DOUBLEBUFFER, 1, None};
    configs = real(dpy, screen, minimal, nelements);
    if (getenv("CSNEO_TRACE"))
        fprintf(stderr, "CSNeo: glXChooseFBConfig minimal attrs result=%p nelements=%d\n", (void *)configs, nelements ? *nelements : 0);
    if (configs && nelements)
        *nelements = 1;
    return configs;
}

// glXChooseFBConfigSGIX: same relaxation.
typedef GLXFBConfig *(*ChooseFBConfigSGIXFn)(Display *, int, int *, int *);
GLXFBConfig *glXChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    static ChooseFBConfigSGIXFn real;
    if (!real)
        real = (ChooseFBConfigSGIXFn)dlsym(RTLD_NEXT, "glXChooseFBConfigSGIX");
    if (getenv("CSNEO_TRACE"))
        fprintf(stderr, "CSNeo: glXChooseFBConfigSGIX called\n");
    if (!real)
        return NULL;

    GLXFBConfig *configs = real(dpy, screen, attrib_list, nelements);
    if (configs)
        return configs;

    int minimal[] = {GLX_DOUBLEBUFFER, 1, None};
    configs = real(dpy, screen, minimal, nelements);
    if (configs && nelements)
        *nelements = 1;
    return configs;
}
