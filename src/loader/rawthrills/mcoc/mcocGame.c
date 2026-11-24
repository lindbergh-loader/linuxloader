// Marvel Contest of Champions (Raw Thrills/Kabam, mcoc2_Data): a 64-bit
// Unity 2021 game on the Raw Thrills Unity base (rawthrills/unity),
// offline, two players on a RIO2: a virtual board here, its pins and
// switch matrix fed by the players' inputs (docs/marvel-contest-of-
// champions.md). Its plugins, settings and card binder: mcocPlugins.c,
// mcocWebcam.c, mcocBinder.cpp.
#define _GNU_SOURCE
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mcoc.h"
#include "mcocBinder.h"

// Its GameInput values (read as the RIO2's pins and switch matrix): a
// stick and four attack buttons (light, medium, heavy, special) for each
// player.
static const UnitySwitchKey mcocSwitches[] = {
    {0, "TEST_BUTTON"}, {1, "PLAYER_1_BUTTON_SERVICE"}, {2, "VOLUME_UP"}, {3, "VOLUME_DOWN"},
    {10, "PLAYER_1_COIN"}, {11, "PLAYER_2_COIN"},
    {13, "PLAYER_1_BUTTON_START"}, {14, "PLAYER_2_BUTTON_START"},
    {17, "PLAYER_1_BUTTON_UP"}, {18, "PLAYER_1_BUTTON_DOWN"}, {19, "PLAYER_1_BUTTON_LEFT"}, {20, "PLAYER_1_BUTTON_RIGHT"},
    {21, "PLAYER_1_BUTTON_1"}, {22, "PLAYER_1_BUTTON_2"}, {23, "PLAYER_1_BUTTON_3"}, {24, "PLAYER_1_BUTTON_4"},
    {25, "PLAYER_2_BUTTON_UP"}, {26, "PLAYER_2_BUTTON_DOWN"}, {27, "PLAYER_2_BUTTON_LEFT"}, {28, "PLAYER_2_BUTTON_RIGHT"},
    {29, "PLAYER_2_BUTTON_1"}, {30, "PLAYER_2_BUTTON_2"}, {31, "PLAYER_2_BUTTON_3"}, {32, "PLAYER_2_BUTTON_4"},
    {0, NULL},
};

// MAME's default layout. P1: the arrows, Left
// Ctrl, Left Alt, Space, Left Shift (light, medium, heavy, special); P2: R F
// D G, A S Q W. 1 / 2 start, 5 / 6 coins, F2 the test button, 9 service,
// Page Up / Down the volume. The game's own map (its developers': WASD and
// the arrows) is off (Config.NoKeyboard, mcocPlugins.c): it took the arrows
// for P2.
static const UnitySwitchKeysym mcocKeys[] = {
    {0, 0xffbf /* F2 */}, {1, '9'}, {2, 0xff55 /* Prior */}, {3, 0xff56 /* Next */},
    {10, '5'}, {11, '6'}, {13, '1'}, {14, '2'},
    {17, 0xff52 /* Up */}, {18, 0xff54 /* Down */}, {19, 0xff51 /* Left */}, {20, 0xff53 /* Right */},
    {21, 0xffe3 /* Control_L */}, {22, 0xffe9 /* Alt_L */}, {23, ' '}, {24, 0xffe1 /* Shift_L */},
    {25, 'r'}, {26, 'f'}, {27, 'd'}, {28, 'g'},
    {29, 'a'}, {30, 's'}, {31, 'q'}, {32, 'w'},
    {0, 0},
};

// ---------------------------------------------------------------------------
// The players' inputs, a player's hidden while the card binder takes them:
// Start 13 + p, the stick and the four attacks 17 + 8p .. 24 + 8p.

static volatile int hiddenPlayers; // a bit a player

void mcocInputHidePlayer(int player, int hidden)
{
    if (player < 0 || player > 1)
        return;
    if (hidden)
        hiddenPlayers |= 1 << player;
    else
        hiddenPlayers &= ~(1 << player);
}

void mcocInputSample(UnityInput *in)
{
    int hidden = hiddenPlayers;
    unityInputSample(in);
    for (int p = 0; p < 2; p++)
        if (hidden & (1 << p))
        {
            in->switches[13 + p] = 0;
            for (int i = 17 + 8 * p; i <= 24 + 8 * p; i++)
                in->switches[i] = 0;
        }
}

// ---------------------------------------------------------------------------
// The RIO2.

// Its pins (IOManager.GPIO_Mappings): the GameInput each GPIO reads, the
// switch index filled for it (-1: none). Its card dispensers' status pins
// answer a dispenser of the Asahi kind (the operator menu's default) ready
// to give a card, neither empty, jammed nor low (its low signal is active
// low).
static const signed char mcocGpio[24] = {
    0, 1, 2, 3,     // test, service, volume up, down
    10, 11, 12, -1, // coin 1, 2, bill validator
    13, 14,         // P1, P2 start
    -1, -1, -1, -1, // ticket mechanisms
    -1, -1, -1, -1, -1, // P1 dispenser: vend, ready, jam, empty, low
    -1, -1, -1, -1, -1, // P2's
};

static int mcocPin(int pin, uint32_t *value)
{
    UnityInput in;
    if (pin < 0 || pin >= 24)
        return 1;
    switch (pin)
    {
        case 15: case 20: // ready to vend
        case 18: case 23: // not low
            *value = 1;
            return 1;
        default:
            break;
    }
    if (mcocGpio[pin] < 0)
        return 1;
    mcocInputSample(&in);
    *value = in.switches[(int)mcocGpio[pin]] ? 1u : 0u;
    return 1;
}

// Bank 0 P1's, 1 P2's: up, down, left, right, light, medium, heavy, special
// (GameInput P1Up..P1Special, P2Up..).
static int mcocMatrix(int bank, int sw, uint32_t *value)
{
    UnityInput in;
    if (bank >= 2 || sw >= 8)
        return 0;
    mcocInputSample(&in);
    *value = in.switches[17 + bank * 8 + sw] ? 1u : 0u;
    return 1;
}

// ---------------------------------------------------------------------------
// The cabinet.

// The game runs from the cabinet's /pm, the game's directory (the run tree,
// its current one), and copies its offline cache to root's Unity data,
// /root/.config/unity3d/Kabam/Champions, where the player keeps it
// (persistentDataPath): the user's.
#define MCOC_ROOT_UNITY "/root/.config/unity3d"
static char mcocUnityDir[PATH_MAX];

static const char *mcocMapPath(const char *path)
{
    static __thread char mapped[PATH_MAX];
    size_t n = sizeof(MCOC_ROOT_UNITY) - 1;
    if (!strcmp(path, "/pm") || !strcmp(path, "/pm/"))
        return ".";
    if (!strncmp(path, "/pm/", 4))
        return path + 4;
    if (*mcocUnityDir && !strncmp(path, MCOC_ROOT_UNITY, n) && (path[n] == '/' || !path[n]))
    {
        snprintf(mapped, sizeof(mapped), "%s%s", mcocUnityDir, path + n);
        return mapped;
    }
    return path;
}

// A saved language (PlayerPrefs user_language) the game needs: without one
// its boot (Setup.DoUserLanguageSelection, Kabam's mobile flow) waits for a
// logo flow the arcade never runs, ThemeManager is never made, and the
// quest board's load (QuestMode.Load) fails on it after the card scan: the
// game hangs there. A cabinet has one from the factory; a new install
// gets English (the test menu's region still picks the game's language).
// Unity keeps its PlayerPrefs, strings base64, in
// <unity3d>/<company>/<product>/prefs: unknown/unknown for this build.
static void mcocLanguagePref(void)
{
    static const char entry[] = "\t<pref name=\"user_language\" type=\"string\">ZW4=</pref>\n";
    static const char end[] = "</unity_prefs>";
    char dir[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX + 8];
    char *text = NULL, *at;
    long size = 0;
    FILE *f;

    if (!*mcocUnityDir)
        return;
    snprintf(dir, sizeof(dir), "%s/unknown/unknown", mcocUnityDir);
    snprintf(path, sizeof(path), "%s/prefs", dir);
    if ((f = fopen(path, "rb")))
    {
        if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
            (text = malloc(size + 1)) && fread(text, 1, size, f) == (size_t)size)
            text[size] = 0;
        else
        {
            free(text);
            text = NULL;
        }
        fclose(f);
        if (!text)
            return;
        if (strstr(text, "name=\"user_language\"") || !(at = strstr(text, end)))
        {
            free(text);
            return;
        }
    }
    else
    {
        // mkdir -p
        for (char *p = dir + 1; *p; p++)
            if (*p == '/')
            {
                *p = 0;
                mkdir(dir, 0755);
                *p = '/';
            }
        mkdir(dir, 0755);
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!(f = fopen(tmp, "wb")))
    {
        free(text);
        return;
    }
    if (text)
        fprintf(f, "%.*s%s%s", (int)(at - text), text, entry, at);
    else
        fprintf(f, "<unity_prefs version_major=\"1\" version_minor=\"1\">\n%s%s\n", entry, end);
    if (fclose(f) == 0 && rename(tmp, path) == 0)
        unityLog("user_language set to English in %s\n", path);
    else
        unlink(tmp);
    free(text);
}

// Where Unity's player keeps the game's data: $XDG_CONFIG_HOME or
// ~/.config, then unity3d/<company>/<product>.
static void mcocInit(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg == '/')
        snprintf(mcocUnityDir, sizeof(mcocUnityDir), "%s/unity3d", xdg);
    else if (home && *home)
        snprintf(mcocUnityDir, sizeof(mcocUnityDir), "%s/.config/unity3d", home);
    mcocLanguagePref();
}

// The game runs the cabinet's commands (Process.Start: a fork, then
// execve): reboot (once a day, and after a factory restore), the USB
// stick's udev rule, mount point and unmount, pcscd's restart. None on the
// host, where the game may run as root: the reboot ends the game instead
// (back to the frontend), the others do nothing.
static void mcocCommand(const char *name, char *const argv[])
{
    if (!strcmp(name, "reboot") || !strcmp(name, "shutdown") || !strcmp(name, "poweroff"))
    {
        // The fork that would run it: the game is its parent.
        pid_t game = getppid();
        unityLog("%s: the game ends\n", name);
        kill(game, SIGTERM);
        for (int i = 0; i < 30 && kill(game, 0) == 0; i++)
            usleep(100000);
        kill(game, SIGKILL);
        _exit(0);
    }
    if (!strcmp(name, "udevadm") || !strcmp(name, "systemctl") || !strcmp(name, "mount") ||
        !strcmp(name, "umount") || !strcmp(name, "mountpoint") ||
        (!strcmp(name, "mkdir") && argv && argv[1] && !strncmp(argv[1], "/mnt/", 5)))
    {
        unityLog("not run: %s %s\n", name, argv && argv[1] ? argv[1] : "");
        _exit(0);
    }
}

const UnityGame mcocGame = {
    .name = "Marvel Contest of Champions",
    .dataDir = "/mcoc2_Data",
    .init = mcocInit,
    .mapPath = mcocMapPath,
    .command = mcocCommand,
    // Kabam's servers, its CDN, its chat: it runs from its offline cache.
    .offline = 1,
    .switches = mcocSwitches,
    .keys = mcocKeys,
    .video = 1,
    .overlay = mcocBinderDraw, // the card binder
    .rio2Virtual = 1,
    .rio2Update = mcocFrame,
    .rio2Pin = mcocPin,
    .rio2Matrix = mcocMatrix,
};
