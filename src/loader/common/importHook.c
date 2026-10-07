// Hooks on the game executable's own imports (see importHook.h).

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "importHook.h"
#include "../log/log.h"

static void *hookImport(struct link_map *exe, const char *name, void *replacement, int writable);

void *hookExecutableImport(const char *name, void *replacement)
{
    return hookImport(dlopen(NULL, RTLD_NOW), name, replacement, 0);
}

void *hookLibraryImport(void *handle, const char *name, void *replacement)
{
    struct link_map *lib = (struct link_map *)handle;
    void *real = hookImport(lib, name, replacement, 1);
    const Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    const Elf32_Rel *rel = NULL;
    size_t relSize = 0;

    // A library built without -fPIC (Crazy Shoot's plugin: TEXTREL) calls
    // its imports directly, each call's displacement relocated (R_386_PC32)
    // in its code: those are pointed at replacement too.
    for (const Elf32_Dyn *dyn = (const Elf32_Dyn *)lib->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_REL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_RELSZ)
            relSize = dyn->d_un.d_val;
    }
    for (size_t i = 0; symtab && strtab && rel && i < relSize / sizeof(*rel); i++)
    {
        unsigned type = ELF32_R_TYPE(rel[i].r_info);
        if ((type != R_386_PC32 && type != R_386_32) ||
            strcmp(strtab + symtab[ELF32_R_SYM(rel[i].r_info)].st_name, name) != 0)
            continue;
        uintptr_t at = lib->l_addr + rel[i].r_offset, page = at & ~(uintptr_t)0xfff;
        mprotect((void *)page, at + 4 - page, PROT_READ | PROT_WRITE | PROT_EXEC);
        *(uint32_t *)at = type == R_386_PC32 ? (uint32_t)((uintptr_t)replacement - (at + 4)) : (uint32_t)(uintptr_t)replacement;
        if (!real)
            real = dlsym(RTLD_NEXT, name);
    }
    return real;
}

static void *hookImport(struct link_map *exe, const char *name, void *replacement, int writable)
{
    const Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    const Elf32_Rel *rel = NULL;
    size_t relSize = 0;

    if (!exe)
        return NULL;
    for (const Elf32_Dyn *dyn = (const Elf32_Dyn *)exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_JMPREL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_PLTRELSZ)
            relSize = dyn->d_un.d_val;
    }
    for (size_t i = 0; symtab && strtab && rel && i < relSize / sizeof(*rel); i++)
    {
        if (ELF32_R_TYPE(rel[i].r_info) != R_386_JMP_SLOT ||
            strcmp(strtab + symtab[ELF32_R_SYM(rel[i].r_info)].st_name, name) != 0)
            continue;
        void **slot = (void **)(uintptr_t)(rel[i].r_offset + (writable ? exe->l_addr : 0));
        if (writable)
        {
            // (a library's slots: relative to its base, maybe made read-only)
            uintptr_t page = (uintptr_t)slot & ~(uintptr_t)0xfff;
            mprotect((void *)page, 0x1000, PROT_READ | PROT_WRITE);
        }
        *slot = replacement;
        return dlsym(RTLD_NEXT, name);
    }
    return NULL;
}

// The loader's own handle: its symbols, then its dependencies'.
static void *loaderHandle(void)
{
    static void *self;
    Dl_info info;

    if (!self && dladdr((void *)loaderHandle, &info))
        self = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    return self;
}

void *importTarget(const char *name)
{
    void *self = loaderHandle();

    return self ? dlsym(self, name) : NULL;
}

// The version the executable asks for its symbol index sym (DT_VERSYM,
// DT_VERNEED), NULL if none.
static const char *importVersion(const Elf32_Dyn *dynamic, const char *strtab, unsigned sym)
{
    const Elf32_Half *versym = NULL;
    const Elf32_Verneed *verneed = NULL;

    for (const Elf32_Dyn *dyn = dynamic; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_VERSYM)
            versym = (const Elf32_Half *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_VERNEED)
            verneed = (const Elf32_Verneed *)dyn->d_un.d_ptr;
    }
    if (!versym || !verneed || (versym[sym] & 0x7fff) < 2)
        return NULL;
    for (const Elf32_Verneed *vn = verneed;; vn = (const Elf32_Verneed *)((const char *)vn + vn->vn_next))
    {
        const Elf32_Vernaux *aux = (const Elf32_Vernaux *)((const char *)vn + vn->vn_aux);
        for (unsigned i = 0; i < vn->vn_cnt; i++, aux = (const Elf32_Vernaux *)((const char *)aux + aux->vna_next))
            if (aux->vna_other == (versym[sym] & 0x7fff))
                return strtab + aux->vna_name;
        if (!vn->vn_next)
            return NULL;
    }
}

// The stub: push ebp; mov ebp, esp; push esi; push edi; sub esp, 96;
// and esp, -16; lea esi, [ebp + 8]; mov edi, esp; mov ecx, 24; cld;
// rep movsd; call [target]; lea esp, [ebp - 8]; pop edi; pop esi; pop ebp;
// ret. The return registers (eax, edx, st0) are the import's.
static const uint8_t alignStub[] = {0x55, 0x89, 0xe5, 0x56, 0x57, 0x83, 0xec, 0x60, 0x83, 0xe4, 0xf0, 0x8d, 0x75,
                                    0x08, 0x89, 0xe7, 0xb9, 0x18, 0x00, 0x00, 0x00, 0xfc, 0xf3, 0xa5, 0xff, 0x15,
                                    0x00, 0x00, 0x00, 0x00, 0x8d, 0x65, 0xf8, 0x5f, 0x5e, 0x5d, 0xc3};
#define ALIGN_STUB_SIZE 48 // a stub and its target

static uintptr_t codeEnd;

// The first object's (the executable's) executable segment.
static int executableCode(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    for (int i = 0; i < info->dlpi_phnum; i++)
        if (info->dlpi_phdr[i].p_type == PT_LOAD && (info->dlpi_phdr[i].p_flags & PF_X))
        {
            *(uintptr_t *)data = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
            codeEnd = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
            break;
        }
    return 1;
}

void alignExecutableImports(void)
{
    static const char *const untouched[] = {"_setjmp", "setjmp", "__sigsetjmp", "sigsetjmp", "longjmp",
                                            "siglongjmp", "_longjmp", "__libc_start_main", "vfork", NULL};
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    const Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    const Elf32_Rel *rel = NULL;
    size_t relSize = 0, count, done = 0;
    uintptr_t pltStart = 0, pltEnd = 0;
    uint8_t *stubs;
    void *self;

    static int aligned;

    if (!exe || aligned++)
        return;
    for (const Elf32_Dyn *dyn = (const Elf32_Dyn *)exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_JMPREL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_PLTRELSZ)
            relSize = dyn->d_un.d_val;
    }
    if (!symtab || !strtab || !rel)
        return;
    count = relSize / sizeof(*rel);
    // The executable's own code (a slot still pointing there is not bound
    // yet: its lazy resolution, in the PLT, would put the import back).
    dl_iterate_phdr(executableCode, &pltStart);
    pltEnd = pltStart ? codeEnd : 0;
    if (!(self = loaderHandle()))
        return;
    // Low in memory, where the game's own code is: callers' return
    // addresses there are the game's for the loader's checks.
    stubs = mmap((void *)0x0f000000, count * ALIGN_STUB_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stubs == MAP_FAILED)
    {
        log_error("alignExecutableImports: no memory for the stubs");
        return;
    }
    for (size_t i = 0; i < count; i++)
    {
        unsigned sym = ELF32_R_SYM(rel[i].r_info);
        const char *name = strtab + symtab[sym].st_name;
        void **slot = (void **)(uintptr_t)rel[i].r_offset;
        uint8_t *stub = stubs + i * ALIGN_STUB_SIZE;
        void **target = (void **)(stub + 40);
        // (an import the executable defines itself, which the dynamic
        // linker binds to its own definition: Intel's unwinder's
        // __register_frame_info, called through the PLT)
        int skip = ELF32_R_TYPE(rel[i].r_info) != R_386_JMP_SLOT || symtab[sym].st_shndx != SHN_UNDEF;

        for (const char *const *u = untouched; *u && !skip; u++)
            skip = !strcmp(name, *u);
        if (skip)
            continue;
        *target = *slot;
        if ((uintptr_t)*target >= pltStart && (uintptr_t)*target < pltEnd)
        {
            // Resolved as the dynamic linker would, past the executable: an
            // import whose address the game takes is defined there, at its
            // PLT entry (which jumps back here). The loader's own functions
            // first (its file interposers), then its dependencies'.
            const char *version = importVersion((const Elf32_Dyn *)exe->l_ld, strtab, sym);
            Dl_info where;
            *target = dlsym(self, name);
            // (a library's: the version asked for, libc's compatibility
            // ones among them)
            if (version && (!*target || !dladdr(*target, &where) || dlopen(where.dli_fname, RTLD_NOW | RTLD_NOLOAD) != self))
            {
                void *versioned = dlvsym(self, name, version);
                if (versioned)
                    *target = versioned;
            }
            if (!*target || ((uintptr_t)*target >= pltStart && (uintptr_t)*target < pltEnd))
                continue;
        }
        memcpy(stub, alignStub, sizeof(alignStub));
        *(void ***)(stub + 26) = target;
        *slot = stub;
        done++;
    }
    log_info("alignExecutableImports: %u imports through stack-aligning stubs", (unsigned)done);
}

static void (*dlopenObservers[4])(const char *, void *);

void dlopenObserve(void (*observer)(const char *file, void *handle))
{
    for (int i = 0; i < 4; i++)
        if (!dlopenObservers[i])
        {
            dlopenObservers[i] = observer;
            return;
        }
}

void dlopenObserved(const char *file, void *handle)
{
    for (int i = 0; i < 4 && dlopenObservers[i]; i++)
        dlopenObservers[i](file, handle);
}
