#!/bin/bash
# Build the 32-bit libraries Go Go Strike (bowling_usa) links and neither the
# desktop nor Batocera has, into the game's lib/ (next to bowling_usa):
#   liballeg.so.4.0   Allegro 4.0.3's shared half: its C sources, X11 window,
#                     OSS sound (the loader's /dev/dsp). The other half,
#                     liballeg_unsharable.a (the i386 assembler: blitters,
#                     sprites, the X window's bank switch), is linked into
#                     bowling_usa itself, which exports it to this library.
#   libNL.so.1.6      HawkNL's API, no network (src/loader/paokai/libs/nl.c):
#                     the lanes' link to their server.
# libstdc++.so.5 is not built here: build-linux/igs-sr-libs has it.
#
# usage: build-gogostrike-libs.sh "<Go Go Strike>/lib"
# CC: an i686 compiler, Batocera's by default, else "gcc -m32". WORK: where
# sources and builds go. ALLEGRO_SRC: a local Allegro 4.0.3 tree, else cloned
# (liballeg's v4-0-3 tag). Allegro's own build (fix.sh, configure) is not
# used: its platform header is written here.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:?usage: $0 <Go Go Strike/lib>}
src=$repo_dir/src/loader/paokai/libs
bato_cc=/home/aderumier/code/batocera.linuxv43/output/x86_wow64/host/bin/i686-buildroot-linux-gnu-gcc
if [ -z "${CC:-}" ]; then
    if [ -x "$bato_cc" ]; then CC=$bato_cc; else CC="gcc -m32"; fi
fi
read -r -a cc <<<"$CC"
work=${WORK:-$HOME/.cache/linuxloader-gogostrike}
allegro=${ALLEGRO_SRC:-$work/allegro-4.0.3}
mkdir -p "$work" "$out"
out=$(cd "$out" && pwd)

if [ ! -d "$allegro/src" ]; then
    git clone -q --depth 1 --branch v4-0-3 https://github.com/liballeg/allegro5.git "$allegro"
fi

# Allegro's platform headers, as its configure would write them for an i386
# Linux X11 build without modules, DGA or VidMode (no mode switches on the
# host's desktop), the console drivers left out.
build=$work/allegro-build
mkdir -p "$build/include/allegro/platform"
echo '#define ALLEGRO_UNIX' >"$build/include/allegro/platform/alplatf.h"
cat >"$build/include/allegro/platform/alunixac.h" <<'EOF'
#define ALLEGRO_COLOR8
#define ALLEGRO_COLOR16
#define ALLEGRO_COLOR24
#define ALLEGRO_COLOR32
#define ALLEGRO_MMX
#define ALLEGRO_SSE
#define ALLEGRO_LITTLE_ENDIAN
#define ALLEGRO_WITH_XWINDOWS
#define ALLEGRO_XWINDOWS_WITH_SHM
#define ALLEGRO_WITH_OSSDIGI
#define ALLEGRO_USE_CONSTRUCTOR
#define ALLEGRO_USE_SCHED_YIELD
#define HAVE_DIRENT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_GETPAGESIZE 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIBPTHREAD 1
#define HAVE_LIMITS_H 1
#define HAVE_LINUX_SOUNDCARD_H 1
#define HAVE_MEMCMP 1
#define HAVE_MEMORY_H 1
#define HAVE_MKSTEMP 1
#define HAVE_MMAP 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_IO_H 1
#define HAVE_SYS_SOUNDCARD_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VPRINTF 1
#define RETSIGTYPE void
#define STDC_HEADERS 1
#define TIME_WITH_SYS_TIME 1
EOF

# The shared half: every C source of the build (misc/deplib.sh puts the
# assembler ones, and udummy.c, in liballeg_unsharable.a), but the FLI player
# (casts as lvalues, which today's gcc refuses: the game plays none).
list() { sed -n "/^$1 = /,/^[[:space:]]*\$/p" "$allegro/makefile.lst" | grep -o 'src/[^ \\]*\.c'; }
sources=$( (list ALLEGRO_SRC_FILES; list ALLEGRO_SRC_I386_FILES; list ALLEGRO_SRC_UNIX_FILES; list ALLEGRO_SRC_X_FILES) |
    grep -v -e alsa -e arts -e uesd -e udummy -e xdga2 -e /fli.c)
cflags=(-O2 -fPIC -std=gnu89 -fcommon -fno-strict-aliasing -w -DALLEGRO_SRC -DALLEGRO_LIB_BUILD
    -I"$build/include" -I"$build/include/allegro/platform" -I"$allegro/include" -I"$allegro/src")
# A compiler whose sysroot has no X11 (Batocera's wow64 one) gets the host's
# X11 headers alone, and links against the host's 32-bit libX11 and libXext
# (the target's own are loaded at run time).
xlibs=()
if ! echo '#include <X11/Xlib.h>' | "${cc[@]}" -E -x c - >/dev/null 2>&1; then
    mkdir -p "$build/xinc" "$build/xlib"
    ln -sfn /usr/include/X11 "$build/xinc/X11"
    ln -sf /usr/lib32/libX11.so.6 "$build/xlib/libX11.so"
    ln -sf /usr/lib32/libXext.so.6 "$build/xlib/libXext.so"
    cflags+=(-isystem "$build/xinc")
    xlibs=(-L"$build/xlib" -Wl,--allow-shlib-undefined)
fi
# sound.c, fixed in a copy for today's gcc: its digi_none declared extern,
# then defined static.
mkdir -p "$build/obj" "$build/src"
rm -f "$build/src/fli.c"
sed 's/^static DIGI_DRIVER digi_none =/DIGI_DRIVER digi_none =/' "$allegro/src/sound.c" >"$build/src/sound.c"
objs=()
for s in $sources; do
    o=$build/obj/$(basename "${s%.c}").o
    c=$allegro/$s
    [ -f "$build/$s" ] && c=$build/$s
    if [ ! -f "$o" ] || [ "$c" -nt "$o" ]; then
        "${cc[@]}" "${cflags[@]}" -c "$c" -o "$o"
    fi
    objs+=("$o")
done
"${cc[@]}" "${cflags[@]}" -c "$src/allegroCompat.c" -o "$build/obj/allegroCompat.o"
objs+=("$build/obj/allegroCompat.o")
"${cc[@]}" -shared -Wl,-soname,liballeg.so.4.0 -o "$out/liballeg.so.4.0" "${objs[@]}" \
    "${xlibs[@]}" -lX11 -lXext -lpthread -lm -ldl

"${cc[@]}" -O2 -fPIC -shared -Wl,-soname,libNL.so.1.6 -o "$out/libNL.so.1.6" "$src/nl.c" -lpthread
echo "built: $out/liballeg.so.4.0 $out/libNL.so.1.6"
