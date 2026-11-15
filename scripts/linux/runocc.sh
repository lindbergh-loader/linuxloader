#!/usr/bin/env bash
# Orange County Choppers Pinball (Incredible Technologies): game.bin runs from
# the game's directory (the cabinet's /game). libpq.so.3 is next to it
# (scripts/linux/build-occ-libs.sh), build-linux/igs-sr-libs has the era's
# libstdc++.so.5. The game's lib/ (the cabinet's, its own glibc) is not
# searched.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Orange County Choppers Pinball}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$repo_dir/build-linux/igs-sr-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
