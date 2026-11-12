#!/usr/bin/env bash
# Temple Run (Coastal Amusements, FreeBSD 9.1): tr runs from the game's
# directory through linuxloader_freebsd (no loader preloaded); its lib/
# holds a 32-bit libopenal.so.0. LIBS32 for the host's 32-bit libraries
# (SDL 1.2, libpng16...), its /usr/lib32 first for ALSA's plugins.
set -e
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${GAME_DIR:-${GAMES_DIR:?Set GAME_DIR or GAMES_DIR}/Temple Run}
cd -- "$game_dir"
export LD_LIBRARY_PATH=$game_dir/lib${LIBS32:+:$LIBS32}
# The cabinet's monitor stands on its side: the picture turned upright.
export FBSD_ROTATE=${FBSD_ROTATE-ccw}
exec "$repo_dir/build-linux/linuxloader_freebsd" ./tr "$@"
