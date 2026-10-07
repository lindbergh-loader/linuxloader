#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <minwindef.h>
#else
#include <elf.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#endif

#include "flowControl.h"
#include "../log/log.h"
#include "../../minhook/include/MinHook.h"

void setVariable(size_t address, size_t value)
{
#ifdef __linux__
    int pagesize = sysconf(_SC_PAGE_SIZE);
    size_t *variable = (size_t *)address;
    void *toModify = (void *)(address - (address % pagesize));
    int prot = mprotect(toModify, pagesize, PROT_EXEC | PROT_WRITE);
    if (prot != 0)
#else
    DWORD oldProtect;
    if (!VirtualProtect((void *)address, sizeof(size_t), PAGE_EXECUTE_READWRITE, &oldProtect))
#endif

    {
        printf("setVariable\n");
        log_error("Error: Cannot unprotect memory region to change variable\n");
        return;
    }
#ifdef __linux__
    *variable = value;
#else
    size_t *variable = (size_t *)address;
    *variable = value;
    VirtualProtect((void *)address, sizeof(size_t), oldProtect, &oldProtect);
#endif
}

void setVariable16(size_t address, uint16_t value)
{
#ifdef __linux__
    int pagesize = sysconf(_SC_PAGE_SIZE);
    uint16_t *variable = (uint16_t *)address;
    void *toModify = (void *)(address - (address % pagesize));
    int prot = mprotect(toModify, pagesize, PROT_EXEC | PROT_WRITE);
    if (prot != 0)
#else
    DWORD oldProtect;
    if (!VirtualProtect((void *)address, sizeof(uint16_t), PAGE_EXECUTE_READWRITE, &oldProtect))
#endif

    {
        printf("setVariable16\n");
        log_error("Error: Cannot unprotect memory region to change variable\n");
        return;
    }
#ifdef __linux__
    *variable = value;
#else
    uint16_t *variable = (uint16_t *)address;
    *variable = value;
    VirtualProtect((void *)address, sizeof(uint16_t), oldProtect, &oldProtect);
#endif
}

void patchMemoryFromString(size_t address, char *value)
{
    size_t size = strlen((void *)value);
    if (size % 2 != 0)
    {
        log_error("Patch sting len should be even.\n");
        exit(EXIT_FAILURE);
    }

#ifdef __linux__
    int pagesize = sysconf(_SC_PAGE_SIZE);
    void *toModify = (void *)(address - (address % pagesize));
    int prot = mprotect(toModify, pagesize, PROT_EXEC | PROT_WRITE);
    if (prot != 0)
#else
    DWORD oldProtect;
    size_t len = strlen((void *)value);
    if (!VirtualProtect((void *)address, len / 2, PAGE_EXECUTE_READWRITE, &oldProtect))
#endif
    {
        log_error("Error: Cannot unprotect memory region to change variable\n");
        return;
    }

    size_t bufSize = size / 2;
    char *buf = (char *)malloc(bufSize);
    if (!buf)
    {
        log_error("Failed to allocate %zu bytes for patch buffer\n", bufSize);
        return;
    }
    char tmpchr[3];
    char *p = value;
    for (size_t i = 0; i < bufSize; i++)
    {
        memcpy(tmpchr, p, 2);
        tmpchr[2] = '\0';
        buf[i] = (int)strtol(tmpchr, NULL, 16);
        p += 2;
    }

    memcpy((void *)address, buf, bufSize);
    free(buf);

#ifdef _WIN32
    VirtualProtect((void *)address, len / 2, oldProtect, &oldProtect);
#endif
}

void detourFunction(size_t address, void *function)
{
    MH_CreateHook((void *)address, function, NULL);
}

void replaceCallAtAddress(size_t address, void *function)
{
#ifdef __linux__
    int pagesize = sysconf(_SC_PAGE_SIZE);
    void *toModify = (void *)(address - (address % pagesize));
    int prot = mprotect(toModify, pagesize, PROT_EXEC | PROT_WRITE);
    if (prot != 0)
#else
    DWORD oldProtect;
    if (!VirtualProtect((void *)address, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
#endif
    {
        log_error("Error: Cannot detour memory region to change variable\n");
        return;
    }

    uint32_t callAddress = (uint32_t)((uintptr_t)function - (uintptr_t)address) - 5;

    char cave[5] = {0xE8, 0x00, 0x00, 0x00, 0x00};
    cave[4] = (callAddress >> (8 * 3)) & 0xFF;
    cave[3] = (callAddress >> (8 * 2)) & 0xFF;
    cave[2] = (callAddress >> (8 * 1)) & 0xFF;
    cave[1] = (callAddress) & 0xFF;

    memcpy((void *)address, cave, 5);

#ifdef _WIN32
    VirtualProtect((void *)address, 5, oldProtect, &oldProtect);
#endif
}

int stubRetZero()
{
    return 0;
}

void stubReturn()
{
    return;
}

int stubRetOne()
{
    return 1;
}

int stubRetThree()
{
    return 3;
}

int stubRetMinusOne()
{
    return -1;
}

char stubRetZeroChar()
{
    return 0x00;
}

#ifdef __linux__
static size_t pageAlignDown(size_t addr)
{
    size_t pagesize = sysconf(_SC_PAGESIZE);
    return addr & ~(pagesize - 1);
}

int patchMemory(void *address, const void *data, size_t size)
{
    size_t page_start = pageAlignDown((size_t)address);
    size_t page_size = size + ((size_t)address - page_start);

    if (mprotect((void *)page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
        log_error("Error in mprotect.");
        return -1;
    }
    memcpy(address, data, size);
    return 0;
}

void *trampolineHook(void *target, void *replacement, size_t saveSize)
{
    void *trampoline = mmap(NULL, saveSize + 5, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }

    uintptr_t src = (uintptr_t)target;
    uintptr_t dst = (uintptr_t)replacement;

    // Save original bytes
    memcpy(trampoline, target, saveSize);

    // Append jmp back after saved bytes
    ((uint8_t *)trampoline)[saveSize] = 0xE9;
    uintptr_t jump_from = (uintptr_t)trampoline + saveSize;
    uintptr_t jump_to = src + saveSize;
    *(uint32_t *)((uint8_t *)trampoline + saveSize + 1) = (uint32_t)(jump_to - (jump_from + 5));

    // Patch original
    uint8_t jmp[5] = {0xE9};
    *(uint32_t *)&jmp[1] = (uint32_t)(dst - src - 5);
    if (patchMemory(target, jmp, sizeof(jmp)) != 0)
    {
        log_error("Error patching original function at %p\n", target);
    }

    return trampoline;
}

uintptr_t getBaseAddress()
{
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp)
        return 0;

    uint32_t base = 0;
    fscanf(fp, "%x-", &base);
    fclose(fp);
    return base;
}

// The main program's file: the first mapping's (/proc/self/exe is the
// dynamic linker when the game was started through it, see hostLinker.c),
// opened past the loader's own open (which maps /tmp paths).
static int openMainProgram(void)
{
    char line[PATH_MAX + 128];
    FILE *fp = fopen("/proc/self/maps", "r");
    int fd = -1;

    if (fp && fgets(line, sizeof(line), fp))
    {
        char *path = strchr(line, '/');
        if (path)
        {
            path[strcspn(path, "\n")] = '\0';
            fd = (int)syscall(SYS_openat, AT_FDCWD, path, O_RDONLY);
        }
    }
    if (fp)
        fclose(fp);
    return fd >= 0 ? fd : open("/proc/self/exe", O_RDONLY);
}

void *findStaticFnAddr(const char *functionName)
{
    int fd = openMainProgram();
    if (fd < 0)
        return NULL;

    struct stat64 st;
    if (fstat64(fd, &st) < 0)
    {
        close(fd);
        return NULL;
    }

    uint8_t *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED)
        return NULL;

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)data;
    Elf32_Shdr *shdrs = (Elf32_Shdr *)(data + ehdr->e_shoff);
    const char *shstrtab = (const char *)(data + shdrs[ehdr->e_shstrndx].sh_offset);

    Elf32_Shdr *symtabHdr = NULL, *strtabHdr = NULL;

    // Locate .symtab and .strtab
    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        const char *secname = shstrtab + shdrs[i].sh_name;
        if (strcmp(secname, ".symtab") == 0)
        {
            symtabHdr = &shdrs[i];
        }
        else if (strcmp(secname, ".strtab") == 0)
        {
            strtabHdr = &shdrs[i];
        }
    }

    if (!symtabHdr || !strtabHdr)
    {
        munmap(data, st.st_size);
        return NULL;
    }

    // Check if the binary is PIE (ET_DYN)
    int isPie = (ehdr->e_type == ET_DYN);
    uintptr_t base = isPie ? getBaseAddress() : 0;

    Elf32_Sym *symtab = (Elf32_Sym *)(data + symtabHdr->sh_offset);
    const char *strtab = (const char *)(data + strtabHdr->sh_offset);
    int numSyms = symtabHdr->sh_size / sizeof(Elf32_Sym);

    for (int i = 0; i < numSyms; i++)
    {
        const char *sym_name = strtab + symtab[i].st_name;
        if (strcmp(sym_name, functionName) == 0)
        {
            uintptr_t addr = symtab[i].st_value;
            munmap(data, st.st_size);
            return (void *)(base + addr);
        }
    }

    munmap(data, st.st_size);
    return NULL;
}

#else
extern void *GetStaticFunctionAddress(const char *name);
void *findStaticFnAddr(const char *functionName)
{
    // On Windows, ElfLoader has already parsed the ELF's SHT_SYMTAB memory
    // and registered all static functions during ExportSymbols().
    return GetStaticFunctionAddress(functionName);
}

void *trampolineHook(void *target, void *replacement, size_t saveSize)
{
    // Note: On Windows with MinHook, the 'saveSize' parameter is completely ignored
    // because MinHook handles instruction length disassembly automatically.
    void *originalFunctionTrampoline = NULL;
    // 1. Create the hook and ask MinHook to give us the trampoline pointer
    if (MH_CreateHook(target, replacement, &originalFunctionTrampoline) != MH_OK)
    {
        log_error("trampolineHook: MH_CreateHook failed for target %p", target);
        return NULL;
    }
    // 2. Enable the hook immediately (since the Linux mmap version enables it immediately)
    if (MH_EnableHook(target) != MH_OK)
    {
        log_error("trampolineHook: MH_EnableHook failed for target %p", target);
        return NULL;
    }
    // 3. Return the pointer to the original function trampoline so the replaced
    //    function can call back into the original code if it wants to.
    return originalFunctionTrampoline;
}
#endif
