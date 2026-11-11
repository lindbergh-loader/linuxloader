// Speed Driver's second sound card: the cabinet gives each seat its own,
// and IGS's SDL had a second audio device for it (SDL_OpenAudio2 and the
// rest), which its SDL_mixer calls (Mix_OpenAudio2...). Today's SDL 1.2 has
// none: the loader's are an SDL3 stream of their own on the host's output,
// mixed with the first card's there.

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "../log/log.h"

// SDL 1.2's audio spec and formats.
typedef struct
{
    int freq;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint16_t padding;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void *userdata;
} Sdl12AudioSpec;

#define SDL12_AUDIO_U8 0x0008
#define SDL12_AUDIO_S8 0x8008
#define SDL12_AUDIO_U16LSB 0x0010
#define SDL12_AUDIO_S16LSB 0x8010
#define SDL12_AUDIO_S16MSB 0x9010

// The game links SDL 1.2, whose SDL_* names shadow SDL3's: SDL3 through
// its own handle.
static struct
{
    bool (*InitSubSystem)(SDL_InitFlags);
    SDL_AudioStream *(*OpenAudioDeviceStream)(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *);
    bool (*ResumeAudioStreamDevice)(SDL_AudioStream *);
    bool (*PauseAudioStreamDevice)(SDL_AudioStream *);
    void (*DestroyAudioStream)(SDL_AudioStream *);
    bool (*PutAudioStreamData)(SDL_AudioStream *, const void *, int);
    bool (*LockAudioStream)(SDL_AudioStream *);
    bool (*UnlockAudioStream)(SDL_AudioStream *);
    const char *(*GetError)(void);
} sdl3;

static SDL_AudioStream *stream;
static Sdl12AudioSpec spec;
static uint8_t buffer[65536];

static int loadSdl3(void)
{
    void *h;

    if (sdl3.OpenAudioDeviceStream)
        return 0;
    if (!(h = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD)) && !(h = dlopen("libSDL3.so.0", RTLD_NOW)))
        return -1;
#define LOAD(name) *(void **)&sdl3.name = dlsym(h, "SDL_" #name)
    LOAD(InitSubSystem);
    LOAD(OpenAudioDeviceStream);
    LOAD(ResumeAudioStreamDevice);
    LOAD(PauseAudioStreamDevice);
    LOAD(DestroyAudioStream);
    LOAD(PutAudioStreamData);
    LOAD(LockAudioStream);
    LOAD(UnlockAudioStream);
    LOAD(GetError);
#undef LOAD
    return sdl3.OpenAudioDeviceStream ? 0 : -1;
}

// SDL3 asks for more: the game's callback fills SDL 1.2's buffer size at a
// time.
static void SDLCALL feed(void *userdata, SDL_AudioStream *s, int additional, int total)
{
    (void)userdata;
    (void)total;
    while (additional > 0)
    {
        uint32_t size = spec.size < sizeof(buffer) ? spec.size : sizeof(buffer);
        memset(buffer, spec.silence, size);
        spec.callback(spec.userdata, buffer, (int)size);
        sdl3.PutAudioStreamData(s, buffer, (int)size);
        additional -= (int)size;
    }
}

static int bytesPerSample(uint16_t format)
{
    return (format & 0xff) / 8;
}

int SDL_OpenAudio2(Sdl12AudioSpec *desired, Sdl12AudioSpec *obtained)
{
    SDL_AudioSpec want;

    if (stream || !desired || !desired->callback || loadSdl3() != 0 || !sdl3.InitSubSystem(SDL_INIT_AUDIO))
        return -1;
    spec = *desired;
    switch (spec.format)
    {
    case SDL12_AUDIO_U8:
        want.format = SDL_AUDIO_U8;
        spec.silence = 0x80;
        break;
    case SDL12_AUDIO_S8:
        want.format = SDL_AUDIO_S8;
        spec.silence = 0;
        break;
    case SDL12_AUDIO_S16MSB:
        want.format = SDL_AUDIO_S16BE;
        spec.silence = 0;
        break;
    default:
        // (U16 too: SDL3 has none, the mixer asks for S16)
        spec.format = SDL12_AUDIO_S16LSB;
        want.format = SDL_AUDIO_S16LE;
        spec.silence = 0;
        break;
    }
    if (!spec.samples)
        spec.samples = 4096;
    spec.size = (uint32_t)spec.samples * spec.channels * bytesPerSample(spec.format);
    want.channels = spec.channels;
    want.freq = spec.freq;
    stream = sdl3.OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, feed, NULL);
    if (!stream)
    {
        log_error("IGS: the second sound card: %s", sdl3.GetError());
        return -1;
    }
    if (obtained)
        *obtained = spec;
    else
        *desired = spec;
    return 0;
}

void SDL_PauseAudio2(int pause)
{
    if (!stream)
        return;
    if (pause)
        sdl3.PauseAudioStreamDevice(stream);
    else
        sdl3.ResumeAudioStreamDevice(stream);
}

void SDL_LockAudio2(void)
{
    if (stream)
        sdl3.LockAudioStream(stream);
}

void SDL_UnlockAudio2(void)
{
    if (stream)
        sdl3.UnlockAudioStream(stream);
}

void SDL_CloseAudio2(void)
{
    if (stream)
        sdl3.DestroyAudioStream(stream);
    stream = NULL;
}

// The second card's mixing is the first's (SDL 1.2's, by the second card's
// format).
void SDL_MixAudio2(uint8_t *dst, const uint8_t *src, uint32_t len, int volume)
{
    static void (*mix)(uint8_t *, const uint8_t *, uint32_t, int);

    if (!mix)
        *(void **)&mix = dlsym(RTLD_DEFAULT, "SDL_MixAudio");
    if (mix)
        mix(dst, src, len, volume);
}
