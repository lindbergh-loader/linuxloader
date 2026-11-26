#!/usr/bin/env bash
# Tokyo Cop: gameport runs from the game's directory (the cabinet's
# /home/joc2001/joc2001/salidas, with save0/ and save1/ in it); it links
# glut and GLU, which the desktop may only have in 64-bit: LIBS32 is a
# directory with their 32-bit builds.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/gaelco/Tokyo Cop/Tokyo Cop}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
