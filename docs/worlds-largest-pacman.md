# World's Largest Pac-Man (Raw Thrills)

Native support targets the v1.71 `game_dump.exec` executable. The loader
rebuilds its Raw Thrills import table, resolves the embedded SDL and RIO
exports, redirects `/pm/pacman/pacman` asset paths into the game directory,
and fits the native 224×288 picture into the configured display size. This
is a joystick game; no lightgun mapping is installed. The game has native
HASP login and memory-read functions without dynamic symbols, so their
addresses are included in the profile for the dongle emulation. Without those
hooks the game reports “no dongle found” and loops forever during startup.
With the hooks, the game boots, coin and menu inputs work, and gameplay has
been confirmed on the cabinet.

The profile preserves the envelope's version and symbol hash tables when
resetting the dumped runtime state. The game's startup queries
`SDL_GetWindowSurface` solely to create an offscreen software renderer; its
picture is presented through OpenGL. The loader supplies a separate RGB
surface for this renderer, keeping the OpenGL window and context intact.
Querying the real window surface can replace its GLX drawable with native
SDL2 (Batocera/NVIDIA exits with `GLXBadDrawable`) or switch the current
context with sdl2-compat (music with a black screen).

## Launch

Keep `game_dump.exec`, the original `game`, and the game's data directories in
the same folder. The `/pm` path is redirected to that folder.

```sh
GAME_DIR="/path/to/World's Largest Pac-Man" \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/runwlp.sh
```

`GAMES_DIR` can name the parent folder instead of `GAME_DIR`. `TP_LIBS` can
provide extra native ELF libraries. The launcher runs the native executable;
Wine and TeknoParrot are not needed.

## Controls

The RIO profile maps coin 1/2, test, service, start, and joystick directions.
Desktop arrow keys and the configured evdev controller feed the same switch
inputs. The first SDL gamepad drives player 1 and the second drives player 2:
each d-pad steers, Start starts that player, and Back inserts that player's
coin. South/East are buttons 1/2. Player 1's L3 is service and R3 is test.
The switch assignments follow
the game's native RIO polling table: switches 36–39 steer player 1 and
40–43 steer player 2. On the keyboard, `1`/`I` start player 1, `2`/`O`
start player 2, `5`/`U` insert coin 1, and `6` inserts coin 2.
Arrow keys steer player 1 and `WASD` steer player 2.
`Space`/`Z` are player 1's buttons 1/2; `F`/`G` are player 2's buttons 1/2.
`F2` opens the operator menu. `Page Up`/`Page Down` control volume and
navigate that menu; the player start buttons enter/back out of a menu.
The loader blocks WLP's SDL keyboard events and key state so its shortcuts
cannot intercept desktop coin and player controls, which the loader reads
directly.

The cabinet's Start buttons also act as action buttons in gameplay. In
attract mode they enter the game's Pac-Man/Galaga selector. Those actions
come from the game's RIO cabinet controls, independently of SDL shortcuts.

The portrait picture keeps its 7:9 aspect ratio with `KEEP_ASPECT_RATIO`.
The projection hook uses the cabinet's logical dimensions separately from
the fullscreen window, as for Space Invaders Frenzy. `bezel.png` in the game
directory is supported by the same portrait display path.
