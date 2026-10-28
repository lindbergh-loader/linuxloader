#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <stdio.h>
#include <string.h>

#include "forceFeedback.h"
#include "../../input/sdlInput.h"
#include "../../log/log.h"


static const int FFB_POWER_ON = 0b01;
static const int FFB_PLAY_CONTINUOUS = 0b10;

static const int RUN_ONCE_DURATION = 100; // ms

static int ffb_power_mode = 0;

extern float gFFBGlobalGain;
extern float gFFBAutocenterGain;
extern float gFFBRumbleGain;

static int effect_id = -1;
static SDL_HapticEffect current_effect;
extern SDLControllers sdlJoysticks;


static SDL_HapticEffect constant_effect;
static int constant_effect_id = -1;

static SDL_HapticEffect friction_effect;
static int friction_effect_id = -1;


static SDL_Thread *playback_thread = NULL;

static const int PLAYBACK_SPEED = 16; // ms

typedef struct {
    int direction;
    float strength;
} SUD_Subpackage;

typedef struct {
    SUD_Subpackage pkg[16];
} SUD_Package;

static SUD_Package SUD_packages[16] = {0};

static int uploading_package_id = -1;
static int uploading_subpackage_id = -1;

static SDL_HapticEffect playback_effect = {0};
static int playback_effect_id = -1;

// host environment
static int threading_attempted = 0;

// shared environment
static SDL_Mutex *playback_mutex;
static SDL_Condition  *playback_wakeup;
static int playback_request = -1;
static bool playback_active = false;

// thread environment
static int playback_playing = -1;


void sdlFfbSetStrength(float strength)
{
    if (!sdlJoysticks.haptics[0])
        return;
    if(!SDL_SetHapticGain(sdlJoysticks.haptics[0], (int)(strength*100.0f)))
        log_error("\tGain Haptic Effect did not work: %s\n", SDL_GetError());
}


void sdlFfbInit(void)
{
    // initialize constant effect
    memset(&constant_effect, 0, sizeof(constant_effect));
    constant_effect.type = SDL_HAPTIC_CONSTANT;
    constant_effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    constant_effect.constant.direction.dir[0] = 1;
    constant_effect.constant.attack_length = 0;
    constant_effect.constant.fade_length = 0;

    memset(&playback_effect, 0, sizeof(playback_effect));
    playback_effect.type = SDL_HAPTIC_CONSTANT;
    playback_effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    playback_effect.constant.direction.dir[0] = 1;
    playback_effect.constant.attack_length = 0;
    playback_effect.constant.fade_length = 0;
    playback_effect.constant.length = SDL_HAPTIC_INFINITY;

    memset(&friction_effect, 0, sizeof(friction_effect));
    friction_effect.type = SDL_HAPTIC_DAMPER;
    friction_effect.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
    friction_effect.condition.direction.dir[0] = 1;


    // SDL input mode: open haptic from already-opened joysticks
    for (int i = 0; i < sdlJoysticks.joysticksCount && i < MAX_JOYSTICKS; ++i)
    {
        SDL_Joystick *joy = NULL;

        if (sdlJoysticks.controllers[i])
            joy = SDL_GetGamepadJoystick(sdlJoysticks.controllers[i]);
        else if (sdlJoysticks.joysticks[i])
            joy = sdlJoysticks.joysticks[i];

        if (!joy)
            continue;

        if (!SDL_IsJoystickHaptic(joy))
            continue;

        sdlJoysticks.haptics[i] = SDL_OpenHapticFromJoystick(joy);
        if (sdlJoysticks.haptics[i])
        {
            printf("Joystick %d (%s) supports %d FFB effects.\n", i, SDL_GetJoystickName(joy), SDL_GetMaxHapticEffectsPlaying(sdlJoysticks.haptics[i]));
            if (SDL_GetHapticFeatures(sdlJoysticks.haptics[i]) & SDL_HAPTIC_LEFTRIGHT)
            {
                log_warn("FFB: Haptic opened from joystick %d (%s)", i, SDL_GetJoystickName(joy));
                sdlFfbRumble(0.1, 0.1, 200);
                sdlFfbSetStrength(gFFBGlobalGain);
            }
            else
            {
                log_warn("FFB: Joystick %d has no LEFTRIGHT support, skipping", i);
                SDL_CloseHaptic(sdlJoysticks.haptics[i]);
                sdlJoysticks.haptics[i] = NULL;
            }
        }
    }

    // EVDEV input mode: fall back to standalone haptic devices
    if (!sdlJoysticks.haptics[0])
    {
        int num_haptics;
        SDL_HapticID *haptics = SDL_GetHaptics(&num_haptics);
        if (haptics)
        {
            for (int i = 0; i < num_haptics && i < MAX_JOYSTICKS; ++i)
            {
                if (sdlJoysticks.haptics[i])
                    continue;

                sdlJoysticks.haptics[i] = SDL_OpenHaptic(haptics[i]);
                if (sdlJoysticks.haptics[i])
                {
                    if (SDL_GetHapticFeatures(sdlJoysticks.haptics[i]) & SDL_HAPTIC_LEFTRIGHT)
                    {
                        log_warn("FFB: Standalone haptic %d: %s", i, SDL_GetHapticNameForID(haptics[i]));
                        sdlFfbRumble(1.0, 1.0, 200);
                    }
                    else
                    {
                        log_warn("FFB: Standalone haptic %d has no LEFTRIGHT support, skipping", i);
                        SDL_CloseHaptic(sdlJoysticks.haptics[i]);
                        sdlJoysticks.haptics[i] = NULL;
                    }
                }
            }
            SDL_free(haptics);
        }
    }

    if (!sdlJoysticks.haptics[0])
        log_warn("FFB: No haptic device found");
}

void sdlFfbRumble(float left, float right, int duration_ms)
{
    if (!sdlJoysticks.haptics[0])
        return;

    SDL_HapticEffect effect;
    memset(&effect, 0, sizeof(effect));
    effect.type = SDL_HAPTIC_LEFTRIGHT;
    effect.leftright.length = duration_ms;
    effect.leftright.large_magnitude = (Uint16)(left * 0xFFFF);
    effect.leftright.small_magnitude = (Uint16)(right * 0xFFFF);
    if (effect_id >= 0)
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], effect_id);
    effect_id = SDL_CreateHapticEffect(sdlJoysticks.haptics[0], &effect);
    SDL_RunHapticEffect(sdlJoysticks.haptics[0], effect_id, 1);
}


void sdlFfbConstant(int direction, float strength, uint32_t duration_ms)
{
    if (!sdlJoysticks.haptics[0])
        return;
    
    int should_recreate = 1;
    if ((ffb_power_mode & FFB_PLAY_CONTINUOUS) && constant_effect_id >= 0)
        should_recreate = 0;

    if (should_recreate && constant_effect_id >= 0)
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], constant_effect_id);

    constant_effect.constant.length = duration_ms;
    constant_effect.constant.level = (strength * 0x7FFF)*direction;
    
    if (should_recreate) {
        constant_effect_id = SDL_CreateHapticEffect(sdlJoysticks.haptics[0], &constant_effect);
        if(!SDL_RunHapticEffect(sdlJoysticks.haptics[0], constant_effect_id, 1))
            log_error("\tConstant Haptic Effect did not run: %s\n", SDL_GetError());
    }
    else {
        SDL_UpdateHapticEffect(sdlJoysticks.haptics[0], constant_effect_id, &constant_effect);
    }
}


int sdlFfbPlaybackThread(void* data)
{
    uint64_t time_since_last_playback = SDL_GetTicks();

    while(true)
    {
        if (playback_effect_id >= 0) {
            playback_effect.constant.level = 0;
            SDL_UpdateHapticEffect(sdlJoysticks.haptics[0], playback_effect_id, &playback_effect);
        }
        SDL_LockMutex(playback_mutex);
        //printf("Waiting for a new playback...\n");

        while (playback_request < 0 && playback_active)
            SDL_WaitCondition(playback_wakeup, playback_mutex);
        
        if (!playback_active) {
            SDL_UnlockMutex(playback_mutex);
            return 0;
        }

        playback_playing = playback_request;
        int effect = playback_playing;
        if (effect & 0b10000) {
            effect = playback_playing ^ 0b10000;
        }
        int effect_direction = playback_playing & 0b10000 ? -1 : 1;
        
        //printf("Playing new effect: 0x%02x %d\n", effect, effect_direction);
        playback_request = -1;

        SDL_UnlockMutex(playback_mutex);

        for (int i = 0; i < 16; i++) {
            SDL_LockMutex(playback_mutex);
            if (playback_request >= 0) {
                SDL_UnlockMutex(playback_mutex);
                //printf("Playback interrupted\n");
                playback_playing = -1;
                break;
            }

            int should_recreate = 1;
            if ((ffb_power_mode & FFB_PLAY_CONTINUOUS) && playback_effect_id >= 0) {
                should_recreate = 0;
            }


            float strength = SUD_packages[effect].pkg[i].strength;
            int direction = SUD_packages[effect].pkg[i].direction * effect_direction;
            //printf("\tPlaying package: 0x%02x 0x%02x %d %d, package requested: %d, interval: %d, str: %g\n", playback_playing, effect, i, should_recreate, playback_request, SDL_GetTicks() - time_since_last_playback, strength);
            time_since_last_playback = SDL_GetTicks();

            playback_effect.constant.level = (strength * 0x7FFF)*direction;
            
            if (should_recreate) {
                playback_effect_id = SDL_CreateHapticEffect(sdlJoysticks.haptics[0], &playback_effect);
            
                if (playback_effect_id < 0)
                    log_error("\tPlayback Haptic Effect 0x%02x 0x%02x (lvl: 0x%04x) could not be created: %s\n", effect, i, playback_effect.constant.level, SDL_GetError());
                if(!SDL_RunHapticEffect(sdlJoysticks.haptics[0], playback_effect_id, 1))
                    log_error("\tPlayback Haptic Effect 0x%02x 0x%02x did not run: %s\n", effect, i, SDL_GetError());
            }
            else {
                SDL_UpdateHapticEffect(sdlJoysticks.haptics[0], playback_effect_id, &playback_effect);
            }
            
            SDL_WaitConditionTimeout(playback_wakeup, playback_mutex, PLAYBACK_SPEED);

            SDL_UnlockMutex(playback_mutex);
        }
        
        SDL_LockMutex(playback_mutex);
        if (playback_request == playback_playing) {
            playback_request = -1;
        }
        playback_playing = -1;
        SDL_UnlockMutex(playback_mutex);
    }
}

void sdlFfbPlayback(uint8_t effect)
{
    if (!sdlJoysticks.haptics[0])
        return;
    
    if (!threading_attempted) { // if this failed once, we should not try to make another thread. 
        threading_attempted = 1;
        playback_active = true;
        playback_mutex = SDL_CreateMutex();
        playback_wakeup = SDL_CreateCondition();
        playback_thread = SDL_CreateThread(sdlFfbPlaybackThread, "FFBPlayback", NULL);
        if (playback_thread == NULL) {
            log_error("playback_thread could not be started: %s\n", SDL_GetError());
            playback_active = false;
        }
        printf("Started FFB Playback thread.\n");
    }
    
    if (!playback_active)
        return;
    
    SDL_LockMutex(playback_mutex);
    //printf("requesting FFB Playback effect 0x%02x %d\n", effect, threading_attempted);
    playback_request = effect;
    SDL_SignalCondition(playback_wakeup);
    SDL_UnlockMutex(playback_mutex);
}

void sdlFfbFriction(float power, float coverage, uint32_t duration_ms)
{
    if (!sdlJoysticks.haptics[0])
        return;
    
    int should_recreate = 1;
    if ((ffb_power_mode & FFB_PLAY_CONTINUOUS) && friction_effect_id >= 0) {
        should_recreate = 0;
    }

    if (should_recreate && friction_effect_id >= 0)
    {
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], friction_effect_id);
    }

    friction_effect.condition.length = duration_ms;
    friction_effect.condition.right_sat[0] = ((1-power) * 0xFFFF);
    friction_effect.condition.left_sat[0] = ((1-power) * 0xFFFF);
    friction_effect.condition.right_coeff[0] = 32767;
    friction_effect.condition.left_coeff[0] = 32767;
    friction_effect.condition.deadband[0] = ((1 - coverage) * 0xFFFF);
    
    if (should_recreate) {
        friction_effect_id = SDL_CreateHapticEffect(sdlJoysticks.haptics[0], &friction_effect);
        if(friction_effect_id == -1)
            log_error("\tFriction Haptic Effect did not create: %s\n", SDL_GetError());
        if(!SDL_RunHapticEffect(sdlJoysticks.haptics[0], friction_effect_id, 1))
            log_error("\tFriction Haptic Effect did not run: %s\n", SDL_GetError());
    }
    else {
        SDL_UpdateHapticEffect(sdlJoysticks.haptics[0], friction_effect_id, &friction_effect);
    }
}

void sdlFfbCentering(float power)
{
    if (!sdlJoysticks.haptics[0])
        return;
    
    log_info("Autocenter power: %f\n", power);
    
    if(!SDL_SetHapticAutocenter(sdlJoysticks.haptics[0], (int)(power*100.0f)))
        log_error("\tAutocenter Haptic Effect did not work: %s\n", SDL_GetError());
}

void sdlFfbStopEffect(void)
{
    if (!sdlJoysticks.haptics[0])
        return;
    if (effect_id >= 0) {
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], effect_id);
        effect_id = -1;
    }
    if (constant_effect_id >= 0) {
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], constant_effect_id);
        constant_effect_id = -1;
    }
    if (friction_effect_id >= 0) {
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], friction_effect_id);
        friction_effect_id = -1;
    }
    if (playback_effect_id >= 0) {
        SDL_DestroyHapticEffect(sdlJoysticks.haptics[0], playback_effect_id);
        playback_effect_id = -1;
    }
    SDL_SetHapticAutocenter(sdlJoysticks.haptics[0], 0);
}

void sdlFfbShutdown(void)
{
    if (sdlJoysticks.haptics[0])
    {
        SDL_CloseHaptic(sdlJoysticks.haptics[0]);
        sdlJoysticks.haptics[0] = NULL;
        if (playback_active) {
            SDL_LockMutex(playback_mutex);
            playback_active = false;
            SDL_SignalCondition(playback_wakeup);
            SDL_UnlockMutex(playback_mutex);
        }
    }
}

void sdlFfbDriveboard(const unsigned char *buffer, size_t count)
{
    /*if (buffer[0] != 0x80 && buffer[0] != 0x84 && buffer[0] != 0x85 && buffer[0] != 0xfd && buffer[0] != 0x86 && buffer[0] != 0x8b){
        printf("FFB driveboard: count=%zu, data:", count);
        for (size_t i = 0; i < count; ++i) {
            printf(" 0x%02x", buffer[i]);
        }
        printf("\n");
    }*/

    switch (buffer[0])
    {
        case 0x80:
        { // power
            if (buffer[2] == 0x01)
            {
                if (buffer[1] == 0x00)
                    ffb_power_mode = FFB_POWER_ON | FFB_PLAY_CONTINUOUS;
                else
                    ffb_power_mode = FFB_POWER_ON;
                log_info("0x80 command: Setting the power level of FFB: %d\n", ffb_power_mode);
            }
            else
            {
                ffb_power_mode = 0;
                sdlFfbStopEffect();
                log_info("0x80 command: Setting the power level of FFB: %d\n", ffb_power_mode);
            }
            break;
        }
        case 0x83:
        { // Strength of motors
            float strength = ((float)buffer[1]) / 127.0f;
            sdlFfbSetStrength(strength*gFFBGlobalGain);
            break;
        }
        case 0x85:
        { // Rumble/vibrate
            // uint8_t speed = buffer[1];
            uint8_t power = buffer[2];

            float force = (float)power / 63.0f; // Map power 1-63 to 0.0-1.0

            int duration = 100;
            log_info("0x85 command: Triggering rumble: force=%.2f, duration=%dms\n", force, duration);
            sdlFfbRumble(force*gFFBRumbleGain, force*gFFBRumbleGain, duration);
            break;
        }
        case 0x84:
        { // Movement command
            uint8_t direction = buffer[1];
            uint8_t value = buffer[2];

            float strength = 0.0f;
            uint32_t duration = ffb_power_mode & FFB_PLAY_CONTINUOUS ? SDL_HAPTIC_INFINITY : RUN_ONCE_DURATION;

            if (direction == 0x00)
            { // right - value from 0x7F (min) to 0x01 (max)
                strength = (127.0f - (float)value) / 127.0f;
            }
            else if (direction == 0x01)
            { // left  - value goes from 0x00 (min) to 0x7F (max)

                strength = (float)value / 127.0f;
            }

            log_info("0x84 command: Triggering movement: direction=%d, strength=%.2f, duration=%dms\n", direction, strength, duration);
            sdlFfbConstant(((int)direction)*2 - 1, strength, duration);
            break;
        }
        case 0x86: // Friction
        {
            log_info("Friction:");
            float power = ((float)buffer[1])/127.0f;
            float coverage = ((float)buffer[2])/127.0f;
            uint32_t duration = ffb_power_mode & FFB_PLAY_CONTINUOUS ? SDL_HAPTIC_INFINITY : RUN_ONCE_DURATION;

            log_info("0x86 command: Damper: strength=%.2f, coverage=%.2f\n", power, coverage);

            sdlFfbFriction(power, coverage, duration);
            break;
        }
        case 0x8:
            break;
        case 0x87: // move and set target point
            printf("Move and set target point\n");
            break;
        case 0x88: // move to current target point
            printf("Move to current target point\n");
            break;
        case 0x8B: // centering strength
        {
            float power = ((float)buffer[1])/127.0f;
            log_info("0x8b command: centering: strength=%.2f\n", power);
            sdlFfbCentering(power*gFFBAutocenterGain);
            break;
        }
        case 0xFB:
        { // Playback sud package
            uint8_t effect = buffer[2];
            //printf("Outrun FFB 0xFB effect: 0x%02x arg 0x%02x\n", effect, buffer[1]);

            sdlFfbPlayback(effect);

            break;
        }
        case 0x9D:
        { // Upload part 1
            if (buffer[2] > 0xf || buffer[1] > 0xf) {
                log_error("Package upload 0x%02x 0x%02x 0x%02x 0x%02x got invalid IDs\n", buffer[0], buffer[1], buffer[2], buffer[3]);
            }
            else {
                uploading_package_id = buffer[2];
                uploading_subpackage_id = buffer[1];
                //printf("Uploading package 0x%02x 0x%02x\n", buffer[2], buffer[1]);
            }
            break;
        }
        case 0x9E:
        { // Upload part 2
            if (uploading_subpackage_id < 0 || uploading_package_id < 0) {
                log_error("FFB SUD Package data 0x%02x 0x%02x 0x%02x 0x%02x cannot be uploaded yet\n", buffer[0], buffer[1], buffer[2], buffer[3]);
            }
            else {
                uint8_t direction = buffer[1];
                uint8_t value = buffer[2];

                float strength = 0.0f;
                uint32_t duration = ffb_power_mode & FFB_PLAY_CONTINUOUS ? SDL_HAPTIC_INFINITY : RUN_ONCE_DURATION;

                if (direction == 0x00)
                { // right - value from 0x7F (min) to 0x01 (max)
                    strength = (127.0f - (float)value) / 127.0f;
                }
                else if (direction == 0x01)
                { // left  - value goes from 0x00 (min) to 0x7F (max)

                    strength = (float)value / 127.0f;
                }
                SUD_packages[uploading_package_id].pkg[uploading_subpackage_id].direction = ((int)direction)*2 - 1;
                SUD_packages[uploading_package_id].pkg[uploading_subpackage_id].strength = strength;
                //printf("Package 0x%02x 0x%02x uploaded with direction %d and strength %f\n", uploading_package_id, uploading_subpackage_id, direction, strength);
                uploading_subpackage_id = -1;
                uploading_package_id = -1;
            }
            break;
        }
        default:
            break;
    }
}

void ffb_output(const unsigned char *buffer, size_t count)
{
    /*     printf("FFB output: count=%zu, data:", count);
        for (size_t i = 0; i < count; ++i)
            printf(" 0x%02x", buffer[i]);
        printf("\n"); */

    if (count > 0)
    {
        if (buffer[0] & 0x40)
        {
            // printf("GPO: ABC Vibration triggered (bit 6 set)\n");
            // sdlFfbRumble(1.0f, 1.0f, 120);
        }
    }
}