#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Tux Racer through the loader (test harness); SIGKILLs the whole process
# group after T seconds.
# The game's lib/ holds what Batocera lacks (Tcl 8.3, SDL 1.2's sdl12-compat,
# SDL_mixer 1.2, libpng12, see docs/tux-racer.md); LIBS32 the 32-bit
# libraries the desktop may lack as well (libSM, libICE, libXt, libXmu,
# libvorbis, SDL2: a colon-separated list).
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Tux Racer/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
setsid $repo_dir/build-linux/linuxloader -L $repo_dir/build-linux "$@" &
pid=$!
for i in $(seq ${T:-60}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null
wait $pid 2>/dev/null
