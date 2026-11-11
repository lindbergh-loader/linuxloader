#include "config/config.h"

#include "hardware/lindbergh/cardReader.h"
#include "hardware/lindbergh/baseBoard.h"
#include "hardware/lindbergh/driveBoard.h"
#include "hardware/lindbergh/eeprom.h"
#include "hardware/lindbergh/forceFeedback.h"
#include "graphics/fpsLimiter.h"
#include "graphics/gpuVendor.h"
#include "hardware/lindbergh/jvs.h"
#include "patching/patch.h"
#include "patching/patchResolution.h"
#include "hardware/lindbergh/rideBoard.h"
#include "graphics/sdlCalls.h"
#include "input/sdlInput.h"
#include "hardware/lindbergh/securityBoard.h"
#include "config/config.h"
#include "../minhook/include/MinHook.h"
#include "log/log.h"
#include "version.h"

#if defined(__linux__)
#include "input/evdevInput.h"
#endif

uint32_t gId = 0;
int gGrp = -1;

int gWidth;
int gHeight;

extern FpsLimit fpsLimit;
extern SDLControllers sdlJoysticks;
extern char *configFolder;

#ifdef __linux__
Controllers controllers = {0};
#endif

void initMain(char *configPath, char *controlsPath)
{
#ifdef __linux__
    configPath = getenv("LINUX_LOADER_CONFIG_PATH");
    if (configPath == NULL || configPath[0] == '\0')
        configPath = "";
    controlsPath = getenv("LINUX_LOADER_CONTROLS_PATH");
    if (controlsPath == NULL || controlsPath[0] == '\0')
        controlsPath = "";

#endif
    initConfig(configPath);


    gId = getConfig()->crc32;
    gGrp = getConfig()->gameGroup;
    gWidth = getConfig()->width;
    gHeight = getConfig()->height;

    initFpsLimiter();

    getConfig()->GPUVendor = getGPUVendorID();

    if (getConfig()->GPUVendor == ERROR_GPU)
    {
        log_error("Failed to detect GPU\nUsing default values\n");
        getConfig()->GPUVendor = UNKNOWN_GPU;
    }

    if (MH_Initialize() != MH_OK)
    {
        log_error("Failed to initialize MinHook");
        exit(1);
    }

    if (initEeprom() != 0)
        exit(1);
    log_info("EEPROM initialized");

    if (initPatch() != 0)
        exit(1);
    log_info("Patches initialized");

    if (initResolutionPatches() != 0)
        exit(1);
    log_info("Resolution patches initialized");

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        log_error("Failed to enable hooks");
        exit(1);
    }

    if (initSDL() != 0)
        exit(1);
    log_info("SDL initialized");

    if (initBaseboard() != 0)
        exit(1);
    log_info("Baseboard initialized");

    if (initJVS() != 0)
        exit(1);
    log_info("JVS initialized");

    if (initSecurityBoard() != 0)
        exit(1);
    log_info("Security board initialized");

    if (getConfig()->emulateDriveboard)
    {
        if (initDriveboard() != 0)
            exit(1);
    }
    log_info("Driveboard initialized");

    if (getConfig()->emulateRideboard)
    {
        if (initRideboard() != 0)
            exit(1);
    }
    log_info("Rideboard initialized");

    if (getConfig()->emulateHW210CardReader)
    {
        if (initCardReader() != 0)
            exit(1);
    }
    log_info("Card reader initialized");

    if (initSdlInput(controlsPath) != 0)
        exit(1);
    log_info("SDL input initialized");

#if defined(__linux__)
    if (initEvdevControllers(&controllers) != 0)
        exit(1);
    log_info("Evdev controllers initialized");
#endif

    // EVDEV mode: initSdlInput() skips sdlFfbInit(), so init FFB here
    if (getConfig()->inputMode == 2)
        sdlFfbInit();

    securityBoardSetDipResolution(getConfig()->width, getConfig()->height);
    log_info("Security board dip resolution set");

    printf("\nLinux Loader Development Version %d.%d.%d\nBy the Linux Loader Development Team 2026\n\n", MAJOR_VERSION, MINOR_VERSION, UPDATE_VERSION);
    printf("  GAME:        %s\n", getGameName());
    printf("  GAME ID:     %s\n", getGameId());
    printf("  DVP:         %s\n", getDvpName());
    printf("  GPU VENDOR:  %s\n", getConfig()->GPUVendorString);

#if defined(__linux__)
    for (int i = 0; i < controllers.count; i++)
    {
        if (controllers.controller[i].inUse)
        {
            printf("  CONTROLLER: %s\n", controllers.controller[i].name);
        }
    }
    printf("\n");
#endif

    for (int i = 0; i < MAX_JOYSTICKS; i++)
    {
        SDL_Joystick *joy = NULL;
        if (sdlJoysticks.controllers[i])
            joy = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joy = sdlJoysticks.joysticks[i];

        if (joy != NULL)
            printf("  SDL CONTROLLER %d: %s\n", i, SDL_GetJoystickName(joy));
    }
    printf("\n");
}