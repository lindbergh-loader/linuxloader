// Target: Terror Gold (Raw Thrills, 2004 engine): its packed file system,
// its JAMMA board (a serial one), its window and clock, and crosshairs.
//
// Files: the game opens its data through RTFS, by a hash of the lowercased
// name ("/tt/<hash>", LZW-packed for RTFS_LZWfopen); the dump has them
// unpacked under data/, by name. RTFS_open, RTFS_fopen, RTFS_LZWfopen and
// RTFS_TranslateFilename open them there. Its settings, audits and hiscores
// are other /tt files (the descriptor's path aliases: save/).
//
// I/O: with no USB board (/dev/usb/jusb), the game talks to the JAMMA board
// on /dev/ttyS0, lines of text ("\n...\r"): it asks for the DIP switches
// ('s') and the version ('v') at its start, then a polling proc reads the
// board's reports, each frame until there is none:
//  - "S00" and 14 hex digits, 0 or 1, the switches: gun 1 and gun 2
//    triggers (0 while pulled), their reload buttons, starts, coins, volume
//    up and down, test, service, then one unused and the bill acceptor;
//  - "g" (gun 1) or "G" (gun 2), "0000", y and x as 4 hex digits each: the
//    gun's position, in the board's 640x480 space, which the game's gun
//    calibration (saved with its settings) takes to the screen. A trigger
//    pull waits 0.1 s for it, then counts as a shot off the screen (a
//    reload).
// The JAMMA functions are answered here: reports from the loader's input,
// read every few ms on the game's main thread (its procs are cooperative).
// Input: with evdev input (INPUT_MODE 2), the loader's JVS state, guns on
// ANALOGUE_1/2 and 3/4, BUTTON_1 the trigger, BUTTON_2 the reload button
// (also the player's start, as on the cabinet's guns); otherwise the
// desktop's (see common/desktopInput.h): the mouse is player 1's gun (a
// crosshair pointer over the window), left fires, right reloads, 1 or I
// start, 5 or U coin, F1 or P service, F2 test, up/down arrows the volume.
// The desktop keys also work with evdev.

#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <glad/gl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#include <SDL3/SDL_pixels.h>
#include <SDL3_image/SDL_image.h>

#include "rawthrills.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define TT_GUNS 2
#define TT_GUN_WIDTH 640
#define TT_GUN_HEIGHT 480
// The board's descriptor: anything but 0 or 1 (an error) does.
#define TT_JAMMA_FD 0x7454
#define TT_REPORT_SIZE 32
// The polling proc runs about 2000 times a second: the input is read at
// most this often.
#define TT_POLL_MS 4
// glutSetCursor's.
#define GLUT_CURSOR_CROSSHAIR 9
#define GLUT_CURSOR_NONE 101
#define GLUT_WINDOW_WIDTH 102
#define GLUT_WINDOW_HEIGHT 103

// ---------------------------------------------------------------------------
// Files

// RTFS's name: lowercased, backslashes as slashes, under data/.
static void dataPath(const char *name, char *path, size_t size)
{
    size_t n = (size_t)snprintf(path, size, "./data/%s", name ? name : "");
    for (char *p = path + 7; p < path + (n < size ? n : size - 1); p++)
        *p = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
}

static int rtfsOpen(const char *name)
{
    char path[512];
    dataPath(name, path, sizeof(path));
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        log_warn("Target: Terror: %s not found", path);
    return fd;
}

static FILE *rtfsFopen(const char *name, const char *mode)
{
    char path[512];
    dataPath(name, path, sizeof(path));
    FILE *f = fopen(path, mode);
    if (!f && mode && mode[0] == 'r')
        log_warn("Target: Terror: %s not found", path);
    return f;
}

// The path RTFS would open: out holds 156 bytes.
static void rtfsTranslateFilename(const char *name, char *out)
{
    char path[512];
    if (!name || !out)
        return;
    dataPath(name, path, sizeof(path));
    snprintf(out, 156, "%s", path);
}

// RTFS's unpacked file in memory, read by RTFS_fread, RTFS_fgetc...
// (text mode turns "\r\n" into "\n"), freed with its data by
// RTFS_LZWfclose.
typedef struct
{
    uint8_t binary;
    uint8_t eof;
    uint8_t pad[2];
    uint32_t size;
    uint32_t pos;
    uint8_t *data;
} RtfsFile;

static RtfsFile *rtfsLzwFopen(const char *name, const char *mode)
{
    char path[512];
    RtfsFile *file;
    FILE *f;
    long size;

    dataPath(name, path, sizeof(path));
    if (!(f = fopen(path, "rb")))
    {
        log_warn("Target: Terror: %s not found", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = calloc(1, sizeof(*file));
    if (file && size >= 0)
        file->data = malloc(size ? size : 1);
    if (!file || !file->data || fread(file->data, 1, size, f) != (size_t)size)
    {
        if (file)
            free(file->data);
        free(file);
        fclose(f);
        return NULL;
    }
    fclose(f);
    file->size = size;
    file->binary = mode && (mode[0] == 'b' || mode[0] == 'B' || (mode[0] && (mode[1] == 'b' || mode[1] == 'B')));
    return file;
}

// ---------------------------------------------------------------------------
// JAMMA board

// Reports waiting for the game's reads, each a length (its characters
// after the "\n") then the characters.
static uint8_t reports[8][TT_REPORT_SIZE];
static int reportCount, reportNext;
static int polled;

static void queueReport(const char *text)
{
    size_t n = strlen(text);
    if (reportCount >= (int)(sizeof(reports) / sizeof(reports[0])) || n + 1 >= TT_REPORT_SIZE)
        return;
    reports[reportCount][0] = (uint8_t)(n + 1);
    memcpy(&reports[reportCount][1], text, n);
    reports[reportCount][n + 1] = '\0';
    reportCount++;
}

// The board's switches, in their order in its "S" report.
enum
{
    SW_GUN1_TRIGGER,
    SW_GUN2_TRIGGER,
    SW_GUN1_RELOAD,
    SW_GUN2_RELOAD,
    SW_START1,
    SW_START2,
    SW_COIN1,
    SW_COIN2,
    SW_VOLUME_UP,
    SW_VOLUME_DOWN,
    SW_TEST,
    SW_SERVICE,
    SW_UNUSED,
    SW_BILL,
    SW_COUNT
};

static const RtIoInput ttSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, SW_GUN1_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, SW_GUN2_RELOAD, BUTTON_2},
    // The reload buttons also start, as on the cabinet's guns.
    {RT_IO_SWITCH, PLAYER_1, SW_START1, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, SW_START2, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, SW_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, SW_START2, BUTTON_START},
    {RT_IO_COIN, 0, SW_COIN1, 0},
    {RT_IO_COIN, 1, SW_COIN2, 0},
    {RT_IO_SWITCH, PLAYER_1, SW_VOLUME_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, SW_VOLUME_DOWN, BUTTON_DOWN},
    {RT_IO_SWITCH, SYSTEM, SW_TEST, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_END, 0, 0, 0},
};

// Each gun's aim, as a fraction of the picture from its top left, for the
// crosshairs (aimShown: on the screen).
static float aimX[TT_GUNS], aimY[TT_GUNS];
static int aimShown[TT_GUNS];

// A gun's position in the board's space; 0 off the screen.
static int gunPosition(JVSIO *io, int gun, int *x, int *y)
{
    float fx, fy;
    aimShown[gun] = rtJammaPosition(io, ANALOGUE_1 + 2 * gun, ANALOGUE_2 + 2 * gun, &fx, &fy);
    if (!aimShown[gun])
        return 0;
    aimX[gun] = fx;
    aimY[gun] = fy;
    *x = (int)(fx * TT_GUN_WIDTH);
    *y = (int)(fy * TT_GUN_HEIGHT);
    return 1;
}

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// The board's reports for the input as it is now: the switches when they
// changed, then the guns' positions when they moved (always after a
// trigger pull, which waits for one), but for a gun off the screen.
static void pollBoard(void)
{
    static char lastSwitches[SW_COUNT + 1];
    static int lastX[TT_GUNS] = {-1, -1}, lastY[TT_GUNS] = {-1, -1}, lastFire[TT_GUNS];
    int evdev = getConfig()->inputMode == 2;
    JVSIO *io = evdev ? getJVSIO() : desktopInputState();
    JVSIO *desktop = desktopKeyboardState();
    char switches[SW_COUNT + 1], text[TT_REPORT_SIZE];
    int fire[TT_GUNS];

    memset(switches, '0', SW_COUNT);
    switches[SW_COUNT] = '\0';
    for (const RtIoInput *in = ttSwitches; in->type != RT_IO_END; in++)
    {
        int held = rtIoSwitchState(in, io);
        // The desktop's keys work with evdev input too.
        if (evdev && in->type == RT_IO_SWITCH)
            held |= (desktop->state.inputSwitch[in->player] & in->source) != 0;
        if (held)
            switches[in->io] = '1';
    }
    for (int g = 0; g < TT_GUNS; g++)
    {
        fire[g] = (io->state.inputSwitch[PLAYER_1 + g] & BUTTON_1) != 0;
        // Pulled: 0.
        switches[SW_GUN1_TRIGGER + g] = fire[g] ? '0' : '1';
    }
    if (strcmp(switches, lastSwitches) != 0)
    {
        snprintf(text, sizeof(text), "S00%s", switches);
        queueReport(text);
        memcpy(lastSwitches, switches, sizeof(switches));
    }
    for (int g = 0; g < TT_GUNS; g++)
    {
        int x, y, pulled = fire[g] && !lastFire[g];
        lastFire[g] = fire[g];
        if (!gunPosition(io, g, &x, &y) || (!pulled && x == lastX[g] && y == lastY[g]))
            continue;
        lastX[g] = x;
        lastY[g] = y;
        snprintf(text, sizeof(text), "%c0000%04X%04X", g ? 'G' : 'g', y & 0x3ff, x & 0x7ff);
        queueReport(text);
    }
}

static int jammaOpen(int unused)
{
    (void)unused;
    log_info("Target: Terror: JAMMA board emulated");
    return TT_JAMMA_FD;
}

static void jammaClose(int fd)
{
    (void)fd;
}

// The board's DIP switches (mode 1: all off) and version (mode 2: 1.CC,
// the version byte first); 1: answered.
static int jammaGetValue(int fd, int *value, int mode)
{
    (void)fd;
    if (!value)
        return 0;
    *value = mode == 2 ? 0x01cc : 0;
    return 1;
}


// The next report (0), or none (1). Each run of the polling proc reads
// until there is none: the input is read at its first read.
static int jammaGetReport(int fd, uint8_t *report)
{
    static uint64_t lastPoll;
    (void)fd;
    if (reportNext >= reportCount)
    {
        reportCount = reportNext = 0;
        if (polled)
        {
            polled = 0;
            return 1;
        }
        polled = 1;
        uint64_t now = nowMs();
        if (now - lastPoll < TT_POLL_MS)
        {
            polled = 0;
            return 1;
        }
        lastPoll = now;
        pollBoard();
        if (!reportCount)
        {
            polled = 0;
            return 1;
        }
    }
    memcpy(report, reports[reportNext], reports[reportNext][0] + 1);
    reportNext++;
    return 0;
}

// The game's requests (coin meters, the watchdog, its settings): taken.
static int jammaSendReport(int fd, const char *text)
{
    (void)fd;
    (void)text;
    return 0;
}

static void jammaSendSerial(int fd)
{
    (void)fd;
}

// ---------------------------------------------------------------------------
// Clock

// The game's clock counts TSC ticks, at the rate its rtTime_Init takes from
// the "cpu MHz" of /proc/cpuinfo: the cabinet's fixed one, but on a recent
// CPU the first core's current frequency, which a boost or a power saving
// state makes another than the TSC's (the game ran slow, or fast, as it
// was at the start). The TSC's own rate instead, measured over 200 ms.
#define TT_TIME_TICKS 0x0814ede8 // uint64: TSC ticks per 1/1024 s
#define TT_TIME_START 0x0814ede0 // uint64: TSC at the start
#define TT_TIME_UNIT 0x0814edf0  // double: 1/1024
#define TT_TIME_RATE 0x0814edf8  // double: 1024

static double nowSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int timeInit(void)
{
    struct timespec delay = {0, 200000000};
    double start = nowSeconds();
    unsigned long long ticks = __rdtsc();

    nanosleep(&delay, NULL);
    ticks = __rdtsc() - ticks;
    double hz = ticks / (nowSeconds() - start);
    *(volatile uint64_t *)TT_TIME_TICKS = (uint64_t)hz >> 10;
    *(volatile uint64_t *)TT_TIME_START = __rdtsc();
    *(volatile double *)TT_TIME_UNIT = 1.0 / 1024;
    *(volatile double *)TT_TIME_RATE = 1024;
    log_info("Target: Terror: clock at the TSC's %.3f MHz", hz / 1e6);
    return 1;
}

// ---------------------------------------------------------------------------
// Window

static void (*windowViewport)(int, int, int, int);
static int (*glutGetReal)(int);

// A viewport of the window, through the window scaling's wrapper: a
// full-window one is the 4:3 picture in the middle of a wider window (black
// bars at the sides), which the guns are placed in (frameScale.h).
static void viewportOf(int x, int y, int width, int height)
{
    if (!windowViewport)
        *(void **)&windowViewport = frameScaleWrapper("glViewport");
    if (!windowViewport)
        *(void **)&windowViewport = dlsym(RTLD_NEXT, "glViewport");
    if (windowViewport)
        windowViewport(x, y, width, height);
}

// The game leaves its window to glut, whose default reshape fills it.
static void reshape(int width, int height)
{
    viewportOf(0, 0, width, height);
}

// The game's viewports are of its 640x480 screen: set once at its start,
// and, while the screen shakes (grenades, explosions), moved by the shake
// then put back at the end of it, which on a bigger window left the picture
// in its bottom left corner. Those are the window's picture, moved by as
// much of it; the others are left as they are.
static void gameViewport(int x, int y, int width, int height)
{
    int w = glutGetReal ? glutGetReal(GLUT_WINDOW_WIDTH) : 0, h = glutGetReal ? glutGetReal(GLUT_WINDOW_HEIGHT) : 0;
    const RtGame *game = rtCurrentGame();

    if (width != TT_GUN_WIDTH || height != TT_GUN_HEIGHT || w <= 0 || h <= 0)
    {
        viewportOf(x, y, width, height);
        return;
    }
    if (x == 0 && y == 0)
    {
        viewportOf(0, 0, w, h);
        return;
    }
    int pw = w, ph = h;
    if (game && game->frameAspect[0] && getConfig()->keepAspectRatio && (long)w * game->frameAspect[1] > (long)h * game->frameAspect[0])
        pw = h * game->frameAspect[0] / game->frameAspect[1];
    viewportOf((w - pw) / 2 + x * pw / TT_GUN_WIDTH, y * ph / TT_GUN_HEIGHT, pw, ph);
}

static void installReshape(void)
{
    void (*reshapeFunc)(void (*)(int, int)) = dlsym(RTLD_NEXT, "glutReshapeFunc");

    *(void **)&glutGetReal = dlsym(RTLD_NEXT, "glutGet");
    if (!reshapeFunc || !glutGetReal)
        return;
    reshapeFunc(reshape);
    reshape(glutGetReal(GLUT_WINDOW_WIDTH), glutGetReal(GLUT_WINDOW_HEIGHT));
}

// Called on the game's window, just opened (InitGLwindow). The game hides
// the pointer, which a light gun does not need: with the desktop's mouse for
// a gun, a crosshair pointer instead, unless the crosshairs are drawn.
static void setCursor(int cursor)
{
    static void (*real)(int);
    if (!real)
    {
        *(void **)&real = dlsym(RTLD_NEXT, "glutSetCursor");
        installReshape();
    }
    if (cursor == GLUT_CURSOR_NONE && getConfig()->inputMode != 2 && !getConfig()->enableCrosshairs)
        cursor = GLUT_CURSOR_CROSSHAIR;
    if (real)
        real(cursor);
}

void *rtTtOverride(const char *name)
{
    if (!strcmp(name, "glutSetCursor"))
        return (void *)setCursor;
    if (!strcmp(name, "glViewport"))
        return (void *)gameViewport;
    return NULL;
}

// ---------------------------------------------------------------------------
// Crosshairs

// The guns' crosshairs, which the cabinet has none of, with [CrossHairs]
// ENABLE_CROSSHAIRS: P1_CROSSHAIR_PATH and P2_CROSSHAIR_PATH, or the dump's
// P1.png and P2.png, CUSTOM_CROSSHAIRS_WIDTH x HEIGHT pixels of a 1280x960
// picture (the default 64: a 15th of the screen's height), centred on each
// gun's aim while it is on the screen. Drawn over the frame before the swap,
// the fixed-function way, the game's state put back.
static struct
{
    GLuint texture;
    int failed;
} crosshair[TT_GUNS];

static int loadCrosshair(int gun)
{
    const char *configured = gun ? getConfig()->p2CrossHairPath : getConfig()->p1CrossHairPath;
    char path[PATH_MAX];
    GLint bound = 0, alignment = 4;

    if (crosshair[gun].texture || crosshair[gun].failed)
        return crosshair[gun].texture != 0;
    if (configured[0] == '/')
        snprintf(path, sizeof(path), "%s", configured);
    else if (configured[0])
        snprintf(path, sizeof(path), "%s/%s", rtGameDir(), configured);
    else
        snprintf(path, sizeof(path), "%s/P%d.png", rtGameDir(), gun + 1);
    SDL_Surface *loaded = IMG_Load(path);
    SDL_Surface *rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : NULL;
    if (loaded)
        SDL_DestroySurface(loaded);
    if (!rgba)
    {
        log_warn("Target: Terror: no crosshair for player %d (%s)", gun + 1, path);
        crosshair[gun].failed = 1;
        return 0;
    }
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glad_glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glad_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glad_glGenTextures(1, &crosshair[gun].texture);
    glad_glBindTexture(GL_TEXTURE_2D, crosshair[gun].texture);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glad_glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    SDL_DestroySurface(rgba);
    return 1;
}

// x, y, width, height: the window's drawable area, from its bottom left.
void rtTtFrameDraw(int x, int y, int width, int height)
{
    int pw = width, ph = height, shown = 0;
    GLint activeTexture = GL_TEXTURE0, program = 0;

    if (!getConfig()->enableCrosshairs)
        return;
    for (int g = 0; g < TT_GUNS; g++)
        shown |= aimShown[g];
    if (!shown)
        return;
    // The picture in the area: 4:3 in the middle of it (see reshape).
    const RtGame *game = rtCurrentGame();
    if (game->frameAspect[0] && getConfig()->keepAspectRatio)
    {
        if ((long)pw * game->frameAspect[1] > (long)ph * game->frameAspect[0])
            pw = ph * game->frameAspect[0] / game->frameAspect[1];
        else
            ph = pw * game->frameAspect[1] / game->frameAspect[0];
    }
    float halfW = getConfig()->customCrossHairWidth * pw / (4.f * TT_GUN_WIDTH);
    float halfH = getConfig()->customCrossHairHeight * ph / (4.f * TT_GUN_HEIGHT);

    glad_glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (glad_glUseProgram)
    {
        glad_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glad_glUseProgram(0);
    }
    if (glad_glActiveTexture)
    {
        glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glad_glActiveTexture(GL_TEXTURE0);
    }
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glOrtho(0, width, 0, height, -1, 1);
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPushMatrix();
    glad_glLoadIdentity();
    glad_glViewport(x, y, width, height);
    glad_glDisable(GL_DEPTH_TEST);
    glad_glDisable(GL_STENCIL_TEST);
    glad_glDisable(GL_SCISSOR_TEST);
    glad_glDisable(GL_ALPHA_TEST);
    glad_glDisable(GL_CULL_FACE);
    glad_glDisable(GL_LIGHTING);
    glad_glDisable(GL_FOG);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glad_glEnable(GL_TEXTURE_2D);
    glad_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glad_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    for (int g = 0; g < TT_GUNS; g++)
    {
        if (!aimShown[g] || !loadCrosshair(g))
            continue;
        float cx = (width - pw) / 2.f + aimX[g] * pw, cy = (height - ph) / 2.f + (1.f - aimY[g]) * ph;
        glad_glBindTexture(GL_TEXTURE_2D, crosshair[g].texture);
        glad_glBegin(GL_QUADS);
        glad_glTexCoord2f(0, 1);
        glad_glVertex2f(cx - halfW, cy - halfH);
        glad_glTexCoord2f(1, 1);
        glad_glVertex2f(cx + halfW, cy - halfH);
        glad_glTexCoord2f(1, 0);
        glad_glVertex2f(cx + halfW, cy + halfH);
        glad_glTexCoord2f(0, 0);
        glad_glVertex2f(cx - halfW, cy + halfH);
        glad_glEnd();
    }
    glad_glMatrixMode(GL_MODELVIEW);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_PROJECTION);
    glad_glPopMatrix();
    glad_glMatrixMode(GL_TEXTURE);
    glad_glPopMatrix();
    if (glad_glActiveTexture)
        glad_glActiveTexture((GLenum)activeTexture);
    if (glad_glUseProgram)
        glad_glUseProgram((GLuint)program);
    glad_glPopAttrib();
}

void rtTtInstall(const RtGame *game)
{
    // A 4:3 game: in the middle of a wider window (see reshape), unless
    // [Display] KEEP_ASPECT_RATIO 0 stretches it.
    if (game->frameAspect[0] && getConfig()->keepAspectRatio)
        frameScaleSetAspect(game->frameAspect[0], game->frameAspect[1]);
    if (rtDetour("rtTime_Init", timeInit) != 0)
        log_warn("Target: Terror: the game's clock takes the CPU's current frequency");
    if (rtDetour("RTFS_open", rtfsOpen) != 0 || rtDetour("RTFS_fopen", rtfsFopen) != 0 ||
        rtDetour("RTFS_LZWfopen", rtfsLzwFopen) != 0 || rtDetour("RTFS_TranslateFilename", rtfsTranslateFilename) != 0)
        log_error("Target: Terror: file system not installed, the game will not find its data");
    if (rtDetour("JAMMA_Open", jammaOpen) != 0 || rtDetour("JAMMA_Close", jammaClose) != 0 ||
        rtDetour("JAMMA_GetValue", jammaGetValue) != 0 || rtDetour("JAMMA_GetReport", jammaGetReport) != 0 ||
        rtDetour("JAMMA_SendReport", jammaSendReport) != 0 || rtDetour("JAMMA_SendSerial", jammaSendSerial) != 0)
        log_error("Target: Terror: JAMMA board not installed, no input");
}
