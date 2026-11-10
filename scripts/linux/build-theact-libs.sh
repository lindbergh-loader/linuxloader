#!/bin/bash
# Build the 32-bit libraries The Act (Cecropia, decaf_Linux) asks for and
# neither the desktop nor Batocera has, into the game's lib/ (decaf/bin/lib,
# next to decaf_Linux):
#   libc.so.0, libm.so.0      uClibc's names (src/loader/cecropia/libs/uclibc.c)
#   libavformat.so.50         FFmpeg 2006's API on a minimal FFmpeg 7.1 (h264,
#   libavcodec.so.51,         PCM, mov: the game's movies), see avOld.c
#   libavutil.so.49
#   libsqlite3.so.0           sqlite 3 (its operator state database)
# SDL 1.2 is not built here: sdl12-compat (and the SDL2/SDL3 under it) come
# from the system or LIBS32.
#
# usage: build-theact-libs.sh "<The Act>/decaf/bin/lib"
# CC: an i686 compiler, Batocera's by default (its glibc is the oldest the
# libraries then need), else "gcc -m32". WORK: where sources and builds go.
# FFMPEG_TARBALL, SQLITE_TARBALL: local copies of the sources (an
# ffmpeg-7.1.1.tar.xz, a sqlite-autoconf or -amalgamation archive), else
# downloaded.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:?usage: $0 <The Act/decaf/bin/lib>}
src=$repo_dir/src/loader/cecropia/libs
bato_cc=/home/aderumier/code/batocera.linuxv43/output/x86_wow64/host/bin/i686-buildroot-linux-gnu-gcc
if [ -z "${CC:-}" ]; then
    if [ -x "$bato_cc" ]; then CC=$bato_cc; else CC="gcc -m32"; fi
fi
read -r -a cc <<<"$CC"
cross_prefix=
case "${cc[0]}" in *-gcc) cross_prefix=${cc[0]%gcc} ;; esac
ld=${cross_prefix}ld
objcopy=${cross_prefix}objcopy
command -v "$ld" >/dev/null || ld=ld
command -v "$objcopy" >/dev/null || objcopy=objcopy
work=${WORK:-$HOME/.cache/linuxloader-theact}
ffmpeg_ver=7.1.1
sqlite_ver=3470000
sqlite_year=2024
mkdir -p "$work" "$out"
out=$(cd "$out" && pwd)

# FFmpeg: the decoders, parser and demuxer the movies need, static, PIC.
# No asm: its 32-bit x86 code is not position independent (text
# relocations), and C decodes the movies' 640x480 H.264 fast enough. HEVC:
# the H.264 decoder's SEI code needs its film grain code (FFmpeg 7.1).
ffmpeg_src=$work/ffmpeg-$ffmpeg_ver
ffmpeg_prefix=$work/ffmpeg-prefix
if [ ! -f "$ffmpeg_prefix/lib/libavformat.a" ]; then
    if [ ! -d "$ffmpeg_src" ]; then
        if [ -n "${FFMPEG_TARBALL:-}" ]; then
            tar -xJf "$FFMPEG_TARBALL" -C "$work"
        else
            curl -fL "https://ffmpeg.org/releases/ffmpeg-$ffmpeg_ver.tar.xz" | tar -xJ -C "$work"
        fi
    fi
    mkdir -p "$work/ffmpeg-build"
    (cd "$work/ffmpeg-build" &&
        "$ffmpeg_src/configure" --prefix="$ffmpeg_prefix" --cc="$CC" --arch=x86_32 --cpu=i686 --target-os=linux \
            --enable-cross-compile --enable-pic --disable-asm --enable-static --disable-shared --disable-autodetect \
            --disable-programs --disable-doc --disable-network --disable-everything --disable-avdevice \
            --disable-avfilter --disable-swresample --disable-postproc --enable-swscale \
            --enable-decoder=h264,hevc,pcm_s16be,pcm_s16le --enable-parser=h264 --enable-demuxer=mov \
            --enable-protocol=file --extra-cflags=-fvisibility=hidden &&
        make -j"$(nproc)" && make install)
fi

# sqlite: the amalgamation (sqlite3.c, in either archive).
sqlite_src=$work/sqlite
if [ ! -f "$sqlite_src/sqlite3.c" ]; then
    rm -rf "$sqlite_src" && mkdir -p "$sqlite_src"
    case "${SQLITE_TARBALL:-}" in
    *.tar.gz) tar -xzf "$SQLITE_TARBALL" -C "$sqlite_src" --strip-components=1 ;;
    *.zip) unzip -joq "$SQLITE_TARBALL" -d "$sqlite_src" ;;
    *)
        curl -fL -o "$work/sqlite.zip" "https://www.sqlite.org/$sqlite_year/sqlite-amalgamation-$sqlite_ver.zip"
        unzip -joq "$work/sqlite.zip" -d "$sqlite_src"
        ;;
    esac
fi

build=$work/build
mkdir -p "$build"
# The bridge and the FFmpeg under it in one object, every name but the
# bridge's local: FFmpeg's own av_read_frame, av_free, ... stay out of the
# way of the old API's.
"${cc[@]}" -O2 -fPIC -I"$ffmpeg_prefix/include" -c "$src/avModern.c" -o "$build/avModern.o"
"$ld" -m elf_i386 -r -o "$build/ffmpeg.o" "$build/avModern.o" -L"$ffmpeg_prefix/lib" \
    --start-group -lavformat -lavcodec -lswscale -lavutil --end-group
"$objcopy" --wildcard --keep-global-symbol='bridge*' --keep-global-symbol='__x86.get_pc_thunk.*' "$build/ffmpeg.o" "$build/ffmpegLocal.o"
"${cc[@]}" -O2 -fPIC -shared -s -Wl,-soname,libavformat.so.50 -Wl,-z,noexecstack -Wl,-z,text -Wl,-z,defs -o "$out/libavformat.so.50" \
    "$src/avOld.c" "$build/ffmpegLocal.o" -lm -lpthread
for lib in libavcodec.so.51 libavutil.so.49 libm.so.0; do
    "${cc[@]}" -O2 -fPIC -shared -Wl,-soname,$lib -o "$out/$lib" "$src/soname.c"
done
"${cc[@]}" -O2 -fPIC -shared -Wl,-soname,libc.so.0 -o "$out/libc.so.0" "$src/uclibc.c"
"${cc[@]}" -O2 -fPIC -shared -s -Wl,-soname,libsqlite3.so.0 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
    -o "$out/libsqlite3.so.0" "$sqlite_src/sqlite3.c" -lpthread -lm
ls -l "$out"
