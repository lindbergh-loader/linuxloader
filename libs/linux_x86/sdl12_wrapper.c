// SDL 1.2 wrapper for Counter Strike NEO (installed at csneo2/lib/libSDL-1.2.so.0).
//
// Two problems to solve, both because we cannot byte-patch the engine
// (mprotect ENOMEM in the 32-bit process) and LD_PRELOAD cannot intercept the
// engine's locally-resolved SDL calls:
//
//  1. The engine's CMtSDL::InitDraw / sdl_main set cabinet GL attributes
//     (RED/GREEN/BLUE_SIZE = 5, the 15-bit Alchemy panel) that no Mesa GLX
//     visual matches -> "Couldn't find matching GLX visual". We override
//     SDL_GL_SetAttribute to clamp sub-8-bit color sizes up to 8.
//
//  2. The engine's other NEEDED SDL libs (libSDL_mixer/ttf/image) resolve
//     their SDL_* symbols against libSDL-1.2.so.0. When that is this wrapper
//     (instead of the full real SDL), those symbols must still be provided.
//     We forward the exact set the three libs need (see the UND list of each),
//     resolved lazily via dlsym(RTLD_NEXT) to the real SDL loaded alongside.
//
// The wrapper is linked against the real standard SDL (kept as
// libSDL-1.2.real.so.0, same dir) so it is present in the same load group.
//
// Build:
//   cp <standard-sdl> csneo2/lib/libSDL-1.2.real.so.0
//   gcc -m32 -shared -fPIC -o csneo2/lib/libSDL-1.2.so.0 sdl12_wrapper.c \
//       -L csneo2/lib -l:libSDL-1.2.real.so.0 -Wl,--no-as-needed -ldl

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>

// --- The one override: clamp cabinet 5-bit RGB to 8-bit, drop stereo. ---
typedef int (*RealGLSetAttr)(int, int);
int SDL_GL_SetAttribute(int attr, int value)
{
    static RealGLSetAttr real;
    if (!real)
        real = (RealGLSetAttr)dlsym(RTLD_NEXT, "SDL_GL_SetAttribute");
    if (!real)
        return -1;
    if (attr >= 0 && attr <= 3 && value > 0 && value < 8)
        value = 8; // RED/GREEN/BLUE/ALPHA: cabinet 5-bit panel -> 8-bit
    if (attr == 12)
        return 0;  // STEREO: no stereo on a PC
    return real(attr, value);
}

// --- Forwarded symbols needed by libSDL_mixer-1.2.so.0 ---
typedef int (*f_i_i)(int);
typedef int (*f_i_iiii)(int, int, int, int);
typedef int (*f_i_iiiiii)(int, int, int, int, int, int);
typedef void (*f_v_i)(int);
typedef void (*f_v_v)(void);
typedef void (*f_v_p)(void *);
typedef unsigned int (*f_ui_v)(void);
typedef unsigned int (*f_ui_p)(void *);
typedef void *(*f_p_p)(void *);
typedef void *(*f_p_ii)(void *, int);
typedef void *(*f_p_iiii)(void *, int, int, int);
typedef const char *(*f_cs_v)(void);

int SDL_OpenAudio(int freq, int format, int channels, int callbacksize)
{
    static f_i_iiii real;
    if (!real) real = (f_i_iiii)dlsym(RTLD_NEXT, "SDL_OpenAudio");
    return real ? real(freq, format, channels, callbacksize) : -1;
}
void SDL_CloseAudio(void)
{
    static f_v_v real;
    if (!real) real = (f_v_v)dlsym(RTLD_NEXT, "SDL_CloseAudio");
    if (real) real();
}
void SDL_PauseAudio(int pause_on)
{
    static f_v_i real;
    if (!real) real = (f_v_i)dlsym(RTLD_NEXT, "SDL_PauseAudio");
    if (real) real(pause_on);
}
void SDL_LockAudio(void)
{
    static f_v_v real;
    if (!real) real = (f_v_v)dlsym(RTLD_NEXT, "SDL_LockAudio");
    if (real) real();
}
void SDL_UnlockAudio(void)
{
    static f_v_v real;
    if (!real) real = (f_v_v)dlsym(RTLD_NEXT, "SDL_UnlockAudio");
    if (real) real();
}
void SDL_MixAudio(void *dst, const void *src, int len, int volume)
{
    static void (*real)(void *, const void *, int, int);
    if (!real) real = (void (*)(void *, const void *, int, int))dlsym(RTLD_NEXT, "SDL_MixAudio");
    if (real) real(dst, src, len, volume);
}
int SDL_MixAudioVolume(int volume)
{
    static f_i_i real;
    if (!real) real = (f_i_i)dlsym(RTLD_NEXT, "SDL_MixAudioVolume");
    return real ? real(volume) : 0;
}
int SDL_LoadWAV_RW(void *src, int freesrc, void **audio_buf, unsigned int *audio_len)
{
    static int (*real)(void *, int, void **, unsigned int *);
    if (!real) real = (int (*)(void *, int, void **, unsigned int *))dlsym(RTLD_NEXT, "SDL_LoadWAV_RW");
    return real ? real(src, freesrc, audio_buf, audio_len) : -1;
}
int SDL_FreeWAV(void *audio_buf)
{
    static int (*real)(void *);
    if (!real) real = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_FreeWAV");
    return real ? real(audio_buf) : -1;
}
int SDL_ConvertAudio(void *cvt)
{
    static int (*real)(void *);
    if (!real) real = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_ConvertAudio");
    return real ? real(cvt) : -1;
}
int SDL_BuildAudioCVT(void *cvt, int src_format, int src_channels, int src_rate, int dst_format, int dst_channels, int dst_rate)
{
    static int (*real)(void *, int, int, int, int, int, int);
    if (!real) real = (int (*)(void *, int, int, int, int, int, int))dlsym(RTLD_NEXT, "SDL_BuildAudioCVT");
    return real ? real(cvt, src_format, src_channels, src_rate, dst_format, dst_channels, dst_rate) : -1;
}
unsigned int SDL_GetTicks(void)
{
    static f_ui_v real;
    if (!real) real = (f_ui_v)dlsym(RTLD_NEXT, "SDL_GetTicks");
    return real ? real() : 0;
}
void SDL_Delay(unsigned int ms)
{
    static void (*real)(unsigned int);
    if (!real) real = (void (*)(unsigned int))dlsym(RTLD_NEXT, "SDL_Delay");
    if (real) real(ms);
}
void *SDL_RWFromFP(void *fp, int close_rw)
{
    static f_p_ii real;
    if (!real) real = (f_p_ii)dlsym(RTLD_NEXT, "SDL_RWFromFP");
    return real ? real(fp, close_rw) : 0;
}
void *SDL_RWFromFile(const char *file, const char *type)
{
    static void *(*real)(const char *, const char *);
    if (!real) real = (void *(*)(const char *, const char *))dlsym(RTLD_NEXT, "SDL_RWFromFile");
    return real ? real(file, type) : 0;
}
unsigned int SDL_ReadBE16(void *src)
{
    static f_ui_p real;
    if (!real) real = (f_ui_p)dlsym(RTLD_NEXT, "SDL_ReadBE16");
    return real ? real(src) : 0;
}
unsigned int SDL_ReadBE32(void *src)
{
    static f_ui_p real;
    if (!real) real = (f_ui_p)dlsym(RTLD_NEXT, "SDL_ReadBE32");
    return real ? real(src) : 0;
}
unsigned int SDL_ReadLE16(void *src)
{
    static f_ui_p real;
    if (!real) real = (f_ui_p)dlsym(RTLD_NEXT, "SDL_ReadLE16");
    return real ? real(src) : 0;
}
unsigned int SDL_ReadLE32(void *src)
{
    static f_ui_p real;
    if (!real) real = (f_ui_p)dlsym(RTLD_NEXT, "SDL_ReadLE32");
    return real ? real(src) : 0;
}
const char *SDL_GetError(void)
{
    static f_cs_v real;
    if (!real) real = (f_cs_v)dlsym(RTLD_NEXT, "SDL_GetError");
    return real ? real() : 0;
}
const char *SDL_Error(int error_code)
{
    static const char *(*real)(int);
    if (!real) real = (const char *(*)(int))dlsym(RTLD_NEXT, "SDL_Error");
    return real ? real(error_code) : 0;
}
int SDL_ClearError(void)
{
    static int (*real)(void);
    if (!real) real = (int (*)(void))dlsym(RTLD_NEXT, "SDL_ClearError");
    return real ? real() : 0;
}
void SDL_SetError(const char *errfmt, ...)
{
    static void (*real)(const char *, ...);
    if (!real) real = (void (*)(const char *, ...))dlsym(RTLD_NEXT, "SDL_SetError");
    if (real) real(errfmt);
}
void *SDL_CreateRGBSurface(unsigned int flags, int width, int height, int depth, unsigned int Rmask, unsigned int Gmask, unsigned int Bmask, unsigned int Amask)
{
    static void *(*real)(unsigned int, int, int, int, unsigned int, unsigned int, unsigned int, unsigned int);
    if (!real) real = (void *(*)(unsigned int, int, int, int, unsigned int, unsigned int, unsigned int, unsigned int))dlsym(RTLD_NEXT, "SDL_CreateRGBSurface");
    return real ? real(flags, width, height, depth, Rmask, Gmask, Bmask, Amask) : 0;
}
void *SDL_FreeSurface(void *surface)
{
    static f_v_p real;
    if (!real) real = (f_v_p)dlsym(RTLD_NEXT, "SDL_FreeSurface");
    if (real) real(surface);
}
void SDL_SetColorKey(void *surface, int flag, unsigned int key)
{
    static void (*real)(void *, int, unsigned int);
    if (!real) real = (void (*)(void *, int, unsigned int))dlsym(RTLD_NEXT, "SDL_SetColorKey");
    if (real) real(surface, flag, key);
}
unsigned int SDL_MapRGB(void *format, unsigned char r, unsigned char g, unsigned char b)
{
    static unsigned int (*real)(void *, unsigned char, unsigned char, unsigned char);
    if (!real) real = (unsigned int (*)(void *, unsigned char, unsigned char, unsigned char))dlsym(RTLD_NEXT, "SDL_MapRGB");
    return real ? real(format, r, g, b) : 0;
}

// --- libambizdev-nsl.so (cabinet network lib) also needs these ---
void *SDL_ConvertSurface(void *surface, void *format, int flags)
{
    static void *(*real)(void *, void *, int);
    if (!real) real = (void *(*)(void *, void *, int))dlsym(RTLD_NEXT, "SDL_ConvertSurface");
    return real ? real(surface, format, flags) : 0;
}
int SDL_SetAlpha(void *surface, int flag, unsigned int surface_alpha)
{
    static int (*real)(void *, int, unsigned int);
    if (!real) real = (int (*)(void *, int, unsigned int))dlsym(RTLD_NEXT, "SDL_SetAlpha");
    return real ? real(surface, flag, surface_alpha) : -1;
}
int SDL_UpperBlit(void *src, void *srcrect, void *dst, void *dstrect)
{
    static int (*real)(void *, void *, void *, void *);
    if (!real) real = (int (*)(void *, void *, void *, void *))dlsym(RTLD_NEXT, "SDL_UpperBlit");
    return real ? real(src, srcrect, dst, dstrect) : -1;
}
