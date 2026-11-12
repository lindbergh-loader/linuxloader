# Temple Run (Coastal Amusements, FreeBSD)

Native support targets Temple Run 1.8.3-R (`tr`, build 483, February 15,
2014), Coastal Amusements' ticket redemption version of Imangi's game: a
32-bit **FreeBSD 9.1** executable. Played on the desktop (2026-11-12,
working). Started by its own program (below), not by the linuxloader;
Batocera's generator runs it (not played there yet).

## Running a FreeBSD game on Linux (`src/freebsd/`)

`linuxloader_freebsd <ELF> [arguments]` (`fbsdRunner.c`) does what FreeBSD's
rtld would: maps the ELF at its addresses, loads the host's libraries for its
own (SDL 1.2, GL, GLU, OpenAL, libpng 1.6 for its 1.5, zlib, libstdc++,
libgcc_s; glibc for its libc.so.7 and libm.so.5), binds its imports,
registers its C++ exception tables, runs its `_init` and `main`.

Where FreeBSD's ABI is not glibc's, shims (`fbsdLibc.c`): `open`'s flags,
`struct stat`, `__error`, `__assert`, `_init_tls`, termios (the serial
port's set-up), the pthread mutex and once (pointers on FreeBSD), `mount`
(the USB stick for updates: none), the disk's IDENTIFY ioctl (256 bytes),
`atexit`, libpng's version check.

C++ (`fbsdStdcxx.c`): the ABI is the same but FreeBSD's `basic_filebuf` is
larger (its `mbstate_t` is 128 bytes, its mutex a pointer). The game builds
its ifstreams and ofstreams inline: their filebuf is the host's, built in
place, and their vtables, VTTs and destructors are rebuilt with FreeBSD's
virtual base offsets. The game's copies of the library's objects
(`R_386_COPY`) are otherwise pointed at the library's own (old
`std::string` tells its empty representation by address).

## The cabinet

- Coastal's I/O board on `/dev/cuau0` (`fbsdCoastal.c`), stood in for at
  its bytes: reset, revision, events (four level switches, start, the menu,
  coins), the ticket dispenser, 32 KiB of NV memory (the game directory's
  `coastal.nv`), lamps and meters accepted.
- Its security check ("NuCrypt"): the board's 128-byte block, RC4'd with the
  disk's IDENTIFY data, must be `dot.tga`'s last 128 bytes; the board answers
  that trailer RC4'd back (with the game's own RC4) for the disk the shims
  make up.
- The monitor is portrait: the game draws rotated for it. On a landscape
  screen the runner turns each frame back before the swap (`FBSD_ROTATE`:
  `ccw`, `runtr.sh`'s default, or `cw`; empty leaves it), upright and
  pillarboxed, in the framebuffer sdl12-compat shows. In full screen the game
  asks for the desktop's size but draws a 1366x768 frame in its corner: that
  frame is what is turned, scaled to fit the whole window.

## The dump

`games/Temple Run/` (TeknoParrot's): `tr`, its data, `tr.cfg`. Added:
`lib/libopenal.so.0`, a 32-bit OpenAL Soft (Batocera's linuxloader package's:
the repository's `libs/linux_x86` one needs sndio). Run with
`scripts/linux/runtr.sh` (`GAMES_DIR`, `LIBS32` with the host's
`/usr/lib32` first). `tr.cfg` asks for full screen.

## Controls

| Cabinet | Desktop | Gamepad |
| --- | --- | --- |
| Trackball (lean = lane, flick = turn) | mouse | left stick or d-pad |
| Jump | up arrow or W | X |
| Slide | down arrow or S | A |
| Start | Return or `1` | Start |
| Menu | F2 | R3 |
| Coin | `5` | Back (also down) |
| Quit | Esc | |

The game plays with its trackball (`"trackball" = 1` in `tr.cfg`): the
mouse's motion (`g_mousedelta`, `0x0817969c`), a little sideways leaning
into the next lane, a flick turning at a junction, jumping or sliding. Its
own SDL events also give the arrows (turn, jump, slide); the board's four
level switches it ignores in play.

The keyboard reaches both the game's events and the board (SDL 1.2's key
state). Every gamepad (the game controller layout of the SDL2 under
sdl12-compat, all of them OR'ed: a light gun or a streaming host's virtual
device can be the first one; looked for again when the joystick count
changes): the left stick or the d-pad adds to the mouse's motion, axes
swapped and left-right reversed for the turned picture
(`COASTAL_TRACKBALL` counts a 60th of a second at full push, 12); X and A
(and Back) are pushed as the up and down arrow keys. `runtr.sh` sets
`SDL_JOYSTICK_HIDAPI=0`: SDL3's HIDAPI drivers blocked in libusb when a pad
came or went.

In window mode (`"fullscreen" = 0`) the game draws upright itself, 768x1366:
the runner leaves a portrait frame alone.

## Bezel

With the picture turned (`FBSD_ROTATE`), `bezel.png` in the game's
directory (`FBSD_BEZEL=<path>`, `0` for none) is drawn over the window and
the picture fitted in its hole, the transparent run of pixels through its
centre (the image's 1376x774, the hole 433x774 at 472,0).

## Batocera

The rom is the game's directory or a `.squashfs` of it (`arcadepc` or
`rawthrills`, the linuxloader emulator chosen for it). The generator (kind `templerun`,
from the rom's name) runs `linuxloader_freebsd ./tr` from it, with
`FBSD_ROTATE=ccw` (none with the rotate option: a monitor on its side), its
`lib/` first in the library path and sdl12-compat's `libSDL-1.2.so.0` from
beside the linuxloader (Batocera has no SDL 1.2), and `FBSD_BEZEL=0` with
the bezel option off. The pads come through SDL
(`SDL_GAMECONTROLLERCONFIG`, `SDL_JOYSTICK_HIDAPI=0`).

## Known issues

- No evdev input (the keyboard, the mouse and the SDL gamepads only).
- Three sound effects are missing from the dump (`ui_endmod`, `pupyay`,
  `checkpoint`).

## Debugging

- `FBSD_SHOT=<dir>`: a frame every `FBSD_SHOT_PERIOD` seconds (PPM).
- `FBSD_TRACE=1`: the last imports called, on a crash.
- `COASTAL_TRACE=1`: the board's commands and events; `COASTAL_EVENTS=seconds:byte,...`
  (hexadecimal) its events forced (`81` then `89` a coin, `41` start).
