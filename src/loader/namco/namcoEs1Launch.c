// Launcher side of Namco ES1 support.
//
// The games name the cabinet's dynamic linker (/opt/arcade/i686/lib/
// ld-linux.so.2) as their interpreter, which a PC does not have: the kernel
// cannot start them. They are run through the host's 32-bit dynamic linker
// instead ("/lib/ld-linux.so.2 ./a.elf"), which LD_PRELOAD still applies to.
//
// Nirin's picture flickers under NVIDIA's OpenGL (the cabinet's 2008 driver
// aside), on Windows too, where the known fix runs it on Mesa's software
// renderer. Under Mesa it does not: on a host running NVIDIA's driver, the
// game gets Mesa's zink, OpenGL on the same GPU through its Vulkan driver,
// unless the environment already picks an OpenGL vendor.

#include <elf.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "namcoEs1Game.h"
#include "../log/log.h"

static const char *const hostLinkers[] = {
    "/lib/ld-linux.so.2",
    "/lib32/ld-linux.so.2",
    "/usr/lib32/ld-linux.so.2",
    "/usr/lib/i386-linux-gnu/ld-linux.so.2",
    NULL,
};

// The ELF's PT_INTERP, or "" if it has none.
static void elfInterpreter(const char *path, char *interp, size_t size)
{
    FILE *f = fopen(path, "rb");
    Elf32_Ehdr eh;

    interp[0] = '\0';
    if (!f)
        return;
    if (fread(&eh, sizeof(eh), 1, f) == 1 && memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
        eh.e_ident[EI_CLASS] == ELFCLASS32)
    {
        for (int i = 0; i < eh.e_phnum; i++)
        {
            Elf32_Phdr ph;
            if (fseek(f, eh.e_phoff + i * eh.e_phentsize, SEEK_SET) != 0 || fread(&ph, sizeof(ph), 1, f) != 1)
                break;
            if (ph.p_type != PT_INTERP)
                continue;
            size_t n = ph.p_filesz < size ? ph.p_filesz : size - 1;
            if (fseek(f, ph.p_offset, SEEK_SET) == 0 && fread(interp, 1, n, f) == n)
                interp[n] = '\0';
            else
                interp[0] = '\0';
            break;
        }
    }
    fclose(f);
}

void namcoEs1PrepareCommand(char *command, size_t size, uint32_t fileCrc32)
{
    char elfPath[PATH_MAX], interp[PATH_MAX], copy[PATH_MAX * 2];
    const char *space;
    const NamcoEs1Game *g = namcoEs1GetGameByFileCrc(fileCrc32);

    if (!g)
        return;

    if (g->mesaOnNvidia && access("/proc/driver/nvidia/version", F_OK) == 0 && !getenv("__GLX_VENDOR_LIBRARY_NAME"))
    {
        setenv("__GLX_VENDOR_LIBRARY_NAME", "mesa", 1);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
        setenv("LIBGL_KOPPER_DRI2", "1", 1);
        log_info("Namco: NVIDIA driver: OpenGL through Mesa's zink");
    }

    snprintf(elfPath, sizeof(elfPath), "%s", command);
    if ((space = strrchr(command, ' ')) && strcmp(space, " -t") == 0)
        elfPath[space - command] = '\0';
    // The cabinet's command line (its boot scripts pick it).
    if (g->arguments)
    {
        snprintf(copy, sizeof(copy), "%s %s%s", elfPath, g->arguments, command + strlen(elfPath));
        if (strlen(copy) < size)
            strcpy(command, copy);
    }
    elfInterpreter(elfPath, interp, sizeof(interp));
    if (interp[0] == '\0' || access(interp, X_OK) == 0)
        return;

    for (const char *const *linker = hostLinkers; *linker; linker++)
    {
        if (access(*linker, X_OK) != 0)
            continue;
        snprintf(copy, sizeof(copy), "%s %s", *linker, command);
        if (strlen(copy) >= size)
            break;
        strcpy(command, copy);
        printf("Namco: %s runs through %s (it asks for %s)\n", elfPath, *linker, interp);
        return;
    }
    log_error("Namco: no 32-bit dynamic linker found to run %s (it asks for %s)", elfPath, interp);
}
