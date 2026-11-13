#!/usr/bin/env bash
# Power Truck (IGS): pt runs from the TeknoRaven package's game/ beside
# ../data, with its libraries (game/, lib_extra/) and the loader. LIBS32 for
# the host's 32-bit libraries. Power Truck S: GAME_DIR=.../Power Truck S/game
# (its pt repaired by tools/igs-pts-repair.py).
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Power Truck/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir:$game_dir/lib_extra:$repo_dir/build-linux/igs-sr2-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
