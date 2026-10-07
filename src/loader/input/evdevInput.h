#ifdef __linux__
#include <pthread.h>

#include <linux/input-event-codes.h>

#define SIZE 300
#define CONTROLLER_THREAD_MAX 256
#define MAX_INPUTS 512

typedef struct
{
    char *arcade[SIZE];
    char *pc[SIZE];
    int count;
} ControllerMapping;

#define MAX_EXIT_COMBOS 4
#define MAX_KEYS_PER_COMBO 4

typedef struct {
    int evCodes[MAX_KEYS_PER_COMBO];
    int numKeys;
} ExitGameCombo;

typedef struct
{
    char name[SIZE];
    int player;
    int channel;
    int enabled;

    char minName[SIZE];
    int minPlayer;
    int minChannel;
    int minEnabled;

    char maxName[SIZE];
    int maxPlayer;
    int maxChannel;
    int maxEnabled;

    int isAnalogue;
    int isNeg; // reversed axis

    char shakeName[SIZE];
    int shakeChannel;
    int shakeEnabled;
    int shakePlayer;
    double shakePreviousScaled;

    int isCoin;
    int isExitGame;

    // Whether this source holds its switch (name, min, max): see sourceSwitch().
    int held, minHeld, maxHeld;
} ArcadeInput;

typedef enum
{
    CONTROLLER_STATUS_SUCCESS = 0,
    CONTROLLER_STATUS_ERROR = 1
} ControllerStatus;

typedef enum {
    NO_SPECIAL_FUNCTION = 0,
    ANALOGUE_TO_DIGITAL_MAX,
    ANALOGUE_TO_DIGITAL_MIN,
    DIGITAL_TO_ANALOGUE,
    ANALOGUE_SHAKE
} SpecialFunction;

typedef struct
{
    char inputName[SIZE];
    char inputTechName[SIZE];
    char inputTechNegName[SIZE];
    int evType;
    int evCode;
    SpecialFunction specialFunction;
} ControllerInput;

typedef struct
{
    char name[SIZE];
    char path[SIZE];
    char physicalLocation[SIZE];
    int absMax[ABS_CNT];
    int absMin[ABS_CNT];

    ControllerInput inputs[MAX_INPUTS];
    int inputCount;

    ArcadeInput keyTriggers[KEY_CNT];
    ArcadeInput absTriggers[ABS_CNT];
    // A mouse's axes aiming a gun: its moves make a position on the screen.
    ArcadeInput relTriggers[REL_CNT];
    double relPosition[REL_CNT];

    int enabled;
    int inUse;
    int hasFFB;
    int ffbEffectId;
    double lastAnalogueValue[8];
    int keyStates[KEY_CNT];
    ExitGameCombo exitCombos[MAX_EXIT_COMBOS];
    int numExitCombos;

} Controller;

typedef struct
{
    Controller *controller;
    int count;

    pthread_t thread[CONTROLLER_THREAD_MAX];
    int threadIndex;
    int threadsRunning;
} Controllers;

ControllerStatus initEvdevControllers(Controllers *controllers);
void setSdlKeyboardSwitch(int player, int channel, int value);
// The controller in use that has one of its axes mapped to the input name
// (e.g. "ANALOGUE_1"), or NULL.
Controller *evdevAxisController(Controllers *controllers, const char *name);
// pthread_create() for the loader's own threads: with every signal blocked.
// They start before the game's main(), unblocked, and a signal the game
// blocks in its threads to take it in sigwait() (WMMT3's SIGALRM, from a
// process-wide setitimer()) would land in one of them, and kill the game.
int createQuietThread(pthread_t *thread, void *(*start)(void *), void *arg);
ControllerStatus stopEvdevControllers(Controllers *controllers);
ControllerStatus loadEvdevControllers(Controllers *controllers);
#endif