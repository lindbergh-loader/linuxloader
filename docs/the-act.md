# The Act (Cecropia)

Native support targets release009g (`decaf_Linux` 2.45, built December 12,
2006). Startup, the attract movies, coins, the button, starting a story and
the knob (mouse, gamepad stick) were checked on the desktop on 2026-11-10.
Deployed to Batocera the same day (`rawthrills/The Act.pc`), not played
there yet.

## The game

`decaf` plays hand-drawn movies (H.264 and big-endian PCM in QuickTime
files) and the scripts around them. The binary is 32-bit, links SDL 1.2,
FFmpeg as of 2006 (libavformat 50, libavcodec 51, libavutil 49), sqlite 3
and ALSA, and was built against uClibc 0.9.28: it asks for
`/lib/ld-uClibc.so.0`, `libc.so.0` and `libm.so.0`.

The cabinet layout is `~/dist/decaf/`: `bin/decaf_Linux`, `release009g/`
and `current`, a link to the release, which `run_decaf_Linux` enters before
starting `../bin/decaf_Linux`. Copies of the disk often hold `current` as an
empty file: the loader enters `current` when it is a directory, else the last
`release*` directory.

## Libraries

Neither the desktop nor Batocera has the old libraries. They go in the
game's `decaf/bin/lib/`, built by `scripts/linux/build-theact-libs.sh` from
the sources in `src/loader/cecropia/libs/`:

| Library | What it is |
| --- | --- |
| `libc.so.0`, `libm.so.0` | uClibc's names. The game's libc calls are all glibc's; `libc.so.0` adds `__uClibc_main`, the start-up its `_start` calls. |
| `libavformat.so.50` | The 2006 API the game's player uses (`av_open_input_file`, `avcodec_decode_video`, `img_convert`, ...), with its structures laid out as the game reads them (`avOld.c`), on a minimal static FFmpeg 7.1 (`avModern.c`: the mov demuxer, H.264, PCM, swscale). FFmpeg's own symbols stay local to the library. |
| `libavcodec.so.51`, `libavutil.so.49` | Empty, for their names. |
| `libsqlite3.so.0` | sqlite 3.47, for the operator state database. |

```sh
FFMPEG_TARBALL=/path/to/ffmpeg-7.1.1.tar.xz \
SQLITE_TARBALL=/path/to/sqlite-autoconf-3470000.tar.gz \
scripts/linux/build-theact-libs.sh '/path/to/The Act/decaf/bin/lib'
```

Without the tarball variables it downloads them. `CC` defaults to Batocera's
i686 compiler when it is there, else `gcc -m32`. FFmpeg is built without asm:
its 32-bit x86 code needs text relocations, and C decodes the 640x480 movies
fast enough.

SDL 1.2 is not built: sdl12-compat (with SDL2/SDL3) comes from the system or
`LIBS32`. Batocera has a 32-bit SDL2 but no SDL 1.2: a 32-bit sdl12-compat
`libSDL-1.2.so.0` (1.2.76, glibc 2.34) goes in the same `lib/`.

## Batocera

The rom is the game directory as `/userdata/roms/rawthrills/The Act.pc` (a
writable directory: the game writes its config and `decaf/var/state.db`).
The generator recognises it by name (kind `theact`) and starts the loader in
`decaf/bin`, with `decaf/bin/lib` in the library path. On a pad, the left stick
and the d-pad turn the knob, B, A and Start are the button, and Select inserts
a coin. The screen is larger than 640x480, so the game runs fullscreen.

## Launch

```sh
GAME_DIR='/path/to/The Act' \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/runact.sh
```

The launcher always starts `decaf_Linux` through the host's 32-bit dynamic
linker (`/lib/ld-linux.so.2 ./decaf_Linux`), since `ld-uClibc.so.0` does not
exist. The game runs windowed at 640x480. It goes fullscreen (scaled by
sdl12-compat) with `[Display] FULLSCREEN = 1`, or when the configured size is
larger than 640x480, as Batocera's is.

## Cabinet hardware

- **GIB board** (USB serial on `/dev/ttyUSB*`): a security handshake checked
  at boot and every 20 seconds. Without it, the game shows its violation
  screen. `GIB_Open`, `GIB_Close`, `GIB_Reset`, `GIB_Challenge`,
  `GIB_GetVersion` and `GIB_GetBoardID` are replaced and answer that the board
  is there. The game does not use the board's coin counts.
- **State database**: `/var/decaf/state.db` (sqlite) is the game
  directory's `decaf/var/state.db`.
- The `/sys/class/graphics/fb0/...`, `/etc/release` and hwmon reads are for
  its statistics screen only and are left alone.

## Focus events

The game's event loop treats every SDL focus event (`SDL_ACTIVEEVENT`) as its
pause toggle (the P key's event, 0x19). The cabinet has no window manager to
send any, but a desktop sends one each time the window gains or loses focus:
the game paused on a click in its window and went on when the pointer left.
The loader's `SDL_PollEvent` hook drops them.

## Movie paths

A sequence's movies are named relative to its directory (`01.mov`).
`Movie::Movie` runs on the main thread in that directory, but the movie is
opened later by its demuxing thread. Meanwhile the main thread `chdir`s
elsewhere for a moment while it loads the sequence's art packs
(`getFullPath`, `BMPPack::_readInPak`). The first movie of a story then failed
to open now and then ("Movie -- unable to open input file: 01.mov"). The
constructor is hooked to make the path absolute.

## Controls

The game reads its controls through SDL: the knob is the mouse's horizontal
position across the screen (`InputManager::getActiveControlValue`), and the
button and the coins are keys (the cabinet's: Enter or 3, 1 or 2). Mouse
buttons do nothing. The loader's usual keys are mapped on them as SDL hands
the game its key events: 1 (start) is the button, 5 a coin.

| Control | Keyboard / mouse | Gamepad (desktop) | evdev (`INPUT_MODE = 2`) |
| --- | --- | --- | --- |
| Knob | mouse left/right | left stick, d-pad left/right | `ANALOGUE_1`, P1 LEFT/RIGHT |
| Button | 1, Enter or 3 | A or Start | P1 START or BUTTON_1 |
| Coin | 5 or 2 | Back | the coin switches |
| Quit | Esc | | |

The stick turns the knob rather than setting its position: the further it is
pushed, the faster the knob turns (past a 15% dead zone, with the square of
the travel, for fine moves), up to the whole range in one second. The knob
stays where it is when the stick is let go, as the cabinet's does, and the
mouse takes it back as soon as it moves. The game's `knobBackwards` setting
reverses both.

The game's own debug keys still work: the right arrow skips the sequence and
the down arrow restarts it. That is why the keyboard's arrows do not turn the
knob.
