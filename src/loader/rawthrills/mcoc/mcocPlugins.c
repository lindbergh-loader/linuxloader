// Marvel Contest of Champions' cabinet plugins (see docs/marvel-contest-of-champions.md),
// stood in for by linuxloader64.so under their names:
//
// - libUnityNatives.so: the dongle (UnityNatives_Hasp*), which holds the
//   cabinet's serial number, its type, and how many cards the operator has
//   authorised the dispensers to give out; it also encrypts the files the
//   game keeps on the operator's smart cards. Kept in LocalData/system.
// - libUnitySCard.so, libSCard.so: the operator's smart card reader, used by
//   the test menu's card authorisation only: no card.
// - libWebCamRT.so: the two cameras reading the players' hero cards: the
//   webcams (mcocWebcam.c); the players also pick their champions with the
//   stick (mcocCards), or their cards from a binder over the game
//   (mcocBinder.cpp).
//
// The cabinet's own are wrapped in Sentinel envelopes asking for its dongle.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "mcoc.h"
#include "mcocBinder.h"

// ---------------------------------------------------------------------------
// The game's settings, from its main thread once a frame (the RIO2's
// update):
//
// - the window: the game asks Unity for its cabinet's 1920x1080 window at
//   boot (GameFlow.OneTimeCreate: Screen.SetResolution(1920, 1080, false)),
//   over the size linuxloader64 started the player with: [Display] WIDTH x
//   HEIGHT (else 1280x720) and FULLSCREEN, asked again whenever the size
//   or the full screen mode differs;
// - the cameras: with no webcam (mcocWebcam.c), none (the developers'
//   Config.NoWebCam), else the game shows its scanners disconnected;
// - the pricing: coins, not the card swipes the dump's settings have
//   (Currency Input, preference 6: 0 cash, 1 tokens, 2 card swipes).

#define MCOC_PREF_CURRENCY_INPUT 6
#define MCOC_CURRENCY_SWIPES 2

static int screenSize(MonoMethod *get)
{
    MonoObject *exc = NULL, *v = unityMonoInvoke(get, NULL, NULL, &exc);
    return exc || !v ? -1 : unityMonoUnboxInt(v);
}

static int screenFull(MonoMethod *get)
{
    MonoObject *exc = NULL, *v = unityMonoInvoke(get, NULL, NULL, &exc);
    return exc || !v ? -1 : (unityMonoUnboxInt(v) & 0xff) != 0;
}

static void mcocScreen(void)
{
    static MonoMethod *set, *getWidth, *getHeight, *getFull;
    static int failed;
    int w = unityIniInt("Display", "WIDTH", 0), h = unityIniInt("Display", "HEIGHT", 0);
    uint8_t fs = unityIniInt("Display", "FULLSCREEN", 0) != 0;
    MonoObject *exc = NULL;
    void *args[3] = {&w, &h, &fs};
    if (failed)
        return;
    if (!set)
    {
        MonoImage *core = unityMonoImage("UnityEngine.CoreModule");
        set = unityMonoMethodIn(core, "UnityEngine", "Screen", "Screen:SetResolution(int,int,bool)");
        getWidth = unityMonoMethodIn(core, "UnityEngine", "Screen", "Screen:get_width()");
        getHeight = unityMonoMethodIn(core, "UnityEngine", "Screen", "Screen:get_height()");
        getFull = unityMonoMethodIn(core, "UnityEngine", "Screen", "Screen:get_fullScreen()");
        if (!set || !getWidth || !getHeight)
        {
            unityLog("Unity's Screen not found: the game's 1920x1080 window stays\n");
            failed = 1;
            return;
        }
    }
    if (w <= 0 || h <= 0)
    {
        w = 1280;
        h = 720;
    }
    // The game's own SetResolution(1920, 1080, false) also leaves full
    // screen: at a 1920x1080 screen only the mode differs.
    if (screenSize(getWidth) == w && screenSize(getHeight) == h && (!getFull || screenFull(getFull) == fs))
        return;
    unityMonoInvoke(set, NULL, args, &exc);
    unityLog("window %dx%d%s%s\n", w, h, fs ? " fullscreen" : "", exc ? ": the game threw" : "");
}

// ---------------------------------------------------------------------------
// The hero cards, without the cameras: on the scan screen, each player's
// stick left / right shows the previous / next champion in the player's
// next slot, with its card, and Start takes it (Start with none shown fills
// the team at random, as on the cabinet).
//
// The game's developers browse them with F1 / F2 (next, P1 / P2) and F3 /
// F4 (previous), read through the IOManager (ScanScreen.UpdatePlayer:
// GameFlow.IO.GetDepressed(Debug_F1 + player)). A key is newly pressed
// when its RIO switch count is odd and changed since the previous frame,
// and the RIO2 maps no switch to these: their counts are set here, on the
// RIO2's update (IOManager.Update's first call), one frame pressed.

#define MCOC_STATE_SCAN 5 // RT.GameState.Scan
#define MCOC_INPUT_NEXT 41 // RT.GameInput.Debug_F1, + player
#define MCOC_INPUT_PREV 43 // Debug_F3, + player
#define MCOC_INPUT_LEFT(p) (19 + (p) * 8) // P1Left, P2Left (mcocGame.c's)
#define MCOC_INPUT_RIGHT(p) (20 + (p) * 8)
// Held, the stick repeats: after half a second, 6 champions a second.
#define MCOC_REPEAT_DELAY 0.5
#define MCOC_REPEAT_RATE (1.0 / 6)

static double mcocNow(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

// A debug key pressed (one frame) or not.
static void mcocDebugKey(int input, int pressed)
{
    MonoImage *game = unityMonoGame();
    MonoObject *io = unityMonoStaticField(game, "RT", "GameFlow", "IO"), *inputs = NULL, *data;
    uint32_t count = pressed ? 1 : 0, last = 0;
    if (!unityMonoGetField(io, "m_InputData", &inputs) || !(data = unityMonoArrayItem(inputs, input)))
        return;
    unityMonoSetField(data, "rioSwitchCnt", &count);
    unityMonoSetField(data, "rioSwitchCntLast", &last);
}

// Whether the scan screen shows: asked of the game a few times a second.
static int mcocOnScanScreen(void)
{
    static int frames, shown;
    int state = 0;
    if (frames++ % 10 == 0)
    {
        MonoObject *flow = unityMonoStaticProperty(unityMonoGame(), "RT", "GameFlow", "Instance");
        shown = unityMonoCallInt(flow, "GameFlow:GetGameStateCurEnum()", &state) && state == MCOC_STATE_SCAN;
    }
    return shown;
}

static void mcocCards(int onScan)
{
    static struct
    {
        int dir;      // the stick held: -1 left, 1 right, 0
        double next;  // when it repeats
        int pressed;  // the debug key pressed on the previous frame, else 0
    } players[2];
    UnityInput in;
    double now = mcocNow();

    mcocInputSample(&in); // a player's binder open: not theirs
    for (int p = 0; p < 2; p++)
    {
        int dir = in.switches[MCOC_INPUT_RIGHT(p)] - in.switches[MCOC_INPUT_LEFT(p)], press = 0;
        if (players[p].pressed)
        {
            mcocDebugKey(players[p].pressed, 0);
            players[p].pressed = 0;
        }
        if (dir && dir != players[p].dir)
        {
            press = 1;
            players[p].next = now + MCOC_REPEAT_DELAY;
        }
        else if (dir && now >= players[p].next)
        {
            press = 1;
            players[p].next = now + MCOC_REPEAT_RATE;
        }
        players[p].dir = dir;
        // On the scan screen only: the debug keys do other things elsewhere.
        if (!press || !onScan)
            continue;
        players[p].pressed = (dir > 0 ? MCOC_INPUT_NEXT : MCOC_INPUT_PREV) + p;
        mcocDebugKey(players[p].pressed, 1);
    }
}

// ---------------------------------------------------------------------------
// The card binder (mcocBinder.cpp): on the scan screen, a player's stick
// pushed down opens it, and that player's stick, attacks and Start then
// drive it, hidden from the game (until all are let go once it closes).
// The cards it scans are handed to the scan screen one by one, as the
// camera's codes are (ScanScreen.OnPlayerCodeEvent), once the player has
// joined and the previous one was taken.

#define MCOC_INPUT_START(p) (13 + (p))
#define MCOC_INPUT_UP(p) (17 + (p) * 8)
#define MCOC_INPUT_DOWN(p) (18 + (p) * 8)
#define MCOC_INPUT_ATTACK(p, a) (21 + (p) * 8 + (a)) // light, medium, heavy, special
#define MCOC_PLAYER_SCAN_CARDS 1 // ScanScreen.PlayerState.ScanCards
#define MCOC_CARD_GAP 0.3 // seconds between two cards handed

static pthread_mutex_t queueLock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
    char codes[MCOC_HAND_MAX][9];
    int count;
    double next;
} queue[MCOC_PLAYERS];
static volatile int scanning[MCOC_PLAYERS];

void mcocPresent(int player, const char (*codes)[9], int count)
{
    if (player < 0 || player >= MCOC_PLAYERS)
        return;
    pthread_mutex_lock(&queueLock);
    queue[player].count = count < MCOC_HAND_MAX ? count : MCOC_HAND_MAX;
    memcpy(queue[player].codes, codes, sizeof(queue[player].codes[0]) * queue[player].count);
    pthread_mutex_unlock(&queueLock);
}

int mcocWaiting(int player)
{
    return player >= 0 && player < MCOC_PLAYERS ? queue[player].count : 0;
}

int mcocScanning(int player)
{
    return player >= 0 && player < MCOC_PLAYERS ? scanning[player] : 0;
}

// The scan screen's data of the player (ScanScreen.PlayerData), and the
// screen.
static MonoObject *mcocScanPlayer(int player, MonoObject **screen)
{
    MonoObject *flow = unityMonoStaticProperty(unityMonoGame(), "RT", "GameFlow", "Instance"), *state, *all = NULL;
    *screen = NULL;
    state = unityMonoCall(flow, "GameFlow:GetGameStateCur()", NULL, NULL);
    if (!unityMonoGetField(state, "mScan", screen) || !*screen || !unityMonoGetField(*screen, "mPlayerData", &all))
        return NULL;
    return unityMonoArrayItem(all, player);
}

static void mcocHandCards(int onScan)
{
    static unsigned frames;
    double now = mcocNow();
    frames++;
    for (int p = 0; p < MCOC_PLAYERS; p++)
    {
        MonoObject *screen, *data, *scanned = NULL;
        int state = -1, waiting;
        char code[9];
        if (!onScan)
        {
            scanning[p] = 0;
            queue[p].count = 0;
            continue;
        }
        waiting = queue[p].count;
        if ((!waiting || now < queue[p].next) && frames % 10)
            continue;
        if (!(data = mcocScanPlayer(p, &screen)) || !unityMonoGetField(data, "mState", &state))
            continue;
        scanning[p] = state == MCOC_PLAYER_SCAN_CARDS;
        if (!waiting || now < queue[p].next || !scanning[p] || !unityMonoGetField(data, "mScannedHero", &scanned) ||
            scanned)
            continue;
        pthread_mutex_lock(&queueLock);
        if ((waiting = queue[p].count) > 0)
        {
            memcpy(code, queue[p].codes[0], sizeof(code));
            memmove(queue[p].codes, queue[p].codes + 1, sizeof(queue[p].codes[0]) * --queue[p].count);
        }
        pthread_mutex_unlock(&queueLock);
        if (waiting > 0)
        {
            int player = p, ok = 0;
            void *args[2] = {&player, unityMonoString(code)};
            if (args[1])
                unityMonoCall(screen, "ScanScreen:OnPlayerCodeEvent(int,string)", args, &ok);
            unityLog("binder: P%d card %s%s\n", p + 1, code, ok ? "" : ": not taken (the game threw)");
            queue[p].next = now + MCOC_CARD_GAP;
        }
    }
}

static void mcocBinderInput(int onScan)
{
    static int last[MCOC_PLAYERS][MCOC_IN_COUNT], hidden[MCOC_PLAYERS], lastDown[MCOC_PLAYERS];
    UnityInput in;
    int owner = mcocBinderOwner();

    unityInputSample(&in);
    mcocBinderScanScreen(onScan);
    if (owner >= 0 && !onScan)
    {
        mcocBinderClose(); // the scan screen timed out
        owner = -1;
    }
    for (int p = 0; p < MCOC_PLAYERS; p++)
    {
        int now[MCOC_IN_COUNT] = {
            in.switches[MCOC_INPUT_UP(p)],        in.switches[MCOC_INPUT_DOWN(p)],
            in.switches[MCOC_INPUT_LEFT(p)],      in.switches[MCOC_INPUT_RIGHT(p)],
            in.switches[MCOC_INPUT_ATTACK(p, 0)], in.switches[MCOC_INPUT_ATTACK(p, 1)],
            in.switches[MCOC_INPUT_ATTACK(p, 2)], in.switches[MCOC_INPUT_ATTACK(p, 3)],
            in.switches[MCOC_INPUT_START(p)],
        };
        int any = 0, down = now[MCOC_IN_DOWN];
        for (int b = 0; b < MCOC_IN_COUNT; b++)
            any |= now[b];
        if (owner < 0 && onScan && down && !lastDown[p] && !hidden[p])
        {
            mcocBinderOpen(p);
            owner = p;
            memcpy(last[p], now, sizeof(now)); // held as it opens: not a press
        }
        if (owner == p)
        {
            for (int b = 0; b < MCOC_IN_COUNT; b++)
                if (now[b] != last[p][b])
                    mcocBinderButton(b, now[b]);
            hidden[p] = 1;
        }
        else if (hidden[p] && !any)
            hidden[p] = 0;
        mcocInputHidePlayer(p, hidden[p]);
        memcpy(last[p], now, sizeof(now));
        lastDown[p] = down;
    }
}

void mcocFrame(void)
{
    static unsigned frames;
    if (frames == 0)
    {
        MonoImage *game = unityMonoGame();
        // The game's own keyboard map off: linuxloader's (MAME's layout,
        // mcocGame.c) feeds the RIO2.
        if (!unityMonoSetStaticBool(game, "RT", "GameFlow", "Config", "NoKeyboard", 1))
            unityLog("GameFlow.Config not found: the game's own keys stay\n");
        if (mcocWebcamCount())
            mcocWebcamAssign();
        else if (!unityMonoSetStaticBool(game, "RT", "GameFlow", "Config", "NoWebCam", 1))
            unityLog("GameFlow.Config not found: the cameras are missed\n");
        if (unityMonoPref(MCOC_PREF_CURRENCY_INPUT) == MCOC_CURRENCY_SWIPES)
        {
            unityMonoSetPref(MCOC_PREF_CURRENCY_INPUT, 0);
            unityLog("pricing: card swipes changed to coins\n");
        }
    }
    // A request takes effect at the end of the frame: then checked once a
    // second.
    if (frames % 60 == 0)
        mcocScreen();
    {
        int onScan = mcocOnScanScreen();
        mcocBinderInput(onScan);
        mcocCards(onScan);
        mcocHandCards(onScan);
    }
    frames++;
}

// ---------------------------------------------------------------------------
// The dongle.

#define MCOC_DONGLE_FILE "LocalData/system/mcoc-dongle"

static struct
{
    int loaded, loggedIn;
    int serial, cards;
    uint8_t security, cabinet;
} dongle;

static void dongleLoad(void)
{
    FILE *f;
    if (dongle.loaded)
        return;
    dongle.loaded = 1;
    // A set dongle (serial > 0: else "dongle not set"), the standard cabinet
    // (1 is China's), and cards enough for the dispensers (one per card
    // given out).
    dongle.serial = 12345;
    dongle.cards = 99999;
    dongle.security = 1;
    dongle.cabinet = 0;
    if ((f = fopen(MCOC_DONGLE_FILE, "r")))
    {
        int serial, cards, security, cabinet;
        if (fscanf(f, "serial %d cards %d security %d cabinet %d", &serial, &cards, &security, &cabinet) == 4)
        {
            dongle.serial = serial;
            dongle.cards = cards;
            dongle.security = (uint8_t)security;
            dongle.cabinet = (uint8_t)cabinet;
        }
        fclose(f);
    }
}

static int dongleSave(void)
{
    FILE *f = fopen(MCOC_DONGLE_FILE, "w");
    if (!f)
        return 0;
    fprintf(f, "serial %d cards %d security %d cabinet %d\n", dongle.serial, dongle.cards, dongle.security,
            dongle.cabinet);
    fclose(f);
    return 1;
}

// C#'s bool comes back as a 4-byte BOOL: int.
typedef void (*McocLogCallback)(const char *);

int UnityNatives_SetLog(McocLogCallback cb)
{
    (void)cb;
    return 0;
}

int UnityNatives_HaspLogin(void)
{
    dongleLoad();
    dongle.loggedIn = 1;
    return 1;
}

void UnityNatives_HaspLogout(void) { dongle.loggedIn = 0; }
int UnityNatives_HaspIsLoggedIn(void) { return dongle.loggedIn; }

// The smart cards' files, encrypted by the dongle's key: written and read
// back here only, so any reversible transform does. A fixed XOR stream.
static uint8_t cipherByte(uint64_t i)
{
    return (uint8_t)(0x5a ^ (i * 0x9d) ^ (i >> 8));
}

int UnityNatives_HaspEncrypt(uint8_t *buf, uint64_t len)
{
    if (!dongle.loggedIn || !buf)
        return 0;
    for (uint64_t i = 0; i < len; i++)
        buf[i] ^= cipherByte(i);
    return 1;
}

int UnityNatives_HaspDecrypt(uint8_t *buf, uint64_t len) { return UnityNatives_HaspEncrypt(buf, len); }

int UnityNatives_HaspGetAuthorzedCards(void)
{
    dongleLoad();
    return dongle.cards;
}

int UnityNatives_HaspAddAuthorzedCards(int count)
{
    dongleLoad();
    if (dongle.cards + count < 0)
        return 0;
    dongle.cards += count;
    // Never short: the arcade's card stock is not ours to keep.
    if (dongle.cards < 1000)
        dongle.cards = 99999;
    return dongleSave();
}

int UnityNatives_HaspResetDongle(void)
{
    dongleLoad();
    dongle.cards = 99999;
    return dongleSave();
}

int UnityNatives_HaspGetSerial(void)
{
    dongleLoad();
    return dongle.serial;
}

int UnityNatives_HaspSetSerial(int value)
{
    dongleLoad();
    dongle.serial = value;
    return dongleSave();
}

uint8_t UnityNatives_HaspGetSecurity(void)
{
    dongleLoad();
    return dongle.security;
}

int UnityNatives_HaspSetSecurity(uint8_t value)
{
    dongleLoad();
    dongle.security = value;
    return dongleSave();
}

uint8_t UnityNatives_HaspGetCabinet(void)
{
    dongleLoad();
    return dongle.cabinet;
}

int UnityNatives_HaspSetCabinet(uint8_t value)
{
    dongleLoad();
    dongle.cabinet = value;
    return dongleSave();
}

// The keys of the RIO2's secured link: the virtual board takes any.
void UnityNatives_GetRIOKeys(void *pub, void *priv)
{
    if (pub)
        memset(pub, 0, 32);
    if (priv)
        memset(priv, 0, 32);
}

// ---------------------------------------------------------------------------
// The operator's smart card reader (UnitySCard.Result): no card.

enum
{
    SCARD_OK = 0,
    SCARD_NO_CARD = 4,
};

typedef void (*SCardLogCallback)(const char *);

void SCard_ForUnity(void) {}
int UnitySCard_SetLog(SCardLogCallback cb)
{
    (void)cb;
    return SCARD_OK;
}

static void copyText(char *sb, unsigned n, const char *text)
{
    if (sb && n)
        snprintf(sb, n, "%s", text);
}

void UnitySCard_GetErrorMsg(int result, char *sb, unsigned n)
{
    copyText(sb, n, result == SCARD_NO_CARD ? "No card" : result == SCARD_OK ? "OK" : "Error");
}
int UnitySCard_IsCardPresent(void) { return SCARD_NO_CARD; }
int UnitySCard_ReadCard(void) { return SCARD_NO_CARD; }
void UnitySCard_GetReadResults_UID(char *sb, unsigned n) { copyText(sb, n, ""); }
void UnitySCard_GetReadResults_TimeCardCreation(char *sb, unsigned n) { copyText(sb, n, ""); }
void UnitySCard_GetReadResults_TimeCardConsumption(char *sb, unsigned n) { copyText(sb, n, ""); }
unsigned UnitySCard_GetReadResults_CardSerial(void) { return 0; }
unsigned UnitySCard_GetReadResults_CardsRemaining(void) { return 0; }
unsigned UnitySCard_GetReadResults_CardsUsed(void) { return 0; }
int UnitySCard_GetReadResults_CabinetSerial(void) { return 0; }
unsigned UnitySCard_GetReadResults_ProductId(void) { return 0; }
int UnitySCard_ConsumeCard(void) { return SCARD_NO_CARD; }
