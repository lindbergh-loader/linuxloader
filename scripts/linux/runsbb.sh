#!/usr/bin/env bash
# SpongeBob SquarePants Bikini Bottom Bowling: the launcher removes the Rockey envelope in its cache.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/SpongeBob SquarePants Bikini Bottom Bowling}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}${TP_LIBS:+:$TP_LIBS}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
