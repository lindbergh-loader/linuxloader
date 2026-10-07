// Sound. WMMT3 links NVIDIA's nForce OpenAL in, which drives the cabinet's
// APU through /dev/dsp (absent here, see namcoN2RedirectPath): its OpenAL
// 1.1 entry points, all exported by the game, are sent to the host's OpenAL
// (OpenAL Soft), as the Pacloader fork does (n2Audio.cpp).

#include <dlfcn.h>
#include <stddef.h>

#include "namcoN2.h"
#include "../log/log.h"

static void *openal;
static void *(*realOpenDevice)(const char *);
static void *(*realCreateContext)(void *, const int *);
static int (*realGetError)(void *);

// The default device, whatever the name (nForce's device names mean nothing
// to OpenAL Soft; the game passes NULL anyway).
static void *openDevice(const char *name)
{
    (void)name;
    void *device = realOpenDevice(NULL);
    if (!device)
        log_error("Namco N2: no OpenAL output device");
    return device;
}

// Without the attributes (nForce hints): OpenAL Soft's own defaults.
static void *createContext(void *device, const int *attributes)
{
    (void)attributes;
    void *context = realCreateContext(device, NULL);
    if (!context)
        log_error("Namco N2: alcCreateContext failed (ALC error %d)", realGetError ? realGetError(device) : 0);
    return context;
}

static const char *const forwarded[] = {
    "alEnable", "alDisable", "alIsEnabled", "alGetString", "alGetBooleanv", "alGetIntegerv", "alGetFloatv",
    "alGetDoublev", "alGetBoolean", "alGetInteger", "alGetFloat", "alGetDouble", "alGetError",
    "alIsExtensionPresent", "alGetProcAddress", "alGetEnumValue", "alListenerf", "alListener3f", "alListenerfv",
    "alListeneri", "alGetListenerf", "alGetListener3f", "alGetListenerfv", "alGetListeneri", "alGenSources",
    "alDeleteSources", "alIsSource", "alSourcef", "alSource3f", "alSourcefv", "alSourcei", "alGetSourcef",
    "alGetSource3f", "alGetSourcefv", "alGetSourcei", "alSourcePlayv", "alSourceStopv", "alSourceRewindv",
    "alSourcePausev", "alSourcePlay", "alSourceStop", "alSourceRewind", "alSourcePause", "alSourceQueueBuffers",
    "alSourceUnqueueBuffers", "alGenBuffers", "alDeleteBuffers", "alIsBuffer", "alBufferData", "alGetBufferf",
    "alGetBufferi", "alDopplerFactor", "alDopplerVelocity", "alDistanceModel", "alcCloseDevice",
    "alcMakeContextCurrent", "alcProcessContext", "alcSuspendContext", "alcDestroyContext", "alcGetCurrentContext",
    "alcGetContextsDevice", "alcGetError", "alcIsExtensionPresent", "alcGetProcAddress", "alcGetEnumValue",
    "alcGetString", "alcGetIntegerv",
};

void namcoN2AudioInit(void)
{
    static const char *const libraries[] = {"libopenal.so.1", "libopenal.so.0"};
    for (size_t i = 0; i < sizeof(libraries) / sizeof(libraries[0]) && !openal; i++)
        openal = dlopen(libraries[i], RTLD_NOW | RTLD_LOCAL);
    if (!openal)
    {
        log_error("Namco N2: no OpenAL library (libopenal.so.1: %s), the game stays silent", dlerror());
        return;
    }
    *(void **)&realOpenDevice = dlsym(openal, "alcOpenDevice");
    *(void **)&realCreateContext = dlsym(openal, "alcCreateContext");
    *(void **)&realGetError = dlsym(openal, "alcGetError");
    if (!realOpenDevice || !realCreateContext)
    {
        log_error("Namco N2: the OpenAL library has no alcOpenDevice/alcCreateContext, the game stays silent");
        return;
    }

    int redirected = namcoN2Hook("alcOpenDevice", (void *)openDevice) + namcoN2Hook("alcCreateContext", (void *)createContext);
    for (size_t i = 0; i < sizeof(forwarded) / sizeof(forwarded[0]); i++)
    {
        void *function = dlsym(openal, forwarded[i]);
        if (function)
            redirected += namcoN2Hook(forwarded[i], function);
        else
            log_warn("Namco N2: the OpenAL library has no %s", forwarded[i]);
    }
    log_info("Namco N2: %d OpenAL entry points on the host's OpenAL", redirected);
}
