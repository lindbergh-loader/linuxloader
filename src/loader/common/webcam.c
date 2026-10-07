// Webcams (Cruis'n Blast's player photos): the games look for their cameras
// among /dev/video0, 1, ... and check that each is still there by opening
// and closing it, thousands of times a second. On a PC:
//  - the devices that cannot give a colour picture in the format the games
//    ask for (YUYV) are hidden: a camera's metadata nodes, a laptop's
//    infrared camera (whose emitter then flashed); and the light guns' own
//    cameras (Sinden), which their driver streams from;
//  - the cameras kept are held open here, and the games' checks (read-only
//    opens) get a copy of that: the camera is not woken up at each one (its
//    LED flashed). The games' own opens to capture go through.

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "webcam.h"
#include "../rawthrills/rawthrills.h"

#define VIDEO_PREFIX "/dev/video"
#define MAX_VIDEO_DEVICES 64

// Per device: 0 not looked at, 1 a camera (fd held), -1 hidden.
static int videoState[MAX_VIDEO_DEVICES];
static int videoFd[MAX_VIDEO_DEVICES];

int webcamIsPath(const char *path)
{
    const char *n = path + sizeof(VIDEO_PREFIX) - 1;
    if (!isRawThrillsGame() || strncmp(path, VIDEO_PREFIX, sizeof(VIDEO_PREFIX) - 1) || !*n)
        return 0;
    for (; *n; n++)
        if (*n < '0' || *n > '9')
            return 0;
    return 1;
}

// A capture device that gives YUYV pictures, not a light gun's.
static int isColourCamera(int fd)
{
    struct v4l2_capability cap = {0};
    struct v4l2_fmtdesc fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 || strstr((const char *)cap.card, "Sinden"))
        return 0;
    if (!((cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities) & V4L2_CAP_VIDEO_CAPTURE))
        return 0;
    for (fmt.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++)
        if (fmt.pixelformat == V4L2_PIX_FMT_YUYV)
            return 1;
    return 0;
}

// Looks at /dev/videoN (once): 1 a camera, held open, -1 not one, 0 not
// there yet.
static int lookAt(int n, int (*realOpen)(const char *, int, ...))
{
    char path[32];

    if (videoState[n])
        return videoState[n];
    snprintf(path, sizeof(path), VIDEO_PREFIX "%d", n);
    int fd = realOpen(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return 0; // not there (yet): looked at again next time
    if (isColourCamera(fd))
    {
        videoState[n] = 1;
        videoFd[n] = fd;
    }
    else
    {
        videoState[n] = -1;
        close(fd);
    }
    return videoState[n];
}

// The first camera, or -1.
static int firstCamera(int (*realOpen)(const char *, int, ...))
{
    for (int n = 0; n < 16; n++)
        if (lookAt(n, realOpen) > 0)
            return n;
    return -1;
}

int webcamHasCamera(void)
{
    return firstCamera((int (*)(const char *, int, ...))open) >= 0;
}

int webcamOpen(const char *path, int flags, int mode, int (*realOpen)(const char *, int, ...))
{
    int n = atoi(path + sizeof(VIDEO_PREFIX) - 1);

    if (n < 0 || n >= MAX_VIDEO_DEVICES)
        return realOpen(path, flags, mode);
    if (!lookAt(n, realOpen))
        return realOpen(path, flags, mode);
    if (videoState[n] < 0)
    {
        errno = ENOENT;
        return -1;
    }
    if ((flags & O_ACCMODE) == O_RDONLY)
        return dup(videoFd[n]);
    return realOpen(path, flags, mode);
}
