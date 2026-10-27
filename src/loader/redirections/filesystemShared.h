#pragma once

#ifdef __linux__
#include <sys/select.h>
#else
#include <winsock2.h>
#endif
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
// hooks[] holds the emulated devices' fds, NO_DEVICE when not opened: no
// valid fd. (At 0, sharedSelect() took a select() on stdin for one on the
// unopened baseboard: Police Trainer 2's sound daemon reads its pipe so.)
typedef enum
{
    NO_DEVICE = -1,
    BASEBOARD = 1,
    EEPROM = 2,
    SERIAL0 = 3,
    SERIAL1 = 4,
    PCI_CARD_000 = 5
} DeviceType;

typedef enum
{
    CPUINFO = 0,
    OSRELEASE = 1,
    PCI_CARD_1F0 = 2,
    FILE_RW1 = 3,
    FILE_RW2 = 4,
    FILE_HARLEY = 5,
    FILE_FONT_ABC = 6,
    FILE_FONT_TGA = 7,
    FILE_LOGO_TGA = 8,
    ROUTE = 9
} FileTypes;

#ifdef __cplusplus
extern "C" {
#endif

void ConvertPath(char *dst, const char *src, size_t size);
int sharedRemove(const char *path);
int sharedMkdir(const char *path, mode_t mode);
int sharedOpen(const char *pathname, int flags, ...);
int sharedOpen64(const char *pathname, int flags, ...);
int sharedWrite(int fd, const void *buf, size_t count);
FILE *sharedFopen(const char *pathname, const char *mode);
FILE *sharedFopen64(const char *pathname, const char *mode);
int sharedFclose(FILE *stream);
int sharedClose(int fd);
char *sharedFgets(char *str, int n, FILE *stream);
ssize_t sharedRead(int fd, void *buf, size_t count);
size_t sharedFread(void *buf, size_t size, size_t count, FILE *stream);
long int sharedFtell(FILE *stream);
int sharedFseek(FILE *stream, long int offset, int whence);
void sharedRewind(FILE *stream);
int sharedIoctl(int fd, unsigned long int request, ...);
int sharedSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#ifdef __cplusplus
}
#endif