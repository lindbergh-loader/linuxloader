// libc.so.0: uClibc 0.9.28's name for The Act's C library. The game's
// calls are glibc's (same names, same structures for the ones it uses):
// this library is only there for the dynamic linker to find, with the one
// function glibc lacks, uClibc's start-up, called by the game's _start.

#include <stdlib.h>

extern char **environ;

__attribute__((force_align_arg_pointer)) int __uClibc_main(int (*main)(int, char **, char **), int argc,
                                                           char **argv, void (*appInit)(void),
                                                           void (*appFini)(void), void (*rtldFini)(void),
                                                           void *stackEnd)
{
    (void)stackEnd;
    // The dynamic linker's fini (edx at _start), then the program's, run at
    // exit as glibc's start-up registers them.
    if (rtldFini)
        atexit(rtldFini);
    if (appFini)
        atexit(appFini);
    // The executable's constructors (_init): glibc's dynamic linker runs the
    // libraries', the main program's are its start-up's.
    if (appInit)
        appInit();
    exit(main(argc, argv, environ));
}
