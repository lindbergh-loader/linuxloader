#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Wangan Midnight Maximum Tune 3 (Namco N2) directly with linuxloader.so.
# GAME=3dxp (default) or 3 (the export WMMT3). T=<seconds> stops it after that
# long (default 45); NAMCO_N2_TRACE=1 logs the shell commands, NAMCO_JVS_TRACE=1
# the I/O board's traffic. Output goes to $LOG (default /tmp/wmmt3.log).
# The game's lib/ holds what the system lacks (copied from its libso/: Boost
# and Cg), as for the Raw Thrills games; libso/ itself also has 2007 copies of
# librt, libz and libstdc++ that would shadow the host's, so it is not used.
case "${GAME:-3dxp}" in
    3) dir="Wangan Midnight Maximum Tune 3 (Export)" ;;
    *) dir="Wangan Midnight Maximum Tune 3DX+" ;;
esac
LOG=${LOG:-/tmp/wmmt3.log}
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/$dir/" || exit 1
export LD_LIBRARY_PATH=$repo_dir/libs/linux_x86:${LIBS32:+$LIBS32:}$repo_dir/build-linux:$PWD/lib
LD_PRELOAD=$repo_dir/build-linux/linuxloader.so /lib/ld-linux.so.2 ./main >"$LOG" 2>&1 &
pid=$!
for i in $(seq ${T:-45}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
echo "stopped; log: $LOG"
