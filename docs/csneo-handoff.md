# CSNeo display — HANDOFF to Claude (2026-10-06)

Goal: get Counter Strike NEO (Namco N2, csneo2) to **render to a visible
window** on a non-NVIDIA (AMD/Mesa) host. Boot-to-attract (headless) already
works and is committed. This doc is a self-contained handoff with all the
disassembly evidence and the concrete remaining fix.

## TL;DR

- Video init is **fixed** (the SDL wrapper works; the engine creates a real
  GLX window + GL context). See `docs/csneo.md` for that whole story.
- The engine then enters the menu and crashes (`SIGSEGV si_addr=NULL`) in the
  Neo renderer's pixel-buffer setup.
- **Root cause (confirmed by disassembly):** the Neo renderer's pixel buffer
  is `CNeoPixelBufferADMImpl`, backed by NVIDIA's ADM API. It calls
  `SDL_ADM_GetDevice()` to get an ADM device. Our display shim returns **NULL**
  for that, so every `adm*` call returns 0 (NULL pbuffer + NULL GL context).
  The engine then calls `admMakeContextCurrent(NULL)` and does GL on a NULL
  context → NULL deref.
- **The fix:** make the ADM layer return a **valid device + pbuffer + GL
  context backed by the real GLX window** (the one `CMtSDL::InitDraw` already
  created). The engine only uses a handful of `adm*` functions, all of which
  can be wrapped over a normal GLX context. This is the remaining work.

## Repo / build

- Repo: `/home/aderumier/code/linuxloader`, branch `rawthrills`, 32-bit `-m32`
  builds. Build: `cmake --build /home/aderumier/code/linuxloader/build-linux
  --target linuxloader -j4`.
- Host: AMD GPU (NOT NVIDIA), glibc 2.44, XWayland (`DISPLAY=:0`,
  `XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.*` — re-check each session).
- Recent commits: `dc64d72` (docs/csneo.md), `189892a` (hlds_amd switch +
  display shims), `d1e8fe5` (boot to attract).

## Game dir (outside repo, has a SPACE in the path)

```
GAME="/home/aderumier/code/jurassicpark.wine/drive_c/game/CounterStrike Neo"
# engine launcher:  $GAME/csneo2/linux/hlds_amd
# engine + mods:    $GAME/csneo2/linux/engine_amd.so  (stripped, full .dynsym)
# engine libs:      $GAME/csneo2/lib/   (RPATH "../lib" = this dir; beats LD_LIBRARY_PATH)
```

32-bit SDL/compat libs: `/home/aderumier/code/jp-native/lib32/`.
32-bit interpreter: `/usr/lib32/ld-linux.so.2` (`/lib/ld-linux.so.2` is broken).

## Display-path launch (no full loader) — WORKS up to the render crash

```
cd "$GAME/csneo2/linux"
DISPLAY=:0 XAUTHORITY=<xauth> \
LD_LIBRARY_PATH="/home/aderumier/code/jp-native/lib32:/usr/lib32:$GAME/csneo2/lib:$PWD" \
LD_PRELOAD=/home/aderumier/code/linuxloader/libs/csneo32/libcsneo_display.so \
/usr/lib32/ld-linux.so.2 ./hlds_amd -game czero -console -norestart
```
Or `MODE=display bash scripts/linux/runcsneo.sh`.
- Do NOT use `timeout` (64-bit binary breaks the 32-bit LD_PRELOAD).
- `libcsneo_display.so` is built from `libs/linux_x86/csneo_display.c`:
  `gcc -m32 -shared -fPIC -o libs/csneo32/libcsneo_display.so
  libs/linux_x86/csneo_display.c -ldl`. It provides `SDL_ADM_GetDevice`→NULL
  and CgGL no-ops (the engine can't load without `SDL_ADM_GetDevice`).

## The SDL wrapper (already done — do not break)

Installed at `csneo2/lib/libSDL-1.2.so.0`. It overrides
`SDL_GL_SetAttribute` (clamps 5-bit RGB → 8-bit, drops STEREO) and forwards 59
SDL symbols to the real SDL. The real SDL is `csneo2/lib/libSDL-1.2.real.so.0`;
the wrapper resolves it **by absolute path via dlopen** (NOT RTLD_NEXT — the
real lib's SONAME is still `libSDL-1.2.so.0`, which would self-reference).
Sources: `libs/linux_x86/sdl12_wrapper.c`, `gen_sdl_wrapper.sh`,
`build_sdl_wrapper.sh`. Rebuild with
`bash libs/linux_x86/build_sdl_wrapper.sh /tmp/need_syms.txt` if the symbol
list grows. `/tmp/need_syms.txt` = the 59-symbol union (recompute from
`objdump -T` over the engine + mods + `csneo2/lib/*.so*` if missing).

Dump-local (NOT in repo, in `csneo2/lib/`): GLU stub (`libGLU.so.1`, original
`.cabinet.bak`), old glibc moved to `oldglibc/`, old `libz.so.1` →
`.cabinet.bak`. Documented in `docs/csneo.md`.

## THE FIX TO IMPLEMENT — a real ADM shim

### Where to put it
Extend `libs/linux_x86/csneo_display.c` (the `LD_PRELOAD`ed display shim) —
it's already loaded before the engine and is the right place to override
`SDL_ADM_GetDevice` and the `adm*` functions. The engine imports `adm*` and
`SDL_ADM_GetDevice` as PLT/UND symbols, and since the shim is `LD_PRELOAD`ed
(global scope) the engine's PLT resolves them to the shim.

### The ADM device / handle "magic" tags (from disassembly)
Handles are validated by a 4-char magic in their first DWORD:
- **Device**: `0x49564544` = "DEVI" (`cmpl $0x49564544, 0(%device)`)
- **FBConfig**: `0x46434246` = "FBCF" (checked by `admCreatePbufferi` at the
  FBConfig arg: `cmpl $0x46434246,(%fbconfig)`, and the FBConfig's 2nd DWORD
  (offset 4) must == the device pointer).
- **Pbuffer (drawable)**: `0x46554250` = "PBUF" (`admCreatePbufferi` writes
  `movl $0x46554250,(%esi)` at 0x6c91b8; offset 4 = device pointer).
Every `adm*` function does `test %arg,%arg; je ret0` then a magic check; on
failure it **returns 0** (does NOT crash). So a NULL device → all adm calls
return 0 → downstream GL-on-NULL-context crash.

### Exact ADM call sequence the pixel buffer uses
`CNeoPixelBufferADMImpl::init` (engine_amd.so 0x3b3000):
```
SDL_ADM_GetDevice()          -> device  (store at [this-0x824])
memset(0, 0x800)             (local FBConfig query buffer)
admChooseFBConfigi(device, <query...>, <countout>, <fbconfigs out>)
                              returns count (n>=1 to continue)
memset(0, 0x800)
admCreatePbufferi(device, <pbuffer out ptr>, <numAttribs>, <attribs...>)
                              returns pbuffer handle (magic "PBUF")
admCreateGraphicsContext(device, <context out ptr>)
                              returns GL context handle
admGetDrawableAttribi(pbuffer, ATTR, &val)   (x2)
admGetCurrentContext()
```
If any of the three creates returns 0 → the code hits `Sys_Error` paths
(but the observed crash is a raw SIGSEGV, so a handle is created-but-NULL or
used while NULL).

`CNeoPixelBufferADMImpl::activate` (0x3b343c):
```
admGetCurrentContext()
admGetCurrentDrawDrawable()
admGetCurrentReadDrawable()
admMakeContextCurrent(<ctx>, <draw>, <read>)
admMakeContextCurrent(...)
```
`CNeoPixelBufferADMImpl::bind` (0x3b35d8) — **this is where GL is actually
used on the ADM context**:
```
admGetCurrentContext()
admGetCurrentDrawDrawable()
admGetCurrentReadDrawable()
admMakeContextCurrent(ctx, draw, read)
glGetIntegerv(...)
glReadBuffer(...)
glCopyTexImage2D(...)     <-- copies GL back buffer into a texture
glReadBuffer(...)
admMakeContextCurrent(...)
```
So the ADM context the engine uses for these GL calls **must be a real,
current GLX context** — the same one `CMtSDL::InitDraw` created (SDL_GL
window, 1024x768 or the menu size).

### Concrete implementation sketch
In `csneo_display.c`, maintain module state:
```c
static void *g_admDevice;      // static struct { int magic="DEVI"; ... }
static void *g_admPbuffer;     // static struct { int magic="PBUF"; void *device; ... }
static void *g_glXContext;     // the real GLX context from the SDL window
static void *g_glXDrawable;    // the SDL window's X visual info / drawable
```
- `SDL_ADM_GetDevice()` → return a pointer to `g_admDevice` (magic "DEVI").
  Do NOT return NULL.
- `admChooseFBConfigi(device, attrs, count, fbconfigs)` → fill `*count=1`,
  `*fbconfigs` points to a static struct with magic "FBCF" and offset4=device.
  Return 1.
- `admCreatePbufferi(device, pbufOut, nAttribs, attribs[])` → set
  `g_admPbuffer.magic="PBUF"`, `g_admPbuffer.device=device`, copy the attrib
  values (width/height) into the struct; `*pbufOut=&g_admPbuffer`; return it.
- `admCreateGraphicsContext(device, ctxOut)` → **obtain the real GLX context**.
  This is the tricky part: the context belongs to the SDL window. Options:
  (a) if `glXGetCurrentContext()` is already non-NULL when this is called
  (likely, since InitDraw made it current), capture it here:
  `g_glXContext = glXGetCurrentContext(); *ctxOut = g_glXContext;` return it.
  (b) If the context isn't current yet, get the X display + drawable from
  `glXGetCurrentDisplay()`/`glXGetCurrentDrawable()`, or open the display via
  the SDL window. The engine's `admMakeContextCurrent` will make it current
  before GL, so storing the real GLXContext handle is enough.
- `admMakeContextCurrent(ctx, draw, read)` → translate: if `ctx==g_glXContext`,
  call `glXMakeCurrent(display, drawable, g_glXContext)`. Return 0 (success).
  (The engine passes draw/read drawables; for a single-window setup, make the
  current GLX context current on the current drawable.)
- `admGetCurrentContext()` → `return g_glXContext;`
- `admGetCurrentDrawDrawable()` / `admGetCurrentReadDrawable()` → return the
  GLX drawable (the SDL window's X window id, or `glXGetCurrentDrawable()`).
- `admGetDrawableAttribi(pbuf, attr, &val)` → answer sensible values for the
  width/height attrs the pixel buffer asks (1024x768 or the window size); 0
  otherwise.
- `admDestroyPbuffer` / `admDestroyContext` / `admFree` → no-op (or
  `glXDestroyContext` if we created one; prefer no-op since we're reusing the
  SDL context).
- `admSwapBuffers` → `glXSwapBuffers` on the current drawable (or
  `SDL_GL_SwapBuffers`).
- All other `adm*` (cursor, monitor, suspend, etc.) → no-op returning 0/1.

### The one hard part
Tying `admCreateGraphicsContext`/`admMakeContextCurrent` to the **existing
SDL/GLX context** rather than creating a second one. The safest approach:
capture `glXGetCurrentContext()`/`glXGetCurrentDisplay()`/
`glXGetCurrentDrawable()` when `admCreateGraphicsContext` runs (the SDL window
context should be current at that point, right after `InitDraw`). If those are
NULL at that moment, fall back to opening the display (`XOpenDisplay(NULL)`)
and querying the window — but prefer the "already current" capture.
Link the shim with `-lGL -lGLX -lX11` (32-bit) so these are available.

### Build
```
gcc -m32 -shared -fPIC -O1 -g \
  -o libs/csneo32/libcsneo_display.so \
  libs/linux_x86/csneo_display.c \
  -ldl -lGL -lGLX -lX11
```
(Keep the existing CgGL no-ops + `SDL_ADM_GetDevice` in the same file.)

## Verification loop
```
cd "$GAME/csneo2/linux"
DISPLAY=:0 XAUTHORITY=<xauth> \
LD_LIBRARY_PATH="/home/aderumier/code/jp-native/lib32:/usr/lib32:$GAME/csneo2/lib:$PWD" \
LD_PRELOAD=/home/aderumier/code/linuxloader/libs/csneo32/libcsneo_display.so \
/usr/lib32/ld-linux.so.2 ./hlds_amd -game czero -console -norestart
```
Success = a window appears and the menu/scene renders (no `si_addr=NULL`
crash, `init:PrepareMenu` no longer the last line, and `bind`/`activate`
complete). Watch for the NEXT wall: the Cg shader path may then need the CgGL
shim to correctly report "not supported" so the engine's GLSL fallback runs —
that's already in the shim, but re-verify once the ADM context is real.

## Diagnostics
- `strace -f` (64-bit strace works on 32-bit children). Crash tell-tale:
  `SIGSEGV si_code=SI_KERNEL si_addr=NULL` right after a
  `modify_ldt(17, {entry_number=666, base_addr=0x..., limit=0x000200, ...})`.
  NOTE: that `modify_ldt` is a **thread/stack bootstrap** (limit 0x200=512
  bytes, a data segment), NOT a crash-handler safe stack. The engine installs
  handlers for SIGINT/SIGTERM/SIGRT_1 but **NO SIGSEGV handler**.
- `libs/linux_x86/segv_dump.c` → `libs/csneo32/libsegv_dump.so`: 32-bit
  SIGSEGV dumper that interposes `sigaction`/`signal` to chain before any
  engine handler. **It is currently not dumping** (only the `(watch)`
  constructor marker appears) even though the engine has no SIGSEGV handler —
  worth a quick look: the interposed `sigaction`/`signal` wrappers may be
  clobbering the install, or the fault is on the bootstrap thread before the
  handler's `write`. Fixing it would give the exact faulting EIP, which would
  confirm the GL-on-NULL-context theory. (Low priority — the disassembly
  above already pins the mechanism.)
  - Test harness: `gcc -m32 -o /tmp/crash32 /tmp/crash32.c`
    (`/tmp/crash32.c` = `int main(){int*p=0;return*p;}`), then
    `LD_PRELOAD=.../libsegv_dump.so /usr/lib32/ld-linux.so.2 /tmp/crash32`
    should write `/tmp/csneo_segv.txt` with `eip=...`.
- `objdump -d --start-address=0x... --stop-address=0x... engine_amd.so` for
  the disassembly. Key addresses:
  - `CNeoPixelBufferADMImpl::init` 0x3b3000
  - `CNeoPixelBufferADMImpl::activate` 0x3b343c
  - `CNeoPixelBufferADMImpl::bind` 0x3b35d8
  - `Neo_CreatePixelBuffer` 0x3b3788 (always builds the ADM impl; no fallback)
  - `admChooseFBConfigi` 0x6c3940, `admCreatePbufferi` 0x6c9160,
    `admCreateGraphicsContext` 0x6bc720, `admInitDevicei` 0x6bf8a4
  - `sdl_main` 0x65ec04, `CMtSDL::InitDraw` 0x4467b4,
    `CMtSDL::Initialize` 0x44694c

## Pitfalls (learned)
- `mprotect(RWX)` on engine code FAILS (32-bit ENOMEM). Can't byte-patch the
  engine; the fix MUST be a preloaded shim overriding the `adm*` /
  `SDL_ADM_GetDevice` symbols, not an engine patch.
- LD_PRELOAD cannot intercept the engine's GLX/X11 calls that SDL3 makes
  (SDL3 dlopens them); but the `adm*` and `SDL_ADM_GetDevice` symbols ARE
  resolved from global scope (the shim's), so overriding them works.
- `O_CREAT` is `0x40` on i386 (not 2); `O_WRONLY|O_CREAT|O_TRUNC` for opens.
- The `modify_ldt` `entry_number=666` and the literal `666` are red herrings;
  it's a per-thread 512-byte data segment the ADM/engine bootstrap sets up.
- Benign offline-cabinet noise: `_stat ... failed!!!`, `sendApdu(): No such
  host`, `scandir failed:.../SAVE` — don't chase.
- Don't break the committed attract path (`MODE=full`) or the i486 baseline.

## Files that are the "fix" surface (edit these)
- `libs/linux_x86/csneo_display.c` — ADD the ADM shim (device/pbuffer/context
  structs + the `adm*` wrappers over the GLX context). Primary work.
- `libs/linux_x86/gen_sdl_wrapper.sh`, `build_sdl_wrapper.sh`,
  `sdl12_wrapper.c` — the SDL wrapper (already done; only touch if a new SDL
  symbol is undefined).
- `scripts/linux/runcsneo.sh` — `MODE=display` launch (already documents both).
- `docs/csneo.md` — update the "Where a real fix would have to live" section
  with the outcome once the ADM shim works (or document the next wall).

## Open questions to resolve while implementing
1. Is the SDL/GLX context **current** at the moment `admCreateGraphicsContext`
   is called? (If yes, `glXGetCurrentContext()` capture is trivial. Add a
   quick `fprintf(stderr,...)` in the shim to confirm at runtime.)
2. Which `admGetDrawableAttribi` attr IDs does the pixel buffer ask, and what
   values does it need (width/height/pixel format)? Read the two call sites in
   `init` (0x3b3000..0x3b3300) — the attr constants are in the `mov` immediates
   just before the calls.
3. After the ADM context works, does the CgGL shim correctly steer to the
   GLSL fallback, or does the Neo renderer need more (e.g. the Alchemy
   texture pipeline)? Expect to possibly hit another NVIDIA-specific path.
