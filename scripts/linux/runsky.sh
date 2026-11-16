#!/usr/bin/env bash
# Skycurser: GameMaker's runner runs from the game's directory (it reads
# assets/game.unx there). Its lib/ holds OpenAL Soft (OSS output, the
# loader's /dev/dsp) and OpenSSL 1.0.0
# (scripts/linux/build-skycurser-libs.sh); build-linux/igs-sr-libs the
# loader's libglut. Its window is the runner's X11
# one, full screen at the display's size.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Skycurser}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib:$repo_dir/build-linux/igs-sr-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
