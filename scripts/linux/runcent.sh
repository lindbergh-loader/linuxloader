#!/usr/bin/env bash
# Launch Centipede Chaos's 64-bit Linux executable (the dump, game2) with the
# g7 preload. Without -f<w>x<h> it opens a 1280x720 window.
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${CENT_GAME_DIR:?Set CENT_GAME_DIR to the game folder (.../g7/centipede)}
preload=${G7_RT_SO:-"$repo_dir/build-linux/g7_rt.so"}
timeout_seconds=${CENT_TIMEOUT:-1800}

if [[ ! -f $game_dir/game2 ]]; then
    printf 'Centipede dump not found: %s/game2\n' "$game_dir" >&2
    exit 1
fi
if [[ ! -f $preload ]]; then
    printf 'g7 preload not found: %s\nBuild it with: make linux\n' "$preload" >&2
    exit 1
fi

cd -- "$game_dir"
exec env LD_BIND_NOW=1 \
    timeout --signal=KILL "$timeout_seconds" /lib64/ld-linux-x86-64.so.2 \
    --preload "$preload" ./game2 "$@"
