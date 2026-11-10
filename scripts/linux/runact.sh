#!/usr/bin/env bash
# The Act: decaf_Linux runs from the game's decaf/bin; its lib/ holds the
# libraries scripts/linux/build-theact-libs.sh builds (uClibc's names, FFmpeg
# 2006's API, sqlite). LIBS32: the 32-bit libraries the desktop lacks
# (sdl12-compat and the SDL2/SDL3 under it: a colon-separated list).
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/The Act}
cd -- "$game_dir/decaf/bin"
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
