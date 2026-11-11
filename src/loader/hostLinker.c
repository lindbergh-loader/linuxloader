// Launcher side: dumps that lost their executable mode.
//
// Copies of a cabinet's disk may have lost the executables' mode (a dump
// made on another system): the kernel cannot start them. Those are run
// through the host's 32-bit dynamic linker ("/lib/ld-linux.so.2
// ./med_pball"), which LD_PRELOAD still applies to. For the Teamplay,
// Global VR and Roxor games, whose dumps come that way, and the Cecropia
// ones always: built against uClibc, they ask for its dynamic linker
// (/lib/ld-uClibc.so.0), which glibc's stands in for.
// Speed Driver 3 dumps can also lack their executable mode.

#include <limits.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hostLinker.h"
#include "globalvr/gvrGame.h"
#include "log/log.h"
#include "roxor/roxorGame.h"
#include "cecropia/cecropiaGame.h"
#include "teamplay/teamplayGame.h"
#include "igs/igsGame.h"

static const char *const hostLinkers[] = {
    "/lib/ld-linux.so.2",
    "/lib32/ld-linux.so.2",
    "/usr/lib32/ld-linux.so.2",
    "/usr/lib/i386-linux-gnu/ld-linux.so.2",
    NULL,
};

const char *hostLinkerPath(void)
{
    for (const char *const *linker = hostLinkers; *linker; linker++)
        if (access(*linker, X_OK) == 0)
            return *linker;
    return NULL;
}

static char igsCopy[PATH_MAX];

__attribute__((destructor)) static void removeIgsCopy(void)
{
    if (*igsCopy)
        unlink(igsCopy);
}

// SD2's DT_NEEDED names /lib/libcasmcard.so. Stage a copy with that import
// resolved by SONAME, leaving the supplied executable and its mode intact.
static int prepareIgsCopy(const char *path)
{
    FILE *in = fopen(path, "rb"), *out;
    unsigned char *elf;
    long length;
    int fd, changed = 0;
    if (!in)
        return -1;
    if (fseek(in, 0, SEEK_END) || (length = ftell(in)) < (long)sizeof(Elf32_Ehdr) ||
        fseek(in, 0, SEEK_SET) || !(elf = malloc((size_t)length)))
    {
        fclose(in);
        return -1;
    }
    if (fread(elf, 1, (size_t)length, in) != (size_t)length)
    {
        fclose(in);
        free(elf);
        return -1;
    }
    fclose(in);
    // This is an exact, CRC-identified ELF. Keep the replacement in place:
    // every segment, code address and the loader's partial CRC stay intact.
    static const char needed[] = "/lib/libcasmcard.so";
    for (size_t i = 0; i + sizeof(needed) <= (size_t)length; i++)
        if (!memcmp(elf + i, needed, sizeof(needed)))
        {
            memset(elf + i, 0, sizeof(needed));
            memcpy(elf + i, "libcasmcard.so", sizeof("libcasmcard.so"));
            changed++;
        }
    if (changed != 1)
    {
        free(elf);
        return -1;
    }
    strcpy(igsCopy, "/tmp/linuxloader-sd2.XXXXXX");
    fd = mkstemp(igsCopy);
    if (fd < 0 || !(out = fdopen(fd, "wb")))
    {
        if (fd >= 0) close(fd);
        removeIgsCopy();
        free(elf);
        return -1;
    }
    int ok = fwrite(elf, 1, (size_t)length, out) == (size_t)length;
    if (fclose(out)) ok = 0;
    free(elf);
    if (!ok)
    {
        removeIgsCopy();
        return -1;
    }
    return 0;
}

void hostLinkerPrepareCommand(char *command, size_t size, uint32_t fileCrc32)
{
    char elfPath[PATH_MAX], copy[PATH_MAX * 2];
    const char *space;
    int uclibc = cecropiaGetGameByFileCrc(fileCrc32) != NULL;
    const IgsGame *igs = igsGetGameByFileCrc(fileCrc32);

    if (!teamplayGetGameByFileCrc(fileCrc32) && !gvrGetGameByFileCrc(fileCrc32) && !roxorGetGameByFileCrc(fileCrc32) &&
        !uclibc && !(igs && (igs->a36Rom || igs->sd2Rom || igs->sr1Rom)))
        return;

    snprintf(elfPath, sizeof(elfPath), "%s", command);
    if ((space = strrchr(command, ' ')) && strcmp(space, " -t") == 0)
        elfPath[space - command] = '\0';
    if (igs && igs->sd2Rom)
    {
        if (prepareIgsCopy(elfPath))
        {
            log_error("Could not stage Speed Driver 2's card-library import");
            exit(EXIT_FAILURE);
        }
        snprintf(command, size, "'%s'%s", igsCopy, space ? " -t" : "");
    }
    else if (!uclibc && access(elfPath, X_OK) == 0)
        return;

    const char *linker = hostLinkerPath();
    if (linker && (size_t)snprintf(copy, sizeof(copy), "%s %s", linker, command) < size)
    {
        strcpy(command, copy);
        printf("%s runs through %s\n", elfPath, linker);
        return;
    }
    log_error("No 32-bit dynamic linker found to run %s", elfPath);
}
