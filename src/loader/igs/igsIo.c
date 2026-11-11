// The PLX PCI9030 card of the IGS games and the IGS036 behind it (see
// igsGame.h).
//
// The PLX SDK's find, open and BAR map are replaced: the card is there, its
// BARs plain memory nobody else writes. The IGS036 and the game talk through
// that shared RAM, each block XORed with a key of its own, the rounds paced
// by a sequence number and a doorbell; the exchange itself is replaced, so
// none of that is needed: each round fills the IGS036's block (decrypted, as
// the game reads it) and lets the game handle it and write its own.
//
// The IGS036's block, as the game uses it (32-bit words):
//   5      the cabinet's switches ([IOCardKeyValue], a bit per switch, the
//          game's own: see IgsGame's keys)
//   6      the helm, a potentiometer (0..IgsGame's helmMax)
//   9      two more switches (bit 1: the ticket dispenser empty or jammed)
//   12, 13 coins since the last round (coin 1, 2)
//   14     tickets paid out since the last round
//   15     the command acknowledged
//   16     the command's result
//   17...  the operator's settings (0x3ca words), as the IGS036 keeps them
// The game's block: its command and argument at 5 and 6 (8 words), the
// settings it hands over (command 0xd) from 8.
//
// The IGS036's answers: the stage flow (command 0x13, see stageAfter), the
// stage's segments (5), its
// ROM version (0x10), the operator's settings (0xd stores them, in the
// game's directory, 0xe reads them back), the game's action groups and the
// ticket dispenser (0x15 pays tickets out, 0x14 tells how many are still
// owed, 0x16 stops it; the game counts them as they come, word 14), and the
// weapons' data (0xf, see weaponsInit).

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "igs.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"
#include "../patching/flowControl.h"
#include "../../minhook/include/MinHook.h"
#include <SDL3/SDL_gamepad.h>

#define PLX_OK 0x200
#define PLX_BARS 6
#define PLX_BAR_SIZE 0x10000

#define MCU_KEYS 5
#define MCU_HELM 6
#define MCU_SWITCHES 9
#define MCU_COUNTER1 12
#define MCU_COUNTER2 13
#define MCU_TICKETS 14
#define MCU_ACK 15
#define MCU_RESULT 16
#define MCU_SETTINGS 17
#define PC_SETTINGS 8
#define SETTINGS_WORDS 0x3ca

#define CMD_SOD 5
// Past the stage's farthest object, the end of its segment; the boat's
// cruising speed in it (the ships start at 5 and speed up to it, braking for
// the segment's end), so that it gets there in about SEGMENT_FRAMES of the
// fight's 3600 (60 s), not slower than SEGMENT_SPEED.
#define SEGMENT_MARGIN 16000
#define SEGMENT_FRAMES 3000
#define SEGMENT_SPEED 30
#define CMD_GROUP_NEW 6
#define CMD_GROUP_FREE 7
#define CMD_GROUP_GET 8
#define CMD_GROUP_SET 9
#define CMD_GROUP_ADD 10
#define CMD_WEAPONS 0xf
#define CMD_SET_SETTINGS 0xd
#define CMD_GET_SETTINGS 0xe
#define CMD_ROM_VERSION 0x10
#define CMD_STAGE 0x13
#define CMD_TICKETS_OWED 0x14
#define CMD_TICKETS_PAY 0x15
#define CMD_TICKETS_STOP 0x16

// A ticket out every this many rounds (the game's frames: 5 a second).
#define TICKET_ROUNDS 12

// The game's action groups (its animations' sets, 128) are allocated by the
// IGS036, which keeps three values of each for the game (commands 8 to 10:
// the group in the argument's low byte, the value in bits 8-10, the operand
// from bit 11).
#define GROUPS 128
#define GROUP_VALUES 3


// The stick's dead zone.
#define HELM_DEADZONE 0.12

static const IgsGame *game;
static void (*mcuProcess)(void);
static uint32_t settings[SETTINGS_WORDS];
static char settingsPath[PATH_MAX];
static int settingsKept;
static int ticketsOwed;
static struct
{
    int used;
    int32_t value[GROUP_VALUES];
} groups[GROUPS];

static unsigned char bars[PLX_BARS][PLX_BAR_SIZE] __attribute__((aligned(4096)));

// The weapons' data package (command 0xf: 168 types of 8 words, from word 17
// of the IGS036's block): the cabinet's is in the IGS036's ROM, which is not
// on the disk. Each type is a shot of the player's (below 0x2f), an enemy or
// an object of the sea (0x2f-0xa2) or a special one; its words: the power of
// its hits (the damage is power * (100 - armour) / 100), its speed and its
// range (shots: the distance it flies), the frames before the cannon that
// fired it fires again (the player's; an enemy's shot: a lift, -1.5 times
// it), its hit points (capped at 999, scaled with the difficulty), one
// unused, the width and depth its homing looks in (0: none). These are made
// up to play well (the player has 900 hit points and rams for 100; the
// cabinet's shots, seen on video, fly about half the screen in half a
// second), and are overridden by
// var/igs036-weapons.txt in the game's directory: lines of a type then its 8
// words.
#define WEAPON_TYPES 168
#define WEAPON_WORDS 8
#define WEAPON_ENEMIES 0x2f
#define WEAPON_SPECIALS 0xa3
#define WEAPON_RELOAD 10

static int32_t weapons[WEAPON_TYPES][WEAPON_WORDS];

static void weaponsInit(const char *dir)
{
    char path[PATH_MAX], line[256];
    FILE *f;

    for (int t = 0; t < WEAPON_TYPES; t++)
    {
        int32_t *w = weapons[t];
        if (t < WEAPON_ENEMIES)
        {
            w[0] = 100;
            w[1] = 200;
            w[2] = 8000;
            w[3] = WEAPON_RELOAD;
            w[4] = 1;
        }
        else if (t < WEAPON_SPECIALS)
        {
            w[0] = 50;
            w[4] = 100;
        }
        else
        {
            w[0] = 200;
            w[1] = 150;
            w[2] = 8000;
            w[3] = WEAPON_RELOAD;
            w[4] = 1;
        }
    }
    snprintf(path, sizeof(path), "%s/var/igs036-weapons.txt", dir);
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof(line), f))
    {
        int t, v[WEAPON_WORDS];
        if (line[0] == '#' || sscanf(line, "%i %i %i %i %i %i %i %i %i", &t, &v[0], &v[1], &v[2], &v[3], &v[4],
                                     &v[5], &v[6], &v[7]) != 1 + WEAPON_WORDS)
            continue;
        if (t >= 0 && t < WEAPON_TYPES)
            memcpy(weapons[t], v, sizeof(v));
    }
    fclose(f);
}

// PlxPciDeviceFind: the card (vendor 0x10b5, device 0x9030) is the first.
__attribute__((force_align_arg_pointer)) static int plxDeviceFind(unsigned char *device)
{
    device[2] = 0x30;
    device[3] = 0x90;
    device[4] = 0xb5;
    device[5] = 0x10;
    return 1;
}

__attribute__((force_align_arg_pointer)) static int plxDeviceOpen(unsigned char *device, int *handle)
{
    (void)device;
    *handle = open("/dev/null", O_RDWR);
    return PLX_OK;
}

__attribute__((force_align_arg_pointer)) static int plxBarMap(int handle, unsigned char bar, void **address)
{
    (void)handle;
    if (bar >= PLX_BARS)
        return 0x21b;
    *address = bars[bar];
    return PLX_OK;
}

static void loadSettings(void)
{
    FILE *f = fopen(settingsPath, "rb");

    if (!f)
        return;
    settingsKept = fread(settings, sizeof(settings), 1, f) == 1;
    if (!settingsKept)
        memset(settings, 0, sizeof(settings));
    fclose(f);
}

static void saveSettings(void)
{
    FILE *f = fopen(settingsPath, "wb");

    if (!f || fwrite(settings, sizeof(settings), 1, f) != 1)
        log_error("IGS: cannot save the operator's settings to %s", settingsPath);
    if (f)
        fclose(f);
}

// A word forced from the environment, to find what the game does with it:
// IGS_IN<n>=value, or IGS_IN<n>=seconds:value,seconds:value,... (the value
// from that many seconds after the start on).
static int forced(int word, uint32_t *value)
{
    static struct timespec start;
    struct timespec now;
    char name[16];
    const char *env, *step;
    double elapsed;

    snprintf(name, sizeof(name), "IGS_IN%d", word);
    if (!(env = getenv(name)))
        return 0;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!start.tv_sec)
        start = now;
    elapsed = (double)(now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
    if (!strchr(env, ':'))
    {
        *value = (uint32_t)strtoul(env, NULL, 0);
        return 1;
    }
    *value = 0;
    for (step = env; step; step = strchr(step, ','), step = step ? step + 1 : NULL)
        if (strtod(step, NULL) <= elapsed)
            *value = (uint32_t)strtoul(strchr(step, ':') + 1, NULL, 0);
    return 1;
}

// The game's stage flow is the IGS036's: each stage ends with an event (its
// command 0x13's argument) and plays the stage the IGS036 answers (an index
// in the game's switch of stages; 12 ends the game). The table is rebuilt
// from what the stages are: 2 the ship select, 3 the count of fights and
// chapters (the game starts it at -1: the next fight's choice needs it run
// first) and the chapter's end (continue or not), 4 the next fight's choice
// (its type: events 4, 5, 6; type 1 goes to stage 6 itself), 5, 6 and 9 the
// fights of types 0, 1 and 3, 7 the boss (type 2: on what fight 5 loaded and
// kept, with the time 5 left; it frees it all and shows the result itself),
// 10 the result, 11 the game over. Fight 5 goes on to the next fight's
// choice, which then picks the boss: a result between them would play over
// what is kept. Events 0 (no continue), 12 (score too low) and 14 (test) end
// it. Pirate Revenge plays the same stages (6 is its race, 9 its staff
// fight), but starts each game with event 2.
#define STAGE_END 12

static const int stageAfter[] = {
    [0] = STAGE_END, // continue timed out
    [1] = 3,         // fight (type 0) over: the next fight (the boss)
    [2] = 1,         // game starting (Pirate Revenge): its start
    [3] = 2,         // game started: ship select
    [4] = 5,         // fight type 0
    [5] = 7,         // fight type 2, the boss
    [6] = 9,         // fight type 3
    [7] = STAGE_END, // game over seen
    [8] = 10,        // boss over (7 shows the result itself, then event 9)
    [9] = 3,         // result seen: chapter's end
    [10] = 10,       // fight (type 3) over: result
    [11] = 3,        // ship chosen: the chapter's count (from -1) and on
    [12] = STAGE_END, // score too low to go on
    [13] = 4,        // going on: choose the fight
    [14] = STAGE_END, // test button
};

static JVSIO *inputState(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
}

// The cabinet's switches: the left and right buttons (button 1 and 2: the
// mouse's left and right buttons, the pad's A and B), the bomb button if
// there is one (button 3: the mouse's middle one, the pad's X), test and
// service. Start is the button that starts and confirms: the bomb, else the
// right one.
static uint32_t keys(void)
{
    JVSIO *io = inputState();
    unsigned int player = io->state.inputSwitch[PLAYER_1];
    int start = !!(player & BUTTON_START);
    uint32_t word = 0;

    if (player & BUTTON_1)
        word |= game->keyLeft;
    if (player & BUTTON_2 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_EAST) || (start && !game->keyBomb))
        word |= game->keyRight;
    if (player & BUTTON_3 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_WEST) || start)
        word |= game->keyBomb;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        word |= game->keyTest;
    if (player & BUTTON_SERVICE)
        word |= game->keyService;
    return word;
}

// The helm: the pad's left stick or evdev's ANALOGUE_1, the arrows (or the
// d-pad) all the way. The game reads it as 100 - value / 5.12 percent.
// With a helmSpeed, it turns no faster than that a frame (the game takes a
// fast turn, counted in its frames, for a dodge), and the dodge buttons (button 4 and 5: c and v, the
// pad's LB and RB) turn it all the way left or right at once.
static uint32_t helm(void)
{
    static double slewed;
    static unsigned int lastFrame;
    JVSIO *io = inputState();
    unsigned int player = io->state.inputSwitch[PLAYER_1];
    double turn = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;

    if (getConfig()->inputMode == 2 && io->analogueMax > 0)
        turn += io->state.analogueChannel[ANALOGUE_1] * 2.0 / io->analogueMax - 1;
    if (turn > -HELM_DEADZONE && turn < HELM_DEADZONE)
        turn = 0;
    turn += !!(player & BUTTON_RIGHT) - !!(player & BUTTON_LEFT);
    turn = turn > 1 ? 1 : turn < -1 ? -1 : turn;
    if (game->helmSpeed > 0)
    {
        unsigned int frame = igsFrames();
        double step = game->helmSpeed * (frame - lastFrame);
        int dodge = !!(player & BUTTON_5 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) -
                    !!(player & BUTTON_4 || desktopGamepadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));

        lastFrame = frame;
        if (dodge)
            slewed = dodge;
        else if (turn > slewed)
            slewed = turn < slewed + step ? turn : slewed + step;
        else
            slewed = turn > slewed - step ? turn : slewed - step;
        turn = slewed;
    }
    if (game->helmReversed)
        turn = -turn;
    return (uint32_t)((turn + 1) * (game->helmMax - 1) / 2 + 0.5);
}

// Coins inserted since the last round, coin slots 1 and 2.
static void coins(uint32_t *in)
{
    static int seen[2] = {-1, -1};
    JVSIO *io = inputState();

    for (int c = 0; c < 2; c++)
    {
        int count = io->state.coinCount[c];
        in[MCU_COUNTER1 + c] = seen[c] >= 0 && count > seen[c] ? (uint32_t)(count - seen[c]) : 0;
        seen[c] = count;
    }
}

// The segment fight's loop leaves on its time up, unless the segments are
// done, without asking the IGS036 for the next stage: the game plays that
// fight again over the one still loaded (its action files' ids taken: the
// game closes the file on that error and reads on). Its time up ends
// the segments, as its debug key does: the fight ends as finished, on to
// its result.
static void timeUp(void)
{
    int32_t *segment = (int32_t *)(uintptr_t)game->segment;
    int32_t count;

    if (!game->fightTime)
        return;
    count = *(int32_t *)(uintptr_t)game->segmentCount;
    if (*(int32_t *)(uintptr_t)game->stage == game->segmentFight &&
        *(int32_t *)(uintptr_t)game->fightTime < 1 && *segment < count)
        *segment = count;
}

// The segment fight keeps what it loaded for the boss's fight that may
// follow, which frees it; any other fight next (another one, one of its own
// types, a new game) would load over it: the game's loader errors on the
// action files' ids still taken, closes the file and reads on. Each fight's
// setup frees what is still loaded first, with the segment fight's own
// teardown.
static void (*realFightSetups[3])(void);
static void (*realWeaponsRelease)(void);
static int fightLoaded;

static void fightSetup(int fight)
{
    if (fightLoaded)
    {
        void (*release)(void) = (void (*)(void))(uintptr_t)game->fightRelease;
        *(int32_t *)(uintptr_t)game->fightReleaseFlag = 1;
        release();
    }
    realFightSetups[fight]();
    fightLoaded = fight == 0;
}

__attribute__((force_align_arg_pointer)) static void fightSetup0(void)
{
    fightSetup(0);
}

__attribute__((force_align_arg_pointer)) static void fightSetup1(void)
{
    fightSetup(1);
}

__attribute__((force_align_arg_pointer)) static void fightSetup2(void)
{
    fightSetup(2);
}

__attribute__((force_align_arg_pointer)) static void weaponsRelease(void)
{
    fightLoaded = 0;
    realWeaponsRelease();
}

static uint32_t reply(int command, int argument)
{
    int group = argument & 0xff, value = (argument >> 8) & 7, operand = argument >> 11;

    if (command >= CMD_GROUP_GET && command <= CMD_GROUP_ADD && (group >= GROUPS || value >= GROUP_VALUES))
        return 0;
    switch (command)
    {
    case CMD_SOD:
    {
        // The stage's segments (10 of start, length, attribute; -1: none):
        // the cabinet's are the IGS036's (the SOD file's copy, section 11,
        // is blank). One segment from the start to past the stage's farthest
        // object stands in for them: the game spawns the objects of a
        // segment and ends the stage at its end; its attribute is the boat's
        // speed there.
        int entry = operand, word = value, far = 0, speed;
        if (entry != 0 || word > 2)
            return (uint32_t)-1;
        if (word == 0)
            return 0;
        for (int i = 0; i < game->sodObjects; i++)
        {
            int depth = *(int *)(uintptr_t)(game->sodObjectDepth + (uint32_t)(i * game->sodObjectSize));
            if (depth > far)
                far = depth;
        }
        far += SEGMENT_MARGIN;
        if (word == 1)
            return (uint32_t)far;
        speed = (far + SEGMENT_FRAMES - 1) / SEGMENT_FRAMES;
        return (uint32_t)(speed > SEGMENT_SPEED ? speed : SEGMENT_SPEED);
    }
    case CMD_GROUP_NEW:
        for (group = 0; group < GROUPS; group++)
            if (!groups[group].used)
            {
                memset(&groups[group], 0, sizeof(groups[group]));
                groups[group].used = 1;
                return (uint32_t)group;
            }
        log_error("IGS036: no action group left");
        return 0;
    case CMD_GROUP_FREE:
        if (argument >= 0 && argument < GROUPS)
            memset(&groups[argument], 0, sizeof(groups[argument]));
        return 0;
    case CMD_GROUP_GET:
        return (uint32_t)groups[group].value[value];
    case CMD_GROUP_SET:
        groups[group].value[value] = operand;
        return 0;
    case CMD_GROUP_ADD:
        groups[group].value[value] += operand;
        return 0;
    case CMD_STAGE:
    {
        // IGS_STAGE<event>=<stage> (debugging): another stage after it.
        char name[32];
        snprintf(name, sizeof(name), "IGS_STAGE%d", argument);
        if (getenv(name))
            return (uint32_t)atoi(getenv(name));
    }
        if (argument >= 0 && argument < (int)(sizeof(stageAfter) / sizeof(stageAfter[0])) &&
            (argument == 0 || stageAfter[argument]))
            return (uint32_t)stageAfter[argument];
        log_error("IGS036: no stage after event %d", argument);
        return STAGE_END;
    case CMD_ROM_VERSION:
        // I:S100 and O:S100.
        return 100u << 3 | 100u << 17;
    case CMD_TICKETS_PAY:
        ticketsOwed = argument > 0 ? argument : 0;
        return 0;
    case CMD_TICKETS_OWED:
        return (uint32_t)ticketsOwed;
    case CMD_TICKETS_STOP:
        ticketsOwed = 0;
        return 0;
    case CMD_GET_SETTINGS:
        // Not 0: none kept, the game sets its defaults (and hands them over).
        return !settingsKept;
    default:
        return 0;
    }
}

// The game's defaults leave the helm uncalibrated (both ends 0): the cabinet's
// operator runs VR Setting first. The full range stands in (the game reads
// the helm as 100 - value / 5.12: 100 to -99, centred on 0), in the game's
// settings as in the ones kept.
static void helmCalibrate(void)
{
    int32_t *kept = (int32_t *)&settings[game->helmCalibrationWord];
    int32_t *used = (int32_t *)(uintptr_t)game->helmCalibration;

    if (!game->helmCalibration || kept[0] || kept[1] || kept[2])
        return;
    kept[0] = used[0] = 100;
    kept[1] = used[1] = -99;
    kept[2] = used[2] = 0;
}

__attribute__((force_align_arg_pointer)) static uint32_t mcuExchange(void)
{
    uint32_t *in = (uint32_t *)(uintptr_t)game->mcuIn;
    uint32_t *out = (uint32_t *)(uintptr_t)game->pcOut;
    int command = *(int *)(uintptr_t)game->pendingCommand;
    int argument = *(int *)(uintptr_t)game->pendingArgument;
    static int lastCommand = -1, lastArgument, ticketRound;

    in[0] = MCU_SETTINGS + SETTINGS_WORDS;
    in[MCU_KEYS] = keys();
    in[MCU_HELM] = helm();
    in[MCU_SWITCHES] = 0;
    coins(in);
    in[MCU_TICKETS] = 0;
    if (ticketsOwed > 0 && ++ticketRound >= TICKET_ROUNDS)
    {
        ticketRound = 0;
        ticketsOwed--;
        in[MCU_TICKETS] = 1;
    }
    for (int w = 1; w < MCU_ACK; w++)
        forced(w, &in[w]);
    if (getenv("IGS_TRACE"))
    {
        static uint32_t lastKeys = ~0u;
        static unsigned rounds;
        rounds++;
        if (in[MCU_KEYS] != lastKeys)
            fprintf(stderr, "IGS036: keys 0x%x (round %u)\n", in[MCU_KEYS], rounds);
        lastKeys = in[MCU_KEYS];
    }
    in[MCU_ACK] = (uint32_t)command;
    if (command)
    {
        in[MCU_RESULT] = reply(command, argument);
        if (command == CMD_GET_SETTINGS)
            memcpy(&in[MCU_SETTINGS], settings, sizeof(settings));
        if (command == CMD_WEAPONS)
            memcpy(&in[MCU_SETTINGS], weapons, sizeof(weapons));
        if (getenv("IGS_TRACE") && (command != lastCommand || argument != lastArgument))
            fprintf(stderr, "IGS036: command 0x%x (0x%x) -> 0x%x\n", command, argument, in[MCU_RESULT]);
        lastCommand = command;
        lastArgument = argument;
    }

    timeUp();
    mcuProcess();

    if (command == CMD_SET_SETTINGS)
    {
        memcpy(settings, &out[PC_SETTINGS], sizeof(settings));
        settingsKept = 1;
        helmCalibrate();
        saveSettings();
    }
    return in[MCU_ACK];
}

void igsIoInstall(const IgsGame *g)
{
    char cwd[PATH_MAX];

    game = g;
    *(void **)&mcuProcess = (void *)(uintptr_t)g->mcuProcess;
    if (!getcwd(cwd, sizeof(cwd)))
        cwd[0] = '\0';
    snprintf(settingsPath, sizeof(settingsPath), "%s/var/igs036.bin", cwd);
    loadSettings();
    weaponsInit(cwd);

    // With evdev input the pads are evdev's: the SDL gamepad, read as well,
    // would give each press and turn twice.
    if (getConfig()->inputMode != 2)
        desktopGamepadEnable();
    detourFunction(g->plxDeviceFind, plxDeviceFind);
    detourFunction(g->plxDeviceOpen, plxDeviceOpen);
    detourFunction(g->plxBarMap, plxBarMap);
    detourFunction(g->mcuExchange, mcuExchange);

    if (!g->weaponsRelease)
        return;
    void (*setups[])(void) = {fightSetup0, fightSetup1, fightSetup2};
    for (int i = 0; i < 3; i++)
        if (MH_CreateHook((void *)(uintptr_t)g->fightSetups[i], (void *)setups[i], (void **)&realFightSetups[i]) != MH_OK)
            log_error("IGS: cannot hook fight setup %d", i);
    if (MH_CreateHook((void *)(uintptr_t)g->weaponsRelease, (void *)weaponsRelease, (void **)&realWeaponsRelease) != MH_OK)
        log_error("IGS: cannot hook the weapons' release");
}
