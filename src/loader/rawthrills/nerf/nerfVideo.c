// The light gun border ([Display] BORDER_ENABLED, WHITE_BORDER_PERCENTAGE,
// BLACK_BORDER_PERCENTAGE), drawn around each frame right before the swap,
// as the other gun games get it.
//
// Unity's player does not link libGL: it opens libGL.so.1 and looks
// glXSwapBuffers and glXGetProcAddressARB up by dlsym. Its own dlsym import
// slot is pointed at a wrapper handing it the loader's swap instead. That
// one slot only: an exported dlsym would stand in for every library's (and
// the g7 games', linuxloader64.so being theirs too).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <glad/gl.h>
#include "nerf.h"
#include "../../graphics/borderFrame.h"

static struct
{
    int enabled;
    float white, black; // fractions of the picture
} border;

static void *(*realGetProcAddress)(const char *);
static void (*realSwap)(void *, unsigned long);
static int (*getGeometry)(void *, unsigned long, unsigned long *, int *, int *, unsigned int *, unsigned int *,
                          unsigned int *, unsigned int *);

static void gameSwap(void *display, unsigned long drawable)
{
    static int glLoaded;
    unsigned long root;
    int x, y, bordered = 0;
    unsigned int w, h, bw, depth;

    if (!glLoaded)
    {
        void *x11 = dlopen("libX11.so.6", RTLD_NOW);
        *(void **)&getGeometry = x11 ? dlsym(x11, "XGetGeometry") : NULL;
        glLoaded = realGetProcAddress && getGeometry && gladLoadGL((GLADloadfunc)realGetProcAddress) ? 1 : -1;
        if (glLoaded < 0)
            nerfLog("cannot load the GL entry points, no gun border\n");
        else if (getGeometry(display, drawable, &root, &x, &y, &w, &h, &bw, &depth))
            nerfLog("gun border around the %ux%u window\n", w, h);
    }
    if (glLoaded > 0 && getGeometry(display, drawable, &root, &x, &y, &w, &h, &bw, &depth))
    {
        borderFrameBegin((int)w, (int)h, 1, border.white, border.black);
        bordered = 1;
    }
    realSwap(display, drawable);
    if (bordered)
        borderFrameEnd();
}

static void *glEntry(const char *name, void *real)
{
    if (real && name && !strcmp(name, "glXSwapBuffers"))
    {
        realSwap = real;
        return (void *)gameSwap;
    }
    return real;
}

static void *gameGetProcAddress(const char *name)
{
    return glEntry(name, realGetProcAddress(name));
}

static void *gameDlsym(void *handle, const char *name)
{
    void *p = dlsym(handle, name);
    if (p && name && (!strcmp(name, "glXGetProcAddressARB") || !strcmp(name, "glXGetProcAddress")))
    {
        realGetProcAddress = p;
        return (void *)gameGetProcAddress;
    }
    return glEntry(name, p);
}

// The executable's import slot of name, NULL if it has none.
static void **importSlot(const char *name)
{
    struct link_map *map = dlopen(NULL, RTLD_NOW | RTLD_NOLOAD);
    const Elf64_Rela *rela = NULL;
    const Elf64_Sym *symtab = NULL;
    const char *strtab = NULL;
    size_t size = 0;

    if (!map)
        return NULL;
    for (const ElfW(Dyn) *d = map->l_ld; d->d_tag != DT_NULL; d++)
        switch (d->d_tag)
        {
            case DT_JMPREL: rela = (const Elf64_Rela *)d->d_un.d_ptr; break;
            case DT_PLTRELSZ: size = d->d_un.d_val; break;
            case DT_SYMTAB: symtab = (const Elf64_Sym *)d->d_un.d_ptr; break;
            case DT_STRTAB: strtab = (const char *)d->d_un.d_ptr; break;
        }
    if (!rela || !symtab || !strtab)
        return NULL;
    for (size_t i = 0; i < size / sizeof(*rela); i++)
        if (ELF64_R_TYPE(rela[i].r_info) == R_X86_64_JUMP_SLOT &&
            !strcmp(strtab + symtab[ELF64_R_SYM(rela[i].r_info)].st_name, name))
            return (void **)(map->l_addr + rela[i].r_offset);
    return NULL;
}

void nerfVideoInit(void)
{
    const char *v;
    void **slot;
    long page = sysconf(_SC_PAGESIZE);

    border.enabled = nerfIniInt("Display", "BORDER_ENABLED", 0);
    border.white = ((v = nerfIniValue("Display", "WHITE_BORDER_PERCENTAGE")) ? strtof(v, NULL) : 2.f) / 100.f;
    border.black = ((v = nerfIniValue("Display", "BLACK_BORDER_PERCENTAGE")) ? strtof(v, NULL) : 0.f) / 100.f;
    if (!border.enabled)
        return;
    if (!(slot = importSlot("dlsym")))
    {
        nerfLog("the player imports no dlsym: no gun border\n");
        return;
    }
    // Full RELRO (BIND_NOW) leaves the slot read-only.
    mprotect((void *)((uintptr_t)slot & ~(uintptr_t)(page - 1)), page, PROT_READ | PROT_WRITE);
    *slot = (void *)gameDlsym;
    nerfLog("gun border: white %.1f%%, black %.1f%%\n", border.white * 100.f, border.black * 100.f);
}
