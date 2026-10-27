// Hooks on the game executable's own imports (see importHook.h).

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdint.h>
#include <string.h>

#include "importHook.h"

void *hookExecutableImport(const char *name, void *replacement)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
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
        *(void **)(uintptr_t)rel[i].r_offset = replacement;
        return dlsym(RTLD_NEXT, name);
    }
    return NULL;
}
