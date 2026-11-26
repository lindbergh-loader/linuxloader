# Orange County Choppers Pinball (Incredible Technologies)

Native support targets Orange County Choppers Pinball (2005, IT's "Pinball 1
OCC", product code 16, `V0.00.51, build Aug 24 2005`): a 32-bit Linux OpenGL
game on IT's "Nighthawk" PC, its monitor on its side. Played on the desktop
(2026-11-15, working): attract, coins, games, the initials, its settings
and audits saved.

## The dump

`games/Orange County Choppers Pinball/`: the cabinet's `/game/static` and
`/game/dynamic` merged (`pinball/` the assets and the writable
`configuration/`, `syslog/`, `savedgames/`; `lib/` the drivers, firmware and
the cabinet's own glibc 2.3.2), `game.bin`, `game.bin.encrypted` (the same
with a 100-byte IT header), `game.earlier` (an older build, unused),
`db_primary.tar.gz` and `pg_backup/` (the PostgreSQL database, unused),
`dynamic_backup/dynamic.tar.gz`.

- Executable: `game.bin`, file CRC 0x91bfaf71, loader ID 0x0d4666eb. It
  exports its 12229 symbols: the IT platform layer (`ITInitializeUsbIoBoard1`,
  `ITopen`, ...) is hooked by name. It lost its executable mode: it runs
  through the host's dynamic linker (`hostLinker.c`).
- It links `libstdc++.so.5` (`build-linux/igs-sr-libs`, Speed Rider's) and
  `libpq.so.3`, built into the game directory by
  `scripts/linux/build-occ-libs.sh "<game>"` from
  `src/loader/itech/libs/pq.c`: PostgreSQL's client API without a server.
  The game's `lib/` is never on the library path (its glibc).
- Run from the game directory: `scripts/linux/runocc.sh` (`GAMES_DIR`).
  The loader writes there: `itdongle1.bin` (the dongle), `db_primary/`
  (settings, audits, leaderboard), `syslog/`, `saved_cores/`.

## Batocera

`games/Orange County Choppers Pinball.squashfs` (zstd, `-b 131072
-all-root`, `libpq.so.3` and `libstdc++.so.5` beside `game.bin`, without
`itdongle1.bin`, `db_primary/`, `syslog/`, `saved_cores/`: made at the
first start, on the overlay) in `/userdata/roms/arcadepc/`, emulator
linuxloader per game (`arcadepc["Orange County Choppers
Pinball.squashfs"].emulator/core=linuxloader`). The generator's kind
`occpinball`: linuxloader's own input (the first pad's buttons above, the
keyboard), the game's `lib/` off the library path, full screen.

## The cabinet (`src/loader/itech/`)

- Paths: the cabinet ran in `/game`, by absolute and relative paths
  (`static/...`, `dynamic/...`): all are the game directory.
- Devices: every driver goes through `ITopen`, `ITioctl`, `ITread`,
  `ITwrite` and `ITclose`, detoured (`itechIo.c`). An emulated device's fd
  is a real one (`/dev/null`'s).
  - The USB I/O board (`/dev/ioboard1`, ioctl magic 0xd1): versions, its PIC
    (coinage and country block at 0x86), 12-byte packets in. A read blocks
    until the next packet, every 8 ms, as the driver's did: that beat is the
    game's clock (a read returning at once made the physics catch up for
    ever, 0.5 fps). Its 24-byte output writes (lamps) are dropped.
  - Its security (an answer the board puts in its packets to the game's
    challenges, wrong or missing ones reboot the cabinet): the game's
    `SecurityControls*` checks are stubbed.
  - The dongle, "cabinet ID device" (`/dev/itdongle1`): 8 blocks of 512
    bytes, each ending in its CRC32, in `itdongle1.bin`, made at the first
    start: block 0 header version 0, product code 0x10, serial 100001 (the
    game shows ID 16-100001). No ITnet data (header version 1's, encrypted),
    so ITnet stays offline.
  - The rest (BIOS worm, card reader, modem, accelerometer, PCI scan,
    hardware monitor) is absent, as the game allows.
- Persistence: the layer's file store (`/game/db_primary/filestore`) in
  place of the database (`PersistenceConfig::toDB` false). That store was
  never used on the cabinet and builds its paths with `std::ends` (a NUL
  inside a `std::string`): `std::ends` writes nothing.
- Start-up: `mlockall`, `iopl` and the user switches succeed; `system()`
  runs nothing, `popen()` only reads `/proc` (`/proc/partitions` answered
  with the cabinet's one drive: the host's many stalled its parser); the
  game's driver loading (`insmod`, `lsmod`) and firmware updates are
  skipped; its own X server (`StartXServer`) is the desktop's display; its
  SIGSEGV handler is not installed (the loader's handles its parallel port
  LED, `out 0x378`).
- `pthread_cond_timedwait`: the game's deadlines carry no seconds
  (`usec * 1000 + ms % 1000 * 1000000`); glibc refuses a nanosecond count
  above a second, the wait loop then spins holding its mutex and the game
  deadlocks. The deadline is normalised.
- Graphics: NVIDIA's extensions (vertex array range, register combiners, NV
  vertex programs) emulated by `common/nvGl.c` and `common/nvProgram.c`,
  shared with the Gaelco games. The 640x480 frame, drawn for a monitor on
  its side, is turned upright into a portrait window (600x800, or [Display]
  WIDTH x HEIGHT, the screen with FULLSCREEN; ROTATE_VERTICAL 1 keeps it as
  drawn), through `graphics/frameScale.c` (`itechFrame.c`).
- `ITECH_SHOT=<dir>`: the window's picture every `ITECH_SHOT_PERIOD` seconds
  (4), as PPM.

## Controls

The I/O board's switches (packet bytes 7 and 8):

| Switch | Game | Keys | Gamepad |
| --- | --- | --- | --- |
| P1 C | left flipper (menus: choice) | Left | LB, LT, d-pad left |
| P1 D | right flipper (menus: choice) | Right | RB, RT, d-pad right |
| P1 B | nudge the cabinet left | z | X |
| P1 A | nudge the cabinet right | x | B |
| P1 A + B | bump the cabinet | Up | Y |
| P1 E | select | Space | A |
| Start | start, select | 1 | Start |
| Coin 1 / 2 | coins ($0.25 each, $1.00 a game) | 5 / 6 | Back |
| Test | operator menu | F2 | R3 |
| Service | service credit | F1 | L3 |
| Volume up / down | | Page Up / Page Down | |

Nudge too much: TILT. Hold a flipper and release to launch the ball.

## Notes

- ITnet tries the local database twice at start (no server): offline.
- Not done: the lamps.
