#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Deal or No Deal directly with linuxloader.so. DOND=us (default), uk or dlx.
case "${DOND:-us}" in
uk) dir="Deal or No Deal Uk" ;;
dlx) dir="Deal or No Deal Deluxe" ;;
*) dir="Deal or Not Deal" ;;
esac
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/$dir/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86${LIBS32:+:$LIBS32}${TP_LIBS:+:$TP_LIBS}:$repo_dir/build-linux
export vblank_mode=0
LD_PRELOAD=$repo_dir/build-linux/linuxloader.so setsid ./game -c "$@" &
pid=$!
for i in $(seq ${T:-600}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -$pid 2>/dev/null; wait $pid 2>/dev/null
