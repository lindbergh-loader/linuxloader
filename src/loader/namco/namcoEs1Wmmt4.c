// Wangan Midnight Maximum Tune 4 (WMN4r, rev 1.10.18): what only this game
// needs, from the Pacloader fork's es1Wmmt4.cpp. The game is stripped and
// links the HASP HL API, its JVIO library and its own network diagnostic
// statically: these are found addresses, valid for this build only (the
// launcher knows it by its file CRC).

#include <arpa/inet.h>
#include <dlfcn.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#include <glad/gl.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <unistd.h>

#include "namcoEs1.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/shaderPatches.h"
#include "../input/evdevFfb.h"
#include "../../minhook/include/MinHook.h"
#include "../log/log.h"

// Overwrite a function's entry with a jmp to the replacement, if its first
// bytes are the expected ones.
static void detour(uint32_t address, const uint8_t *expected, size_t n, void *replacement)
{
    uint8_t *target = (uint8_t *)(uintptr_t)address;
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (memcmp(target, expected, n) != 0)
    {
        log_error("WMMT4: unexpected code at %#x, not hooked", address);
        return;
    }
    if (mprotect((void *)page, (address + 5 - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        log_error("WMMT4: cannot make %#x writable", address);
        return;
    }
    target[0] = 0xe9;
    *(int32_t *)(target + 1) = (int32_t)((uint8_t *)replacement - (target + 5));
}

// ---------------------------------------------------------------------------
// HASP HL: the game reads its dongle's memory file (3392 bytes): its points
// and their limits (big-endian, each with an MPEG-2 CRC32), and the system
// block, the cabinet's serial (its ALL.Net id is ABEN and its last seven
// digits) with a sum and its complement. Writes go to the image only. The
// sixth digit says drive cabinet (0) or terminal (1).

#define HASP_SIZE 3392
#define HASP_SYSTEM 3328
#define HASP_SERIAL "267610069420"

static uint8_t hasp[HASP_SIZE];

static uint32_t crc32Mpeg(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= (uint32_t)data[i] << 24;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x80000000 ? crc << 1 ^ 0x04c11db7 : crc << 1;
    }
    return crc;
}

static void putBe32(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static void haspImage(void)
{
    uint8_t sum = 0;

    memset(hasp, 0, sizeof(hasp));
    // Point limit: initialised, upper token 5, CRC of its first 12 bytes.
    hasp[0] = 1;
    putBe32(hasp + 4, 5);
    putBe32(hasp + 12, crc32Mpeg(hasp, 12));
    // Two points: cost 1, limit 9999, current 1.
    for (int i = 0; i < 2; i++)
    {
        uint8_t *point = hasp + 16 + 16 * i;
        putBe32(point, 1);
        putBe32(point + 4, 9999);
        putBe32(point + 8, 1);
        putBe32(point + 12, crc32Mpeg(point, 12));
    }
    memcpy(hasp + HASP_SYSTEM, HASP_SERIAL, 12);
    for (int i = 0; i < 62; i++)
        sum += hasp[HASP_SYSTEM + i];
    hasp[HASP_SYSTEM + 62] = sum;
    hasp[HASP_SYSTEM + 63] = ~sum;
}

__attribute__((force_align_arg_pointer)) static int haspLogin(uint32_t feature, const void *vendorCode, uint32_t *handle)
{
    (void)feature;
    (void)vendorCode;
    if (handle)
        *handle = 1;
    return 0;
}

__attribute__((force_align_arg_pointer)) static int haspNothing(uint32_t handle)
{
    (void)handle;
    return 0;
}

__attribute__((force_align_arg_pointer)) static int haspGetSize(uint32_t handle, uint32_t fileId, uint32_t *size)
{
    (void)handle;
    (void)fileId;
    if (size)
        *size = sizeof(hasp);
    return 0;
}

static int haspRange(uint32_t offset, uint32_t length)
{
    return offset <= sizeof(hasp) && length <= sizeof(hasp) - offset;
}

__attribute__((force_align_arg_pointer)) static int haspRead(uint32_t handle, uint32_t fileId, uint32_t offset,
                                                             uint32_t length, void *buffer)
{
    (void)handle;
    (void)fileId;
    if (!buffer || !haspRange(offset, length))
        return 1; // HASP_MEM_RANGE
    memcpy(buffer, hasp + offset, length);
    return 0;
}

__attribute__((force_align_arg_pointer)) static int haspWrite(uint32_t handle, uint32_t fileId, uint32_t offset,
                                                              uint32_t length, const void *buffer)
{
    (void)handle;
    (void)fileId;
    if (!buffer || !haspRange(offset, length))
        return 1;
    memcpy(hasp + offset, buffer, length);
    return 0;
}

// ---------------------------------------------------------------------------
// The JVIO library's monitor (a text dump of the board's state for the
// debug overlay): an empty string. Its send command (to the cabinet's
// althmand daemon, which is not run): done.

__attribute__((force_align_arg_pointer)) static char *jvioMonitor(int a, int b, char *buffer, size_t size)
{
    (void)a;
    (void)b;
    if (buffer && size)
        buffer[0] = '\0';
    return buffer;
}

__attribute__((force_align_arg_pointer)) static int sendCommand(int a, int b, int c, int d, char *e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    return 0;
}

// The game's log (Sys::LogMes), which goes nowhere on the cabinet: on
// stderr with NAMCO_GAME_LOG set.
static int gameLog;

__attribute__((force_align_arg_pointer)) static void logMes(int type, const char *format, ...)
{
    char message[2048];
    va_list args;

    if (!gameLog)
        return;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format ? format : "", args);
    va_end(args);
    size_t n = strlen(message);
    fprintf(stderr, "WMMT4 [%d]: %s%s", type, message, n && message[n - 1] == '\n' ? "" : "\n");
}

// ---------------------------------------------------------------------------
// The network diagnostic the game runs before going online (cable, gateway,
// shop router, hops...): eth0 is the host's interface, and the local checks
// pass. Authentication is left alone: the game finds itself offline by its
// own means.

enum
{
    NET_NONE,
    NET_INTERFACE,
    NET_CABLE,
    NET_GATEWAY,
    NET_SHOP_ADDRESS,
    NET_HOPS,
    NET_AUTH,
    NET_SYNC_DATE,
    NET_DONE,
    NET_RENEW,
    NET_TEST,
    NET_WIFI,
    NET_SET_DATE,
    NET_ONLINE,
    NET_CABLE_CHECK,
    NET_NTP,
    NET_PACKAGE_FILE,
};

#define STATE_GOOD 3

typedef struct __attribute__((packed))
{
    uint8_t pad0[436];
    uint32_t resolveState;
    uint8_t pad1[104];
    uint32_t renewState, syncDateState, cableState, gatewayState, contentRouterState, shopRouterState, hopsState;
    uint8_t pad2[4];
    uint32_t hops;
    uint32_t wifiRouterState, onlineState, setDateState, ntpState, packageFileState;
    uint8_t pad3[28];
    uint32_t check;
} NetworkImpl;

_Static_assert(__builtin_offsetof(NetworkImpl, check) == 628, "WMMT4 network state layout");

typedef struct __attribute__((packed))
{
    uint8_t pad0[8];
    uint8_t name[4]; // std::string
    uint32_t address, netmask, gateway;
    uint8_t pad1[12];
    uint8_t mac[4]; // std::string
} NetworkInterface;

_Static_assert(__builtin_offsetof(NetworkInterface, mac) == 36, "WMMT4 network interface layout");

#define NETWORK_GET_INSTANCE 0x080a5c60
#define NETWORK_APPLY_STATE 0x080a9b70
#define STRING_ASSIGN 0x080579c8 // std::string::assign(const char *), PLT

// Sys::Net::Interface::isAddressChange(): never.
__attribute__((force_align_arg_pointer)) static int addressChanged(void *self)
{
    (void)self;
    return 0;
}

static uint32_t pack(const unsigned char octets[4])
{
    return (uint32_t)octets[0] << 24 | (uint32_t)octets[1] << 16 | (uint32_t)octets[2] << 8 | octets[3];
}

// Sys::Net::Interface::update(): a cabinet's address on the shop's LAN
// (192.168.92.11/24, as the Pacloader fork gives it) and the host's MAC. The
// content router check wants the gateway at .254 of that network (E0502
// "Content Router mismatch" otherwise: the host's own network fails it).
__attribute__((force_align_arg_pointer)) static int interfaceUpdate(NetworkInterface *state)
{
    static int logged;
    unsigned char address[4], mask[4], mac[6];
    char macText[13];
    void (*assign)(void *, const char *) = (void (*)(void *, const char *))STRING_ASSIGN;

    if (!state)
        return 0;
    namcoEs1HostInterface(address, mask, mac);
    memcpy(address, namcoEs1CurrentGame()->eth0, 4);
    memcpy(mask, namcoEs1CurrentGame()->eth0 + 4, 4);
    state->address = pack(address);
    state->netmask = pack(mask);
    state->gateway = (pack(address) & pack(mask)) | 254;
    snprintf(macText, sizeof(macText), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    assign(state->name, "eth0");
    assign(state->mac, macText);
    if (!logged)
    {
        logged = 1;
        log_info("WMMT4: eth0 is %u.%u.%u.%u/%u.%u.%u.%u, %s", address[0], address[1], address[2], address[3],
                 mask[0], mask[1], mask[2], mask[3], macText);
    }
    return 1;
}

// Sys::Network::UpdateState(): each local check passes before the game
// publishes the states.
__attribute__((force_align_arg_pointer)) static void networkUpdateState(void *states)
{
    static int cableVerified;
    void **(*getNetwork)(void) = (void **(*)(void))NETWORK_GET_INSTANCE;
    void (*apply)(NetworkImpl *, void *) = (void (*)(NetworkImpl *, void *))NETWORK_APPLY_STATE;
    void **network = getNetwork();
    NetworkImpl *impl = network ? *network : NULL;

    if (!impl)
        return;
    // The game re-checks the cable about once a second (Busy meanwhile): a
    // cable once found stays plugged in.
    if (impl->cableState == STATE_GOOD)
        cableVerified = 1;
    else if (cableVerified)
        impl->cableState = STATE_GOOD;
    switch (impl->check)
    {
    case NET_CABLE:
    case NET_CABLE_CHECK:
        impl->cableState = STATE_GOOD;
        break;
    case NET_GATEWAY:
        impl->gatewayState = STATE_GOOD;
        break;
    case NET_SHOP_ADDRESS:
        impl->resolveState = STATE_GOOD;
        break;
    case NET_HOPS:
        impl->hops = 1;
        impl->hopsState = STATE_GOOD;
        break;
    case NET_SYNC_DATE:
        impl->syncDateState = STATE_GOOD;
        break;
    case NET_RENEW:
        impl->renewState = STATE_GOOD;
        break;
    case NET_TEST:
        impl->cableState = impl->shopRouterState = impl->contentRouterState = impl->hopsState = STATE_GOOD;
        break;
    case NET_WIFI:
        impl->wifiRouterState = STATE_GOOD;
        break;
    case NET_SET_DATE:
        impl->setDateState = STATE_GOOD;
        break;
    case NET_ONLINE:
        impl->onlineState = STATE_GOOD;
        break;
    case NET_NTP:
        impl->ntpState = STATE_GOOD;
        break;
    case NET_PACKAGE_FILE:
        impl->packageFileState = STATE_GOOD;
        break;
    }
    apply(impl, states);
}

// ---------------------------------------------------------------------------
// The loader's GL wrappers (glViewport, glEnable...) call glad's entry
// points, loaded once a context of the game's is current. The game makes GL
// calls on its window before it has one (no-ops on the cabinet's driver):
// they are loaded beforehand, from a context of the loader's own on a
// pbuffer. The entry points are the GL library's dispatch, good for the
// game's context too.

static void loadGlEntryPoints(void)
{
    static const int attributes[] = {GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT, None};
    static const int pbufferAttributes[] = {GLX_PBUFFER_WIDTH, 16, GLX_PBUFFER_HEIGHT, 16, None};
    GLXContext (*createNewContext)(Display *, GLXFBConfig, int, GLXContext, Bool) =
        (GLXContext(*)(Display *, GLXFBConfig, int, GLXContext, Bool))dlsym(RTLD_NEXT, "glXCreateNewContext");
    Bool (*makeContextCurrent)(Display *, GLXDrawable, GLXDrawable, GLXContext) =
        (Bool(*)(Display *, GLXDrawable, GLXDrawable, GLXContext))dlsym(RTLD_NEXT, "glXMakeContextCurrent");
    Display *(*openDisplay)(const char *) = (Display * (*)(const char *)) dlsym(RTLD_NEXT, "XOpenDisplay");
    Display *dpy = openDisplay ? openDisplay(NULL) : NULL;
    GLXFBConfig *configs;
    int n = 0;

    if (!dpy || !createNewContext || !makeContextCurrent)
        return;
    if ((configs = glXChooseFBConfig(dpy, DefaultScreen(dpy), attributes, &n)) && n > 0)
    {
        GLXPbuffer pbuffer = glXCreatePbuffer(dpy, configs[0], pbufferAttributes);
        GLXContext context = createNewContext(dpy, configs[0], GLX_RGBA_TYPE, NULL, True);
        if (context && makeContextCurrent(dpy, pbuffer, pbuffer, context))
        {
            if (!gladLoadGL((GLADloadfunc)glXGetProcAddressARB))
                log_error("WMMT4: cannot load the GL entry points");
            makeContextCurrent(dpy, None, None, NULL);
        }
        if (context)
            glXDestroyContext(dpy, context);
        glXDestroyPbuffer(dpy, pbuffer);
    }
    if (configs)
        XFree(configs);
    XCloseDisplay(dpy);
}

// ---------------------------------------------------------------------------
// The shaders: Cg programs compiled for NVIDIA's GeForce 8 profiles (gp4vp,
// gp4fp: NV_gpu_program4 assembly, data/Shader/SHD_ALL_*.BIN), loaded as
// objects with the latest profile Cg finds. A driver without NV_gpu_program4
// (Mesa) gets them rewritten as ARB programs (namcoEs1Gp4Translate), under
// the ARB profiles; one that cannot be (the loops of a few post-effect
// filters) is a copy of its source picture.

#define CG_OBJECT 0x1011
#define CG_PROFILE_ARBVP1 6150
#define CG_PROFILE_ARBFP1 7000
#define CG_GL_VERTEX 8

static int (*realGetLatestProfile)(int);
static void *(*realCreateProgram)(void *, int, const char *, int, const char *, const char **);

__attribute__((force_align_arg_pointer)) static int getLatestProfile(int profileClass)
{
    if (GLAD_GL_NV_gpu_program4)
        return realGetLatestProfile(profileClass);
    return profileClass == CG_GL_VERTEX ? CG_PROFILE_ARBVP1 : CG_PROFILE_ARBFP1;
}

__attribute__((force_align_arg_pointer)) static void *createProgram(void *context, int type, const char *program,
                                                                    int profile, const char *entry, const char **args)
{
    static int translated, stubbed;
    int vertex;

    if (GLAD_GL_NV_gpu_program4 || type != CG_OBJECT || !program ||
        (strncmp(program, "!!NVvp4.0", 9) && strncmp(program, "!!NVfp4.0", 9)))
        return realCreateProgram(context, type, program, profile, entry, args);
    vertex = program[4] == 'v';
    char *arb = namcoEs1Gp4Translate(program, strlen(program));
    if (arb)
        translated++;
    else
    {
        stubbed++;
        log_warn("WMMT4: a %s program could not be rewritten for this driver: its source picture instead",
                 vertex ? "vertex" : "fragment");
        arb = strdup(vertex ? "!!ARBvp1.0\nMOV result.position, vertex.position;\nEND\n"
                            : "!!ARBfp1.0\nTEX result.color, fragment.texcoord[0], texture[0], 2D;\nEND\n");
    }
    void *result = realCreateProgram(context, type, arb, vertex ? CG_PROFILE_ARBVP1 : CG_PROFILE_ARBFP1, entry, args);
    free(arb);
    if ((translated + stubbed) % 100 == 0)
        log_info("WMMT4: %d NV_gpu_program4 programs rewritten as ARB ones, %d replaced", translated, stubbed);
    return result;
}

// ---------------------------------------------------------------------------
// The window: the game covers the whole screen with an override-redirect
// window of the root's size (then resized to it again), and asks the window
// manager for full screen. Unless [Display]
// FULLSCREEN is set, it is an ordinary window of [Display] WIDTH x HEIGHT
// instead; the game renders at its window's size.

// The game opens ":0.0" whatever $DISPLAY says: it gets the display the
// loader is run on (a virtual one in tests).
static Display *(*realOpenDisplay)(const char *);

__attribute__((force_align_arg_pointer)) static Display *openDisplay(const char *name)
{
    return realOpenDisplay(NULL);
}

static Window (*realCreateWindow)(Display *, Window, int, int, unsigned, unsigned, unsigned, int, unsigned, Visual *,
                                  unsigned long, XSetWindowAttributes *);
static int (*realMoveResizeWindow)(Display *, Window, int, int, unsigned, unsigned);
static Window gameWindow;

static void windowSize(unsigned *width, unsigned *height)
{
    int w = getConfig()->width, h = getConfig()->height;
    *width = w > 0 ? w : 1360;
    *height = h > 0 ? h : 768;
}

__attribute__((force_align_arg_pointer)) static Window createWindow(Display *dpy, Window parent, int x, int y,
                                                                    unsigned width, unsigned height, unsigned border,
                                                                    int depth, unsigned cls, Visual *visual,
                                                                    unsigned long mask,
                                                                    XSetWindowAttributes *attributes)
{
    int topLevel = parent == DefaultRootWindow(dpy);

    if (topLevel && !getConfig()->fullscreen)
    {
        windowSize(&width, &height);
        if (attributes)
            attributes->override_redirect = False;
        mask &= ~CWOverrideRedirect;
    }
    Window window = realCreateWindow(dpy, parent, x, y, width, height, border, depth, cls, visual, mask, attributes);
    if (window && topLevel)
    {
        // This process's window, for the loader's quit watch (Esc).
        long pid = getpid();
        XChangeProperty(dpy, window, XInternAtom(dpy, "_NET_WM_PID", False), XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&pid, 1);
        XStoreName(dpy, window, "Wangan Midnight Maximum Tune 4");
        gameWindow = window;
    }
    return window;
}

__attribute__((force_align_arg_pointer)) static int moveResizeWindow(Display *dpy, Window window, int x, int y,
                                                                     unsigned width, unsigned height)
{
    if (window == gameWindow && !getConfig()->fullscreen)
    {
        windowSize(&width, &height);
        return XResizeWindow(dpy, window, width, height);
    }
    return realMoveResizeWindow(dpy, window, x, y, width, height);
}

// The size it renders at comes from the root window's attributes: the
// window's, when that is not the screen.
static Status (*realGetWindowAttributes)(Display *, Window, XWindowAttributes *);

__attribute__((force_align_arg_pointer)) static Status getWindowAttributes(Display *dpy, Window window,
                                                                           XWindowAttributes *attributes)
{
    Status status = realGetWindowAttributes(dpy, window, attributes);
    if (status && attributes && !getConfig()->fullscreen &&
        (window == DefaultRootWindow(dpy) || window == gameWindow))
    {
        unsigned width, height;
        windowSize(&width, &height);
        attributes->width = width;
        attributes->height = height;
    }
    return status;
}

// Its request to the window manager for full screen (_NET_WM_STATE), too.
static int (*realChangeProperty)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int);

__attribute__((force_align_arg_pointer)) static int changeProperty(Display *dpy, Window window, Atom property,
                                                                   Atom type, int format, int mode,
                                                                   const unsigned char *data, int elements)
{
    if (window == gameWindow && !getConfig()->fullscreen && property == XInternAtom(dpy, "_NET_WM_STATE", False))
        return 1;
    return realChangeProperty(dpy, window, property, type, format, mode, data, elements);
}

// ---------------------------------------------------------------------------
// The frame pacing: the game waits for each vertical retrace with
// GLX_SGI_video_sync (got through glXGetProcAddress). Mesa's, on XWayland,
// blocks for good while the window is not being presented (behind another):
// the game stopped there. A 60 Hz clock of the loader's instead (the
// cabinet's monitor's rate, whatever the host's).

#define FRAME_RATE 60.0

// The clock in frames (doubles: the loader has no 64-bit division).
static double nowFrames(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec + ts.tv_nsec / 1e9) * FRAME_RATE;
}

__attribute__((force_align_arg_pointer)) static int getVideoSync(unsigned *count)
{
    if (count)
        *count = (unsigned)nowFrames();
    return 0;
}

__attribute__((force_align_arg_pointer)) static int waitVideoSync(int divisor, int remainder, unsigned *count)
{
    unsigned frame = (unsigned)nowFrames() + 1;

    if (divisor > 1)
        while (frame % (unsigned)divisor != (unsigned)remainder % (unsigned)divisor)
            frame++;
    double until = frame / FRAME_RATE;
    struct timespec ts = {(time_t)until, (long)((until - (time_t)until) * 1e9)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0)
        ;
    if (count)
        *count = frame;
    return 0;
}

// NV_fence, which the game syncs its texture uploads with (its 2D layouts
// and menus came out as stale pieces of earlier frames): a driver without it
// (Mesa) gives functions that do nothing, a fence never completing. The
// loader's emulation on sync objects instead (graphics/shaderPatches.c),
// under a lock: the game's threads share it.
#define GL_FENCE_STATUS_NV 0x84F3
#define GL_FENCE_CONDITION_NV 0x84F4
#define GL_ALL_COMPLETED_NV 0x84F2

static pthread_mutex_t fenceLock = PTHREAD_MUTEX_INITIALIZER;

__attribute__((force_align_arg_pointer)) static void genFences(GLsizei n, GLuint *fences)
{
    pthread_mutex_lock(&fenceLock);
    bridgeglGenFencesNV(n, fences);
    pthread_mutex_unlock(&fenceLock);
}

__attribute__((force_align_arg_pointer)) static void deleteFences(GLsizei n, const GLuint *fences)
{
    pthread_mutex_lock(&fenceLock);
    bridgeglDeleteFencesNV(n, fences);
    pthread_mutex_unlock(&fenceLock);
}

__attribute__((force_align_arg_pointer)) static void setFence(GLuint fence, GLenum condition)
{
    pthread_mutex_lock(&fenceLock);
    bridgeglSetFenceNV(fence, condition);
    pthread_mutex_unlock(&fenceLock);
}

__attribute__((force_align_arg_pointer)) static GLboolean testFence(GLuint fence)
{
    pthread_mutex_lock(&fenceLock);
    GLboolean done = bridgeglTestFenceNV(fence);
    pthread_mutex_unlock(&fenceLock);
    return done;
}

__attribute__((force_align_arg_pointer)) static void finishFence(GLuint fence)
{
    pthread_mutex_lock(&fenceLock);
    bridgeglFinishFenceNV(fence);
    pthread_mutex_unlock(&fenceLock);
}

__attribute__((force_align_arg_pointer)) static GLboolean isFence(GLuint fence)
{
    pthread_mutex_lock(&fenceLock);
    GLboolean is = bridgeglIsFenceNV(fence);
    pthread_mutex_unlock(&fenceLock);
    return is;
}

__attribute__((force_align_arg_pointer)) static void getFenceiv(GLuint fence, GLenum name, GLint *value)
{
    if (!value)
        return;
    if (name == GL_FENCE_STATUS_NV)
        *value = testFence(fence);
    else if (name == GL_FENCE_CONDITION_NV)
        *value = GL_ALL_COMPLETED_NV;
}

static const struct
{
    const char *name;
    void *function;
} fenceFunctions[] = {
    {"glGenFencesNV", (void *)genFences},   {"glDeleteFencesNV", (void *)deleteFences},
    {"glSetFenceNV", (void *)setFence},     {"glTestFenceNV", (void *)testFence},
    {"glFinishFenceNV", (void *)finishFence}, {"glIsFenceNV", (void *)isFence},
    {"glGetFenceivNV", (void *)getFenceiv},
};

static void *(*realGetProcAddress)(const char *);

__attribute__((force_align_arg_pointer)) static void *getProcAddress(const char *name)
{
    if (name && !strcmp(name, "glXGetVideoSyncSGI"))
        return (void *)getVideoSync;
    if (name && !strcmp(name, "glXWaitVideoSyncSGI"))
        return (void *)waitVideoSync;
    for (size_t i = 0; name && !GLAD_GL_NV_fence && i < sizeof(fenceFunctions) / sizeof(fenceFunctions[0]); i++)
        if (!strcmp(name, fenceFunctions[i].name))
            return fenceFunctions[i].function;
    return realGetProcAddress(name);
}

// ---------------------------------------------------------------------------
// The cabinet LAN (multicast 225.0.0.1, the terminal's and other cabinets'
// announcements): kept on the loopback interface, where the loader's
// terminal sends. The game joins the group on the default interface, where a
// firewall drops the looped datagrams (and the LAN would reach the network):
// its sockets join on loopback too and send there.

static int (*realSetsockopt)(int, int, int, const void *, socklen_t);

__attribute__((force_align_arg_pointer)) static int gameSetsockopt(int fd, int level, int name, const void *value,
                                                                   socklen_t length)
{
    int result = realSetsockopt(fd, level, name, value, length);

    if (result == 0 && level == IPPROTO_IP && name == IP_ADD_MEMBERSHIP && value && length >= sizeof(struct ip_mreq))
    {
        struct ip_mreq request = *(const struct ip_mreq *)value;
        struct in_addr loopback = {htonl(INADDR_LOOPBACK)};
        unsigned char one = 1;
        if (request.imr_interface.s_addr != loopback.s_addr)
        {
            request.imr_interface = loopback;
            realSetsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request));
        }
        realSetsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &loopback, sizeof(loopback));
        realSetsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));
    }
    return result;
}

// On loopback every announcement comes from 127.0.0.1, while the cabinet
// tells its peers apart by address and recognises its own datagram by its
// eth0's (192.168.92.11, see interfaceUpdate): the source the game reads is
// that of the sender's PCB (the packet's second byte) on that network,
// 192.168.92.11 + id (the terminal, 4: .15).
#define LAN_PORT 50765

static void lanSource(struct sockaddr_in *from, const uint8_t *data, ssize_t n)
{
    if (!from || n < 2 || from->sin_family != AF_INET || from->sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
        from->sin_port != htons(LAN_PORT) || data[0] != 1)
        return;
    from->sin_addr.s_addr = htonl(0xc0a85c0b + data[1]); // 192.168.92.11 + PCB id
}

static ssize_t (*realRecvmsg)(int, struct msghdr *, int);
static ssize_t (*realRecvfrom)(int, void *, size_t, int, struct sockaddr *, socklen_t *);

__attribute__((force_align_arg_pointer)) static ssize_t gameRecvmsg(int fd, struct msghdr *msg, int flags)
{
    ssize_t n = realRecvmsg(fd, msg, flags);
    if (n > 0 && msg && msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_in) && msg->msg_iovlen > 0)
        lanSource(msg->msg_name, msg->msg_iov[0].iov_base,
                  n < (ssize_t)msg->msg_iov[0].iov_len ? n : (ssize_t)msg->msg_iov[0].iov_len);
    return n;
}

__attribute__((force_align_arg_pointer)) static ssize_t gameRecvfrom(int fd, void *buffer, size_t size, int flags,
                                                                     struct sockaddr *from, socklen_t *length)
{
    ssize_t n = realRecvfrom(fd, buffer, size, flags, from, length);
    if (n > 0 && from && length && *length >= sizeof(struct sockaddr_in))
        lanSource((struct sockaddr_in *)from, buffer, n);
    return n;
}

// ---------------------------------------------------------------------------
// WMMT4_SHOT=<directory> (debugging): a frame every 4 s (WMMT4_SHOT_PERIOD,
// 0 for none) saved there as a PPM, read back before the swap (a capture of
// the window comes out black); touching <directory>/grab saves the next one
// as grab.ppm.

static const char *shotDir;
static int shotPeriod = 4; // WMMT4_SHOT_PERIOD, 0: on request only
static void (*realSwapBuffers)(Display *, GLXDrawable);

__attribute__((force_align_arg_pointer)) static void swapBuffers(Display *dpy, GLXDrawable drawable)
{
    static double next;
    static int count;
    double now = nowFrames() / FRAME_RATE;
    unsigned width, height;

    char path[512];
    snprintf(path, sizeof(path), "%s/grab", shotDir);
    int grab = access(path, F_OK) == 0;
    if (grab)
        unlink(path);
    if ((grab || (shotPeriod && now >= next)) && glad_glReadPixels)
    {
        next = now + shotPeriod;
        if (getConfig()->fullscreen)
        {
            width = DisplayWidth(dpy, DefaultScreen(dpy));
            height = DisplayHeight(dpy, DefaultScreen(dpy));
        }
        else
            windowSize(&width, &height);
        unsigned char *pixels = malloc((size_t)width * height * 3);
        if (grab)
            snprintf(path, sizeof(path), "%s/grab.ppm", shotDir);
        else
            snprintf(path, sizeof(path), "%s/wmmt4_%03d.ppm", shotDir, count++);
        FILE *f = pixels ? fopen(path, "wb") : NULL;
        if (f)
        {
            glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glad_glReadBuffer(GL_BACK);
            glad_glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
            fprintf(f, "P6\n%u %u\n255\n", width, height);
            for (unsigned y = height; y-- > 0;)
                fwrite(pixels + (size_t)y * width * 3, 1, (size_t)width * 3, f);
            fclose(f);
        }
        free(pixels);
    }
    realSwapBuffers(dpy, drawable);
}

// ---------------------------------------------------------------------------
// The network checks' commands (run as root, not here): the link is up and
// the shop's router answers one hop away, else the game stays offline
// ("cable/gateway is unknown").

const char *namcoEs1Wmmt4CommandOutput(const char *command)
{
    if (strstr(command, "ethtool"))
        return "Settings for eth0:\n\tSpeed: 100Mb/s\n\tDuplex: Full\n\tLink detected: yes\n";
    if (strstr(command, "traceroute"))
    {
        static char hop[128];
        const char *target = strstr(command, "-m 99 ");
        unsigned a, b, c, d;
        if (!target || sscanf(target + 6, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
            return NULL;
        snprintf(hop, sizeof(hop), " 1  %u.%u.%u.%u  0.215 ms  0.180 ms  0.172 ms\n", a, b, c, d);
        return hop;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// The panel (as the Pacloader fork maps it): the loader's player 1 buttons
// become the cabinet's switches. Button 1 is Enter, 2 view change (switch
// 9), 5 intrude (switch 10); 3 and 4 shift up and down a gear. The cabinet's
// shifter is an H pattern whose gears each close two switches: 5 the left
// column (1, 2), 6 the right one (5, 6), 3 the top row (1, 3, 5), 4 the
// bottom one (2, 4, 6); neutral closes none. It starts in neutral; shifting
// up first selects first, and down never goes below it. Switch 2 (a terminal
// cabinet) stays open.

void namcoEs1Wmmt4Panel(JVSIO *io)
{
    static int gear, lastUp, lastDown;
    int in = io->state.inputSwitch[PLAYER_1];
    int up = (in & BUTTON_3) != 0, down = (in & BUTTON_4) != 0;

    if (up && !lastUp && gear < 6)
        gear++;
    if (down && !lastDown && gear > 1)
        gear--;
    lastUp = up;
    lastDown = down;

    int out = in & ~(BUTTON_2 | BUTTON_3 | BUTTON_4 | BUTTON_5 | BUTTON_6 | BUTTON_7 | BUTTON_8 | BUTTON_9 | BUTTON_10);
    if (in & BUTTON_2)
        out |= BUTTON_9;
    if (in & BUTTON_5)
        out |= BUTTON_10;
    if (gear == 1 || gear == 2)
        out |= BUTTON_5;
    if (gear == 5 || gear == 6)
        out |= BUTTON_6;
    if (gear == 1 || gear == 3 || gear == 5)
        out |= BUTTON_3;
    if (gear == 2 || gear == 4 || gear == 6)
        out |= BUTTON_4;
    io->state.inputSwitch[PLAYER_1] = out;
}

// ---------------------------------------------------------------------------
// The steering wheel's force feedback, as the Pacloader fork takes it
// (es1Wmmt4Ffb.cpp): the game's force feedback object hands its output to
// its steering board driver once a frame (force, spring, damper and
// vibration, floats), which goes to the evdev wheel while the game has the
// motor powered. Its power on/off is the board's motor power too (the
// cabinet switches it through its I/O board: State_PowerOffWait fails with
// E2212 otherwise), and its self-check start completes the driver's check.
// The vibration is not played (no such effect on evdev here).
// NAMCO_FFB_TRACE set: the output on stderr every second.

typedef void (*FfbApply)(void *, float, float, float, float);
typedef void (*FfbPower)(void *, int);
typedef void (*FfbSelfCheck)(void *);

static FfbApply realFfbApply;
static FfbPower realFfbPowerOn, realFfbPowerOff;
static FfbSelfCheck realFfbSelfCheck;
static volatile int ffbPowered, ffbTrace;

static float clampUnit(float value)
{
    return value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : value;
}

static void ffbOff(void)
{
    evdevFfbConstant(0);
    evdevFfbSpring(0);
    evdevFfbDamper(0);
}

__attribute__((force_align_arg_pointer)) static void ffbApply(void *object, float force, float spring, float damper,
                                                              float vibration)
{
    static unsigned frames;

    realFfbApply(object, force, spring, damper, vibration);
    if (ffbTrace && frames++ % 60 == 0)
        fprintf(stderr, "WMMT4 FFB: %s force %.3f spring %.3f damper %.3f vibration %.3f\n",
                ffbPowered ? "on" : "off", force, spring, damper, vibration);
    if (!ffbPowered)
        return;
    evdevFfbConstant(clampUnit(force));
    evdevFfbSpring(clampUnit(spring < 0 ? 0 : spring));
    evdevFfbDamper(clampUnit(damper < 0 ? 0 : damper));
}

__attribute__((force_align_arg_pointer)) static void ffbPowerOn(void *object, int running)
{
    namcoEs1KickbackReportMotorPower(1);
    realFfbPowerOn(object, running);
    ffbPowered = (running & 0xff) != 0;
}

__attribute__((force_align_arg_pointer)) static void ffbPowerOff(void *object, int running)
{
    namcoEs1KickbackReportMotorPower(0);
    realFfbPowerOff(object, running);
    ffbPowered = 0;
    ffbOff();
}

// The driver's check, completed as the board's own answer would (the
// fork's completeVirtualSelfCheck): its driver is at +4 in the object.
__attribute__((force_align_arg_pointer)) static void ffbSelfCheck(void *object)
{
    static const uint8_t completeEntry[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0xc6, 0x40, 0x40, 0x00};
    int (*complete)(void *) = (int (*)(void *))0x08366230;

    realFfbSelfCheck(object);
    if (object && *(void **)((uint8_t *)object + 4) &&
        !memcmp((const void *)(uintptr_t)0x08366230, completeEntry, sizeof(completeEntry)))
    {
        complete(*(void **)((uint8_t *)object + 4));
        ((uint8_t *)object)[0x50] = 0;
    }
    namcoEs1KickbackReportSelfCheck();
}

static void hookWithOriginal(uint32_t address, const uint8_t *expected, size_t n, void *replacement, void *original)
{
    if (memcmp((const void *)(uintptr_t)address, expected, n) != 0)
    {
        log_error("WMMT4: unexpected code at %#x, not hooked", address);
        return;
    }
    if (MH_CreateHook((void *)(uintptr_t)address, replacement, (void **)original) != MH_OK ||
        MH_EnableHook((void *)(uintptr_t)address) != MH_OK)
        log_error("WMMT4: cannot hook %#x", address);
}

static void installFfb(void)
{
    static const uint8_t applyEntry[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x8b, 0x40,
                                         0x04, 0x89, 0x45, 0x08, 0x5d, 0xe9};
    static const uint8_t powerEntry[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x0f, 0xb6,
                                         0x45, 0x0c, 0x89, 0x45, 0x0c, 0x8b, 0x45, 0x08};
    static const uint8_t selfCheckEntry[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x8b, 0x45,
                                             0x08, 0xc6, 0x40, 0x50, 0x01, 0x8b, 0x40, 0x04};

    ffbTrace = getenv("NAMCO_FFB_TRACE") != NULL;
    MH_Initialize(); // already done is fine
    hookWithOriginal(0x08366e30, applyEntry, sizeof(applyEntry), (void *)ffbApply, &realFfbApply);
    hookWithOriginal(0x08367aa0, powerEntry, sizeof(powerEntry), (void *)ffbPowerOn, &realFfbPowerOn);
    hookWithOriginal(0x08367ac0, powerEntry, sizeof(powerEntry), (void *)ffbPowerOff, &realFfbPowerOff);
    hookWithOriginal(0x08367ee0, selfCheckEntry, sizeof(selfCheckEntry), (void *)ffbSelfCheck, &realFfbSelfCheck);
    evdevFfbOpen("WMMT4");
}

// ---------------------------------------------------------------------------

void namcoEs1Wmmt4Install(void)
{
    static const uint8_t haspEntry[] = {0x83, 0xec}; // sub $imm8,%esp
    static const uint8_t frameEntry[] = {0x55, 0x89, 0xe5}; // push %ebp; mov %esp,%ebp

    gameLog = getenv("NAMCO_GAME_LOG") != NULL;
    loadGlEntryPoints();
    haspImage();
    detour(0x08921fd0, haspEntry, sizeof(haspEntry), haspLogin);
    detour(0x08922070, haspEntry, sizeof(haspEntry), haspNothing);  // hasp_logout
    detour(0x0892215c, haspEntry, sizeof(haspEntry), haspNothing);  // hasp_encrypt
    detour(0x08922248, haspEntry, sizeof(haspEntry), haspNothing);  // hasp_decrypt
    detour(0x08922f60, haspEntry, sizeof(haspEntry), haspGetSize);
    detour(0x08922dc8, haspEntry, sizeof(haspEntry), haspRead);
    detour(0x08922e94, haspEntry, sizeof(haspEntry), haspWrite);
    detour(0x08390ba0, frameEntry, sizeof(frameEntry), jvioMonitor);
    detour(0x089e5c80, frameEntry, sizeof(frameEntry), sendCommand);
    detour(0x0809ddb0, frameEntry, sizeof(frameEntry), logMes);
    detour(0x0821c920, (const uint8_t[]){0x55}, 1, addressChanged);
    detour(0x0821d4f0, frameEntry, sizeof(frameEntry), interfaceUpdate);
    detour(0x080aa7d0, frameEntry, sizeof(frameEntry), networkUpdateState);
    *(void **)&realOpenDisplay = hookExecutableImport("XOpenDisplay", (void *)openDisplay);
    *(void **)&realCreateWindow = hookExecutableImport("XCreateWindow", (void *)createWindow);
    *(void **)&realSetsockopt = hookExecutableImport("setsockopt", (void *)gameSetsockopt);
    *(void **)&realRecvmsg = hookExecutableImport("recvmsg", (void *)gameRecvmsg);
    *(void **)&realRecvfrom = hookExecutableImport("recvfrom", (void *)gameRecvfrom);
    *(void **)&realGetProcAddress = hookExecutableImport("glXGetProcAddress", (void *)getProcAddress);
    *(void **)&realGetWindowAttributes = hookExecutableImport("XGetWindowAttributes", (void *)getWindowAttributes);
    if (getenv("WMMT4_SHOT_PERIOD"))
        shotPeriod = atoi(getenv("WMMT4_SHOT_PERIOD"));
    if ((shotDir = getenv("WMMT4_SHOT")))
        *(void **)&realSwapBuffers = hookExecutableImport("glXSwapBuffers", (void *)swapBuffers);
    *(void **)&realChangeProperty = hookExecutableImport("XChangeProperty", (void *)changeProperty);
    *(void **)&realMoveResizeWindow = hookExecutableImport("XMoveResizeWindow", (void *)moveResizeWindow);
    *(void **)&realGetLatestProfile = hookExecutableImport("cgGLGetLatestProfile", (void *)getLatestProfile);
    *(void **)&realCreateProgram = hookExecutableImport("cgCreateProgram", (void *)createProgram);
    installFfb();
    namcoEs1Wmmt4StartTerminal(HASP_SERIAL);
    log_info("WMMT4: dongle serial %s", HASP_SERIAL);
}
