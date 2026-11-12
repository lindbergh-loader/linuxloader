#!/usr/bin/env bash
# Speed Driver 4 (IGS, 2012): olympus (UPX unpacked) runs from the dump's
# game/ (the cabinet's /SD4/readwrite, tools/igs-e3000-extract.py) beside
# ../data, with its libraries and the loader. LIBS32 for the host's 32-bit
# libraries; build-linux/igs-sd4-libs for what neither has (libtiff.so.3,
# libjpeg.so.62, libpng12.so.0).
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Speed Driver 4/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir:$repo_dir/build-linux/igs-sd4-libs:$repo_dir/build-linux/igs-sr2-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
