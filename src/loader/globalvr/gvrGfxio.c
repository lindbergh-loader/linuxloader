// The GFXIO board of Global VR cabinets: an IO-Warrior USB chip behind the
// gfxio kernel driver (gfxio.o, /dev/usb/gfxio0).
//
// A thread of the game reads the board's reports (4 bytes) as they come,
// blocking:
//  byte 0, 1  the trackball's counts since the last report (signed x, y;
//             positive rolling left, and away from the player)
//  byte 2     buttons, held: bit 0 CAMERA, 1 MUSIC, 2 COMM, 3 MENU, 4 SELECT
//  byte 3     bit 0 COIN1, 1 COIN2, 2 TEST, 3 SERV, 4 DOOR (open)
// The game counts a coin on its switch's press. Its writes are the lamps
// (ioctl IOW_WRITE, 0x4004c001).
//
// Input: the loader's JVS state (the SDL keyboard, or evdev) and the
// trackball's motion (input/trackball.h: the mouse, captured, or an evdev
// mouse/trackball's relative axes):
//  SELECT  player 1 START or BUTTON_1, the left mouse button
//  MENU    player 2 START or player 1 BUTTON_2, the right mouse button
//  CAMERA, MUSIC, COMM  player 1 BUTTON_3, BUTTON_4, BUTTON_5
//  DOOR    player 1 BUTTON_6, open while held
//  TEST, SERV  TEST, SERVICE; COIN1, COIN2 the coins
//  the trackball also from player 1's directions (pads, keys).

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gvr.h"
#include "../hardware/lindbergh/jvs.h"
#include "../input/trackball.h"

#define GFXIO_REPORT_SIZE 4
#define GFXIO_REPORT_US 8000 // the interrupt endpoint's period
#define IOW_WRITE 0x4004c001

#define BTN_CAMERA 0x01
#define BTN_MUSIC 0x02
#define BTN_COMM 0x04
#define BTN_MENU 0x08
#define BTN_SELECT 0x10
#define SYS_COIN1 0x01
#define SYS_COIN2 0x02
#define SYS_TEST 0x04
#define SYS_SERV 0x08
#define SYS_DOOR 0x10

// A coin's switch stays pressed for this many reports (about 100 ms).
#define COIN_REPORTS 12
// A shot is a stroke the game measures by the frame: about 18 board counts
// a report, sustained for a quarter of a second, for the slowest; its power
// is the stroke's speed at the end. A mouse moves a shorter way than a
// 3-inch ball and its flicks are short, so:
// - its counts (or an evdev trackball's) are accelerated: the gain grows
//   with the speed (mouse counts a report, smoothed over a few reports),
//   slow moves staying fine for aiming;
// - a report counts REPORT_COUNTS_MAX at most, the rest of a flick waiting
//   for the next ones: a short flick makes a stroke as long as a ball's,
//   at a power the game can take.
#define TRACKBALL_GAIN_MIN 4.0f
#define TRACKBALL_GAIN_MAX 10.0f
#define TRACKBALL_SPEED_PER_GAIN 2.0f
#define REPORT_COUNTS_MAX 45
// At most this much motion waits for later reports (a second's worth).
#define PENDING_MAX (REPORT_COUNTS_MAX * 125)
// A held direction counts from this, a report, up to the maximum in about
// a quarter of a second: a pad's stick shoots too.
#define DIRECTION_COUNTS 10
#define DIRECTION_COUNTS_MAX 40
// Mouse buttons in trackballButtons() (SDL's left 1, right 3).
#define MOUSE_LEFT (1u << 0)
#define MOUSE_RIGHT (1u << 2)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int gfxioFd = -1;
static int trace;

int gvrGfxioIsPath(const char *path)
{
    return isGvrGame() && path && !strcmp(path, "/dev/usb/gfxio0");
}

int gvrGfxioIsFd(int fd)
{
    return fd >= 0 && fd == gfxioFd;
}

int gvrGfxioOpen(int (*realOpen)(const char *, int, ...))
{
    pthread_mutex_lock(&lock);
    trace = getenv("GVR_GFXIO_TRACE") != NULL;
    if (gfxioFd < 0)
        gfxioFd = realOpen("/dev/null", O_RDWR);
    pthread_mutex_unlock(&lock);
    // The trackball is the mouse: kept in the window.
    trackballRequestCapture();
    return gfxioFd;
}

void gvrGfxioClose(void)
{
    pthread_mutex_lock(&lock);
    gfxioFd = -1;
    pthread_mutex_unlock(&lock);
}

static int8_t counts(int *pending)
{
    int c = *pending < -REPORT_COUNTS_MAX ? -REPORT_COUNTS_MAX
            : *pending > REPORT_COUNTS_MAX ? REPORT_COUNTS_MAX
                                           : *pending;
    *pending -= c;
    return (int8_t)c;
}

ssize_t gvrGfxioRead(void *buf, size_t count)
{
    static int pendingX, pendingY, directionCounts = DIRECTION_COUNTS;
    static float speed, restX, restY;
    static int lastCoins[2], coinReports[2], coinsSeen;
    uint8_t *report = buf;
    JVSIO *io = getJVSIO();
    unsigned int mouse = trackballButtons();
    int dx, dy;

    if (count < GFXIO_REPORT_SIZE)
        return -1;
    usleep(GFXIO_REPORT_US);

    trackballTake(&dx, &dy);
    // The mouse's motion comes by the video frame: the speed is smoothed
    // over the reports in between.
    speed += ((abs(dx) + abs(dy)) - speed) * 0.25f;
    float gain = TRACKBALL_GAIN_MIN + speed / TRACKBALL_SPEED_PER_GAIN;
    if (gain > TRACKBALL_GAIN_MAX)
        gain = TRACKBALL_GAIN_MAX;
    restX += dx * gain;
    restY += dy * gain;
    dx = (int)restX;
    dy = (int)restY;
    restX -= dx;
    restY -= dy;

    uint32_t p1 = io->state.inputSwitch[PLAYER_1], p2 = io->state.inputSwitch[PLAYER_2];
    if (p1 & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN))
    {
        if (directionCounts < DIRECTION_COUNTS_MAX)
            directionCounts++;
    }
    else
        directionCounts = DIRECTION_COUNTS;
    if (p1 & BUTTON_LEFT)
        dx -= directionCounts;
    if (p1 & BUTTON_RIGHT)
        dx += directionCounts;
    if (p1 & BUTTON_UP)
        dy -= directionCounts;
    if (p1 & BUTTON_DOWN)
        dy += directionCounts;
    // The board counts the other way round from the mouse on both axes:
    // a roll to the left, and away from the player (the mouse moved up),
    // positive.
    pendingX -= dx;
    pendingY -= dy;
    pendingX = pendingX < -PENDING_MAX ? -PENDING_MAX : pendingX > PENDING_MAX ? PENDING_MAX : pendingX;
    pendingY = pendingY < -PENDING_MAX ? -PENDING_MAX : pendingY > PENDING_MAX ? PENDING_MAX : pendingY;

    memset(report, 0, GFXIO_REPORT_SIZE);
    report[0] = (uint8_t)counts(&pendingX);
    report[1] = (uint8_t)counts(&pendingY);

    if ((p1 & (BUTTON_START | BUTTON_1)) || (mouse & MOUSE_LEFT))
        report[2] |= BTN_SELECT;
    if ((p2 & BUTTON_START) || (p1 & BUTTON_2) || (mouse & MOUSE_RIGHT))
        report[2] |= BTN_MENU;
    if (p1 & BUTTON_3)
        report[2] |= BTN_CAMERA;
    if (p1 & BUTTON_4)
        report[2] |= BTN_MUSIC;
    if (p1 & BUTTON_5)
        report[2] |= BTN_COMM;

    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        report[3] |= SYS_TEST;
    if ((p1 | p2) & BUTTON_SERVICE)
        report[3] |= SYS_SERV;
    if (p1 & BUTTON_6)
        report[3] |= SYS_DOOR;
    for (int c = 0; c < 2; c++)
    {
        int coins = io->state.coinCount[c];
        if (coinsSeen && coins > lastCoins[c])
            coinReports[c] = COIN_REPORTS;
        lastCoins[c] = coins;
        if (coinReports[c] > 0)
        {
            coinReports[c]--;
            report[3] |= c ? SYS_COIN2 : SYS_COIN1;
        }
    }
    coinsSeen = 1;

    if (trace && (report[0] || report[1] || report[2] || report[3]))
        fprintf(stderr, "GVR GFXIO: %+4d %+4d buttons %02x %02x\n", (int8_t)report[0], (int8_t)report[1], report[2],
                report[3]);
    return GFXIO_REPORT_SIZE;
}

int gvrGfxioIoctl(unsigned long request, void *arg)
{
    if (trace && request == IOW_WRITE && arg)
    {
        static uint32_t lamps = 0xffffffff;
        uint32_t value = *(uint32_t *)arg;
        if (value != lamps)
            fprintf(stderr, "GVR GFXIO: lamps %08x\n", value);
        lamps = value;
    }
    return 0;
}
