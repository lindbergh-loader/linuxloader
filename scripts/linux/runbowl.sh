#!/usr/bin/env bash
# Rockin' Bowl-O-Rama: bowlgold runs from the game's directory (the
# cabinet's /bowl); its lib/ holds a libXmu.so.6 stand-in (linked, unused).
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Rockin Bowl-O-Rama}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
