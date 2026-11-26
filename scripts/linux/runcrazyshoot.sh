#!/usr/bin/env bash
# Crazy Shoot (IGS E2100, 2013, Unity 4.1): CrazyShoot runs from the dump's game/
# (the cabinet's /ShootBin with /CrazyShoot, tools/igs-e2200-extract.py: its data, ShootRope_Data, also as CrazyShoot_Data), with its
# libraries in game/lib and the loader. LIBS32 for the host's 32-bit
# libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Crazy Shoot/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
