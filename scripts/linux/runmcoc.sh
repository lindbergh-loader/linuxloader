#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Marvel Contest of Champions through the loader (test harness); SIGKILLs
# the whole process group after T seconds. linuxloader hands it to
# linuxloader64, which starts the dump's own Linux player (see
# docs/marvel-contest-of-champions.md).
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Marvel Contest of Champions/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86
setsid $repo_dir/build-linux/linuxloader -g . "$@" &
pid=$!
for i in $(seq ${T:-60}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null
wait $pid 2>/dev/null
