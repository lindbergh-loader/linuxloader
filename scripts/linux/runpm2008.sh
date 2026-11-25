#!/usr/bin/env bash
# Percussion Master 2008 (IGS, A27 board): PM2008v2 runs from the TeknoRaven package's game/
# (its relative paths; its ./*_data from data/). LIBS32 for the host's 32-bit
# libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Percussion Master 2008/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/libso:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
