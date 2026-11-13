#!/usr/bin/env bash
# Cruis'n Blast 1.25 (decrypted memory dump) through the loader — local test harness.
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Cruisn Blast}
cd -- "$game_dir" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}${TP_LIBS:+:$TP_LIBS}
export vblank_mode=0
setsid "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@" &
pid=$!
for i in $(seq ${T:-20}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null; wait $pid 2>/dev/null
