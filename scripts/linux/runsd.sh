#!/usr/bin/env bash
# Speed Driver: sdlinux runs from the game's directory (game/ in the dump);
# its lib_extra/ holds SDL_mixer (with the dual-card Mix_*2 calls), the card
# reader's libcasmcard.so and libpcsclite.so.0, and Ogg Vorbis.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Speed Driver/game}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib_extra:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
exec "$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
