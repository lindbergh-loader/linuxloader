// Minecraft Dungeons Arcade input.
//
// Minecraft reads its controls through SDL's GameController layer and ships the
// standard controller database, so the loader feeds it by BEING a controller:
// it creates a uinput virtual pad that looks like an Xbox 360 gamepad (SDL maps
// that GUID on its own) and a thread drives it from the keyboard.
//
// The keyboard is read at the evdev level (/dev/input), not through X: XQueryKeymap
// only reflects the focused surface under Wayland, so an X-based poll sees nothing
// there.  Reading evdev works under both Wayland and a bare X cabinet, and both
// ends (keyboard in, pad out) are kernel devices, so no display server is involved.
//
// The cabinet START button is the pad's Start; TEST/operator is a cabinet switch,
// not on the player pad, so it is not covered here.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "g7.h"

// ---------------------------------------------------------------------------
// The virtual pad (Xbox 360 layout: SDL recognises 045e:028e and maps it).
static int ufd = -1;

static void emit(int type, int code, int val)
{
    struct input_event ev = {0};
    ev.type = type;
    ev.code = code;
    ev.value = val;
    ssize_t n = write(ufd, &ev, sizeof ev);
    (void)n;
}

static int makePad(void)
{
    static const int buttons[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR,
                                  BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    static const int axes[] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};

    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0 && (ufd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK)) < 0)
    {
        g7Log("minecraft: cannot open /dev/uinput (%s); no keyboard pad\n", strerror(errno));
        return 0;
    }
    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_ABS);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
        ioctl(ufd, UI_SET_KEYBIT, buttons[i]);
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; i++)
        ioctl(ufd, UI_SET_ABSBIT, axes[i]);

    struct uinput_user_dev dev = {0};
    snprintf(dev.name, sizeof dev.name, "Microsoft X-Box 360 pad");
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0x045e;
    dev.id.product = 0x028e;
    dev.id.version = 0x0114;
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; i++)
    {
        int a = axes[i], hat = a == ABS_HAT0X || a == ABS_HAT0Y, trig = a == ABS_Z || a == ABS_RZ;
        dev.absmin[a] = hat ? -1 : trig ? 0 : -32768;
        dev.absmax[a] = hat ? 1 : trig ? 255 : 32767;
    }
    if (write(ufd, &dev, sizeof dev) != sizeof dev || ioctl(ufd, UI_DEV_CREATE) < 0)
    {
        g7Log("minecraft: uinput device setup failed (%s)\n", strerror(errno));
        close(ufd);
        ufd = -1;
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The keymap, in evdev KEY_* codes.  Buttons are one key each; the d-pad and
// left stick are key pairs on one axis.
static const struct { int key, code; } padButtons[] = {
    {KEY_ENTER, BTN_START}, {KEY_KPENTER, BTN_START},  // Enter: START
    {KEY_SPACE, BTN_A},                                 // Space: A (confirm)
    {KEY_BACKSPACE, BTN_B},                             // Backspace: B (cancel)
    {KEY_Z, BTN_A}, {KEY_X, BTN_B}, {KEY_C, BTN_X}, {KEY_V, BTN_Y},
    {KEY_Q, BTN_TL}, {KEY_E, BTN_TR},
    {KEY_TAB, BTN_SELECT},                              // Tab: Back
    {KEY_F2, BTN_MODE},                                 // F2: Guide (operator, best guess)
};
static const struct { int neg, pos, code, mag; } padAxes[] = {
    {KEY_LEFT, KEY_RIGHT, ABS_HAT0X, 1},   // d-pad X
    {KEY_UP, KEY_DOWN, ABS_HAT0Y, 1},      // d-pad Y (up = -1)
    {KEY_A, KEY_D, ABS_X, 32767},          // left stick X
    {KEY_W, KEY_S, ABS_Y, 32767},          // left stick Y (up = -1)
};

// Open every evdev keyboard (a device that reports KEY_ENTER and the letters).
#define KBD_MAX 8
static int kbdFd[KBD_MAX];
static int kbdCount;

static int isKeyboard(int fd)
{
    unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return 0;
#define HAS(b) (bits[(b) / (8 * sizeof(long))] & (1UL << ((b) % (8 * sizeof(long)))))
    return HAS(KEY_ENTER) && HAS(KEY_A) && HAS(KEY_Z) && !HAS(BTN_LEFT);
#undef HAS
}

static void openKeyboards(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    if (!d)
        return;
    while ((e = readdir(d)) && kbdCount < KBD_MAX)
    {
        char path[300];
        if (strncmp(e->d_name, "event", 5))
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (isKeyboard(fd))
            kbdFd[kbdCount++] = fd;
        else
            close(fd);
    }
    closedir(d);
}

// Current key state, indexed by KEY_* code.
static unsigned char keyState[KEY_CNT];

static void *poller(void *arg)
{
    (void)arg;
    int prevBtn[sizeof padButtons / sizeof padButtons[0]] = {0};
    int prevAx[sizeof padAxes / sizeof padAxes[0]] = {0};
    g7Log("minecraft: keyboard pad ready over %d evdev keyboard(s) (Enter = START)\n", kbdCount);

    for (;;)
    {
        // drain all pending key events
        for (int i = 0; i < kbdCount; i++)
        {
            struct input_event ev;
            while (read(kbdFd[i], &ev, sizeof ev) == (ssize_t)sizeof ev)
                if (ev.type == EV_KEY && ev.code < KEY_CNT && ev.value != 2)
                    keyState[ev.code] = ev.value ? 1 : 0;
        }
        int changed = 0;
        for (size_t i = 0; i < sizeof padButtons / sizeof padButtons[0]; i++)
        {
            int v = keyState[padButtons[i].key] ? 1 : 0;
            if (v != prevBtn[i])
            {
                emit(EV_KEY, padButtons[i].code, v);
                prevBtn[i] = v;
                changed = 1;
            }
        }
        for (size_t i = 0; i < sizeof padAxes / sizeof padAxes[0]; i++)
        {
            int v = (keyState[padAxes[i].pos] ? padAxes[i].mag : 0) - (keyState[padAxes[i].neg] ? padAxes[i].mag : 0);
            if (v != prevAx[i])
            {
                emit(EV_ABS, padAxes[i].code, v);
                prevAx[i] = v;
                changed = 1;
            }
        }
        if (changed)
            emit(EV_SYN, SYN_REPORT, 0);
        usleep(6000);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// PC/SC card reader stub.
//
// The game reads its NFC cards through the PC/SC API (libpcsclite: SCard*).
// On a cabinet (and under TeknoParrot, which stubs winscard) the reader is
// present, so the card system comes up and the game is "authorized"; on a bare
// Linux box with no reader those calls fail and the game shows "needs to be
// authorized to dispense more cards".  So the loader presents a reader itself.
//
// This first cut reports one reader with no card inserted -- enough to bring
// the card system up.  Injecting a card (for actual play) is a later step:
// SCardGetStatusChange would report PRESENT with an ATR and SCardTransmit would
// answer the NTAG reads.  On this Linux PC/SC's LONG/DWORD/SCARDCONTEXT are all
// 64-bit (wintypes.h non-Apple branch).
typedef long SC_LONG;
typedef unsigned long SC_DWORD;
typedef unsigned long SC_CTX;
typedef unsigned long SC_CARD;
#define SCARD_S_SUCCESS 0L
#define SCARD_E_TIMEOUT 0x8010000AL
#define SCARD_E_NO_SMARTCARD 0x8010000CL
#define SCARD_E_INSUFFICIENT_BUFFER 0x80100008L
#define SCARD_STATE_CHANGED 0x0002
#define SCARD_STATE_EMPTY 0x0010
#define SCARD_AUTOALLOCATE ((SC_DWORD) -1)

typedef struct
{
    const char *szReader;
    void *pvUserData;
    SC_DWORD dwCurrentState;
    SC_DWORD dwEventState;
    SC_DWORD cbAtr;
    unsigned char rgbAtr[33];
} SC_READERSTATE;

static const char scReaderName[] = "ACS ACR122U PICC Interface 00 00"; // one reader, double-null below

static SC_LONG scEstablish(SC_DWORD scope, const void *r1, const void *r2, SC_CTX *ctx)
{
    (void)scope; (void)r1; (void)r2;
    g7Log("minecraft: SCardEstablishContext (stub)\n");
    if (ctx) *ctx = 1;
    return SCARD_S_SUCCESS;
}
static SC_LONG scRelease(SC_CTX ctx) { (void)ctx; return SCARD_S_SUCCESS; }
static SC_LONG scCancel(SC_CTX ctx) { (void)ctx; return SCARD_S_SUCCESS; }

static SC_LONG scListReaders(SC_CTX ctx, const char *groups, char *readers, SC_DWORD *len)
{
    (void)ctx; (void)groups;
    SC_DWORD need = sizeof scReaderName + 1; // name + its NUL + list-terminating NUL
    if (!len) return SCARD_E_INSUFFICIENT_BUFFER;
    if (readers && *len == SCARD_AUTOALLOCATE)
    {
        char *buf = malloc(need);
        if (!buf) return SCARD_E_INSUFFICIENT_BUFFER;
        memcpy(buf, scReaderName, sizeof scReaderName);
        buf[sizeof scReaderName] = 0;
        *(char **)readers = buf;
        *len = need;
        return SCARD_S_SUCCESS;
    }
    if (!readers || *len < need) { *len = need; return readers ? SCARD_E_INSUFFICIENT_BUFFER : SCARD_S_SUCCESS; }
    memcpy(readers, scReaderName, sizeof scReaderName);
    readers[sizeof scReaderName] = 0;
    *len = need;
    return SCARD_S_SUCCESS;
}

// No card present: report EMPTY once, then wait out the timeout so the poll
// loop neither spins nor sees a phantom card.
static SC_LONG scGetStatusChange(SC_CTX ctx, SC_DWORD timeout, SC_READERSTATE *states, SC_DWORD n)
{
    (void)ctx;
    SC_LONG ret = SCARD_E_TIMEOUT;
    for (SC_DWORD i = 0; i < n; i++)
    {
        SC_DWORD ev = SCARD_STATE_EMPTY;
        if (!(states[i].dwCurrentState & SCARD_STATE_EMPTY))
        {
            ev |= SCARD_STATE_CHANGED;
            ret = SCARD_S_SUCCESS;
        }
        states[i].dwEventState = ev;
        states[i].cbAtr = 0;
    }
    if (ret == SCARD_E_TIMEOUT)
        usleep((timeout == (SC_DWORD)-1 || timeout > 400 ? 400 : timeout) * 1000);
    return ret;
}
static SC_LONG scConnect(SC_CTX ctx, const char *reader, SC_DWORD share, SC_DWORD proto,
                         SC_CARD *card, SC_DWORD *active)
{
    (void)ctx; (void)reader; (void)share; (void)proto; (void)card; (void)active;
    return SCARD_E_NO_SMARTCARD; // no card to connect to yet
}
static SC_LONG scDisconnect(SC_CARD card, SC_DWORD disp) { (void)card; (void)disp; return SCARD_S_SUCCESS; }
static SC_LONG scTransmit(SC_CARD card, const void *spci, const unsigned char *sbuf, SC_DWORD slen,
                          void *rpci, unsigned char *rbuf, SC_DWORD *rlen)
{
    (void)card; (void)spci; (void)sbuf; (void)slen; (void)rpci; (void)rbuf;
    if (rlen) *rlen = 0;
    return SCARD_E_NO_SMARTCARD;
}

// The game resolves SCard* through dlsym, so both the import slots and the
// dlsym path must land on the stubs.
static const struct { const char *name; uintptr_t slot; void *fn; } scard[] = {
    {"SCardEstablishContext", 0x16d2c28, scEstablish},
    {"SCardReleaseContext", 0x16d3600, scRelease},
    {"SCardCancel", 0x16d2f70, scCancel},
    {"SCardListReaders", 0x16d2af8, scListReaders},
    {"SCardGetStatusChange", 0x16d3850, scGetStatusChange},
    {"SCardConnect", 0x16d3d08, scConnect},
    {"SCardDisconnect", 0x16d3c20, scDisconnect},
    {"SCardTransmit", 0x16d3cd0, scTransmit},
};

static void *mcdDlsym(const char *name)
{
    for (size_t i = 0; i < sizeof scard / sizeof scard[0]; i++)
        if (!strcmp(name, scard[i].name))
        {
            static int said[sizeof scard / sizeof scard[0]];
            if (!said[i]++) g7Log("minecraft: dlsym %s -> PC/SC stub\n", name);
            return scard[i].fn;
        }
    return NULL;
}

static void installPcsc(void)
{
    int ok = 0;
    for (size_t i = 0; i < sizeof scard / sizeof scard[0]; i++)
    {
        *(void **)scard[i].slot = scard[i].fn;
        if (*(void **)scard[i].slot == scard[i].fn) ok++;
    }
    g7GameDlsymHook = mcdDlsym;
    g7Log("minecraft: PC/SC card reader stub installed (%d/%zu slots + dlsym hook)\n",
          ok, sizeof scard / sizeof scard[0]);
}

void g7MinecraftInstall(const G7Game *game)
{
    (void)game;
    installPcsc();
    openKeyboards();
    if (!kbdCount)
    {
        g7Log("minecraft: no evdev keyboard found (need read on /dev/input/event*); no keyboard input\n");
        return;
    }
    if (!makePad())
        return;
    pthread_t t;
    pthread_create(&t, NULL, poller, NULL);
    pthread_detach(t);
}
