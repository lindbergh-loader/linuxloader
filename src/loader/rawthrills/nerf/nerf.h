#ifndef NERF_H
#define NERF_H

// Nerf Arcade (Raw Thrills, 2018): a 64-bit Unity 2018.2.21f1 game for
// Linux whose managed code drives the cabinet through native plugins:
// librio.so (the RIO1 I/O board: switches, the guns' potentiometers, lamps,
// solenoids), libUnityNatives.so (the dongle, tickets, periodic reboot) and
// libhasp_linux_23714.so.  The dumps carry the Windows player instead; the
// game runs under Unity's own Linux player (linuxloader64 starts it, see
// docs/nerf-arcade.md) with linuxloader64.so preloaded and, under each of
// those names, standing in for the plugins: one library, so one state.
// nerf_rt.c, nerfRio.c, nerfInput.c, nerfMono.c and nerfVideo.c are its
// Nerf part.
//
// Superbikes 3 (rawthrills/sb3) drives the same RIO1 and dongle API: the
// settings, the cabinet's files, the dongle, the RIO1 and the input thread
// here serve it too, nerfGame telling which game runs.

#include <stdint.h>

typedef enum
{
    NERF_GAME_NONE, // not a Raw Thrills Unity game: the hooks change nothing
    NERF_GAME_NERF,
    NERF_GAME_SB3, // Superbikes 3 (fnfmega_Data)
} NerfGame;

extern NerfGame nerfGame;

// The RIO1 objects the game uses (Rio1_Object).
enum
{
    RIO_COIN1_SW = 0,
    RIO_COIN2_SW = 1,
    RIO_BILL_SW = 2,
    RIO_TEST_SW = 3,
    RIO_SERVICE_SW = 4,
    RIO_VOL_UP_SW = 5,
    RIO_VOL_DN_SW = 6,
    RIO_START1_SW = 7,
    RIO_START2_SW = 8,
    RIO_VIEW1_SW = 9,
    RIO_TUNES_SW = 12,
    RIO_GEAR1_SW = 13, // P1 shoulder button
    RIO_GEAR3_SW = 15, // P1 trigger
    RIO_GEAR4_SW = 16, // P2 shoulder button
    RIO_DASH_SW = 17,  // P2 trigger
    RIO_NUM_SW = 19,
    RIO_ADC1 = 19, // P1 X, Y, then P2 X, Y
    RIO_ADC4 = 22,
    RIO_SERIAL = 24,
    RIO_HWVERSION = 27,
    RIO_GUN_SOL_1 = 102,
    RIO_GUN_DUAL_SOL_1 = 104,
};

// The guns' potentiometers read 12 bits.
#define NERF_ADC_MAX 4095

// What the players hold, sampled once a frame (nerfInput.c).
typedef struct
{
    int switches[RIO_NUM_SW]; // held, by RIO switch object
    // [EVDEV] ANALOGUE_1..4, 0..1 (an axis from its minimum); -1: none.
    // Nerf: P1's aim X, Y (from the left, from the top), then P2's.
    // Superbikes 3: the handlebar (from the left), the throttle, the brake.
    float analog[4];
    // Superbikes 3 on the desktop: the Up and Down arrows held, the
    // throttle and the brake in a race, up and down in the test menu.
    int arrowUp, arrowDown;
} NerfInput;

void nerfInputInit(void);
void nerfInputSample(NerfInput *in);
// The device of [EVDEV] ANALOGUE_<a + 1> with evdev input, else NULL.
const char *nerfAnalogDevice(int a);

// The settings (nerf_rt.c): linuxloader.ini, named by LINUXLOADER_CONFIG.
const char *nerfIniValue(const char *section, const char *key);
int nerfIniInt(const char *section, const char *key, int def);

// The calibration the RIO's scale needs, forced into the game's
// preferences (nerfMono.c, sb3Mono.c): called from the main thread.
void nerfForceCalibration(void);
void sb3ForceCalibration(void);
// Superbikes 3 shows its test menu (sb3Mono.c): from the main thread.
int sb3InTestMenu(void);
// Superbikes 3's handlebar range on the RIO1 (sb3Mono.c): from the main
// thread, again now and then (a calibration changes it).
void sb3SteerRange(void);
// Superbikes 3's handlebar motor on the steering wheel (sb3Plugins.c): from
// the main thread, once a frame.
void sb3FfbUpdate(void);

// Mono's embedding API, as the player loaded it (nerfMono.c), for the
// games' parts: the game's assembly (Assembly-CSharp), a method of one of
// its classes by description ("Class:Method(int,int)"), a call.
typedef void MonoImage, MonoClass, MonoMethod, MonoObject;
MonoImage *nerfMonoGame(void);
MonoMethod *nerfMonoMethod(MonoImage *image, const char *className, const char *desc);
MonoObject *nerfMonoInvoke(MonoMethod *method, void *self, void **args, MonoObject **exc);
int nerfMonoUnboxInt(MonoObject *object);
// The light gun border, around the player's frames (nerfVideo.c): called
// once the settings are read.
void nerfVideoInit(void);
// Mono, as the player loaded it (nerf_rt.c).
extern void *nerfMonoHandle;

#define nerfLog(...) fprintf(stderr, "nerf: " __VA_ARGS__)

#endif // NERF_H
