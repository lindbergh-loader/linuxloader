#!/usr/bin/env bash
# Speed Rider 2 (IGS): speedrider2 runs from the dump's game/bin_linux
# (TeknoRaven's layout) beside ../data, with its libraries (libso/) and the
# loader. LIBS32 for the host's 32-bit libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Speed Rider 2/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir/bin_linux"
export LD_LIBRARY_PATH=$game_dir/bin_linux:$game_dir/bin_linux/libso:$repo_dir/build-linux/igs-sr2-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
