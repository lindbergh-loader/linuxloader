# Namco N2: Wangan Midnight Maximum Tune 3DX+ (and 3)

State on 2026-10-02, and the rendering investigation still open.

## Status

| | Mesa (AMD, desktop) | NVIDIA (Batocera, RTX 2070 SUPER, 590) |
|---|---|---|
| Boot, network check, attract | OK | OK |
| Attract demo and race (track, cars) | OK | OK |
| Car selection: car bodies | **missing** (black/transparent, interior visible) | OK |
| Input | keyboard (no config) | keyboard without controller; pads through evdev |
| Picture size | scaled to the window | scaled to the window (4:3, bars) |
| Sound | OpenAL Soft (none on the desktop: no 32-bit one with its dependencies) | OK (OpenAL Soft 1.24) |

The export WMMT3 (WM3100) works too (user-tested, 2026-10-02), steering force
feedback included (`namco/namcoFfb.c`). Its dump's `lib/` must hold real
copies of the Boost libraries under their `-1_33_1.so.1.33.1` names: copied
from `libso/`, those are symlinks saved as text files (the target's name),
and the dynamic linker stops on them ("file too short").

## Fixed (uncommitted at the time of writing)

- **Grey, flickering screen (Mesa).** The full-screen effect programs (glow,
  blur, flares, yuv: Cg's vp40) read their texture coordinates as
  `vertex.attrib[8..11]`. NVIDIA aliases generic attributes with the
  conventional ones (2 normal, 3 colour, 8-15 texcoords); Mesa does not, so
  every pixel read texel (0, 0). `aliasAttributes()` in `namcoN2Graphics.c`
  rewrites them to `vertex.texcoord[n]` (etc.) before Mesa sees the program.
- **Unwritten program outputs (Mesa).** `blur.fp` writes only alpha; the
  rewritten NV programs now start their output temporaries at 0, as NVIDIA's
  (`namcoEs1ArbProgram.c`).
- **Crash on Batocera** ("Failed to Map the address", SIGSEGV). The game's
  linked-in NVIDIA nForce OpenAL opens `/dev/dsp` and maps the cabinet's APU
  through it; Batocera has a `/dev/dsp`, the desktop has not. N2 games get
  `/dev/dsp` as absent (`namcoN2RedirectPath`).
- **"Cannot send/receive the ghost data, restart all linked cabinets"**
  (Batocera, 300 s countdown frozen, cannot be skipped). The game hears its own
  multicast announcement (225.0.0.1) on a wired eth0 and links with itself.
  `setsockopt` is interposed: N2 multicast sockets get `IP_MULTICAST_LOOP` 0.
  The interface report also says the link is down for N2 games (the fork's
  `NETWORK_ENABLED 0`; not enough on its own).
- **EmulationStation launch** ("No known game file found"): the launcher's
  game file list now has `main`. The launcher build was also broken by the
  Wheel of Fortune hooks (`rtWofInstall`/`rtWofOverride` now weak).
- **Picture size.** The game renders at 640x480 whatever the window: it
  draws into a 640x480 frame (graphics/frameScale, as the ES1 portrait games
  and Raw Thrills ones), scaled to the window at each swap.
- **Sound.** The game's linked-in nForce OpenAL entry points go to the
  host's OpenAL Soft (`namcoN2Audio.c`, the fork's n2Audio.cpp). Under
  Batocera it needs `XDG_RUNTIME_DIR` (EmulationStation sets it) to reach
  PipeWire.
- **Generator** (rgs repo, deployed): names with "wangan"/"maximum tune" are
  the `driving` kind; with no controller mapped, `INPUT_MODE` 1 (keyboard)
  instead of an empty evdev mode.

## Open

### Car bodies in car selection (Mesa only)

What the tests showed (desktop, `NAMCO_N2_TRACE` snapshots of 100 ms of draws):

- None of the 196 assembly programs is rejected by Mesa.
- No NVIDIA-only GL function is called (a per-entry-point call census).
- Framebuffers are always complete.
- Alchemy's own Cg 2.0 shaders (`-entry vmain/pmain`) never receive their
  matrices, on NVIDIA as well: they draw nothing on either driver. Painting
  them red showed nothing red.
- `car_blend.cg` draws the **wheels** (painting its fragment output red made
  the wheels red, bodies unchanged); its light parameters arrive correctly.
- The **bodies are fixed-function draws**: no program, lighting and colour
  material on, unit 0 only, `GL_COMBINE` modulate (`PRIMARY_COLOR x TEXTURE`,
  for colour and alpha), alpha test `GL_GREATER 0`, culling and depth on.
  - With alpha test, culling and texture off: bodies drawn, white.
  - With only the texture off: bodies white (some parts still transparent).
  - So the body texture samples black: its texture is DXT1 (`0x83f1`, one
    level, `GL_LINEAR`, complete); uploading it as opaque DXT1 (`0x83f0`)
    changed nothing; none of the 360 compressed uploads is empty.
  - Texture coordinates: unit 0's array is enabled, **half floats**
    (`GL_HALF_FLOAT` 0x140B, 2 components) at **offset 22 of a 26-byte vertex**
    in a VBO. Read back from the buffer they are valid (0..1); the texture
    matrix is the identity.
- The attract demo's race cars do have their painted bodies: the problem may be
  limited to the car-selection models (`data/car/*/normal/menu.igb`).

**Next lead:** half-float texture coordinates at an offset that is not 4-byte
aligned (22, stride 26). The normals in the same buffer work, but they may be
floats. To try: log every pointer's type/offset for the body draws; then copy
the texcoords into a float VBO (or set the texcoord pointer from a converted
copy) for half-float arrays, and see whether the bodies get their paint.

Tried and ruled out (not kept): capping `GL_TEXTURE_MAX_LEVEL` at the last
uploaded mipmap (the recoloured paint is uploaded down to 4x4 only, out of 11
levels), forcing `car_blend`'s alpha to 1, mapping `glProgramLocalParameters4fvNV`
to Mesa's EXT batch (never called), opaque DXT1.

## How the tests were run

- Desktop: `scripts/linux/runwmmt3.sh` (`T=<seconds>`, `GAME=3` for the
  export WMMT3), log in `/tmp/wmmt3.log`. `NAMCO_N2_TRACE=1` logs each
  program's first line and Mesa's errors.
- Batocera, by hand from ssh (EmulationStation does not pass the trace
  variables): in the game folder, `DISPLAY=:0.0`, `LD_LIBRARY_PATH` as the
  generator sets it plus the game's `lib/`, then
  `script -qfc "LD_PRELOAD=<loader>/linuxloader.so /lib/ld-linux.so.2 ./main" /tmp/wmmt3.log`.
- The game folder needs `lib/` with the Boost and Cg libraries of its own
  `libso/` (26 files) and `main` executable.
- The snapshot, call census, program dump and experiments (red outputs, opaque
  bodies, fixed-function switches) were debug code and are removed; they lived
  in `namcoN2Graphics.c` (wrappers on `glDrawArrays`/`glDrawElements`,
  `glProgramLocalParameter4*`, `glCompressedTexImage2D*`, a counting stub per
  GL entry point). Note: the N2 code maps `/tmp/...` into the game folder's
  `tmp/`, so debug output must go elsewhere (e.g. `~/.cache`).
