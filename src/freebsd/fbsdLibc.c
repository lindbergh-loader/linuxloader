// FreeBSD 9.1 (i386) libc functions standing in for the host's where the
// ABIs differ (see fbsdLibc.h). The rest of what the game imports (strings,
// memory, stdio through FILE pointers, math, time, exit) is the same.
//
// The game's calls come with FreeBSD's 4-byte stack alignment: the shims
// realign theirs.

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "fbsdLibc.h"
#include "fbsdCoastal.h"

#define SHIM __attribute__((force_align_arg_pointer))

// --- errno ---

SHIM static int *fbsdError(void)
{
    return &errno;
}

SHIM static void fbsdInitTls(void)
{
}

SHIM static void fbsdAssert(const char *function, const char *file, int line, const char *expression)
{
    fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n", expression, function, file, line);
    abort();
}

// --- files ---

#define FBSD_O_ACCMODE 0x0003
#define FBSD_O_NONBLOCK 0x0004
#define FBSD_O_APPEND 0x0008
#define FBSD_O_NOFOLLOW 0x0100
#define FBSD_O_CREAT 0x0200
#define FBSD_O_TRUNC 0x0400
#define FBSD_O_EXCL 0x0800
#define FBSD_O_NOCTTY 0x8000
#define FBSD_O_DIRECTORY 0x20000

static int openFlags(int f)
{
    int flags = f & FBSD_O_ACCMODE;

    if (f & FBSD_O_NONBLOCK)
        flags |= O_NONBLOCK;
    if (f & FBSD_O_APPEND)
        flags |= O_APPEND;
    if (f & FBSD_O_NOFOLLOW)
        flags |= O_NOFOLLOW;
    if (f & FBSD_O_CREAT)
        flags |= O_CREAT;
    if (f & FBSD_O_TRUNC)
        flags |= O_TRUNC;
    if (f & FBSD_O_EXCL)
        flags |= O_EXCL;
    if (f & FBSD_O_NOCTTY)
        flags |= O_NOCTTY;
    if (f & FBSD_O_DIRECTORY)
        flags |= O_DIRECTORY;
    return flags;
}

// The cabinet's disk, its serial read for the settings' key: a device of
// our own (any descriptor will do).
static int diskFd = -1;

SHIM static int fbsdOpen(const char *path, int flags, ...)
{
    va_list ap;
    int mode;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    int fd;

    if (path && !strncmp(path, "/dev/da", 7))
    {
        diskFd = open("/dev/null", O_RDONLY);
        return diskFd;
    }
    if ((fd = fbsdCoastalOpen(path)) != -2)
        return fd;
    // (the second serial port: none)
    if (path && !strncmp(path, "/dev/cuau", 9))
    {
        errno = ENOENT;
        return -1;
    }
    return open(path, openFlags(flags), mode);
}

// FreeBSD 9's struct stat (i386: 96 bytes).
struct fbsdStat
{
    uint32_t st_dev, st_ino;
    uint16_t st_mode, st_nlink;
    uint32_t st_uid, st_gid, st_rdev;
    int32_t atimeSec, atimeNsec, mtimeSec, mtimeNsec, ctimeSec, ctimeNsec;
    int64_t st_size, st_blocks;
    uint32_t st_blksize, st_flags, st_gen;
    int32_t st_lspare;
    int32_t birthtimeSec, birthtimeNsec;
    uint32_t pad[2];
};

SHIM static int fbsdStatCall(const char *path, struct fbsdStat *out)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return -1;
    memset(out, 0, sizeof(*out));
    out->st_dev = (uint32_t)st.st_dev;
    out->st_ino = (uint32_t)st.st_ino;
    out->st_mode = (uint16_t)st.st_mode;
    out->st_nlink = (uint16_t)st.st_nlink;
    out->st_uid = st.st_uid;
    out->st_gid = st.st_gid;
    out->st_rdev = (uint32_t)st.st_rdev;
    out->atimeSec = (int32_t)st.st_atim.tv_sec;
    out->mtimeSec = (int32_t)st.st_mtim.tv_sec;
    out->ctimeSec = (int32_t)st.st_ctim.tv_sec;
    out->birthtimeSec = out->ctimeSec;
    out->st_size = st.st_size;
    out->st_blocks = st.st_blocks;
    out->st_blksize = (uint32_t)st.st_blksize;
    return 0;
}

// ATA's IDENTIFY (FreeBSD's IOCATAGPARMS, struct ata_params: 256 bytes, as
// the request says): the disk's serial at word 10.
#define FBSD_IOCATAGPARMS 0x41006489u
#define ATA_PARAMS_SIZE 0x100

void fbsdDiskIdentity(uint8_t *params)
{
    static const char serial[] = "0000COASTALTEMPLERUN"; // (20 characters)

    memset(params, 0, ATA_PARAMS_SIZE);
    // ATA strings: byte-swapped words
    for (int i = 0; i < 20; i += 2)
    {
        params[20 + i] = (uint8_t)serial[i + 1];
        params[20 + i + 1] = (uint8_t)serial[i];
    }
}

SHIM static ssize_t fbsdRead(int fd, void *buf, size_t count)
{
    if (fbsdCoastalIsFd(fd))
        return fbsdCoastalRead(buf, count);
    return read(fd, buf, count);
}

SHIM static ssize_t fbsdWrite(int fd, const void *buf, size_t count)
{
    if (fbsdCoastalIsFd(fd))
        return fbsdCoastalWrite(buf, count);
    return write(fd, buf, count);
}

SHIM static int fbsdClose(int fd)
{
    if (fbsdCoastalIsFd(fd))
        fbsdCoastalClose();
    return close(fd);
}

// glibc's atexit is libc_nonshared's: the C++ runtime's.
SHIM static int fbsdAtexit(void (*function)(void))
{
    extern int __cxa_atexit(void (*)(void *), void *, void *);
    return __cxa_atexit((void (*)(void *))function, NULL, NULL);
}

SHIM static int fbsdIoctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (fd >= 0 && fd == diskFd && request == FBSD_IOCATAGPARMS && arg)
    {
        fbsdDiskIdentity(arg);
        return 0;
    }
    if (fbsdCoastalIsFd(fd))
        return 0;
    return ioctl(fd, request, arg);
}

// The game's USB stick (its updates): none.
SHIM static int fbsdMount(const char *type, const char *dir, int flags, void *data)
{
    (void)type;
    (void)dir;
    (void)flags;
    (void)data;
    errno = ENODEV;
    return -1;
}

SHIM static int fbsdUnmount(const char *dir, int flags)
{
    (void)dir;
    (void)flags;
    return 0;
}

// --- termios: FreeBSD's struct (44 bytes); the serial ports are the I/O
// board's, the loader's to stand in for: set-up accepted ---

SHIM static void fbsdCfmakeraw(void *t)
{
    (void)t;
}

SHIM static int fbsdCfsetspeed(void *t, unsigned int speed)
{
    (void)t;
    (void)speed;
    return 0;
}

SHIM static int fbsdTcgetattr(int fd, void *t)
{
    (void)fd;
    memset(t, 0, 44);
    return 0;
}

SHIM static int fbsdTcsetattr(int fd, int action, const void *t)
{
    (void)fd;
    (void)action;
    (void)t;
    return 0;
}

SHIM static int fbsdTcflush(int fd, int queue)
{
    (void)fd;
    (void)queue;
    return 0;
}

SHIM static int fbsdTcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    return 0;
}

// --- pthread: FreeBSD's mutex is a pointer (static ones NULL, or 1 for
// the adaptive initialiser), its once a {state, mutex} pair ---

static pthread_mutex_t creating = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *mutexOf(void **m)
{
    if ((uintptr_t)*m <= 1)
    {
        pthread_mutex_lock(&creating);
        if ((uintptr_t)*m <= 1)
        {
            pthread_mutex_t *mutex = malloc(sizeof(*mutex));
            pthread_mutex_init(mutex, NULL);
            *m = mutex;
        }
        pthread_mutex_unlock(&creating);
    }
    return *m;
}

SHIM static int fbsdMutexLock(void **m)
{
    return pthread_mutex_lock(mutexOf(m));
}

SHIM static int fbsdMutexUnlock(void **m)
{
    return pthread_mutex_unlock(mutexOf(m));
}

struct fbsdOnce
{
    int state;
    void *mutex;
};

SHIM static int fbsdOnce(struct fbsdOnce *once, void (*routine)(void))
{
    void **m = &once->mutex;

    pthread_mutex_lock(mutexOf(m));
    if (once->state == 0)
    {
        once->state = 1;
        routine();
        once->state = 2;
    }
    pthread_mutex_unlock(mutexOf(m));
    return 0;
}

// --- libpng: the game's is 1.5, the host's 1.6 (the same API for what the
// game uses), whose read struct refuses another version's string ---

SHIM static void *fbsdSetVideoMode(int width, int height, int bpp, uint32_t flags)
{
    static void *(*setVideoMode)(int, int, int, uint32_t);

    if (!setVideoMode)
        *(void **)&setVideoMode = dlsym(RTLD_DEFAULT, "SDL_SetVideoMode");
    fprintf(stderr, "freebsd: video mode %dx%d %d 0x%x\n", width, height, bpp, flags);
    return setVideoMode(width, height, bpp, flags);
}

SHIM static void *fbsdPngCreateReadStruct(const char *version, void *error, void *errorFn, void *warningFn)
{
    static void *(*create)(const char *, void *, void *, void *);
    static const char *(*libpngVersion)(void *);

    (void)version;
    if (!create)
    {
        *(void **)&create = dlsym(RTLD_DEFAULT, "png_create_read_struct");
        *(void **)&libpngVersion = dlsym(RTLD_DEFAULT, "png_get_libpng_ver");
    }
    if (!create || !libpngVersion)
        return NULL;
    return create(libpngVersion(NULL), error, errorFn, warningFn);
}

// FBSD_ROTATE=ccw (or cw): the frame turned a quarter turn at each swap,
// fitted in the window with its shape kept: games drawing for a monitor on
// its side (Temple Run's portrait cabinet) upright on a landscape screen.
#define GLF(name, ...) static void (*name)(__VA_ARGS__)
static void rotateFrame(int clockwise)
{
    GLF(genTextures, int, unsigned int *);
    GLF(bindTexture, unsigned int, unsigned int);
    GLF(texParameteri, unsigned int, unsigned int, int);
    GLF(copyTexImage2D, unsigned int, int, unsigned int, int, int, int, int, int);
    GLF(getIntegerv, unsigned int, int *);
    GLF(viewport, int, int, int, int);
    GLF(clearColor, float, float, float, float);
    GLF(clear, unsigned int);
    GLF(pushAttrib, unsigned int);
    GLF(popAttrib, void);
    GLF(matrixMode, unsigned int);
    GLF(pushMatrix, void);
    GLF(popMatrix, void);
    GLF(loadIdentity, void);
    GLF(enable, unsigned int);
    GLF(disable, unsigned int);
    GLF(begin, unsigned int);
    GLF(end, void);
    GLF(texCoord2f, float, float);
    GLF(vertex2f, float, float);
    GLF(color4f, float, float, float, float);
    GLF(texEnvi, unsigned int, unsigned int, int);
    GLF(activeTexture, unsigned int);
    GLF(useProgram, unsigned int);
    static unsigned int texture;
    static int loaded;
    int vp[4], bound = 0, active = 0x84c0, program = 0;

    if (!loaded)
    {
        // (GL past 1.2 through glXGetProcAddress: libglvnd exports little)
        void *(*getProcAddress)(const char *) = (void *(*)(const char *))dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
#define LOAD(var, name)                                                                                               \
    do                                                                                                                \
    {                                                                                                                 \
        *(void **)&var = dlsym(RTLD_DEFAULT, name);                                                                   \
        if (!var && getProcAddress)                                                                                   \
            *(void **)&var = getProcAddress(name);                                                                    \
    } while (0)
        LOAD(genTextures, "glGenTextures");
        LOAD(bindTexture, "glBindTexture");
        LOAD(texParameteri, "glTexParameteri");
        LOAD(copyTexImage2D, "glCopyTexImage2D");
        LOAD(getIntegerv, "glGetIntegerv");
        LOAD(viewport, "glViewport");
        LOAD(clearColor, "glClearColor");
        LOAD(clear, "glClear");
        LOAD(pushAttrib, "glPushAttrib");
        LOAD(popAttrib, "glPopAttrib");
        LOAD(matrixMode, "glMatrixMode");
        LOAD(pushMatrix, "glPushMatrix");
        LOAD(popMatrix, "glPopMatrix");
        LOAD(loadIdentity, "glLoadIdentity");
        LOAD(enable, "glEnable");
        LOAD(disable, "glDisable");
        LOAD(begin, "glBegin");
        LOAD(end, "glEnd");
        LOAD(texCoord2f, "glTexCoord2f");
        LOAD(vertex2f, "glVertex2f");
        LOAD(color4f, "glColor4f");
        LOAD(texEnvi, "glTexEnvi");
        LOAD(activeTexture, "glActiveTexture");
        LOAD(useProgram, "glUseProgram");
#undef LOAD
        loaded = genTextures && copyTexImage2D && begin && activeTexture ? 1 : -1;
        if (loaded < 0)
            fprintf(stderr, "freebsd: no GL entry points to turn the frame with\n");
        if (loaded > 0)
            genTextures(1, &texture);
    }
    if (loaded < 0)
        return;
    getIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &bound);
    getIntegerv(0x84E0 /* GL_ACTIVE_TEXTURE */, &active);
    getIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &program);
    // (turned in the framebuffer bound: sdl12-compat's own, which its swap
    // shows, scaled to the window)
    pushAttrib(0x000fffff /* GL_ALL_ATTRIB_BITS */);
    if (useProgram)
        useProgram(0);
    activeTexture(0x84c1);
    disable(0x0DE1);
    activeTexture(0x84c0);
    {
        // the game's frame: its viewport (1366x768, in the corner of a
        // larger full screen window), turned and fitted in the window
        static void *(*getVideoSurface)(void);
        int w, h, ww, wh, dw, dh;
        getIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
        w = vp[2];
        h = vp[3];
        if (w <= 0 || h <= 0)
            goto done;
        ww = vp[0] + w;
        wh = vp[1] + h;
        if (!getVideoSurface)
            *(void **)&getVideoSurface = dlsym(RTLD_DEFAULT, "SDL_GetVideoSurface");
        if (getVideoSurface && getVideoSurface())
        {
            // SDL 1.2's SDL_Surface: flags, format, w, h
            const int *surface = getVideoSurface();
            ww = surface[2];
            wh = surface[3];
        }
        bindTexture(0x0DE1, texture);
        copyTexImage2D(0x0DE1, 0, 0x8058 /* GL_RGBA8 */, vp[0], vp[1], w, h, 0);
        texParameteri(0x0DE1, 0x2801, 0x2601); // linear
        texParameteri(0x0DE1, 0x2800, 0x2601);
        // the turned picture (h x w) fitted in the window (ww x wh)
        if ((double)h * wh > (double)w * ww)
        {
            dw = ww;
            dh = (int)((double)ww * w / h);
        }
        else
        {
            dh = wh;
            dw = (int)((double)wh * h / w);
        }
        viewport(0, 0, ww, wh);
        disable(0x0C11 /* GL_SCISSOR_TEST */);
        clearColor(0, 0, 0, 1);
        clear(0x4000 /* GL_COLOR_BUFFER_BIT */);
        viewport((ww - dw) / 2, (wh - dh) / 2, dw, dh);
        disable(0x0B71 /* depth */);
        disable(0x0BE2 /* blend */);
        disable(0x0BC0 /* alpha test */);
        disable(0x0B44 /* cull */);
        disable(0x0B50 /* lighting */);
        disable(0x0B60 /* fog */);
        enable(0x0DE1);
        texEnvi(0x2300, 0x2200, 0x1E01 /* GL_REPLACE */);
        color4f(1, 1, 1, 1);
        matrixMode(0x1702 /* texture */);
        pushMatrix();
        loadIdentity();
        matrixMode(0x1701 /* projection */);
        pushMatrix();
        loadIdentity();
        matrixMode(0x1700 /* modelview */);
        pushMatrix();
        loadIdentity();
        // screen corners (bottom left, counterclockwise) and the frame's
        // point shown there: anticlockwise, (x, y) shows (y, 1 - x)
        static const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        static const float ccw[4][2] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
        static const float cw[4][2] = {{1, 0}, {1, 1}, {0, 1}, {0, 0}};
        begin(0x0007 /* GL_QUADS */);
        for (int i = 0; i < 4; i++)
        {
            const float *t = clockwise ? cw[i] : ccw[i];
            texCoord2f(t[0], t[1]);
            vertex2f(corner[i][0], corner[i][1]);
        }
        end();
        matrixMode(0x1700);
        popMatrix();
        matrixMode(0x1701);
        popMatrix();
        matrixMode(0x1702);
        popMatrix();
    }
done:
    popAttrib();
    bindTexture(0x0DE1, (unsigned int)bound);
    activeTexture((unsigned int)active);
    if (useProgram)
        useProgram((unsigned int)program);
}

// FBSD_SHOT=<directory> (debugging): a frame every FBSD_SHOT_PERIOD seconds
// (4) saved there as a PPM, read back before the swap.
static void fbsdShot(void)
{
    static void (*swap)(void);
    static void (*readPixels)(int, int, int, int, unsigned int, unsigned int, void *);
    static void (*getIntegerv)(unsigned int, int *);
    static void (*pixelStorei)(unsigned int, int);
    static double next;
    static int count;
    const char *dir = getenv("FBSD_SHOT");
    struct timespec ts;
    double now;

    if (!swap)
    {
        *(void **)&swap = dlsym(RTLD_DEFAULT, "SDL_GL_SwapBuffers");
        *(void **)&readPixels = dlsym(RTLD_DEFAULT, "glReadPixels");
        *(void **)&getIntegerv = dlsym(RTLD_DEFAULT, "glGetIntegerv");
        *(void **)&pixelStorei = dlsym(RTLD_DEFAULT, "glPixelStorei");
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (dir && readPixels && getIntegerv && now >= next)
    {
        int viewport[4];
        char path[4096];
        unsigned char *pixels;
        FILE *f;

        next = now + (getenv("FBSD_SHOT_PERIOD") ? atof(getenv("FBSD_SHOT_PERIOD")) : 4);
        getIntegerv(0x0BA2 /* GL_VIEWPORT */, viewport);
        if (getenv("FBSD_ROTATE") && *getenv("FBSD_ROTATE"))
        {
            // the turned frame fills the window
            void *(*getVideoSurface)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT, "SDL_GetVideoSurface");
            const int *surface = getVideoSurface ? getVideoSurface() : NULL;
            if (surface)
            {
                viewport[0] = viewport[1] = 0;
                viewport[2] = surface[2];
                viewport[3] = surface[3];
            }
        }
        snprintf(path, sizeof(path), "%s/shot_%04d.ppm", dir, count);
        fprintf(stderr, "freebsd: shot %d\n", count++);
        pixels = malloc((size_t)viewport[2] * viewport[3] * 3);
        if (pixels && (f = fopen(path, "wb")))
        {
            int alignment = 4;
            getIntegerv(0x0D05 /* GL_PACK_ALIGNMENT */, &alignment);
            pixelStorei(0x0D05, 1);
            readPixels(viewport[0], viewport[1], viewport[2], viewport[3], 0x1907 /* GL_RGB */,
                       0x1401 /* GL_UNSIGNED_BYTE */, pixels);
            pixelStorei(0x0D05, alignment);
            fprintf(f, "P6\n%d %d\n255\n", viewport[2], viewport[3]);
            for (int y = viewport[3]; y-- > 0;)
                fwrite(pixels + (size_t)y * viewport[2] * 3, 1, (size_t)viewport[2] * 3, f);
            fclose(f);
        }
        free(pixels);
    }
    if (swap)
        swap();
}

// The swap: the frame turned first (FBSD_ROTATE), then saved (FBSD_SHOT).
static void fbsdShot(void);

SHIM static void fbsdSwap(void)
{
    static void (*pumpEvents)(void);
    static uint8_t *(*getKeyState)(int *);
    const char *rotate = getenv("FBSD_ROTATE");

    // Esc quits (the cabinet has no way out)
    if (!pumpEvents)
    {
        *(void **)&pumpEvents = dlsym(RTLD_DEFAULT, "SDL_PumpEvents");
        *(void **)&getKeyState = dlsym(RTLD_DEFAULT, "SDL_GetKeyState");
    }
    if (pumpEvents && getKeyState)
    {
        pumpEvents();
        if (getKeyState(NULL)[27 /* SDLK_ESCAPE */])
            _exit(0);
    }

    if (rotate && (!strcmp(rotate, "ccw") || !strcmp(rotate, "cw")))
        rotateFrame(!strcmp(rotate, "cw"));
    fbsdShot();
}

void *fbsdShim(const char *name)
{
    static const struct
    {
        const char *name;
        void *shim;
    } shims[] = {
        {"__error", (void *)fbsdError},
        {"_init_tls", (void *)fbsdInitTls},
        {"__assert", (void *)fbsdAssert},
        {"open", (void *)fbsdOpen},
        {"read", (void *)fbsdRead},
        {"write", (void *)fbsdWrite},
        {"close", (void *)fbsdClose},
        {"atexit", (void *)fbsdAtexit},
        {"png_create_read_struct", (void *)fbsdPngCreateReadStruct},
        {"SDL_GL_SwapBuffers", (void *)fbsdSwap},
        {"SDL_SetVideoMode", (void *)fbsdSetVideoMode},
        {"stat", (void *)fbsdStatCall},
        {"ioctl", (void *)fbsdIoctl},
        {"mount", (void *)fbsdMount},
        {"unmount", (void *)fbsdUnmount},
        {"cfmakeraw", (void *)fbsdCfmakeraw},
        {"cfsetspeed", (void *)fbsdCfsetspeed},
        {"tcgetattr", (void *)fbsdTcgetattr},
        {"tcsetattr", (void *)fbsdTcsetattr},
        {"tcflush", (void *)fbsdTcflush},
        {"tcflow", (void *)fbsdTcflow},
        {"pthread_mutex_lock", (void *)fbsdMutexLock},
        {"pthread_mutex_unlock", (void *)fbsdMutexUnlock},
        {"pthread_once", (void *)fbsdOnce},
    };

    for (size_t i = 0; i < sizeof(shims) / sizeof(shims[0]); i++)
        if (!strcmp(name, shims[i].name))
            return shims[i].shim;
    return NULL;
}
