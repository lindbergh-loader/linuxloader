#ifndef OSS_DSP_H
#define OSS_DSP_H

#include <stddef.h>
#include <sys/types.h>

// OSS /dev/dsp emulation on top of SDL3 audio (ossDsp.c), for the Raw
// Thrills and Namco ES1 games.
int ossDspIsPath(const char *path);
int ossDspOpen(void);
int ossDspIsFd(int fd);
ssize_t ossDspWrite(const void *buf, size_t count);
int ossDspIoctl(unsigned long request, void *arg);
void ossDspClose(void);

#endif // OSS_DSP_H
