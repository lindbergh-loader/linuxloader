// The RIO1 board's library (librio.so), as the game's Rio1 class calls it.
// The switches report a count that each press and each release bump, its
// low bit the switch's state (IOManager.ButtonPressed); the ADCs are 12
// bits, the scale nerfMono.c and sb3Mono.c give the games' calibration:
// Nerf's guns X from the left, Y from the bottom; Superbikes 3's handlebar
// (ADC1) from the left, its throttle (ADC2) from released. Outputs (lamps,
// solenoids, meters) are taken and dropped.
#include "nerf.h"

enum
{
    RIO_OK = 0,
    RIO_INVALID_OBJECT = -2,
};

static NerfInput input;
static int lastHeld[RIO_NUM_SW];
static unsigned int counts[RIO_NUM_SW];
static int initialised;

typedef int (*RioCallback)(int obj, uint8_t type, uint32_t data, uint32_t time);

int RIO_Init(void)
{
    if (!initialised)
    {
        nerfInputInit();
        initialised = 1;
    }
    // Nerf: IOManager.Start has read the calibration just before.
    // Superbikes 3: its RIO1 manager reads it once the board is up.
    if (nerfGame == NERF_GAME_SB3)
        sb3ForceCalibration();
    else
        nerfForceCalibration();
    return RIO_OK;
}

int RIO_Close(void) { return RIO_OK; }
int RIO_Connect(void) { return RIO_OK; }
int RIO_ConnectEx(int flags) { (void)flags; return RIO_OK; }
int RIO_Disconnect(void) { return RIO_OK; }
int RIO_Connected(void) { return RIO_OK; }
int RIO_Interface_Version(void) { return 1; }
int RIO_GetReadThreadTicks(void) { return 0; }
int RIO_GetUserByte(void) { return 0; }
int RIO_SetUserByte(int userByte) { (void)userByte; return RIO_OK; }
void RIO_DebugOutputInfo(void) {}
int RIO_IsError(int code) { return code < 0; }
int RIO_GetLastError(void) { return RIO_OK; }
void RIO_ClearLastError(void) {}
const char *RIO_ErrorToString(int code) { return code ? "error" : "ok"; }
const char *RIO_GetLibVersion(void) { return "linuxloader64"; }
int RIO_QueueMsgs(void) { return RIO_OK; }
int RIO_Uses(int obj, RioCallback cb) { (void)obj; (void)cb; return RIO_OK; }
int RIO_ProcessCallbacks(void) { return 0; }

int RIO_SampleInput(void)
{
    nerfInputSample(&input);
    if (nerfGame == NERF_GAME_SB3)
    {
        static unsigned samples;
        if (samples++ % 60 == 0)
            sb3SteerRange();
        sb3FfbUpdate();
    }
    // Superbikes 3's arrows: in its test menu, its up and down (the volume
    // buttons there; the brake goes back), the throttle kept for the
    // calibration; else the brake.
    if (nerfGame == NERF_GAME_SB3 && (input.arrowUp || input.arrowDown))
    {
        int menu = sb3InTestMenu();
        if (input.arrowDown)
            input.switches[menu ? RIO_VOL_DN_SW : RIO_START2_SW] = 1;
        if (input.arrowUp && menu)
            input.switches[RIO_VOL_UP_SW] = 1;
    }
    for (int i = 0; i < RIO_NUM_SW; i++)
    {
        int held = input.switches[i] != 0;
        if (held != lastHeld[i])
        {
            counts[i]++;
            lastHeld[i] = held;
        }
    }
    return RIO_OK;
}

int RIO_SW_Count(int obj)
{
    return obj >= 0 && obj < RIO_NUM_SW ? (int)counts[obj] : RIO_INVALID_OBJECT;
}

int RIO_SW_State(int obj)
{
    // SW_DOWN 1, SW_UP 2
    return obj >= 0 && obj < RIO_NUM_SW ? (lastHeld[obj] ? 1 : 2) : RIO_INVALID_OBJECT;
}
int RIO_SW_State_Raw(int obj) { return RIO_SW_State(obj); }

int RIO_SW_StateEx(int obj, int *count, int *duration)
{
    if (count) *count = RIO_SW_Count(obj);
    if (duration) *duration = 0;
    return RIO_SW_State(obj);
}

int RIO_ADC_Val(int obj)
{
    float v;
    if (obj < RIO_ADC1 || obj > RIO_ADC4)
        return RIO_INVALID_OBJECT;
    v = input.analog[obj - RIO_ADC1];
    if (nerfGame == NERF_GAME_SB3)
    {
        // No handlebar: straight; no pedal: released.
        if (v < 0.f)
            v = obj == RIO_ADC1 ? 0.5f : 0.f;
    }
    else
    {
        if (v < 0.f) // no gun: the middle
            v = 0.5f;
        if ((obj - RIO_ADC1) % 2) // Y: the ADC counts from the bottom
            v = 1.f - v;
    }
    return (int)(v * NERF_ADC_MAX + 0.5f);
}

int RIO_ADC_Val_Ex(int obj, int *duration)
{
    if (duration) *duration = 0;
    return RIO_ADC_Val(obj);
}

int RIO_GetValue(int obj)
{
    switch (obj)
    {
        case RIO_SERIAL: return 12345;
        case RIO_HWVERSION: return 0x0100;
        default: return 0;
    }
}

int RIO_AudiomuteStatus(int obj) { (void)obj; return 0; }
// Superbikes 3 takes 0..11 as a key of its keypad held.
int RIO_Kpad_SW(int obj) { (void)obj; return nerfGame == NERF_GAME_SB3 ? -1 : 0; }
int RIO_TicketStatus(int obj) { (void)obj; return 0; }
int RIO_WdogStatus(void) { return 0; }
int RIO_TicketAdd(int obj, int tickets) { (void)obj; (void)tickets; return RIO_OK; }
int RIO_TicketSet(int obj, int tickets) { (void)obj; (void)tickets; return RIO_OK; }
int RIO_TicketSetPolarity(int polarity) { (void)polarity; return RIO_OK; }
int RIO_SerialnoSet(unsigned int serial) { (void)serial; return RIO_OK; }
int RIO_DipswitchSet(unsigned int dipsw) { (void)dipsw; return RIO_OK; }
int RIO_EnterBootloader(uint8_t magic) { (void)magic; return RIO_OK; }
int RIO_WdogThrowBone(int seconds) { (void)seconds; return RIO_OK; }
int RIO_WdogDisable(void) { return RIO_OK; }
int RIO_WdogClearWdoggedFlag(void) { return RIO_OK; }
int RIO_CoinCountClear(void) { return RIO_OK; }
int RIO_HicOutSet(int obj, int on) { (void)obj; (void)on; return RIO_OK; }
int RIO_PwmOutSet(int obj, int value) { (void)obj; (void)value; return RIO_OK; }
int RIO_GunSolenoidCfgSet(int obj, uint8_t cfg) { (void)obj; (void)cfg; return RIO_OK; }
int RIO_GunModeSet(int obj, uint8_t type, uint8_t mode, uint8_t pulse, uint8_t period)
{
    (void)obj; (void)type; (void)mode; (void)pulse; (void)period;
    return RIO_OK;
}
int RIO_DualSolenoidWaveform(int obj, uint8_t type, uint8_t n, const uint8_t *cmds)
{
    (void)obj; (void)type; (void)n; (void)cmds;
    return RIO_OK;
}
int RIO_CoinMeterAdd(int obj, int value) { (void)obj; (void)value; return RIO_OK; }
int RIO_AdcConfig(int obj, int hysteresis, int enabled) { (void)obj; (void)hysteresis; (void)enabled; return RIO_OK; }
int RIO_AudiomuteConfig(int obj, int enabled) { (void)obj; (void)enabled; return RIO_OK; }
int RIO_UpdateInterval(int sw, int status) { (void)sw; (void)status; return RIO_OK; }
