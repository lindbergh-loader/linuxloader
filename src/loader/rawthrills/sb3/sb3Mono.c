// Superbikes 3's calibration. The game scales the RIO1's handlebar ADC by
// the range its test menu stored (preferences RIO_STEER_MIN/MAX/CENTER),
// its throttle's by RIO_GAS_MIN/MAX, and boots into the test menu while
// CALIBRATED is 0; the dumps' preferences carry no handlebar range (their
// Windows player did without the RIO). The RIO's full scale, centred, is
// written there through Mono's embedding API when the game opens the RIO1,
// before its RIO1 manager reads it (GameCtrl.MapInput); the game saves them
// with its other preferences.
#include <math.h>
#include <stdio.h>
#include "../nerf/nerf.h"

// PrefID
enum
{
    PREF_CALIBRATED = 2,
    PREF_RIO_GAS_MIN = 90,
    PREF_RIO_GAS_MAX = 91,
    PREF_RIO_STEER_MIN = 92,
    PREF_RIO_STEER_MAX = 93,
    PREF_RIO_STEER_CENTER = 94,
};

void sb3ForceCalibration(void)
{
    static const int prefs[][2] = {
        {PREF_RIO_STEER_MIN, 0},   {PREF_RIO_STEER_MAX, NERF_ADC_MAX}, {PREF_RIO_STEER_CENTER, (NERF_ADC_MAX + 1) / 2},
        {PREF_RIO_GAS_MIN, 0},     {PREF_RIO_GAS_MAX, NERF_ADC_MAX},   {PREF_CALIBRATED, 1},
    };
    MonoImage *image = nerfMonoGame();
    MonoMethod *get = nerfMonoMethod(image, "Prefs", "Prefs:GetI(PrefID)");
    MonoMethod *set = nerfMonoMethod(image, "Prefs", "Prefs:Set(PrefID,int)");
    MonoObject *exc = NULL;
    int changed = 0;

    if (!get || !set)
    {
        nerfLog("calibration: the game's Prefs not found, the handlebar stays uncalibrated\n");
        return;
    }
    for (size_t i = 0; i < sizeof(prefs) / sizeof(prefs[0]); i++)
    {
        int id = prefs[i][0], value = prefs[i][1];
        void *args[2] = {&id, &value};
        MonoObject *current = nerfMonoInvoke(get, NULL, args, &exc);
        if (exc || !current)
            break;
        if (nerfMonoUnboxInt(current) == value)
            continue;
        nerfMonoInvoke(set, NULL, args, &exc);
        if (exc)
            break;
        changed = 1;
    }
    if (exc)
        nerfLog("calibration: the game threw, the handlebar stays as it is\n");
    else
        nerfLog("calibration %s (0..%d)\n", changed ? "set" : "kept", NERF_ADC_MAX);
}

// GlobalState.TestMenu, from GameCtrl.inst.gState.
#define GLOBAL_STATE_TEST_MENU 3

int sb3InTestMenu(void)
{
    static MonoMethod *inst, *state;
    static int looked;
    MonoObject *ctrl, *value, *exc = NULL;

    if (!looked)
    {
        MonoImage *image = nerfMonoGame();
        inst = nerfMonoMethod(image, "GameCtrl", "GameCtrl:get_inst()");
        state = nerfMonoMethod(image, "GameCtrl", "GameCtrl:get_gState()");
        looked = 1;
        if (!inst || !state)
            nerfLog("GameCtrl's state not found: the arrows ride in the test menu too\n");
    }
    if (!inst || !state || !(ctrl = nerfMonoInvoke(inst, NULL, NULL, &exc)) || exc)
        return 0;
    value = nerfMonoInvoke(state, ctrl, NULL, &exc);
    return !exc && value && nerfMonoUnboxInt(value) == GLOBAL_STATE_TEST_MENU;
}

// The handlebar on the RIO1. The game maps it in two steps: the RIO1's
// ADC1 input, which it leaves at its raw value, then the Steer input,
// which it gives the calibrated range but overwrites with 0..1 -> -1..1
// whenever its preferences change (PrefVals.CheckDirty, from the first
// frame on): the RIO2 it was made for gives 0..1 there, the RIO1 a raw
// 0..4095, which reads full right. ADC1 is given the calibrated range to
// 0..1 here (centred as GameInputManager.SetSteeringRange does), and the
// game's own step does the rest.
void sb3SteerRange(void)
{
    static MonoMethod *inst, *input, *getI, *setRange;
    static int looked;
    MonoObject *ctrl, *manager, *exc = NULL, *v;
    int prefs[3] = {PREF_RIO_STEER_MIN, PREF_RIO_STEER_MAX, PREF_RIO_STEER_CENTER}, value[3], adc1 = 0;
    float rawMin, rawMax, outMin = 0.f, outMax = 1.f;

    if (!looked)
    {
        MonoImage *image = nerfMonoGame();
        inst = nerfMonoMethod(image, "GameCtrl", "GameCtrl:get_inst()");
        input = nerfMonoMethod(image, "GameCtrl", "GameCtrl:get_input()");
        getI = nerfMonoMethod(image, "Prefs", "Prefs:GetI(PrefID)");
        setRange = nerfMonoMethod(image, "GameInputManager",
                                  "GameInputManager:SetRange(AnalogRIOInput,single,single,single,single)");
        looked = 1;
        if (!inst || !input || !getI || !setRange)
            nerfLog("the game's input not found: the handlebar is not centred\n");
    }
    if (!inst || !input || !getI || !setRange || !(ctrl = nerfMonoInvoke(inst, NULL, NULL, &exc)) || exc ||
        !(manager = nerfMonoInvoke(input, ctrl, NULL, &exc)) || exc)
        return;
    for (int i = 0; i < 3; i++)
    {
        void *args[1] = {&prefs[i]};
        if (!(v = nerfMonoInvoke(getI, NULL, args, &exc)) || exc)
            return;
        value[i] = nerfMonoUnboxInt(v);
    }
    {
        float toMin = (float)(value[0] - value[2]), toMax = (float)(value[1] - value[2]);
        float span = fabsf(toMax) > fabsf(toMin) ? -toMin : toMax;
        rawMin = (float)value[2] - span;
        rawMax = (float)value[2] + span;
    }
    if (fabsf(rawMax - rawMin) < 1.f) // no calibration: leave it be
        return;
    void *args[5] = {&adc1, &rawMin, &rawMax, &outMin, &outMax};
    nerfMonoInvoke(setRange, manager, args, &exc);
}
