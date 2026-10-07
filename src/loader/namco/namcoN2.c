// Namco System N2 games (see namcoN2.h): the game's start, its /tmp, and the
// hooks the Wangan titles need (the Pacloader fork's n2Wmmt3.cpp).

#include <dlfcn.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <locale.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "namcoN2.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const NamcoN2Game *namcoN2CurrentGame(void)
{
    static uint32_t cachedId;
    static const NamcoN2Game *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = namcoN2GetGame(gId);
    }
    return game;
}

int isNamcoN2Game(void)
{
    return namcoN2CurrentGame() != NULL;
}

const char *namcoN2GameDir(void)
{
    return gameDir;
}

// /tmp/... -> <game>/tmp/... (but /tmp/.X11-unix and such, the host's).
const char *namcoN2RedirectPath(const char *path, char *buf, size_t size)
{
    // The game's NVIDIA nForce OpenAL (linked in) drives the cabinet's APU
    // through /dev/dsp: where a PC has one (Batocera's OSS emulation), its
    // register mapping fails and the game crashes. Without it, that back end
    // is off, as on a PC without /dev/dsp.
    if (isNamcoN2Game() && path && !strcmp(path, "/dev/dsp"))
        return "/dev/.namco-n2-no-dsp";
    if (!isNamcoN2Game() || !gameDir[0] || !path || strncmp(path, "/tmp", 4) != 0 ||
        (path[4] != '/' && path[4] != '\0') || path[5] == '.')
        return path;
    snprintf(buf, size, "%s%s", gameDir, path);
    return buf;
}

// ---------------------------------------------------------------------------
// The JVS board's analog inputs, in the cabinet's raw counts (the fork's
// n2AnalogueCount): the wheel swings its whole 16-bit range, the pedals stay
// inside the window clInputDeviceJamma calibrates them in.

static int window(int value, int max, int low, int high)
{
    if (value < 0)
        value = 0;
    if (value > max)
        value = max;
    return low + (int)((float)value * (high - low) / (max > 0 ? max : 1) + 0.5f);
}

void namcoN2Calibrate(int *channel)
{
    int max = getJVSIO()->analogueMax;
    channel[ANALOGUE_1] = window(channel[ANALOGUE_1], max, 0, 65535);
    channel[ANALOGUE_2] = window(channel[ANALOGUE_2], max, 35000, 55480);
    channel[ANALOGUE_3] = window(channel[ANALOGUE_3], max, 29000, 49480);
}

// ---------------------------------------------------------------------------
// The Wangan hooks (n2Wmmt3InstallHooks).

static int one(void)
{
    return 1;
}

// The card reader's requests (clCardDeviceGameService), with no card
// emulator behind /dev/ttyM2: completed at once (the ones a reader with no
// card in it gets: no card is ever inserted, read or written), its status block (+0x2c:
// result, +8 no card, +0xc dispenser available) filled and the pending
// process (+0x34) cleared. The fork answers "reader not connected"
// (emGCPResult 3, the cabinet's E51, which stops the boot) when its external
// YACardEmu is not there; here the reader answers, with no card in it
// (emGCPResult 12, success: 0 and 1 are "busy", 3..11 the errors), so the
// game plays without cards.
static int cardResult(uint8_t *service, int result)
{
    if (!service)
        return 0;
    uint8_t *status = *(uint8_t **)(service + 0x2c);
    if (status)
    {
        *(int *)status = result;
        *(int *)(status + 8) = 0;
        status[0x0c] = 0;
    }
    *(void **)(service + 0x34) = NULL;
    return 1;
}

static int cardDone(uint8_t *service)
{
    return cardResult(service, 12);
}

static int cardDoneArg(uint8_t *service, int arg)
{
    (void)arg;
    return cardResult(service, 12);
}


// clSystemN2::isError(), reported once when it becomes true (the test menu
// then shows PCB ERROR).
static int (*realSystemIsError)(void *);

static int systemIsError(void *self)
{
    static int reported;
    int r = realSystemIsError(self);
    if ((r & 0xff) && !reported)
    {
        reported = 1;
        log_warn("Namco N2: clSystemN2 reports a system error");
    }
    return r;
}

// The link search: 1 s instead of 300 (the movl's immediate, if it is the
// expected instruction).
static void shortLinkSearch(uint32_t address)
{
    uint8_t *mov = (uint8_t *)(uintptr_t)address;
    uintptr_t page = address & ~(uintptr_t)0xfff;

    if (!address)
        return;
    if (mov[0] != 0xc7 || *(uint32_t *)(mov + 3) != 18000 ||
        mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        log_error("Namco N2: unexpected code at %#x, the link search not shortened", address);
        return;
    }
    *(uint32_t *)(mov + 3) = 60;
}

int namcoN2Init(void)
{
    const NamcoN2Game *g = namcoN2CurrentGame();
    char tmp[PATH_MAX];

    // The cabinet's locale is C: under another (a decimal comma), the game's
    // Lua reads "0.0" as a malformed number.
    setenv("LC_ALL", "C", 1);
    setlocale(LC_ALL, "C");

    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    snprintf(tmp, sizeof(tmp), "%s/tmp", gameDir);
    mkdir(tmp, 0755);

    // gRomInfo: name, region, release type, date, time (32 bytes each), a
    // revision number, then its name.
    const char *rom = namcoN2Symbol("gRomInfo");
    const char *revision = rom ? rom + 164 : "";
    printf("  REVISION:    %.32s\n", revision);
    if (strncmp(revision, g->revision, strlen(g->revision)) != 0)
        log_warn("Namco N2: revision %.32s, expected %s...", revision, g->revision);

    namcoN2Hook("_ZN18clInputDeviceJamma8checkUseEv", one);
    namcoN2Hook("_ZN16clInputDevicePad12handleEventsEv", one);
    namcoN2HookOriginal("_ZN10clSystemN27isErrorEv", systemIsError, (void **)&realSystemIsError);
    namcoN2Hook("_ZN23clCardDeviceGameService16requestGetStatusEv", cardDone);
    namcoN2Hook("_ZN23clCardDeviceGameService11requestInitEb", cardDoneArg);
    namcoN2Hook("_ZN23clCardDeviceGameService21requestCheckDispenserEv", cardDone);
    // No card comes: the insert request ends at once, no card in (the card
    // screen then asks again, as it polls a reader).
    namcoN2Hook("_ZN23clCardDeviceGameService13requestInsertEv", cardDone);
    namcoN2Hook("_ZN23clCardDeviceGameService19requestInsertCancelEv", cardDone);
    namcoN2Hook("_ZN23clCardDeviceGameService12requestEjectEb", cardDoneArg);
    namcoN2Hook("_ZN23clCardDeviceGameService15requestCleaningEv", cardDone);
    shortLinkSearch(g->linkSearchMov);
    namcoN2HaspInit();
    namcoN2KickbackInit();
    namcoN2AudioInit();
    namcoN2GraphicsInit();
    return 0;
}

// A single cabinet. The game finds the other cabinets of its link through
// multicast (225.0.0.1), where it also hears itself wherever the host loops
// multicast back to its sender (Batocera's wired eth0): it then links with
// itself and waits for its own ghost data ("cannot send/receive the ghost
// data, restart all the linked cabinets"). Its multicast sockets get no
// loopback, so it only ever finds itself alone.
int setsockopt(int fd, int level, int name, const void *value, socklen_t length)
{
    static int (*real)(int, int, int, const void *, socklen_t);
    if (!real)
        real = (int (*)(int, int, int, const void *, socklen_t))dlsym(RTLD_NEXT, "setsockopt");
    int r = real(fd, level, name, value, length);
    if (r == 0 && level == IPPROTO_IP && name == IP_ADD_MEMBERSHIP && isNamcoN2Game())
    {
        unsigned char off = 0;
        real(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &off, sizeof(off));
        if (getenv("NAMCO_N2_TRACE"))
            fprintf(stderr, "Namco N2: multicast socket %d without loopback\n", fd);
    }
    return r;
}
