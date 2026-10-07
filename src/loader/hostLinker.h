#ifndef HOST_LINKER_H
#define HOST_LINKER_H

#include <stddef.h>
#include <stdint.h>

// Launcher: a game's ELF that is not executable (a dump copied without its
// mode) is run through the host's 32-bit dynamic linker (hostLinker.c).
void hostLinkerPrepareCommand(char *command, size_t size, uint32_t fileCrc32);

// The host's 32-bit dynamic linker, or NULL.
const char *hostLinkerPath(void);

#endif // HOST_LINKER_H
