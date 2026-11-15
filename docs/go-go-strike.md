# Go Go Strike (Paokai)

Native support targets Go Go Strike (`bowling_usa`, its `BOWLING E.1.0.0`),
Paokai Electronic Enterprise's trackball bowling game (2004, the data's
logo and paths say WMH, `w040101`): a 32-bit Linux Allegro 4.0 game. Played
on the desktop (2026-11-15, working), windowed and full screen through
gamescope (`gamescope -W 1920 -H 1200 -w 640 -h 480 -f -S fit -- ...`).

## The dump

`games/Go Go Strike/`: `bowling_usa`, `img/` (the Allegro datafiles, fonts,
the company movie), an empty `bowling.log` and `alleg40.dll` (Windows
Allegro 4.0.2: unused).

- Executable: `bowling_usa`, file CRC 0x5356b0a1, loader ID 0xa6ac3b6c. Its
  dynamic symbols name the game's functions (`vKeyIoIsr`, `lpSramDeviceOpen`,
  ...). It lost its executable mode: it runs through the host's dynamic
  linker (`hostLinker.c`).
- It links `liballeg.so.4.0`, `libNL.so.1.6` (HawkNL) and
  `libstdc++.so.5`, which no system has today. `lib/` holds the first two,
  built by `scripts/linux/build-gogostrike-libs.sh "<game>/lib"` (Batocera's
  i686 compiler by default, the host's X11 headers):
  - `liballeg.so.4.0`: Allegro 4.0.3's C sources (liballeg's `v4-0-3` tag),
    built without its own build system: X11 window (MIT-SHM), OSS sound, no
    console drivers, DGA, VidMode or modules, no FLI player. Allegro's
    assembler half (`liballeg_unsharable.a`: blitters, sprites, the X
    window's bank switch) is linked into `bowling_usa`, which exports it.
    The game also copies data from the cabinet's Allegro: the VBE/AF
    driver's variables and its patched PS/2 mouse driver's packets
    (`ps2mouseidx`, `ps2mousepx`, `ps2mousepy`), defined in
    `src/loader/paokai/libs/allegroCompat.c`.
  - `libNL.so.1.6`: HawkNL's API without a network
    (`src/loader/paokai/libs/nl.c`): the lanes' link to a server finds no
    peer; threads and mutexes are pthreads'.
  - `libstdc++.so.5`: `build-linux/igs-sr-libs` (Speed Rider's) on the
    desktop; a copy in `lib/` for Batocera, which has none.
- Run from the game directory: `scripts/linux/runggs.sh` (`GAMES_DIR`).
  The loader makes `sram.bin` there (64 KiB, the battery-backed SRAM).

## Batocera

`games/Go Go Strike.squashfs` (zstd, `-b 131072 -all-root`, without
`sram.bin`) in `/userdata/roms/arcadepc/`, emulator linuxloader per game
(`arcadepc["Go Go Strike.squashfs"].emulator/core=linuxloader`). The
generator's kind `gogostrike`: 640x480, linuxloader's own input (mouse or
trackball, keyboard, the first pad: left stick the trackball, Start or A
start, Back coin, R3 setup, L3 bookkeeping), the window scaled to the screen
by gamescope (`-w 640 -h 480 -f -S fit`).

## The cabinet (`src/loader/paokai/`)

- The I/O card (the `w040101` driver): `/dev/w040101_io0..2`, a byte each
  (a read the switches, set bits pressed; a write the outputs: lamps, the
  hopper's motor, the meters), and `/dev/w040101_ctl`, `_io101`, `_io104..106`
  (written once at the start). All are `/dev/null`; the loader answers the
  game's port reads (`ubKeyIoRead`, detoured, `paokaiIo.c`). Its timer
  (`vKeyIoIsr`) reads the three ports in turn: port 0's read latches the
  loader's input state.
- The switches, from the game's key table (0x080a6750) and coin table
  (0x080a67a0):

  | Port.bit | Switch | Keys |
  | --- | --- | --- |
  | 1.3 | START | 1, Space, gamepad Start or A |
  | 0.5 | SETUP | F2 |
  | 1.5 | RECORD (bookkeeping) | F1 |
  | 2.6 | ERR_RESET (clears errors) | z |
  | 2.5 | coin A | 5 |
  | 0.0 | coin B | 6 |
  | 1.0 | key-in credit | F1 + 5 |

  A coin is held 40 port reads, then released as long (the game counts one
  held 15 to 200 reads).
- The trackball: a PS/2 mouse the cabinet's Allegro read, leaving each
  packet in `ps2mousepx/py` and their count in `ps2mouseidx`. At each port 0
  read the loader puts there one packet, the motion since the last (y
  upwards): from evdev's mice (`trackball.c`), or on the desktop from the
  mouse over the game's window (Allegro's `_xwin`; the pointer is warped
  back to the window's centre before it reaches an edge), the arrows and a
  gamepad's left stick. Pushing the ball up throws it.
- The SRAM, `/dev/w040101sram0`, mapped (64 KiB): the game directory's
  `sram.bin`. The game takes a mapping above 2 GiB as a failure (an int),
  as every one is for a 32-bit process on a 64-bit kernel: its mappings
  are placed between 1 and 2 GiB (`mmap` hooked).
- The serial port (`/dev/ttyS0`, `iRS232OpenPort(0, 0)`): its opening fails.
  `system()` runs nothing (logged).
- Sound: Allegro's OSS driver on the loader's `/dev/dsp` and `/dev/mixer`.

## Notes

- The loader's own exported `font` (glutBridge's SDL_ttf font) took the
  place of Allegro's `font` in the game's copy of it (a NULL font, a crash in
  the start-up message's timer): it is `glutBitmapFont` now.
- The game's `__ASSERT` prints on the screen: one before the graphics mode
  crashes in `textout` (a NULL screen). Break at 0x0806f870 for its message.
- Not done: the hopper / ticket payout (its sensor never pulses), the
  network link between lanes.
