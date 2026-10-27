// libSDLAdm.so: provides the SDL_ADM_* entry points the Counter Strike NEO
// engine (engine_amd.so) references directly. The cabinet's libSDL-1.2.so.0
// (Namco's Alchemy-patched SDL) exports these; a standard SDL 1.2 does not,
// so the engine fails to load against the standard SDL with "undefined
// symbol: SDL_ADM_GetDevice". This shim supplies the one symbol the engine
// actually imports (SDL_ADM_GetDevice), returning NULL (no Alchemy device).
// engine's AL (audio) manager and the few adm query sites tolerate a NULL
// device and fall back to the standard SDL audio path.

#include <stddef.h>

// The engine calls SDL_ADM_GetDevice(index) and reads a struct pointer back
// (fields at +0, +4, +8). With no Alchemy device present it returns NULL and
// the engine's callers treat it as "device not available".
void *SDL_ADM_GetDevice(int index)
{
    (void)index;
    return NULL;
}
