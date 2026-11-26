// The Cosmodog games' serial I/O controller (see cosmodogGame.h), stood in
// for at its packets: the game's IOPACKET_SendMessage is the controller's
// receiving one, its IOPACKET_GetMessage hands it the controller's answers,
// none of the serial line, its 7-bit packing, CRC or key.
//
// The game's messages (their first byte) and the controller's answers (a
// length byte, then theirs):
//   0      its version?     -> 0: version (3 bytes), licence slots total and
//                              used, the licences' CRC
//   1      poll             -> 1: switches (2 bytes, see the bits below), coin
//                              counters 1-3 and the trackball's horizontal and
//                              vertical counts (16-bit, big-endian, running
//                              totals), a flag byte
//   4      reset            -> 4: ready
//   5      serial number?   -> 6: the cabinet's serial number (16-bit)
//   0xc    write a licence  -> 0xc: its status (bit 7: refused)
//   0xd    find a licence   -> 0xd: its status, SHA-1 of the licence and "ok"
// (2 sets the coin meters, 9 the lamps, 0xa and 0xb the ticket dispenser,
// 6 hands it the MAC address and disk serial: nothing to answer.)
//
// Every licence asked for is there: the game's features (tournaments, the
// sixties lane) are on.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cosmodog.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../input/trackball.h"
#include "../log/log.h"
#include "../patching/flowControl.h"
#include <SDL3/SDL_gamepad.h>

#define MSG_VERSION 0
#define MSG_POLL 1
#define MSG_RESET 4
#define MSG_SERIAL 5
#define MSG_SERIAL_ANSWER 6
#define MSG_WRITE_LICENCE 0xc
#define MSG_FIND_LICENCE 0xd

// The switches' bits in the poll's answer (byte 2 << 8 | byte 3), as the
// game's ioBitMapping takes them, and the controls the loader gives them.
#define SW_REPLAY 0x1000
#define SW_SCORESHEET 0x2000
#define SW_JUKEBOX 0x4000
#define SW_START 0x8000
#define SW_SERVICE_CREDIT 0x0002
#define SW_VOLUME_UP 0x0004
#define SW_VOLUME_DOWN 0x0008
#define SW_MENU 0x0080

// The stick turns the trackball this many counts a poll at full tilt.
#define STICK_SPEED 40
#define STICK_DEADZONE 0.15

#define QUEUE 16
#define MESSAGE 64

static const CosmodogGame *game;
static uint8_t queue[QUEUE][MESSAGE];
static int queueHead, queueCount;
static uint8_t current[MESSAGE];

static void (*gameSha1)(const void *, unsigned int, int, void *);
static void (*convertHash)(const void *, void *);

static void answer(const uint8_t *message, int length)
{
    uint8_t *slot;

    if (queueCount == QUEUE || length + 1 > MESSAGE)
        return;
    slot = queue[(queueHead + queueCount++) % QUEUE];
    memset(slot, 0, MESSAGE);
    slot[0] = (uint8_t)(length + 1);
    memcpy(slot + 1, message, (size_t)length);
}

static JVSIO *inputState(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
}

// Start 1 / Start, replay Space / A, scoresheet z / X, jukebox x / Y, the
// menu F2, a service credit F1, the volume Page Up / Down; coins 5 and 6.
static uint16_t switches(JVSIO *io)
{
    unsigned int in = io->state.inputSwitch[PLAYER_1];
    uint16_t sw = 0;

    if (in & BUTTON_START)
        sw |= SW_START;
    if (in & BUTTON_1 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH))
        sw |= SW_REPLAY;
    if (in & BUTTON_2 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_WEST))
        sw |= SW_SCORESHEET;
    if (in & BUTTON_3 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_NORTH))
        sw |= SW_JUKEBOX;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        sw |= SW_MENU;
    if (in & BUTTON_SERVICE)
        sw |= SW_SERVICE_CREDIT;
    if (in & BUTTON_7)
        sw |= SW_VOLUME_UP;
    if (in & BUTTON_8)
        sw |= SW_VOLUME_DOWN;
    return sw;
}

// Debugging, from the first poll on: COSMODOG_SWITCHES=seconds:mask[,...]
// (hexadecimal, the switch bits above) the switches from then on,
// COSMODOG_COINS=seconds[,...] a coin then, COSMODOG_BALL=seconds:dx:dy[,...]
// the trackball rolled that much a poll from then on.
static double debugTime(void)
{
    static struct timespec start;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!start.tv_sec)
        start = now;
    return (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
}

static void debugInputs(uint16_t *sw, int *coins, int *dx, int *dy)
{
    double now = debugTime();
    const char *list;

    for (list = getenv("COSMODOG_SWITCHES"); list && *list;)
    {
        double at;
        unsigned int mask;
        if (sscanf(list, "%lf:%x", &at, &mask) == 2 && now >= at)
            *sw = (uint16_t)mask;
        list = strchr(list, ',');
        list = list ? list + 1 : NULL;
    }
    for (list = getenv("COSMODOG_COINS"); list && *list;)
    {
        double at;
        if (sscanf(list, "%lf", &at) == 1 && now >= at)
            (*coins)++;
        list = strchr(list, ',');
        list = list ? list + 1 : NULL;
    }
    for (list = getenv("COSMODOG_BALL"); list && *list;)
    {
        double at;
        int x, y;
        if (sscanf(list, "%lf:%d:%d", &at, &x, &y) == 3 && now >= at)
        {
            *dx = x;
            *dy = y;
        }
        list = strchr(list, ',');
        list = list ? list + 1 : NULL;
    }
}

static void poll(void)
{
    static int16_t ballX, ballY;
    JVSIO *io = inputState();
    uint16_t sw = switches(io);
    uint8_t m[14];
    int dx, dy;
    double sx = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;
    double sy = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTY) / 32768.0;

    int debugCoins = 0, ballX0 = 0, ballY0 = 0;

    trackballTake(&dx, &dy);
    debugInputs(&sw, &debugCoins, &ballX0, &ballY0);
    dx += ballX0;
    dy += ballY0;
    if (sx < -STICK_DEADZONE || sx > STICK_DEADZONE)
        dx += (int)(sx * STICK_SPEED);
    if (sy < -STICK_DEADZONE || sy > STICK_DEADZONE)
        dy += (int)(sy * STICK_SPEED);
    ballX = (int16_t)(ballX + dx);
    ballY = (int16_t)(ballY + dy);

    m[0] = MSG_POLL;
    m[1] = (uint8_t)(sw >> 8);
    m[2] = (uint8_t)sw;
    for (int c = 0; c < 3; c++)
    {
        int count = c < 2 ? io->state.coinCount[c] + (c ? 0 : debugCoins) : 0;
        m[3 + 2 * c] = (uint8_t)(count >> 8);
        m[4 + 2 * c] = (uint8_t)count;
    }
    m[9] = (uint8_t)((uint16_t)ballX >> 8);
    m[10] = (uint8_t)ballX;
    m[11] = (uint8_t)((uint16_t)ballY >> 8);
    m[12] = (uint8_t)ballY;
    m[13] = 0;
    answer(m, sizeof(m));
}

// A licence found: its hash as the game checks it (SHA-1 of the 20-byte
// licence and "ok", through its own functions).
static void findLicence(const uint8_t *licence)
{
    uint8_t data[64] = {0}, hash[64] = {0}, m[22];

    memcpy(data, licence, 20);
    data[20] = 'o';
    data[21] = 'k';
    gameSha1(data, 22 * 8, 0, hash);
    convertHash(hash, data);
    m[0] = MSG_FIND_LICENCE;
    m[1] = 0;
    memcpy(m + 2, data, 20);
    answer(m, sizeof(m));
}

__attribute__((force_align_arg_pointer)) static int sendMessage(int device, int length, const uint8_t *message, int secure)
{
    static const uint8_t version[] = {MSG_VERSION, 2, 2, 1, 8, 3, 0};
    static const uint8_t ready[] = {MSG_RESET};
    static const uint8_t serial[] = {MSG_SERIAL_ANSWER, 0x12, 0x34};
    static const uint8_t written[] = {MSG_WRITE_LICENCE, 0};

    (void)device;
    (void)secure;
    if (length < 1 || !message)
        return 0;
    switch (message[0])
    {
    case MSG_VERSION:
        answer(version, sizeof(version));
        break;
    case MSG_POLL:
        poll();
        break;
    case MSG_RESET:
        answer(ready, sizeof(ready));
        break;
    case MSG_SERIAL:
        answer(serial, sizeof(serial));
        break;
    case MSG_WRITE_LICENCE:
        answer(written, sizeof(written));
        break;
    case MSG_FIND_LICENCE:
        if (length >= 21)
            findLicence(message + 1);
        break;
    }
    return 1;
}

__attribute__((force_align_arg_pointer)) static uint8_t *getMessage(int device)
{
    static int started;

    (void)device;
    // Powered up: ready, as after a reset.
    if (!started)
    {
        static const uint8_t ready[] = {MSG_RESET};
        started = 1;
        answer(ready, sizeof(ready));
    }
    if (!queueCount)
        return NULL;
    memcpy(current, queue[queueHead], MESSAGE);
    queueHead = (queueHead + 1) % QUEUE;
    queueCount--;
    return current;
}

// The serial port: none to open.
__attribute__((force_align_arg_pointer)) static int ioInit(void)
{
    return 1;
}

void cosmodogIoInstall(const CosmodogGame *g)
{
    game = g;
    *(void **)&gameSha1 = (void *)(uintptr_t)g->sha1;
    *(void **)&convertHash = (void *)(uintptr_t)g->convertHash;
    // With evdev input the pads are evdev's (read twice otherwise).
    if (getConfig()->inputMode != 2)
        desktopGamepadEnable();
    // The mouse is the trackball: captured (unbounded, its pointer hidden).
    trackballRequestCapture();
    detourFunction(g->ioInit, ioInit);
    detourFunction(g->sendMessage, sendMessage);
    detourFunction(g->getMessage, getMessage);
    log_info("Cosmodog: I/O controller emulated");
}
