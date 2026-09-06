#include "linuxStack.hpp"
#include "../log/log.h"
#include <windows.h>
#include <intrin.h>

// Linux auxiliary vector type constants
#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_CLKTCK 17

uint32_t LinuxStack::Setup(uint32_t size, int argc, char** argv, uint32_t* outStackBase, uint32_t* outStackLimit, uint32_t reserveSize, const ElfAuxvInfo* auxvInfo)
{
    // 1. Align size to page boundary (4KB) just to be safe
    size = (size + 4095) & ~4095;

    void* stack = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // 2. Calculate TIB limits (High/Low)
    uint32_t highAddr = (uint32_t)stack + size;
    uint32_t lowAddr = (uint32_t)stack;

    // 4. Return these to the caller
    if (outStackBase) *outStackBase = highAddr;
    if (outStackLimit) *outStackLimit = lowAddr;
    
    uint32_t esp = highAddr - reserveSize;
    
    uint32_t* ptr = (uint32_t*)esp;

    // --- Auxiliary vector (highest addresses, pushed first) ---
    *(--ptr) = 0;           // AT_NULL value
    *(--ptr) = AT_NULL;     // AT_NULL type

    if (auxvInfo)
    {
        *(--ptr) = 100;                 *(--ptr) = AT_CLKTCK;   // Clock ticks per second
        *(--ptr) = 0;                   *(--ptr) = AT_EGID;     // Effective GID
        *(--ptr) = 0;                   *(--ptr) = AT_GID;      // GID
        *(--ptr) = 0;                   *(--ptr) = AT_EUID;     // Effective UID
        *(--ptr) = 0;                   *(--ptr) = AT_UID;      // UID
        *(--ptr) = auxvInfo->AtEntry;   *(--ptr) = AT_ENTRY;    // Entry point
        *(--ptr) = auxvInfo->AtPageSz;  *(--ptr) = AT_PAGESZ;   // Page size
        *(--ptr) = auxvInfo->AtPhnum;   *(--ptr) = AT_PHNUM;    // Number of program headers
        *(--ptr) = auxvInfo->AtPhent;   *(--ptr) = AT_PHENT;    // Program header entry size
        *(--ptr) = auxvInfo->AtPhdr;    *(--ptr) = AT_PHDR;     // Program header address
    }

    *(--ptr) = 0;  // envp terminator

    *(--ptr) = 0;  // argv end (null terminator)

    for (int i = argc - 1; i >= 0; i--)
    {
        *(--ptr) = (uint32_t)argv[i];
    }
    
    *(--ptr) = argc;  // argc
    
    return (uint32_t)ptr;
}

bool LinuxStack::CommitCurrentThreadStack()
{
    // Cheap enough to call from any ELF entry point: after the first successful
    // call on a thread this is a single TLS load.
    static thread_local bool alreadyCommitted = false;
    if (alreadyCommitted)
        return true;

    // A local gives us an address that is guaranteed to sit inside the current
    // thread's committed stack region.
    volatile char probe = 0;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)&probe, &mbi, sizeof(mbi)) != sizeof(mbi))
    {
        log_error("CommitCurrentThreadStack: VirtualQuery failed (%lu)", GetLastError());
        return false;
    }

    // AllocationBase is the bottom of the entire stack reservation; BaseAddress
    // is the bottom of the committed run we are currently standing in. Anything
    // between the two is the guard page plus still-reserved pages.
    uint8_t* allocBase = (uint8_t*)mbi.AllocationBase;
    uint8_t* committedBase = (uint8_t*)mbi.BaseAddress;

    if (!allocBase || committedBase <= allocBase)
    {
        alreadyCommitted = true; // Already fully committed (e.g. the main ELF stack)
        return true;
    }

    SIZE_T uncommitted = (SIZE_T)(committedBase - allocBase);

    // MEM_COMMIT over an already-committed page is legal and simply re-applies
    // the protection, so this single call also clears PAGE_GUARD from the guard
    // page that sits at the bottom of the committed run.
    if (!VirtualAlloc(allocBase, uncommitted, MEM_COMMIT, PAGE_READWRITE))
    {
        log_error("CommitCurrentThreadStack: failed to commit %zu bytes at %p (%lu)",
                  (size_t)uncommitted, (void*)allocBase, GetLastError());
        return false;
    }

    // Keep the TIB's StackLimit in sync with the real bottom of the stack. This
    // only ever widens the range the OS considers valid, so it cannot make an
    // otherwise-legal access look out of bounds.
    __writefsdword(0x08, (DWORD)(uintptr_t)allocBase);

    alreadyCommitted = true;
    return true;
}
