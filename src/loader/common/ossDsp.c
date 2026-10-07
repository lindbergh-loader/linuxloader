// OSS /dev/dsp emulation on top of SDL3 audio, for the PC-based arcade
// systems: the Raw Thrills g5 engine's JPS sound engine, the OSS backend of
// the OpenAL the Namco ES1 games ship.
// The games only use the basic OSS API: format/channels/rate setup, fragment
// setup, GETOSPACE, mixer volume ioctls and blocking writes.

#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <sys/soundcard.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "ossDsp.h"
#include "../rawthrills/rawthrills.h"
#include "../namco/namcoEs1.h"
#include "../log/log.h"

#define DSP_FRAGMENTS 4
#define DSP_FRAGMENT_SIZE 4096

// The game links SDL 1.2, whose SDL_Init/SDL_WasInit/SDL_Delay/... shadow
// SDL3's in the global scope: call SDL3 through its own library handle.
static struct
{
    SDL_InitFlags (*WasInit)(SDL_InitFlags);
    bool (*InitSubSystem)(SDL_InitFlags);
    const char *(*GetError)(void);
    SDL_AudioStream *(*OpenAudioDeviceStream)(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *);
    bool (*ResumeAudioStreamDevice)(SDL_AudioStream *);
    void (*DestroyAudioStream)(SDL_AudioStream *);
    int (*GetAudioStreamQueued)(SDL_AudioStream *);
    bool (*PutAudioStreamData)(SDL_AudioStream *, const void *, int);
    bool (*ClearAudioStream)(SDL_AudioStream *);
    void (*Delay)(Uint32);
} sdl3;

static int loadSdl3(void)
{
    void *h;
    if (sdl3.Delay)
        return 0;
    if (!(h = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD)) && !(h = dlopen("libSDL3.so.0", RTLD_NOW)))
    {
        log_error("Raw Thrills: libSDL3.so.0 not found: %s", dlerror());
        return -1;
    }
#define LOAD(name) *(void **)&sdl3.name = dlsym(h, "SDL_" #name)
    LOAD(WasInit);
    LOAD(InitSubSystem);
    LOAD(GetError);
    LOAD(OpenAudioDeviceStream);
    LOAD(ResumeAudioStreamDevice);
    LOAD(DestroyAudioStream);
    LOAD(GetAudioStreamQueued);
    LOAD(PutAudioStreamData);
    LOAD(ClearAudioStream);
    LOAD(Delay);
#undef LOAD
    if (!sdl3.WasInit || !sdl3.InitSubSystem || !sdl3.OpenAudioDeviceStream || !sdl3.Delay)
        return -1;
    return 0;
}

static int dspFd = -1;
static SDL_AudioStream *dspStream;
static SDL_AudioSpec dspSpec = {SDL_AUDIO_S16LE, 2, 44100};
static int fragments = DSP_FRAGMENTS;
static int fragmentSize = DSP_FRAGMENT_SIZE;

int ossDspIsPath(const char *path)
{
    return (isRawThrillsGame() || isNamcoEs1Game()) && path && strcmp(path, "/dev/dsp") == 0;
}

int ossDspIsFd(int fd)
{
    return fd >= 0 && fd == dspFd;
}

// The device is (re)created on the first write, once the game has chosen
// its format.
static int dspStart(void)
{
    if (dspStream)
        return 0;
    if (loadSdl3() != 0)
        return -1;
    if (!sdl3.WasInit(SDL_INIT_AUDIO) && !sdl3.InitSubSystem(SDL_INIT_AUDIO))
    {
        log_error("Raw Thrills: SDL audio init failed: %s", sdl3.GetError());
        return -1;
    }
    dspStream = sdl3.OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dspSpec, NULL, NULL);
    if (!dspStream)
    {
        log_error("Raw Thrills: cannot open audio stream: %s", sdl3.GetError());
        return -1;
    }
    sdl3.ResumeAudioStreamDevice(dspStream);
    return 0;
}

static void dspStop(void)
{
    if (dspStream)
    {
        sdl3.DestroyAudioStream(dspStream);
        dspStream = NULL;
    }
}

int ossDspOpen(void)
{
    if (dspFd >= 0)
        return dspFd;
    // A real descriptor the game can keep; all I/O on it is intercepted.
    dspFd = open("/dev/null", O_WRONLY | O_CLOEXEC);
    return dspFd;
}

void ossDspClose(void)
{
    dspStop();
    dspFd = -1;
}

static int bufferSize(void)
{
    return fragments * fragmentSize;
}

static int queuedBytes(void)
{
    return dspStream ? sdl3.GetAudioStreamQueued(dspStream) : 0;
}

// Like OSS, block while the device buffer is full.
ssize_t ossDspWrite(const void *buf, size_t count)
{
    if (dspStart() != 0)
        return (ssize_t)count;
    while (queuedBytes() > bufferSize())
        sdl3.Delay(1);
    if (!sdl3.PutAudioStreamData(dspStream, buf, (int)count))
        return -1;
    return (ssize_t)count;
}

int ossDspIoctl(unsigned long request, void *arg)
{
    int *value = arg;

    switch (request)
    {
    case SNDCTL_DSP_RESET:
    case SNDCTL_DSP_SYNC:
        if (dspStream)
            sdl3.ClearAudioStream(dspStream);
        return 0;
    case SNDCTL_DSP_GETFMTS:
        *value = AFMT_S16_LE | AFMT_U8;
        return 0;
    case SNDCTL_DSP_SETFMT:
        if (*value == AFMT_U8)
            dspSpec.format = SDL_AUDIO_U8;
        else
            *value = AFMT_S16_LE, dspSpec.format = SDL_AUDIO_S16LE;
        dspStop();
        return 0;
    case SNDCTL_DSP_CHANNELS:
        dspSpec.channels = *value > 0 ? *value : 2;
        dspStop();
        return 0;
    case SNDCTL_DSP_STEREO:
        dspSpec.channels = *value ? 2 : 1;
        dspStop();
        return 0;
    case SNDCTL_DSP_SPEED:
        dspSpec.freq = *value > 0 ? *value : 44100;
        dspStop();
        return 0;
    case SNDCTL_DSP_SETFRAGMENT:
        // 0xMMMMSSSS: max fragments, log2 of the fragment size.
        fragments = (*value >> 16) & 0xffff;
        fragmentSize = 1 << (*value & 0xffff);
        if (fragments <= 0 || fragments > 64)
            fragments = DSP_FRAGMENTS;
        if (fragmentSize < 256 || fragmentSize > 65536)
            fragmentSize = DSP_FRAGMENT_SIZE;
        return 0;
    case SNDCTL_DSP_GETBLKSIZE:
        *value = fragmentSize;
        return 0;
    case SNDCTL_DSP_GETOSPACE:
    {
        audio_buf_info *info = arg;
        int space = bufferSize() - queuedBytes();
        if (space < 0)
            space = 0;
        info->fragstotal = fragments;
        info->fragsize = fragmentSize;
        info->fragments = space / fragmentSize;
        info->bytes = space;
        return 0;
    }
    case SNDCTL_DSP_GETODELAY:
        *value = queuedBytes();
        return 0;
    default:
        // Mixer volume settings (SOUND_MIXER_WRITE_*) and anything else.
        return 0;
    }
}
