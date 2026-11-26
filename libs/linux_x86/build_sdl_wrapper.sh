#!/bin/bash
# Build the CSNeo libSDL-1.2.so.0 wrapper (forwarders + SDL_GL_SetAttribute
# override) and install it into the game dir. The real SDL stays as
# libSDL-1.2.real.so.0; the wrapper resolves it by absolute path via dlopen.
set -e
cd /home/aderumier/code/linuxloader
GAME="/home/aderumier/code/jurassicpark.wine/drive_c/game/CounterStrike Neo"
REAL="$GAME/csneo2/lib/libSDL-1.2.real.so.0"
WRAPPER="$GAME/csneo2/lib/libSDL-1.2.so.0"
SYMS="${1:-/tmp/need_syms.txt}"

bash libs/linux_x86/gen_sdl_wrapper.sh "$SYMS" /tmp/sdl12_wrapper_fwd.c "$REAL"

cat > /tmp/sdl12_override.c <<EOF
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
static void *_sdl_ov_handle(void) {
    static void *h = 0;
    if (!h) h = dlopen("$REAL", RTLD_NOW | RTLD_GLOBAL);
    return h;
}
int SDL_GL_SetAttribute(int attr, int value) {
    static int (*real)(int, int) = 0;
    if (!real) real = (int(*)(int,int))dlsym(_sdl_ov_handle(), "SDL_GL_SetAttribute");
    if (!real) return -1;
    if (attr >= 0 && attr <= 3 && value > 0 && value < 8) value = 8;
    if (attr == 12) return 0;
    return real(attr, value);
}
EOF

cat /tmp/sdl12_override.c /tmp/sdl12_wrapper_fwd.c > /tmp/sdl12_combined.c
gcc -m32 -shared -fPIC -o "$WRAPPER" /tmp/sdl12_combined.c -ldl
echo "built wrapper: $WRAPPER"
readelf -d "$WRAPPER" | grep -E "NEEDED|SONAME"
