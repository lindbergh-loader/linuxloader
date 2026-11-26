// The IT games' cabinet devices (see itechGame.h), stood in for at the IT
// platform layer's own system calls: ITopen, ITioctl, ITread, ITwrite and
// ITclose, which every driver's use goes through. A device's fd is a real
// one (/dev/null's), so that the game's own close() or select() on it work;
// its reads, writes and ioctls are the loader's.
//
// The USB I/O board (/dev/ioboard1, the ITIOBOARD1 driver, ioctl magic
// 0xd1): its packets in are 12 bytes, read() a packet at a time. The
// switches in bytes 7-10, active low (cleared when pressed: the game's
// events test the switch word inverted), as CONTROLS_ReadSwitch puts them
// in the switch word of the PCI card the board replaced:
//   byte 8: bit 7 test, 6 service, 5 volume down, 4 volume up, 3 bill,
//           2 coin 3, 1 coin 2, 0 coin 1
//   byte 7: bit 0 start, bits 1-7 player 1's switches A-G: C and D the
//           flippers (left and right), A and B the cabinet's nudges (right
//           and left; both, a bump), E select
//   byte 9: bit 1 coin 4, bit 7 player 1's switch H; byte 10: bit 0 switch I
// Bytes 6 and 9 also hold the DIP switches, read inverted too: all zero, all
// on, the video ones 640x480 (index 15). Bytes 1-3 carry the
// board's security answers; the game's checks of them are the loader's
// (itech.c). Its 24-byte writes (lamps, meters) are taken and dropped.
//
// The USB dongle, the "cabinet ID device" (/dev/itdongle1, the
// ITDongle1Driver, magic 0xd1 too): 8 storage blocks of 512 bytes, each
// ending in its CRC32, kept in the game directory's itdongle1.bin.
// Block 0 is the cabinet's identity: header version (byte 0), product code
// (byte 4, 0x10 "Pinball 1 OCC") and serial number (bytes 8-11); block 1
// header version 0's ITnet data (the operator). Made at the first start.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <SDL3/SDL_gamepad.h>

#include "itech.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"
#include "../mainShared.h"

// The driver's ioctls the game uses.
#define IOB_GET_OVERFLOW 0x8004d101
#define IOB_GET_STORAGE0 0x8004d102
#define IOB_GET_VERSION 0x8004d105
#define IOB_GET_LATEST 0x8004d106
#define IOB_PIC 0x8004d108
#define IOB_GET_DRVR_VERSION 0x8004d180

#define DONGLE_READ 0x8004d100
#define DONGLE_WRITE 0x4004d101
#define DONGLE_GET_VERSION 0x8004d102
#define DONGLE_GET_DMODE 0x8001d103
#define DONGLE_GET_DRVR_VERSION 0x8004d180

#define DONGLE_BLOCKS 8
#define DONGLE_BLOCK 0x200
#define OCC_PRODUCT_CODE 0x10
#define DONGLE_SERIAL 100001

#define PACKET_SIZE 12
#define OUT_SIZE 24
// The board's packet rate: one each 8 ms.
#define PACKET_NS 8000000L

#define COIN_TICKS 6

// Player 1's switches in byte 7.
#define SWITCH_A 0x02
#define SWITCH_B 0x04
#define SWITCH_C 0x08
#define SWITCH_D 0x10
#define SWITCH_E 0x20
// A gamepad's trigger pulled this far presses its flipper.
#define TRIGGER_PRESSED 16384

typedef enum
{
    DEV_NONE,
    DEV_IOBOARD1,
    DEV_DONGLE1,
} DeviceKind;

#define MAX_FDS 1024
static DeviceKind devices[MAX_FDS];

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static JVSIO *inputState(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
}

// The version blocks the driver and the board answer (34 bytes): a name, a
// version and a date.
typedef struct __attribute__((packed))
{
    char name[20];
    uint16_t version, month, day, year;
    uint8_t pad[6];
} VersionBlock;

static int version(void *arg, size_t size, const char *name, uint16_t v)
{
    VersionBlock b = {{0}, v, 8, 17, 2005, {0}};
    snprintf(b.name, sizeof(b.name), "%s", name);
    memcpy(arg, &b, size);
    return size;
}

// A coin slot's pulses, held then released COIN_TICKS packets each.
typedef struct
{
    int counted, pending, ticks;
} Coin;

static int coinTick(Coin *c, int count)
{
    if (count > c->counted)
        c->pending += count - c->counted;
    c->counted = count;
    if (!c->ticks && c->pending)
    {
        c->pending--;
        c->ticks = 2 * COIN_TICKS;
    }
    if (!c->ticks)
        return 0;
    return c->ticks-- > COIN_TICKS;
}

static void packet(uint8_t p[PACKET_SIZE])
{
    static Coin coin1, coin2;
    JVSIO *io = inputState();
    unsigned int in = io->state.inputSwitch[PLAYER_1];
    int leftTrigger = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_PRESSED;
    int rightTrigger = desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_PRESSED;

    memset(p, 0, PACKET_SIZE);
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        p[8] |= 0x80;
    if (in & BUTTON_SERVICE)
        p[8] |= 0x40;
    if (in & BUTTON_7)
        p[8] |= 0x10;
    if (in & BUTTON_8)
        p[8] |= 0x20;
    if (coinTick(&coin1, io->state.coinCount[0]))
        p[8] |= 0x01;
    if (coinTick(&coin2, io->state.coinCount[1]))
        p[8] |= 0x02;
    if (in & BUTTON_START)
        p[7] |= 0x01;
    // Player 1's switches: C and D the flippers (left and right, also the
    // menus' choice), A and B the cabinet's nudges (right and left; both, a
    // bump), E select.
    if (in & BUTTON_LEFT || leftTrigger || desktopGamepadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
        p[7] |= SWITCH_C;
    if (in & BUTTON_RIGHT || rightTrigger || desktopGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
        p[7] |= SWITCH_D;
    int bump = in & BUTTON_UP || desktopGamepadButton(SDL_GAMEPAD_BUTTON_NORTH);
    if (bump || in & BUTTON_3 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_EAST))
        p[7] |= SWITCH_A;
    if (bump || in & BUTTON_2 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_WEST))
        p[7] |= SWITCH_B;
    if (in & BUTTON_1)
        p[7] |= SWITCH_E;
    // The switches are active low: a released one reads set.
    p[7] = ~p[7];
    p[8] = ~p[8];
    p[9] ^= 0x82;
    p[10] ^= 0x01;
}

// A read blocks until the board's next packet, as the driver's does: that
// beat is the game's clock (the controls task reads a period's packets,
// ReadControlsPeriod / 8, then counts the frame time passed).
static ssize_t ioboardRead(void *buf, size_t count)
{
    static struct timespec next;
    struct timespec now;

    if (count < PACKET_SIZE)
    {
        errno = EINVAL;
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!next.tv_sec || now.tv_sec > next.tv_sec + 1)
        next = now;
    next.tv_nsec += PACKET_NS;
    if (next.tv_nsec >= 1000000000L)
    {
        next.tv_sec++;
        next.tv_nsec -= 1000000000L;
    }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
        ;
    pthread_mutex_lock(&lock);
    packet(buf);
    pthread_mutex_unlock(&lock);
    return PACKET_SIZE;
}

static int ioboardIoctl(unsigned long request, void *arg)
{
    switch (request)
    {
        case IOB_GET_DRVR_VERSION:
            return version(arg, sizeof(VersionBlock), "IOBOARD1Drvr", 7);
        case IOB_GET_VERSION:
            return version(arg, sizeof(VersionBlock), "ITIOBoard1", 0x1d);
        case IOB_GET_OVERFLOW:
            *(uint32_t *)arg = 0;
            return 0;
        case IOB_GET_STORAGE0:
            // Never written (the game's "never initialized").
            memset((uint8_t *)arg + 2, 0, 0x7e);
            return 0;
        case IOB_GET_LATEST:
        {
            // {length, result, packet}
            int32_t *r = arg;
            packet((uint8_t *)&r[2]);
            r[1] = PACKET_SIZE;
            return 0;
        }
        case IOB_PIC:
        {
            // {result, read (1) or write, address, value}: the PIC's EEPROM,
            // its coinage and country block at 0x86 (not all zeros).
            uint8_t *b = arg;
            *(int32_t *)b = 0;
            if (b[4] == 1)
                b[6] = b[5] == 0x86 ? 1 : 0;
            return 0;
        }
        default:
            // Resets, the blink code, the program and storage writes.
            return 0;
    }
}

// The dongle's storage: {block, pad, buffer, length}, as the driver takes
// it.
typedef struct
{
    uint16_t block, pad;
    uint8_t *buf;
    uint16_t length;
} DongleIo;

static uint8_t dongle[DONGLE_BLOCKS][DONGLE_BLOCK];
static char donglePath[4096];

static void dongleSeal(uint8_t *block)
{
    uint32_t crc = getCrc32Mem(block, DONGLE_BLOCK - 4);
    memcpy(block + DONGLE_BLOCK - 4, &crc, 4);
}

static void dongleSave(void)
{
    FILE *f = fopen(donglePath, "wb");
    if (!f || fwrite(dongle, sizeof(dongle), 1, f) != 1)
        log_warn("IT: cannot save the dongle to %s", donglePath);
    if (f)
        fclose(f);
}

static void dongleLoad(void)
{
    char buf[4096];
    FILE *f;

    snprintf(donglePath, sizeof(donglePath), "%s", itechRedirectPath("/game/dynamic/itdongle1.bin", buf, sizeof(buf)));
    if ((f = fopen(donglePath, "rb")))
    {
        size_t n = fread(dongle, sizeof(dongle), 1, f);
        fclose(f);
        if (n == 1)
            return;
    }
    // A new cabinet's: its identity and an operator block, the others
    // unwritten (erased).
    memset(dongle, 0xff, sizeof(dongle));
    memset(dongle[0], 0, DONGLE_BLOCK);
    dongle[0][4] = OCC_PRODUCT_CODE;
    uint32_t serial = DONGLE_SERIAL;
    memcpy(&dongle[0][8], &serial, 4);
    dongleSeal(dongle[0]);
    memset(dongle[1], 0, DONGLE_BLOCK);
    dongleSeal(dongle[1]);
    dongleSave();
}

static int dongleIoctl(unsigned long request, void *arg)
{
    DongleIo *io = arg;

    switch (request)
    {
        case DONGLE_GET_DRVR_VERSION:
            return version(arg, 0x20, "ITDongle1Drvr", 7);
        case DONGLE_GET_VERSION:
            return version(arg, 0x20, "ITDongle1", 0x100);
        case DONGLE_GET_DMODE:
            // Production, not debug.
            *(uint8_t *)arg = 0;
            return 0;
        case DONGLE_READ:
        case DONGLE_WRITE:
            if (io->block >= DONGLE_BLOCKS || io->length > DONGLE_BLOCK)
            {
                errno = EINVAL;
                return -1;
            }
            if (request == DONGLE_READ)
                memcpy(io->buf, dongle[io->block], io->length);
            else
            {
                memcpy(dongle[io->block], io->buf, io->length);
                dongleSave();
            }
            return io->length;
        default:
            return 0;
    }
}

__attribute__((force_align_arg_pointer)) static int itOpen(const char *path, int flags)
{
    DeviceKind kind = DEV_NONE;

    if (path && !strcmp(path, "/dev/ioboard1"))
        kind = DEV_IOBOARD1;
    else if (path && !strcmp(path, "/dev/itdongle1"))
        kind = DEV_DONGLE1;
    if (kind == DEV_NONE)
        return open64(path, flags);
    int fd = open64("/dev/null", O_RDWR);
    if (fd >= 0 && fd < MAX_FDS)
        devices[fd] = kind;
    return fd;
}

static DeviceKind deviceOf(int fd)
{
    return fd >= 0 && fd < MAX_FDS ? devices[fd] : DEV_NONE;
}

__attribute__((force_align_arg_pointer)) static int itIoctl(int fd, unsigned long request, void *arg)
{
    int r;

    switch (deviceOf(fd))
    {
        case DEV_IOBOARD1:
            pthread_mutex_lock(&lock);
            r = ioboardIoctl(request, arg);
            pthread_mutex_unlock(&lock);
            return r;
        case DEV_DONGLE1:
            pthread_mutex_lock(&lock);
            r = dongleIoctl(request, arg);
            pthread_mutex_unlock(&lock);
            return r;
        default:
            return ioctl(fd, request, arg);
    }
}

__attribute__((force_align_arg_pointer)) static ssize_t itRead(int fd, void *buf, size_t count)
{
    ssize_t r;

    switch (deviceOf(fd))
    {
        case DEV_IOBOARD1:
            return ioboardRead(buf, count);
        default:
            while ((r = read(fd, buf, count)) < 0 && errno == EINTR)
                ;
            return r;
    }
}

__attribute__((force_align_arg_pointer)) static ssize_t itWrite(int fd, const void *buf, size_t count)
{
    switch (deviceOf(fd))
    {
        case DEV_IOBOARD1:
        case DEV_DONGLE1:
            return count;
        default:
            return write(fd, buf, count);
    }
}

__attribute__((force_align_arg_pointer)) static int itClose(int fd)
{
    if (fd >= 0 && fd < MAX_FDS)
        devices[fd] = DEV_NONE;
    return close(fd);
}

void itechIoInstall(void)
{
    dongleLoad();
    itechDetour("ITopen", (void *)itOpen);
    itechDetour("ITioctl", (void *)itIoctl);
    itechDetour("ITread", (void *)itRead);
    itechDetour("ITwrite", (void *)itWrite);
    itechDetour("ITclose", (void *)itClose);
    desktopGamepadEnable();
}
