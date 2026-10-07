// The N2 steering wheel's force feedback board (clKickback) on its own serial
// port, /dev/ttyM1: a port of the Pacloader fork's (n2Kickback.cpp, and the
// hooks of n2SteeringIo.cpp that drive it). The force goes to the
// host's wheel through namcoFfb.c.
//
// The game sends 10-byte frames (ff ff ...) and reads 3 ASCII characters
// back. Its board object's state (this+0x2c): 0 idle (send() transmits;
// waitOnPower ends there), 1 motor stopped (waitOffPower ends there), 3 a
// reply expected, 4 a latched error. So:
// - every frame is answered "E00", one reply, replacing (not queueing: the
//   game would read answers to requests minutes old), or send() counts to E20;
// - the reports the game waits for, power ("C01" on, "C06" off) and the
//   self-check's ("E00" then "C06"), are queued apart and go first;
// - a "C06" waits while the state is 0 with the self-check bit (this+0x30,
//   0x80) up: decordResultCode() refuses it then, as an "E20".
// - the test menu's I/F INITIALIZE screen (this+0x71 set) waits for position
//   reports ("H" and the wheel, 16 bits), one each time this+0x34 is clear.
//
// NAMCO_KB_TRACE set: the board's traffic on stderr.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "namcoN2.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define FRAME 10

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int kbFd = -1;
static int trace;
static unsigned char command[256], reply[3], volunteered[16];
static size_t commandLen, replyLen, volunteeredLen;
static uint8_t *const *instance; // clKickback::sm_instance

static const uint8_t *board(void)
{
    return instance ? *instance : NULL;
}

static int boardState(void)
{
    const uint8_t *b = board();
    return b ? *(const int *)(b + 0x2c) : -1;
}

static void volunteer(const char *reports, size_t n)
{
    memcpy(volunteered, reports, n);
    volunteeredLen = n;
}

// Whether the queued report can go now (see the "C06" rule above).
static int welcome(void)
{
    const uint8_t *b = board();
    if (volunteeredLen < 3 || memcmp(volunteered, "C06", 3) != 0 || !b)
        return 1;
    return !(*(const int *)(b + 0x2c) == 0 && (b[0x30] & 0x80));
}

// The test menu's wheel position report. Caller holds lock.
static void positionReport(void)
{
    const uint8_t *b = board();
    if (!b || !b[0x71] || b[0x34] || volunteeredLen)
        return;
    const JVSIO *io = getJVSIO();
    int max = io->analogueMax > 0 ? io->analogueMax : 1, raw = io->state.analogueChannel[ANALOGUE_1];
    raw = raw < 0 ? 0 : raw > max ? max : raw;
    unsigned position = (unsigned)((float)raw * 0xffff / max) & 0xffff;
    volunteered[0] = 'H';
    volunteered[1] = position >> 8;
    volunteered[2] = position;
    volunteeredLen = 3;
}

int namcoN2KickbackIsPath(const char *path)
{
    const NamcoN2Game *g = namcoN2CurrentGame();
    return g && g->kickbackDevice && path && strcmp(path, g->kickbackDevice) == 0;
}

int namcoN2KickbackOpen(int (*realOpen)(const char *, int, ...))
{
    pthread_mutex_lock(&lock);
    trace = getenv("NAMCO_KB_TRACE") != NULL;
    if (kbFd < 0)
        kbFd = realOpen("/dev/null", O_RDWR);
    commandLen = replyLen = volunteeredLen = 0;
    pthread_mutex_unlock(&lock);
    if (trace)
        fprintf(stderr, "Namco N2 kickback: opened [state %d]\n", boardState());
    return kbFd;
}

int namcoN2KickbackIsFd(int fd)
{
    return fd >= 0 && fd == kbFd;
}

void namcoN2KickbackClose(void)
{
    pthread_mutex_lock(&lock);
    commandLen = replyLen = volunteeredLen = 0;
    kbFd = -1;
    pthread_mutex_unlock(&lock);
}

ssize_t namcoN2KickbackWrite(const void *buf, size_t count)
{
    pthread_mutex_lock(&lock);
    if (commandLen + count > sizeof(command))
        commandLen = 0;
    if (count <= sizeof(command))
    {
        memcpy(command + commandLen, buf, count);
        commandLen += count;
    }
    for (;;)
    {
        size_t skip = 0;
        while (skip < commandLen && command[skip] != 0xff)
            skip++;
        memmove(command, command + skip, commandLen - skip);
        commandLen -= skip;
        if (commandLen < FRAME)
            break;
        memmove(command, command + FRAME, commandLen - FRAME);
        commandLen -= FRAME;
        memcpy(reply, "E00", 3);
        replyLen = 3;
    }
    pthread_mutex_unlock(&lock);
    return count;
}

ssize_t namcoN2KickbackRead(void *buf, size_t count)
{
    unsigned char *source;
    size_t *len, n;

    pthread_mutex_lock(&lock);
    positionReport();
    if (volunteeredLen && welcome())
    {
        source = volunteered;
        len = &volunteeredLen;
    }
    else
    {
        source = reply;
        len = &replyLen;
    }
    n = *len < count ? *len : count;
    memcpy(buf, source, n);
    memmove(source, source + n, *len - n);
    *len -= n;
    pthread_mutex_unlock(&lock);
    if (n == 0)
    {
        errno = EAGAIN;
        return -1;
    }
    if (trace && memcmp(buf, "E00", n < 3 ? n : 3) != 0)
        fprintf(stderr, "Namco N2 kickback: read %.*s [state %d]\n", (int)n, (const char *)buf, boardState());
    return n;
}

int namcoN2KickbackIoctl(unsigned long request, void *arg)
{
    if (request == FIONREAD)
    {
        pthread_mutex_lock(&lock);
        positionReport();
        *(int *)arg = volunteeredLen && welcome() ? 3 : (int)replyLen;
        pthread_mutex_unlock(&lock);
    }
    return 0;
}

static void report(const char *reports, size_t n, const char *why)
{
    pthread_mutex_lock(&lock);
    volunteer(reports, n);
    pthread_mutex_unlock(&lock);
    if (trace)
        fprintf(stderr, "Namco N2 kickback: %s, %.*s queued [state %d]\n", why, (int)n, reports, boardState());
}

// ---------------------------------------------------------------------------
// clKickback's power and self-check: the game's own, then the board's report.

typedef int (*Method)(void *);
static Method realOnPower, realOffPower, realWaitOnPower, realWaitOffPower, realRequestSelfCheck;

static int onPower(void *self)
{
    int r = realOnPower(self);
    report("C01", 3, "power on");
    return r;
}

static int offPower(void *self)
{
    int r = realOffPower(self);
    report("C06", 3, "power off");
    return r;
}

static int waitOnPower(void *self)
{
    report("C01", 3, "waiting for the power");
    return realWaitOnPower(self);
}

static int waitOffPower(void *self)
{
    report("C06", 3, "waiting for the power off");
    return realWaitOffPower(self);
}

static int requestSelfCheck(void *self)
{
    int r = realRequestSelfCheck(self);
    report("E00C06", 6, "self-check");
    return r;
}

void namcoN2KickbackInit(void)
{
    int n = 0;

    instance = namcoN2Symbol("_ZN10clKickback11sm_instanceE");
    n += namcoN2HookOriginal("_ZN10clKickback7onPowerEv", onPower, (void **)&realOnPower);
    n += namcoN2HookOriginal("_ZN10clKickback8offPowerEv", offPower, (void **)&realOffPower);
    n += namcoN2HookOriginal("_ZN10clKickback11waitOnPowerEv", waitOnPower, (void **)&realWaitOnPower);
    n += namcoN2HookOriginal("_ZN10clKickback12waitOffPowerEv", waitOffPower, (void **)&realWaitOffPower);
    n += namcoN2HookOriginal("_ZN10clKickback16requestSelfCheckEv", requestSelfCheck, (void **)&realRequestSelfCheck);
    log_info("Namco N2: steering board on %s, %d hooks%s", namcoN2CurrentGame()->kickbackDevice, n,
             instance ? "" : " (no clKickback::sm_instance)");
    namcoFfbStart(&namcoN2CurrentGame()->ffb, (void *const *)instance, "Namco N2");
}
