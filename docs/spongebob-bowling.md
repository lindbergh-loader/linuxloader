# SpongeBob SquarePants Bikini Bottom Bowling (ICE / PlayMechanix)

Native support targets the v1.05 bundle's `game` executable, EXE v01.01.02,
built July 9, 2007. Native startup, gameplay, the desktop controls and
fullscreen were confirmed on 2026-11-10. The Batocera cabinet setup still
needs validation.

## Launch

Keep the untouched `game`, `sbbdata/`, `sbbuser/` and `env/` in the game
directory. No Wine, TeknoParrot or dongle recording is needed. The native
game links 32-bit GL, GLU and GLUT; `LIBS32` can provide libraries the host
does not carry.

```sh
GAME_DIR='/path/to/SpongeBob SquarePants Bikini Bottom Bowling' \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/runsbb.sh
```

For fullscreen, put this in the game directory's `linuxloader.ini`:

```ini
[Display]
WIDTH = 640
HEIGHT = 480
FULLSCREEN = 1
KEEP_ASPECT_RATIO = 1

[Input]
INPUT_MODE = 1
```

GLUT game mode is replaced with a regular window at the configured size and
fullscreen follows `[Display] FULLSCREEN`; the 4:3 picture is scaled to it.

## Rockey boot envelope

The ELF carries the same Rockey envelope as Big Buck Hunter Pro: its entry
at `0x080dd3d0` talks to `/dev/rockey`, decrypts the code with a fixed
32-bit XOR mask and forks a dongle watchdog. The envelope's own data gives
the encrypted region, `0x5b930` bytes from `0x0804ac90` (up to `.fini`).

The launcher identifies the original by full CRC32 `0xf96d95cc`, decrypts
that region in a cached copy with mask `0xc79b25eb`, starts it at the
restored `_start` and clears the Rockey marker from the ELF identification
padding. The prepared copy's partial CRC is `0x0333f6fa`. The original
executable remains unchanged.

## Cabinet input and paths

The game uses the g3 JAMMA board API: the profile hooks `JammaPoll`
(`0x0804c7d0`) and `JammaOp` (`0x080a2c04`), hands the game the emulated
switch counters and stubs `JammaLoop` so the absent serial board is not
reset over and over. Its switch table is Big Buck Hunter Pro's.

The trackball is two more board counters, 15 (x) and 16 (y): the game's
poll posts their changes as events `0x40`/`0x41`, which add the signed
counts to its trackball position (its menus move past 5 counts a frame).
The loader adds to them:

- Desktop: the mouse's motion over the window (the pointer is put back at
  the window's centre), and the first SDL gamepad's left stick as a speed
  (40 counts a frame at full deflection, past a dead zone).
- Evdev: mapped mice (`EV_REL` X/Y), plus the SDL gamepad's stick.

Keys: 5/6 coin, 1/2 start, F1 service, F2 test, up/down volume (also the
test menu's navigation). Esc or Alt+F4 exits.

Absolute `/sbbuser` paths are redirected to `sbbuser/` in the game
directory. The game's data paths are already relative to that directory.
