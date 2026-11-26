#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Wheel of Fortune directly with linuxloader.so.
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Wheel of Fortune/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}${TP_LIBS:+:$TP_LIBS}:$repo_dir/build-linux
export vblank_mode=0
LD_PRELOAD=$repo_dir/build-linux/linuxloader.so setsid ./game "$@" &
pid=$!
for i in $(seq ${T:-600}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null; wait $pid 2>/dev/null
