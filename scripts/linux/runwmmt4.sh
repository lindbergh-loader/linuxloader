#!/bin/bash
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Run Wangan Midnight Maximum Tune 4 (Namco ES1) directly with linuxloader.so,
# through the host's dynamic linker (the game asks for
# /opt/arcade/i686/lib/ld-linux.so.2). The game's folder is the cabinet's
# /opt/arcade/exec (WMN4/: WMN4r at its top); its lib/ holds what the system
# lacks (libprotobuf.so.7, OpenSSL 0.9.8). T=<seconds> stops it after that
# long (default 60); NAMCO_JVS_TRACE=1 logs the I/O board's traffic. Output
# goes to $LOG (default /tmp/wmmt4.log).
LOG=${LOG:-/tmp/wmmt4.log}
cd "${GAMES_DIR:?Set GAMES_DIR to the folder holding the games}/Wangan Midnight Maximum Tune 4/WMN4/" || exit 1
export LD_LIBRARY_PATH=$PWD/lib:$repo_dir/libs/linux_x86:${LIBS32:+$LIBS32:}$repo_dir/build-linux
# XVFB=1: on a virtual display of its own (:${XVFB_DISPLAY:-99}, software
# OpenGL), silent, out of the desktop's way; keys can be sent to it with
# DISPLAY=:99 xdotool. The display is stopped with the game.
if [ -n "$XVFB" ]; then
    Xvfb :${XVFB_DISPLAY:-99} -screen 0 1360x768x24 -nolisten tcp >/dev/null 2>&1 &
    xvfb=$!
    sleep 1
    export DISPLAY=:${XVFB_DISPLAY:-99} SDL_AUDIODRIVER=dummy
fi
# Its own process group: the game forks helpers (its monitor server), which
# outlive it when it crashes; they are stopped with it.
set -m
LD_PRELOAD=$repo_dir/build-linux/linuxloader.so /lib/ld-linux.so.2 ./WMN4r "$@" >"$LOG" 2>&1 &
pid=$!
for i in $(seq ${T:-60}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 -- -$pid 2>/dev/null; wait $pid 2>/dev/null
[ -n "$xvfb" ] && kill $xvfb 2>/dev/null
echo "stopped; log: $LOG"
