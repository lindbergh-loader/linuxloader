// Gamepads for the g7 games (see g7.h): read straight from the kernel's input
// devices, the first ones found going to players 1, 2, ... (by device
// number). A device is a pad if it has gamepad or joystick buttons
// (BTN_GAMEPAD, BTN_JOYSTICK); they are looked for again every few seconds
// while there are fewer than the players, so a pad can be plugged in late.
// Each pad reports, held:
//   G7_PAD_UP/DOWN/LEFT/RIGHT  the d-pad (buttons or hat) or the left stick
//                              past half its travel
//   G7_PAD_BUTTON              A, B, X or Y (a joystick's first buttons)
//   G7_PAD_START, G7_PAD_SELECT
//   G7_PAD_TEST, G7_PAD_SERVICE  R3, L3: the operator's buttons
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "g7.h"

#define BITS_PER_LONG (8 * sizeof(unsigned long))
#define TEST_BIT(bit, array) ((array[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)
#define RESCAN_S 3

static struct
{
    int fd;
    int number; // /dev/input/event<number>
    unsigned int held;
    // The stick's axes: their middle and half their travel.
    int axisMid[2], axisHalf[2];
    int stick, hat; // the directions they hold
} pads[G7_MAX_PADS];
static int padCount;

static int isPad(int fd)
{
    unsigned long keys[KEY_MAX / BITS_PER_LONG + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
        return 0;
    return TEST_BIT(BTN_GAMEPAD, keys) || TEST_BIT(BTN_JOYSTICK, keys) || TEST_BIT(BTN_TRIGGER, keys);
}

static int known(int number)
{
    for (int i = 0; i < padCount; i++)
        if (pads[i].number == number)
            return 1;
    return 0;
}

static void scan(void)
{
    int numbers[64], n = 0;
    DIR *d = opendir("/dev/input");
    struct dirent *e;

    if (!d)
        return;
    while ((e = readdir(d)) && n < 64)
        if (!strncmp(e->d_name, "event", 5))
            numbers[n++] = atoi(e->d_name + 5);
    closedir(d);
    // By device number: the order they were found in.
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && numbers[j - 1] > numbers[j]; j--)
        {
            int t = numbers[j];
            numbers[j] = numbers[j - 1];
            numbers[j - 1] = t;
        }
    for (int i = 0; i < n && padCount < G7_MAX_PADS; i++)
    {
        char path[64], name[128] = "";
        struct input_absinfo abs;
        int fd;

        if (known(numbers[i]))
            continue;
        snprintf(path, sizeof(path), "/dev/input/event%d", numbers[i]);
        if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
            continue;
        if (!isPad(fd))
        {
            close(fd);
            continue;
        }
        memset(&pads[padCount], 0, sizeof(pads[padCount]));
        pads[padCount].fd = fd;
        pads[padCount].number = numbers[i];
        for (int a = 0; a < 2; a++)
        {
            if (ioctl(fd, EVIOCGABS(a ? ABS_Y : ABS_X), &abs) == 0 && abs.maximum > abs.minimum)
            {
                pads[padCount].axisMid[a] = (abs.minimum + abs.maximum) / 2;
                pads[padCount].axisHalf[a] = (abs.maximum - abs.minimum) / 4;
            }
        }
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        g7Log("pad %d: %s (%s)\n", padCount + 1, name, path);
        padCount++;
    }
}

static unsigned int keyBit(int code)
{
    switch (code)
    {
    case BTN_SOUTH: case BTN_EAST: case BTN_NORTH: case BTN_WEST:
    case BTN_TRIGGER: case BTN_THUMB: case BTN_THUMB2: case BTN_TOP:
        return G7_PAD_BUTTON;
    case BTN_START: case BTN_BASE4:
        return G7_PAD_START;
    case BTN_SELECT: case BTN_BASE3:
        return G7_PAD_SELECT;
    case BTN_THUMBR:
        return G7_PAD_TEST;
    case BTN_THUMBL:
        return G7_PAD_SERVICE;
    case BTN_DPAD_UP: return G7_PAD_UP;
    case BTN_DPAD_DOWN: return G7_PAD_DOWN;
    case BTN_DPAD_LEFT: return G7_PAD_LEFT;
    case BTN_DPAD_RIGHT: return G7_PAD_RIGHT;
    default: return 0;
    }
}

static void readPad(int p)
{
    struct input_event ev;

    errno = 0;
    while (read(pads[p].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
    {
        if (ev.type == EV_KEY)
        {
            unsigned int bit = keyBit(ev.code);
            if (ev.value)
                pads[p].held |= bit;
            else
                pads[p].held &= ~bit;
        }
        else if (ev.type == EV_ABS && (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y))
        {
            unsigned int lo = ev.code == ABS_HAT0X ? G7_PAD_LEFT : G7_PAD_UP;
            unsigned int hi = ev.code == ABS_HAT0X ? G7_PAD_RIGHT : G7_PAD_DOWN;
            pads[p].hat = (pads[p].hat & ~(lo | hi)) | (ev.value < 0 ? lo : ev.value > 0 ? hi : 0);
        }
        else if (ev.type == EV_ABS && (ev.code == ABS_X || ev.code == ABS_Y) && pads[p].axisHalf[ev.code])
        {
            int a = ev.code, offset = ev.value - pads[p].axisMid[a];
            unsigned int lo = a ? G7_PAD_UP : G7_PAD_LEFT, hi = a ? G7_PAD_DOWN : G7_PAD_RIGHT;
            pads[p].stick = (pads[p].stick & ~(lo | hi)) |
                            (offset < -pads[p].axisHalf[a] ? lo : offset > pads[p].axisHalf[a] ? hi : 0);
        }
    }
    if (errno == ENODEV)
    {
        // Unplugged: the pads after it move up a player.
        g7Log("pad %d unplugged\n", p + 1);
        close(pads[p].fd);
        memmove(&pads[p], &pads[p + 1], (size_t)(padCount - p - 1) * sizeof(pads[0]));
        padCount--;
    }
}

unsigned int g7PadHeld(int player)
{
    return player >= 0 && player < padCount ? pads[player].held | pads[player].stick | pads[player].hat : 0;
}

void g7PadsPoll(int players)
{
    static struct timespec lastScan;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (padCount < players && (!lastScan.tv_sec || now.tv_sec - lastScan.tv_sec >= RESCAN_S))
    {
        lastScan = now;
        scan();
    }
    for (int p = padCount - 1; p >= 0; p--)
        readPad(p);
}
