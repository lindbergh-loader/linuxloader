// linuxloader_freebsd: runs a 32-bit FreeBSD game (Temple Run, FreeBSD 9.1)
// on Linux.
//
// The game's ELF is mapped as FreeBSD's rtld would, at its addresses, and its
// imports bound to the host's libraries: the same ones for its graphics,
// sound and C++ (SDL 1.2, GL, OpenAL, libpng, zlib, libstdc++), FreeBSD's
// libc and libm to glibc's, through the shims of fbsdLibc.c where FreeBSD's
// ABI differs. Its copies of the C++ library's objects (R_386_COPY) are the
// library's own: the game's references to them are pointed there (old
// std::string tells its empty representation by address). Its C++
// exception tables are registered with the host's unwinder; its _init (the
// C++ constructors) runs, then its main.
//
// Run with the linuxloader preloaded, the loader knows the game by its ELF
// (LINUXLOADER_FREEBSD_ELF) and is told when the game is mapped
// (linuxloaderFreebsdMapped), for the hooks of the game's own functions.
//
//   linuxloader_freebsd <game ELF> [arguments...]

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>

#include "fbsdLibc.h"
#include "fbsdStdcxx.h"

#define PAGE 4096u

typedef struct
{
    const char *freebsd, *host, *fallback;
} LibraryName;

// FreeBSD 9.1's library names, the host's.
static const LibraryName libraries[] = {
    {"libc.so.7", NULL},
    {"libm.so.5", "libm.so.6"},
    {"libz.so.6", "libz.so.1"},
    {"libpng15.so.15", "libpng16.so.16"},
    {"libSDL-1.2.so.11", "libSDL-1.2.so.0"},
    {"libstdc++.so.6", "libstdc++.so.6"},
    {"libgcc_s.so.1", "libgcc_s.so.1"},
    {"libGL.so.1", "libGL.so.1"},
    {"libGLU.so.1", "libGLU.so.1"},
    {"libopenal.so.1", "libopenal.so.1", "libopenal.so.0"}, // (the loader's libs/linux_x86)
};

static uint8_t *image;
static size_t imageSize;

static void fail(const char *what, const char *detail)
{
    fprintf(stderr, "linuxloader_freebsd: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    exit(1);
}

static void loadLibrary(const char *name)
{
    for (size_t i = 0; i < sizeof(libraries) / sizeof(libraries[0]); i++)
        if (!strcmp(name, libraries[i].freebsd))
        {
            if (libraries[i].host && !dlopen(libraries[i].host, RTLD_NOW | RTLD_GLOBAL) &&
                (!libraries[i].fallback || !dlopen(libraries[i].fallback, RTLD_NOW | RTLD_GLOBAL)))
                fail("cannot load", dlerror());
            return;
        }
    if (!dlopen(name, RTLD_NOW | RTLD_GLOBAL))
        fail("cannot load", dlerror());
}

typedef struct
{
    Elf32_Ehdr *ehdr;
    Elf32_Phdr *phdr;
    Elf32_Dyn *dynamic;
    const char *strtab;
    Elf32_Sym *symtab;
    Elf32_Rel *rel, *jmprel;
    size_t relSize, jmprelSize;
    uint32_t init, low, high;
    uint32_t ehFrame;
} Game;

static void mapGame(const char *path, Game *g)
{
    int fd = open(path, O_RDONLY);
    off_t size;

    if (fd < 0)
        fail("cannot open", path);
    size = lseek(fd, 0, SEEK_END);
    imageSize = (size_t)size;
    image = malloc(imageSize);
    if (!image || pread(fd, image, imageSize, 0) != (ssize_t)imageSize)
        fail("cannot read", path);
    close(fd);

    g->ehdr = (Elf32_Ehdr *)image;
    if (memcmp(g->ehdr->e_ident, ELFMAG, SELFMAG) || g->ehdr->e_machine != EM_386 || g->ehdr->e_type != ET_EXEC)
        fail("not a 32-bit x86 executable", path);
    g->phdr = (Elf32_Phdr *)(image + g->ehdr->e_phoff);
    g->low = UINT32_MAX;
    g->high = 0;
    for (int i = 0; i < g->ehdr->e_phnum; i++)
    {
        Elf32_Phdr *p = &g->phdr[i];
        if (p->p_type != PT_LOAD)
            continue;
        if (p->p_vaddr < g->low)
            g->low = p->p_vaddr & ~(PAGE - 1);
        if (p->p_vaddr + p->p_memsz > g->high)
            g->high = (p->p_vaddr + p->p_memsz + PAGE - 1) & ~(PAGE - 1);
    }
    // One read-write-execute mapping for it all: the game's references are
    // rewritten in its code below.
    if (mmap((void *)(uintptr_t)g->low, g->high - g->low, PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void *)(uintptr_t)g->low)
        fail("cannot map the game at its address", NULL);
    for (int i = 0; i < g->ehdr->e_phnum; i++)
    {
        Elf32_Phdr *p = &g->phdr[i];
        if (p->p_type == PT_LOAD)
            memcpy((void *)(uintptr_t)p->p_vaddr, image + p->p_offset, p->p_filesz);
        else if (p->p_type == PT_DYNAMIC)
            g->dynamic = (Elf32_Dyn *)(uintptr_t)p->p_vaddr;
    }
    if (!g->dynamic)
        fail("no dynamic section", path);

    // The exception tables: the .eh_frame section (by name).
    {
        Elf32_Shdr *sh = (Elf32_Shdr *)(image + g->ehdr->e_shoff);
        const char *names = (const char *)image + sh[g->ehdr->e_shstrndx].sh_offset;
        for (int i = 0; g->ehdr->e_shoff && i < g->ehdr->e_shnum; i++)
            if (!strcmp(names + sh[i].sh_name, ".eh_frame"))
                g->ehFrame = sh[i].sh_addr;
    }

    for (Elf32_Dyn *d = g->dynamic; d->d_tag != DT_NULL; d++)
        switch (d->d_tag)
        {
        case DT_STRTAB:
            g->strtab = (const char *)(uintptr_t)d->d_un.d_ptr;
            break;
        case DT_SYMTAB:
            g->symtab = (Elf32_Sym *)(uintptr_t)d->d_un.d_ptr;
            break;
        case DT_REL:
            g->rel = (Elf32_Rel *)(uintptr_t)d->d_un.d_ptr;
            break;
        case DT_RELSZ:
            g->relSize = d->d_un.d_val;
            break;
        case DT_JMPREL:
            g->jmprel = (Elf32_Rel *)(uintptr_t)d->d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            g->jmprelSize = d->d_un.d_val;
            break;
        case DT_INIT:
            g->init = d->d_un.d_ptr;
            break;
        }
    for (Elf32_Dyn *d = g->dynamic; d->d_tag != DT_NULL; d++)
        if (d->d_tag == DT_NEEDED)
            loadLibrary(g->strtab + d->d_un.d_val);
}

static void *resolve(const char *name)
{
    void *shim = fbsdShim(name);

    if (!shim)
        shim = fbsdStdcxxShim(name);
    if (shim)
        return shim;
    return dlsym(RTLD_DEFAULT, name);
}

// FBSD_TRACE=1 (debugging): each import through a stub keeping its name in
// a ring of the last ones called, shown on a crash.
#define TRACE_RING 32
static const char *traceNames[4096];
static volatile uint32_t traceRing[TRACE_RING], traceNext;
static uint8_t *stubs;
static int stubCount;

__attribute__((used)) static void traceCall(uint32_t id)
{
    traceRing[traceNext++ % TRACE_RING] = id;
}

static void *traceStub(const char *name, void *target)
{
    // pushad; push id; call traceCall; add esp,4; popad; jmp target
    uint8_t *p;

    if (!stubs)
        stubs = mmap(NULL, 4096 * 32, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stubCount >= 4096 || !target)
        return target;
    traceNames[stubCount] = name;
    p = stubs + stubCount * 32;
    p[0] = 0x60;
    p[1] = 0x68;
    memcpy(p + 2, &(uint32_t){(uint32_t)stubCount}, 4);
    p[6] = 0xe8;
    memcpy(p + 7, &(int32_t){(int32_t)((uintptr_t)traceCall - (uintptr_t)(p + 11))}, 4);
    p[11] = 0x83;
    p[12] = 0xc4;
    p[13] = 0x04;
    p[14] = 0x61;
    p[15] = 0xe9;
    memcpy(p + 16, &(int32_t){(int32_t)((uintptr_t)target - (uintptr_t)(p + 20))}, 4);
    stubCount++;
    return p;
}

static void traceCrash(int signal)
{
    fprintf(stderr, "linuxloader_freebsd: signal %d; the last imports called:\n", signal);
    for (uint32_t i = traceNext > TRACE_RING ? traceNext - TRACE_RING : 0; i < traceNext; i++)
        fprintf(stderr, "  %s\n", traceNames[traceRing[i % TRACE_RING]]);
    _exit(128 + signal);
}

// The game's references to a copied object (in its code's immediates, its
// data's pointers): to the library's.
static void retarget(Game *g, uint32_t from, uint32_t size, uint32_t to)
{
    for (uint32_t a = g->low; a + 4 <= g->high; a++)
    {
        uint32_t v;
        memcpy(&v, (void *)(uintptr_t)a, 4);
        if (v >= from && v < from + size)
        {
            v = to + (v - from);
            memcpy((void *)(uintptr_t)a, &v, 4);
            a += 3;
        }
    }
}

// The copied objects (name, where in the game).
#define MAX_COPIES 64
static const char *copyName[MAX_COPIES];
static uint32_t copyWhere[MAX_COPIES];
static int copies;

// The file streams' VTTs: their complete vtables are the game's copies.
static void fixStreams(void)
{
    for (int i = 0; i < copies; i++)
    {
        if (strncmp(copyName[i], "_ZTV", 4) != 0)
            continue;
        for (int j = 0; j < copies; j++)
            if (!strncmp(copyName[j], "_ZTT", 4) && !strcmp(copyName[j] + 4, copyName[i] + 4))
                fbsdStdcxxFixVtt(copyWhere[i], copyWhere[j], copyName[i]);
    }
}

static void relocate(Game *g, Elf32_Rel *rel, size_t size)
{
    for (size_t i = 0; rel && i < size / sizeof(*rel); i++)
    {
        uint32_t type = ELF32_R_TYPE(rel[i].r_info);
        Elf32_Sym *sym = &g->symtab[ELF32_R_SYM(rel[i].r_info)];
        const char *name = g->strtab + sym->st_name;
        uint32_t *where = (uint32_t *)(uintptr_t)rel[i].r_offset;
        void *target;

        if (type == R_386_NONE)
            continue;
        if (type == R_386_RELATIVE)
            continue; // an executable at its own addresses
        target = ELF32_R_SYM(rel[i].r_info) ? resolve(name) : NULL;
        if (!target && ELF32_R_SYM(rel[i].r_info) && ELF32_ST_BIND(sym->st_info) != STB_WEAK)
            fprintf(stderr, "linuxloader_freebsd: %s unresolved\n", name);
        switch (type)
        {
        case R_386_JMP_SLOT:
            if (getenv("FBSD_TRACE"))
                target = traceStub(name, target);
            *where = (uint32_t)(uintptr_t)target;
            break;
        case R_386_GLOB_DAT:
            *where = (uint32_t)(uintptr_t)target;
            break;
        case R_386_32:
            *where += (uint32_t)(uintptr_t)target;
            break;
        case R_386_PC32:
            *where += (uint32_t)(uintptr_t)target - rel[i].r_offset;
            break;
        case R_386_COPY:
            if (!target)
                break;
            if (copies < MAX_COPIES)
            {
                copyName[copies] = name;
                copyWhere[copies++] = rel[i].r_offset;
            }
            if (fbsdStdcxxCopy(name, where, target, sym->st_size))
                break;
            memcpy(where, target, sym->st_size);
            retarget(g, rel[i].r_offset, sym->st_size, (uint32_t)(uintptr_t)target);
            break;
        default:
            fprintf(stderr, "linuxloader_freebsd: relocation %u of %s not handled\n", type, name);
        }
    }
}

// The game's own environ and __progname (FreeBSD's crt1 defines them in the
// executable), as the host's libc has them.
static void setGlobals(Game *g, const char *progname)
{
    extern char **environ;
    size_t count = 0;

    for (Elf32_Dyn *d = g->dynamic; d->d_tag != DT_NULL; d++)
        if (d->d_tag == DT_HASH)
            count = ((uint32_t *)(uintptr_t)d->d_un.d_ptr)[1];
    for (size_t i = 0; i < count; i++)
    {
        Elf32_Sym *s = &g->symtab[i];
        const char *name = g->strtab + s->st_name;
        if (s->st_shndx == SHN_UNDEF || !s->st_value)
            continue;
        if (!strcmp(name, "environ"))
            *(char ***)(uintptr_t)s->st_value = environ;
        else if (!strcmp(name, "__progname"))
            *(const char **)(uintptr_t)s->st_value = progname;
    }
}

// A function of the game's symbol table (.symtab: main is not exported).
static uint32_t gameSymbol(const char *wanted)
{
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)image;
    Elf32_Shdr *sh = (Elf32_Shdr *)(image + ehdr->e_shoff);

    for (int i = 0; ehdr->e_shoff && i < ehdr->e_shnum; i++)
    {
        Elf32_Sym *sym;
        const char *names;
        if (sh[i].sh_type != SHT_SYMTAB)
            continue;
        sym = (Elf32_Sym *)(image + sh[i].sh_offset);
        names = (const char *)image + sh[sh[i].sh_link].sh_offset;
        for (size_t j = 0; j < sh[i].sh_size / sizeof(*sym); j++)
            if (sym[j].st_name && !strcmp(names + sym[j].st_name, wanted))
                return sym[j].st_value;
    }
    return 0;
}

int main(int argc, char **argv)
{
    Game g = {0};
    void (*mapped)(uint32_t, uint32_t);
    void (*registerFrame)(void *);
    void (*init)(void);
    int (*gameMain)(int, char **, char **);
    extern char **environ;
    const char *slash;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <FreeBSD game ELF> [arguments...]\n", argv[0]);
        return 2;
    }
    if (getenv("FBSD_TRACE"))
        signal(SIGSEGV, traceCrash);
    mapGame(argv[1], &g);
    relocate(&g, g.rel, g.relSize);
    relocate(&g, g.jmprel, g.jmprelSize);
    fixStreams();
    slash = strrchr(argv[1], '/');
    setGlobals(&g, slash ? slash + 1 : argv[1]);

    *(void **)&registerFrame = dlsym(RTLD_DEFAULT, "__register_frame");
    if (g.ehFrame && registerFrame)
        registerFrame((void *)(uintptr_t)g.ehFrame);

    // The loader's hooks on the game's own functions.
    *(void **)&mapped = dlsym(RTLD_DEFAULT, "linuxloaderFreebsdMapped");
    if (mapped)
        mapped(g.low, g.high);

    if (g.init)
    {
        *(void **)&init = (void *)(uintptr_t)g.init;
        init();
    }
    *(void **)&gameMain = (void *)(uintptr_t)gameSymbol("main");
    if (!gameMain)
        fail("no main", NULL);
    exit(gameMain(argc - 1, argv + 1, environ));
}
