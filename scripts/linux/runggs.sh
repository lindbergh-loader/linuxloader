#!/usr/bin/env bash
# Go Go Strike: bowling_usa runs from the game's directory. Its lib/ holds
# liballeg.so.4.0 and libNL.so.1.6 (scripts/linux/build-gogostrike-libs.sh);
# build-linux/igs-sr-libs the era's libstdc++.so.5. Its window is Allegro's
# X11 one, 640x480.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Go Go Strike}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$repo_dir/build-linux/igs-sr-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
