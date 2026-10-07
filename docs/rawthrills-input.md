# Buttons and light guns for a Raw Thrills g5 game

This is how Big Buck HD Wild got its buttons, its keypad and its light guns
(desktop mouse and evdev, the Sinden included), and how to do the same for the
next g5 game. The code lives in `src/loader/rawthrills/rtIo.c`, driven by the
per-game fields of `rtGames.c`; Jurassic Park is the reference game, because
its dump exports the engine's functions by name.

## How input reaches a g5 game

The g5 engine keeps two sets of ids:

- **I/O slots**, where the I/O backends write what the hardware reports:
  the RIO board's switches, the IR tracking of the guns, the keypad board,
  and the SDL keyboard and mouse (whose ids are SDL 1.2 key codes, then the
  mouse from `0x144`).
- **Engine inputs**, what the game reads: "P1 start", "gun 0 X", "coin 0".

An **input map** of `(slot, input)` pairs joins them. It is registered once at
startup through `InputAddMap(src, dst)`, and at every frame `io_loop` runs the
backends' loops, then the engine turns the slots into inputs through the map.

So the loader can feed a game in two places:

1. **The map.** Add pairs so that some source the loader controls (a key, the
   mouse) drives an input. Jurassic Park's desktop keys work this way.
2. **The slots.** Write the cabinet's own slots right after the backends have
   run, with `io_get_input_digital(id)` / `io_get_input_analog(id)` to find
   them. The game's own map then does the rest, exactly as with the real
   boards. This is what the evdev input does, and for Big Buck HD Wild the
   desktop input too.

## What the loader needs from the game

For a dump that exports the engine (Jurassic Park) everything is found by
name. Big Buck HD Wild is stripped, so each address goes in its `symbols`
table (`RtSymbol`), and `rtSymbol()` looks there before the exports:

| Name | What it is | Big Buck HD Wild |
|---|---|---|
| `InputAddMap` | adds a map pair | `0x815a1d0` |
| `io_get_input_digital` | digital slot of an id | `0x8256804` |
| `io_get_input_analog` | analog slot of an id | `0x8256cb8` |
| `io_input_analog_update` | sets an analog slot from a raw value | `0x8256210` |
| `io_set_input_raw_range` | an analog slot's raw range | `0x82565c3` |
| `io_new_data_present` | returns the "new data this frame" flag | `0x825618f` |
| `io_sdl_loop` | the SDL backend's loop, fed after | `0x825a4eb` |

Plus, per game (`RtGame`): the trampoline sizes of the hooked functions
(`inputAddMapPrologue`, `ioLoopPrologue`), the slots to write (`ioInputs`)
and the extra map pairs (`extraMaps`).

## Finding them in a stripped binary

Disassemble both games (`objdump -d`; a dump without section headers is
disassembled raw from its first `PT_LOAD`, `objdump -D -b binary -m i386
--adjust-vma=<vaddr>`), and use Jurassic Park's exported functions as the key.

**Exact matching fails.** The two games were built by different compilers:
normalising the addresses away and matching instruction sequences found almost
nothing. What survives is the *constants* and the *source order*.

**Constants.** The accessors check the id against the enum's ranges:
Jurassic Park's `io_get_input_digital` compares with `0x142` and dispatches on
`id - 0x146`, `io_get_input_analog` on `id - 0x144`. Searching Big Buck HD
Wild for `$0x142` next to `sub $0x146` gives `0x8256804`, and `sub $0x144`
right after it the analog one. `InputAddMap` caps its table: `cmp $0x3ff`
with a store at `table(,%eax,8)` finds it (`0x815a1d0`), and gives the table
(`0x8cc6234`) and its count (`0x8cc8234`).

**Source order.** Both builds lay `io.c` out in the same order, and Jurassic
Park's exports give that order: `io_input_record_get`, `io_new_data_present`,
…, `io_input_analog_update`, `io_input_digital_update`,
`io_set_input_raw_range`, …, `io_get_input_digital`, `io_get_input_analog`.
Walking back from the accessors, the function starts fall into place: a
one-line `mov flag,%eax; ret` is `io_new_data_present`, the one that
`memcpy`s a slot is the digital update, the one storing a slot's `+0x14`,
`+0x18` and their difference is the raw range.

**Slot layout.** Check it before writing slots by hand: Big Buck HD Wild's
digital slots are `0x2c` bytes (`imul $0x2c` in its accessor) where Jurassic
Park's are `0x20`. Its digital update showed the first `0x20` bytes laid out
the same (pressed, released, held time, press and release counts, raw value
and count), so the loader's writer (`ioDigitalSet`) works unchanged.

**The backend loop.** `io_loop` (`0x8255f80`: the timer, the watchdog, then
calls through pointers) runs the backends from a table (`0x8c57340`, entries
of `{mask, init, quit, loop, state}`, `0x54` bytes). Dump the table from the
binary: the entry whose functions surround `io_sdl:create_window()` (a string
that names itself) is `io_sdl`, and its loop is the one to hook. The loader's
feed runs after it, so that nothing it writes is overwritten in the frame.
`io_loop` itself takes no argument and computes `dt` for the backends, which
is why the hook goes on the backend loop, which receives `dt`.

## Reading the game's input map

Rather than guessing ids, read the pairs the game registers. In Big Buck HD
Wild all 69 `InputAddMap` calls are in `main`, as immediates
(`movl $dst,0x4(%esp); movl $src,(%esp); call InputAddMap`), so a small script
over the disassembly lists them. They come in groups:

- the cabinet guns: slots `0x181`/`0x182` (gun 0 X/Y), `0x17b`/`0x17c`
  (trigger, pump), `0x183`/`0x184` and `0x17e`/`0x17f` for gun 1, onto the
  gun inputs and the menus' pointer and select;
- the board's switches: coins `0x14c`/`0x14d`, test `0x150`, service `0x151`,
  volume `0x152`/`0x153`, starts `0x154`/`0x155` (the same slots as Jurassic
  Park's);
- the keypad: slots `0x163`–`0x16e` (and again `0x16f`–`0x17a`);
- a developer map (the mouse on both guns), registered instead of the
  cabinet guns when `0x80ae610()` returns bit `0x8`. The release build's
  `0x80ae610` returns 0, so the cabinet map is the one in use.

The inputs' names come from the switch test's table of `{id, name}` pairs
(`SwitchTestCabStart0`, `SwitchTestNumpad0` at `0x8c57270`, …): find a name
string, then the words around the pointer to it. Mind which word is the id: in
this table the id comes *before* the name. Checking against a pair already
known (start `0x154` → `0x177` = `SwitchTestCabStart0`) settles it.

The service switch (`0x151` → `0x17d`) gives a service credit, as on the
cabinet: F1 adding a coin is the game's behaviour, not a mapping error.

## What the loader does with it

`rtInstallInput()` (`rtIo.c`):

- **No `GameInputMaps` to hook.** Jurassic Park registers its map in a
  `GameInputMaps` function the loader wraps; Big Buck HD Wild does it inline
  in `main`. With `inputAddMapPrologue` set, `InputAddMap` itself is hooked:
  filtered pairs are dropped as they come, and the `extraMaps` are added at its
  first call.
- **The switches, in both modes.** `ioInputs` lists the slots and what feeds
  them. With `INPUT_MODE = 2` the source is the loader's evdev state (the
  `[EVDEV]` mappings). With `ioDesktop` set, the desktop feeds the same slots
  outside evdev mode (`desktopInputState()`, common/desktopInput.c: 1/2 start, 5/6 coin, F1 service, F2 test,
  arrows up/down volume, and the mouse over the game window is P1's gun, left
  button the trigger, right the pump). The desktop and the Sinden go through
  the same code, so checking with the mouse checks it for the guns.
- **The guns, through the IR camera** (next section).
- **The keypad** has no JVS switch to come from, so it is added to the map:
  the desktop numpad's SDL key codes (`KEY_KP0` = 256 …) onto the keypad's
  inputs, `*` on the numpad's `*`, `#` on its Enter.

Big Buck HD Wild's evdev inputs:

| `[EVDEV]` | Goes to | Game |
|---|---|---|
| `ANALOGUE_1` / `ANALOGUE_2` | camera, gun 0 | P1 gun X / Y |
| `ANALOGUE_3` / `ANALOGUE_4` | camera, gun 1 | P2 gun X / Y |
| `PLAYER_n_BUTTON_1` | camera, button 0 | trigger |
| `PLAYER_n_BUTTON_2` | camera, button 1 | pump |
| `PLAYER_n_BUTTON_START` | `0x154` / `0x155` | start |
| `PLAYER_n_COIN` | `0x14c` / `0x14d` | coin |
| `PLAYER_n_BUTTON_SERVICE` | `0x151` | service credit |
| `TEST_BUTTON` | `0x150` | test |
| `PLAYER_1_BUTTON_UP` / `DOWN` | `0x152` / `0x153` | volume, menu up / down |

## The IR guns

The cabinet's guns are seen by an IR camera, through a camera manager
(`cmgr`, strings `cmgr_init`, `cmgr_thread_main`) and the `io_irtrack`
backend. Writing the gun slots is not enough there, and it took three tries
to see why; each failure pointed at the next layer down.

1. **Slots only.** The mouse moved the gun slots, but the game ignored them
   and attract mode said "Left gun Not Connected!". The tracking backend's
   loop (`0x8259b23`) asks the camera manager, for each gun, whether it is
   active (`0x85cc328`); without a camera it is not, and the loop flags the
   gun's slots offscreen (`0x10000` in their first word) every frame, a flag
   the loader's later writes never clear.
2. **Answering "active" and the position.** With the gun active the loop
   asks for its position (`0x85cda1a`, `-1` off the screen) and its buttons
   (`0x85cd35e(gun, button)`, a count of the button's transitions, odd while
   held, which the loop turns into presses of the trigger and pump slots).
   Answering those made the gun "connected": the game started with its gun
   calibration, and the reticle followed. But shots did nothing on the level
   selection, and its countdown stalled. The trace showed every press
   reaching the trigger slot, and the saved calibration
   (`pmuser/aud/save/gun0Calibration.*.xml`) had all its gun points at 0: the
   position function is the camera's *raw* coordinates mapped through the
   calibration (`0x85f1392`), and the calibration screen reads the raw
   coordinates itself, which were not answered.
3. **Answering the raw coordinates.** The camera manager keeps them in two
   arrays its thread fills, read by two getters: `0x85cd081` (as they are,
   what the calibration reads) and `0x85cd0f9` (filtered, what the position
   reads). Answering those two, in an 800x600 camera space (the space of the
   default calibration the game prints at startup), lets the game's own
   calibration, transform and everything built on them work as on the
   cabinet: calibrate once from the boot screen, and it is saved.

So the loader answers (`irGunActiveSymbol`, `irGunRawSymbols`,
`irGunButtonSymbol`): guns 0 and 1 active, the raw coordinates from the gun
input (`ANALOGUE_1/2`, `3/4`, `-1` off the screen), and the button counts
from `PLAYER_n_BUTTON_1` (trigger) and `BUTTON_2` (pump). The gun slots are
left to the tracking backend. The second gun only becomes active once it has
pointed at the screen: a connected gun that never does stalls the menus.

A calibration saved while the raw coordinates were not answered is all
zeros, and the aim stays wrong with it: move `gun*Calibration.*.xml` aside
and the game calibrates again at boot.

## The display side

Big Buck HD Wild links SDL 1.2 statically and gets every X and GLX function
with `dlsym`. For a dump the loader already hands the game its own `dlsym`;
a normally linked executable keeps the real one, so the loader rebinds the
executable's `dlsym` import too (`rebindImports` in `rtDump.c`). Through it the
game gets:

- the swap hook (`glXSwapBuffers`), which draws the gun border and scales the
  frame when the window is resized (`graphics/frameScale.c`);
- `XSetWMNormalHints` without the equal minimum and maximum size SDL sets on a
  window it was not asked to make resizable, so the window can be resized.

## A g6 variant: Pink Panther Jewel Heist

Pink Panther Jewel Heist (g6, a ticket game on a portrait monitor) uses the same slot ids as Big Buck HD Wild, but the obvious port of
its setup did nothing. The keys reached the loader, and the loader wrote the
slots, yet the game never reacted. How the working setup was found:

**Reading the map at run time.** The map is data-driven here, so a scan of
`main` finds no `InputAddMap` calls. The engine's map table was located from
the `0x3ff` cap instead (entries from `0x8a17254`, count at `0x8a19254`). A
temporary thread in the loader printed the table 20 s after start (gdb
cannot attach: `ptrace_scope` is 1). That gave 30 pairs: Big Buck HD Wild's
cabinet switches, plus six slots `0x159`–`0x15e` feeding
`SwitchTestExtSwitch0..5` (`0x19c`–`0x1a1`). The switch-test table shows the
names, with the id before the name as before. The map is registered by a
`GameInputMaps` at `0x8068000` calling `InputAddMap` at `0x80aac90`, but no
extra pairs are needed, so neither is hooked.

**Which inputs the game reads.** List the ids pushed before each call to the
engine's accessor: `0x185` (fed by every Ext switch, both starts and the
test switch), `0x183`/`0x184` (menu up/down), `0x186`, `0x18b`/`0x18c`. None
is analog. Reading the ids is not the whole story, though: the game also
reads the start switches' own inputs (`0x177`/`0x178`), and on this cabinet
the two start switches are the left and right buttons that move the
Panther. Playing it showed that (keys 1 and 2 moved it), not the
disassembly. `BUTTON_LEFT`/`BUTTON_RIGHT` feed them too, and `BUTTON_1`
drives Ext switch 0.

**The accessor that finds nothing.** The first accessor found by the doc's
constants (`<= 0x143`, `sub $0x146`) is `0x80aaec0`, with `0x2c` byte slots
at `0x8a128ac`. Writing there had no effect. A trace in the loader's feed,
printing each written slot and the mapped engine input's raw value next
frame, showed the writes landing and being lost. The reason is in the
engine's input update (`0x80ab1c0`), found as the caller of
`io_new_data_present`. It calls `io_loop` itself, then `memset`s the engine's
table and copies io.c's own state (`0x8a72f60`) over it. In this build the
engine keeps a copy, and writes into the copy made inside `io_loop` are
overwritten as soon as it returns.

**io.c's own functions.** io.c is built unoptimised, so every function
starts `push %ebp; mov %esp,%ebp` and the file's layout reads straight off
the disassembly. `io_init` (`0x81855e2`) is the one indexing the backends'
table (`0x89e6060`, the same `{mask, init, quit, loop, state}` entries of
`0x54` bytes). After it:

- `io_loop` (`0x81857b0`), which clears then sets the new-data flag `0x8a72f58`;
- a one-line getter of that flag, `io_new_data_present` (`0x8185a18`);
- io.c's slot accessor `0x818604f`: `<= 0x142` directly (`0x2c` byte slots
  from `0x8a72f60 + 0x1a28`), `0x146` on through a jump table into the same
  state.

A `mov flag,%eax; ret` getter elsewhere (`0x80a9af0`) looks like
`io_new_data_present` but is not: its flag is written by the dongle code.
Check who writes a flag before trusting it.

**The last backend.** The enabled mask (`0x902`) runs `io_sdl` (`0x800`)
and then `io_rio` (`0x2`). As in Cruis'n Blast, the feed hooks the RIO
loop (`0x8184e83`, `ioLoopSymbol = "io_rio_loop"`), so nothing runs after it
within the frame.

With io.c's accessor and the RIO loop hooked, the start slot's engine copy
(`0x177`) follows the key. Since there are no analog inputs, the loader does
not need the analog update and range functions (it only requires them when
`ioInputs` has an analog entry). With no map to patch, `rtInstallInput`
installs the feed without a map hook.

**The bezel.** Pac-Man and Galaga Assault draw through a projection
function the loader fits into the bezel's hole. This game has none: it draws
upright at whatever size `-f<w>x<h>` gives it. With `bezel.png` beside the
game and a landscape `[Display]` size, the loader instead (`bezelFrame`):

- gives the game the hole's size (606x1080 on a 1920x1080 screen);
- opens the window at the screen's size, by hooking `SDL_SetVideoMode`.
  SDL is the system's here, so the hook sits on its PLT entry, `jmp *GOT`,
  six bytes that relocate as they are;
- has the frame scaler take the game's frame from the first swap and fit
  it in the hole (`frameScaleSetFrame`).

The first attempt showed the bezel with the boot text frozen in the hole.
The game fetches `glBindFramebuffer` through its own `glXGetProcAddress`
import, which the loader did not wrap: only the lookups of a linked-in SDL
were. So its renderer bound the real framebuffer 0, and each swap covered
the result with the scaler's stale copy. The executable's
`glXGetProcAddress`/`glXGetProcAddressARB` imports now go through the same
wrapper (`rebindImports`). A game frozen on its first frames while the
scaler is active points at an entry point the scaler does not see.

The controls: from the desktop, the Left/Right arrows (or 1/2) move the
Panther and Space or the left mouse button is the button, with the usual
coin, test, service and volume keys. A pad works with `INPUT_MODE 2`, its
d-pad mapped to `PLAYER_1_BUTTON_LEFT`/`RIGHT` and a button to
`PLAYER_1_BUTTON_1` in `[EVDEV]`. The desktop keys keep working there.

## Aliens Armageddon: Big Buck HD Wild's guns, Pink Panther's io layer

Aliens Armageddon (g6, `/pm/g6/aa`) links glut like Terminator Salvation,
but it draws and reads its input through the g5/g6 io layer: SDL 1.2 linked
in, GL through `dlsym`, and the IR guns through the `cmgr` camera manager.
Two earlier games cover everything it needs:

- **The camera manager is Big Buck HD Wild's** at `-0xecc00`. The raw
  coordinate getters were found at the same distance apart (`0x78`), and
  the delta then gives the active check and the buttons. The 400-byte
  calibration function is identical.
- **The io layer is Pink Panther Jewel Heist's.** The engine copies io.c's
  slots after `io_loop`, so the loader writes io.c's own slots (accessor
  `0x81b148c`) and hooks `io_rio_loop` (`0x81b5c74`), the last backend
  enabled in the table at `0x88b1040`.

`GameInputMaps` (`0x8081180`) calls `InputAddMap` (`0x80b98c0`, not the
callback registration at `0x80b9510`, which has the `0x3ff` cap too) with
literals, so the pairs read straight off the disassembly. The one addition
to Big Buck HD Wild's map is a **third button on each gun**. io_irtrack's
loop asks the camera for buttons 0, 1 and **3** of each gun and writes
them to slots `0x17b`..`0x17d` (gun 2: `0x17e`..`0x180`). The switch test
names them trigger, pump and grenade. The loader's `irGunButton` answers
button 3 from `BUTTON_3`, so the mapping is Terminator Salvation's:
`BUTTON_1` trigger, `BUTTON_2` pump, `BUTTON_3` grenade. From the desktop
those are the mouse's left, right and middle buttons.

The video mode is gCLArgs (`0x890b540`), set by `ParseCommandLineArgs`
(`0x8083b40`) as in The Walking Dead. Its fullscreen flag is a byte, next
to the byte that selects the guns, so it is not written. Fullscreen goes
through the `SDL_SetVideoMode` hook instead.

**Out of address space.** The first run stopped a few seconds in, with
radeonsi's `LLVM failed to upload shader` and then a lost GPU context. It
was not a shader bug (the shaders are all `#version 150`, which the loader's
g3 shader rewrite does not touch). The core showed 4084 MB mapped, the
whole 32-bit address space, including 14 anonymous 64 MB blocks: thread
stacks. The launcher raises `RLIMIT_STACK` to 64 MB for the main thread,
and glibc gives every new thread that much too. With `threadStackSize` (8
MB), set through `pthread_setattr_default_np` at init, the game stays
around 2.2 GB. A lost GPU context in a 32-bit game is worth checking
against its mapped size (the core's `PT_LOAD` segments) before the shaders.

## Debugging tips

- **Screenshots on a Wayland desktop.** `ffmpeg -f x11grab -window_id <id>`
  captures a game window; the root window comes out black. `xprop -root
  _NET_CLIENT_LIST` lists the windows, `xprop -id <id> WM_NAME` names them.
- **Injected keys do not reach the game there.** XTest presses do not show in
  `XQueryKeymap` under GNOME's Xwayland: test keys by hand.
- **Size hints.** `xprop -id <id> WM_NORMAL_HINTS` shows a pinned window
  (equal program-specified minimum and maximum size).
- **Child windows.** Some SDL builds draw in a child of their window; list the
  children (`XQueryTree`) before trusting the drawable's size.
- **Stale instances.** Killing the launcher can leave `./game` running; check
  `pgrep -a -x game` (and each one's working directory) before judging a
  change on screen.
- **stdout is buffered** when redirected to a file: the loader's `printf`s may
  only appear at exit. Trace to stderr while debugging.
