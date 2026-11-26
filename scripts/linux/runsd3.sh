#!/usr/bin/env bash
# Speed Driver 3 (IGS, 2010): sd3 runs from the dump's game/ (TeknoRaven's
# layout) with its libraries (libso/, lib_extra/) and the loader. LIBS32 for
# the host's 32-bit libraries.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Speed Driver 3/game}
game_dir=$(cd -- "$game_dir" && pwd)
cd -- "$game_dir"
# Some archives flattened these symlinks into small files containing their
# targets. Stage repaired links without changing the dump.
compat_libs=$(mktemp -d "${TMPDIR:-/tmp}/linuxloader-sd3-libs.XXXXXX")
trap 'rm -f -- "$compat_libs"/*; rmdir -- "$compat_libs"' EXIT
for name in libGLU.so.1 libidn.so.11 libogg.so.0 libpcsclite.so.1 libtheoradec.so.1 libvorbis.so.0; do
    path=$game_dir/lib_extra/$name
    if [[ -f $path && ! -L $path ]] && [[ $(stat -c %s -- "$path") -lt 128 ]]; then
        target=$(< "$path")
        if [[ $target != */* && -f $game_dir/lib_extra/$target ]]; then
            ln -s -- "$game_dir/lib_extra/$target" "$compat_libs/$name"
        fi
    fi
done
export LD_LIBRARY_PATH=$compat_libs:$game_dir:$game_dir/libso:$game_dir/lib_extra:$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}
"$repo_dir/build-linux/linuxloader" -L "$repo_dir/build-linux" "$@"
