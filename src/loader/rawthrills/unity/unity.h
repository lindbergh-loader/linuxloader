#ifndef UNITY_H
#define UNITY_H

// The Raw Thrills Unity games: 64-bit Unity players for Linux whose managed
// code drives the cabinet through native plugins: librio.so (the RIO1 I/O
// board: switches, ADCs, lamps, solenoids), libRIO2.so (the RIO2),
// libUnityNatives.so (the dongle, tickets, periodic reboot) and the HASP
// library. linuxloader64 starts the player (see docs/nerf-arcade.md) with
// linuxloader64.so preloaded and, under each of those names, standing in
// for the plugins: one library, so one state.
//
// This directory is the base all of them share: the settings
// (unityRt.c), the cabinet's files kept off the host, the dongle, Mono's
// embedding API (unityMono.c), the players' inputs (unityInput.c), the
// RIO1 (unityRio.c), the RIO2 (unityRio2.c) and the swap (unityVideo.c).
// Each game is a module of its own (rawthrills/nerf, sb3, tmnt, mcoc)
// describing itself with a UnityGame, listed in unityGames.c.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// The RIO1 objects the games use (Rio1_Object).
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

// The switches a game's inputs fill: RIO1 switches, or the game's own
// GameInput values for the RIO2 games.
#define UNITY_NUM_INPUTS 50

// The RIO1's ADCs read 12 bits.
#define UNITY_ADC_MAX 4095

// How often the input thread samples, in ms.
#define UNITY_POLL_MS 8

// What the players hold, sampled once a frame (unityInput.c).
typedef struct
{
    int switches[UNITY_NUM_INPUTS]; // held, by RIO switch or GameInput
    // [EVDEV] ANALOGUE_1..4, 0..1 (an axis from its minimum); -1: none.
    float analog[4];
    // Superbikes 3 on the desktop: the Up and Down arrows held.
    int arrowUp, arrowDown;
} UnityInput;

// [EVDEV] key names to switches, desktop keysyms to switches: a game's maps,
// ended by {0, NULL} / {0, 0}.
typedef struct
{
    int sw;
    const char *key;
} UnitySwitchKey;

typedef struct
{
    int sw;
    unsigned long keysym;
} UnitySwitchKeysym;

// A game: what it is, then what it changes in the base. Any hook may be
// NULL, any flag 0.
typedef struct
{
    const char *name; // for the log
    // The game's data directory, "/<name>_Data" beside the executable; NULL
    // for the game taken when none matches (Nerf Arcade).
    const char *dataDir;

    // Startup, once the settings are read and LocalData/system made.
    void (*init)(void);

    // The cabinet's paths: the host's one for that path, or path unchanged.
    const char *(*mapPath)(const char *path);
    // A command the game runs (execve), its name without directory: the
    // hook may end the process instead of running it.
    void (*command)(const char *name, char *const argv[]);
    // No network but the local host's.
    int offline;

    // The dongle's cabinet type (libUnityNatives' HaspReadCabType).
    uint8_t dongleCabType;

    // The inputs: [EVDEV] keys and desktop keys; the desktop mouse in the
    // game's window is P1's gun; changes once a sample is read from evdev
    // and from the desktop keys (last: the previous sample).
    const UnitySwitchKey *switches;
    const UnitySwitchKeysym *keys;
    int mouseGun;
    void (*evdevState)(UnityInput *in);
    void (*desktopState)(UnityInput *in, const UnityInput *last, const char keys[32]);

    // The swap is hooked (unityVideo.c): the light gun border when enabled,
    // and the game's overlay drawn over its frame.
    int video;
    void (*overlay)(void *display, unsigned long drawable, int width, int height);

    // The RIO1: at RIO_Init (the calibration), once a sample, an ADC's
    // value (0..1, -1 for none) on its 0..1 scale, the keypad's answer.
    void (*rioInit)(void);
    void (*rioSample)(UnityInput *in);
    float (*rioAdc)(int adc, float value);
    int rioKeypad;

    // The RIO2: a virtual board, connected (else none, the game takes its
    // RIO1); its update (once a frame from the game's main thread), a pin's
    // value and a switch matrix's (1 if the game answers).
    int rio2Virtual;
    void (*rio2Update)(void);
    int (*rio2Pin)(int pin, uint32_t *value);
    int (*rio2Matrix)(int bank, int sw, uint32_t *value);
} UnityGame;

// The game running; outside one (another 64-bit game), an empty UnityGame
// named "none", and the hooks change nothing.
extern const UnityGame *unityGame;
// The games (unityGames.c), ended by NULL.
extern const UnityGame *const unityGames[];

// The settings (unityRt.c): linuxloader.ini, named by LINUXLOADER_CONFIG.
const char *unityIniValue(const char *section, const char *key);
int unityIniInt(const char *section, const char *key, int def);
// The local file standing for one of the cabinet's system files, else NULL.
const char *unityCabinetFile(const char *path);

// The players' inputs (unityInput.c): the thread started (once), a
// sample, the device of [EVDEV] ANALOGUE_<a + 1> with evdev input (else
// NULL), a desktop key held in a sample's keymap.
void unityInputInit(void);
void unityInputSample(UnityInput *in);
const char *unityAnalogDevice(int a);
int unityKeysymDown(const char keys[32], unsigned long keysym);

// Mono's embedding API, as the player loaded it (unityMono.c), for the
// games' modules: the game's assembly (Assembly-CSharp), a method of one of
// its classes by description ("Class:Method(int,int)"), a call.
typedef void MonoImage, MonoClass, MonoMethod, MonoObject;
MonoImage *unityMonoGame(void);
MonoMethod *unityMonoMethod(MonoImage *image, const char *className, const char *desc);
// Any assembly by name ("UnityEngine.CoreModule"), a method of a class in a
// namespace ("UnityEngine", "Screen", "Screen:SetResolution(int,int,bool)").
MonoImage *unityMonoImage(const char *name);
MonoMethod *unityMonoMethodIn(MonoImage *image, const char *nameSpace, const char *className, const char *desc);
// The game's operator settings (TestPreferences): a value (-1: none), a new
// one, saved.
int unityMonoPref(int pref);
void unityMonoSetPref(int pref, int value);
// A text setting: read (1 if found, "" if none), written and saved.
int unityMonoPrefString(int pref, char *out, size_t size);
void unityMonoSetPrefString(int pref, const char *value);
// A bool field of the object a class's static field holds (GameFlow.Config's
// NoWebCam): 1 if set.
int unityMonoSetStaticBool(MonoImage *image, const char *nameSpace, const char *className, const char *staticField,
                           const char *boolField, int value);
MonoObject *unityMonoInvoke(MonoMethod *method, void *self, void **args, MonoObject **exc);
int unityMonoUnboxInt(MonoObject *object);
// Objects: a class's static property (Instance), the Instance of a
// singleton (SingletonMonoBehaviour<Class>'s, its parent's), its static
// field; an object's field read or written (value sized as the field: a
// reference, an int, a bool), an item of an array of references, an int
// returned by a method with no argument ("GameFlow:GetGameStateCurEnum()",
// an enum alike). 0 / NULL if missing.
MonoObject *unityMonoStaticProperty(MonoImage *image, const char *nameSpace, const char *className, const char *name);
MonoObject *unityMonoSingleton(MonoImage *image, const char *nameSpace, const char *className);
MonoObject *unityMonoStaticField(MonoImage *image, const char *nameSpace, const char *className, const char *name);
int unityMonoGetField(MonoObject *object, const char *name, void *value);
int unityMonoSetField(MonoObject *object, const char *name, void *value);
MonoObject *unityMonoArrayItem(MonoObject *array, int index);
int unityMonoCallInt(MonoObject *object, const char *desc, int *value);
MonoObject *unityMonoCall(MonoObject *object, const char *desc, void **args, int *ok);
MonoObject *unityMonoString(const char *text);

// The swap hooked for the light gun border and the game's overlay
// (unityVideo.c): called once the settings are read.
void unityVideoInit(void);
// Mono, as the player loaded it (unityRt.c).
extern void *unityMonoHandle;

#define unityLog(...) fprintf(stderr, "unity: " __VA_ARGS__)

#endif // UNITY_H
