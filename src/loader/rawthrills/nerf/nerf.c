// Nerf Arcade (Raw Thrills, 2018): a 64-bit Unity 2018.2.21f1 game for
// Linux on the Raw Thrills Unity base (rawthrills/unity): librio.so (the
// RIO1: switches, the guns' potentiometers, lamps, solenoids),
// libUnityNatives.so and libhasp_linux_23714.so. The dumps carry the
// Windows player instead; the game runs under Unity's own Linux player
// (docs/nerf-arcade.md).
//
// Its part: the guns (two, on the RIO1's ADCs, the desktop mouse P1's),
// their calibration, the light gun border.
#include "../unity/unity.h"

static const UnitySwitchKey nerfSwitches[] = {
    {RIO_GEAR3_SW, "PLAYER_1_BUTTON_1"},     {RIO_DASH_SW, "PLAYER_2_BUTTON_1"},
    {RIO_GEAR1_SW, "PLAYER_1_BUTTON_2"},     {RIO_GEAR4_SW, "PLAYER_2_BUTTON_2"},
    {RIO_START1_SW, "PLAYER_1_BUTTON_START"}, {RIO_START2_SW, "PLAYER_2_BUTTON_START"},
    {RIO_COIN1_SW, "PLAYER_1_COIN"},         {RIO_COIN2_SW, "PLAYER_2_COIN"},
    {RIO_TEST_SW, "TEST_BUTTON"},            {RIO_SERVICE_SW, "PLAYER_1_BUTTON_SERVICE"},
    {RIO_VOL_UP_SW, "PLAYER_1_BUTTON_UP"},   {RIO_VOL_DN_SW, "PLAYER_1_BUTTON_DOWN"},
    {0, NULL},
};

// 5/6 the coins, 1/2 the starts, F2 test (held a moment, the test menu), F1
// service, Page Up/Down the volume; the mouse is P1's gun.
static const UnitySwitchKeysym nerfKeys[] = {
    {RIO_COIN1_SW, '5'},      {RIO_COIN2_SW, '6'},          {RIO_START1_SW, '1'},
    {RIO_START2_SW, '2'},     {RIO_TEST_SW, 0xffbf /* F2 */}, {RIO_SERVICE_SW, 0xffbe /* F1 */},
    {RIO_VOL_UP_SW, 0xff55 /* Prior */}, {RIO_VOL_DN_SW, 0xff56 /* Next */},
    {0, 0},
};

// A gun aims with both axes or not at all.
static void nerfEvdevState(UnityInput *in)
{
    for (int g = 0; g < 2; g++)
        if (in->analog[g * 2] < 0.f || in->analog[g * 2 + 1] < 0.f)
            in->analog[g * 2] = in->analog[g * 2 + 1] = -1.f;
}

// No gun: the middle; Y: the ADC counts from the bottom.
static float nerfAdc(int adc, float v)
{
    if (v < 0.f)
        v = 0.5f;
    return (adc - RIO_ADC1) % 2 ? 1.f - v : v;
}

// The guns' calibration. The game scales each ADC by the range its test
// menu stored (preferences 67..74: P1 min X, max X, min Y, max Y, then P2),
// and without one it opens the test menu at boot to have the guns
// calibrated. The aim here is absolute already, on the RIO's full scale
// (unityRio.c): that range is written into the preferences, through Mono's
// embedding API, and the game reads it again
// (IOManager.GrabCalibrationFromPreferences). At RIO_Init: IOManager.Start
// has read the calibration just before.
static void nerfForceCalibration(void)
{
    // P1 X min, max, Y min, max; P2 the same.
    static const int range[8] = {0, UNITY_ADC_MAX, 0, UNITY_ADC_MAX, 0, UNITY_ADC_MAX, 0, UNITY_ADC_MAX};
    MonoImage *image = unityMonoGame();
    MonoObject *prefs, *io;
    int changed = 0, ok = 1;

    if (!image)
    {
        unityLog("no Mono runtime, the guns stay uncalibrated\n");
        return;
    }
    prefs = unityMonoStaticProperty(image, "", "TestPreferences", "Instance");
    io = unityMonoSingleton(image, "", "IOManager");
    if (!prefs || !io)
    {
        unityLog("calibration: the game's classes not found, the guns stay uncalibrated\n");
        return;
    }
    for (int i = 0; i < 8 && ok; i++)
    {
        int pref = 67 + i, value = range[i];
        void *args[2] = {&pref, &value};
        MonoObject *current = unityMonoCall(prefs, "TestPreferences:Prefs_GetI(int)", args, &ok);
        if (!ok || !current)
            break;
        if (unityMonoUnboxInt(current) == value)
            continue;
        unityMonoCall(prefs, "TestPreferences:Prefs_Set(int,int)", args, &ok);
        changed = 1;
    }
    if (!ok)
    {
        unityLog("calibration: the game threw, the guns stay as they are\n");
        return;
    }
    if (changed)
        unityMonoCall(prefs, "TestPreferences:WritePrefs()", NULL, NULL);
    unityMonoCall(io, "IOManager:GrabCalibrationFromPreferences()", NULL, NULL);
    unityLog("calibration %s (0..%d)\n", changed ? "set" : "kept", UNITY_ADC_MAX);
}

const UnityGame nerfGame = {
    .name = "Nerf Arcade",
    .dataDir = NULL, // the default
    .switches = nerfSwitches,
    .keys = nerfKeys,
    .mouseGun = 1,
    .evdevState = nerfEvdevState,
    .video = 1,
    .rioInit = nerfForceCalibration,
    .rioAdc = nerfAdc,
};
