#!/usr/bin/env bash
# MuziBox (IGS, A36 board): mb runs from the TeknoRaven package's game/ (its
# relative paths), with its libraries in game/lib and the loader. LIBS32 for
# the host's 32-bit libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/MuziBox/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$game_dir:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
