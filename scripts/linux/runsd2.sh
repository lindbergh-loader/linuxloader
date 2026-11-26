#!/usr/bin/env bash
# Speed Driver 2 M207CNC: run from the directory containing sd2, data/ and
# roms/SD2_EPROM.dec.bin. LIBS32 supplies the host's 32-bit legacy libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Speed Driver 2}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
# Archives can flatten the GLU SONAME symlink into a text file.
compat_libs=$(mktemp -d "${TMPDIR:-/tmp}/linuxloader-sd2-libs.XXXXXX")
trap 'rm -f -- "$compat_libs"/*; rmdir -- "$compat_libs"' EXIT
path=$game_dir/lib_extra/libGLU.so.1
if [[ -f $path && ! -L $path ]] && [[ $(stat -c %s -- "$path") -lt 128 ]]; then
    target=$(< "$path")
    if [[ $target != */* && -f $game_dir/lib_extra/$target ]]; then
        ln -s -- "$game_dir/lib_extra/$target" "$compat_libs/libGLU.so.1"
    fi
fi
export LD_LIBRARY_PATH=$compat_libs:$game_dir:$game_dir/lib_extra:$repo_dir/build-linux/igs-sd2-libs:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
