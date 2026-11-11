#!/usr/bin/env bash
# Gold Captain: GoldCaptain runs from the game's directory (the cabinet's
# /GoldCaptain); its lib/ holds libcasmcard.so and libpcsclite.so.0, from the
# disk's root partition.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Gold Captain}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
