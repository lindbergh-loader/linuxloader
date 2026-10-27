// Launcher side of Raw Thrills support.
//
// In the dumps, a few undefined dynamic symbols (malloc, free, ...) carry the
// address of an envelope import stub as their value. ld.so then binds other
// libraries' references to those stubs, and libX11's constructor calls malloc
// through one before the loader can fix the envelope tables. The launcher
// therefore runs a copy of the ELF with those values cleared, kept in the
// user's cache directory; the game directory is left untouched.

#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rtGame.h"
#include "../config/iniParser.h"
#include "../log/log.h"

#define RT_STACK_SIZE (64u << 20)

static int mkdirs(const char *path)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return mkdir(buf, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

static int readFile(const char *path, unsigned char **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;

    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        !(*data = malloc(len)) || fread(*data, 1, len, f) != (size_t)len)
    {
        fclose(f);
        return -1;
    }
    fclose(f);
    *size = len;
    return 0;
}

// Map a virtual address to a file offset through the PT_LOAD segments.
static long vaddrToOffset(const unsigned char *elf, size_t size, Elf32_Addr vaddr)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    for (int i = 0; i < eh->e_phnum; i++)
    {
        const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_LOAD && vaddr >= ph->p_vaddr && vaddr < ph->p_vaddr + ph->p_filesz)
        {
            long off = ph->p_offset + (vaddr - ph->p_vaddr);
            return off < (long)size ? off : -1;
        }
    }
    return -1;
}

// The Rockey envelope encrypts the code with one XOR mask, then starts a
// watchdog child polling /dev/rockey. Start directly at the restored _start
// instead; neither the driver nor a captured dongle reply is needed.
static int unwrapRockey(unsigned char *elf, size_t size, const RtGame *game)
{
    if (!game->rockeyTextSize)
        return 0;
    if (size < sizeof(Elf32_Ehdr) || game->rockeyTextSize % 4)
        return -1;
    long off = vaddrToOffset(elf, size, game->rockeyTextStart);
    long last = vaddrToOffset(elf, size, game->rockeyTextStart + game->rockeyTextSize - 1);
    if (off < 0 || last < off || (size_t)last >= size || last - off + 1 != game->rockeyTextSize)
        return -1;
    for (uint32_t i = 0; i < game->rockeyTextSize; i += 4)
    {
        uint32_t word;
        memcpy(&word, elf + off + i, sizeof(word));
        word ^= game->rockeyTextXor;
        memcpy(elf + off + i, &word, sizeof(word));
    }
    // The restored i386 C runtime startup: xor %ebp,%ebp; pop %esi;
    // mov %esp,%ecx; and $-16,%esp. Refuse a mismatched executable.
    static const unsigned char startup[] = {0x31, 0xed, 0x5e, 0x89, 0xe1, 0x83, 0xe4, 0xf0};
    if (game->rockeyTextSize < sizeof(startup) || memcmp(elf + off, startup, sizeof(startup)))
        return -1;
    Elf32_Ehdr *eh = (Elf32_Ehdr *)elf;
    eh->e_entry = game->rockeyTextStart;
    // Rockey stores a marker in the reserved ELF identification bytes;
    // current ld.so rejects that nonzero padding.
    memset(eh->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
    return 0;
}

// Some dumps (Angry Birds Arcade) lost the symbol hash table and the version
// definitions, which sit in data the envelope had cleared: ld.so then finds
// none of the executable's own symbols (the copies of libstdc++'s objects
// the libraries must share, the envelope entry its GOT names). The hash
// table is rebuilt in its place, as a single chain through every symbol,
// and the empty version definitions are dropped.
static int repairSymbolLookup(unsigned char *elf, size_t size, Elf32_Dyn *dyn, long symOff, long strOff)
{
    const Elf32_Sym *syms = (const Elf32_Sym *)(elf + symOff);
    static const Elf32_Sym empty;
    uint32_t *hash = NULL;
    uint32_t count;
    long hashOff;
    int patched = 0;

    for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++)
        if (d->d_tag == DT_HASH && (hashOff = vaddrToOffset(elf, size, d->d_un.d_ptr)) >= 0 &&
            hashOff + 8 <= (long)size)
            hash = (uint32_t *)(elf + hashOff);
    if (hash && hash[0] == 0 && hash[1] == 0)
    {
        // The symbol table is padded with empty entries up to the strings.
        for (count = (strOff - symOff) / sizeof(Elf32_Sym); count > 1; count--)
            if (memcmp(&syms[count - 1], &empty, sizeof(empty)) != 0)
                break;
        size_t words = 3 + count;
        int clear = (unsigned char *)(hash + words) <= elf + size;
        for (size_t i = 0; clear && i < words; i++)
            clear = hash[i] == 0;
        if (clear)
        {
            hash[0] = 1;         // nbucket
            hash[1] = count;     // nchain
            hash[2] = count - 1; // the bucket: the last symbol, chained down to the first
            for (uint32_t i = 1; i < count; i++)
                hash[3 + i] = i - 1;
            patched++;
        }
    }

    for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++)
    {
        long off;
        if (d->d_tag != DT_VERDEF || (off = vaddrToOffset(elf, size, d->d_un.d_ptr)) < 0 ||
            ((const Elf32_Verdef *)(elf + off))->vd_version == VER_DEF_CURRENT)
            continue;
        Elf32_Dyn *out = dyn;
        for (Elf32_Dyn *in = dyn;; in++)
        {
            if (in->d_tag != DT_VERDEF && in->d_tag != DT_VERDEFNUM)
                *out++ = *in;
            if (in->d_tag == DT_NULL)
                break;
        }
        patched++;
        break;
    }
    return patched;
}

// TeknoParrot's rebuilt executables (MotoGP) map their code from .interp on,
// past the ELF and program headers: no segment holds the headers, so the
// kernel passes no AT_PHDR and ld.so crashes on a null header table. The
// code segment is extended down to the file's start, the page it lands on
// anyway. Their headers also list the code segment second, where the
// loader's id is read from the third (see mainL.c): the dynamic segment is
// moved before the loads, which ld.so and the kernel find by type.
static int repairProgramHeaders(unsigned char *elf, size_t size)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    Elf32_Phdr *ph = (Elf32_Phdr *)(elf + eh->e_phoff);
    size_t end = eh->e_phoff + (size_t)eh->e_phnum * sizeof(Elf32_Phdr);
    int first = -1, patched = 0;

    if (eh->e_phentsize != sizeof(Elf32_Phdr) || end > size)
        return 0;
    for (int i = 0; i < eh->e_phnum; i++)
    {
        if (ph[i].p_type == PT_PHDR)
            return 0;
        if (ph[i].p_type == PT_LOAD && first < 0)
            first = i;
    }
    if (first < 0 || ph[first].p_offset <= eh->e_phoff || ph[first].p_offset != (ph[first].p_vaddr & 0xfff) ||
        !(ph[first].p_flags & PF_X))
        return 0;

    ph[first].p_filesz += ph[first].p_offset;
    ph[first].p_memsz += ph[first].p_offset;
    ph[first].p_vaddr -= ph[first].p_offset;
    ph[first].p_paddr = ph[first].p_vaddr;
    ph[first].p_offset = 0;
    ph[first].p_align = 0x1000;
    patched++;

    // Non-load headers first, the loads after them in their order.
    if (eh->e_phnum >= 3 && first != 2)
    {
        Elf32_Phdr sorted[16];
        int n = 0;
        if (eh->e_phnum > 16)
            return patched;
        for (int i = 0; i < eh->e_phnum; i++)
            if (ph[i].p_type != PT_LOAD)
                sorted[n++] = ph[i];
        for (int i = 0; i < eh->e_phnum; i++)
            if (ph[i].p_type == PT_LOAD)
                sorted[n++] = ph[i];
        if (sorted[2].p_type == PT_LOAD && (sorted[2].p_flags & PF_X))
        {
            memcpy(ph, sorted, eh->e_phnum * sizeof(Elf32_Phdr));
            patched++;
        }
    }
    return patched;
}

// Clear the value of every undefined dynamic symbol that has one, drop the
// directory of libraries needed by absolute path, and repair the symbol
// lookup of dumps that lost it.
static int patchUndefinedSymbols(unsigned char *elf, size_t size)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    Elf32_Addr symtab = 0, strtab = 0, rel = 0;
    Elf32_Word relSize = 0;
    long dynOff = -1, symOff, strOff, relOff;
    int patched = 0;

    if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -1;
    for (int i = 0; i < eh->e_phnum; i++)
    {
        const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_DYNAMIC)
            dynOff = ph->p_offset;
    }
    if (dynOff < 0)
        return -1;
    for (const Elf32_Dyn *d = (const Elf32_Dyn *)(elf + dynOff); d->d_tag != DT_NULL; d++)
    {
        if (d->d_tag == DT_SYMTAB)
            symtab = d->d_un.d_ptr;
        else if (d->d_tag == DT_STRTAB)
            strtab = d->d_un.d_ptr;
        else if (d->d_tag == DT_REL)
            rel = d->d_un.d_ptr;
        else if (d->d_tag == DT_RELSZ)
            relSize = d->d_un.d_val;
    }
    symOff = vaddrToOffset(elf, size, symtab);
    strOff = vaddrToOffset(elf, size, strtab);
    // The dynamic string table directly follows the symbol table.
    if (symOff < 0 || strOff <= symOff)
        return -1;

    // TeknoParrot's rebuilt executables (MotoGP) list the variables they
    // keep a copy of (R_386_COPY: stdout, libstdc++'s empty string) as
    // undefined: the libraries then bind to their own, and libstdc++ frees
    // the game's empty string as if it were on the heap. Every symbol at a
    // copy's address is defined there again.
    if (rel && (relOff = vaddrToOffset(elf, size, rel)) >= 0 && relOff + relSize <= size)
    {
        const Elf32_Rel *r = (const Elf32_Rel *)(elf + relOff);
        for (size_t i = 0; i < relSize / sizeof(Elf32_Rel); i++)
        {
            if (ELF32_R_TYPE(r[i].r_info) != R_386_COPY)
                continue;
            for (Elf32_Sym *sym = (Elf32_Sym *)(elf + symOff) + 1; (unsigned char *)(sym + 1) <= elf + strOff; sym++)
                if (sym->st_shndx == SHN_UNDEF && sym->st_value == r[i].r_offset)
                {
                    sym->st_shndx = SHN_ABS;
                    sym->st_info = ELF32_ST_INFO(STB_GLOBAL, STT_OBJECT);
                    patched++;
                }
        }
    }

    for (Elf32_Sym *sym = (Elf32_Sym *)(elf + symOff) + 1; (unsigned char *)(sym + 1) <= elf + strOff; sym++)
    {
        if (sym->st_shndx == SHN_UNDEF && sym->st_value != 0)
        {
            sym->st_value = 0;
            patched++;
        }
    }

    // TeknoParrot's rebuilt symbol tables (MotoGP) lost some bindings: an
    // import turned local (in6addr_any), which ld.so binds to address 0, and
    // a second, global copy of a weak import (__gmon_start__), fatal when
    // nothing defines it. Both get their binding back.
    Elf32_Sym *first = (Elf32_Sym *)(elf + symOff) + 1;
    for (Elf32_Sym *sym = first; (unsigned char *)(sym + 1) <= elf + strOff; sym++)
    {
        if (sym->st_shndx != SHN_UNDEF || sym->st_name == 0)
            continue;
        if (ELF32_ST_BIND(sym->st_info) == STB_LOCAL)
        {
            sym->st_info = ELF32_ST_INFO(STB_GLOBAL, ELF32_ST_TYPE(sym->st_info));
            patched++;
            continue;
        }
        if (ELF32_ST_BIND(sym->st_info) != STB_GLOBAL)
            continue;
        const char *name = (const char *)elf + strOff + sym->st_name;
        for (const Elf32_Sym *other = first; (const unsigned char *)(other + 1) <= elf + strOff; other++)
            if (other->st_shndx == SHN_UNDEF && ELF32_ST_BIND(other->st_info) == STB_WEAK &&
                strcmp((const char *)elf + strOff + other->st_name, name) == 0)
            {
                sym->st_info = ELF32_ST_INFO(STB_WEAK, ELF32_ST_TYPE(sym->st_info));
                patched++;
                break;
            }
    }

    // Libraries the cabinet loaded from an absolute path: look them up by
    // name instead (the string is shortened in place).
    for (const Elf32_Dyn *d = (const Elf32_Dyn *)(elf + dynOff); d->d_tag != DT_NULL; d++)
    {
        char *name = (char *)elf + strOff + d->d_un.d_val;
        char *base;
        if (d->d_tag != DT_NEEDED || name[0] != '/' || !(base = strrchr(name, '/')))
            continue;
        memmove(name, base + 1, strlen(base + 1) + 1);
        patched++;
    }
    return patched + repairSymbolLookup(elf, size, (Elf32_Dyn *)(elf + dynOff), symOff, strOff) +
           repairProgramHeaders(elf, size);
}

static int writeFile(const char *path, const unsigned char *data, size_t size)
{
    char tmp[PATH_MAX];
    FILE *f;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!(f = fopen(tmp, "wb")))
        return -1;
    if (fwrite(data, 1, size, f) != size)
    {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    chmod(tmp, 0755);
    return rename(tmp, path);
}

static void cacheDir(char *buf, size_t size, uint32_t fileCrc32)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");

    if (xdg && xdg[0])
        snprintf(buf, size, "%s/linuxloader/rawthrills/%08x", xdg, fileCrc32);
    else
        snprintf(buf, size, "%s/.cache/linuxloader/rawthrills/%08x", home ? home : "/tmp", fileCrc32);
}

// [Display] WIDTH and HEIGHT of the configuration, 0 if not set.
static void configSize(const char *configPath, int *width, int *height)
{
    IniConfig *ini = configPath && configPath[0] ? iniLoad(configPath) : NULL;
    const char *w = ini ? iniGetValue(ini, "Display", "WIDTH") : NULL;
    const char *h = ini ? iniGetValue(ini, "Display", "HEIGHT") : NULL;

    *width = w ? atoi(w) : 0;
    *height = h ? atoi(h) : 0;
    if (ini)
        iniFree(ini);
}

// command is "./<elf>" optionally followed by " -t" (test mode).
void rtPrepareCommand(char *command, size_t size, uint32_t fileCrc32, const char *configPath)
{
    const RtGame *game = rtGetGameByFileCrc(fileCrc32);
    char sizeArgument[64] = "";
    char elfPath[PATH_MAX], suffix[16] = "", dir[PATH_MAX], copy[PATH_MAX];
    struct stat src, dst;
    unsigned char *data = NULL;
    size_t len;
    const char *space;

    if (!game)
        return;
    if (game->sizeArgument)
    {
        int width, height;
        configSize(configPath, &width, &height);
        // A turned frame is drawn at the game's own size, the only one it
        // takes (see rtFrame.c): the game is told it whatever [Display] says.
        if (game->frameTurn)
        {
            width = game->frameTurnWidth;
            height = game->frameTurnHeight;
        }
        if (width > 0 && height > 0)
        {
            sizeArgument[0] = ' ';
            snprintf(sizeArgument + 1, sizeof(sizeArgument) - 1, game->sizeArgument, width, height);
        }
    }

    snprintf(elfPath, sizeof(elfPath), "%s", command);
    if ((space = strrchr(command, ' ')) && strcmp(space, " -t") == 0)
    {
        elfPath[space - command] = '\0';
        snprintf(suffix, sizeof(suffix), "%s", space);
    }

    cacheDir(dir, sizeof(dir), fileCrc32);
    const char *base = strrchr(elfPath, '/');
    snprintf(copy, sizeof(copy), "%s/%s", dir, base ? base + 1 : elfPath);

    if (stat(elfPath, &src) != 0)
        return;
    if (stat(copy, &dst) != 0 || dst.st_size != src.st_size || dst.st_mtime < src.st_mtime)
    {
        int patched;
        if (mkdirs(dir) != 0 || readFile(elfPath, &data, &len) != 0 ||
            unwrapRockey(data, len, game) != 0 ||
            (patched = patchUndefinedSymbols(data, len)) < 0 || writeFile(copy, data, len) != 0)
        {
            log_error("Raw Thrills: cannot prepare a patched copy of %s in %s", elfPath, dir);
            free(data);
            return;
        }
        free(data);
        printf("Raw Thrills: prepared %s (%d symbols patched)\n", copy, patched);
    }

    snprintf(command, size, "%s%s%s", copy, sizeArgument, suffix);

    // The dump's GOT still holds the cabinet's library addresses: have ld.so
    // resolve every relocatable import at startup instead of lazily.
    setenv("LD_BIND_NOW", "1", 1);

    // Some games use more than the usual 8 MB of main thread stack.
    struct rlimit stack;
    if (getrlimit(RLIMIT_STACK, &stack) == 0 && stack.rlim_cur != RLIM_INFINITY && stack.rlim_cur < RT_STACK_SIZE)
    {
        stack.rlim_cur = stack.rlim_max == RLIM_INFINITY || stack.rlim_max >= RT_STACK_SIZE ? RT_STACK_SIZE : stack.rlim_max;
        setrlimit(RLIMIT_STACK, &stack);
    }
}
