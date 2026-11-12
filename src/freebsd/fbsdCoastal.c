// Coastal Amusements' I/O board (Temple Run), on the cabinet's serial port
// (/dev/cuau0), stood in for at its bytes: the game's CoastalBoard writes a
// command (its byte, its data) and reads the board's answer, of the length
// it queued with the command (read back from its queue).
//
//   0x80 reset         -> '*' (then the game asks 0xae and 0x84)
//   0xae revision      -> minor, major
//   0x81 next event    -> an event byte, '~' none:
//                         0x4c-0x4f / 0x6c-0x6f a level switch on / off
//                         (bits 0-3), 'A' start, 'B' the menu, 0x81-0x87 a
//                         coin slot then 0x89 a coin in it
//   0x94 tickets owed  -> 2 bytes (0)
//   0x95 dispenser     -> status (0: ready)
//   0x98 NV write      -> '*' (data: length, address, 16-bit each, bytes)
//   0x99 NV read       -> the bytes, '*' (32 KiB, the game directory's
//                         coastal.nv)
//   0x9a security      -> byte i of the security block (data: i), see below
//   others             -> '*' (lamps, meters, the dispenser, bills)
//
// The security check: the board's 128-byte block, RC4'd with the disk's
// IDENTIFY data (fbsdLibc.c's) as the key, must be the last 128 bytes of
// dot.tga. The board's block is that trailer RC4'd back, with the game's own
// RC4.

#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fbsdCoastal.h"

#define NV_SIZE 0x8000
#define OUT_SIZE 4096
#define EVENTS 64

// Temple Run 1.8.3-R's CoastalBoard: its command queue (entries: length,
// answer length, command, data) and read position, its RC4.
#define QUEUE 0x08171e40u
#define QUEUE_HEAD 0x08171f41u
#define RC4_INIT 0x081109b0u
#define RC4_CRYPT 0x08110a20u

static int boardFd = -1;
static uint8_t out[OUT_SIZE];
static int outLength;
static uint8_t nv[NV_SIZE];
static uint8_t security[128];
static int securityReady;
static uint8_t events[EVENTS];
static int eventHead, eventCount;
static uint8_t levels; // the level switches' bits as the game last heard
static int trace;

static void answer(const uint8_t *bytes, int length)
{
    if (outLength + length > OUT_SIZE)
        return;
    memcpy(out + outLength, bytes, (size_t)length);
    outLength += length;
}

static void event(uint8_t byte)
{
    if (trace)
        fprintf(stderr, "coastal: event %02x\n", byte);
    if (eventCount < EVENTS)
        events[(eventHead + eventCount++) % EVENTS] = byte;
}

static void saveNv(void)
{
    FILE *f = fopen("coastal.nv", "wb");
    if (f)
    {
        fwrite(nv, 1, sizeof(nv), f);
        fclose(f);
    }
}

// The security block: dot.tga's trailer RC4'd with the disk's key (RC4 is
// its own inverse).
static void makeSecurity(void)
{
    uint8_t state[0x110], key[512];
    void (*rc4Init)(void *, const void *, int) = (void *)(uintptr_t)RC4_INIT;
    void (*rc4Crypt)(void *, const void *, void *, int) = (void *)(uintptr_t)RC4_CRYPT;
    FILE *f = fopen("dot.tga", "rb");

    securityReady = 1;
    if (!f)
        return;
    fseek(f, -128, SEEK_END);
    if (fread(security, 1, sizeof(security), f) != sizeof(security))
        memset(security, 0, sizeof(security));
    fclose(f);
    fbsdDiskIdentity(key);
    rc4Init(state, key, 0x100);
    rc4Crypt(state, security, security, sizeof(security));
}

// SDL 1.2's keys (the game's SDL, the host's sdl12-compat).
enum
{
    KEY_RETURN = 13,
    KEY_1 = 49,
    KEY_5 = 53,
    KEY_A = 97,
    KEY_D = 100,
    KEY_S = 115,
    KEY_W = 119,
    KEY_UP = 273,
    KEY_DOWN = 274,
    KEY_RIGHT = 275,
    KEY_LEFT = 276,
    KEY_F2 = 283,
};

// The first gamepad, through the SDL2 under sdl12-compat (its game
// controller layout): d-pad or left stick, A / Start, Back, R3.
enum
{
    PAD_A = 0,
    PAD_BACK = 4,
    PAD_START = 6,
    PAD_RIGHTSTICK = 8,
    PAD_UP = 11,
    PAD_DOWN = 12,
    PAD_LEFT = 13,
    PAD_RIGHT = 14,
    PAD_STICK = 16000,
};

static struct
{
    int (*initSubSystem)(uint32_t);
    int (*numJoysticks)(void);
    int (*isGameController)(int);
    void *(*open)(int);
    void (*close)(void *);
    int (*attached)(void *);
    void (*update)(void);
    uint8_t (*button)(void *, int);
    int16_t (*axis)(void *, int);
    void *pad;
    time_t nextLook;
    int loaded;
} sdl2;

static void *gamepad(void)
{
    if (!sdl2.loaded)
    {
        void *lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
        if (!lib)
            lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW);
        sdl2.loaded = -1;
        if (lib)
        {
            *(void **)&sdl2.initSubSystem = dlsym(lib, "SDL_InitSubSystem");
            *(void **)&sdl2.numJoysticks = dlsym(lib, "SDL_NumJoysticks");
            *(void **)&sdl2.isGameController = dlsym(lib, "SDL_IsGameController");
            *(void **)&sdl2.open = dlsym(lib, "SDL_GameControllerOpen");
            *(void **)&sdl2.close = dlsym(lib, "SDL_GameControllerClose");
            *(void **)&sdl2.attached = dlsym(lib, "SDL_GameControllerGetAttached");
            *(void **)&sdl2.update = dlsym(lib, "SDL_GameControllerUpdate");
            *(void **)&sdl2.button = dlsym(lib, "SDL_GameControllerGetButton");
            *(void **)&sdl2.axis = dlsym(lib, "SDL_GameControllerGetAxis");
            if (sdl2.initSubSystem && sdl2.numJoysticks && sdl2.isGameController && sdl2.open && sdl2.close &&
                sdl2.attached && sdl2.update && sdl2.button && sdl2.axis &&
                sdl2.initSubSystem(0x2000 /* SDL_INIT_GAMECONTROLLER */) == 0)
                sdl2.loaded = 1;
        }
        if (sdl2.loaded < 0)
            fprintf(stderr, "coastal: no SDL2 under sdl12-compat, no gamepad\n");
    }
    if (sdl2.loaded < 0)
        return NULL;
    sdl2.update();
    if (sdl2.pad && !sdl2.attached(sdl2.pad))
    {
        sdl2.close(sdl2.pad);
        sdl2.pad = NULL;
    }
    // (looked for once a second: plugged in later)
    if (!sdl2.pad && time(NULL) >= sdl2.nextLook)
    {
        sdl2.nextLook = time(NULL) + 1;
        for (int i = 0; i < sdl2.numJoysticks() && !sdl2.pad; i++)
            if (sdl2.isGameController(i))
                sdl2.pad = sdl2.open(i);
        if (sdl2.pad)
            fprintf(stderr, "coastal: gamepad opened\n");
    }
    return sdl2.pad;
}

// The host's controls, as the board's events: the arrows (and WASD) the
// level switches, Return / 1 start, F2 the menu, 5 a coin; the gamepad's
// d-pad or left stick, A or Start, R3, Back.
static void pollInputs(void)
{
    static int startWas, menuWas, coinWas;
    static void (*pumpEvents)(void);
    static uint8_t *(*getKeyState)(int *);
    int count = 0;
    uint8_t *keys;
    uint8_t now = 0;
    int start, menu, coin;

    if (!getKeyState)
    {
        *(void **)&pumpEvents = dlsym(RTLD_DEFAULT, "SDL_PumpEvents");
        *(void **)&getKeyState = dlsym(RTLD_DEFAULT, "SDL_GetKeyState");
        if (!pumpEvents || !getKeyState)
            return;
    }
    pumpEvents();
    keys = getKeyState(&count);
    if (!keys || count <= KEY_F2)
        return;
    if (keys[KEY_LEFT] || keys[KEY_A])
        now |= 1;
    if (keys[KEY_RIGHT] || keys[KEY_D])
        now |= 2;
    if (keys[KEY_UP] || keys[KEY_W])
        now |= 4;
    if (keys[KEY_DOWN] || keys[KEY_S])
        now |= 8;
    void *pad = gamepad();
    if (pad)
    {
        int x = sdl2.axis(pad, 0), y = sdl2.axis(pad, 1);
        if (sdl2.button(pad, PAD_LEFT) || x < -PAD_STICK)
            now |= 1;
        if (sdl2.button(pad, PAD_RIGHT) || x > PAD_STICK)
            now |= 2;
        if (sdl2.button(pad, PAD_UP) || y < -PAD_STICK)
            now |= 4;
        if (sdl2.button(pad, PAD_DOWN) || y > PAD_STICK)
            now |= 8;
    }
    for (int bit = 0; bit < 4; bit++)
        if ((now ^ levels) & (1 << bit))
            event((uint8_t)((now & (1 << bit) ? 0x4c : 0x6c) + bit));
    levels = now;
    start = keys[KEY_RETURN] || keys[KEY_1] || (pad && (sdl2.button(pad, PAD_A) || sdl2.button(pad, PAD_START)));
    menu = keys[KEY_F2] || (pad && sdl2.button(pad, PAD_RIGHTSTICK));
    coin = keys[KEY_5] || (pad && sdl2.button(pad, PAD_BACK));
    if (start && !startWas)
        event('A');
    if (menu && !menuWas)
        event('B');
    if (coin && !coinWas)
    {
        event(0x81);
        event(0x89);
    }
    startWas = start;
    menuWas = menu;
    coinWas = coin;
}

// COASTAL_EVENTS=seconds:byte[,...] (debugging, hexadecimal): the board's
// event then (from the board's start).
static void debugEvents(void)
{
    static struct timespec start;
    static int done;
    struct timespec now;
    double t;
    int i = 0;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!start.tv_sec)
        start = now;
    t = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
    for (const char *list = getenv("COASTAL_EVENTS"); list && *list; i++)
    {
        double at;
        unsigned int byte;
        if (sscanf(list, "%lf:%x", &at, &byte) == 2 && t >= at && i >= done)
        {
            event((uint8_t)byte);
            done = i + 1;
        }
        list = strchr(list, ',');
        list = list ? list + 1 : NULL;
    }
}

static void command(const uint8_t *bytes, int length)
{
    const uint8_t *queue = (const uint8_t *)(uintptr_t)QUEUE;
    uint8_t head = *(const uint8_t *)(uintptr_t)QUEUE_HEAD;
    int wanted = queue[(uint8_t)(head + 1)];
    uint8_t cmd = bytes[0], reply[300];
    const uint8_t *data = bytes + 1;
    static const uint8_t ok = '*';

    if (trace)
    {
        fprintf(stderr, "coastal: command %02x (%d bytes, answer %d)\n", cmd, length, wanted);
    }
    memset(reply, '*', sizeof(reply));
    switch (cmd)
    {
    case 0x81:
        pollInputs();
        debugEvents();
        if (eventCount)
        {
            reply[0] = events[eventHead];
            eventHead = (eventHead + 1) % EVENTS;
            eventCount--;
        }
        else
            reply[0] = '~';
        break;
    case 0xae:
        reply[0] = 3;
        reply[1] = 1;
        break;
    case 0x94:
        reply[0] = reply[1] = 0;
        break;
    case 0x95:
        reply[0] = 0;
        break;
    case 0x99:
        if (length >= 5)
        {
            int n = data[0] << 8 | data[1], address = data[2] << 8 | data[3];
            for (int i = 0; i < n && i + 1 < (int)sizeof(reply); i++)
                reply[i] = nv[(address + i) % NV_SIZE];
        }
        break;
    case 0x98:
        if (length >= 5)
        {
            int n = data[0] << 8 | data[1], address = data[2] << 8 | data[3];
            for (int i = 0; i < n && 5 + i < length; i++)
                nv[(address + i) % NV_SIZE] = data[4 + i];
            saveNv();
        }
        break;
    case 0x9a:
        if (!securityReady)
            makeSecurity();
        reply[0] = security[length >= 2 ? data[0] & 0x7f : 0];
        break;
    default:
        break;
    }
    if (wanted < 1 || wanted > (int)sizeof(reply))
        wanted = 1;
    // (the answer of a write, a reset, a lamp: '*' alone)
    if (wanted == 1 && cmd != 0x81 && cmd != 0x95 && cmd != 0x9a)
        reply[0] = ok;
    answer(reply, wanted);
}

int fbsdCoastalOpen(const char *path)
{
    FILE *f;

    if (!path || strcmp(path, "/dev/cuau0") != 0)
        return -2;
    if (boardFd < 0)
        boardFd = open("/dev/null", O_RDWR);
    f = fopen("coastal.nv", "rb");
    if (f)
    {
        if (fread(nv, 1, sizeof(nv), f) != sizeof(nv))
            fprintf(stderr, "coastal: coastal.nv short\n");
        fclose(f);
    }
    trace = getenv("COASTAL_TRACE") != NULL;
    return boardFd;
}

int fbsdCoastalIsFd(int fd)
{
    return fd >= 0 && fd == boardFd;
}

ssize_t fbsdCoastalWrite(const void *buf, size_t count)
{
    if (count)
        command(buf, (int)count);
    return (ssize_t)count;
}

ssize_t fbsdCoastalRead(void *buf, size_t count)
{
    int n = outLength < (int)count ? outLength : (int)count;

    memcpy(buf, out, (size_t)n);
    memmove(out, out + n, (size_t)(outLength - n));
    outLength -= n;
    return n;
}

void fbsdCoastalClose(void)
{
    boardFd = -1;
}
