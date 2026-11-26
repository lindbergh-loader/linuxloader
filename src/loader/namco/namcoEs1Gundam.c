// Mobile Suit Gundam: Bonds of the Battlefield (Namco ES1): what only it
// needs. Its network check resolves the shop's content router by name
// (tenporouter.loc, from the router's own DNS): the router of its eth0
// network here (see namcoEs1Games.c). Its sockets are bound to its eth0
// address, which the host does not have (EADDRNOTAVAIL): bound to any address
// instead, reusable (it binds each port on the broadcast address too).
//
// GKE_SHOT=<directory> (debugging): its frames saved there as PPM, one every
// GKE_SHOT_PERIOD milliseconds (default 500), the first 600.

#include <dlfcn.h>
#include <arpa/inet.h>
#include <math.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xutil.h>

#include "namcoEs1Game.h"
#include "../common/importHook.h"
#include "../../minhook/include/MinHook.h"

#define CABINET_ADDRESS "192.168.92.11"
// Where its sockets bound to the broadcast address are (see gameBind, gameSendto).
#define BROADCAST_LOOPBACK "127.0.0.2"

static struct hostent *(*realGethostbyname)(const char *name);
static int (*realBind)(int fd, const struct sockaddr *address, socklen_t length);
static int (*realConnect)(int fd, const struct sockaddr *address, socklen_t length);
static ssize_t (*realSendto)(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t length);
static ssize_t (*realRecvfrom)(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *length);
static int (*realAccept)(int fd, struct sockaddr *address, socklen_t *length);
static int (*realGetsockname)(int fd, struct sockaddr *address, socklen_t *length);
static void (*realSwapBuffers)(Display *dpy, GLXDrawable drawable);
static const char *shotDir;
static long shotPeriod = 500;

__attribute__((force_align_arg_pointer)) static struct hostent *gameGethostbyname(const char *name)
{
    return realGethostbyname(name && !strcmp(name, "tenporouter.loc") ? "192.168.92.1" : name);
}

__attribute__((force_align_arg_pointer)) static int gameBind(int fd, const struct sockaddr *address, socklen_t length)
{
    if (address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in))
    {
        struct sockaddr_in local = *(const struct sockaddr_in *)address;
        int on = 1;
        if (local.sin_addr.s_addr == inet_addr(CABINET_ADDRESS))
            local.sin_addr.s_addr = htonl(INADDR_ANY);
        else if (local.sin_addr.s_addr == INADDR_BROADCAST) // its broadcasts' receiver
            local.sin_addr.s_addr = inet_addr(BROADCAST_LOOPBACK);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        return realBind(fd, (struct sockaddr *)&local, sizeof(local));
    }
    return realBind(fd, address, length);
}

// Its own address (its servers, the relay server's) is this machine's: sent
// to the loopback; what comes from this machine (the loopback, the host's
// addresses) comes from its address. Its broadcasts (how the PODs and the
// local server find each other) stay on this machine: a host does not always
// deliver its own, and the shop's LAN is not there. They go to its sockets
// bound to the broadcast address (on BROADCAST_LOOPBACK here) and to its
// sockets bound to any address, as a broadcast would.
static int hostAddress(in_addr_t address)
{
    static in_addr_t own[16];
    static int count = -1;
    if (count < 0)
    {
        struct ifaddrs *list, *i;
        count = 0;
        if (getifaddrs(&list) == 0)
        {
            for (i = list; i && count < 16; i = i->ifa_next)
                if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET)
                    own[count++] = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
            freeifaddrs(list);
        }
    }
    if ((ntohl(address) >> 24) == 127)
        return 1;
    for (int i = 0; i < count; i++)
        if (own[i] == address)
            return 1;
    return 0;
}

static const struct sockaddr *toHost(const struct sockaddr *address, socklen_t length, struct sockaddr_in *buf)
{
    in_addr_t to = address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)
                       ? ((const struct sockaddr_in *)address)->sin_addr.s_addr
                       : 0;
    if (to && to == inet_addr(CABINET_ADDRESS))
    {
        *buf = *(const struct sockaddr_in *)address;
        buf->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return (struct sockaddr *)buf;
    }
    return address;
}

static void fromHost(struct sockaddr *address, const socklen_t *length)
{
    if (address && length && *length >= sizeof(struct sockaddr_in) && address->sa_family == AF_INET &&
        hostAddress(((struct sockaddr_in *)address)->sin_addr.s_addr))
        ((struct sockaddr_in *)address)->sin_addr.s_addr = inet_addr(CABINET_ADDRESS);
}

__attribute__((force_align_arg_pointer)) static int gameConnect(int fd, const struct sockaddr *address,
                                                                socklen_t length)
{
    struct sockaddr_in buf;
    return realConnect(fd, toHost(address, length, &buf), length);
}

__attribute__((force_align_arg_pointer)) static ssize_t gameSendto(int fd, const void *data, size_t n, int flags,
                                                                   const struct sockaddr *to, socklen_t length)
{
    struct sockaddr_in buf;
    if (to && to->sa_family == AF_INET && length >= sizeof(struct sockaddr_in) &&
        (((const struct sockaddr_in *)to)->sin_addr.s_addr == INADDR_BROADCAST ||
         ((const struct sockaddr_in *)to)->sin_addr.s_addr == inet_addr("192.168.92.255")))
    {
        buf = *(const struct sockaddr_in *)to;
        buf.sin_addr.s_addr = inet_addr(BROADCAST_LOOPBACK);
        ssize_t r = realSendto(fd, data, n, flags, (struct sockaddr *)&buf, sizeof(buf));
        buf.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        realSendto(fd, data, n, flags, (struct sockaddr *)&buf, sizeof(buf));
        return r;
    }
    return realSendto(fd, data, n, flags, toHost(to, length, &buf), length);
}

__attribute__((force_align_arg_pointer)) static ssize_t gameRecvfrom(int fd, void *data, size_t n, int flags,
                                                                     struct sockaddr *from, socklen_t *length)
{
    ssize_t r = realRecvfrom(fd, data, n, flags, from, length);
    if (r >= 0)
        fromHost(from, length);
    return r;
}

__attribute__((force_align_arg_pointer)) static int gameAccept(int fd, struct sockaddr *address, socklen_t *length)
{
    int r = realAccept(fd, address, length);
    if (r >= 0)
        fromHost(address, length);
    return r;
}

__attribute__((force_align_arg_pointer)) static int gameGetsockname(int fd, struct sockaddr *address,
                                                                    socklen_t *length)
{
    int r = realGetsockname(fd, address, length);
    if (r == 0)
        fromHost(address, length);
    return r;
}

static int currentFbo;
static void (*realBindFramebuffer)(GLenum target, GLuint fbo);
static void (*realBindTexture)(GLenum target, GLuint texture);

// ---------------------------------------------------------------------------
// The dome. The game draws its 3D picture in a framebuffer object as two
// halves side by side, each its camera's (87.5 degrees wide, turned 43.75
// degrees left and right, 739x1358 at (285, 345) and (1024, 345) of a
// 2048x2048 texture), then the window from them through a lookup texture
// (a 1024x1024 LUMINANCE16_ALPHA16 one: for each pixel of the window's
// 1024x768, the halves' texture coordinates it shows; ARB program "TEX
// R0.xw, ..., texture[1]; TEX R0, R0.xwxx, texture[0]"), made for the
// dome's projector. As for Mach Storm (windowsarcadeloader), the lookup gets
// a flat screen map instead: a cylinder, 160 degrees wide (the pixel's
// angle across it follows x), its height as a plane's, the same scale as
// across at its centre for the 4:3 window, the horizon high as on the dome
// (its HUD at the bottom shown); each pixel's direction projected by the
// camera of the half it is in, so the 3D halves join without a bend (the 2D
// panels across the middle are drawn folded there, as on the cabinet).
//
// The 2D screens alone (notices, the test menu) are drawn at (0, 1280),
// 1024x768, of the same texture, and the boot's screens whole in an object
// of their own (1024x768, then reduced in another, last): without the 3D
// halves in a frame, the last object drawn to before the window is copied
// over it at the swap, its aspect kept. GKE_DOME=1 keeps the cabinet's
// warped picture.

#define MAP_SIZE 1024
static int flatten = 1;
static GLuint lookupTexture;
static uint16_t *flatMap;
static void (*realTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*realTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);

static void makeFlatMap(void)
{
    const double halfWide = 87.5 * M_PI / 180, across = 80 * M_PI / 180;
    const double tanHalf = tan(halfWide / 2), tanHigh = tanHalf * 1358 / 739;
    const double height = 0.75 * across; // a plane's height at the cylinder's 4:3 scale
    const int horizon = 320;              // the window's row of the horizon (the cabinet's 314)

    flatMap = calloc((size_t)MAP_SIZE * MAP_SIZE * 2, sizeof(uint16_t));
    for (int y = 0; y < 768; y++) // row 0 at the top of the window
        for (int x = 0; x < 1024; x++)
        {
            double nx = (x + 0.5) / 512 - 1, ny = (horizon - (y + 0.5)) / 384;
            double angle = nx * across, up = ny * height;
            int right = angle >= 0;
            double local = angle - (right ? halfWide / 2 : -halfWide / 2);
            // The direction (sin, up, cos) in the half's camera.
            double tx = tan(local) / tanHalf, ty = up / cos(local) / tanHigh;
            double u = ((right ? 1024 : 285) + 739 * (tx + 1) / 2) / 2048;
            double v = (345 + 1358 * (ty + 1) / 2) / 2048;
            uint16_t *texel = flatMap + ((size_t)y * MAP_SIZE + x) * 2;
            texel[0] = (uint16_t)(fmin(fmax(u, 0), 1) * 65535);
            texel[1] = (uint16_t)(fmin(fmax(v, 0), 1) * 65535);
        }
}

// After each upload to the lookup texture: the flat screen map over it.
static void flatLookup(GLenum target)
{
    GLint bound = 0;
    if (!flatten || target != GL_TEXTURE_2D || !lookupTexture)
        return;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if ((GLuint)bound != lookupTexture)
        return;
    if (!flatMap)
        makeFlatMap();
    realTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAP_SIZE, MAP_SIZE, GL_LUMINANCE_ALPHA, GL_UNSIGNED_SHORT, flatMap);
}

__attribute__((force_align_arg_pointer)) static void gameTexImage2D(GLenum target, GLint level, GLint internal,
                                                                    GLsizei w, GLsizei h, GLint border, GLenum format,
                                                                    GLenum type, const void *pixels)
{
    realTexImage2D(target, level, internal, w, h, border, format, type, pixels);
    if (target == GL_TEXTURE_2D && level == 0 && internal == 0x8048 /* LUMINANCE16_ALPHA16 */ && w == MAP_SIZE &&
        h == MAP_SIZE)
    {
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        lookupTexture = bound;
    }
    if (level == 0)
        flatLookup(target);
}

__attribute__((force_align_arg_pointer)) static void gameTexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
                                                                       GLsizei w, GLsizei h, GLenum format,
                                                                       GLenum type, const void *pixels)
{
    realTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
    if (level == 0)
        flatLookup(target);
}

static int sceneFbo, halvesDrawn, screenDrawn, bootFbo;
static void (*realViewport)(GLint x, GLint y, GLsizei w, GLsizei h);

__attribute__((force_align_arg_pointer)) static void gameViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w == 739 && h == 1358)
        halvesDrawn = 1;
    else if (x == 0 && y == 1280 && w == 1024 && h == 768)
        screenDrawn = 1;
    else if (x == 0 && y == 0 && w == 1024 && h == 768 && currentFbo > 0)
        bootFbo = currentFbo;
    realViewport(x, y, w, h);
}

static void (*blitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

__attribute__((force_align_arg_pointer)) static void gameBindFramebuffer(GLenum target, GLuint fbo)
{
    if (fbo == 0 && currentFbo > 0)
        sceneFbo = currentFbo;
    currentFbo = fbo;
    realBindFramebuffer(target, fbo);
}

// The picture's rectangle in a framebuffer object (its color texture's size,
// cached).
static int pictureRect(GLint fbo, GLint rect[4])
{
    static GLint sizes[64][2];
    static void (*getAttachment)(GLenum, GLenum, GLenum, GLint *);
    if (fbo <= 0 || fbo >= 64)
        return 0;
    if (!sizes[fbo][0])
    {
        GLint texture = 0, saved = 0;
        if (!getAttachment)
            *(void **)&getAttachment = dlsym(RTLD_DEFAULT, "glGetFramebufferAttachmentParameterivEXT");
        if (!getAttachment)
            return 0;
        getAttachment(0x8CA8 /* READ_FRAMEBUFFER */, 0x8CE0 /* COLOR_ATTACHMENT0 */, 0x8CD1 /* OBJECT_NAME */,
                      &texture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved);
        realBindTexture(GL_TEXTURE_2D, texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &sizes[fbo][0]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sizes[fbo][1]);
        realBindTexture(GL_TEXTURE_2D, saved);
        if (sizes[fbo][0] <= 0 || sizes[fbo][1] <= 0)
        {
            sizes[fbo][0] = 0;
            return 0;
        }
    }
    if (sizes[fbo][0] == 2048 && sizes[fbo][1] == 2048 && halvesDrawn)
    {
        rect[0] = 285, rect[1] = 345, rect[2] = 2 * 739, rect[3] = 1358;
        return 1;
    }
    if (sizes[fbo][0] == 2048 && sizes[fbo][1] == 2048 && screenDrawn)
    {
        rect[0] = 0, rect[1] = 1280, rect[2] = 1024, rect[3] = 768;
        return 1;
    }
    rect[0] = rect[1] = 0, rect[2] = sizes[fbo][0], rect[3] = sizes[fbo][1];
    return 1;
}

// The flat picture's own pass draws the window's 1024x768 (its "xga") in its
// corner: copied to a framebuffer object of that size (a multisampled window
// resolved), from which it is scaled as the other pictures.
static GLuint cornerFbo(void)
{
    static GLuint fbo, buffer;
    static int failed;
    void (*genFramebuffers)(GLsizei, GLuint *);
    void (*genRenderbuffers)(GLsizei, GLuint *);
    void (*bindRenderbuffer)(GLenum, GLuint);
    void (*renderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void (*framebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    GLenum (*checkFramebuffer)(GLenum);

    if (fbo || failed)
        return fbo;
    *(void **)&genFramebuffers = dlsym(RTLD_DEFAULT, "glGenFramebuffersEXT");
    *(void **)&genRenderbuffers = dlsym(RTLD_DEFAULT, "glGenRenderbuffersEXT");
    *(void **)&bindRenderbuffer = dlsym(RTLD_DEFAULT, "glBindRenderbufferEXT");
    *(void **)&renderbufferStorage = dlsym(RTLD_DEFAULT, "glRenderbufferStorageEXT");
    *(void **)&framebufferRenderbuffer = dlsym(RTLD_DEFAULT, "glFramebufferRenderbufferEXT");
    *(void **)&checkFramebuffer = dlsym(RTLD_DEFAULT, "glCheckFramebufferStatusEXT");
    failed = 1;
    if (!genFramebuffers || !genRenderbuffers || !bindRenderbuffer || !renderbufferStorage ||
        !framebufferRenderbuffer || !checkFramebuffer)
        return 0;
    GLint savedBuffer = 0;
    glGetIntegerv(0x8CA7 /* RENDERBUFFER_BINDING */, &savedBuffer);
    genRenderbuffers(1, &buffer);
    bindRenderbuffer(0x8D41 /* RENDERBUFFER */, buffer);
    renderbufferStorage(0x8D41, GL_RGBA8, 1024, 768);
    bindRenderbuffer(0x8D41, savedBuffer);
    genFramebuffers(1, &fbo);
    realBindFramebuffer(0x8CA9 /* DRAW_FRAMEBUFFER */, fbo);
    framebufferRenderbuffer(0x8CA9, 0x8CE0 /* COLOR_ATTACHMENT0 */, 0x8D41, buffer);
    if (checkFramebuffer(0x8CA9) != 0x8CD5 /* FRAMEBUFFER_COMPLETE */)
    {
        fprintf(stderr, "Gundam: no framebuffer object to scale the picture\n");
        fbo = 0;
        return 0;
    }
    failed = 0;
    return fbo;
}

static void flatPicture(Display *dpy, GLXDrawable drawable)
{
    unsigned width = 0, height = 0;
    GLint v[4], read, draw, scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLfloat clear[4];

    if (!blitFramebuffer || !sceneFbo)
        return;
    int found = 0;
    glGetIntegerv(0x8CAA /* READ_FRAMEBUFFER_BINDING */, &read);
    glGetIntegerv(0x8CA6 /* DRAW_FRAMEBUFFER_BINDING */, &draw);
    glXQueryDrawable(dpy, drawable, GLX_WIDTH, &width);
    glXQueryDrawable(dpy, drawable, GLX_HEIGHT, &height);
    if (!width || !height)
        return;
    glDisable(GL_SCISSOR_TEST);
    GLint source = sceneFbo;
    if (halvesDrawn && lookupTexture)
    {
        // Through the flat screen map (the game's own pass), in the window's
        // corner: scaled unless the window is its size.
        halvesDrawn = screenDrawn = bootFbo = 0;
        if ((width == 1024 && height == 768) || !(source = cornerFbo()))
            goto restore;
        realBindFramebuffer(0x8CA8 /* READ_FRAMEBUFFER */, 0);
        realBindFramebuffer(0x8CA9 /* DRAW_FRAMEBUFFER */, source);
        blitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        v[0] = v[1] = 0, v[2] = 1024, v[3] = 768;
        realBindFramebuffer(0x8CA8, source);
    }
    else
    {
        if (!halvesDrawn && !screenDrawn && bootFbo)
            source = bootFbo;
        realBindFramebuffer(0x8CA8 /* READ_FRAMEBUFFER */, source);
        found = pictureRect(source, v);
        halvesDrawn = screenDrawn = bootFbo = 0;
        if (!found)
            goto restore;
    }
    // The picture's aspect kept, centred.
    unsigned w = width, h = (unsigned)((double)width * v[3] / v[2]);
    if (h > height)
        h = height, w = (unsigned)((double)height * v[2] / v[3]);
    GLint x = (width - w) / 2, y = (height - h) / 2;

    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    realBindFramebuffer(0x8CA9 /* DRAW_FRAMEBUFFER */, 0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    blitFramebuffer(v[0], v[1], v[0] + v[2], v[1] + v[3], x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
restore:
    realBindFramebuffer(0x8CA8, read);
    realBindFramebuffer(0x8CA9, draw);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

// Its timing follows the projector's 60 Hz swaps; without vertical sync (or
// on a faster screen) it runs too fast: 60 frames a second, whatever the
// display does.
static void limitFrameRate(void)
{
    static struct timespec deadline;
    struct timespec now;
    const long frame = 1000000000L / 60;

    clock_gettime(CLOCK_MONOTONIC, &now);
    deadline.tv_nsec += frame;
    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    // Late by more than a frame (or the first frame): from now on.
    long late = (now.tv_sec - deadline.tv_sec) * 1000000000L + (now.tv_nsec - deadline.tv_nsec);
    if (late > frame)
    {
        deadline = now;
        return;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
}



__attribute__((force_align_arg_pointer)) static void gameSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    static void (*readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    static long next;
    static int count;
    struct timespec ts;

    if (flatten)
        flatPicture(dpy, drawable);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (!readPixels)
        *(void **)&readPixels = dlsym(RTLD_DEFAULT, "glReadPixels");
    if (shotDir && readPixels && count < 600 && now >= next)
    {
        // Its "xga" picture (its drawable is a GLX one: XGetGeometry fails).
        unsigned width = 1024, height = 768;
        static int shotFbo = -1;
        GLint savedRead = 0, texture = 0, savedTexture = 0, w = 0, h = 0;
        static void (*getAttachment)(GLenum, GLenum, GLenum, GLint *);
        if (shotFbo < 0)
        {
            shotFbo = getenv("GKE_SHOT_FBO") ? atoi(getenv("GKE_SHOT_FBO")) : 0;
            *(void **)&getAttachment = dlsym(RTLD_DEFAULT, "glGetFramebufferAttachmentParameterivEXT");
        }
        if (shotFbo && realBindFramebuffer && getAttachment)
        {
            glGetIntegerv(0x8CAA /* READ_FRAMEBUFFER_BINDING */, &savedRead);
            realBindFramebuffer(0x8CA8 /* READ_FRAMEBUFFER */, shotFbo);
            getAttachment(0x8CA8, 0x8CE0, 0x8CD1 /* OBJECT_NAME */, &texture);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture);
            realBindTexture(GL_TEXTURE_2D, texture);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            realBindTexture(GL_TEXTURE_2D, savedTexture);
            if (w > 0 && h > 0)
                width = w, height = h;
            if (count == 0)
                fprintf(stderr, "GKE_SHOT: fbo %d texture %d %ux%u\n", shotFbo, texture, width, height);
        }
        next = now + shotPeriod;
        unsigned char *pixels = malloc((size_t)width * height * 3);
        char path[512];
        snprintf(path, sizeof(path), "%s/frame%04d.ppm", shotDir, count++);
        FILE *f = fopen(path, "wb");
        if (pixels && f)
        {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            if (!shotFbo)
                glReadBuffer(GL_BACK);
            readPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
            fprintf(f, "P6\n%u %u\n255\n", width, height);
            for (unsigned row = height; row-- > 0;)
                fwrite(pixels + (size_t)row * width * 3, 1, (size_t)width * 3, f);
        }
        if (f)
            fclose(f);
        free(pixels);
        if (shotFbo && realBindFramebuffer)
            realBindFramebuffer(0x8CA8, savedRead);
    }
    realSwapBuffers(dpy, drawable);
    limitFrameRate();
}

// Replace n bytes of code at address, if they are the expected ones.
static void patchCode(uint32_t address, const uint8_t *expected, const uint8_t *replacement, size_t n)
{
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (memcmp((void *)(uintptr_t)address, expected, n) != 0 ||
        mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        fprintf(stderr, "Gundam: unexpected code at %#x, not patched\n", address);
        return;
    }
    memcpy((void *)(uintptr_t)address, replacement, n);
}

// ---------------------------------------------------------------------------
// The noise cancel board, on the I/O board's lines: the boot's check (E[21-01],
// "NOISE CANCEL PC board error") raises a general purpose output (bit 6 of
// its first byte) and waits for the board's answer on player 1 button 6, low
// before, high after. The answer follows the output.

static volatile uint8_t gpo[2];

static void gundamGpo(unsigned char index, unsigned char data)
{
    if (index < sizeof(gpo))
        gpo[index] = data;
}

// The POD's own switches are its board's player 1 switches 17 to 32 (its
// input table, 0x8cc46e0, reads the player's second word: game bits 3
// start, 1, 11, then the levers' buttons 4 6 5 7 and 12 14 0 2), the levers'
// buttons active low on this (Japanese) wiring: the board reports 32
// switches a player (the loader's 16 left them unread, the buttons held). In
// the loader's state those are player 1's bits 31..16 (jvs.c). From the
// loader's player 1: start (1) is the POD's start, space z x c the first
// lever group, v Page Up Page Down the second.

#define POD(mask) ((mask) << 16)

static int pod(int in, int bit)
{
    return (in & bit) != 0;
}

void namcoEs1GundamPanel(JVSIO *io)
{
    static int gpoHooked;
    int in = io->state.inputSwitch[PLAYER_1];
    if (!gpoHooked)
    {
        setJVSGpoHandler(gundamGpo);
        io->capabilities.switches = 32;
        gpoHooked = 1;
    }
    int p1 = in & 0xffff & ~(BUTTON_6 | BUTTON_7 | BUTTON_8 | BUTTON_9 | BUTTON_10);
    if (pod(in, BUTTON_START))
        p1 |= POD(0x8000);
    // Active low: released reads high.
    if (!pod(in, BUTTON_1))
        p1 |= POD(0x0800);
    if (!pod(in, BUTTON_2))
        p1 |= POD(0x0400);
    if (!pod(in, BUTTON_3))
        p1 |= POD(0x0200);
    if (!pod(in, BUTTON_4))
        p1 |= POD(0x0100);
    if (!pod(in, BUTTON_5))
        p1 |= BUTTON_7;
    if (!pod(in, BUTTON_7))
        p1 |= BUTTON_8;
    if (!pod(in, BUTTON_8))
        p1 |= BUTTON_9;
    if (!pod(in, BUTTON_LEFT))
        p1 |= BUTTON_10;
    p1 &= ~(BUTTON_LEFT | BUTTON_RIGHT);
    // TEST: the chat's other two switches (one of them may be the start).
    if (io->state.inputSwitch[PLAYER_2] & BUTTON_START) // 2
        p1 |= POD(0x4000);
    if (pod(in, BUTTON_RIGHT))
        p1 |= POD(0x2000);
    if ((gpo[0] | gpo[1]) & 0x40)
        p1 |= BUTTON_6;
    io->state.inputSwitch[PLAYER_1] = p1;
    // The levers at rest, centred (0 read full left and down); the pedals
    // released.
    for (int i = 0; i < 4; i++)
        io->state.analogueChannel[i] = 0x8000;
}

// ---------------------------------------------------------------------------
// The Banapassport card reader (Card R/W, /dev/ttyS1): driven through the
// BngRw library, linked in (stripped; the entry points found by their
// arguments, as WMMT4's, namcoEs1Wmmt4Card.c). Its calls are answered here,
// completion callbacks called at once (status 0: done). No card is put on the
// reader: a touch wait stays pending.

#define BNG_OK 1
#define BNG_BAD_DEVICE (-100)
#define BNG_NO_CARD (-102)

typedef void (*BngDone)(int device, int status, void *user);

static int bngBadDevice(int device)
{
    return device < 0 || device > 1;
}

static int bngComplete(int device, BngDone done, void *user)
{
    if (bngBadDevice(device))
        return BNG_BAD_DEVICE;
    if (done)
        done(device, 0, user);
    return BNG_OK;
}

// BngRwIsCmdExec: no command running.
__attribute__((force_align_arg_pointer)) static int bngIsCmdExec(int device)
{
    return bngBadDevice(device) ? -1 : 0;
}

__attribute__((force_align_arg_pointer)) static int bngAttach(int device, const char *port, int a, int b, BngDone done,
                                                              void *user)
{
    (void)port;
    (void)a;
    (void)b;
    return bngComplete(device, done, user);
}

__attribute__((force_align_arg_pointer)) static int bngReset(int device, BngDone done, void *user)
{
    return bngComplete(device, done, user);
}

// LED and beep requests.
__attribute__((force_align_arg_pointer)) static int bngRequest(int device, int value, BngDone done, void *user)
{
    (void)value;
    return bngComplete(device, done, user);
}

__attribute__((force_align_arg_pointer)) static int bngCancel(int device)
{
    return bngBadDevice(device) ? BNG_BAD_DEVICE : BNG_OK;
}

__attribute__((force_align_arg_pointer)) static int bngWaitTouch(int device, int a, int b, void *touched, void *user)
{
    (void)a;
    (void)b;
    (void)touched;
    (void)user;
    return bngBadDevice(device) ? BNG_BAD_DEVICE : BNG_OK;
}

__attribute__((force_align_arg_pointer)) static int bngAuth(int device, int a, int b, const void *version,
                                                            const char *request, const char *extra, BngDone done,
                                                            void *user)
{
    (void)a;
    (void)b;
    (void)version;
    (void)request;
    (void)extra;
    (void)done;
    (void)user;
    return bngBadDevice(device) ? BNG_BAD_DEVICE : BNG_NO_CARD;
}

static void detour(uint32_t address, void *replacement)
{
    void *original;
    if (*(const uint8_t *)(uintptr_t)address != 0x55 || // push %ebp
        MH_CreateHook((void *)(uintptr_t)address, replacement, &original) != MH_OK ||
        MH_EnableHook((void *)(uintptr_t)address) != MH_OK)
        fprintf(stderr, "Gundam: cannot hook %#x\n", address);
}

// ---------------------------------------------------------------------------
// The voice letter unit (AmiVoice voice recognition, on /dev/ttyS3): its
// module's status is answered (E[22-01], "VOICE LETTER PC board error",
// without the unit). The boot's check waits for "ready" (0), starts the unit
// and waits for "running" (1); its serial traffic is not run.

static volatile int voiceStarted;

__attribute__((force_align_arg_pointer)) static int voiceStatus(void)
{
    return voiceStarted ? 1 : 0;
}

__attribute__((force_align_arg_pointer)) static void voiceStart(void)
{
    voiceStarted = 1;
}

__attribute__((force_align_arg_pointer)) static void voiceNothing(void)
{
}

// ---------------------------------------------------------------------------
// Its local relay server (the shop's match server, run by the main terminal
// POD: "./n_gun_relay_server ./localserver_release.conf &") asks for the
// cabinet's dynamic linker: run through the host's, without the loader. It
// is stopped by name ("killall n_gun_relay_server"), which is ld-linux.so.2's
// now: by its command line instead.

__attribute__((force_align_arg_pointer)) static int gameSystem(const char *command)
{
    char buf[512];
    if (command && !strncmp(command, "./n_gun_relay_server ", 21))
    {
        // Stopped with the game too: it would outlive it (killed by the
        // frontend, its own process group's), holding its files, the rom's
        // mount with them.
        size_t n = strcspn(command, "&");
        snprintf(buf, sizeof(buf),
                 "(env -u LD_PRELOAD /lib/ld-linux.so.2 %.*s& r=$!; "
                 "while kill -0 %d 2>/dev/null && kill -0 $r 2>/dev/null; do sleep 1; done; kill $r 2>/dev/null) &",
                 (int)n, command, (int)getpid());
        command = buf;
    }
    else if (command && !strncmp(command, "killall n_gun_relay_server", 26))
    {
        snprintf(buf, sizeof(buf), "pkill -f '^/lib/ld-linux.so.2 ./n_gun_relay_server'%s", command + 26);
        command = buf;
    }
    return system(command); // the loader's
}

// ---------------------------------------------------------------------------
// Its window carries no WM hints: a window manager that follows the input
// hint (GNOME's) never gives it the keyboard, and the loader's keys (read
// from the X server's key map, Xwayland's only for a focused window) stay
// up. It accepts input.

static int (*realMapWindow)(Display *dpy, Window window);

// It opens its display by number (":0.0", from its settings): the loader's
// $DISPLAY instead, where its keyboard is read (on a test run's own X
// server, the game came out on the desktop's, its keys not seen).
__attribute__((force_align_arg_pointer)) static Display *gameOpenDisplay(const char *name)
{
    return XOpenDisplay(getenv("DISPLAY") ? NULL : name); // the loader's
}

__attribute__((force_align_arg_pointer)) static int gameMapWindow(Display *dpy, Window window)
{
    XWMHints hints = {.flags = InputHint, .input = True};
    XSetWMHints(dpy, window, &hints);
    return realMapWindow(dpy, window);
}

void namcoEs1GundamInstall(void)
{
    MH_Initialize(); // already done is fine
    detour(0x087c47e0, (void *)bngIsCmdExec);
    detour(0x087c486c, (void *)bngAttach);
    detour(0x087c4350, (void *)bngReset);
    detour(0x087c4114, (void *)bngRequest);
    detour(0x087c3ff4, (void *)bngRequest);
    detour(0x087c3b22, (void *)bngCancel);
    detour(0x087c421e, (void *)bngWaitTouch);
    detour(0x087c4476, (void *)bngAuth);
    detour(0x084bb6cc, (void *)voiceStatus);
    detour(0x084bb7f0, (void *)voiceStart);
    detour(0x084bb0e4, (void *)voiceNothing); // its serial exchange
    detour(0x084bb6e4, (void *)voiceNothing); // its port reopened after an error

    // This POD is the shop's main terminal: it runs the local (relay) server
    // the PODs connect to, found by itself (E[04-01], "cannot confirm the main
    // terminal", after a 3 minute search without one). The network setup's
    // flag for it (0x18d5c304, nothing else sets it): "movl $0,0x4(%ebx)"
    // made "movl $1,0x4(%ebx)".
    static const uint8_t flag0[] = {0xc7, 0x43, 0x04, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t flag1[] = {0xc7, 0x43, 0x04, 0x01, 0x00, 0x00, 0x00};
    patchCode(0x08054198, flag0, flag1, sizeof(flag1));
    *(void **)&realGethostbyname = hookExecutableImport("gethostbyname", (void *)gameGethostbyname);
    *(void **)&realBind = hookExecutableImport("bind", (void *)gameBind);
    hookExecutableImport("system", (void *)gameSystem);
    *(void **)&realConnect = hookExecutableImport("connect", (void *)gameConnect);
    *(void **)&realSendto = hookExecutableImport("sendto", (void *)gameSendto);
    *(void **)&realRecvfrom = hookExecutableImport("recvfrom", (void *)gameRecvfrom);
    *(void **)&realAccept = hookExecutableImport("accept", (void *)gameAccept);
    *(void **)&realGetsockname = hookExecutableImport("getsockname", (void *)gameGetsockname);
    flatten = !(getenv("GKE_DOME") && atoi(getenv("GKE_DOME")));
    hookExecutableImport("XOpenDisplay", (void *)gameOpenDisplay);
    *(void **)&realMapWindow = hookExecutableImport("XMapWindow", (void *)gameMapWindow);
    *(void **)&realBindFramebuffer = hookExecutableImport("glBindFramebufferEXT", (void *)gameBindFramebuffer);
    *(void **)&realViewport = hookExecutableImport("glViewport", (void *)gameViewport);
    *(void **)&realTexImage2D = hookExecutableImport("glTexImage2D", (void *)gameTexImage2D);
    *(void **)&realTexSubImage2D = hookExecutableImport("glTexSubImage2D", (void *)gameTexSubImage2D);
    *(void **)&realBindTexture = dlsym(RTLD_DEFAULT, "glBindTexture");
    if (!(*(void **)&blitFramebuffer = dlsym(RTLD_DEFAULT, "glBlitFramebuffer")))
        *(void **)&blitFramebuffer = dlsym(RTLD_DEFAULT, "glBlitFramebufferEXT");
    if ((shotDir = getenv("GKE_SHOT")) && getenv("GKE_SHOT_PERIOD"))
        shotPeriod = atol(getenv("GKE_SHOT_PERIOD"));
    *(void **)&realSwapBuffers = hookExecutableImport("glXSwapBuffers", (void *)gameSwapBuffers);
}
