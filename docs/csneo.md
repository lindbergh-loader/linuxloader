# Namco N2: Counter Strike NEO (csneo2)

State on 2026-10-06. The game boots to the PrepareMenu/attract state under the
full loader, and the display path gets **past** video init (the GLX visual
error is fixed) but is **blocked** by the Neo renderer's NVIDIA-only ADM
backend, so a visible window is not reachable on a non-NVIDIA host.

## What the game is

CSNeo is, unlike the Wangan N2 titles, **not** a Namco clSystemN2 app. The
cabinet runs Valve's GoldSrc HLDS: `hlds_amd` (a stripped launcher, the one
TeknoParrot runs) dlopen's `engine_amd.so` (the client + dedicated-server
engine, the czero mod) from `csneo2/linux`, and the two talk over loopback.
TeknoParrot runs `hlds_amd` from `Contents2\csneo2\linux` with a 32-bit
`hlds_amd` DLL wrapper; on Linux we run the cabinet's own 32-bit
`hlds_amd`/`engine_amd.so` under the loader.

`engine_amd.so` is the **rendering** build: a full "Neo" OpenGL engine
(`Neo_RenderAll`, `CNeoOpenGLMaterialImpl`, the `NEO_CGFX_*` Cg shader path)
on SDL 1.2 + GL 1.x. There is also `hlds_i486`/`engine_i486.so`, a
**non-rendering** build (no OpenGL; the display is an external NVIDIA
"Alchemy" SystemModule). We drive `hlds_amd` because it has its own GL window.

Background: `docs/namcon2info.html` (saved N2 page), `docs/namco-n2-wmmt3.md`.

## Status

| | full loader (`MODE=full`) | display path (`MODE=display`) |
|---|---|---|
| Boot to PrepareMenu/attract | **OK** (headless) | OK |
| Video init (SDL `InitDraw`) | fails ("x11 not available") | **OK** (window/GLX context created) |
| Enters menu | OK | OK |
| Render loop / visible frame | — (no display) | **crash** (SIGSEGV, NVIDIA ADM) |

`MODE` is a knob in `scripts/linux/runcsneo.sh`:
- `full` (default): `LD_PRELOAD` the whole `linuxloader.so`. The loader's
  X11 interposers break the engine's dlopen'd X11/GLX stack
  ("x11 not available"), so it runs headless attract.
- `display`: **no** full loader — only the small display shim
  (`libs/csneo32/libcsneo_display.so`) + the SDL-1.2 wrapper already installed
  in `csneo2/lib`. Gets past video init but dies in the render loop.

## The display stack (32-bit)

`engine_amd.so → libSDL-1.2.so.0 (=sdl12-compat) → dlopen libSDL2-2.0.so.0
(=sdl2-compat) → dlopen libSDL3.so.0 (=real SDL3, does the X11/GLX work)`.
SDL3 dlopens `libGL.so.1`/`libX11.so.6`/`libEGL.so.1` at runtime (libGLVND),
so **LD_PRELOAD interposers of GLX/X11 are invisible** to it — it resolves
those via its dlopen handle, not the global scope. The 32-bit libs come from
`/home/aderumier/code/jp-native/lib32/`; the 32-bit interpreter is
`/usr/lib32/ld-linux.so.2` (`/lib/ld-linux.so.2` is broken on this host).

## Fixed this session

### 1. The "Couldn't find matching GLX visual" error (SDL wrapper)

The engine's `sdl_main` (0x65ec04) and `CMtSDL::InitDraw` (0x4467b4) set
`SDL_GL_SetAttribute(RED/GREEN/BLUE_SIZE, 5)` — the 15-bit Alchemy panel.
No Mesa GLX visual has 5-bit RGB, so `SDL_SetVideoMode` failed with
"Couldn't find matching GLX visual".

Fix: a wrapper `libSDL-1.2.so.0` (installed into `csneo2/lib/`) that
overrides `SDL_GL_SetAttribute` to **clamp sub-8-bit colour sizes (attrs
0-3) up to 8** and **drop STEREO** (attr 12), then forwards every needed SDL
symbol to the **real** SDL.

The real SDL is kept as `csneo2/lib/libSDL-1.2.real.so.0`. **Key gotcha:** its
SONAME inside the ELF is still `libSDL-1.2.so.0`, so linking it as a NEEDED
of the wrapper emitted a *self-referencing* `NEEDED` and `RTLD_NEXT` resolved
to nothing (`SDL_GetVideoInfo` → null → "Video query failed"). The fix is to
resolve the real SDL **by absolute path via `dlopen`** (in each forwarder),
which sidesteps the SONAME collision and the load-order race entirely.

Sources (32-bit, built with `gcc -m32`):
- `libs/linux_x86/sdl12_wrapper.c` — hand-written `SDL_GL_SetAttribute`
  override + a few explicit forwarders.
- `libs/linux_x86/gen_sdl_wrapper.sh` — generates `sdl12_wrapper_fwd.c`, a
  generic 6-arg i386-cdecl trampoline `long NAME(long×6)` →
  `dlsym(dlopen(real), name)` for each symbol in the list. The 6-arg shape is
  safe: i386 cdecl passes all args on the stack as 4-byte slots, so any real
  signature forwards correctly; the `long` return covers int/pointer/Uint32.
- `libs/linux_x86/build_sdl_wrapper.sh` — combines override + forwarders and
  builds/installs the wrapper. Re-run it if the symbol list grows.

The forwarded-symbol list is the union of SDL_ imports over
`engine_amd.so cs_amd.so client_amd.so filesystem_stdio_amd.so` + all
`csneo2/lib/*.so*` (59 symbols). If a new "undefined symbol: SDL_..." appears,
add it to that union and rebuild.

### 2. The cabinet `libGLU.so.1` breaking GLX (dump-local, not in repo)

The engine NEEDs `libGLU.so.1` (RPATH `../lib` = `csneo2/lib/`). The cabinet's
real GLU references GL entry points as undefined symbols; loading it grabs an
early/broken GL context and breaks SDL3's later GLX visual lookup. **Dump
local fix:** replaced `csneo2/lib/libGLU.so.1` with a stub
(`libs/linux_x86/glu_stub.c`) providing only the 5 symbols the engine imports
(`gluErrorString, gluBuild2DMipmaps, gluLookAt, gluOrtho2D, gluPerspective`)
+ no-ops, no GL deps. Original backed up as `libGLU.so.1.cabinet.bak`.

### 3. Ancient host glibc + old zlib (dump-local, not in repo)

- `csneo2/lib/` shipped a **glibc 2.3.2** (`libc/libm/libdl/libpthread`) from
  the ~2007 cabinet. The engine only needs up to `GLIBC_2.3.2` and the system
  glibc (2.44) exports all of it, so the old glibc was unnecessary and was
  breaking SDL3's GLX/EGL backend. **Dump local fix:** moved to
  `csneo2/lib/oldglibc/`.
- Cabinet `libz.so.1` lacked `ZLIB_1.2.3.4` (needed by system `libpng16`)
  → "symbol lookup error: inflateReset2". **Dump local fix:** moved to
  `csneo2/lib/libz.so.1.cabinet.bak`; system zlib-ng is used.

These three dump-local changes live **outside the repo** (in the game dir) and
cannot be committed; they are documented here so a fresh dump can be set up.
The display-path launch (no full loader) is `MODE=display bash
scripts/linux/runcsneo.sh`.

## The remaining blocker: the Neo renderer's NVIDIA ADM pixel buffer

With video init fixed, the engine enters the menu and then crashes in the
render loop with `SIGSEGV (si_addr=NULL)`. Root cause:

The Neo renderer's pixel buffer is **`CNeoPixelBufferADMImpl`** (NVIDIA's
"Alchemy Display Manager"). `Neo_CreatePixelBuffer` (0x3b3788) **always**
constructs `CNeoPixelBufferADMImpl` — there is no SDL/GLX/non-ADM fallback.
Its `init` (0x3b3000) calls `admChooseFBConfigi` / `admCreatePbufferi` /
`admCreateGraphicsContext`.

The ADM layer (in `engine_amd.so`'s `_nv000633adm` at 0x6f4f20, and in
`engine_amd.so`'s NEEDED `libambizdev-nsl.so` at 0x1b61df) sets up a
segment-relative data area by:
```
mmap(block) ; modify_ldt(LDT entry -> block) ; mov (addr<<3)|7, %gs ; use %gs:off
```
i.e. it treats a linear mmap address as a **segment selector** and builds a
GDT/LDT entry for it. That only works on the NVIDIA cabinet's kernel, which
populates the segment base accordingly. On a Mesa/X11/GLX host the segment
base is invalid, so the first `%gs:offset` dereference faults at **NULL**.
This is a hardware-backend mismatch, not a tunable:
- The engine's own `mprotect(RWX)` byte-patching does not work here (32-bit
  address-space exhaustion → ENOMEM), so we can't patch `CNeoPixelBufferADMImpl`
  or the ADM code in place.
- The faulting code lives in the stripped `engine_amd.so` + the cabinet
  `libambizdev-nsl.so`, not in the loader's interposable surface, so the
  loader cannot redirect it to the SDL/GLX window that `InitDraw` already
  created.

## Where a real fix would have to live

Interpose the **ADM layer** itself: provide a replacement
`libambizdev-nsl.so` (and match the engine's inlined `_nv000633adm` behaviour)
whose `admChooseFBConfigi` / `admCreatePbufferi` / `admCreateGraphicsContext`
map onto the SDL/GLX window that `CMtSDL::InitDraw` already created, so
`CNeoPixelBufferADMImpl` gets a real GL context instead of the cabinet ADM's.
That is a substantial new shim (the ADM API surface is large — see the
`adm*` symbols in `engine_amd.so`), and is the next thing to attempt if a
visible window on non-NVIDIA hardware is the goal.

## Launch (display path)

```
GAME="/home/aderumier/code/jurassicpark.wine/drive_c/game/CounterStrike Neo/csneo2/linux"
cd "$GAME"
DISPLAY=:0 XAUTHORITY=<current-xauth> \
LD_LIBRARY_PATH="/home/aderumier/code/jp-native/lib32:/usr/lib32:$GAME/../lib:$PWD" \
LD_PRELOAD=/home/aderumier/code/linuxloader/libs/csneo32/libcsneo_display.so \
/usr/lib32/ld-linux.so.2 ./hlds_amd -game czero -console -norestart
```
Or `MODE=display bash scripts/linux/runcsneo.sh` (sets the same env). Notes:
- `XAUTHORITY` changes each XWayland session: `ls /run/user/1000/.mutter-Xwaylandauth.*`.
- The game dir path has a **space** — quote it.
- Do **not** use `timeout` (the 64-bit binary breaks the 32-bit LD_PRELOAD);
  the script caps runtime with a background process + `kill`.

## Diagnostics

- `strace -f` (64-bit strace works on 32-bit children): shows the last syscalls
  before the crash — the tell-tale is `modify_ldt(...)` right before
  `SIGSEGV si_code=SI_KERNEL si_addr=NULL`.
- `libs/linux_x86/segv_dump.c` → `libs/csneo32/libsegv_dump.so`: a 32-bit
  SIGSEGV register dumper (interposes `sigaction`/`signal` to chain before the
  engine's own handler). `LD_PRELOAD="...libcsneo_display.so ...libsegv_dump.so"`,
  crash writes `/tmp/csneo_segv.txt` (+ `_bt.txt`). Useful for confirming the
  ADM/`%gs` fault.
- The engine installs its own SIGSEGV handler (the `modify_ldt` safe-stack
  seen in strace is its crash-reporting setup), so a plain constructor-installed
  handler is overridden — hence the `sigaction` interposition in segv_dump.c.

## References

- `src/loader/namco/namcoN2Csneo.c` — the loader hooks (shim preloading, hasp
  login, card reader, cabinet-management channel).
- `src/loader/graphics/x11Bridge.c` — `isNamcoN2Game()` XOpenDisplay passthrough.
- `src/loader/redirections/filesystemShared.c` — X11-socket/Xauthority early-returns.
- `scripts/linux/runcsneo.sh` — `MODE=full` / `MODE=display` launch.
- `csneo.net/download/` — TeknoParrot runs `hlds_amd` from
  `Contents2\csneo2\linux`.
- Maps: `czero/maps/` (`neo_00collision`, `neo_01..12collision`). Console
  (ALT+F7): `login 12`, `map neo_06collision`, `bot_add`.
