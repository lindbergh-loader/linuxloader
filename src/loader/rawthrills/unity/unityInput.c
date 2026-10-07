// What the players hold (see unity.h), from one thread:
//
// - [Input] INPUT_MODE 2: the devices of the [EVDEV] section, as the
//   generator writes it for the other gun games: ANALOGUE_1/2 P1's aim (X,
//   Y), ANALOGUE_3/4 P2's, each "<device>:ABS:<code>" (a gun, a touchpad:
//   a position) or "<device>:REL:<code>" (a mouse: moves); and
//   PLAYER_<n>_BUTTON_1 (trigger), _BUTTON_2 (the gun's shoulder button),
//   _BUTTON_START, _COIN, TEST_BUTTON, PLAYER_1_BUTTON_SERVICE and
//   PLAYER_1_BUTTON_UP/DOWN (volume, the test menu's moves), each
//   "<device>:KEY:<code>", a comma-separated list for several.
// - otherwise the desktop: the mouse in the game's window is P1's gun (left
//   button the trigger, right the shoulder button), 5/6 the coins, 1/2 the
//   starts, F2 test (held a moment, the test menu), F1 service, Page Up/Down
//   the volume (Nerf's keys; each game has its own).
//
// Either way Esc quits, while the game's window has the keyboard.
//
// Each game names its own switches (UnityGame.switches, .keys) and may
// change a sample (.evdevState, .desktopState): Superbikes 3 rides with
// ANALOGUE_1..3 and the arrows, the mouse is a gun in Nerf only (.mouseGun).
//
// A source is "<device>:KEY:<code>", "<device>:ABS:<code>" (ABS_NEG: the
// axis reversed), "<device>:REL:<code>", or for a switch an axis pushed to
// one end, "<device>:ABS:<code>:MIN" or ":MAX" (a d-pad, a stick), as the
// generator writes them.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "unity.h"

#define MAX_DEVICES 16
#define MAX_SOURCES 4
#define POLL_MS UNITY_POLL_MS

// An evdev source: a device's key, or one of its axes.
typedef struct
{
    int device; // index in devices, -1: none
    int type;   // EV_KEY, EV_ABS, EV_REL
    int code;
    int reversed; // ABS_NEG
    int end;      // a switch on an axis: -1 its minimum, 1 its maximum
} Source;

static struct
{
    char path[256];
    int fd;
    uint8_t keys[KEY_CNT];
    int abs[ABS_CNT];
    struct input_absinfo absInfo[ABS_CNT];
    float rel[REL_CNT]; // a mouse's position, 0..1
} devices[MAX_DEVICES];
static int deviceCount;

static Source switchSources[UNITY_NUM_INPUTS][MAX_SOURCES];
static Source analogSources[4]; // ANALOGUE_1..4
static int evdevMode;
static int relSpan[2] = {1280, 720};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static UnityInput state;

static int deviceIndex(const char *path)
{
    for (int i = 0; i < deviceCount; i++)
        if (!strcmp(devices[i].path, path))
            return i;
    if (deviceCount == MAX_DEVICES)
        return -1;
    snprintf(devices[deviceCount].path, sizeof(devices[0].path), "%s", path);
    devices[deviceCount].fd = -1;
    for (int c = 0; c < REL_CNT; c++)
        devices[deviceCount].rel[c] = 0.5f;
    return deviceCount++;
}

// "<device>:<KEY|ABS|ABS_NEG|REL>:<code>[:MIN|:MAX]"
static int parseSource(const char *text, Source *s)
{
    char copy[300], path[256], type[8];
    char *c2, *c1;
    s->end = 0;
    s->reversed = 0;
    snprintf(copy, sizeof(copy), "%s", text);
    c2 = strrchr(copy, ':');
    if (c2 && (!strcmp(c2, ":MIN") || !strcmp(c2, ":MAX")))
    {
        s->end = c2[3] == 'N' ? -1 : 1;
        *c2 = 0;
        c2 = strrchr(copy, ':');
    }
    if (!c2 || c2 == copy)
        return 0;
    for (c1 = c2 - 1; c1 > copy && *c1 != ':'; c1--)
        ;
    if (c1 == copy || c2 - c1 - 1 >= (int)sizeof(type) || c1 - copy >= (int)sizeof(path))
        return 0;
    memcpy(path, copy, c1 - copy);
    path[c1 - copy] = 0;
    memcpy(type, c1 + 1, c2 - c1 - 1);
    type[c2 - c1 - 1] = 0;
    s->code = atoi(c2 + 1);
    s->reversed = !strcmp(type, "ABS_NEG");
    s->type = !strcmp(type, "KEY")                       ? EV_KEY
              : !strcmp(type, "ABS") || s->reversed ? EV_ABS
              : !strcmp(type, "REL")                     ? EV_REL
                                                         : -1;
    if (s->end && s->type != EV_ABS)
        return 0;
    if (s->type < 0 || s->code < 0 || (s->type == EV_KEY && s->code >= KEY_CNT) ||
        (s->type == EV_ABS && s->code >= ABS_CNT) || (s->type == EV_REL && s->code >= REL_CNT))
        return 0;
    s->device = deviceIndex(path);
    return s->device >= 0;
}

static void mapSwitch(int sw, const char *key)
{
    const char *value = unityIniValue("EVDEV", key);
    char list[1024], *save, *tok;
    int n = 0;
    if (!value)
        return;
    while (n < MAX_SOURCES && switchSources[sw][n].device >= 0)
        n++;
    snprintf(list, sizeof(list), "%s", value);
    for (tok = strtok_r(list, ",", &save); tok && n < MAX_SOURCES; tok = strtok_r(NULL, ",", &save))
    {
        while (*tok == ' ')
            tok++;
        if (parseSource(tok, &switchSources[sw][n]) &&
            (switchSources[sw][n].type == EV_KEY || switchSources[sw][n].end))
            n++;
        else
        {
            switchSources[sw][n].device = -1;
            unityLog("[EVDEV] %s: not a key: %s\n", key, tok);
        }
    }
}

static void evdevConfig(void)
{
    const UnitySwitchKey *map = unityGame->switches;
    for (int i = 0; i < UNITY_NUM_INPUTS; i++)
        for (int j = 0; j < MAX_SOURCES; j++)
            switchSources[i][j].device = -1;
    for (; map && map->key; map++)
        mapSwitch(map->sw, map->key);
    for (int a = 0; a < 4; a++)
    {
        char key[16];
        const char *value;
        snprintf(key, sizeof(key), "ANALOGUE_%d", a + 1);
        analogSources[a].device = -1;
        if ((value = unityIniValue("EVDEV", key)) && !parseSource(value, &analogSources[a]))
            unityLog("[EVDEV] %s: not an axis: %s\n", key, value);
        if (analogSources[a].device >= 0 && (analogSources[a].type == EV_KEY || analogSources[a].end))
            analogSources[a].device = -1;
    }
    relSpan[0] = unityIniInt("Display", "WIDTH", 0) > 0 ? unityIniInt("Display", "WIDTH", 0) : 1280;
    relSpan[1] = unityIniInt("Display", "HEIGHT", 0) > 0 ? unityIniInt("Display", "HEIGHT", 0) : 720;
    for (int i = 0; i < deviceCount; i++)
    {
        if ((devices[i].fd = open(devices[i].path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
        {
            unityLog("cannot open %s\n", devices[i].path);
            continue;
        }
        for (int c = 0; c < ABS_CNT; c++)
            if (ioctl(devices[i].fd, EVIOCGABS(c), &devices[i].absInfo[c]) == 0)
                devices[i].abs[c] = devices[i].absInfo[c].value;
        unityLog("evdev: %s\n", devices[i].path);
    }
}

static void evdevRead(int i)
{
    struct input_event ev[64];
    ssize_t n;
    while ((n = read(devices[i].fd, ev, sizeof(ev))) > 0)
        for (size_t e = 0; e < (size_t)n / sizeof(ev[0]); e++)
        {
            int code = ev[e].code;
            if (ev[e].type == EV_KEY && code < KEY_CNT)
                devices[i].keys[code] = ev[e].value != 0;
            else if (ev[e].type == EV_ABS && code < ABS_CNT)
                devices[i].abs[code] = ev[e].value;
            else if (ev[e].type == EV_REL && code < REL_CNT)
            {
                // A count moves the aim a pixel of the screen.
                float p = devices[i].rel[code] + (float)ev[e].value / relSpan[code == REL_Y];
                devices[i].rel[code] = p < 0.f ? 0.f : p > 1.f ? 1.f : p;
            }
        }
}

static float axisValue(const Source *s)
{
    const struct input_absinfo *info;
    if (s->device < 0 || devices[s->device].fd < 0)
        return -1.f;
    if (s->type == EV_REL)
        return devices[s->device].rel[s->code];
    info = &devices[s->device].absInfo[s->code];
    if (info->maximum <= info->minimum)
        return -1.f;
    float v = (float)(devices[s->device].abs[s->code] - info->minimum) / (float)(info->maximum - info->minimum);
    v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
    return s->reversed ? 1.f - v : v;
}

static int switchHeld(const Source *s)
{
    if (s->device < 0)
        return 0;
    if (s->type == EV_KEY)
        return devices[s->device].keys[s->code];
    // An axis pushed past half way to its end.
    float v = axisValue(s);
    return v >= 0.f && (s->end < 0 ? v < 0.25f : v > 0.75f);
}

static void evdevState(UnityInput *in)
{
    for (int sw = 0; sw < UNITY_NUM_INPUTS; sw++)
        for (int j = 0; j < MAX_SOURCES; j++)
        {
            if (switchHeld(&switchSources[sw][j]))
                in->switches[sw] = 1;
        }
    for (int a = 0; a < 4; a++)
        in->analog[a] = axisValue(&analogSources[a]);
    if (unityGame->evdevState)
        unityGame->evdevState(in);
}

// ---------------------------------------------------------------------------
// The desktop: the X server, through a connection of our own.

static struct
{
    void *(*openDisplay)(const char *);
    unsigned long (*defaultRoot)(void *);
    int (*queryKeymap)(void *, char[32]);
    unsigned char (*keysymToKeycode)(void *, unsigned long);
    int (*getInputFocus)(void *, unsigned long *, int *);
    int (*queryTree)(void *, unsigned long, unsigned long *, unsigned long *, unsigned long **, unsigned int *);
    int (*queryPointer)(void *, unsigned long, unsigned long *, unsigned long *, int *, int *, int *, int *,
                        unsigned int *);
    int (*getGeometry)(void *, unsigned long, unsigned long *, int *, int *, unsigned int *, unsigned int *,
                       unsigned int *, unsigned int *);
    unsigned long (*internAtom)(void *, const char *, int);
    int (*getWindowProperty)(void *, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long *,
                             int *, unsigned long *, unsigned long *, unsigned char **);
    int (*freeData)(void *);
    void *display;
    unsigned long pidAtom;
    int (*previousHandler)(void *, void *);
    volatile int error;
} x;

// Xlib's default error handler ends the process: errors on our connection
// (a window the game has just destroyed, when it changes its size) only
// make the call fail, the game's go to its own handler.
static int xError(void *display, void *event)
{
    if (display == x.display)
    {
        x.error = 1;
        return 0;
    }
    return x.previousHandler ? x.previousHandler(display, event) : 0;
}

static int xLoad(void)
{
    void *lib = dlopen("libX11.so.6", RTLD_NOW);
    int (*(*setErrorHandler)(int (*)(void *, void *)))(void *, void *);
    if (!lib)
        return 0;
    *(void **)&setErrorHandler = dlsym(lib, "XSetErrorHandler");
    if (!setErrorHandler)
        return 0;
    *(void **)&x.openDisplay = dlsym(lib, "XOpenDisplay");
    *(void **)&x.defaultRoot = dlsym(lib, "XDefaultRootWindow");
    *(void **)&x.queryKeymap = dlsym(lib, "XQueryKeymap");
    *(void **)&x.keysymToKeycode = dlsym(lib, "XKeysymToKeycode");
    *(void **)&x.getInputFocus = dlsym(lib, "XGetInputFocus");
    *(void **)&x.queryTree = dlsym(lib, "XQueryTree");
    *(void **)&x.queryPointer = dlsym(lib, "XQueryPointer");
    *(void **)&x.getGeometry = dlsym(lib, "XGetGeometry");
    *(void **)&x.internAtom = dlsym(lib, "XInternAtom");
    *(void **)&x.getWindowProperty = dlsym(lib, "XGetWindowProperty");
    *(void **)&x.freeData = dlsym(lib, "XFree");
    if (!x.openDisplay || !x.defaultRoot || !x.queryKeymap || !x.keysymToKeycode || !x.getInputFocus ||
        !x.queryTree || !x.queryPointer || !x.getGeometry || !x.internAtom || !x.getWindowProperty ||
        !x.freeData || !(x.display = x.openDisplay(NULL)))
        return 0;
    x.previousHandler = setErrorHandler(xError);
    x.pidAtom = x.internAtom(x.display, "_NET_WM_PID", 0);
    return 1;
}

static int ownWindow(unsigned long window)
{
    unsigned long type, nitems, after;
    unsigned char *data = NULL;
    int format, ours = 0;
    if (x.getWindowProperty(x.display, window, x.pidAtom, 0, 1, 0, 6 /* XA_CARDINAL */, &type, &format, &nitems,
                            &after, &data) == 0 && data)
    {
        // Xlib returns 32-bit properties as longs.
        ours = nitems == 1 && (pid_t)*(unsigned long *)data == getpid();
        x.freeData(data);
    }
    return ours;
}

// The game's window: the largest of ours among the root's children and
// theirs (a window manager's frames hold the clients).
static unsigned long findGameWindow(unsigned long window, int depth, unsigned int *bestArea)
{
    unsigned long root, parent, *children = NULL, best = 0;
    unsigned int n;
    if (ownWindow(window))
    {
        unsigned long r;
        int wx, wy;
        unsigned int w, h, bw, d;
        if (x.getGeometry(x.display, window, &r, &wx, &wy, &w, &h, &bw, &d) && w * h > *bestArea)
        {
            *bestArea = w * h;
            best = window;
        }
    }
    if (depth < 3 && x.queryTree(x.display, window, &root, &parent, &children, &n))
    {
        for (unsigned int i = 0; i < n; i++)
        {
            unsigned long found = findGameWindow(children[i], depth + 1, bestArea);
            if (found)
                best = found;
        }
        if (children)
            x.freeData(children);
    }
    return best;
}

static int gameFocused(void)
{
    unsigned long window, root, parent, *children;
    unsigned int n;
    int revert;
    x.getInputFocus(x.display, &window, &revert);
    if (window <= 1) // None or PointerRoot (gamescope): nobody else has it
        return 1;
    for (int depth = 0; window > 1 && depth < 16; depth++)
    {
        if (ownWindow(window))
            return 1;
        if (!x.queryTree(x.display, window, &root, &parent, &children, &n))
            break;
        if (children)
            x.freeData(children);
        if (parent == root)
            break;
        window = parent;
    }
    return 0;
}

static int keyDown(const char keys[32], unsigned char code)
{
    return code && (keys[code / 8] & (1 << (code % 8)));
}

int unityKeysymDown(const char keys[32], unsigned long keysym)
{
    return x.display && keyDown(keys, x.keysymToKeycode(x.display, keysym));
}

static void desktopKeys(UnityInput *in, const char keys[32])
{
    const UnitySwitchKeysym *map = unityGame->keys;
    for (const UnitySwitchKeysym *k = map; k && (k->keysym || k->sw); k++)
        if (unityKeysymDown(keys, k->keysym))
            in->switches[k->sw] = 1;
}

static void desktopState(UnityInput *in, const char keys[32])
{
    static unsigned long gameWindow;
    static int searches;
    unsigned long root, child, r;
    int rx, ry, wx, wy, gx, gy;
    unsigned int mask, w, h, bw, d, area = 0;

    desktopKeys(in, keys);
    if (unityGame->desktopState)
        unityGame->desktopState(in, &state, keys);
    if (!unityGame->mouseGun)
        return;
    // Looked for again now and then: the game makes its window late.
    if (!gameWindow && searches++ % 64 == 0)
        gameWindow = findGameWindow(x.defaultRoot(x.display), 0, &area);
    if (!gameWindow)
        return;
    x.error = 0;
    if (!x.queryPointer(x.display, gameWindow, &root, &child, &rx, &ry, &wx, &wy, &mask) ||
        !x.getGeometry(x.display, gameWindow, &r, &gx, &gy, &w, &h, &bw, &d) || x.error || !w || !h)
    {
        gameWindow = 0; // gone (a new one when the game goes fullscreen)
        return;
    }
    if (wx >= 0 && wy >= 0 && wx < (int)w && wy < (int)h)
    {
        in->analog[0] = (float)wx / (float)(w - 1);
        in->analog[1] = (float)wy / (float)(h - 1);
    }
    else
    {
        // Off the window: where it left.
        in->analog[0] = state.analog[0];
        in->analog[1] = state.analog[1];
    }
    if (mask & (1 << 8)) // Button1Mask
        in->switches[RIO_GEAR3_SW] = 1;
    if (mask & (1 << 10)) // Button3Mask
        in->switches[RIO_GEAR1_SW] = 1;
}

// ---------------------------------------------------------------------------

static void *inputThread(void *arg)
{
    int haveX = xLoad();
    unsigned char escape = haveX ? x.keysymToKeycode(x.display, 0xff1b) : 0;
    (void)arg;
    if (!haveX)
        unityLog("no X display: no desktop input, Esc does not quit\n");
    for (;;)
    {
        struct pollfd fds[MAX_DEVICES];
        int nfds = 0;
        char keys[32] = {0};
        int focused = haveX && (x.queryKeymap(x.display, keys), gameFocused());
        UnityInput in;

        if (focused && keyDown(keys, escape))
        {
            unityLog("quit from the keyboard\n");
            _exit(0);
        }
        memset(&in, 0, sizeof(in));
        for (int a = 0; a < 4; a++)
            in.analog[a] = -1.f;
        if (evdevMode)
        {
            for (int i = 0; i < deviceCount; i++)
                if (devices[i].fd >= 0)
                {
                    fds[nfds].fd = devices[i].fd;
                    fds[nfds++].events = POLLIN;
                }
            poll(fds, nfds, POLL_MS);
            for (int i = 0; i < deviceCount; i++)
                if (devices[i].fd >= 0)
                    evdevRead(i);
            evdevState(&in);
            if (focused)
                desktopKeys(&in, keys);
        }
        else
        {
            if (focused)
                desktopState(&in, keys);
            else if (haveX)
            {
                in.analog[0] = state.analog[0];
                in.analog[1] = state.analog[1];
            }
            usleep(POLL_MS * 1000);
        }
        pthread_mutex_lock(&lock);
        state = in;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

void unityInputInit(void)
{
    static int started;
    pthread_t thread;
    if (started++)
        return;
    evdevMode = unityIniInt("Input", "INPUT_MODE", 1) == 2;
    for (int a = 0; a < 4; a++)
        state.analog[a] = -1.f;
    if (evdevMode)
        evdevConfig();
    unityLog("input: %s\n", evdevMode ? "evdev" : "desktop mouse and keyboard");
    if (pthread_create(&thread, NULL, inputThread, NULL) == 0)
        pthread_detach(thread);
}

const char *unityAnalogDevice(int a)
{
    const Source *s = &analogSources[a];
    return evdevMode && a >= 0 && a < 4 && s->device >= 0 ? devices[s->device].path : NULL;
}

void unityInputSample(UnityInput *in)
{
    pthread_mutex_lock(&lock);
    *in = state;
    pthread_mutex_unlock(&lock);
}
