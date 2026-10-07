#!/usr/bin/env bash
# Launch Halo: Fireteam Raven's 64-bit Linux executable with the g7 preload.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${HALO_GAME_DIR:?Set HALO_GAME_DIR to the game folder (.../pm/g7/halo)}
preload=${G7_RT_SO:-"$repo_dir/build-linux/g7_rt.so"}
timeout_seconds=${HALO_TIMEOUT:-1800}

if [[ ! -x $game_dir/game ]]; then
    printf 'Halo executable not found or not executable: %s/game\n' "$game_dir" >&2
    exit 1
fi
if [[ ! -f $preload ]]; then
    printf 'g7 preload not found: %s\nBuild it with: make linux\n' "$preload" >&2
    exit 1
fi

cd -- "$game_dir"
exec env LD_LIBRARY_PATH=lib LD_BIND_NOW=1 \
    timeout --signal=KILL "$timeout_seconds" /lib64/ld-linux-x86-64.so.2 \
    --preload "$preload" ./game "$@"
