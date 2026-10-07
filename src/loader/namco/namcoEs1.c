// Namco ES1 support: game detection, init, the JVS I/O board on its serial
// port, the HASP dongle and the cabinet's administration. See namcoEs1.h.

#include <cpuid.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#include <glad/gl.h>

#include "namcoEs1.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"
#include "../rawthrills/rawthrills.h"
#include "../common/desktopInput.h"
#include "../common/importHook.h"
#include "../common/webcam.h"
#include "../graphics/frameScale.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <GL/glx.h>

extern uint32_t gId;

static const NamcoEs1Game *game;

const NamcoEs1Game *namcoEs1CurrentGame(void)
{
    static uint32_t cachedId;
    if (cachedId != gId)
    {
        cachedId = gId;
        game = namcoEs1GetGame(gId);
    }
    return game;
}

int isNamcoEs1Game(void)
{
    return namcoEs1CurrentGame() != NULL;
}

// NAMCO_JVS_TRACE set: the board's traffic on stderr.
static int trace;

// Per-thread call notes (diagnostic): which thread drives the JVS path, and
// how. First 30 calls per (thread, kind) are logged, then one "(more)".
static const char *const noteKind[] = {"write", "read", "fionread", "tiocmget"};
static long noteSeen[64][4];
static int noteCount[64][4];

static void note(long tid, int kind)
{
    int s = (int)(tid & 63);
    if (!trace)
        return;
    if (noteSeen[s][kind] != tid)
    {
        noteSeen[s][kind] = tid;
        noteCount[s][kind] = 0;
    }
    if (noteCount[s][kind] == 31)
        return;
    if (++noteCount[s][kind] <= 31)
        fprintf(stderr, "Namco JVS %s%s tid=%d\n", noteKind[kind], noteCount[s][kind] == 31 ? " (more)" : "", (int)tid);
}

static void traceBytes(const char *what, const unsigned char *data, size_t size)
{
    if (!trace)
        return;
    fprintf(stderr, "Namco JVS %s:", what);
    for (size_t i = 0; i < size; i++)
        fprintf(stderr, " %02x", data[i]);
    fprintf(stderr, "\n");
}

// ---------------------------------------------------------------------------
// JVS I/O board. The game writes whole packets (SYNC, node, length, data,
// checksum, escaped) and polls for the replies (FIONREAD, then read). Each
// packet goes to the loader's JVS emulation (hardware/lindbergh/jvs.c) as it
// completes, and its reply is queued for reading. A board does not answer a
// bus reset. The sense line (CTS, TIOCMGET) says a board is addressed and
// is the last one: a single board.

static pthread_mutex_t jvsLock = PTHREAD_MUTEX_INITIALIZER;
extern pthread_mutex_t jvsMutex; // the JVS state's (hardware/lindbergh/jvs.c)
static int jvsFd = -1;
static unsigned char rx[1024], tx[1024];
static size_t rxLen, txLen, txPos;

int namcoEs1JvsIsPath(const char *path)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    if (g && g->jvsDeviceAlias && path && strcmp(path, g->jvsDeviceAlias) == 0)
        return 1;
    return g && g->jvsDevice && path && strcmp(path, g->jvsDevice) == 0;
}

int namcoEs1JvsOpen(int (*realOpen)(const char *, int, ...))
{
    pthread_mutex_lock(&jvsLock);
    if (jvsFd < 0)
        jvsFd = realOpen("/dev/null", O_RDWR);
    rxLen = txLen = txPos = 0;
    pthread_mutex_unlock(&jvsLock);
    return jvsFd;
}

int namcoEs1JvsIsFd(int fd)
{
    return fd >= 0 && fd == jvsFd;
}

void namcoEs1JvsClose(void)
{
    jvsFd = -1;
}

// Raw length of the packet at the start of p (p[0] is SYNC): 0 if it is not
// complete yet, -n if a SYNC at n starts another one first. *command: its
// first data byte.
static long packetLength(const unsigned char *p, size_t n, int *command)
{
    size_t i = 1;
    int decoded = 0, need = 2;

    while (i < n)
    {
        int c = p[i];
        if (c == SYNC)
            return -(long)i;
        if (c == ESCAPE)
        {
            if (i + 1 >= n)
                return 0;
            c = p[i + 1] + 1;
            i += 2;
        }
        else
        {
            i++;
        }
        if (decoded == 1)
            need = 2 + c;
        else if (decoded == 2)
            *command = c;
        if (++decoded == need && decoded > 2)
            return i;
    }
    return 0;
}

// The test switch's latched state (see latchTest).
static int testOn;

// The driving games without evdev input (Nirin, the Dead Heat games, every
// game the loader calibrates): the arrows are a joystick, not
// switches (but in the test menu, which is driven by the up/down switches,
// and where F1, the service key, is its ENTER SW, P1 button 1). Left/right steer, reaching full lock in a quarter of a second
// and coming back to the centre as fast; up accelerates, down brakes (and
// presses the brake switch of a game whose brake is one).
static float approach(float value, float target, float step)
{
    return value < target ? (value + step < target ? value + step : target)
                          : (value - step > target ? value - step : target);
}

// keys: the keyboard's player 1 switches; merge: a gamepad drives too
// (evdev input), whose analog inputs are only overridden while the keyboard
// moves them.
static void keyboardAnalogFrom(const NamcoEs1Game *g, JVSIO *io, int keys, int merge)
{
    static float steer, gas, brake;
    static struct timespec last;
    struct timespec now;
    int p1 = testOn ? 0 : keys;

    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = last.tv_sec ? (now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9f : 0;
    last = now;
    if (dt > 0.1f)
        dt = 0.1f;
    steer = approach(steer, (p1 & BUTTON_RIGHT ? 1.f : 0.f) - (p1 & BUTTON_LEFT ? 1.f : 0.f), dt * 4);
    gas = approach(gas, p1 & BUTTON_UP ? 1.f : 0.f, dt * 6);
    brake = approach(brake, p1 & BUTTON_DOWN ? 1.f : 0.f, dt * 6);
    if (!testOn)
        io->state.inputSwitch[PLAYER_1] &= ~(keys & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN));
    else if (keys & BUTTON_SERVICE)
        io->state.inputSwitch[PLAYER_1] = (io->state.inputSwitch[PLAYER_1] & ~BUTTON_SERVICE) | BUTTON_1;
    if (g && g->brakeSwitch && (p1 & BUTTON_DOWN))
        io->state.inputSwitch[PLAYER_1] |= g->brakeSwitch;
    if (merge && !steer && !gas && !brake)
        return;

    pthread_mutex_lock(&jvsMutex);
    io->state.analogueChannel[ANALOGUE_1] = 0x8000 + (int)(steer * 0x7fff);
    io->state.analogueChannel[ANALOGUE_2] = (int)(gas * 0xffff);
    io->state.analogueChannel[ANALOGUE_3] = (int)(brake * 0xffff);
    pthread_mutex_unlock(&jvsMutex);
}

static void keyboardAnalog(const NamcoEs1Game *g, JVSIO *io)
{
    keyboardAnalogFrom(g, io, io->state.inputSwitch[PLAYER_1], 0);
}

// Without evdev input: the desktop keys on the switches (1 start, 5 coin, F1
// service, F2 test, arrows, space), and the analog inputs at rest, so that
// only the game's own SDL joystick drives.
static void desktopInput(void)
{
    static int seenCoins[2];
    JVSIO *io = getJVSIO(), *keys = desktopInputState();

    for (int p = SYSTEM; p <= PLAYER_2; p++)
        io->state.inputSwitch[p] = keys->state.inputSwitch[p];
    for (int c = 0; c < 2; c++)
    {
        if (keys->state.coinCount[c] > seenCoins[c])
            io->state.coinCount[c] += keys->state.coinCount[c] - seenCoins[c];
        seenCoins[c] = keys->state.coinCount[c];
    }
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    if (g && (g->calibration || g->jammaCalibration || g->axisCalibration || g->keyboardAnalog))
    {
        keyboardAnalog(g, io);
        return;
    }
    // A game the loader does not calibrate sees its JVS analog inputs at 0,
    // as unconnected inputs are on a cabinet (a "handle centred" rest is only
    // meaningful with the game's own rest values to map onto).
    pthread_mutex_lock(&jvsMutex);
    io->state.analogueChannel[ANALOGUE_1] = 0;
    io->state.analogueChannel[ANALOGUE_2] = io->state.analogueChannel[ANALOGUE_3] = 0;
    pthread_mutex_unlock(&jvsMutex);
}

// The game's calibration of its controls (clHandleSetting): raw values of the
// accelerator, brake and handle (whose left is usually the higher value).
typedef struct
{
    int32_t accelMax, accelRest;
    int32_t brakeMax, brakeRest;
    int32_t handleRight, handleLeft, handleCenter;
} NamcoEs1Calibration;

static int clampRaw(float v)
{
    return v < 0 ? 0 : v > 0xffff ? 0xffff : (int)v;
}

// The JAMMA calibration the game starts with (its built-in defaults, before
// its test-mode settings are applied), and the statics' spacing.
static int32_t jammaDefaults[9];

static size_t jammaStep(const NamcoEs1Game *g)
{
    return (g->jammaCalibrationStride ? g->jammaCalibrationStride : 16) / 4;
}

static void saveJammaDefaults(const NamcoEs1Game *g)
{
    const int32_t *s = (const int32_t *)(uintptr_t)g->jammaCalibration;
    for (size_t i = 0; s && i < 9; i++)
        jammaDefaults[i] = s[i * jammaStep(g)];
}

// A cabinet never calibrated in its test menu (Dead Heat's dump) has all
// zeros in its settings, which the game applies: a handle and pedals
// without a range. The built-in defaults are put back then.
static void uncalibratedToDefaults(const NamcoEs1Game *g)
{
    volatile int32_t *s = (volatile int32_t *)(uintptr_t)g->jammaCalibration;
    const size_t step = jammaStep(g);
    if (s[step] != s[2 * step] && s[5 * step] != s[6 * step])
        return;
    for (size_t i = 0; i < 9; i++)
        s[i * step] = jammaDefaults[i];
}

// Tank! Tank! Tank!: the loader's steering (channel 1, 0x8000 centred), gas
// and brake (channels 2 and 3) put through the inverse of the game's
// calibration of its axes, onto its channels: steering, left pedal (the
// gas), right pedal (the brake: as played, 2026-10-26). An axis with no range on a side (the
// steering's, captured with the steering at an end when the settings were
// reset) gets the symmetric calibration of the game's other axes.
static void axisCalibrate(const NamcoEs1Game *g)
{
    int *ch = getJVSIO()->state.analogueChannel;
    float want[3] = {
        0.5f + (ch[ANALOGUE_1] - 0x8000) / 65534.f, // steering, 0..1
        ch[ANALOGUE_2] / 65535.f,                    // left pedal
        ch[ANALOGUE_3] / 65535.f,                    // right pedal
    };

    for (int c = 0; c < 3; c++)
    {
        int axis = ((const int32_t *)(uintptr_t)g->axisIndex)[c];
        volatile float *p = (volatile float *)(uintptr_t)(g->axisCalibration + axis * g->axisStride);
        int inverted = ((const uint8_t *)(uintptr_t)g->axisInvert)[c];
        float o = want[c], x;

        if (!(p[0] < p[1] && p[1] <= p[2] && p[2] < p[3]))
        {
            p[0] = 0.125f;
            p[1] = 0.458333f;
            p[2] = 0.541667f;
            p[3] = 0.875f;
        }
        if (o <= p[4])
            x = p[0];
        else if (o < p[5])
            x = p[0] + (o - p[4]) * (p[1] - p[0]) / (p[5] - p[4]);
        else if (o == p[5])
            x = (p[1] + p[2]) / 2;
        else if (o < p[6])
            x = p[2] + (o - p[5]) * (p[3] - p[2]) / (p[6] - p[5]);
        else
            x = p[3];
        ch[c] = clampRaw((inverted ? 1.f - x : x) * 65536.f);
    }
}

// The loader's analog inputs (channel 1 steering, 2 accelerator, 3 brake) in
// the calibration's range; the values given back are the loader's, to put
// back after the packet.
static void calibrate(const NamcoEs1Game *g, int saved[3])
{
    NamcoEs1Calibration jamma;
    int *ch = getJVSIO()->state.analogueChannel;

    for (int i = 0; i < 3; i++)
        saved[i] = ch[i];
    if (!g)
        return;
    const volatile NamcoEs1Calibration *c = (const volatile NamcoEs1Calibration *)(uintptr_t)g->calibration;
    if (g->axisCalibration)
    {
        axisCalibrate(g);
        return;
    }
    if (g->jammaCalibration)
    {
        const size_t step = jammaStep(g);
        uncalibratedToDefaults(g);
        const volatile int32_t *s = (const volatile int32_t *)(uintptr_t)g->jammaCalibration;
        jamma = (NamcoEs1Calibration){
            .accelMax = s[6 * step], .accelRest = s[5 * step],
            .brakeMax = step == 4 ? s[8 * step] : s[7] + s[8], .brakeRest = s[7 * step],
            .handleRight = s[2 * step], .handleLeft = s[step], .handleCenter = s[0],
        };
        c = &jamma;
    }
    if (!c)
        return;
    float steer = (ch[ANALOGUE_1] - 0x8000) / 32767.f;
    ch[ANALOGUE_1] = clampRaw(c->handleCenter + (steer < 0 ? -steer * (c->handleLeft - c->handleCenter)
                                                           : steer * (c->handleRight - c->handleCenter)));
    ch[ANALOGUE_2] = clampRaw(c->accelRest + ch[ANALOGUE_2] / 65535.f * (c->accelMax - c->accelRest));
    ch[ANALOGUE_3] = clampRaw(c->brakeRest + ch[ANALOGUE_3] / 65535.f * (c->brakeMax - c->brakeRest));
}

// The loader's values back, unless the input changed them meanwhile.
static void uncalibrate(const int saved[3], const int sent[3])
{
    int *ch = getJVSIO()->state.analogueChannel;
    for (int i = 0; i < 3; i++)
        if (ch[i] == sent[i])
            ch[i] = saved[i];
}

// The cabinet's test switch is a toggle (the game stays in its test menu
// while it is on), the key or button standing for it a push button: each
// press turns it over. The game sees the latched state; the key's own is
// put back after the packet, as the analog inputs are.
static int latchTest(void)
{
    static int held;
    int *system = &getJVSIO()->state.inputSwitch[SYSTEM];
    int pressed = (*system & BUTTON_TEST) != 0;

    if (pressed && !held)
        testOn = !testOn;
    held = pressed;
    *system = testOn ? *system | BUTTON_TEST : *system & ~BUTTON_TEST;
    return pressed;
}

static void unlatchTest(int pressed)
{
    int *system = &getJVSIO()->state.inputSwitch[SYSTEM];
    *system = pressed ? *system | BUTTON_TEST : *system & ~BUTTON_TEST;
}

// With evdev input (a gamepad or wheel), the keyboard works too: for each
// reply its switches are added to the pad's and its arrows drive while they
// are used; the pad's state is put back after the reply (saved here), so the
// keyboard never overwrites it. Its coins are counted in.
static void keyboardOverPad(JVSIO *io, int savedSwitches[3], int savedAnalog[3])
{
    static int seenCoins[2];
    JVSIO *keys = desktopInputState();

    pthread_mutex_lock(&jvsMutex);
    memcpy(savedSwitches, io->state.inputSwitch, 3 * sizeof(int));
    memcpy(savedAnalog, io->state.analogueChannel, 3 * sizeof(int));
    for (int p = SYSTEM; p <= PLAYER_2; p++)
        io->state.inputSwitch[p] |= keys->state.inputSwitch[p];
    for (int c = 0; c < 2; c++)
    {
        if (keys->state.coinCount[c] > seenCoins[c])
            io->state.coinCount[c] += keys->state.coinCount[c] - seenCoins[c];
        seenCoins[c] = keys->state.coinCount[c];
    }
    pthread_mutex_unlock(&jvsMutex);
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    if (g && (g->calibration || g->jammaCalibration || g->axisCalibration || g->keyboardAnalog))
        keyboardAnalogFrom(g, io, keys->state.inputSwitch[PLAYER_1], 1);
}

static void jvsPacket(const unsigned char *packet, size_t size, int command)
{
    int replySize = 0;

    traceBytes("<-", packet, size);
    if (size > JVS_MAX_PACKET_SIZE)
        return;
    int evdev = getConfig()->inputMode == 2, savedSwitches[3], savedAnalog[3];
    JVSIO *io = getJVSIO();
    if (!evdev)
        desktopInput();
    else
        keyboardOverPad(io, savedSwitches, savedAnalog);
    int saved[3], sent[3];
    pthread_mutex_lock(&jvsMutex);
    const NamcoEs1Game *game = namcoEs1CurrentGame();
    int panelSaved = io->state.inputSwitch[PLAYER_1];
    if (game && game->panel)
        game->panel(io);
    calibrate(game, saved);
    memcpy(sent, getJVSIO()->state.analogueChannel, sizeof(sent));
    int testPressed = latchTest();
    pthread_mutex_unlock(&jvsMutex);
    memcpy(inputBuffer, packet, size);
    JVSStatus status = processPacket(&replySize);
    pthread_mutex_lock(&jvsMutex);
    uncalibrate(saved, sent);
    unlatchTest(testPressed);
    if (game && game->panel)
        io->state.inputSwitch[PLAYER_1] = panelSaved;
    if (evdev)
    {
        memcpy(io->state.inputSwitch, savedSwitches, sizeof(savedSwitches));
        memcpy(io->state.analogueChannel, savedAnalog, sizeof(savedAnalog));
    }
    pthread_mutex_unlock(&jvsMutex);
    if (command == CMD_RESET)
    {
        txLen = txPos = 0;
        return;
    }
    if (status != JVS_STATUS_SUCCESS || replySize <= 0 || txLen + replySize > sizeof(tx))
        return;
    traceBytes("->", outputBuffer, replySize);
    memcpy(tx + txLen, outputBuffer, replySize);
    txLen += replySize;
}

ssize_t namcoEs1JvsWrite(const void *buf, size_t count)
{
    note(syscall(SYS_gettid), 0);
    pthread_mutex_lock(&jvsLock);
    if (rxLen + count > sizeof(rx))
        rxLen = 0;
    if (count <= sizeof(rx))
    {
        memcpy(rx + rxLen, buf, count);
        rxLen += count;
    }
    for (;;)
    {
        size_t start = 0;
        while (start < rxLen && rx[start] != SYNC)
            start++;
        memmove(rx, rx + start, rxLen - start);
        rxLen -= start;
        if (rxLen == 0)
            break;

        int command = -1;
        long len = packetLength(rx, rxLen, &command);
        if (len == 0)
            break;
        if (len > 0)
            jvsPacket(rx, len, command);
        else
            len = -len;
        memmove(rx, rx + len, rxLen - len);
        rxLen -= len;
    }
    pthread_mutex_unlock(&jvsLock);
    return count;
}

ssize_t namcoEs1JvsRead(void *buf, size_t count)
{
    note(syscall(SYS_gettid), 1);
    pthread_mutex_lock(&jvsLock);
    size_t n = txLen - txPos < count ? txLen - txPos : count;
    memcpy(buf, tx + txPos, n);
    txPos += n;
    if (txPos == txLen)
        txLen = txPos = 0;
    pthread_mutex_unlock(&jvsLock);
    if (n == 0)
    {
        errno = EAGAIN;
        return -1;
    }
    return n;
}

int namcoEs1JvsIoctl(unsigned long request, void *arg)
{
    switch (request)
    {
    case FIONREAD:
        note(syscall(SYS_gettid), 2);
        pthread_mutex_lock(&jvsLock);
        *(int *)arg = txLen - txPos;
        pthread_mutex_unlock(&jvsLock);
        return 0;
    case TIOCMGET:
        note(syscall(SYS_gettid), 3);
        *(int *)arg = TIOCM_CTS | TIOCM_DSR;
        return 0;
    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// HASP dongle: the game only reads its serial number from it (clHASP::check).

#define HASP_STATUS_OK 0
#define HASP_FEATURE_NOT_FOUND 31

static int haspLogin(uint32_t feature, const void *vendorCode, uint32_t *handle)
{
    (void)vendorCode;
    if (feature != game->haspFeature)
        return HASP_FEATURE_NOT_FOUND;
    *handle = 1;
    return HASP_STATUS_OK;
}

static int haspLogout(uint32_t handle)
{
    (void)handle;
    return HASP_STATUS_OK;
}

// The serial number, stored in the clear: hasp_decrypt leaves it as it is.
static int haspRead(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, void *buffer)
{
    size_t n = strlen(game->dongleSerial);
    (void)handle;
    (void)fileId;
    (void)offset;
    memset(buffer, 0, length);
    memcpy(buffer, game->dongleSerial, n < length ? n : length);
    return HASP_STATUS_OK;
}

static int haspDecrypt(uint32_t handle, void *buffer, uint32_t length)
{
    (void)handle;
    (void)buffer;
    (void)length;
    return HASP_STATUS_OK;
}

// Gundam's: a block with a checksum (see NamcoEs1Game.dongleBlockChecksum).
static int haspReadBlock(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, void *buffer)
{
    uint8_t *block = buffer, sum = 0;

    haspRead(handle, fileId, offset, length, buffer);
    if (length < 2)
        return HASP_STATUS_OK;
    for (uint32_t i = 0; i < length - 2; i++)
        sum += block[i];
    block[length - 2] = sum;
    block[length - 1] = (uint8_t)~sum;
    return HASP_STATUS_OK;
}

#define HASP_ID 0x1234567

static int haspGetSessionInfo(uint32_t handle, const char *format, char **info)
{
    (void)handle;
    (void)format;
    if (!info)
        return HASP_FEATURE_NOT_FOUND;
    // Both formats' answers in one: the game looks for the tag it wants.
    if (asprintf(info,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" ?><hasp_info><feature><featureid>%u</featureid>"
                 "</feature><keyinfo><haspid>%u</haspid></keyinfo></hasp_info>",
                 game->haspFeature == 0xffff0000 ? 0 : game->haspFeature, HASP_ID) < 0)
        return HASP_FEATURE_NOT_FOUND;
    return HASP_STATUS_OK;
}

static void haspFree(void *info)
{
    free(info);
}

static int haspGetSize(uint32_t handle, uint32_t fileId, uint32_t *size)
{
    (void)handle;
    (void)fileId;
    *size = game->haspMemorySize;
    return HASP_STATUS_OK;
}

// Replace n bytes of code at address, if they are the expected ones.
static void patchCode(uint32_t address, const uint8_t *expected, const uint8_t *replacement, size_t n)
{
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (!address)
        return;
    if (memcmp((void *)(uintptr_t)address, expected, n) != 0)
    {
        log_error("Namco: unexpected code at %#x, not patched", address);
        return;
    }
    if (mprotect((void *)page, (address + n - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        log_error("Namco: cannot make %#x writable", address);
        return;
    }
    memcpy((void *)(uintptr_t)address, replacement, n);
}

// Overwrite a function's entry with a jmp to the replacement.
static void detour(uint32_t address, void *replacement)
{
    uint8_t *target = (uint8_t *)(uintptr_t)address;
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (!address)
        return;
    if (mprotect((void *)page, (address + 5 - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        log_error("Namco: cannot make %#x writable", address);
        return;
    }
    target[0] = 0xe9;
    *(int32_t *)(target + 1) = (int32_t)((uint8_t *)replacement - (target + 5));
}

static void detourHaspHL(const NamcoEs1Game *g)
{
    detour(g->haspLogin, haspLogin);
    detour(g->haspLogout, haspLogout);
    detour(g->haspRead, g->dongleBlockChecksum ? (void *)haspReadBlock : (void *)haspRead);
    detour(g->haspDecrypt, haspDecrypt);
    detour(g->haspGetSessionInfo, haspGetSessionInfo);
    detour(g->haspFree, haspFree);
    detour(g->haspGetSize, haspGetSize);
}

// ---------------------------------------------------------------------------
// HASP_OLD (DHR and the X11/GLX ES1 games): the dongle is a USB presence
// check. clHASP::Check reads it over libusb and records the error on failure;
// a clHaspChecker thread re-checks it and quits the game if it is lost. It is
// bypassed by answering "no error" everywhere: Check does nothing, the getters
// report none, and the re-check thread ends. These are C++ member functions
// (the object is the first stack argument); the game never sees a non-zero
// error, so the presence is taken for granted.

static void noop(void *self)
{
    (void)self;
}

static int noError(void *self)
{
    (void)self;
    return 0;
}

static void dhrHaspCheck(void *self)
{
    (void)self;
}

static int dhrHaspIsError(void *self)
{
    (void)self;
    return 0;
}

static int dhrHaspErrorType(void *self)
{
    (void)self;
    return 0;
}

// The re-check thread, replaced. The original (ThreadFunc) is the only code
// that clears the "check pending" flag the boot sequence busy-waits on
// (clSeqBootThread::Run spins in IsCheckEnd() until it is cleared), so simply
// returning would hang the boot forever. Reproduce its completion path: no
// error, check done. Layout of the argument (clHaspChecker::stPimpl): byte 0
// = check pending, byte 1 = error, +4 = error type, +0x0c = mutex.
static void *dhrHaspCheckThread(void *arg)
{
    if (arg)
    {
        pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg + 0x0c));
        *(uint8_t *)arg = 0;
        *(uint8_t *)((uint8_t *)arg + 1) = 0;
        *(uint32_t *)((uint8_t *)arg + 4) = 0;
        pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg + 0x0c));
    }
    return NULL;
}

// A re-check request (clHaspChecker::Check) only sets "check pending" for the
// thread to clear; with the thread gone the flag would stay set and the next
// waiter would spin forever (the attract's live-camera demo waits on
// IsCheckEnd() first thing: a black screen after the publisher's logo). The
// request is dropped, so the last check, "no error", stands.
static void dhrHaspCheckRequest(void *self)
{
    (void)self;
}

// ---------------------------------------------------------------------------
// Tank! Tank! Tank!'s dongle layer: present, and its memory holds a serial.

static int tankDongleYes(void)
{
    return 1;
}

static int tankDongleRead(void *buffer)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    size_t n = g->dongleSerial ? strlen(g->dongleSerial) : 0;
    memset(buffer, 0, 64);
    memcpy(buffer, g->dongleSerial, n < 63 ? n : 63);
    return 1;
}

// ---------------------------------------------------------------------------
// LAN boot gate (DHR). The boot fiber ends only when its net thread
// (clSeqBootNetThread) dies with the "connect end" flag clear. The flag is
// set by every link outcome — a connect and the 60-second no-machines
// timeout alike — so a lone cabinet never passes the gate; the one clear
// exit is the LAN failure path: the server or client reports an error
// status, the net thread restarts it, counts down 60 frames, stops the
// network and ends. That path is triggered here: the service's start,
// shared by the clLanServer and clLanClient wrappers, never launches its
// thread — it reports the error status on the first call per service and
// "stopped" on the thread's restart — so the game's own failure handling
// ends the net thread about a second after the boot's signal, the boot does
// its system init and the game reaches attract mode without a network.

static void *dhrLanSessionSeen[4];

static void dhrLanSessionStart(void *self)
{
    int *status = (int *)((char *)self + 0x6c); // the controler's status
    int seen = 0;

    for (int i = 0; i < 4 && !seen; i++)
        if (dhrLanSessionSeen[i] == self)
            seen = 1;
    if (!seen)
        for (int i = 0; i < 4; i++)
            if (!dhrLanSessionSeen[i])
            {
                dhrLanSessionSeen[i] = self;
                break;
            }
    *status = seen ? 0 : 4; // error on the first start, stopped after
}

// ---------------------------------------------------------------------------
// The live camera (DHR). clCameraDeviceManager wraps the nmUVCCamera* V4L2
// driver, which speaks the cabinet camera's ABI: its init state machine
// (nmUVCCameraInitProgress) issues the cabinet driver's stream ioctls, which
// a standard uvcvideo device answers none of, so the driver always ends
// "init complete, no device". Boot then records the camera error and
// clApplication re-draws the CAMERA ERROR screen every frame. The manager is
// answered as a present, healthy camera: the queries say so, and
// InitProgress finishes the state machine the way the driver's own give-up
// path does (state 9, no device). The manager's internal flags then stay
// clear, so every capture, streaming and update call remains a no-op: the
// camera previews draw an empty texture and nothing touches the device.

static int dhrCamIsError(void *self)
{
    (void)self;
    return 0;
}

static int dhrCamIsDevice(void *self)
{
    (void)self;
    return 1;
}

// The driver's global init state (9 = complete). Set it the way the
// "no device" give-up path does: the boot's two camera phases each wait on
// it one frame per yield, and the real state machine would spend its whole
// retry budget rescanning /dev/video* for a device that can never bind.
static int dhrCamInitProgress(void)
{
    *(uint32_t *)(uintptr_t)namcoEs1CurrentGame()->camInitState = 9;
    return 1;
}

// ---------------------------------------------------------------------------
// The window's size (DHR). data/csv/config.csv gives the main size
// (MAIN_*SIZE, 1360x768: the 2D layout and the window) and the output's
// (VIDEO_*SIZE, 1920x1080 in the dump): when they differ, the game renders
// at the main size and its last pass scales that to an output-sized
// viewport — which only fits a window of the output's size, while the game
// opens one of the main size: the picture came out zoomed and shifted. The
// window is opened at the output's size instead (InitSystem's two loads read
// SCREEN_REAL_W/H for SCREEN_W/H); a game whose sizes agree is unchanged.

static void windowAtOutputSize(const NamcoEs1Game *g)
{
    for (int i = 0; i < 2; i++)
    {
        uint32_t operand = g->windowSizeLoad[i] + 2; // mov disp32,%edx: 8b 15 <disp32>
        uintptr_t page = operand & ~(uintptr_t)0xfff;

        if (!g->windowSizeLoad[i] || !g->screenReal)
            return;
        if (mprotect((void *)page, (operand + 4 - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        {
            log_error("Namco: cannot make %#x writable", operand);
            return;
        }
        *(uint32_t *)(uintptr_t)operand = g->screenReal + 4 * i;
    }
}

// The output's size is the loader's [Display] WIDTH x HEIGHT (the game's
// 1360x768 without a configuration): the game keeps its main size (the 2D
// layout) and its last pass scales the picture to that. Set once the game
// has read config.csv, before its window and its graphics are made.
static void outputSizeFromConfig(const NamcoEs1Game *g)
{
    int w = getConfig()->width, h = getConfig()->height;
    if (!g->screenReal || w <= 0 || h <= 0)
        return;
    *(int32_t *)(uintptr_t)g->screenReal = w;
    *(int32_t *)(uintptr_t)(g->screenReal + 4) = h;
}

// DHR: main has read config.csv (SettingFromConfig) and calls InitSystem,
// which makes the window: the output size, and fullscreen with -full or
// [Display] FULLSCREEN.
static int (*realInitSystem)(int, int);

static int dhrInitSystem(int fullscreen, int other)
{
    outputSizeFromConfig(namcoEs1CurrentGame());
    return realInitSystem(fullscreen || getConfig()->fullscreen, other);
}

// Maximum Heat 3D: the same, with InitSystem inlined in main: its window's
// settings (stInitializeXSystemData: fullscreen byte at 0, width at 8,
// height at 12) are made the output's before InitializeXSystem opens it.
static int (*realInitializeXSystem)(void *);

// Called from the game's code, on its 4-byte-aligned stack.
__attribute__((force_align_arg_pointer)) static int mh3dInitializeXSystem(void *data)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    uint8_t *d = data;

    outputSizeFromConfig(g);
    *(int32_t *)(d + 8) = *(int32_t *)(uintptr_t)g->screenReal;
    *(int32_t *)(d + 12) = *(int32_t *)(uintptr_t)(g->screenReal + 4);
    if (getConfig()->fullscreen)
        d[0] = 1;
    return realInitializeXSystem(data);
}

// Maximum Heat 3D's steering board self-check (clKickback::requestSelfCheck):
// the game's request, then the board's answer (see namcoEs1Kickback.c).
static void (*realRequestSelfCheck)(void *);

__attribute__((force_align_arg_pointer)) static void requestSelfCheck(void *self)
{
    realRequestSelfCheck(self);
    namcoEs1KickbackReportSelfCheck();
}

// Maximum Heat 3D's test mode (testModeCall): its clock adjustments zeroed
// before the constructor runs.
static void (*realTestMode)(void *);

__attribute__((force_align_arg_pointer)) static void testModeConstructor(void *self)
{
    if (self)
        memset((uint8_t *)self + namcoEs1CurrentGame()->testModeClock, 0, 0x1c);
    realTestMode(self);
}

// A clock_difference.bin (76 bytes: a 2-byte marker repeated at its end,
// and a date in 100 ns ticks at 58) that is not valid is moved aside.
static void checkClockData(const char *path)
{
    unsigned char d[77];
    uint64_t ticks;
    char aside[PATH_MAX];
    FILE *f = fopen(path, "rb");

    if (!f)
        return;
    size_t n = fread(d, 1, sizeof(d), f);
    fclose(f);
    memcpy(&ticks, d + 58, sizeof(ticks));
    if (n == 76 && d[0] == d[74] && d[1] == d[75] && ticks >= 190000000000000000ULL && ticks <= 500000000000000000ULL)
        return;
    snprintf(aside, sizeof(aside), "%s.corrupt", path);
    if (rename(path, aside) == 0)
        log_warn("Namco: %s was not valid, moved to %s", path, aside);
}

// Point the call at address (e8 rel32) to replacement; returns its target.
static void *redirectCall(uint32_t address, void *replacement)
{
    uint8_t *call = (uint8_t *)(uintptr_t)address;
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (!address || call[0] != 0xe8)
        return NULL;
    if (mprotect((void *)page, (address + 5 - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return NULL;
    void *target = call + 5 + *(int32_t *)(call + 1);
    *(int32_t *)(call + 1) = (int32_t)((uint8_t *)replacement - (call + 5));
    return target;
}

// Set the immediate of a "movl $imm,disp8(%ebp)" (c7 45 disp8 imm32).
static void setMovImmediate(uint32_t address, int32_t value)
{
    uint8_t *mov = (uint8_t *)(uintptr_t)address;
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (!address)
        return;
    if (mov[0] != 0xc7 || mov[1] != 0x45)
    {
        log_error("Namco: unexpected code at %#x, not patched", address);
        return;
    }
    if (mprotect((void *)page, (address + 7 - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return;
    memcpy(mov + 3, &value, 4);
}

// Tank! Tank! Tank!: a portrait game, made for a landscape monitor turned on
// its side: it draws a landscape frame, the picture rotated in it (its
// default, "rright"), which the loader turns back upright when it fits it in
// the window (portraitFrame); with [Display] ROTATE_VERTICAL (a turned
// monitor) the frame is shown as it is. (Its upright mode, rotation 0,
// draws a portrait frame its landscape screen size crops.) Fullscreen (its
// default) only with [Display] FULLSCREEN.
static void portraitWindow(const NamcoEs1Game *g)
{
    if (g->windowFullscreenMov)
        setMovImmediate(g->windowFullscreenMov, getConfig()->fullscreen ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Intel compiler's CPU check (Dead Heat, Maximum Heat 3D). main first calls
// __intel_new_proc_init_T (Dead Heat: SSSE3, __intel_cpu_indicator 0x1000 or
// above) or _S (Maximum Heat 3D: SSE4.1, 0x2000); the compiler's runtime sets
// the indicator from cpuid on "GenuineIntel" CPUs only and leaves others at
// "generic", so the game exits. On another vendor's CPU it is preset to the
// level the CPU has (the runtime then keeps it): the check passes and the
// dispatched memcpy/memset take paths the CPU has.

static void intelCpuIndicator(const NamcoEs1Game *g)
{
    unsigned int eax, ebx, ecx, edx, level;
    uint32_t *indicator = (uint32_t *)(uintptr_t)g->intelCpuIndicator;

    if (!indicator || !__get_cpuid(0, &eax, &ebx, &ecx, &edx))
        return;
    if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e) // GenuineIntel
        return;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return;
    level = ecx & bit_SSE4_1 ? 0x2000 : ecx & bit_SSSE3 ? 0x1000 : 0;
    if (*indicator < level)
        *indicator = level;
}

// ---------------------------------------------------------------------------
// The cabinet's paths. The game names its directory by its absolute path
// there; here it is the directory it runs from. The directories under it the
// cabinet's storage script made (save00, scoredata, rankdata) are made on
// first use.

static char gameDir[PATH_MAX];

static const char *redirectRoot(const NamcoEs1Game *g, const char *path, char *buf, size_t size)
{
    size_t n;

    if (!g || !g->rootPath || !gameDir[0])
        return path;
    for (const char *const *root = g->cabinetRoots; root && *root; root++)
    {
        n = strlen(*root);
        if (!strncmp(path, *root, n) && (path[n] == '/' || path[n] == '\0'))
        {
            snprintf(buf, size, "%s%s", gameDir, path);
            return buf;
        }
    }
    n = strlen(g->rootPath);
    if (strncmp(path, g->rootPath, n) || (path[n] != '/' && path[n] != '\0'))
        return path;
    snprintf(buf, size, "%s%s", gameDir, path + n);
    char *slash = strrchr(buf, '/');
    if (slash && slash > buf + strlen(gameDir))
    {
        *slash = '\0';
        mkdir(buf, 0755); // the file's directory (one level, as the cabinet's)
        *slash = '/';
    }
    return buf;
}

const char *namcoEs1RedirectPath(const char *path, char *buf, size_t size)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    char fallback[PATH_MAX];

    if (!g)
        return path;
    path = redirectRoot(g, path, buf, size);
    for (const char *const *f = g->fileFallbacks; f && f[0] && f[1]; f += 2)
    {
        // The path names the missing file (relative to the game's directory
        // or absolute in it): the other one, when it is really missing (the
        // access syscall: the loader's access() comes back here).
        size_t length = strlen(path), missing = strlen(f[0]);
        if (length < missing || strcmp(path + length - missing, f[0]) ||
            (length > missing && path[length - missing - 1] != '/') ||
            syscall(SYS_access, path, F_OK) == 0)
            continue;
        snprintf(fallback, sizeof(fallback), "%.*s%s", (int)(length - missing), path, f[1]);
        snprintf(buf, size, "%s", fallback);
        return buf;
    }
    return path;
}

// ---------------------------------------------------------------------------
// Cabinet administration and network identity.

int namcoEs1DropCommand(const char *command)
{
    if (!isNamcoEs1Game() || !command)
        return 0;
    if (strncmp(command, "su -c", 5) != 0 && !strstr(command, "sudo "))
        return 0;
    if (namcoEs1CurrentGame()->networkCommands)
        namcoEs1NetworkCommand(command);
    if (trace)
        fprintf(stderr, "Namco: not run: %s\n", command);
    return 1;
}

int namcoEs1Eth0(unsigned long request, void *arg)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    struct ifreq *ifr = arg;
    struct sockaddr_in *in = (struct sockaddr_in *)&ifr->ifr_addr;
    uint8_t value[4];

    if (!g || !g->eth0 || !ifr || strcmp(ifr->ifr_name, "eth0") != 0)
        return -1;
    switch (request)
    {
    case SIOCGIFFLAGS:
        ifr->ifr_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
        return 0;
    case SIOCGIFADDR:
        memcpy(value, g->eth0, 4);
        break;
    case SIOCGIFNETMASK:
        memcpy(value, g->eth0 + 4, 4);
        break;
    case SIOCGIFBRDADDR:
        for (int i = 0; i < 4; i++)
            value[i] = g->eth0[i] | ~g->eth0[4 + i];
        break;
    default:
        return -1;
    }
    memset(in, 0, sizeof(*in));
    in->sin_family = AF_INET;
    memcpy(&in->sin_addr, value, 4);
    return 0;
}

const char *namcoEs1CommandOutput(const char *command)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    return g && g->commandOutput && command ? g->commandOutput(command) : NULL;
}

int namcoEs1HwAddr(int fd, void *arg, int (*realIoctl)(int, int, void *))
{
    struct ifreq *ifr = arg;
    struct if_nameindex *names;

    if (realIoctl(fd, SIOCGIFHWADDR, arg) == 0)
        return 0;
    if ((names = if_nameindex()))
    {
        for (struct if_nameindex *n = names; n->if_index; n++)
        {
            struct ifreq other = {0};
            static const unsigned char none[6];

            snprintf(other.ifr_name, sizeof(other.ifr_name), "%s", n->if_name);
            if (strcmp(n->if_name, "lo") == 0 || realIoctl(fd, SIOCGIFHWADDR, &other) != 0 ||
                memcmp(other.ifr_hwaddr.sa_data, none, sizeof(none)) == 0)
                continue;
            ifr->ifr_hwaddr = other.ifr_hwaddr;
            if_freenameindex(names);
            return 0;
        }
        if_freenameindex(names);
    }
    // No network interface: a locally administered address.
    static const unsigned char fallback[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    ifr->ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr->ifr_hwaddr.sa_data, fallback, sizeof(fallback));
    return 0;
}

// ---------------------------------------------------------------------------
// GLSL shader sources.
//
// The games' #version 120 shaders (the game prepends it when it reads the
// file) use things Mesa's compiler refuses, NVIDIA's of the time accepted:
// - texture2DLod(s, c, l): the explicit-LOD form does not exist in GLSL 1.20;
//   replaced with texture2D(s, c), letting the driver choose the LOD.
// - gl_TexCoord[] indexed by a variable: the built-in is an unsized array;
//   redeclaring it with a size also turns it into a regular varying, which
//   the vertex shader writes and the fragment shader reads.
// - "half" as a variable name (Nirin): a reserved word.

// "texture2DLod(a, b, c)" -> "texture2D(a, b)": drop the explicit LOD.
static char *fixTextureLod(char *src)
{
    char *p = src;
    while ((p = strstr(p, "texture2DLod(")) != NULL)
    {
        const long base = p - src;
        long comma = -1, close = -1;
        int depth = 1;
        for (long i = base + strlen("texture2DLod("); src[i]; i++)
        {
            if (src[i] == '(')
                depth++;
            else if (src[i] == ')' && --depth == 0)
                close = i;
            else if (src[i] == ',' && depth == 1)
                comma = i;
            if (close >= 0)
                break;
        }
        if (comma < 0 || close < 0)
            break; // not a (sampler, coord, lod) call; leave it

        memmove(src + base + 9, src + base + 12, strlen(src + base + 12) + 1); // drop "Lod"
        memmove(src + comma - 2, src + close - 2, strlen(src + close - 2) + 1);
        src[comma - 3] = ')'; // drop ", c"
        src += comma - 2; // continue after the rewritten call
    }
    return src;
}

// "half", a reserved word in GLSL (Nirin's lighting uses it as a variable):
// renamed "halF", same length, wherever it is a whole word.
static void fixHalf(char *src)
{
    for (char *p = strstr(src, "half"); p; p = strstr(p + 4, "half"))
    {
        char before = p > src ? p[-1] : ' ', after = p[4];
        if (!isalnum((unsigned char)before) && before != '_' && !isalnum((unsigned char)after) && after != '_')
            p[3] = 'F';
    }
}

// gl_TexCoord[] indexed by something else than a number (Nirin's
// "gl_TexCoord[2 + loop]" too).
static int texCoordIndexed(const char *src)
{
    for (const char *p = strstr(src, "gl_TexCoord["); p; p = strstr(p + 1, "gl_TexCoord["))
    {
        for (const char *i = p + strlen("gl_TexCoord["); *i && *i != ']'; i++)
            if (*i != ' ' && (*i < '0' || *i > '9'))
                return 1;
    }
    return 0;
}

void namcoEs1PatchShader(char *src)
{
    fixTextureLod(src);
    fixHalf(src);
    if (!texCoordIndexed(src))
        return;
    // After the #version line, which DHR's sources get from the game; at the
    // top of those without one (Dead Heat's: GLSL 1.10).
    static const char sized[] = "varying vec4 gl_TexCoord[gl_MaxTextureCoords];\n";
    size_t n = sizeof(sized) - 1;
    char *ver = strstr(src, "#version"), *at = src;
    if (ver)
    {
        char *nl = strchr(ver, '\n');
        if (!nl)
            return;
        at = nl + 1;
    }
    memmove(at + n, at, strlen(at) + 1);
    memcpy(at, sized, n);
}

// ---------------------------------------------------------------------------
// Video. The game renders its scene at its own size (1360x768, into its own
// framebuffers) and draws the last pass at its window's: the window is opened
// at [Display] WIDTH x HEIGHT, fullscreen with [Display] FULLSCREEN, and the
// picture fills it. (Scaling the window's frame as for the Raw Thrills games
// crops it instead.)
//
// The loader's other GL wrappers (glEnable, glBindTexture, glTexImage2D...,
// see graphics/ and patching/) call through glad's entry points, which need
// loading once the game has a context: right after its SDL_SetVideoMode.

// With evdev input the pad reaches the game through the loader's JVS board
// only: the game's own SDL joystick (summed with the board, see
// docs/namco-es1.md) would read the same pad again under its own button
// numbering, one press then standing for two cabinet buttons (Nirin's Enter
// and View). The game sees no joystick.
static int noJoysticks(void)
{
    return 0;
}

static void *noJoystickOpen(int index)
{
    (void)index;
    return NULL;
}

#define SDL_FULLSCREEN 0x80000000u

static void *(*realSetVideoMode)(int, int, int, uint32_t);

static void *setVideoMode(int width, int height, int bpp, uint32_t flags)
{
    static int glLoaded;
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    int w = getConfig()->width, h = getConfig()->height;

    if (g && g->screenReal)
    {
        // Dead Heat: its last pass draws at its output size (SCREEN_REAL),
        // whatever the window's (see windowAtOutputSize): the window is that
        // size, the loader's [Display] WIDTH x HEIGHT (config.csv is read).
        outputSizeFromConfig(g);
        width = *(int32_t *)(uintptr_t)g->screenReal;
        height = *(int32_t *)(uintptr_t)(g->screenReal + 4);
    }
    else if (w > 0 && h > 0)
    {
        width = w;
        height = h;
    }
    if (getConfig()->fullscreen)
        flags |= SDL_FULLSCREEN;
    else
        flags &= ~SDL_FULLSCREEN;
    printf("Namco: window %dx%d%s\n", width, height, flags & SDL_FULLSCREEN ? " fullscreen" : "");
    void *surface = realSetVideoMode(width, height, bpp, flags);

    if (surface && !glLoaded)
    {
        GLADloadfunc getProcAddress = (GLADloadfunc)dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
        glLoaded = 1;
        if (!getProcAddress || !gladLoadGL(getProcAddress))
            log_error("Namco: cannot load the GL entry points");
    }
    return surface;
}


// The game's dlsym: the sound driver's entry points are the loader's (see
// namcoEs1Sound.c), and the functions it gets keep its stack aligned where it
// needs it.
static void *(*realDlsym)(void *, const char *);

static void *gameDlsym(void *handle, const char *name)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    void *p = g && g->soundEmulation ? namcoEs1SoundSymbol(name) : NULL;
    const Elf32_Sym *sym = NULL;
    Dl_info info;

    if (!p && (!(p = realDlsym(handle, name)) || !dladdr1(p, &info, (void **)&sym, RTLD_DL_SYMENT) || !sym ||
               ELF32_ST_TYPE(sym->st_info) != STT_FUNC))
        return p;
    return g && g->alignStack ? namcoEs1AlignedPointer(p) : p;
}

// ---------------------------------------------------------------------------
// The frame scaled into the window (Tank! Tank! Tank!), as for the Raw
// Thrills games (graphics/frameScale.h): the game draws its 1360x768 frame,
// its "framebuffer 0" being an offscreen one of that size, and each swap
// fits it in the middle of the window, which is [Display] WIDTH x HEIGHT,
// turned upright unless the monitor is (ROTATE_VERTICAL).

static int frameScaling;
static Window (*realCreateWindow)(Display *, Window, int, int, unsigned, unsigned, unsigned, int, unsigned, Visual *,
                                  unsigned long, XSetWindowAttributes *);

static Window portraitCreateWindow(Display *dpy, Window parent, int x, int y, unsigned width, unsigned height,
                                   unsigned border, int depth, unsigned cls, Visual *visual, unsigned long mask,
                                   XSetWindowAttributes *attributes)
{
    int w = getConfig()->width, h = getConfig()->height;
    if (parent == DefaultRootWindow(dpy) && !getConfig()->fullscreen && w > 0 && h > 0)
    {
        width = w;
        height = h;
    }
    Window window = realCreateWindow(dpy, parent, x, y, width, height, border, depth, cls, visual, mask, attributes);
    // This process's window (_NET_WM_PID), as SDL marks its own: the quit
    // watch (Esc) only acts on a window so marked, and this hook bypasses
    // the loader's XCreateWindow, which marks the others.
    if (window && parent == DefaultRootWindow(dpy))
    {
        long pid = getpid();
        XChangeProperty(dpy, window, XInternAtom(dpy, "_NET_WM_PID", False), XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&pid, 1);
    }
    return window;
}

static void portraitFrame(const NamcoEs1Game *g)
{
    static const char *const wrapped[] = {"glViewport",  "glGetIntegerv",        "glBindFramebuffer",
                                          "glBindFramebufferEXT", "glReadBuffer", "glDrawBuffer"};
    if (!g->windowRotationMov)
        return;
    frameScaleInit((void *(*)(const char *))dlsym(RTLD_DEFAULT, "glXGetProcAddressARB"), getConfig()->keepAspectRatio);
    frameScaleSetFrame(1360, 768, NULL);
    if (!getConfig()->rotateVertical)
        frameScaleSetRotation(-1);
    for (size_t i = 0; i < sizeof(wrapped) / sizeof(wrapped[0]); i++)
    {
        void *w = frameScaleWrapper(wrapped[i]);
        if (w)
            hookExecutableImport(wrapped[i], w);
    }
    *(void **)&realCreateWindow = hookExecutableImport("XCreateWindow", (void *)portraitCreateWindow);
    frameScaling = 1;
}

void namcoEs1BeforeSwap(void *dpy, unsigned long drawable)
{
    static void (*queryDrawable)(Display *, GLXDrawable, int, unsigned *);
    unsigned w = 0, h = 0;

    if (!frameScaling || !isNamcoEs1Game())
        return;
    if (!queryDrawable)
        *(void **)&queryDrawable = dlsym(RTLD_DEFAULT, "glXQueryDrawable");
    if (!queryDrawable)
        return;
    queryDrawable(dpy, drawable, GLX_WIDTH, &w);
    queryDrawable(dpy, drawable, GLX_HEIGHT, &h);
    if (w && h)
        frameScalePresent(0, 0, w, h, 0, 0, 0);
}

// ---------------------------------------------------------------------------

int namcoEs1Init(void)
{
    const NamcoEs1Game *g = namcoEs1CurrentGame();
    if (!g)
        return -1;

    trace = getenv("NAMCO_JVS_TRACE") != NULL;

    // The analog channels' rest: steering centred, pedals released. initJVS
    // leaves them at 0, which is full left for the handle; the desktop keys
    // (non-evdev) and evdev drive them from here.
    setAnalogue(ANALOGUE_1, 0x8000);
    setAnalogue(ANALOGUE_2, 0);
    setAnalogue(ANALOGUE_3, 0);
    intelCpuIndicator(g);
    if (g->rootPath && !getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    for (const char *const *dir = g->cabinetDirectories; gameDir[0] && dir && *dir; dir++)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", gameDir, *dir);
        mkdir(path, 0755);
    }
    for (const char *const *link = g->cabinetLinks; gameDir[0] && link && link[0] && link[1]; link += 2)
    {
        char path[PATH_MAX];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", gameDir, link[0]);
        if (lstat(path, &st) != 0 && symlink(link[1], path) != 0)
            log_warn("Namco: cannot link %s to %s", path, link[1]);
    }
    if (g->jammaCalibration)
        saveJammaDefaults(g);

    // Dead Heat's JVS board, whatever its config.csv says: mov $1,%eax.
    static const uint8_t jammaLoad[] = {0x0f, 0xb6, 0x44, 0x24, 0x08}, jammaTrue[] = {0xb8, 0x01, 0x00, 0x00, 0x00};
    patchCode(g->jammaForce, jammaLoad, jammaTrue, sizeof(jammaTrue));
    // Its steering board too: movzbl 0x1(%reg),%reg becomes xor %reg,%reg;
    // inc %reg; nop.
    for (int i = 0; i < 2; i++)
    {
        const uint8_t *code = (const uint8_t *)(uintptr_t)g->steerDeviceForce[i];
        if (!code)
            continue;
        uint8_t reg = (code[2] >> 3) & 7;
        const uint8_t load[] = {0x0f, 0xb6, code[2], 0x01}, one[] = {0x31, (uint8_t)(0xc0 | reg << 3 | reg), (uint8_t)(0x40 + reg), 0x90};
        if ((code[2] & 0xc0) == 0x40)
            patchCode(g->steerDeviceForce[i], load, one, sizeof(one));
        else
            log_error("Namco: unexpected code at %#x, the steering board not forced", g->steerDeviceForce[i]);
    }
    detour(g->dongleInit, tankDongleYes);
    detour(g->dongleReadDecoded, tankDongleRead);
    detour(g->dongleUse, tankDongleYes);
    detour(g->donglePresent, tankDongleYes);

    // The link search: 1 s instead of 60 (mov $60,%edx).
    static const uint8_t linkSearch[] = {0xba, 0x10, 0x0e, 0x00, 0x00}, linkSearchShort[] = {0xba, 0x3c, 0x00, 0x00, 0x00};
    patchCode(g->linkSearchMov, linkSearch, linkSearchShort, sizeof(linkSearchShort));

    if (g->testModeCall[0])
    {
        checkClockData("save0/testmode/clock_difference.bin");
        checkClockData("save1/testmode/clock_difference.bin");
    }
    for (int i = 0; i < 2; i++)
    {
        void *real = g->testModeCall[i] ? redirectCall(g->testModeCall[i], (void *)testModeConstructor) : NULL;
        if (real)
            realTestMode = real;
        else if (g->testModeCall[i])
            log_error("Namco: unexpected code at %#x, the test mode not redirected", g->testModeCall[i]);
    }
    namcoEs1KickbackInit(g);
    for (int i = 0; i < 2; i++)
    {
        void *real = g->selfCheckCall[i] ? redirectCall(g->selfCheckCall[i], (void *)requestSelfCheck) : NULL;
        if (real)
            realRequestSelfCheck = real;
        else if (g->selfCheckCall[i])
            log_error("Namco: unexpected code at %#x, the self-check not redirected", g->selfCheckCall[i]);
    }
    detour(g->systemIsError, noError);
    detour(g->systemIsErrorConnectionCheck, noError);

    // The boot gates of the Dead Heat games (where the game has them): the
    // LAN, and the live camera when there is none (a webcam plays it).
    detour(g->lanSessionStart, dhrLanSessionStart);
    detour(g->testModeStopNet, (void *)noop);
    if (!g->cameraWebcam || !webcamHasCamera())
    {
        detour(g->camIsError, dhrCamIsError);
        detour(g->camIsDevice, dhrCamIsDevice);
        detour(g->camInitProgress, dhrCamInitProgress);
        if (g->cameraPresent)
            *(uint8_t *)(uintptr_t)g->cameraPresent = 0;
    }
    else
    {
        // The driver wants a capture buffer larger than a 640x480 YUYV
        // picture (0x96000, the cabinet camera's were); a webcam's is that
        // size exactly: "jbe" (too small) becomes "jb".
        static const uint8_t jbe[] = {0x0f, 0x86}, jb[] = {0x0f, 0x82};
        patchCode(g->cameraBufferCheck, jbe, jb, sizeof(jb));
        log_info("Namco: the live camera is the webcam");
    }

    if (g->windowX11)
    {
        // DHR: no SDL. The game opens its own X11/GLX window (InitSystem ->
        // InitializeXSystem) and reads its controls over the JVS board and
        // USB; the loader's GL interposers apply to that window. Here there
        // is only the HASP_OLD dongle to answer for.
        detour(g->haspCheck, dhrHaspCheck);
        detour(g->haspIsError, dhrHaspIsError);
        detour(g->haspErrorType, dhrHaspErrorType);
        detour(g->haspCheckThread, dhrHaspCheckThread);
        detour(g->haspCheckRequest, dhrHaspCheckRequest);
        // Maximum Heat 3D: the linked HASP HL instead (as Nirin's).
        if (g->haspLogin)
            detourHaspHL(g);
        windowAtOutputSize(g);
        portraitWindow(g);
        portraitFrame(g);
        if (g->initSystemCall && !(realInitSystem = redirectCall(g->initSystemCall, (void *)dhrInitSystem)))
            log_error("Namco: unexpected code at %#x, InitSystem not redirected", g->initSystemCall);
        if (g->initXSystemCall &&
            !(realInitializeXSystem = redirectCall(g->initXSystemCall, (void *)mh3dInitializeXSystem)))
            log_error("Namco: unexpected code at %#x, InitializeXSystem not redirected", g->initXSystemCall);
        if (g->soundEmulation)
            realDlsym = hookExecutableImport("dlsym", (void *)gameDlsym);
        if (g->alignStack)
            namcoEs1AlignImports();
        if (g->install)
            g->install();
        desktopStartQuitWatch();
        return 0;
    }

    // Nirin: SDL 1.2 window + the linked HASP HL dongle. The games link GL
    // directly: sdl12-compat's GL scaling would present its own, empty,
    // framebuffer (see rtInit()).
    setenv("SDL12COMPAT_OPENGL_SCALING", "0", 1);
    detourHaspHL(g);

    realSetVideoMode = hookExecutableImport("SDL_SetVideoMode", (void *)setVideoMode);
    if (!realSetVideoMode)
    {
        log_error("Namco: the game does not import SDL_SetVideoMode");
        return -1;
    }
    if (getConfig()->inputMode == 2)
    {
        hookExecutableImport("SDL_NumJoysticks", (void *)noJoysticks);
        hookExecutableImport("SDL_JoystickOpen", (void *)noJoystickOpen);
    }
    if (g->soundEmulation)
        realDlsym = hookExecutableImport("dlsym", (void *)gameDlsym);
    if (g->alignStack)
        namcoEs1AlignImports();

    desktopStartQuitWatch();
    return 0;
}
