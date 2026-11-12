#ifndef FBSD_STDCXX_H
#define FBSD_STDCXX_H

#include <stdint.h>

// The C++ library's file streams as FreeBSD 9.1 lays them out
// (fbsdStdcxx.c).

// A copy relocation of the game's (name, where, the host's object, its
// size) this fixes up itself: 1, done (the game's copy stays, not pointed
// at the host's).
int fbsdStdcxxCopy(const char *name, void *where, const void *host, uint32_t size);
// After the copies: a stream's VTT's entries for its complete vtable, the
// game's copy.
void fbsdStdcxxFixVtt(uint32_t gameVtable, uint32_t gameVtt, const char *vtableName);
// The library functions standing in for the host's (destructors).
void *fbsdStdcxxShim(const char *name);

#endif // FBSD_STDCXX_H
