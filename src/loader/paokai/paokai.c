// Paokai games (see paokai.h): their cabinet's devices and the start-up they need.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "paokai.h"
#include "../common/importHook.h"
#include "../log/log.h"

extern uint32_t gId;

// The SRAM's size, as the game maps it.
#define SRAM_SIZE 0x10000

static char gameDir[PATH_MAX];

const PaokaiGame *paokaiCurrentGame(void)
{
    static uint32_t cachedId;
    static const PaokaiGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = paokaiGetGame(gId);
    }
    return game;
}

int isPaokaiGame(void)
{
    return paokaiCurrentGame() != NULL;
}

const char *paokaiRedirectPath(const char *path, char *buf, size_t size)
{
    if (!path || !gameDir[0])
        return path;
    // The I/O card's ports and its control register (writes only, or reads
    // the loader answers: see paokaiIo.c).
    if (!strncmp(path, "/dev/w040101_", 13))
        return "/dev/null";
    if (!strcmp(path, "/dev/w040101sram0"))
    {
        snprintf(buf, size, "%s/sram.bin", gameDir);
        return buf;
    }
    // The serial port (none of its devices on the cabinet's I/O: its opening
    // fails, as an absent port's would).
    if (!strncmp(path, "/dev/ttyS", 9))
        return "/dev/null/ttyS";
    return path;
}

__attribute__((force_align_arg_pointer)) static int gameSystem(const char *command)
{
    if (command)
        printf("Paokai: not run: %s\n", command);
    return 0;
}

// The game takes a mapping below 0 as a failure (an int): its kernel's
// mappings were all below 2 GiB, a 32-bit process's on a 64-bit kernel are
// near 4 GiB. Its mappings (the SRAM's) are placed in its own first 2 GiB.
#define LOW_MAP_START 0x40000000u
#define LOW_MAP_END 0x80000000u
#define LOW_MAP_STEP 0x01000000u

__attribute__((force_align_arg_pointer)) static void *gameMmap(void *addr, size_t length, int prot, int flags, int fd,
                                                               off_t offset)
{
    void *p;

    if (addr || (flags & MAP_FIXED))
        return mmap(addr, length, prot, flags, fd, offset);
    for (uintptr_t a = LOW_MAP_START; a + length <= LOW_MAP_END; a += LOW_MAP_STEP)
        if ((p = mmap((void *)a, length, prot, flags | MAP_FIXED_NOREPLACE, fd, offset)) != MAP_FAILED)
            return p;
    log_warn("Paokai: no mapping below 2 GiB");
    return mmap(addr, length, prot, flags, fd, offset);
}

int paokaiInit(void)
{
    const PaokaiGame *g = paokaiCurrentGame();
    char path[PATH_MAX];
    int fd;

    // Started in the game's directory. The SRAM: a file there, its size the
    // mapping's (zeros at first: the game formats it).
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    snprintf(path, sizeof(path), "%s/sram.bin", gameDir);
    if ((fd = open(path, O_RDWR | O_CREAT, 0644)) < 0 || ftruncate(fd, SRAM_SIZE) != 0)
        log_warn("Paokai: no SRAM file %s", path);
    if (fd >= 0)
        close(fd);

    hookExecutableImport("system", (void *)gameSystem);
    hookExecutableImport("mmap", (void *)gameMmap);
    paokaiIoInstall(g);
    return 0;
}
