# Space Invaders Frenzy (Raw Thrills)

Native support is implemented for the v1.79 `game_dump.exec` executable. The
loader selects it before the original envelope-protected `game` file, fixes
the imported native functions, redirects cabinet asset paths to the game
directory, and sets up the 224×288 game projection in the configured window.
The game was launched at 1920×1200; its portrait playfield filled the screen
height with the expected side bars. Mouse and cabinet input mappings still
need gameplay validation.

## Launch

The game directory should contain `game_dump.exec`, the original `game`, and
the game's data directories (`images/`, `shaders/`, `sounds/`, `utils/`, and
others). A `/pm` subdirectory is not needed.

```sh
GAME_DIR='/path/to/Space Invaders Frenzy' \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/runsif.sh
```

`GAMES_DIR` can name the parent folder instead of `GAME_DIR`. `TP_LIBS` can
provide extra native ELF libraries. The game needs 32-bit GL and its other
`DT_NEEDED` dependencies. The launcher runs the native executable; Wine and
TeknoParrot are not required.

## Input and display

The native renderer uses a fixed 224×288 logical space. The loader routes its
projection setup through the configured display size while retaining the
game's portrait 7:9 aspect ratio when `KEEP_ASPECT_RATIO` is enabled. Mouse
aim, trigger, reload, and the physical gun mappings have not yet been
confirmed in gameplay.

The current JVS mapping routes player 1 analog channels `0x13` and `0x14`,
and player 2 channels `0x15` and `0x16`, to the two gun axes. Coin, service,
test, start, and navigation switches are mapped in the Raw Thrills profile.
