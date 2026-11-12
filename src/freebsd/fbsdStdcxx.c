// The C++ library's file streams, as FreeBSD 9.1's lays them out (see
// fbsdStdcxx.h).
//
// FreeBSD's libstdc++ (GCC 4.2) and today's share their ABI, but not the
// layout of basic_filebuf: FreeBSD's mbstate_t is 128 bytes (three in a
// filebuf), its mutex a pointer: its filebuf is 0x1dc bytes, the host's
// smaller. The game builds its ifstreams and ofstreams inline (their
// basic_ios after its own filebuf's size) and destroys them through the
// library. Fixed up for that:
//   - the filebuf inside is the host's, built in place (it fits): its
//     functions are the host's, and its inline destructor's ~__basic_file
//     at FreeBSD's offset does nothing;
//   - the game's copies of the streams' vtables and VTTs (R_386_COPY) keep
//     the host's entries but FreeBSD's virtual base offsets, with
//     construction vtables of their own, and destructors that know them.
// fstream and stringstream the game builds and destroys through the
// library: the host's layout throughout, in the game's larger space.

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbsdStdcxx.h"

#define SHIM __attribute__((force_align_arg_pointer))
#define VTABLE_WORDS 10
#define VTT_WORDS 4

#define FBSD_FILEBUF_SIZE 0x1dc

typedef struct
{
    const char *vtable, *vtt, *d1, *d0;
    // the stream's own members before its filebuf (istream: vptr and
    // gcount; ostream: vptr)
    uint32_t filebufOffset;
    uint32_t hostVbase, fbsdVbase;
} Stream;

static Stream streams[] = {
    {"_ZTVSt14basic_ifstreamIcSt11char_traitsIcEE", "_ZTTSt14basic_ifstreamIcSt11char_traitsIcEE",
     "_ZNSt14basic_ifstreamIcSt11char_traitsIcEED1Ev", "_ZNSt14basic_ifstreamIcSt11char_traitsIcEED0Ev", 8, 0, 0},
    {"_ZTVSt14basic_ofstreamIcSt11char_traitsIcEE", "_ZTTSt14basic_ofstreamIcSt11char_traitsIcEE",
     "_ZNSt14basic_ofstreamIcSt11char_traitsIcEED1Ev", "_ZNSt14basic_ofstreamIcSt11char_traitsIcEED0Ev", 4, 0, 0},
};

static void (*filebufDtor)(void *);
static void (*iosBaseDtor)(void *);
static void (*operatorDelete)(void *);

static void destroy(const Stream *s, void *self)
{
    if (!filebufDtor)
    {
        *(void **)&filebufDtor = dlsym(RTLD_DEFAULT, "_ZNSt13basic_filebufIcSt11char_traitsIcEED1Ev");
        *(void **)&iosBaseDtor = dlsym(RTLD_DEFAULT, "_ZNSt8ios_baseD2Ev");
        *(void **)&operatorDelete = dlsym(RTLD_DEFAULT, "_ZdlPv");
    }
    filebufDtor((char *)self + s->filebufOffset);
    iosBaseDtor((char *)self + s->fbsdVbase);
}

SHIM static void ifstreamD1(void *self)
{
    destroy(&streams[0], self);
}
SHIM static void ifstreamD0(void *self)
{
    destroy(&streams[0], self);
    operatorDelete(self);
}
SHIM static void ifstreamThunkD1(void *ios)
{
    ifstreamD1((char *)ios - streams[0].fbsdVbase);
}
SHIM static void ifstreamThunkD0(void *ios)
{
    ifstreamD0((char *)ios - streams[0].fbsdVbase);
}
SHIM static void ofstreamD1(void *self)
{
    destroy(&streams[1], self);
}
SHIM static void ofstreamD0(void *self)
{
    destroy(&streams[1], self);
    operatorDelete(self);
}
SHIM static void ofstreamThunkD1(void *ios)
{
    ofstreamD1((char *)ios - streams[1].fbsdVbase);
}
SHIM static void ofstreamThunkD0(void *ios)
{
    ofstreamD0((char *)ios - streams[1].fbsdVbase);
}

static void *const destructors[2][4] = {
    {(void *)ifstreamD1, (void *)ifstreamD0, (void *)ifstreamThunkD1, (void *)ifstreamThunkD0},
    {(void *)ofstreamD1, (void *)ofstreamD0, (void *)ofstreamThunkD1, (void *)ofstreamThunkD0},
};

// A vtable as FreeBSD's: the host's, its virtual base offsets FreeBSD's and,
// for a complete object's, its destructors ours.
static void fixVtable(const Stream *s, uint32_t *words, const uint32_t *host, int complete, int index)
{
    memcpy(words, host, VTABLE_WORDS * 4);
    for (int i = 0; i < VTABLE_WORDS; i++)
        if (words[i] == s->hostVbase)
            words[i] = s->fbsdVbase;
        else if (words[i] == (uint32_t)-(int32_t)s->hostVbase)
            words[i] = (uint32_t)-(int32_t)s->fbsdVbase;
    if (complete)
    {
        words[3] = (uint32_t)(uintptr_t)destructors[index][0];
        words[4] = (uint32_t)(uintptr_t)destructors[index][1];
        words[8] = (uint32_t)(uintptr_t)destructors[index][2];
        words[9] = (uint32_t)(uintptr_t)destructors[index][3];
    }
}

int fbsdStdcxxCopy(const char *name, void *where, const void *host, uint32_t size)
{
    for (int i = 0; i < 2; i++)
    {
        Stream *s = &streams[i];
        if (!strcmp(name, s->vtable) && size == VTABLE_WORDS * 4)
        {
            s->hostVbase = ((const uint32_t *)host)[0];
            s->fbsdVbase = s->filebufOffset + FBSD_FILEBUF_SIZE;
            fixVtable(s, where, host, 1, i);
            return 1;
        }
        if (!strcmp(name, s->vtt) && size == VTT_WORDS * 4)
        {
            const uint32_t *hostVtt = host;
            uint32_t *vtt = where, *construction = malloc(VTABLE_WORDS * 4);
            uint32_t *vtable = dlsym(RTLD_DEFAULT, s->vtable);

            if (!s->hostVbase && vtable)
            {
                s->hostVbase = vtable[0];
                s->fbsdVbase = s->filebufOffset + FBSD_FILEBUF_SIZE;
            }
            // [0], [3]: the complete object's vtable (the game's copy, fixed
            // up with its own relocation); [1], [2]: the construction vtable
            // of its istream / ostream base.
            fixVtable(s, construction, (const uint32_t *)(uintptr_t)(hostVtt[1] - 0xc), 0, i);
            memcpy(vtt, hostVtt, VTT_WORDS * 4);
            vtt[1] = (uint32_t)(uintptr_t)construction + 0xc;
            vtt[2] = (uint32_t)(uintptr_t)construction + (hostVtt[2] - (hostVtt[1] - 0xc));
            return 1;
        }
    }
    return 0;
}

void fbsdStdcxxFixVtt(uint32_t gameVtable, uint32_t gameVtt, const char *vtableName)
{
    for (int i = 0; i < 2; i++)
        if (!strcmp(vtableName, streams[i].vtable))
        {
            uint32_t *vtt = (uint32_t *)(uintptr_t)gameVtt;
            uint32_t *host = dlsym(RTLD_DEFAULT, streams[i].vtable);
            if (!host)
                return;
            vtt[0] = gameVtable + 0xc;
            vtt[3] = gameVtable + 0x20;
        }
}

// The filebuf's inline destructor's member: nothing to do (the host's
// filebuf in its place has none there).
SHIM static void basicFileDtor(void *self)
{
    (void)self;
}

void *fbsdStdcxxShim(const char *name)
{
    if (!strcmp(name, "_ZNSt12__basic_fileIcED1Ev") || !strcmp(name, "_ZNSt12__basic_fileIcED2Ev"))
        return (void *)basicFileDtor;
    for (int i = 0; i < 2; i++)
    {
        if (!strcmp(name, streams[i].d1))
            return destructors[i][0];
        if (!strcmp(name, streams[i].d0))
            return destructors[i][1];
    }
    return NULL;
}
