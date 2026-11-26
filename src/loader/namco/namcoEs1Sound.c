// The Namco ES1 sound driver (nsAdrv.dll), on top of SDL3 audio.
//
// The games' sound core (CbnusCoreDevice) dlopen()s ./nsAdrv.dll and
// dlsym()s its entry points; the real driver plays through ALSA on the
// cabinet's 5.1 device ("surround51"), which a PC does not have: its
// nsAdrv_init fails and the game exits. The loader answers those dlsym()s
// with the functions below instead (see namcoEs1.c). How the game drives them
// (CbnusCoreDevice::Init and adrv_worker_thread):
//
//   nsAdrv_init(mixer device, pcm device, channels 6, bits 32, rate 48000,
//               period 256, mixer state)                       < 0: exit(1)
//   loop: nsAdrv_wait(6) gives the frames the device can take; under 256
//         it calls nsAdrv_mixup(mixer state) and asks again; otherwise it
//         mixes 256 frames, as 32-bit samples in ALSA's 5.1 order (FL FR RL
//         RR C LFE), and nsAdrv_write()s them; nsAdrv_start() after the
//         first.
//   nsAdrv_mixsts(channel): the hardware mixer's status (0: none here, the
//         game's volumes stay its own).

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "namcoEs1.h"
#include "../log/log.h"

// The game links SDL 1.2, whose SDL_* names shadow SDL3's in the global
// scope: SDL3 is called through its own library handle.
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
    void (*Delay)(Uint32);
} sdl3;

static int loadSdl3(void)
{
    void *h;
    if (sdl3.Delay)
        return 0;
    if (!(h = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD)) && !(h = dlopen("libSDL3.so.0", RTLD_NOW)))
    {
        log_error("Namco: libSDL3.so.0 not found: %s", dlerror());
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
    LOAD(Delay);
#undef LOAD
    return sdl3.WasInit && sdl3.InitSubSystem && sdl3.OpenAudioDeviceStream && sdl3.Delay ? 0 : -1;
}

// Frames kept queued: the device is "ready" (nsAdrv_wait) while fewer than
// LOW are, and says it can take up to HIGH.
#define LOW 1024
#define HIGH 2048

static SDL_AudioStream *stream;
static int channels, rate, period;

static int adrvInit(const char *mixer, const char *pcm, int ch, int bits, int hz, int frames, void *mixerState)
{
    (void)mixer;
    (void)mixerState;
    channels = ch;
    rate = hz;
    period = frames > 0 ? frames : 256;
    if (bits != 32 || loadSdl3() != 0)
    {
        log_error("Namco: sound: %d-bit samples or no SDL3, no sound", bits);
        return 0;
    }
    if (!(sdl3.WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !sdl3.InitSubSystem(SDL_INIT_AUDIO))
    {
        log_error("Namco: sound: %s", sdl3.GetError());
        return 0;
    }
    SDL_AudioSpec spec = {SDL_AUDIO_S32LE, ch, hz};
    if (!(stream = sdl3.OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)))
        log_error("Namco: sound: %s", sdl3.GetError());
    else
        log_info("Namco: sound for %s: %d channels, %d Hz", pcm, ch, hz);
    return 0; // without a device, the game runs silent
}

static int adrvTerm(void)
{
    if (stream)
        sdl3.DestroyAudioStream(stream);
    stream = NULL;
    return 0;
}

// The frames the device can take, once it can take a period's worth.
static int adrvWait(int timeout)
{
    (void)timeout;
    if (!stream)
    {
        // no device: the game's pace, a period at a time
        struct timespec t = {0, period * 1000000000L / (rate > 0 ? rate : 48000)};
        nanosleep(&t, NULL);
        return HIGH;
    }
    for (;;)
    {
        int queued = sdl3.GetAudioStreamQueued(stream) / (channels * 4);
        if (queued < LOW)
            return HIGH - queued;
        sdl3.Delay(1);
    }
}

static int adrvStart(void)
{
    if (stream)
        sdl3.ResumeAudioStreamDevice(stream);
    return 0;
}

// ALSA's 5.1 order (FL FR RL RR C LFE) into SDL's (FL FR C LFE RL RR).
static int adrvWrite(const int32_t *samples, int frames)
{
    static const int fromAlsa[6] = {0, 1, 4, 5, 2, 3};
    int32_t block[256 * 6];

    if (!stream)
        return frames;
    if (channels != 6)
    {
        sdl3.PutAudioStreamData(stream, samples, frames * channels * 4);
        return frames;
    }
    for (int done = 0; done < frames;)
    {
        int n = frames - done < 256 ? frames - done : 256;
        for (int f = 0; f < n; f++)
            for (int c = 0; c < 6; c++)
                block[f * 6 + c] = samples[(done + f) * 6 + fromAlsa[c]];
        sdl3.PutAudioStreamData(stream, block, n * 6 * 4);
        done += n;
    }
    return frames;
}

static int adrvMixup(void *mixerState)
{
    (void)mixerState;
    return 0;
}

static int adrvMixsts(int channel)
{
    (void)channel;
    return 0;
}

void *namcoEs1SoundSymbol(const char *name)
{
    static const struct
    {
        const char *name;
        void *function;
    } entries[] = {
        {"nsAdrv_init", (void *)adrvInit},   {"nsAdrv_term", (void *)adrvTerm},   {"nsAdrv_wait", (void *)adrvWait},
        {"nsAdrv_start", (void *)adrvStart}, {"nsAdrv_write", (void *)adrvWrite}, {"nsAdrv_mixup", (void *)adrvMixup},
        {"nsAdrv_mixsts", (void *)adrvMixsts},
    };
    for (size_t i = 0; name && i < sizeof(entries) / sizeof(entries[0]); i++)
        if (!strcmp(name, entries[i].name))
            return entries[i].function;
    return NULL;
}
