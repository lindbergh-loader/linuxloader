#pragma once

#include <cstdint>

// Auxiliary vector info for the Linux process stack.
// Populated from the ELF headers and passed to Setup() so that _start / glibc
// can find program header info, page size, entry point, etc.
struct ElfAuxvInfo
{
    uint32_t AtPhdr;      // AT_PHDR  (3) - address of program headers in memory
    uint32_t AtPhent;     // AT_PHENT (4) - size of one program header entry
    uint32_t AtPhnum;     // AT_PHNUM (5) - number of program headers
    uint32_t AtPageSz;    // AT_PAGESZ(6) - system page size
    uint32_t AtEntry;     // AT_ENTRY (9) - entry point address
};

class LinuxStack {
public:
    // Simple stack setup like Windy project
    // Returns the ESP pointer after building the stack
    static uint32_t Setup(uint32_t size, int argc, char** argv, uint32_t* outStackBase, uint32_t* outStackLimit, uint32_t reserveSize = 0x4000, const ElfAuxvInfo* auxvInfo = nullptr);

    // Commits the whole of the calling thread's reserved stack, removing the
    // guard page. Must be called on any thread that will run ELF code.
    //
    // Linux grows a thread stack on any access below ESP, so GCC emits no stack
    // probes; Windows only grows a stack when the guard page itself is touched.
    // A large ELF prologue (Hummer's MY_GAME_TEST_MENU::OnTick reserves 0x9c5c
    // bytes in one SUB ESP) therefore steps clean over the guard page and faults
    // on reserved memory. Setup() already hands the main thread a fully
    // committed block; this gives emulated pthreads the same guarantee.
    static bool CommitCurrentThreadStack();
};
