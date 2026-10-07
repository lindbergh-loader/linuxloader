#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Nirin (Namco ES1) directly with linuxloader.so, through the host's
# dynamic linker (the game asks for /opt/arcade/i686/lib/ld-linux.so.2).
# The game's folder is the cabinet's /opt/arcade/exec: a.elf at its top.
# T=<seconds> stops it after that long (default 600); NAMCO_JVS_TRACE=1 logs
# the I/O board's traffic.
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Nirin/" || exit 1
export LD_LIBRARY_PATH=${NIRIN_LIBS:+$NIRIN_LIBS:}$repo_dir/libs/linux_x86:${LIBS32:+$LIBS32:}$repo_dir/build-linux
LD_PRELOAD=$repo_dir/build-linux/linuxloader.so /lib/ld-linux.so.2 ./a.elf "$@" &
pid=$!
for i in $(seq ${T:-600}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
