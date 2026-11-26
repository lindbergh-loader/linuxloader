#!/usr/bin/env bash
# Pirate Revenge: sship runs from the game's directory (the cabinet's
# /pr/linux); its lib/ holds libcasmcard.so and libpcsclite.so.0, from the
# disk's root partition.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Pirate Revenge}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
