#!/usr/bin/env bash
# Hero of Robots (IGS E2100, Ogre): child_linux runs from the dump's
# game/exe (the cabinet's /Robots/exe, tools/igs-e2200-extract.py), its
# Ogre libraries in game/data/so, the others in game/lib, and the loader.
# LIBS32 for the host's 32-bit libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Hero of Robots/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir/exe"
export LD_LIBRARY_PATH=$game_dir/data/so:$game_dir/lib:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
