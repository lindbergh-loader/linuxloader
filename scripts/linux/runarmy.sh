#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run America's Army through the loader (test harness); SIGKILLs the whole
# process group after T seconds.
# The game links 32-bit libraries the desktop may lack (libstdc++.so.5), and
# scales its fullscreen with sdl12-compat (libSDL-1.2.so.0 on SDL2/SDL3, the
# loader preloads it in place of the game's own): LIBS32 holds them (a
# colon-separated list).
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/America's Army/System" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}:$repo_dir/build-linux
setsid $repo_dir/build-linux/linuxloader -L $repo_dir/build-linux "$@" &
pid=$!
for i in $(seq ${T:-60}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null
wait $pid 2>/dev/null
