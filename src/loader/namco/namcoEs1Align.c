// Calls from a game built for a 4-byte-aligned stack (Dead Heat, built with
// Intel's compiler) into libraries built for the 16-byte alignment the i386
// ABI has required since: the libraries keep SSE/AVX values on their stack
// with aligned moves, which fault on the game's stack (std::istream::seekg
// in the game's first CSV read). Each of the game's imports goes through a
// stub that aligns the stack, copies the arguments over and calls the real
// function.

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "namcoEs1.h"
#include "../log/log.h"

// The stubs jump here with the real function's address pushed above the
// caller's return address. The first 64 words of arguments are copied to a
// 16-byte-aligned block for the call: as many as a variadic list may hold
// (Tux Racer's Tcl_VarEval builds commands of more than 16 strings, the
// NULL ending them lost when fewer were copied). A function may pop some of them
// itself (a struct returned through a hidden pointer: "ret $4"); as many
// are dropped on the way back. eax/edx (the return value) and the callee-
// saved registers are left as the function returns them. The CFI lets C++
// exceptions unwind through it.
__asm__(".text\n"
        ".type namcoEs1AlignedCall, @function\n"
        "namcoEs1AlignedCall:\n"
        "    .cfi_startproc\n"
        "    .cfi_def_cfa %esp, 8\n"
        "    .cfi_offset %eip, -4\n"
        "    push %ebp\n"
        "    .cfi_def_cfa_offset 12\n"
        "    .cfi_offset %ebp, -12\n"
        "    mov %esp, %ebp\n"
        "    .cfi_def_cfa_register %ebp\n"
        "    sub $256, %esp\n"
        "    and $-16, %esp\n"
        "    mov $64, %ecx\n"
        "1:  mov 8(%ebp,%ecx,4), %eax\n"
        "    mov %eax, -4(%esp,%ecx,4)\n"
        "    dec %ecx\n"
        "    jnz 1b\n"
        "    call *4(%ebp)\n"
        // ecx: the bytes the function popped
        "    lea -256(%ebp), %ecx\n"
        "    and $-16, %ecx\n"
        "    neg %ecx\n"
        "    add %esp, %ecx\n"
        "    mov %ebp, %esp\n"
        "    pop %ebp\n"
        "    .cfi_def_cfa %esp, 8\n"
        // the return address moved over the popped arguments, the stub's
        // slot and those dropped
        "    pushl 4(%esp)\n"
        "    popl 4(%esp,%ecx,1)\n"
        "    lea 4(%esp,%ecx,1), %esp\n"
        "    .cfi_def_cfa_offset 4\n"
        "    ret\n"
        "    .cfi_endproc\n"
        ".size namcoEs1AlignedCall, .-namcoEs1AlignedCall\n");

void namcoEs1AlignedCall(void);

// A stub calling target on an aligned stack: push $target; jmp namcoEs1AlignedCall.
static void *makeStub(uint32_t target)
{
    static uint8_t *page;
    static size_t used = 4096;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    uint8_t *stub;

    pthread_mutex_lock(&lock);
    if (used + 10 > 4096)
    {
        uint8_t *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
        {
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        page = p;
        used = 0;
    }
    stub = page + used;
    used += 10;
    pthread_mutex_unlock(&lock);
    stub[0] = 0x68;
    memcpy(stub + 1, &target, 4);
    stub[5] = 0xe9;
    int32_t rel32 = (int32_t)((uint8_t *)namcoEs1AlignedCall - (stub + 10));
    memcpy(stub + 6, &rel32, 4);
    return stub;
}

void *namcoEs1AlignedPointer(void *function)
{
    static struct
    {
        void *function, *stub;
    } cache[64];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    void *stub = NULL;

    if (!function)
        return NULL;
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < sizeof(cache) / sizeof(cache[0]) && !stub; i++)
        if (cache[i].function == function)
            stub = cache[i].stub;
        else if (!cache[i].function && (stub = makeStub((uint32_t)(uintptr_t)function)))
        {
            cache[i].function = function;
            cache[i].stub = stub;
        }
    pthread_mutex_unlock(&lock);
    return stub ? stub : function;
}

// Functions that work on their caller's frame: not through a stub.
static const char *const direct[] = {
    "setjmp", "_setjmp", "sigsetjmp", "__sigsetjmp", "vfork", "getcontext", "swapcontext", "makecontext", "clone", NULL,
};

// Objects done (a library loaded again keeps its stubs).
static struct link_map *alignedObjects[16];

static int aligned(struct link_map *obj)
{
    const size_t n = sizeof(alignedObjects) / sizeof(alignedObjects[0]);
    for (size_t i = 0; i < n; i++)
        if (alignedObjects[i] == obj)
            return 1;
    for (size_t i = 0; i < n; i++)
        if (!alignedObjects[i])
        {
            alignedObjects[i] = obj;
            break;
        }
    return 0;
}

// The version the executable asks for (symbol index sym), or NULL.
static const char *symbolVersion(const Elf32_Half *versym, const Elf32_Verneed *verneed, const char *strtab, size_t sym)
{
    if (!versym || !verneed)
        return NULL;
    Elf32_Half index = versym[sym] & 0x7fff;
    for (const Elf32_Verneed *vn = verneed;; vn = (const Elf32_Verneed *)((const char *)vn + vn->vn_next))
    {
        const Elf32_Vernaux *aux = (const Elf32_Vernaux *)((const char *)vn + vn->vn_aux);
        for (int i = 0; i < vn->vn_cnt; i++, aux = (const Elf32_Vernaux *)((const char *)aux + aux->vna_next))
            if (aux->vna_other == index)
                return strtab + aux->vna_name;
        if (!vn->vn_next)
            return NULL;
    }
}

// What ld.so binds a slot of obj to: the loader's own definition first (it
// is preloaded), else the version asked for, from the libraries after the
// loader for the executable (not the executable itself: a function whose
// address the game takes is defined there, as its PLT entry, which jumps
// through the slot being bound), from its own scope for a loaded library.
static void *resolve(const char *name, const char *version, struct link_map *obj, int isExe)
{
    Dl_info self, def;
    void *p = dlsym(RTLD_DEFAULT, name);
    void *scope = isExe ? RTLD_NEXT : (void *)obj;

    if (p && dladdr((void *)namcoEs1AlignImports, &self) && dladdr(p, &def) && def.dli_fbase == self.dli_fbase)
        return p;
    if (version && (p = dlvsym(scope, name, version)))
        return p;
    return dlsym(scope, name);
}

static void *alignedDlopen(const char *file, int mode);

// The imports of one object (the executable, or a library it loads).
static void alignObject(struct link_map *obj, int isExe)
{
    const Elf32_Rel *rel = NULL;
    const Elf32_Sym *symtab = NULL;
    const Elf32_Half *versym = NULL;
    const Elf32_Verneed *verneed = NULL;
    const char *strtab = NULL;
    size_t relSize = 0, count = 0, done = 0;
    uint32_t pltStart = UINT32_MAX, pltEnd = 0;

    for (const Elf32_Dyn *dyn = obj->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_JMPREL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_PLTRELSZ)
            relSize = dyn->d_un.d_val;
        else if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_VERSYM)
            versym = (const Elf32_Half *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_VERNEED)
            verneed = (const Elf32_Verneed *)dyn->d_un.d_ptr;
    }
    // ld.so rebased the others in place, not this one.
    if (verneed && (uintptr_t)verneed < obj->l_addr)
        verneed = (const Elf32_Verneed *)((uintptr_t)verneed + obj->l_addr);
    if (!rel || !symtab || !strtab)
        return;
    count = relSize / sizeof(*rel);

    // A slot not bound yet (lazy binding) still points at its PLT entry, in
    // the object's own code.
    Dl_info info;
    uint32_t base = 0;
    for (size_t i = 0; i < count; i++)
    {
        uint32_t v = obj->l_addr + rel[i].r_offset;
        v = *(uint32_t *)(uintptr_t)v;
        if (!base && dladdr(obj->l_ld, &info))
            base = (uint32_t)(uintptr_t)info.dli_fbase;
        if (dladdr((void *)(uintptr_t)v, &info) && (uint32_t)(uintptr_t)info.dli_fbase == base)
        {
            pltStart = v < pltStart ? v : pltStart;
            pltEnd = v > pltEnd ? v : pltEnd;
        }
    }

    for (size_t i = 0; i < count; i++)
    {
        size_t sym = ELF32_R_SYM(rel[i].r_info);
        const char *name = strtab + symtab[sym].st_name;
        uint32_t *slot = (uint32_t *)(uintptr_t)(obj->l_addr + rel[i].r_offset);
        uint32_t target = *slot;
        int skip = ELF32_R_TYPE(rel[i].r_info) != R_386_JMP_SLOT;

        for (int d = 0; direct[d] && !skip; d++)
            skip = !strcmp(name, direct[d]);
        if (skip)
            continue;
        if (target >= pltStart && target <= pltEnd)
        {
            void *p = resolve(name, symbolVersion(versym, verneed, strtab, sym), obj, isExe);
            if (!p)
                continue; // left to ld.so, unaligned
            target = (uint32_t)(uintptr_t)p;
        }
        if (!strcmp(name, "dlopen"))
            target = (uint32_t)(uintptr_t)alignedDlopen;
        void *stub = makeStub(target);
        if (!stub)
        {
            log_error("Namco: no memory for the import stubs");
            break;
        }
        *slot = (uint32_t)(uintptr_t)stub;
        done++;
    }
    log_info("Namco: %s: %zu imports called on an aligned stack", obj->l_name[0] ? obj->l_name : "the game", done);
}

// The libraries the game loads itself (its sound driver, nsAdrv.dll, built
// as it is) get the same.
static void *alignedDlopen(const char *file, int mode)
{
    void *handle = dlopen(file, mode);
    struct link_map *obj;

    if (handle && file && dlinfo(handle, RTLD_DI_LINKMAP, &obj) == 0 && !aligned(obj))
        alignObject(obj, 0);
    return handle;
}

void namcoEs1AlignImports(void)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    if (exe && !aligned(exe))
        alignObject(exe, 1);
}
