#!/bin/bash
# Build the 32-bit libraries Skycurser's runner (GameMaker: Studio, 2017)
# links and neither the desktop nor Batocera has, into the game's lib/ (next
# to runner):
#   libopenal.so.1    OpenAL Soft with its OSS backend only: its /dev/dsp is
#                     the loader's (src/loader/common/ossDsp.c), so the same
#                     library sounds on the desktop and on Batocera.
#   libssl.so.1.0.0, libcrypto.so.1.0.0
#                     OpenSSL 1.0.0's, versioned (OPENSSL_1.0.0) as the
#                     runner links them: not built, copied from OPENSSL_LIBS
#                     (an i386 Debian/Ubuntu build, e.g. The Walking Dead's).
#
# usage: build-skycurser-libs.sh "<Skycurser>/lib"
# CXX/CC: i686 compilers, Batocera's by default, else "g++ -m32"/"gcc -m32".
# WORK: where sources and builds go. OPENAL_TARBALL: OpenAL Soft's sources
# (Batocera's dl/ copy by default).
set -euo pipefail

out=${1:?usage: $0 <Skycurser/lib>}
bato_bin=/home/aderumier/code/batocera.linuxv43/output/x86_wow64/host/bin
if [ -z "${CC:-}" ]; then
    if [ -x "$bato_bin/i686-buildroot-linux-gnu-gcc" ]; then
        CC=$bato_bin/i686-buildroot-linux-gnu-gcc CXX=$bato_bin/i686-buildroot-linux-gnu-g++
    else
        CC="gcc -m32" CXX="g++ -m32"
    fi
fi
work=${WORK:-$HOME/.cache/linuxloader-skycurser}
openal_tarball=${OPENAL_TARBALL:-/home/aderumier/code/batocera.linux/dl/openal/openal-1.24.3.tar.gz}
openssl_libs=${OPENSSL_LIBS:-/home/aderumier/code/twd-native/src/x/lib/i386-linux-gnu}
mkdir -p "$work" "$out"
out=$(cd "$out" && pwd)

openal=$work/$(basename "$openal_tarball" .tar.gz | sed 's/^openal-/openal-soft-/')
[ -d "$openal" ] || tar -C "$work" -xzf "$openal_tarball"
read -r -a cc <<<"$CC"
read -r -a cxx <<<"$CXX"
build=$work/openal-build
cmake -S "$openal" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${cc[0]}" -DCMAKE_C_FLAGS="${cc[*]:1}" \
    -DCMAKE_CXX_COMPILER="${cxx[0]}" -DCMAKE_CXX_FLAGS="${cxx[*]:1}" \
    -DCMAKE_SYSTEM_PROCESSOR=i686 -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--as-needed" \
    -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_INSTALL=OFF -DALSOFT_RTKIT=OFF \
    -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_ALSA=OFF \
    -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_PORTAUDIO=OFF -DALSOFT_BACKEND_SNDIO=OFF \
    -DALSOFT_BACKEND_SDL2=OFF -DALSOFT_BACKEND_SDL3=OFF -DALSOFT_BACKEND_OSS=ON -DALSOFT_REQUIRE_OSS=ON \
    -DALSOFT_BACKEND_WAVE=OFF >/dev/null
cmake --build "$build" -j"$(nproc)" --target OpenAL >/dev/null
cp -L "$build/libopenal.so.1" "$out/libopenal.so.1"
strip "$out/libopenal.so.1"

cp -L "$openssl_libs/libssl.so.1.0.0" "$openssl_libs/libcrypto.so.1.0.0" "$out/"
ls -l "$out"
