#!/bin/bash
# Run Counter Strike NEO (Namco N2, GoldSrc HLDS) with linuxloader.so.
#
# The cabinet runs hlds_amd (a stripped launcher, the one TeknoParrot runs)
# from csneo2/linux; it dlopens engine_amd.so (the client + dedicated-server
# engine, the czero mod) from that directory. engine_amd.so is the rendering
# build: a full "Neo" OpenGL engine (NEO_CGFX_* Cg shader path) on SDL 1.2 +
# GL 1.x. The Cg path needs NVIDIA's Cg; on a non-NVIDIA GPU the Cg/GL bridge
# is the no-op shim in the loader's libs/linux_x86 (libCgGL.so), steering the
# shader init down the engine's own GLSL fallback.
#
# The engine's own libs (SDL 1.2, GLU, Cg, jpeg, z, iconv, boost, stlport, the
# cabinet's MurPort / ambizdev-nsl / axiscpp client) are in csneo2/lib and
# csneo2/linux. libCg.so (NVIDIA Cg core) comes from csneo2/lib; libCgGL.so
# is the loader's shim. Needs an X display (DISPLAY set).
# MODE=full   (default) LD_PRELOAD the whole linuxloader.so. Reaches the
#               PrepareMenu/attract state but the loader's X11 interposers
#               break the engine's dlopen'd X11/GLX stack ("x11 not
#               available"), so no window appears.
# MODE=display  Run WITHOUT the full loader: the display shim
#               (libs/linux_x86/csneo_display.c) + the SDL-1.2 wrapper in
#               csneo2/lib + NVIDIA's Cg 3.1 runtime. engine_amd.so embeds the
#               cabinet's NVIDIA GL driver (ADM); the shim keeps it from ever
#               running by binding the engine's gl* to the system libGL and
#               implementing the few adm* calls over GLX. Needs the dump's wads
#               repaired once with tools/csneo-fix-wads.py (see docs/csneo.md).
# See the header comment for background.
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
GAME_DIR=${GAME_DIR:-/home/aderumier/code/jurassicpark.wine/drive_c/game/CounterStrike Neo}
GAME=${GAME:-amd}
MODE=${MODE:-full}
LOG=${LOG:-/tmp/csneo.log}
LD32=${LD32:-/usr/lib32/ld-linux.so.2}   # 32-bit interpreter; /lib/ld-linux.so.2 is broken here
LIBS32=${LIBS32:-/home/aderumier/code/jp-native/lib32}
cd "$GAME_DIR/csneo2/linux" || exit 1

if [ "$MODE" = "display" ]; then
  # No full loader: the display shim (SDL_ADM_GetDevice->NULL + CgGL no-ops)
  # plus the SDL-1.2 wrapper already installed in csneo2/lib. The wrapper was
  # built by libs/linux_x86/build_sdl_wrapper.sh and resolves the real SDL
  # (csneo2/lib/libSDL-1.2.real.so.0) by absolute path via dlopen.
  export LD_LIBRARY_PATH=$LIBS32:/usr/lib32:$GAME_DIR/csneo2/lib:$PWD
  # Cg 3.1 (arbvp1/arbfp1 run on Mesa); preloaded so the engine's NEEDED
  # libCg.so/libCgGL.so bind to it instead of csneo2/lib (RPATH).
  cg=$repo_dir/libs/csneo32/cg
  [ -f "$cg/libCgGL.so" ] || unzip -q -o "$repo_dir/libs/linux_x86/Cg-3.1.zip" -d "$cg" || exit 1
  shim=$repo_dir/libs/csneo32/libcsneo_display.so
  if [ ! -f "$shim" ] || [ "$repo_dir/libs/linux_x86/csneo_display.c" -nt "$shim" ]; then
    gcc -m32 -shared -fPIC -O1 -o "$shim" "$repo_dir/libs/linux_x86/csneo_display.c" -ldl -lGL -lX11 -lm -lpthread || exit 1
  fi
  # A real GLU too: the engine builds its TGA mipmaps with gluBuild2DMipmaps,
  # and csneo2/lib/libGLU.so.1 may be the no-op stub (docs/csneo.md).
  preload="$shim $cg/libCg.so $cg/libCgGL.so $LIBS32/libGLU.so.1"
  # Axis C++ (the cabinet DB client) reads $AXISCPP_DEPLOY/etc/axiscpp.conf;
  # the dump's etc/axiscpp.conf points at /app/mnt/contents2 paths, and
  # without a parser/transport the DB thread calls a NULL factory. Point it
  # at the dump's own libs (through symlinks: the game path has a space).
  export AXISCPP_DEPLOY=/tmp/csneo-axis
  mkdir -p $AXISCPP_DEPLOY/etc $AXISCPP_DEPLOY/lib
  ln -sf "$GAME_DIR/csneo2/lib/libaxis_xercesc.so.0" $AXISCPP_DEPLOY/lib/
  ln -sf "$GAME_DIR/csneo2/lib/libaxis2_transport.so.0" $AXISCPP_DEPLOY/lib/
  printf 'XMLParser:%s\nTransport_http:%s\n' $AXISCPP_DEPLOY/lib/libaxis_xercesc.so.0 \
    $AXISCPP_DEPLOY/lib/libaxis2_transport.so.0 >$AXISCPP_DEPLOY/etc/axiscpp.conf
  echo "starting (display) $GAME_DIR/csneo2/linux/hlds_$GAME  (log: $LOG)"
  DISPLAY=${DISPLAY:-:0} LD_PRELOAD="$preload" $LD32 ./hlds_$GAME -game czero -console -norestart >"$LOG" 2>&1 &
else
  export LD_LIBRARY_PATH=$PWD:$GAME_DIR/csneo2/lib:$repo_dir/libs/linux_x86:${LIBS32:+$LIBS32:}$repo_dir/build-linux
  echo "starting (full loader) $GAME_DIR/csneo2/linux/hlds_$GAME  (log: $LOG)"
  LD_PRELOAD=$repo_dir/build-linux/linuxloader.so $LD32 ./hlds_$GAME -game czero -console -norestart >"$LOG" 2>&1 &
fi
pid=$!
for i in $(seq ${T:-40}); do sleep 1; kill -0 $pid 2>/dev/null || break; done
kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
echo "stopped after ${T:-40}s cap; log: $LOG"
