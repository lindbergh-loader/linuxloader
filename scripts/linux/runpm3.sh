#!/usr/bin/env bash
# Percussion Master 3 (IGS, A36 board): pm3 runs from the TeknoRaven
# package's game/exe (its ../data, ../media, ../rewrite), with its libraries
# in exe/libso, data/so and game/lib (the X libraries hosts lack) and the
# loader's. LIBS32 for the host's 32-bit libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Percussion Master 3/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir/exe"
export LD_LIBRARY_PATH=$game_dir/lib:$game_dir/exe/libso:$game_dir/data/so:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
