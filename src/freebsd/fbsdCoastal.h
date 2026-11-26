#ifndef FBSD_COASTAL_H
#define FBSD_COASTAL_H

#include <stdint.h>
#include <sys/types.h>

// Coastal Amusements' serial I/O board (fbsdCoastal.c): its port opened (a
// descriptor of its own; -2 for another path), its bytes written and read.
int fbsdCoastalOpen(const char *path);
int fbsdCoastalIsFd(int fd);
ssize_t fbsdCoastalWrite(const void *buf, size_t count);
ssize_t fbsdCoastalRead(void *buf, size_t count);
void fbsdCoastalClose(void);

// The cabinet's disk as the game reads it (ATA IDENTIFY, 256 bytes:
// fbsdLibc.c's).
void fbsdDiskIdentity(uint8_t *params);

#endif // FBSD_COASTAL_H
