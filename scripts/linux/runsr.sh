#!/usr/bin/env bash
# Speed Rider M109: run from the package's game/ beside data/. The game's own
# libso/ holds the era's Ogre 1.4.9 and X11 client libraries; the host's
# 32-bit Mesa gives the GL it renders on (the cabinet's NVIDIA GL stack is
# unavailable on non-NVIDIA hosts, and Ogre's GL render system is vendor
# neutral). build-linux/igs-sr-libs supplies what neither the dump nor the
# host has: the era's libstdc++.so.5, libXmu.so.6 (libXaw's) and libglut.so.3
# (the loader's). LIBS32 supplies any further host 32-bit legacy libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Speed Rider/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
# Archives can flatten SONAME symlinks into text files. Repair them in a
# temporary search directory, preserving the supplied dump.
compat_libs=$(mktemp -d "${TMPDIR:-/tmp}/linuxloader-sr-libs.XXXXXX")
trap 'rm -f -- "$compat_libs"/*; rmdir -- "$compat_libs"' EXIT
for path in "$game_dir"/libso/*.so*; do
    if [[ -f $path && ! -L $path ]] && [[ $(stat -c %s -- "$path") -lt 128 ]]; then
        target=$(< "$path")
        if [[ $target != */* && -f $game_dir/libso/$target ]]; then
            ln -s -- "$game_dir/libso/$target" "$compat_libs/${path##*/}"
        fi
    fi
done
# The game runs through the host's 32-bit glibc; GL, GLU, X11 and z come from
# its 32-bit (Mesa) libraries, found after the dump's own.
export LD_LIBRARY_PATH=$compat_libs:$game_dir:$game_dir/libso:$repo_dir/build-linux/igs-sr-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
