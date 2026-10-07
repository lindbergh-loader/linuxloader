# Namco ES1 (Tank! Tank! Tank!) — porting notes

State: **2026-10-01.** Boots, attract and gameplay on the desktop (Mesa,
confirmed on screen): upright and centred in a landscape window, sound,
saves, keyboard controls. **2026-10-26:** played on Batocera with a pad,
pedals and fire buttons confirmed. The camera (the players' photos) is not
done yet.

- Dump: `<games>/TANK! TANK! TANK!/`, the cabinet's
  `/opt/arcade/exec` (`info`: `tank_us_100226_rev1.03.14`). A scene release:
  `tank_emu.c`/`tio_emu.c` (preloads writing the inputs into memory, not
  used) gave the dongle addresses and hints.
- `n_tank_release`: 32-bit, **stripped**, X11/GLX (GLX 1.3, XF86VidMode),
  `/lib/ld-linux.so.2`. Loader id **`0xd7270b7d`** (`TANKTANKTANK_ES1`),
  file CRC `0x66136ee9`; the launcher knows the name `n_tank_release`.
- Desktop run: `scripts/linux/runtank.sh` (windowed; `[Display]
  FULLSCREEN` for fullscreen).

## Booting, in order

1. **GL entry points.** The game makes its context current with
   `glXMakeContextCurrent` (GLX 1.3), which the loader did not interpose:
   glad stayed unloaded and the first `glViewport` jumped to 0. It loads
   glad now, as `glXMakeCurrent` does; `glXGetCurrentDisplay` passes
   through for games with their own window.
2. **Sound**: the same `nsAdrv.dll` interface as the Dead Heat games
   (`nsAdrv_init(…, "surround51", 6, 32, 48000, 256, …)`): the loader's
   SDL3 driver (`soundEmulation`).
3. **Frame pacing**: the game divides by the refresh period it computes
   from `XF86VidModeGetModeLine`, which the loader answered with nothing
   (SIGFPE). It passes through for games with their own window.
4. **Dongle**: the game's own layer (stripped; the scene's names):
   `0x80ac440` init, `0x80ac3a0` read-and-decode of the 64-byte dongle
   memory (a serial), `0x80ac2e0` use, `0x80ac340` presence — answered.
5. **Saves**: the game writes under `/opt/arcade/exec/` (`save00/`,
   `scoredata/`, `rankdata/`): mapped to the game's directory
   (`rootPath`, `namcoEs1RedirectPath`), the directories made on first use.
   A first boot reports "test mode settings have been reset": F2, F2.
6. **3D: NVIDIA assembly programs.** All 1412 of its programs are Cg's
   NVIDIA profiles (`OPTION NV_vertex_program3`, `NV_fragment_program2`),
   which Mesa rejects: the 3D drew white. `namcoEs1ArbProgram.c` rewrites them
   as plain ARB programs in a `glProgramStringARB` interposer:
   - vertex: `_SAT` expanded (MAX/MIN), the four-component `A0` mapped on
     ARB's `A0.x` (ARL stores the floored value, each relative operand
     reloads it and is fetched into a temporary);
   - fragment: precision suffixes and `SHORT` dropped, the condition code
     a temporary, conditional writes (`GT/NE/EQ.x`) and nested `IF`s (up
     to 15 deep) made `CMP` selects, the constant `LOOP` unrolled, outputs
     temporaries copied at `END`.
   All 1412 load in Mesa (checked offline against the dumped programs).

## Display

The game is portrait, made for a landscape monitor turned on its side: by
default ("rright") it draws a landscape 1360x768 frame with the picture
rotated in it, fullscreen. (Its upright mode, rotation 0, draws a
portrait frame its landscape screen size crops.) The loader:

- sets the fullscreen default (`movl $1,-0x74(%ebp)`, `0x804ff20`) from
  [Display] FULLSCREEN;
- opens its window at [Display] WIDTH x HEIGHT;
- scales its frame into it (`graphics/frameScale`, as the Raw Thrills
  games): framebuffer 0 an offscreen 1360x768 one, turned a quarter turn
  back (`frameScaleSetRotation`, new: the frame drawn as a textured quad)
  and fitted upright in the middle; with ROTATE_VERTICAL (a turned
  monitor) shown as it is.

## Controls

The JVS board (NA-JV, `/dev/ttyS2`); the game polls switches, coins and 3
analog channels: 0 steering, 1 left pedal, 2 right pedal. Each is
normalised (`raw / 65536`, the steering inverted) and calibrated per axis
(`0xed7da20 + 0x30 + axis * 0x3c`: inputs x0 < x1 <= x2 < x3, outputs
y0/y1/y2; channels to axes 2, 4, 5). The loader puts its steering, gas
(the left pedal) and brake (the right pedal, as played) through the
inverse of that calibration (`axisCalibrate`); an axis without a range on a
side (the steering's, captured at an end by a settings reset) gets the
symmetric calibration of the other axes.

The switches (the input device's JVS reader, `0x80ae880`: P1's switch word,
bits as the loader's): Enter P1 button 1, the safety button P1 button 2,
the fire buttons P1 left (L) and right (R), menu up/down P1 up/down, start.
The game turns them into its trigger byte (`0xed80bd9`: L `0x10`, R `0x08`,
safety `0x20`), as the scene release's `tio_emu.c` wrote it directly.

Desktop keys: ←/→ steer, ↑ gas, ↓ brake, space P1 button 1 (Enter),
Z/X/C/V buttons 2-5 (Z the safety), 1 start, 5 coin, F1 service, F2 test;
the arrows being the steering, the fire buttons have no key.

On Batocera the generator (kind `tank`) maps the left stick or wheel, R2
gas, L2 brake, L1 or x (the top face button, Xbox Y) fire L, R1 or y (the
left one, Xbox X) fire R, a (right) Enter, b (bottom) the safety button,
up/down the menus.

## Open items

1. The camera ("look at the camera above"): a webcam is the camera (the
   boot's check passes). Without one (Batocera), the boot's "Camera
   Checking" waited forever on the nmUVCCamera driver (state `0xf098ee0`,
   9 = done): the game's camera flag (`0x87e0ac6`, 1 in the binary) is
   cleared, so it boots and plays as a cabinet without a camera.
2. ~~The triggers and the safety button: which JVS bits.~~ Done (see
   Controls).
3. ~~A Batocera generator kind.~~ Done (`tank`).
