#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Target: Terror Gold through the loader (test harness); SIGKILLs the
# whole process group after T seconds.
# The game links glut and GLU, which the desktop may only have in 64-bit:
# LIBS32 is a directory with their 32-bit builds.
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Target - Terror Gold Enhanced/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
export vblank_mode=0
setsid $repo_dir/build-linux/linuxloader -L $repo_dir/build-linux "$@" &
pid=$!
for i in $(seq ${T:-60}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null
wait $pid 2>/dev/null
