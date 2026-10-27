// The steering wheel's force feedback board ("STR PCB", clKickback), on a
// serial port of its own beside the JVS one: emulated here, as the I/O board
// is in namcoEs1.c. A port of the Pacloader fork's (es1Kickback.cpp and
// es1StrPcbStateMachine.cpp).
//
// The board speaks in 3-byte reports, which clKickback::decordResultCode()
// reads:
//   C01      the motor is powered
//   C06      the self-check is over (or the motor is off)
//   E00      a command is done, no error (E1x, E2x: the board's errors)
//   H hi lo  the wheel's position, 10 bits, 0x1ff centred
// The game's commands are 10 bytes (ff ff, the wheel's centre target, ...),
// each answered E00 (C06 while the motor is off); WMMT4's
// are framed (02 cmd len_hi len_lo payload sum 03) and answered in kind.
//
// A powered board reports on its own, and the game waits for it: Maximum
// Heat 3D sends nothing while a request is pending, and only reads. Its
// board is powered by a JVS general output (kickbackPowerGpo, "GOUT0 STR PCB
// POWER"): C01 on power on, then the position every 4 ms; its self-check
// request (requestSelfCheck) is answered H 01ff, E00, C06.
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
#include <time.h>

#include "namcoEs1.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define COMMAND_LENGTH 10
#define MAX_QUEUED 1024
#define FRAME_HEADER 0xff
#define FRAMED_START 0x02
#define FRAMED_END 0x03
#define FRAMED_OVERHEAD 6
// The position stream's period (the real board's, without flooding a game
// that polls faster).
#define WHEEL_PERIOD_MS 4
// The game stops polling the board in attract: a report left across that
// gap is not a valid answer to its first command after.
#define IDLE_RESYNC_MS 1000
// 'H' and a 10-bit angle, big-endian: centred is 0x1ff.
#define WHEEL_MAX 1023

typedef enum
{
    STR_CLOSED,
    STR_IDLE,
    STR_POWERING_ON,
    STR_SELF_CHECKING,
    STR_RUNNING,
} StrState;

typedef enum
{
    TRANSITION_NONE,
    TRANSITION_POWER_ON,
    TRANSITION_POWER_OFF,
    TRANSITION_SELF_CHECK,
    TRANSITION_POWERED_SELF_CHECK,
} Transition;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int kbFd = -1;
static int trace;
static unsigned char commandBytes[MAX_QUEUED], replyBytes[MAX_QUEUED];
static size_t commandLen, replyLen;
static unsigned long frames;
static uint64_t lastActivity;

// The board's state (StrPcbStateMachine).
static StrState state = STR_CLOSED;
static int reportsUnprompted, volunteersSelfCheck, initialPowerReportPending;
// A self-check asked for while the board is not powered (Maximum Heat 3D
// asks first, then powers it): reported once the power is on.
static int selfCheckPending;
static uint64_t nextWheelReport;

static const char *stateName(void)
{
    static const char *const names[] = {"closed", "idle", "powering-on", "self-checking", "running"};
    return names[state];
}

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void traceBytes(const char *what, const unsigned char *b, size_t n)
{
    if (!trace)
        return;
    fprintf(stderr, "Namco kickback: %s", what);
    for (size_t i = 0; i < n && i < 16; i++)
        fprintf(stderr, " %02x", b[i]);
    fprintf(stderr, " [%s]\n", stateName());
}

static void drop(unsigned char *q, size_t *len, size_t n)
{
    memmove(q, q + n, *len - n);
    *len -= n;
}

static void queueReply(const unsigned char *b, size_t n)
{
    if (replyLen + n > sizeof(replyBytes))
        return;
    memcpy(replyBytes + replyLen, b, n);
    replyLen += n;
}

// The wheel as the board reports it: the I/O board's steering (ANALOGUE_1, 0
// left .. analogueMax right) on 10 bits.
static unsigned wheelPosition(void)
{
    const JVSIO *io = getJVSIO();
    if (!io || io->analogueMax <= 0)
        return 0x1ff;
    int angle = io->state.analogueChannel[ANALOGUE_1] * WHEEL_MAX / io->analogueMax; // 16 bits at most
    return angle < 0 ? 0 : angle > WHEEL_MAX ? WHEEL_MAX : (unsigned)angle;
}

// At most one unprompted report per period: the pending power report, then
// the position while powered.
static int nextUnprompted(unsigned char report[3], uint64_t now)
{
    if (!reportsUnprompted || state == STR_CLOSED)
        return 0;
    if (initialPowerReportPending)
    {
        memcpy(report, "C01", 3);
        initialPowerReportPending = 0;
        state = STR_POWERING_ON;
        nextWheelReport = now + WHEEL_PERIOD_MS;
        return 1;
    }
    if (state == STR_IDLE || state == STR_SELF_CHECKING)
        return 0;
    if (nextWheelReport && now < nextWheelReport)
        return 0;
    unsigned position = wheelPosition();
    report[0] = 'H';
    report[1] = position >> 8;
    report[2] = position;
    nextWheelReport = now + WHEEL_PERIOD_MS;
    return 1;
}

static void resynchronize(uint64_t now)
{
    if (state == STR_RUNNING || state == STR_POWERING_ON)
        nextWheelReport = now;
}

// After the game has stopped polling for a while: a stale position report
// and a half-written command are dropped. Caller holds lock.
static void resynchronizeAfterIdle(uint64_t now)
{
    if (lastActivity && now - lastActivity >= IDLE_RESYNC_MS)
    {
        if (replyLen == 3 && replyBytes[0] == 'H')
            replyLen = 0;
        commandLen = 0;
        resynchronize(now);
        if (trace)
            fprintf(stderr, "Namco kickback: idle %llums, resynchronized [%s]\n",
                    (unsigned long long)(now - lastActivity), stateName());
    }
    lastActivity = now;
}

// Caller holds lock.
static void offerState(void)
{
    uint64_t now = nowMs();
    resynchronizeAfterIdle(now);
    if (replyLen || !reportsUnprompted)
        return;
    unsigned char report[3];
    if (nextUnprompted(report, now))
        queueReply(report, 3);
}

// A powered board is already streaming: a read is not left empty for the
// rest of a period (a long silence is the game's E20 / E2212).
static void ensureStateReport(void)
{
    offerState();
    if (replyLen || !reportsUnprompted)
        return;
    resynchronize(nowMs());
    offerState();
}

static void queueReports(const unsigned char *reports, size_t n, Transition transition)
{
    pthread_mutex_lock(&lock);
    if (kbFd < 0)
    {
        pthread_mutex_unlock(&lock);
        return;
    }
    replyLen = 0;
    queueReply(reports, n);
    initialPowerReportPending = 0;
    nextWheelReport = 0;
    switch (transition)
    {
    case TRANSITION_POWER_ON:
        state = STR_POWERING_ON;
        break;
    case TRANSITION_POWER_OFF:
        state = STR_IDLE;
        break;
    case TRANSITION_SELF_CHECK:
        state = STR_SELF_CHECKING;
        break;
    case TRANSITION_POWERED_SELF_CHECK:
        state = STR_RUNNING;
        break;
    case TRANSITION_NONE:
        break;
    }
    traceBytes("queued", reports, n);
    pthread_mutex_unlock(&lock);
}

// WMMT4's framed commands: the reply echoes the command with its payload
// size.
static int framedReplyLength(unsigned char command)
{
    // Indexed from '0', as the game's own table is.
    static const int lengths[] = {1, 16, 4, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 24};
    int i = command - '0';
    return i < 0 || i >= (int)(sizeof(lengths) / sizeof(lengths[0])) ? 0 : lengths[i];
}

// One framed command answered; 0 when more bytes are needed.
static int consumeFramedCommand(void)
{
    if (commandLen < FRAMED_OVERHEAD)
        return 0;
    size_t payload = ((size_t)commandBytes[2] << 8) | commandBytes[3];
    size_t total = payload + FRAMED_OVERHEAD;
    if (payload > MAX_QUEUED || commandLen < total)
        return 0;
    unsigned char command = commandBytes[1];
    traceBytes("framed", commandBytes, total);
    drop(commandBytes, &commandLen, total);

    // The answer goes before an unprompted position, which would shift it.
    if (replyLen == 3 && replyBytes[0] == 'H')
        replyLen = 0;
    int replyPayload = framedReplyLength(command);
    if (replyPayload <= 0)
        return 1;
    unsigned char reply[FRAMED_OVERHEAD + 32] = {FRAMED_START, command, replyPayload >> 8, replyPayload};
    unsigned char sum = 0;
    for (int i = 1; i < 4 + replyPayload; i++)
        sum += reply[i];
    reply[4 + replyPayload] = sum;
    reply[5 + replyPayload] = FRAMED_END;
    queueReply(reply, replyPayload + FRAMED_OVERHEAD);
    return 1;
}

// Caller holds lock.
static void consumeCommands(void)
{
    for (;;)
    {
        size_t skip = 0;
        while (skip < commandLen && commandBytes[skip] != FRAME_HEADER && commandBytes[skip] != FRAMED_START)
            skip++;
        drop(commandBytes, &commandLen, skip);

        if (commandLen && commandBytes[0] == FRAMED_START)
        {
            if (!consumeFramedCommand())
                return;
            continue;
        }
        if (commandLen < COMMAND_LENGTH)
            return;
        frames++;
        traceBytes("command", commandBytes, COMMAND_LENGTH);
        drop(commandBytes, &commandLen, COMMAND_LENGTH);
        // clKickback::receive() reads exactly three bytes. A self-check's
        // reports (its E00 among them) are not cut short by the command
        // the game sends once the board is powered.
        if (state == STR_SELF_CHECKING && replyLen)
            continue;
        // With its motor off (GOUT0 off) the board answers that it is off,
        // C06, and E00 only when powered: E00 is "on, ready" to the game
        // (state 0), and preTestMode's waitOffPower() spins until it hears
        // the board is off (the fork answers E00 whatever the power).
        replyLen = 0;
        queueReply((const unsigned char *)(state == STR_IDLE ? "C06" : "E00"), 3);
    }
}

int namcoEs1KickbackIsPath(const char *path)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    return g && g->kickbackDevice && path && strcmp(path, g->kickbackDevice) == 0;
}

int namcoEs1KickbackOpen(int (*realOpen)(const char *, int, ...))
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();

    pthread_mutex_lock(&lock);
    trace = getenv("NAMCO_KB_TRACE") != NULL;
    if (kbFd < 0)
        kbFd = realOpen("/dev/null", O_RDWR);
    commandLen = replyLen = 0;
    frames = 0;
    reportsUnprompted = g->kickbackReportsUnprompted;
    volunteersSelfCheck = g->kickbackVolunteersSelfCheck;
    initialPowerReportPending = reportsUnprompted && volunteersSelfCheck;
    nextWheelReport = 0;
    state = STR_IDLE;
    lastActivity = nowMs();
    pthread_mutex_unlock(&lock);
    if (trace)
        fprintf(stderr, "Namco kickback: %s opened\n", g->kickbackDevice);
    log_info("Namco: the steering board (%s) is the loader's", g->kickbackDevice);
    return kbFd;
}

int namcoEs1KickbackIsFd(int fd)
{
    return fd >= 0 && fd == kbFd;
}

void namcoEs1KickbackClose(void)
{
    pthread_mutex_lock(&lock);
    commandLen = replyLen = 0;
    state = STR_CLOSED;
    initialPowerReportPending = 0;
    nextWheelReport = 0;
    lastActivity = 0;
    kbFd = -1;
    pthread_mutex_unlock(&lock);
}

ssize_t namcoEs1KickbackWrite(const void *buf, size_t count)
{
    pthread_mutex_lock(&lock);
    resynchronizeAfterIdle(nowMs());
    if (commandLen + count > sizeof(commandBytes))
        commandLen = 0;
    if (count <= sizeof(commandBytes))
    {
        memcpy(commandBytes + commandLen, buf, count);
        commandLen += count;
    }
    consumeCommands();
    pthread_mutex_unlock(&lock);
    return count;
}

ssize_t namcoEs1KickbackRead(void *buf, size_t count)
{
    if (count == 0)
        return 0;
    pthread_mutex_lock(&lock);
    ensureStateReport();
    size_t n = replyLen < count ? replyLen : count;
    memcpy(buf, replyBytes, n);
    drop(replyBytes, &replyLen, n);
    // The self-check is over once its reports are read.
    if (n && !replyLen && state == STR_SELF_CHECKING)
    {
        state = STR_RUNNING;
        nextWheelReport = 0;
    }
    pthread_mutex_unlock(&lock);
    if (n == 0)
    {
        errno = EAGAIN;
        return -1;
    }
    if (((const unsigned char *)buf)[0] != 'H')
        traceBytes("read", buf, n);
    return n;
}

int namcoEs1KickbackIoctl(unsigned long request, void *arg)
{
    if (request == FIONREAD)
    {
        pthread_mutex_lock(&lock);
        ensureStateReport();
        *(int *)arg = replyLen;
        pthread_mutex_unlock(&lock);
    }
    return 0;
}

// The self-check's reports: centred, no error, done (in that order, over
// separate reads).
static const unsigned char selfCheckReports[] = {'H', 0x01, 0xff, 'E', '0', '0', 'C', '0', '6'};

// The board's power, switched by the game: C01 when it comes on (then the
// self-check asked for before, if any), C06 when it goes off.
void namcoEs1KickbackReportMotorPower(int running)
{
    if (running && selfCheckPending)
    {
        unsigned char reports[3 + sizeof(selfCheckReports)] = {'C', '0', '1'};
        memcpy(reports + 3, selfCheckReports, sizeof(selfCheckReports));
        selfCheckPending = 0;
        queueReports(reports, sizeof(reports), TRANSITION_SELF_CHECK);
        return;
    }
    queueReports((const unsigned char *)(running ? "C01" : "C06"), 3,
                 running ? TRANSITION_POWER_ON : TRANSITION_POWER_OFF);
}

// The game asked for a self-check: reported now if the board is powered,
// else once it is.
void namcoEs1KickbackReportSelfCheck(void)
{
    if (state == STR_IDLE || state == STR_CLOSED)
    {
        selfCheckPending = 1;
        if (trace)
            fprintf(stderr, "Namco kickback: self-check asked for, unpowered [%s]\n", stateName());
        return;
    }
    queueReports(selfCheckReports, sizeof(selfCheckReports), TRANSITION_SELF_CHECK);
}

// Powered, with the self-check's result: for a board the game never asks.
void namcoEs1KickbackReportPoweredSelfCheck(void)
{
    queueReports((const unsigned char *)"C01E00", 6, TRANSITION_POWERED_SELF_CHECK);
}

// The general output that powers the board (kickbackPowerGpo: output byte
// 0, this bit), as the game switches it over JVS.
static void powerOutput(unsigned char index, unsigned char data)
{
    static int running = -1;
    int on = (data & namcoEs1CurrentGame()->kickbackPowerGpo) != 0;

    if (index != 0 || on == running)
        return;
    running = on;
    namcoEs1KickbackReportMotorPower(on);
}

void namcoEs1KickbackInit(const NamcoEs1Game *g)
{
    if (g->kickbackDevice && g->kickbackPowerGpo)
        setJVSGpoHandler(powerOutput);
    if (g->ffb.instance)
        namcoFfbStart(&g->ffb, (void *const *)(uintptr_t)g->ffb.instance, "Namco");
}
