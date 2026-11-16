# Skycurser (Byoyoyo)

Native support targets Skycurser (data 17.10.01), a two-player shoot'em up
cabinet game (2017) made with GameMaker: Studio 1.4: YoYo's Linux runner
(32-bit, X11 + GLX, OpenAL) and the game's data. Attract, coins, start and
the first level reached in a headless gamescope (2026-11-16); not yet played
by hand.

## The dump

`games/Skycurser/`: `runner`, `assets/` (`game.unx`, the game's 232 MB
data, `audiogroup1.dat`, the included files), `config/` (the cabinet's
`~/.config`: `skycurser_BGS/` its save area, openbox, pulse), `env/` (an
empty home, `Byoyoyo`), `bezel.png`.

- Executable: `runner`, file CRC 0x3ce2194f, loader ID 0x4a073bb5 (YoYo
  Runner v1.0 r32908: the ID is the runner's, other games built on the same
  runner would share it). It lost its executable mode: it runs through the
  host's dynamic linker (`hostLinker.c`); the runner finds `assets/` from
  `/proc/self/exe`, which the loader answers with the game directory's
  `runner`.
- It links `libopenal.so.1`, `libssl.so.1.0.0` and `libcrypto.so.1.0.0`,
  which neither the desktop (32-bit) nor Batocera has. `lib/` holds them,
  from `scripts/linux/build-skycurser-libs.sh "<game>/lib"`:
  - `libopenal.so.1`: OpenAL Soft 1.24.3 (Batocera's `dl/` tarball, its
    i686 compiler) with the OSS backend only: its `/dev/dsp` is the
    loader's (`common/ossDsp.c`, SDL3 audio), the same on the desktop and
    on Batocera.
  - OpenSSL 1.0.0's two libraries, versioned (`OPENSSL_1.0.0`) as the runner
    links them: copied from an i386 Ubuntu build (`OPENSSL_LIBS`, The
    Walking Dead's by default). The game's network code (its updates) finds
    no server.
- Run from the game directory: `scripts/linux/runsky.sh` (`GAMES_DIR`).
  The runner needs XF86VidMode (Xvfb has none: it stops at "Could not get
  list of screen modes") and takes the whole display, full screen, its
  320x240 picture scaled to the height. Windowed on the desktop through
  gamescope: `gamescope -W 1600 -H 900 -w 1600 -h 900 -- scripts/linux/runsky.sh`.
- The bezel: the dump's `bezel.png` (1920x1080, its hole the 4:3 picture),
  drawn over each frame before the runner's swap (`byoyoyo.c`, the loader's
  `graphics/bezel.c`), at the window's height, centred: its sides fall
  outside a window narrower than 16:9. `[Display] BEZEL` / `BEZEL_ENABLED`
  as for the other games (0: none).
- Its settings, scores and credits: GameMaker's save area,
  `~/.config/skycurser_BGS/` (the dump's `config/skycurser_BGS/` is the
  cabinet's, not used). The included files there (`switch-*.txt`,
  `status-*.txt`, `version-*.txt`) were the cabinet's network and update
  daemon's link to the game; the runner reads `assets/`' copies when they
  are absent.

## Batocera

`games/Skycurser.squashfs` (zstd, `-b 131072 -all-root`, 278 MB): `runner`
(its executable mode set), `assets/`, `lib/`, `bezel.png`; not the dump's
`config/` (the cabinet's `~/.config`: a second copy of `game.unx`, its
Wi-Fi settings) or `env/`. In `/userdata/roms/arcadepc/`, emulator
linuxloader per game. The generator's kind `skycurser`: evdev input, each
player's pad (d-pad or left stick, b the shotgun, a the katana, y the
switch, Start, Select coin, R3 the operator's menu); the runner takes the
whole screen.

## The controls (`src/loader/byoyoyo/`)

The game reads nothing but the X keyboard (its `scr_update_controls`
script, keys as GameMaker's virtual key codes):

| | Player 1 | Player 2 |
|---|---|---|
| Joystick | arrows | R F D G (up, down, left, right) |
| Shotgun | Left Ctrl | A |
| Katana | Left Alt | S |
| Weapon switch | Space | Q |
| Start | 1 | 2 |
| Coin | 5 | 6 |

F2 the operator's menu, 9 full screen, V/B the volume, P reset, Esc quits.
The desktop keyboard is the game's own. The loader's input becomes those
keys, sent to the game's window (`XSendEvent`, `byoyoyoInput.c`):

- `INPUT_MODE 2` (evdev): PLAYER_1/2's joystick, BUTTON_1 shotgun,
  BUTTON_2 katana, BUTTON_3 switch, START, the coins (a 150 ms press each),
  TEST the operator's menu.
- otherwise the first two SDL gamepads, a player each: d-pad or left stick,
  A shotgun, B katana, X switch, Start, Back coin, R3 (player 1's) the
  operator's menu.
