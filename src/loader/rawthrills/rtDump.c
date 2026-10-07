// Rebuild the import tables of a Raw Thrills game dump and patch its code.
//
// The dumps were taken from running HASP Envelope-protected binaries: their
// GOTs still hold the cabinet's library addresses, and the envelope stripped
// or encrypted the relocation info that would let ld.so fix them. Each slot
// is resolved again by name (rtGame.h tables) before the game code runs.

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../log/log.h"

#define MAX_PRELOADED 8

// Old glibc entry points (__xstat, __strtol_internal, ...) only survive as
// versioned compat symbols, which plain dlsym() does not return.
static void *lookupCompat(const char *name)
{
    static const char *const versions[] = {"GLIBC_2.0", "GLIBC_2.1", "GLIBC_2.2", "GLIBC_2.3"};
    void *p = dlsym(RTLD_DEFAULT, name);
    for (size_t i = 0; !p && i < sizeof(versions) / sizeof(versions[0]); i++)
        p = dlvsym(RTLD_DEFAULT, name, versions[i]);
    return p;
}

// Handles of the LD_PRELOAD libraries (the loader itself, ...). Their symbols
// must win over a versioned lookup, which would bind straight to glibc: that
// is what ld.so would do for a normally linked game.
static size_t getPreloaded(void **handles, size_t max)
{
    const char *env = getenv("LD_PRELOAD");
    size_t count = 0;
    char *list, *save, *tok;

    if (!env || !(list = strdup(env)))
        return 0;
    for (tok = strtok_r(list, ": ", &save); tok && count < max; tok = strtok_r(NULL, ": ", &save))
    {
        void *h = dlopen(tok, RTLD_NOW | RTLD_NOLOAD);
        if (h)
            handles[count++] = h;
    }
    free(list);
    return count;
}

// The loader's GL and X11 wrappers patch Lindbergh games (resolution,
// shaders, its own window) and need state set up by the Lindbergh init; Raw
// Thrills games get the real GL/GLU/glut/GLX and Xlib functions, apart from
// the loader's Raw Thrills shader fix.
static int bypassPreloaded(const char *name)
{
    if (!strncmp(name, "gl", 2))
        return strcmp(name, "glShaderSource") != 0;
    return name[0] == 'X' && name[1] >= 'A' && name[1] <= 'Z';
}

static int isPreloaded(const void *handle, void **preloaded, size_t npreloaded)
{
    for (size_t i = 0; i < npreloaded; i++)
        if (preloaded[i] == handle)
            return 1;
    return 0;
}

// The global definition of name is one of a preloaded library (a loader
// wrapper).
static int wrapped(const char *name, void **preloaded, size_t npreloaded)
{
    void *def = dlsym(RTLD_DEFAULT, name);
    for (size_t i = 0; def && i < npreloaded; i++)
        if (dlsym(preloaded[i], name) == def)
            return 1;
    return 0;
}

// dlsym as the loader calls it (not the games' wrapper below).
void *rtRealDlsym(void *handle, const char *name)
{
    return dlsym(handle, name);
}

// A window SDL was not asked to make resizable is pinned at its size, with
// equal minimum and maximum size hints (The Walking Dead): drop them, so the
// window can be resized and the frame scales with it (see frameScale.h).
static int setWMNormalHints(void *display, unsigned long window, long *hints)
{
    static int (*real)(void *, unsigned long, long *);
    if (!real)
        real = dlsym(RTLD_NEXT, "XSetWMNormalHints");
    if (hints)
        hints[0] &= ~((1L << 4) | (1L << 5));   // XSizeHints.flags: PMinSize, PMaxSize
    return real ? real(display, window, hints) : 0;
}

// Functions the games get in place of the real ones, wherever they look
// them up: the window scaling's GL entry points and the size hints above.
static void *rtOverride(const char *name)
{
    if (name && !strcmp(name, "XSetWMNormalHints"))
        return (void *)setWMNormalHints;
    if (name && rtFrameOverride(name))
        return rtFrameOverride(name);
    if (name && rtCurrentGame() && rtCurrentGame()->override && rtCurrentGame()->override(name))
        return rtCurrentGame()->override(name);
    return frameScaleWrapper(name);
}

// glXSwapBuffers, looked up by a linked-in SDL (with dlsym, or with the
// glXGetProcAddress it got that way), gets the frame hook (light gun border,
// window scaling), and the GL entry points the window scaling redirects get
// its wrappers (see frameScale.h).
static void *(*glxGetProcAddressReal)(const char *);

static void *glxGetProcAddress(const char *name)
{
    void *real;
    if (name && !strcmp(name, "glXSwapBuffers"))
        return (void *)rtGlxSwapBuffers;
    real = glxGetProcAddressReal ? glxGetProcAddressReal(name) : NULL;
    return real && rtOverride(name) ? rtOverride(name) : real;
}

static void *gameDlsym(void *handle, const char *name)
{
    if (name && !strcmp(name, "glXSwapBuffers"))
        return (void *)rtGlxSwapBuffers;
    if (name && rtOverride(name) && dlsym(handle, name))
        return rtOverride(name);
    if (name && (!strcmp(name, "glXGetProcAddressARB") || !strcmp(name, "glXGetProcAddress")))
    {
        void *real = dlsym(handle, name);
        if (!real)
            return NULL;
        glxGetProcAddressReal = real;
        return (void *)glxGetProcAddress;
    }
    return dlsym(handle, name);
}

// The executable's own glXGetProcAddress(ARB) imports, for the games asking
// for it (exeGlxGetProcAddress): games linking SDL 1.2 dynamically (Pink
// Panther Jewel Heist) get glBindFramebuffer and the like through them,
// which the window scaling must see. Not for the others: Big Buck World
// crashed in its attract mode with them wrapped.
static void *exeGlxGetProcAddress(const char *name)
{
    void *real;
    if (strcmp(name, "glXGetProcAddressARB") && strcmp(name, "glXGetProcAddress"))
        return NULL;
    if (!rtCurrentGame() || !rtCurrentGame()->exeGlxGetProcAddress)
        return NULL;
    if (!(real = dlsym(RTLD_NEXT, name)))
        return NULL;
    glxGetProcAddressReal = real;
    return (void *)glxGetProcAddress;
}

// Loader-wrapped imports the Raw Thrills games get a hook of their own for.
static void *rtWrapper(const char *name)
{
    if (!strcmp(name, "glutSwapBuffers"))
        return (void *)rtGlutSwapBuffers;
    return NULL;
}

// ld.so bound the imports of the executable and of the libraries before the
// loader ran: send those the loader wraps to the real library too. Libraries
// matter for games that link Xlib/GLX users directly (freeglut): they would
// get the Lindbergh window instead of theirs. The executable's imports are
// all rebound; a library's only when they would reach a loader wrapper.
static void rebindImports(void **preloaded, size_t npreloaded)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);

    for (struct link_map *obj = exe; obj; obj = obj->l_next)
    {
        // PLT slots, and GOT entries of functions called without the PLT
        // (-fno-plt builds).
        const Elf32_Rel *rels[2] = {NULL, NULL};
        size_t relSizes[2] = {0, 0};
        const Elf32_Sym *symtab = NULL;
        const char *strtab = NULL;

        if (obj != exe && (!obj->l_name[0] || isPreloaded(obj, preloaded, npreloaded)))
            continue;
        for (const Elf32_Dyn *dyn = obj->l_ld; dyn->d_tag != DT_NULL; dyn++)
        {
            if (dyn->d_tag == DT_JMPREL)
                rels[0] = (const Elf32_Rel *)dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_PLTRELSZ)
                relSizes[0] = dyn->d_un.d_val;
            else if (dyn->d_tag == DT_REL)
                rels[1] = (const Elf32_Rel *)dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_RELSZ)
                relSizes[1] = dyn->d_un.d_val;
            else if (dyn->d_tag == DT_SYMTAB)
                symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_STRTAB)
                strtab = (const char *)dyn->d_un.d_ptr;
        }
        for (int t = 0; t < 2 && symtab && strtab; t++)
        {
            for (size_t i = 0; rels[t] && i < relSizes[t] / sizeof(Elf32_Rel); i++)
            {
                const Elf32_Rel *rel = &rels[t][i];
                const Elf32_Sym *sym = &symtab[ELF32_R_SYM(rel->r_info)];
                const char *name = strtab + sym->st_name;
                uintptr_t slot = obj->l_addr + rel->r_offset;
                int type = ELF32_R_TYPE(rel->r_info);
                void *next;

                if (!(type == R_386_JMP_SLOT || (type == R_386_GLOB_DAT && ELF32_ST_TYPE(sym->st_info) != STT_OBJECT)))
                    continue;
                // The executable's own overrides (rtOverride), its dlsym (a
                // linked-in SDL looks up the X and GLX functions it uses: Big
                // Buck HD Wild) and its glXGetProcAddress.
                next = NULL;
                if (obj == exe && !(next = !strcmp(name, "dlsym") ? (void *)gameDlsym : exeGlxGetProcAddress(name)))
                    next = rtOverride(name);
                if (!next &&
                    (!bypassPreloaded(name) || (obj != exe && !wrapped(name, preloaded, npreloaded)) ||
                     (!(next = rtWrapper(name)) && !(next = dlsym(RTLD_NEXT, name)))))
                    continue;
                // The GOT is read-only in libraries linked with full RELRO.
                if (obj != exe)
                    mprotect((void *)(slot & ~(uintptr_t)0xfff), 0x1000, PROT_READ | PROT_WRITE);
                *(uint32_t *)slot = (uint32_t)(uintptr_t)next;
            }
        }
    }
}

static void *resolveImport(const char *name, const char *version, void **preloaded, size_t npreloaded)
{
    if (!strcmp(name, "dlsym"))
        return (void *)gameDlsym;
    if (rtOverride(name))
        return rtOverride(name);
    void *def = dlsym(RTLD_DEFAULT, name);
    for (size_t i = 0; def && i < npreloaded; i++)
    {
        if (dlsym(preloaded[i], name) != def)
            continue;
        if (!bypassPreloaded(name))
            return def;
        void *next = rtWrapper(name);
        if (!next)
            next = dlsym(RTLD_NEXT, name);
        if (next)
            return next;
    }

    void *p = NULL;
    if (version && version[0])
        p = dlvsym(RTLD_DEFAULT, name, version);
    return p ? p : lookupCompat(name);
}

int rtFixImports(const RtGame *game)
{
    void *preloaded[MAX_PRELOADED];
    size_t npreloaded = getPreloaded(preloaded, MAX_PRELOADED);
    int missing = 0;

    // KEEP_ASPECT_RATIO is for the games made for a 4:3 monitor (frameAspect)
    // and those turned upright (frameTurn): the others (Jurassic Park's fixed
    // frame) fill the window.
    frameScaleInit((void *(*)(const char *))dlsym(RTLD_DEFAULT, "glXGetProcAddressARB"),
                   game->frameAspect[0] || game->frameTurn ? getConfig()->keepAspectRatio : 0);
    for (const char *const *lib = game->extraLibs; lib && *lib; lib++)
        if (!dlopen(*lib, RTLD_NOW | RTLD_GLOBAL))
            log_warn("Raw Thrills: %s: %s", *lib, dlerror());
    rebindImports(preloaded, npreloaded);

    uint32_t *envelopeGot = (uint32_t *)(uintptr_t)game->envelopeGot;
    for (size_t i = 0; i < game->envelopeImportCount; i++)
    {
        const RtEnvelopeImport *imp = &game->envelopeImports[i];
        void *p = resolveImport(imp->name, NULL, preloaded, npreloaded);
        if (!p)
        {
            log_warn("Raw Thrills: envelope import %s not found", imp->name);
            missing++;
            continue;
        }
        envelopeGot[imp->slot] = (uint32_t)(uintptr_t)p;
    }
    if (game->envelopeSelfSlot >= 0)
        envelopeGot[game->envelopeSelfSlot] = game->envelopeSelfTarget;

    for (size_t i = 0; i < game->gameImportCount; i++)
    {
        const RtGameImport *imp = &game->gameImports[i];
        void *p = resolveImport(imp->name, imp->version, preloaded, npreloaded);
        if (!p)
        {
            // Weak hooks such as __gmon_start__ are legitimately absent.
            if (strcmp(imp->name, "__gmon_start__") != 0)
            {
                log_warn("Raw Thrills: game import %s@%s not found", imp->name, imp->version);
                missing++;
            }
            continue;
        }
        *(uint32_t *)(uintptr_t)imp->slot = (uint32_t)(uintptr_t)p;
    }

    log_info("Raw Thrills: import tables rebuilt (%d missing)", missing);
    return missing;
}

// Hooks simply overwrite a function entry with a jmp to the replacement.
// Game function by name: listed in the game's descriptor (stripped games,
// or exports that are only envelope stubs), else exported.
static void *lookup(const char *name)
{
    const RtGame *game = rtCurrentGame();
    for (const RtSymbol *s = game ? game->symbols : NULL; s && s->name; s++)
        if (!strcmp(s->name, name))
            return (void *)(uintptr_t)s->address;
    return dlsym(RTLD_DEFAULT, name);
}

void *rtSymbol(const char *name)
{
    return lookup(name);
}

void rtDetourAddress(uint32_t address, void *replacement)
{
    uint8_t *target = (uint8_t *)(uintptr_t)address;

    // Games that are not dumps map their code read-only.
    uintptr_t page = address & ~(uintptr_t)0xfff;
    if (mprotect((void *)page, (address + 5 - page + 0xfff) & ~(uintptr_t)0xfff, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        log_warn("Raw Thrills: cannot make %#x writable", address);
    target[0] = 0xe9;
    *(int32_t *)(target + 1) = (int32_t)((uint8_t *)replacement - (target + 5));
}

int rtDetour(const char *name, void *replacement)
{
    void *target = lookup(name);
    if (!target)
    {
        log_warn("Raw Thrills: hook target %s not found", name);
        return -1;
    }
    rtDetourAddress((uint32_t)(uintptr_t)target, replacement);
    return 0;
}

// Copy the first prologueLength bytes of a function (whole, position
// independent instructions) and jump back after them, so the original can
// still be called once its entry is detoured.
void *rtTrampoline(const char *name, size_t prologueLength)
{
    uint8_t *target = lookup(name);
    if (!target)
        return NULL;
    uint8_t *tramp = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED)
        return NULL;
    memcpy(tramp, target, prologueLength);
    tramp[prologueLength] = 0xe9;
    *(int32_t *)(tramp + prologueLength + 1) =
        (int32_t)((target + prologueLength) - (tramp + prologueLength + 5));
    return tramp;
}

// A dump taken once the game was running also holds the runtime state of
// its .bss (e.g. an initialized SDL with pointers into a heap that no longer
// exists). This runs before the game's constructors, as the kernel would
// hand it over: zero it, keeping the objects ld.so copied in.
void rtClearBss(const RtGame *game)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    const Elf32_Dyn *dyn;
    const Elf32_Rel *rel = NULL;
    const Elf32_Sym *symtab = NULL;
    size_t relSize = 0;
    struct
    {
        uint32_t address;
        uint32_t size;
        uint8_t data[64];
    } saved[16];
    int nsaved = 0;

    if (!game->bssStart || !exe)
        return;
    for (dyn = exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_REL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_RELSZ)
            relSize = dyn->d_un.d_val;
        else if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
    }
    for (size_t i = 0; rel && symtab && i < relSize / sizeof(*rel); i++)
    {
        const Elf32_Sym *sym = &symtab[ELF32_R_SYM(rel[i].r_info)];
        if (ELF32_R_TYPE(rel[i].r_info) != R_386_COPY || rel[i].r_offset < game->bssStart ||
            rel[i].r_offset >= game->bssEnd)
            continue;
        if (nsaved == (int)(sizeof(saved) / sizeof(saved[0])) || sym->st_size > sizeof(saved[0].data))
        {
            log_error("Raw Thrills: cannot keep the copy relocation at %#x, .bss left as dumped", rel[i].r_offset);
            return;
        }
        saved[nsaved].address = rel[i].r_offset;
        saved[nsaved].size = sym->st_size;
        memcpy(saved[nsaved].data, (void *)(uintptr_t)rel[i].r_offset, sym->st_size);
        nsaved++;
    }
    memset((void *)(uintptr_t)game->bssStart, 0, game->bssEnd - game->bssStart);
    for (int i = 0; i < nsaved; i++)
        memcpy((void *)(uintptr_t)saved[i].address, saved[i].data, saved[i].size);
}

// SDL 2 pins a window it was not asked to make resizable at its size, with
// equal minimum and maximum size hints: the games' windows are made
// resizable, so that the frame scales with them (see frameScale.h).
#define SDL_WINDOW_RESIZABLE 0x20
static void *(*sdlCreateWindowReal)(const char *, int, int, int, int, uint32_t);

static void *sdlCreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags)
{
    return sdlCreateWindowReal(title, x, y, w, h, flags | SDL_WINDOW_RESIZABLE);
}

// And its swap gets the frame hook (window scaling, gun border): the
// system's SDL looks glXSwapBuffers up itself.
static void (*sdlGlSwapWindowReal)(void *);

static void sdlGlSwapWindow(void *window)
{
    rtSdlGlSwapWindow(sdlGlSwapWindowReal, window);
}

// Number of dynamic symbols, from DT_HASH (nchain) or DT_GNU_HASH.
static uint32_t dynSymCount(const Elf32_Dyn *dyn)
{
    for (; dyn->d_tag != DT_NULL; dyn++)
        if (dyn->d_tag == DT_HASH)
            return ((const uint32_t *)dyn->d_un.d_ptr)[1];
    return 0;
}

uint32_t rtSymbolSize(const char *name)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    const Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;

    if (!exe)
        return 0;
    for (const Elf32_Dyn *dyn = exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
    }
    for (uint32_t i = 1; symtab && strtab && i < dynSymCount(exe->l_ld); i++)
        if (symtab[i].st_shndx != SHN_UNDEF && !strcmp(strtab + symtab[i].st_name, name))
            return symtab[i].st_size;
    return 0;
}

int rtReplaceLib(const RtGame *game)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    const Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    size_t prefixLen;
    void *lib;
    int replaced = 0, missing = 0;

    if (!game->replacedLib)
        return 0;
    if (!(lib = dlopen(game->replacedLib, RTLD_NOW | RTLD_LOCAL)))
    {
        log_error("Raw Thrills: cannot load %s: %s", game->replacedLib, dlerror());
        return -1;
    }
    for (const Elf32_Dyn *dyn = exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
    }
    prefixLen = strlen(game->replacedLibPrefix);
    for (uint32_t i = 1; symtab && strtab && i < dynSymCount(exe->l_ld); i++)
    {
        const Elf32_Sym *sym = &symtab[i];
        const char *name = strtab + sym->st_name;
        void *target;

        if (sym->st_shndx == SHN_UNDEF || ELF32_ST_TYPE(sym->st_info) != STT_FUNC ||
            strncmp(name, game->replacedLibPrefix, prefixLen))
            continue;
        if (!(target = dlsym(lib, name)))
        {
            missing++;
            continue;
        }
        if (!strcmp(name, "SDL_CreateWindow"))
        {
            sdlCreateWindowReal = target;
            target = (void *)sdlCreateWindow;
        }
        else if (!strcmp(name, "SDL_GL_SwapWindow"))
        {
            sdlGlSwapWindowReal = target;
            target = (void *)sdlGlSwapWindow;
        }
        rtDetourAddress(sym->st_value, target);
        replaced++;
    }
    log_info("Raw Thrills: %d %s functions sent to %s (%d not found)", replaced, game->replacedLibPrefix,
             game->replacedLib, missing);
    return 0;
}
