// The cabinet I/O daemon of America's Army (see gvrGame.h), served by the
// loader.
//
// The game's AAALink (an UnrealScript TcpLink, AAA_Core.u) connects to its
// host name's address on linkPort and trades text lines, comma-separated:
//
// from the daemon:
//  IN,<p>,<x>,<y>,<trigger>,<alt fire>,<reload>,<start>   player p's gun
//                 (0, 1): its aim in pixels of the 640x480 picture, its
//                 buttons (alt fire is only logged)
//  EV,COIN,<n>    n coins in
//  EV,TEST        the test button (the cabinet's test menu was the
//                 daemon's: the game's settings are CFG)
//  EV,VOL,<game>,<attract>   the volumes
//  CFG,<coins per play>,<coins per continue>,<coins display>,
//      <coins to money>,<continue timeout>,<lives>,<difficulty>
//  VER,<n>        the protocol's version
// from the game:
//  ASK,CFG        asks for CFG
//  EV,COIN        the C key (Simulate_Coin): asks for a coin
//  EV,ATTRACT, EV,TEST, EV,PLAYER,<p>,START|CONTINUE|END
//  GUN,<p>,<kick>,<flash>,<rate>   the gun's recoil and muzzle flash
//
// The connect() is sent to a listener of our own on 127.0.0.1; a thread
// answers, and sends the guns from the loader's JVS state (evdev), or the
// desktop's (the mouse over the window is player 1's gun): player 1's gun on
// ANALOGUE_1/2, player 2's on ANALOGUE_3/4, BUTTON_1 the trigger, BUTTON_2
// reload (a shot off the screen too), START; the coins, TEST.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gvr.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"

#define LINK_WIDTH 640
#define LINK_HEIGHT 480
#define LINK_PLAYERS 2
#define LINK_TICK_US 8000
// An unchanged state is sent again this often (in ticks), as the daemon did.
#define LINK_REFRESH_TICKS 12
// The operator settings sent for ASK,CFG: a coin a play and a continue (none
// in free play, the default: the cabinet's test menu, where they were set,
// was the daemon's), 10 s to continue, 3 lives, medium.
#define LINK_CONFIG "CFG,1,1,1,1,10,3,1"
#define LINK_CONFIG_FREEPLAY "CFG,0,0,1,1,10,3,1"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int listenFd = -1;
static struct sockaddr_in listenAddr;
static int trace;

static void sendLine(int fd, const char *line)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s\r\n", line);
    if (trace)
        fprintf(stderr, "GVR link: > %s\n", line);
    for (int off = 0; off < n;)
    {
        ssize_t w = send(fd, buf + off, n - off, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return;
        off += w;
    }
}

static void handleLine(int fd, const char *line)
{
    if (trace)
        fprintf(stderr, "GVR link: < %s\n", line);
    if (!strcmp(line, "ASK,CFG"))
        sendLine(fd, getConfig()->freeplay == 0 ? LINK_CONFIG : LINK_CONFIG_FREEPLAY);
    else if (!strcmp(line, "EV,COIN"))
        sendLine(fd, "EV,COIN,1");
}

// A desktop pointer's position (0..1 over the window) on the 4:3 picture,
// which SDL (sdl12-compat) letterboxes in a window of another shape.
static int desktopToPicture(float *x, float *y)
{
    int w, h;
    if (!desktopPointerWindowSize(&w, &h))
        return 1;
    if (w * LINK_HEIGHT > h * LINK_WIDTH)
    {
        float pictureWidth = (float)h * LINK_WIDTH / LINK_HEIGHT;
        *x = (*x * w - (w - pictureWidth) / 2) / pictureWidth;
    }
    else if (w * LINK_HEIGHT < h * LINK_WIDTH)
    {
        float pictureHeight = (float)w * LINK_HEIGHT / LINK_WIDTH;
        *y = (*y * h - (h - pictureHeight) / 2) / pictureHeight;
    }
    return *x >= 0.f && *x <= 1.f && *y >= 0.f && *y <= 1.f;
}

typedef struct
{
    int x, y;
    int onScreen;
} LinkGun;

// The guns as last sent (read by the crosshairs, gvrCrosshair.c).
static LinkGun guns[LINK_PLAYERS] = {{LINK_WIDTH / 2, LINK_HEIGHT / 2, 0}, {LINK_WIDTH / 2, LINK_HEIGHT / 2, 0}};

int gvrLinkGun(int player, int *x, int *y)
{
    if (player < 0 || player >= LINK_PLAYERS || !guns[player].onScreen)
        return 0;
    *x = guns[player].x;
    *y = guns[player].y;
    return 1;
}

// Player p's gun in the picture's pixels; 0 when it is off the screen (the
// last position is kept).
static int gunPosition(JVSIO *io, int p, int desktop, LinkGun *gun)
{
    int max = io->analogueMax > 0 ? io->analogueMax : 0xffff;
    int ax = io->state.analogueChannel[ANALOGUE_1 + 2 * p], ay = io->state.analogueChannel[ANALOGUE_2 + 2 * p];
    // The desktop's mouse gives 0 off the window; a light gun at the edge
    // sees outside the screen.
    if (ax <= 0 || ay <= 0 || ax >= max || ay >= max)
        return gun->onScreen = 0;
    float x = (float)ax / max, y = (float)ay / max;
    if (desktop && !desktopToPicture(&x, &y))
        return gun->onScreen = 0;
    gun->x = (int)(x * (LINK_WIDTH - 1) + 0.5f);
    gun->y = (int)(y * (LINK_HEIGHT - 1) + 0.5f);
    return gun->onScreen = 1;
}

// An IN line's fields after the position (trigger, reload, start, ...).
static const char *buttonFields(const char *line)
{
    for (int commas = 0; *line && commas < 4; line++)
        if (*line == ',')
            commas++;
    return line;
}

static void serve(int fd)
{
    static const int players[LINK_PLAYERS] = {PLAYER_1, PLAYER_2};
    char sent[LINK_PLAYERS][64] = {{0}}, in[1024];
    int inLength = 0, lastCoins[2] = {0}, coinsSeen = 0, lastTest = 0, ticks = 0;
    int desktop = getConfig()->inputMode != 2;

    for (;;)
    {
        fd_set readable;
        struct timeval timeout = {0, LINK_TICK_US};
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        int ready = select(fd + 1, &readable, NULL, NULL, &timeout);
        if (ready < 0 && errno != EINTR)
            return;
        if (ready > 0)
        {
            ssize_t n = recv(fd, in + inLength, sizeof(in) - 1 - inLength, 0);
            if (n <= 0)
                return;
            inLength += n;
            in[inLength] = '\0';
            char *start = in, *end;
            while ((end = strchr(start, '\n')))
            {
                *end = '\0';
                if (end > start && end[-1] == '\r')
                    end[-1] = '\0';
                handleLine(fd, start);
                start = end + 1;
            }
            inLength -= start - in;
            memmove(in, start, inLength);
            if (inLength >= (int)sizeof(in) - 1)
                inLength = 0;
        }

        JVSIO *io = desktop ? desktopInputState() : getJVSIO();
        int refresh = ++ticks >= LINK_REFRESH_TICKS;
        if (refresh)
            ticks = 0;
        for (int p = 0; p < LINK_PLAYERS; p++)
        {
            unsigned int switches = io->state.inputSwitch[players[p]];
            int trigger = (switches & BUTTON_1) != 0;
            int onScreen = gunPosition(io, p, desktop, &guns[p]);
            int reload = (switches & BUTTON_2) || (trigger && !onScreen);
            char line[64];
            snprintf(line, sizeof(line), "IN,%d,%d,%d,%d,0,%d,%d", p, guns[p].x, guns[p].y, trigger && onScreen,
                     reload, (switches & BUTTON_START) != 0);
            if (refresh || strcmp(line, sent[p]))
            {
                // Traced when the buttons change, not at every move.
                int saved = trace;
                trace = trace && strcmp(buttonFields(line), buttonFields(sent[p]));
                sendLine(fd, line);
                trace = saved;
                strcpy(sent[p], line);
            }
        }

        for (int c = 0; c < 2; c++)
        {
            int coins = io->state.coinCount[c];
            for (int n = coinsSeen ? coins - lastCoins[c] : 0; n > 0; n--)
                sendLine(fd, "EV,COIN,1");
            lastCoins[c] = coins;
        }
        coinsSeen = 1;

        int test = (io->state.inputSwitch[SYSTEM] & BUTTON_TEST) != 0;
        if (test && !lastTest)
            sendLine(fd, "EV,TEST");
        lastTest = test;
    }
}

static void *linkThread(void *arg)
{
    (void)arg;
    for (;;)
    {
        int fd = accept(listenFd, NULL, NULL);
        if (fd < 0)
        {
            if (errno == EINTR)
                continue;
            return NULL;
        }
        if (trace)
            fprintf(stderr, "GVR link: the game connected\n");
        serve(fd);
        close(fd);
        if (trace)
            fprintf(stderr, "GVR link: the game disconnected\n");
    }
}

static int startListener(void)
{
    socklen_t length = sizeof(listenAddr);
    pthread_t thread;

    if (listenFd >= 0)
        return 0;
    trace = getenv("GVR_LINK_TRACE") != NULL;
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&listenAddr, sizeof(listenAddr)) != 0 || listen(fd, 1) != 0 ||
        getsockname(fd, (struct sockaddr *)&listenAddr, &length) != 0)
    {
        close(fd);
        return -1;
    }
    listenFd = fd;
    if (pthread_create(&thread, NULL, linkThread, NULL) != 0)
    {
        close(fd);
        listenFd = -1;
        return -1;
    }
    pthread_detach(thread);
    printf("Global VR: the I/O daemon is the loader's (127.0.0.1:%d)\n", ntohs(listenAddr.sin_port));
    return 0;
}

int gvrLinkConnect(int fd, const struct sockaddr *addr, socklen_t length,
                   int (*realConnect)(int, const struct sockaddr *, socklen_t))
{
    const GvrGame *g = gvrCurrentGame();
    const struct sockaddr_in *in = (const struct sockaddr_in *)addr;

    if (!g || !g->linkPort || !addr || length < sizeof(*in) || in->sin_family != AF_INET ||
        ntohs(in->sin_port) != g->linkPort)
        return realConnect(fd, addr, length);

    pthread_mutex_lock(&lock);
    int started = startListener();
    pthread_mutex_unlock(&lock);
    if (started != 0)
        return realConnect(fd, addr, length);
    return realConnect(fd, (const struct sockaddr *)&listenAddr, sizeof(listenAddr));
}
