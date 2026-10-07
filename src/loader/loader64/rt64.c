// linuxloader64.so: the 64-bit games' library, preloaded into each of them
// (the g7 dumps, Nerf Arcade): each game's part recognises its game and
// stays out of the others. What they share lives here.
#include <stdarg.h>
#include <stdio.h>

// The loader's shared code (iniParser.c, the border) logs through this.
int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    va_list ap;
    (void)level; (void)file; (void)line;
    fprintf(stderr, "linuxloader64: ");
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    return 0;
}
