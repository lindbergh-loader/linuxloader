// Wheel of Fortune (Raw Thrills, g3): the hardware its boot depends on, which
// a PC does not have (see docs/wof.md).
//
// The dongle record. At start-up (0x8076730) the game reads a 0x24-byte
// record from the dongle's memory and its 4-byte checksum after it, through
// the HASP library's internals (DongleMemRead, 0x8206270), not hasp_read: a
// failed read makes main take its shutdown path once initialised, where it
// then hangs (stopping a driver's threads that were never started). With no
// dongle the record is a blank one, with the checksum the game computes for
// it (DongleChecksum, 0x81e9fa4), so it passes the game's check.

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../common/desktopInput.h"
#include "rtGame.h"
#include "../config/config.h"
#include "../input/evdevInput.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define RECORD_SIZE 0x24

static uint8_t record[RECORD_SIZE];
static uint32_t (*dongleChecksum)(const void *data, int length);

// DongleMemRead(handle, offset, length, buffer): 0 on success.
static int dongleMemRead(void *handle, int offset, int length, void *buffer)
{
    (void)handle;
    if (!buffer || offset < 0 || length < 0)
        return 3;
    if (offset == RECORD_SIZE && length == 4)
    {
        uint32_t sum = dongleChecksum ? dongleChecksum(record, RECORD_SIZE) : 0;
        memcpy(buffer, &sum, 4);
        return 0;
    }
    memset(buffer, 0, length);
    if (offset < RECORD_SIZE)
        memcpy(buffer, record + offset, length < RECORD_SIZE - offset ? length : RECORD_SIZE - offset);
    return 0;
}

// The spinner (1241:1111, libusb-0.1): the game's thread reads 4-byte
// reports from it, byte 1 the wheel's movement since the last one (signed),
// and works the spin's speed out from them. It turns from the left and right
// arrows (P1 LEFT/RIGHT with evdev input, a pad's d-pad), slowly and
// steadily, to move through the menus; from the mouse's horizontal moves
// (analogue channel 1) and, with evdev input, from a stick on analogue
// channel 3, as fast as it is pushed: those spin the wheel. The input is
// read on the game's main thread each frame (RIO_SampleInput, as the switches
// are: the desktop keyboard's X connection is not shared between threads),
// and handed to the spinner thread's reads through a counter.

#define SPINNER_REPORT_US 8000
#define SPINNER_KEY_SPEED 200.0f   // counts/s while an arrow is held
#define SPINNER_STICK_MAX 4000.0f  // counts/s, the stick at full deflection
#define SPINNER_MOUSE_SCALE 0.02f // counts per analogue unit (0xffff a window)
#define SPINNER_STICK_DEADZONE 0.1f // of the stick's half travel

extern Controllers controllers;

static int spinnerHandle; // the game's usb_dev_handle: any non-NULL pointer
static int stickMapped;   // ANALOGUE_3 has a device (else it reads 0: full left)
static volatile int spinnerPending;
static int (*rioSampleInputOrig)(void);

static void spinnerAdd(int counts)
{
    __sync_fetch_and_add(&spinnerPending, counts);
}

static int sampleInput(void)
{
    static struct timespec last;
    static float carry;
    static int lastX = -1;
    struct timespec now;
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();

    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = last.tv_sec ? (now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9f : 0;
    last = now;
    if (dt > 0.1f)
        dt = 0.1f;

    int dir = ((io->state.inputSwitch[PLAYER_1] & BUTTON_RIGHT) != 0) -
              ((io->state.inputSwitch[PLAYER_1] & BUTTON_LEFT) != 0);
    carry += dir * SPINNER_KEY_SPEED * dt;

    if (stickMapped && io->analogueMax > 0)
    {
        float half = io->analogueMax / 2.0f;
        float d = (io->state.analogueChannel[ANALOGUE_3] - half) / half;
        if (d > SPINNER_STICK_DEADZONE || d < -SPINNER_STICK_DEADZONE)
        {
            d = (d - (d > 0 ? SPINNER_STICK_DEADZONE : -SPINNER_STICK_DEADZONE)) / (1 - SPINNER_STICK_DEADZONE);
            carry += (d > 1 ? 1 : d < -1 ? -1 : d) * SPINNER_STICK_MAX * dt;
        }
    }

    int x = io->state.analogueChannel[ANALOGUE_1];
    if (x > 0 && lastX > 0)
        carry += (x - lastX) * SPINNER_MOUSE_SCALE;
    lastX = x;

    int counts = (int)carry;
    carry -= counts;
    if (counts)
        spinnerAdd(counts);
    return rioSampleInputOrig ? rioSampleInputOrig() : 0;
}

static int spinnerOpen(void)
{
    *(void **)0x084f3fa0 = &spinnerHandle;
    return 0;
}

// As the game's, without the libusb close of a device it never opened.
static int spinnerClose(void)
{
    *(void **)0x084f3fa0 = NULL;
    *(int *)0x0854e220 = 1; // stops the spinner's thread
    return 0;
}

// usb_interrupt_read(handle, endpoint, buffer, size, timeout): the bytes
// read, -110 on a timeout. The spinner reports only when it moves: the game
// divides the time between reports by their movement.
static int spinnerUsbRead(void *handle, int endpoint, char *buffer, int size, int timeout)
{
    (void)endpoint;
    (void)timeout;
    usleep(SPINNER_REPORT_US);
    if (handle != &spinnerHandle || !buffer || size < 4)
        return -110;
    int counts = spinnerPending;
    if (counts > 127)
        counts = 127;
    if (counts < -127)
        counts = -127;
    if (!counts)
        return -110;
    __sync_fetch_and_sub(&spinnerPending, counts);
    memset(buffer, 0, size);
    buffer[1] = (char)counts;
    return 4;
}

void rtWofInstall(const RtGame *game)
{
    (void)game;
    dongleChecksum = (uint32_t (*)(const void *, int))rtSymbol("DongleChecksum");
    if (rtDetour("DongleMemRead", dongleMemRead) != 0)
        log_warn("Raw Thrills: cannot hook Wheel of Fortune's dongle memory read");
    stickMapped = getConfig()->inputMode == 2 && evdevAxisController(&controllers, "ANALOGUE_3");
    rioSampleInputOrig = rtTrampoline("RIO_SampleInput", 6);
    if (rtDetour("RIO_SampleInput", sampleInput) != 0 || rtDetour("SpinnerOpen", spinnerOpen) != 0 ||
        rtDetour("SpinnerClose", spinnerClose) != 0 || rtDetour("SpinnerUsbRead", spinnerUsbRead) != 0)
        log_warn("Raw Thrills: Wheel of Fortune's spinner not installed");
}
