#!/bin/bash
# Build the 32-bit library Orange County Choppers Pinball (game.bin) links and
# neither the desktop nor Batocera has, into the game's directory (next to
# game.bin, not in its lib/: that is the cabinet's /game/dynamic/lib, with
# the cabinet's own glibc):
#   libpq.so.3        PostgreSQL's client API without a server
#                     (src/loader/itech/libs/pq.c): ITnet stays offline.
# libstdc++.so.5 is not built here: build-linux/igs-sr-libs has it (copy it
# next to game.bin for Batocera, which has none).
#
# usage: build-occ-libs.sh "<Orange County Choppers Pinball>"
# CC: an i686 compiler, Batocera's by default, else "gcc -m32".
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:?usage: $0 <Orange County Choppers Pinball>}
src=$repo_dir/src/loader/itech/libs
bato_cc=/home/aderumier/code/batocera.linuxv43/output/x86_wow64/host/bin/i686-buildroot-linux-gnu-gcc
if [ -z "${CC:-}" ]; then
    if [ -x "$bato_cc" ]; then CC=$bato_cc; else CC="gcc -m32"; fi
fi
read -r -a cc <<<"$CC"

"${cc[@]}" -O2 -shared -fPIC -Wl,-soname,libpq.so.3 -o "$out/libpq.so.3" "$src/pq.c"
echo "built $out/libpq.so.3"
