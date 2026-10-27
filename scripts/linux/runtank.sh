#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Tank! Tank! Tank! (Namco ES1) directly with linuxloader.so, through the
# host's dynamic linker.
# T=<seconds> stops it after that long (default 45); NAMCO_JVS_TRACE=1 logs the
# I/O board's traffic. Output goes to $LOG (default /tmp/tank.log).
# The game runs in a window; FULL=1 gives it the cabinet's -full.
LOG=${LOG:-/tmp/tank.log}
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/TANK! TANK! TANK!/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86:${LIBS32:+$LIBS32:}$repo_dir/build-linux
LD_PRELOAD=$repo_dir/build-linux/linuxloader.so /lib/ld-linux.so.2 ./n_tank_release ${FULL:+-full} >"$LOG" 2>&1 &
pid=$!
for i in $(seq ${T:-45}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
echo "stopped; log: $LOG"
