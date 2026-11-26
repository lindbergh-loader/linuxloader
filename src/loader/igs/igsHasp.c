// The HASP HL dongle of IGS's E2200 games (Fish Hunter SKL 2): the game
// links Sentinel's API in and calls it from its THaspKeyPro singleton; the
// calls are replaced here (see igsGame.h's haspApi).
//
// The game logs in to feature 2, reads its 40-byte file 0xfff5 and passes
// it through the dongle's transform: the FPGA user key (0-15), the PC's and
// the board's first packet keys (0x10, 0x14), the packets' checksum seed
// (0x18) and three words the board must answer (0x1c-0x24). haspMemory is
// that file as the game uses it: the transforms leave buffers as they are.

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igs.h"
#include "../patching/flowControl.h"

#define HASP_STATUS_OK 0
#define HASP_FILE_NOT_FOUND 9 // HASP_INV_FILEID
#define HASP_MEM_RANGE 1

static const IgsGame *game;
static int trace;
// The dongle's file, written to as the game writes
static uint8_t memory[4096];

__attribute__((force_align_arg_pointer)) static int haspLogin(int feature, const void *vendorCode, int *handle)
{
    (void)vendorCode;
    if (handle)
        *handle = 1;
    if (trace)
        printf("IGS HASP: login to feature %d\n", feature);
    return HASP_STATUS_OK;
}

__attribute__((force_align_arg_pointer)) static int haspLogout(int handle)
{
    (void)handle;
    return HASP_STATUS_OK;
}

__attribute__((force_align_arg_pointer)) static int haspRead(int handle, int file, uint32_t offset, uint32_t length, void *buffer)
{
    (void)handle;
    if (trace)
        printf("IGS HASP: read file %#x, %u bytes at %#x\n", file, length, offset);
    if (file != game->haspFile)
        return HASP_FILE_NOT_FOUND;
    if (offset > sizeof(memory) || length > sizeof(memory) - offset)
        return HASP_MEM_RANGE;
    memcpy(buffer, memory + offset, length);
    return HASP_STATUS_OK;
}

__attribute__((force_align_arg_pointer)) static int haspWrite(int handle, int file, uint32_t offset, uint32_t length, const void *buffer)
{
    (void)handle;
    if (trace)
        printf("IGS HASP: write file %#x, %u bytes at %#x\n", file, length, offset);
    if (file != game->haspFile)
        return HASP_FILE_NOT_FOUND;
    if (offset > sizeof(memory) || length > sizeof(memory) - offset)
        return HASP_MEM_RANGE;
    memcpy(memory + offset, buffer, length);
    return HASP_STATUS_OK;
}

__attribute__((force_align_arg_pointer)) static int haspTransform(int handle, void *buffer, uint32_t length)
{
    (void)handle;
    (void)buffer;
    if (trace)
        printf("IGS HASP: transform of %u bytes\n", length);
    return HASP_STATUS_OK;
}

// Fish Hunter SKL 2's sound loader (igsGame.h's soundLoad), with the size
// its file reader gives.
static int (*soundSlot)(void);
static void *(*soundRead)(const char *, int *);

__attribute__((force_align_arg_pointer)) static int soundLoad(const char *path)
{
    static void *(*rwFromMem)(void *, int);
    static void *(*loadWav)(void *, int);
    static const char *(*getError)(void);
    int size = 0, slot = soundSlot();
    void *data = soundRead(path, &size), *chunk;

    if (!rwFromMem)
    {
        *(void **)&rwFromMem = dlsym(RTLD_DEFAULT, "SDL_RWFromMem");
        *(void **)&loadWav = dlsym(RTLD_DEFAULT, "Mix_LoadWAV_RW");
        *(void **)&getError = dlsym(RTLD_DEFAULT, "SDL_GetError");
    }
    chunk = data && rwFromMem && loadWav ? loadWav(rwFromMem(data, size), 1) : NULL;
    ((void **)(uintptr_t)game->soundTable)[slot * 3] = chunk;
    if (!chunk)
        printf("Couldn't load %s: %s\n", path, getError ? getError() : "");
    return slot;
}

static void *const replacements[] = {haspLogin, haspLogout, haspRead, haspWrite, haspTransform, haspTransform};

// The dongle's API in a library (igsGame.h's a36Library), by its names.
void igsHaspLibrary(void *handle)
{
    static const char *const names[] = {"hasp_login", "hasp_logout", "hasp_read", "hasp_write", "hasp_encrypt", "hasp_decrypt"};
    if (!game)
        return;
    for (int i = 0; i < 6; i++)
    {
        void *f = dlsym(handle, names[i]);
        if (f)
            detourFunction((size_t)f, replacements[i]);
    }
}

void igsHaspInstall(const IgsGame *g)
{

    game = g;
    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    if (g->haspMemory)
        memcpy(memory, g->haspMemory, (size_t)g->haspMemorySize < sizeof(memory) ? (size_t)g->haspMemorySize : sizeof(memory));
    for (int i = 0; i < 6; i++)
        if (g->haspApi[i])
            detourFunction(g->haspApi[i], replacements[i]);
    if (g->soundLoad)
    {
        *(void **)&soundSlot = (void *)(uintptr_t)g->soundSlot;
        *(void **)&soundRead = (void *)(uintptr_t)g->soundRead;
        detourFunction(g->soundLoad, soundLoad);
    }
}
