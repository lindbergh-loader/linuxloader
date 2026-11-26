#!/usr/bin/env bash
# Fish Hunter SKL 2 (IGS E2200, 2012): FHSKLv2-8p runs from the dump's game/
# (the cabinet's /FishSKLv2-8p, tools/igs-e2200-extract.py), with its
# libraries in game/lib and the loader. LIBS32 for the host's 32-bit
# libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Fish Hunter SKL 2/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
