#ifndef OSS_DSP_H
#define OSS_DSP_H

#include <stddef.h>
#include <sys/types.h>

// OSS /dev/dsp emulation on top of SDL3 audio (ossDsp.c), for the Raw
// Thrills, Namco ES1, Teamplay and Global VR games.
int ossDspIsPath(const char *path);
// Playback only: a capture open (read-only: America's Army's OpenAL opens
// one for voice chat) fails, rather than taking over the playback device's
// format.
int ossDspOpen(int flags);
int ossDspIsFd(int fd);
ssize_t ossDspWrite(const void *buf, size_t count);
int ossDspIoctl(unsigned long request, void *arg);
void ossDspClose(void);

#endif // OSS_DSP_H
