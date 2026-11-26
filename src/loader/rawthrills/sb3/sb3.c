// Superbikes 3 (Raw Thrills, fnfmega_Data): a 64-bit Unity game on the Raw
// Thrills Unity base (rawthrills/unity), driving the same RIO1 and dongle
// API as Nerf Arcade; its RIO2 never answers, so it takes the RIO1
// (docs/superbikes3.md).
//
// Its part: the handlebar, throttle and brake (ANALOGUE_1..3, the arrows on
// the desktop), their calibration (sb3Mono.c), the handlebar's motor and
// camera (sb3Plugins.c), the cabinet's /pm, cabinet type 1.
#include <string.h>
#include <sys/stat.h>
#include "sb3.h"

// Superbikes 3's cabinet: START2 is its brake switch, VIEW1 the view
// button, TUNES the music button.
static const UnitySwitchKey sb3Switches[] = {
    {RIO_START1_SW, "PLAYER_1_BUTTON_START"}, {RIO_START2_SW, "PLAYER_1_BUTTON_1"},
    {RIO_VIEW1_SW, "PLAYER_1_BUTTON_2"},      {RIO_TUNES_SW, "PLAYER_1_BUTTON_3"},
    {RIO_COIN1_SW, "PLAYER_1_COIN"},          {RIO_COIN2_SW, "PLAYER_2_COIN"},
    {RIO_TEST_SW, "TEST_BUTTON"},             {RIO_SERVICE_SW, "PLAYER_1_BUTTON_SERVICE"},
    {RIO_VOL_UP_SW, "PLAYER_1_BUTTON_UP"},    {RIO_VOL_DN_SW, "PLAYER_1_BUTTON_DOWN"},
    {0, NULL},
};

// Superbikes 3's own keyboard map is its developers', locked unless
// unlocked by a key combination (RTInput): the cabinet's switches here.
static const UnitySwitchKeysym sb3Keys[] = {
    {RIO_COIN1_SW, '5'},          {RIO_START1_SW, 0xff0d /* Return */}, {RIO_START1_SW, 's'},
    {RIO_VIEW1_SW, 'v'},          {RIO_TUNES_SW, 't'},
    {RIO_TEST_SW, 0xffbf /* F2 */}, {RIO_SERVICE_SW, 0xffbe /* F1 */},
    {RIO_VOL_UP_SW, 0xff55 /* Prior */}, {RIO_VOL_DN_SW, 0xff56 /* Next */},
    {0, 0},
};

// Superbikes 3 reads its version from the cabinet's /pm (the dump's
// version.txt and original_version.txt, in the game's directory) and keeps
// its updater's files there (workingAsExpected, audits/): the rest of /pm
// is LocalData/system/pm (the cabinet's own files aside).
#define SB3_PM "LocalData/system/pm"

static const char *sb3MapPath(const char *path)
{
    static __thread char mapped[4096];
    if (unityCabinetFile(path))
        return path;
    // Mono's FileStream looks for the directory first.
    if (!strcmp(path, "/pm") || !strcmp(path, "/pm/"))
        return SB3_PM;
    if (!strncmp(path, "/pm/", 4))
    {
        if (!strcmp(path, "/pm/version.txt") || !strcmp(path, "/pm/original_version.txt"))
            return path + 4;
        snprintf(mapped, sizeof(mapped), SB3_PM "/%s", path + 4);
        return mapped;
    }
    return path;
}

static void sb3Init(void)
{
    mkdir(SB3_PM, 0755);
    mkdir(SB3_PM "/audits", 0755);
}

// The brake is a switch: the pedal presses it half way down.
static void sb3EvdevState(UnityInput *in)
{
    if (in->analog[2] > 0.5f)
        in->switches[RIO_START2_SW] = 1;
}

// On the keyboard, an analog handlebar: Left/Right lean it further the
// longer they are held (full over SB3_LEAN_MS), so a tap is a small
// correction; let go (or the other way) it comes back to the middle faster
// (SB3_RETURN_MS from full). Up and Down: the throttle and the brake, or
// the test menu's up and down (sb3RioSample, by the game's state).
#define SB3_LEAN_MS 1000.f
#define SB3_RETURN_MS 300.f

static void sb3DesktopRide(UnityInput *in, const UnityInput *last, const char keys[32])
{
    int left = unityKeysymDown(keys, 0xff51), right = unityKeysymDown(keys, 0xff53);
    // -1 (full left) .. 1 (full right)
    float lean = last->analog[0] < 0.f ? 0.f : last->analog[0] * 2.f - 1.f,
          dir = (float)(right - left), leanStep = UNITY_POLL_MS / SB3_LEAN_MS * 2.f,
          returnStep = UNITY_POLL_MS / SB3_RETURN_MS * 2.f;
    if (dir != 0.f && lean * dir >= 0.f) // leaning further that way
        lean += dir * leanStep;
    else if (lean > 0.f) // let go, or the other way: back to the middle
        lean = lean - returnStep < 0.f ? (dir != 0.f ? -leanStep : 0.f) : lean - returnStep;
    else if (lean < 0.f)
        lean = lean + returnStep > 0.f ? (dir != 0.f ? leanStep : 0.f) : lean + returnStep;
    lean = lean < -1.f ? -1.f : lean > 1.f ? 1.f : lean;
    in->analog[0] = (lean + 1.f) / 2.f;
    in->arrowUp = unityKeysymDown(keys, 0xff52);
    in->arrowDown = unityKeysymDown(keys, 0xff54);
    in->analog[1] = in->arrowUp ? 1.f : 0.f;
}

// Once a RIO1 sample: the handlebar's range now and then, its motor; the
// arrows: in its test menu, its up and down (the volume buttons there; the
// brake goes back), the throttle kept for the calibration; else the brake.
static void sb3RioSample(UnityInput *in)
{
    static unsigned samples;
    if (samples++ % 60 == 0)
        sb3SteerRange();
    sb3FfbUpdate();
    if (in->arrowUp || in->arrowDown)
    {
        int menu = sb3InTestMenu();
        if (in->arrowDown)
            in->switches[menu ? RIO_VOL_DN_SW : RIO_START2_SW] = 1;
        if (in->arrowUp && menu)
            in->switches[RIO_VOL_UP_SW] = 1;
    }
}

// No handlebar: straight; no pedal: released.
static float sb3Adc(int adc, float v)
{
    return v < 0.f ? (adc == RIO_ADC1 ? 0.5f : 0.f) : v;
}

const UnityGame sb3Game = {
    .name = "Superbikes 3",
    .dataDir = "/fnfmega_Data",
    .init = sb3Init,
    .mapPath = sb3MapPath,
    .dongleCabType = 1, // the standard cabinet (2: the motion one)
    .switches = sb3Switches,
    .keys = sb3Keys,
    .evdevState = sb3EvdevState,
    .desktopState = sb3DesktopRide,
    // Its RIO1 manager reads the calibration once the board is up.
    .rioInit = sb3ForceCalibration,
    .rioSample = sb3RioSample,
    .rioAdc = sb3Adc,
    .rioKeypad = -1, // 0..11: a key of its keypad held
};
