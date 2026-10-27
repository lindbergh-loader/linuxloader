// The MegaJamma board of Teamplay cabinets, behind its kernel driver
// (mjg.o, /dev/mjg).
//
// The game reads the board's status packets (0xb8 bytes) until a read
// returns none, once a frame:
//  +0x00 flags: each gun's count of position samples (gun 1 bits 0-2, gun 2
//        bits 4-6; 0: the gun saw no light, off the screen) and its shot
//        (bit 12, 13), the trigger's flash
//  +0x04 the switches, held (bits below)
//  +0x0c the interrupt count, which must grow ("irq's out of order")
//  +0x10 the guns' last 4 samples (a ring, 4 words per gun): x in bits
//        0-11, y in bits 12-23, in the board's units
//  +0x30 the coin mechs' counts (one word each), each increment a coin
//        ("invalid delta" if one goes down)
// The game averages a gun's samples (dropping the extremes) into screen
// pixels, its shot handler takes the position of a packet with the shot
// flag, (-1, -1) off the screen: a reload. Its ioctls set the board's
// registers (0x40044d03: register << 8 | value; 0x20 has the coin meter,
// bit 4) and a frame count (0x40044d08).
//
// Input: the loader's JVS state (the SDL keyboard and mouse, or evdev), as
// for the Lindbergh gun games: player 1's gun on ANALOGUE_1/2, player 2's
// on ANALOGUE_3/4, BUTTON_1 the trigger, BUTTON_2 a reload (a shot off the
// screen, as firing at the edge of the screen).

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "teamplay.h"
#include "../hardware/lindbergh/jvs.h"

#define MJ_PACKET_SIZE 0xb8
#define MJ_IOCTL_REGISTER 0x40044d03
#define MJ_GUNS 2
#define MJ_SAMPLES 4

// Switches (the game's control test).
#define MJ_START_2 0x00040000
#define MJ_START_1 0x00080000
#define MJ_COIN_2 0x00100000
#define MJ_COIN_1 0x00200000
#define MJ_TEST 0x00800000
#define MJ_SERVICE 0x01000000
#define MJ_TRIGGER_1 0x04000000
#define MJ_TRIGGER_2 0x08000000

// A coin switch shows in the control test for this many packets.
#define COIN_SWITCH_PACKETS 6

typedef struct
{
    uint32_t flags;
    uint32_t switches;
    uint32_t unknown08;
    int32_t irqCount;
    uint32_t samples[MJ_GUNS][MJ_SAMPLES];
    uint32_t coins[2];
    uint8_t rest[MJ_PACKET_SIZE - 0x38];
} MjPacket;

_Static_assert(sizeof(MjPacket) == MJ_PACKET_SIZE, "MegaJamma packet size");

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int mjFd = -1;
static int trace;
static int packetPending;
static int32_t irqCount;
static int coinBase[2], coinBaseSet, coinCount[2];
static int coinSwitch[2], lastCoins[2];
static unsigned int lastTriggers[MJ_GUNS], lastReloads[MJ_GUNS];

int teamplayMjIsPath(const char *path)
{
    return isTeamplayGame() && path && !strcmp(path, "/dev/mjg");
}

int teamplayMjIsFd(int fd)
{
    return fd >= 0 && fd == mjFd;
}

int teamplayMjOpen(int (*realOpen)(const char *, int, ...))
{
    pthread_mutex_lock(&lock);
    trace = getenv("TEAMPLAY_MJ_TRACE") != NULL;
    if (mjFd < 0)
        mjFd = realOpen("/dev/null", O_RDWR);
    packetPending = 1;
    pthread_mutex_unlock(&lock);
    return mjFd;
}

void teamplayMjClose(void)
{
    pthread_mutex_lock(&lock);
    mjFd = -1;
    pthread_mutex_unlock(&lock);
}

// A gun's position as screen pixels of the game, or 0 when it is off the
// screen (at its edge, where light guns see what is outside).
static int gunPixel(const TeamplayGame *g, JVSIO *io, int gun, float *x, float *y)
{
    int max = io->analogueMax, ax = io->state.analogueChannel[ANALOGUE_1 + 2 * gun];
    int ay = io->state.analogueChannel[ANALOGUE_2 + 2 * gun];

    if (max <= 0 || ax <= 0 || ax >= max || ay <= 0 || ay >= max)
        return 0;
    *x = (float)ax / (max + 1) * g->width;
    *y = (float)ay / (max + 1) * g->height;
    return 1;
}

// The board's sample the game turns back into (x, y): its average truncates,
// so the value is rounded up.
static uint32_t gunSample(const TeamplayGame *g, float x, float y)
{
    int rx = (int)((x - g->xOffset) / g->xScale + 0.999f);
    int ry = (int)((y - g->yOffset) / g->yScale + 0.999f);

    rx = rx < 0 ? 0 : rx > 0xfff ? 0xfff : rx;
    ry = ry < 0 ? 0 : ry > 0xfff ? 0xfff : ry;
    return (uint32_t)rx | (uint32_t)ry << 12;
}

static void buildPacket(MjPacket *p)
{
    static const uint32_t starts[MJ_GUNS] = {MJ_START_1, MJ_START_2};
    static const uint32_t triggers[MJ_GUNS] = {MJ_TRIGGER_1, MJ_TRIGGER_2};
    static const uint32_t coinSwitches[2] = {MJ_COIN_1, MJ_COIN_2};
    const TeamplayGame *g = teamplayCurrentGame();
    JVSIO *io = getJVSIO();

    memset(p, 0, sizeof(*p));
    p->irqCount = ++irqCount;

    for (int c = 0; c < 2; c++)
    {
        if (!coinBaseSet)
            coinBase[c] = lastCoins[c] = io->state.coinCount[c];
        int coins = io->state.coinCount[c];
        if (coins > lastCoins[c])
            coinSwitch[c] = COIN_SWITCH_PACKETS;
        lastCoins[c] = coins;
        // The count only grows (the game stops when it goes down).
        if (coins - coinBase[c] > coinCount[c])
            coinCount[c] = coins - coinBase[c];
        p->coins[c] = (uint32_t)coinCount[c];
        if (coinSwitch[c] > 0)
        {
            coinSwitch[c]--;
            p->switches |= coinSwitches[c];
        }
    }
    coinBaseSet = 1;

    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        p->switches |= MJ_TEST;
    for (int gun = 0; gun < MJ_GUNS; gun++)
    {
        int player = PLAYER_1 + gun;
        uint32_t held = io->state.inputSwitch[player];
        float x, y;

        if (held & BUTTON_START)
            p->switches |= starts[gun];
        if (held & BUTTON_SERVICE)
            p->switches |= MJ_SERVICE;
        if (held & BUTTON_1)
            p->switches |= triggers[gun];

        // Presses since the last packet, however short.
        unsigned int fired = io->state.switchPresses[player][__builtin_ctz(BUTTON_1)];
        unsigned int reloaded = io->state.switchPresses[player][__builtin_ctz(BUTTON_2)];
        // Presses from before the board opened (the mouse outside the window
        // is a reload) are not shots.
        if (irqCount == 1)
            lastTriggers[gun] = fired, lastReloads[gun] = reloaded;
        int shot = fired != lastTriggers[gun];
        int reload = reloaded != lastReloads[gun];
        lastTriggers[gun] = fired;
        lastReloads[gun] = reloaded;

        int onScreen = !reload && gunPixel(g, io, gun, &x, &y);
        if (onScreen)
        {
            uint32_t sample = gunSample(g, x, y);
            for (int i = 0; i < MJ_SAMPLES; i++)
                p->samples[gun][i] = sample;
            p->flags |= (uint32_t)MJ_SAMPLES << (4 * gun);
        }
        if (shot || reload)
            p->flags |= 0x1000u << gun;
        if (trace && (shot || reload))
            fprintf(stderr, "Teamplay MegaJamma: gun %d %s at %s%.0f,%.0f\n", gun + 1, reload ? "reload" : "shot",
                    onScreen ? "" : "(off screen) ", onScreen ? x : -1.f, onScreen ? y : -1.f);
    }
}

// One packet a poll: the game reads until there is none.
ssize_t teamplayMjRead(void *buf, size_t count)
{
    ssize_t n = 0;

    pthread_mutex_lock(&lock);
    if (packetPending && count >= MJ_PACKET_SIZE)
    {
        buildPacket(buf);
        n = MJ_PACKET_SIZE;
    }
    packetPending = !packetPending;
    pthread_mutex_unlock(&lock);
    return n;
}

int teamplayMjIoctl(unsigned long request, void *arg)
{
    if (trace && request == MJ_IOCTL_REGISTER)
    {
        static uint8_t regs[256];
        unsigned int value = (unsigned int)(uintptr_t)arg;
        if (regs[(value >> 8) & 0xff] != (value & 0xff))
            fprintf(stderr, "Teamplay MegaJamma: register 0x%02x = 0x%02x\n", (value >> 8) & 0xff, value & 0xff);
        regs[(value >> 8) & 0xff] = value & 0xff;
    }
    return 0;
}
