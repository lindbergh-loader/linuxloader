#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Puck Off through the loader (test harness); SIGKILLs the whole process
# group after T seconds.
# The game links 32-bit libraries the desktop may lack (GLU, libogg,
# libvorbis, libstdc++.so.5): LIBS32 holds them (a colon-separated list).
# libGLEW.so.1.3 is the loader's (build-linux).
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Puck Off/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}:$repo_dir/build-linux
export vblank_mode=0
setsid $repo_dir/build-linux/linuxloader -L $repo_dir/build-linux "$@" &
pid=$!
for i in $(seq ${T:-60}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null
wait $pid 2>/dev/null
