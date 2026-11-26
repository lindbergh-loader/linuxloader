// Marvel Contest of Champions' cameras (libWebCamRT.so, see
// docs/marvel-contest-of-champions.md), stood in for over V4L2: the players
// show their hero cards to webcams.
//
// The cabinet's library only captures: it hands the game greyscale frames
// (GrabFrame), and the game decodes the cards' QR codes itself (ZXing, in
// CameraManager.ScanThread) and looks them up in its own table of the
// cards' codes. Here, the colour webcams when there are (V4L2 capture
// devices offering YUYV, UYVY, NV12 or YU12: the luma is the greyscale; an
// IR face-login camera, GREY only, is left out), P1's first. With one
// webcam, P2's is a blank camera: the game sees both scanners connected
// (a webcam that cannot be opened, busy in another program, is one too).
// With none, the game is told so (Config.NoWebCam, mcocPlugins.c). The
// players pick with the stick in any case (mcocCards). For tests,
// MCOC_TEST_CAMERA names a still picture (a binary PGM) as P1's camera.
//
// The library's answers: 0 is success, or yes (IsFrameReady() == 0: a
// frame is there), and IsDisconnected() is 0 when the camera is gone.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "mcoc.h"

enum
{
    WEBCAM_OK = 0,
    WEBCAM_FAIL = -1,
    WEBCAM_MAX = 4,         // cameras open at once
    WEBCAM_BUFFERS = 4,     // V4L2 buffers
    WEBCAM_SFORMAT_YUYV = 4, // WebCamRT.FormatSource
    WEBCAM_DFORMAT_GREY = 1, // GetProp_DestFormat, with OpenConfig_SetGreyscale: 1 byte a pixel
};

#define WEBCAM_WIDTH 640
#define WEBCAM_HEIGHT 480
#define WEBCAM_FPS 30
#define WEBCAM_BLANK "blank"

typedef struct
{
    int used;
    char path[256], name[64];
    int fd;           // -1: a blank camera or a still picture
    uint32_t format;  // V4L2 pixel format
    int width, height, stride;
    void *map[WEBCAM_BUFFERS];
    size_t mapLength[WEBCAM_BUFFERS];
    unsigned buffers;
    pthread_t thread;
    int threadRunning, capturing, gone;
    pthread_mutex_t lock;
    pthread_cond_t newFrame;
    uint8_t *frame;    // the latest, greyscale, width x height
    uint8_t *still;    // a still picture's, shown 2 s out of 4 (a card shown, then taken away)
    unsigned frames;   // frames received since the capture began
    unsigned scanned;  // the frame count the game's scan thread last saw
    float fps;
} Webcam;

static Webcam cams[WEBCAM_MAX + 1]; // by id, from 1
static pid_t mainThread;

// The cameras the game is offered, P1's first.
static struct
{
    int scanned, count;
    char paths[2][256];
    int reported;
} devices;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    while ((r = ioctl(fd, request, arg)) < 0 && errno == EINTR)
        ;
    return r;
}

// The format a device captures in, a colour one first (an IR camera, a
// laptop's face login, only offers GREY): 0 if none.
static uint32_t deviceFormat(int fd, int colourOnly)
{
    static const uint32_t preferred[] = {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_UYVY, V4L2_PIX_FMT_NV12,
                                         V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_GREY};
    uint32_t found[16];
    unsigned n = 0;
    struct v4l2_fmtdesc d;
    memset(&d, 0, sizeof(d));
    d.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (d.index = 0; n < 16 && xioctl(fd, VIDIOC_ENUM_FMT, &d) == 0; d.index++)
        found[n++] = d.pixelformat;
    for (unsigned p = 0; p < sizeof(preferred) / sizeof(preferred[0]); p++)
    {
        if (colourOnly && preferred[p] == V4L2_PIX_FMT_GREY)
            break;
        for (unsigned i = 0; i < n; i++)
            if (found[i] == preferred[p])
                return found[i];
    }
    return 0;
}

static int isStill(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && !strcmp(path + n - 4, ".pgm");
}

static void scanDevices(void)
{
    const char *test = getenv("MCOC_TEST_CAMERA");
    int n = 0;
    if (devices.scanned)
        return;
    devices.scanned = 1;
    if (test && *test)
        snprintf(devices.paths[n++], sizeof(devices.paths[0]), "%s", test);
    // The colour webcams, in order.
    for (int i = 0; i < 64 && n < 2; i++)
    {
        char path[32];
        struct v4l2_capability cap;
        int fd;
        snprintf(path, sizeof(path), "/dev/video%d", i);
        if ((fd = open(path, O_RDWR | O_NONBLOCK)) < 0)
            continue;
        memset(&cap, 0, sizeof(cap));
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
        {
            uint32_t caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
            if ((caps & V4L2_CAP_VIDEO_CAPTURE) && (caps & V4L2_CAP_STREAMING) && deviceFormat(fd, 1))
                snprintf(devices.paths[n++], sizeof(devices.paths[0]), "%s", path);
        }
        close(fd);
    }
    if (!n)
    {
        unityLog("cameras: no webcam\n");
        return;
    }
    for (int p = 0; p < 2; p++)
    {
        if (!*devices.paths[p])
            snprintf(devices.paths[p], sizeof(devices.paths[p]), "%s", WEBCAM_BLANK);
        unityLog("camera P%d: %s\n", p + 1, devices.paths[p]);
    }
    devices.count = 2;
}

int mcocWebcamCount(void)
{
    scanDevices();
    return devices.count;
}

// Which camera is whose: the test menu keeps their devices (preferences 90
// and 91, P1's and P2's), and without both the game asks the operator to
// check them. Kept when they name these cameras (the menu may swap them),
// else these, P1's first. From the main thread.
#define MCOC_PREF_CAMERA_P1 90

void mcocWebcamAssign(void)
{
    char saved[2][256];
    int keep = devices.count == 2;
    for (int p = 0; p < 2 && keep; p++)
        keep = unityMonoPrefString(MCOC_PREF_CAMERA_P1 + p, saved[p], sizeof(saved[p])) && *saved[p] &&
               (!strcmp(saved[p], devices.paths[0]) || !strcmp(saved[p], devices.paths[1]));
    if (devices.count != 2 || (keep && strcmp(saved[0], saved[1])))
        return;
    for (int p = 0; p < 2; p++)
        unityMonoSetPrefString(MCOC_PREF_CAMERA_P1 + p, devices.paths[p]);
    unityLog("cameras: P1 %s, P2 %s (the test menu's scanner assignment)\n", devices.paths[0], devices.paths[1]);
}

static Webcam *cam(int id)
{
    return id > 0 && id <= WEBCAM_MAX && cams[id].used ? &cams[id] : NULL;
}

// A captured image, its luma into the camera's frame (the sizes match:
// asked at the device's open).
static void storeFrame(Webcam *c, const uint8_t *src, size_t length)
{
    int w = c->width, h = c->height;
    pthread_mutex_lock(&c->lock);
    if (c->format == V4L2_PIX_FMT_YUYV || c->format == V4L2_PIX_FMT_UYVY)
    {
        int o = c->format == V4L2_PIX_FMT_UYVY;
        for (int y = 0; y < h && (size_t)(y + 1) * c->stride <= length; y++)
        {
            const uint8_t *s = src + (size_t)y * c->stride + o;
            uint8_t *d = c->frame + (size_t)y * w;
            for (int x = 0; x < w; x++)
                d[x] = s[x * 2];
        }
    }
    else // the luma plane first
        for (int y = 0; y < h && (size_t)(y + 1) * c->stride <= length; y++)
            memcpy(c->frame + (size_t)y * w, src + (size_t)y * c->stride, w);
    c->frames++;
    pthread_cond_broadcast(&c->newFrame);
    pthread_mutex_unlock(&c->lock);
}

static void *captureThread(void *arg)
{
    Webcam *c = arg;
    double fpsStart = now();
    unsigned fpsFrames = 0;
    while (c->capturing)
    {
        struct pollfd p = {c->fd, POLLIN, 0};
        struct v4l2_buffer b;
        int r = poll(&p, 1, 200);
        if (r < 0 && errno != EINTR)
            break;
        if (r <= 0)
            continue;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            c->gone = 1;
            break;
        }
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(c->fd, VIDIOC_DQBUF, &b) < 0)
        {
            if (errno == EAGAIN)
                continue;
            c->gone = errno == ENODEV;
            break;
        }
        if (b.index < c->buffers && !(b.flags & V4L2_BUF_FLAG_ERROR))
        {
            storeFrame(c, c->map[b.index], b.bytesused);
            if (c->frames == 1)
                unityLog("camera %s: first frame\n", c->path);
        }
        xioctl(c->fd, VIDIOC_QBUF, &b);
        fpsFrames++;
        double t = now();
        if (t - fpsStart >= 1.0)
        {
            c->fps = fpsFrames / (float)(t - fpsStart);
            fpsStart = t;
            fpsFrames = 0;
        }
    }
    if (c->gone)
        unityLog("camera %s: disconnected\n", c->path);
    return NULL;
}

static void unmapBuffers(Webcam *c)
{
    struct v4l2_requestbuffers req;
    for (unsigned i = 0; i < c->buffers; i++)
        if (c->map[i] && c->map[i] != MAP_FAILED)
            munmap(c->map[i], c->mapLength[i]);
    c->buffers = 0;
    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (c->fd >= 0)
        xioctl(c->fd, VIDIOC_REQBUFS, &req);
}

static int startCapture(Webcam *c)
{
    struct v4l2_requestbuffers req;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (c->capturing)
        return WEBCAM_OK;
    c->frames = c->scanned = 0;
    if (c->fd < 0) // blank or still: nothing to stream
    {
        c->capturing = 1;
        return WEBCAM_OK;
    }
    memset(&req, 0, sizeof(req));
    req.count = WEBCAM_BUFFERS;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(c->fd, VIDIOC_REQBUFS, &req) < 0 || !req.count)
    {
        unityLog("camera %s: no buffers (%s)\n", c->path, strerror(errno));
        return WEBCAM_FAIL;
    }
    c->buffers = req.count < WEBCAM_BUFFERS ? req.count : WEBCAM_BUFFERS;
    for (unsigned i = 0; i < c->buffers; i++)
    {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = type;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        c->map[i] = NULL;
        if (xioctl(c->fd, VIDIOC_QUERYBUF, &b) < 0 ||
            (c->map[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, b.m.offset)) == MAP_FAILED ||
            xioctl(c->fd, VIDIOC_QBUF, &b) < 0)
        {
            unityLog("camera %s: buffer %u (%s)\n", c->path, i, strerror(errno));
            unmapBuffers(c);
            return WEBCAM_FAIL;
        }
        c->mapLength[i] = b.length;
    }
    if (xioctl(c->fd, VIDIOC_STREAMON, &type) < 0)
    {
        unityLog("camera %s: no stream (%s)\n", c->path, strerror(errno));
        unmapBuffers(c);
        return WEBCAM_FAIL;
    }
    c->capturing = 1;
    if (pthread_create(&c->thread, NULL, captureThread, c))
    {
        c->capturing = 0;
        xioctl(c->fd, VIDIOC_STREAMOFF, &type);
        unmapBuffers(c);
        return WEBCAM_FAIL;
    }
    c->threadRunning = 1;
    unityLog("camera %s: capturing %dx%d\n", c->path, c->width, c->height);
    return WEBCAM_OK;
}

static void stopCapture(Webcam *c)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!c->capturing)
        return;
    c->capturing = 0;
    if (c->threadRunning)
    {
        pthread_join(c->thread, NULL);
        c->threadRunning = 0;
    }
    if (c->fd >= 0)
    {
        xioctl(c->fd, VIDIOC_STREAMOFF, &type);
        unmapBuffers(c);
    }
    pthread_mutex_lock(&c->lock);
    pthread_cond_broadcast(&c->newFrame);
    pthread_mutex_unlock(&c->lock);
}

// A still picture (a binary PGM) as a camera's frame, scaled to its size.
static int loadStill(Webcam *c)
{
    FILE *f = fopen(c->path, "rb");
    int w, h, max, ok = 0;
    free(c->still);
    if (!(c->still = malloc((size_t)c->width * c->height)))
        ok = 0;
    else if (f && fscanf(f, "P5 %d %d %d", &w, &h, &max) == 3 && fgetc(f) != EOF && w > 0 && h > 0 && max == 255)
    {
        uint8_t *img = malloc((size_t)w * h);
        if (img && fread(img, 1, (size_t)w * h, f) == (size_t)w * h)
        {
            for (int y = 0; y < c->height; y++)
                for (int x = 0; x < c->width; x++)
                    c->still[(size_t)y * c->width + x] = img[(size_t)(y * h / c->height) * w + x * w / c->width];
            ok = 1;
        }
        free(img);
    }
    if (f)
        fclose(f);
    if (!ok)
        unityLog("camera %s: not a binary PGM\n", c->path);
    return ok;
}

static void closeCam(Webcam *c)
{
    stopCapture(c);
    if (c->fd >= 0)
        close(c->fd);
    free(c->frame);
    free(c->still);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->newFrame);
    memset(c, 0, sizeof(*c));
}

// ---------------------------------------------------------------------------
// The library.

int WebCamRT_SetLogCallback(void *cb)
{
    (void)cb; // Mono's: not called from the capture threads
    return WEBCAM_OK;
}

int WebCamRT_Create(void)
{
    mainThread = (pid_t)syscall(SYS_gettid);
    scanDevices();
    return WEBCAM_OK;
}

int WebCamRT_Destroy(void)
{
    for (int id = 1; id <= WEBCAM_MAX; id++)
        if (cams[id].used)
            closeCam(&cams[id]);
    return WEBCAM_OK;
}

// The devices are offered once, at the first update.
int WebCamRT_Enum_UpdateDevices(void) { return WEBCAM_OK; }

int WebCamRT_Enum_NewDeviceCount(void)
{
    scanDevices();
    if (devices.reported)
        return 0;
    devices.reported = 1;
    return devices.count;
}

int WebCamRT_Enum_NewDevicePath(int index, char *out, int max)
{
    if (!out || max <= 0)
        return WEBCAM_FAIL;
    snprintf(out, max, "%s", index >= 0 && index < devices.count ? devices.paths[index] : "");
    return index >= 0 && index < devices.count ? WEBCAM_OK : WEBCAM_FAIL;
}

int WebCamRT_CameraNew(const char *path)
{
    int id;
    for (id = 1; id <= WEBCAM_MAX && cams[id].used; id++)
        ;
    if (!path || id > WEBCAM_MAX)
        return WEBCAM_FAIL;
    Webcam *c = &cams[id];
    memset(c, 0, sizeof(*c));
    c->used = 1;
    c->fd = -1;
    snprintf(c->path, sizeof(c->path), "%s", path);
    snprintf(c->name, sizeof(c->name), "%s", path);
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->newFrame, NULL);
    c->width = WEBCAM_WIDTH;
    c->height = WEBCAM_HEIGHT;
    if (strcmp(path, WEBCAM_BLANK) && !isStill(path))
    {
        struct v4l2_capability cap;
        if ((c->fd = open(path, O_RDWR | O_NONBLOCK)) < 0)
            unityLog("camera %s: %s: a blank camera\n", path, strerror(errno));
        else
        {
            memset(&cap, 0, sizeof(cap));
            if (xioctl(c->fd, VIDIOC_QUERYCAP, &cap) == 0)
                snprintf(c->name, sizeof(c->name), "%s", (const char *)cap.card);
            if (!(c->format = deviceFormat(c->fd, 0)))
            {
                unityLog("camera %s: no YUYV, UYVY, NV12, YU12 or GREY capture: a blank camera\n", path);
                close(c->fd);
                c->fd = -1;
            }
        }
    }
    return id;
}

int WebCamRT_CameraDelete(int id)
{
    Webcam *c = cam(id);
    if (c)
        closeCam(c);
    return WEBCAM_OK;
}

int WebCamRT_DeepProbeDevice(int id) { (void)id; return WEBCAM_FAIL; }
int WebCamRT_DeepProbeDeviceResults(int id, char *out, int *size)
{
    (void)id;
    if (out && size && *size > 0)
        out[0] = 0;
    if (size)
        *size = 0;
    return WEBCAM_FAIL;
}

// One configuration: 640x480 at 30 fps, what the device gives near it.
int WebCamRT_OpenConfig_Probe(int id) { return cam(id) ? WEBCAM_OK : WEBCAM_FAIL; }
int WebCamRT_OpenConfig_GetCount(int id) { return cam(id) ? 1 : 0; }
int WebCamRT_OpenConfig_Get(int id, int index, int *format, int *width, int *height, int *fps)
{
    Webcam *c = cam(id);
    if (!c || index)
        return WEBCAM_FAIL;
    *format = WEBCAM_SFORMAT_YUYV;
    *width = c->width;
    *height = c->height;
    *fps = WEBCAM_FPS;
    return WEBCAM_OK;
}
int WebCamRT_OpenConfig_SetWidth(int id, int v)
{
    Webcam *c = cam(id);
    if (c && v > 0)
        c->width = v;
    return c ? WEBCAM_OK : WEBCAM_FAIL;
}
int WebCamRT_OpenConfig_SetHeight(int id, int v)
{
    Webcam *c = cam(id);
    if (c && v > 0)
        c->height = v;
    return c ? WEBCAM_OK : WEBCAM_FAIL;
}
int WebCamRT_OpenConfig_SetFPS(int id, int v) { (void)v; return cam(id) ? WEBCAM_OK : WEBCAM_FAIL; }
int WebCamRT_OpenConfig_SetFormatSource(int id, int v) { (void)v; return cam(id) ? WEBCAM_OK : WEBCAM_FAIL; }
int WebCamRT_OpenConfig_SetGreyscale(int id) { return cam(id) ? WEBCAM_OK : WEBCAM_FAIL; }

// The size asked: the device's at it exactly, else the camera is refused
// (the game checks it).
int WebCamRT_CameraOpen(int id)
{
    Webcam *c = cam(id);
    if (!c)
        return WEBCAM_FAIL;
    if (c->fd >= 0)
    {
        struct v4l2_format f;
        struct v4l2_streamparm parm;
        memset(&f, 0, sizeof(f));
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        f.fmt.pix.width = c->width;
        f.fmt.pix.height = c->height;
        f.fmt.pix.pixelformat = c->format;
        f.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(c->fd, VIDIOC_S_FMT, &f) < 0 || f.fmt.pix.pixelformat != c->format ||
            (int)f.fmt.pix.width != c->width || (int)f.fmt.pix.height != c->height)
        {
            unityLog("camera %s: cannot capture %dx%d (%ux%u): a blank camera\n", c->path, c->width, c->height,
                    f.fmt.pix.width, f.fmt.pix.height);
            close(c->fd);
            c->fd = -1;
        }
        c->stride = f.fmt.pix.bytesperline ? (int)f.fmt.pix.bytesperline
                    : c->width * (c->format == V4L2_PIX_FMT_YUYV || c->format == V4L2_PIX_FMT_UYVY ? 2 : 1);
        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = WEBCAM_FPS;
        xioctl(c->fd, VIDIOC_S_PARM, &parm);
    }
    free(c->frame);
    if (!(c->frame = calloc((size_t)c->width * c->height, 1)))
        return WEBCAM_FAIL;
    memset(c->frame, 0xff, (size_t)c->width * c->height); // a blank camera sees white
    if (isStill(c->path) && !loadStill(c))
    {
        free(c->still);
        c->still = NULL; // a blank camera
    }
    unityLog("camera %s (%s): open, %dx%d\n", c->path, c->name, c->width, c->height);
    return WEBCAM_OK;
}

int WebCamRT_CameraClose(int id)
{
    Webcam *c = cam(id);
    if (c)
        stopCapture(c);
    return WEBCAM_OK;
}

int WebCamRT_CaptureBegin(int id)
{
    Webcam *c = cam(id);
    return c && c->frame ? startCapture(c) : WEBCAM_FAIL;
}

int WebCamRT_CaptureEnd(int id)
{
    Webcam *c = cam(id);
    if (c)
        stopCapture(c);
    return WEBCAM_OK;
}

int WebCamRT_IsCapturing(int id)
{
    Webcam *c = cam(id);
    return c && c->capturing ? WEBCAM_OK : WEBCAM_FAIL;
}

// A frame is there once one was captured (a still's at once; a blank
// camera's never). The game's scan threads ask in a loop, without a pause,
// before decoding the frame its main thread grabbed: they are made to wait
// for a newer one (up to 100 ms), so that each frame is decoded once.
static int hasFrame(Webcam *c)
{
    return c->capturing && (c->fd >= 0 ? c->frames > 0 : c->still != NULL);
}

int WebCamRT_IsFrameReady(int id)
{
    Webcam *c = cam(id);
    return c && hasFrame(c) ? WEBCAM_OK : WEBCAM_FAIL;
}

int WebCamRT_IsCapturingAndFrameReady(int id)
{
    Webcam *c = cam(id);
    int ready;
    if (!c)
        return WEBCAM_FAIL;
    if ((pid_t)syscall(SYS_gettid) == mainThread)
        return hasFrame(c) ? WEBCAM_OK : WEBCAM_FAIL;
    pthread_mutex_lock(&c->lock);
    if (c->fd >= 0 && c->capturing && c->frames == c->scanned)
    {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 100 * 1000000;
        if (until.tv_nsec >= 1000000000)
        {
            until.tv_sec++;
            until.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&c->newFrame, &c->lock, &until);
    }
    ready = hasFrame(c) && (c->fd < 0 || c->frames != c->scanned);
    c->scanned = c->frames;
    pthread_mutex_unlock(&c->lock);
    if (!ready || c->fd < 0)
        usleep(c->fd < 0 ? 100000 : 0); // a still or a blank camera: 10 times a second
    return ready ? WEBCAM_OK : WEBCAM_FAIL;
}

int WebCamRT_IsDisconnected(int id)
{
    Webcam *c = cam(id);
    return c && !c->gone ? 1 : WEBCAM_OK;
}

int WebCamRT_GrabFrame(int id, void *buf, unsigned size)
{
    Webcam *c = cam(id);
    size_t n;
    if (!c || !c->frame || !buf)
        return WEBCAM_FAIL;
    n = (size_t)c->width * c->height;
    pthread_mutex_lock(&c->lock);
    memcpy(buf, c->still && (long)now() / 2 % 2 ? c->still : c->frame, size < n ? size : n);
    pthread_mutex_unlock(&c->lock);
    return WEBCAM_OK;
}

int WebCamRT_GetProp_DeviceName(int id, char *out, int max)
{
    Webcam *c = cam(id);
    if (!out || max <= 0)
        return WEBCAM_FAIL;
    snprintf(out, max, "%s", c ? c->name : "");
    return c ? WEBCAM_OK : WEBCAM_FAIL;
}

int WebCamRT_GetProp_Width(int id) { return cam(id) ? cam(id)->width : 0; }
int WebCamRT_GetProp_Height(int id) { return cam(id) ? cam(id)->height : 0; }
int WebCamRT_GetProp_DestFormat(int id) { return cam(id) ? WEBCAM_DFORMAT_GREY : 0; }
int WebCamRT_GetProp_DestBytesPerPixel(int id) { return cam(id) ? 1 : 0; }

// The cameras' own settings (brightness...): left to them.
int WebCamRT_GetSettingSupported(int id, int setting) { (void)id; (void)setting; return WEBCAM_FAIL; }
int WebCamRT_GetSettingName(int id, int setting, char *out, int max)
{
    (void)id; (void)setting;
    if (out && max > 0)
        out[0] = 0;
    return WEBCAM_FAIL;
}
int WebCamRT_GetSettingValue01(int id, int setting, float *v) { (void)id; (void)setting; *v = 0.f; return WEBCAM_FAIL; }
int WebCamRT_GetSettingDefalut01(int id, int setting, float *v) { (void)id; (void)setting; *v = 0.f; return WEBCAM_FAIL; }
int WebCamRT_SetSettingValue01(int id, int setting, float v) { (void)id; (void)setting; (void)v; return WEBCAM_FAIL; }
int WebCamRT_GetSettingStep(int id, int setting, float *v) { (void)id; (void)setting; *v = 0.f; return WEBCAM_FAIL; }

int WebCamRT_GetRealFPS(int id, float *v)
{
    Webcam *c = cam(id);
    *v = c ? (c->fd >= 0 ? c->fps : (float)WEBCAM_FPS) : 0.f;
    return c ? WEBCAM_OK : WEBCAM_FAIL;
}
int WebCamRT_GetErrorFlags(int id) { (void)id; return 0; }
int WebCamRT_GetCameraFlags(int id) { (void)id; return 0; }
int WebCamRT_SetCameraFlags(int id, int flags) { (void)id; (void)flags; return WEBCAM_OK; }
int WebCamRT_GetDeviceInfo(int id, int *vid, int *pid, int *bcd)
{
    (void)id;
    *vid = *pid = *bcd = 0;
    return WEBCAM_FAIL;
}
int WebCamRT_GetMaxPath(void) { return 256; }
int WebCamRT_GetMaxName(void) { return 64; }
int WebCamRT_SetTimeout(float v) { (void)v; return WEBCAM_OK; }
int WebCamRT_GetTimeout(float *v)
{
    *v = 15.f;
    return WEBCAM_OK;
}
