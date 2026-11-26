# Power Truck (IGS)

Native support targets Power Truck (`pt`, IGS 2011), a truck racing game on
Speed Driver 3's cabinet and A36 board: wheel, throttle and brake pedals, a
horn, a view button and a four-position gear lever. Desktop (headless
Gamescope/Mesa, 2026-11-13): loading, attract (videos, demo races), coins,
start, truck and transmission (AT/MT) selection, battle, mode and stage
selection, and races on the Downtown course in both transmissions. Played
on the desktop by the user (2026-11-13): working 100%. Not on Batocera.

## The dump

`games/Power Truck/` is a TeknoRaven package (its `game.json`):

| Path | Content |
| --- | --- |
| `game/pt` | the game, with its symbols (file CRC 0x699ae32a, loader ID 0x02fe5b0b) |
| `game/` | Ogre 1.6.1, its plugins, Cg, libcasmcard; `lib_extra/` GLU, Ogg, Vorbis, Theora, pcsclite |
| `game/roms/powertruck_eprom.bin` | the board's decrypted firmware (512 KiB) |
| `game/enc_script/`, `game/enc_speed/` | the scripts (`*.lua.enc`, the FPGA cipher) |
| `data/` | models, scenes (`scene/*.zip`), sounds, videos |

The game runs from `game/` (`../data`) as `./pt 25020 7056 10080`:
`scripts/linux/runpt.sh` (`GAMES_DIR` or `GAME_DIR`; `LIBS32` for the host's
32-bit libraries, e.g. the loader's `libglut`, whose libXaw comes from
`build-linux/igs-sr2-libs/`). The package's `pt` is not executable:
`chmod +x` it. `ogre.cfg` asks for a 1366x768 window.

## Controls

Speed Driver 3's (`igsSd3.c`, its `GameIO` stand-in; the switches' layout is
SD3's):

| Keyboard | Cabinet input |
| --- | --- |
| 5 / U | Coin (4 coins a credit by default) |
| 1 / I | Start / confirm |
| Left / Right | Steering, and the menus |
| Up / Down | Throttle / brake, and the menus |
| Space | Horn (`LK_BOOST`) |
| Z | View |
| C / V | Gear lever up / down |
| F1 / P | Service |
| F2 | Test |

The gear lever is an H pattern of four switches (`LK_G1`..`LK_G4`, one held,
in SD3's gear up/down slots and two of its second word's): gear up and down
move a virtual lever between them, from the first. Manual (MT) races use
it; the HUD shows its position. The desktop gamepad steers with its left
stick, RT throttle, LT brake. Force feedback goes through `VR::setFFB` as
for Speed Driver 3 (the same `Motor` bytes: direction, magnitude capped at
127), to the evdev wheel of `ANALOGUE_1` or the first force feedback device
(a gamepad rumbles); a race sends road feel (10-18) and steering pulls (63)
both ways (`IGS_FFB_TRACE=1`).

## The board (`src/loader/igs/igsA36.c`)

Speed Driver 3's A36 emulation, with Speed Rider 2's internal-ROM services
and table (`0x0200f000`, the firmware's dispatcher at `0x1000337c`; without
them its first service call jumps to 0 and it reboots in a loop), and the
idle loop `0x1000288c..0x100028a0` (it polls `0x02006b8c`). The game's
upload (`P2A_LoaderAdv_iLoadDataPointer`, the firmware embedded as `A36ROM`)
is stood in for. `A36Interface::dispatchAction` hands its replies through
a `toyshop::DataHolder`: Speed Driver 3's reply race is not there.

The cabinet lamps' functions (`lamp_control.lua`) blink on `timer % 2 < 1`
and `> 1` and return nothing at exactly 1, which the frame timing can hit:
`speed::lamp::LightState::update`'s cast to a byte then failed, and
luabind, built without exceptions, asserted (a SIGILL, minutes into the
attract). A failed byte cast now gives 0, the lamp off
(`luabindByteCastFailed`; it is used for the lamps only).

The scripts' cipher mask, `e68837ed7350dcfe4f16ef54d23b107e`, was found by
frequency analysis of the scripts, as Speed Rider 2's; one mask for every
64 KiB.

## Ogre

- **Archives.** `resources.cfg` puts the trucks' directories
  (`../data/car/bus`, `eur`, `fir`, `jap`, `mac`) in its `Kuruma` group, and
  the truck selection adds them to groups of its own (`*_car_group`), which
  `ArchiveManager` shares by name. Leaving the selection the game unloads
  them (`car.cpp`'s `closeResource`: `ArchiveManager::unload` deletes,
  without a reference count): `Kuruma` keeps the freed archives, and the
  race's scene search called through them (a crash at every race start, in
  `ResourceGroupManager::resourceExists` or `OSMScene::initialise`). The
  game's `ArchiveManager::unload` calls are left out (`ogreKeepArchives`):
  its archives stay loaded, and loading the same name again finds them.
- **Light shafts.** `Billboard_LightShaft_ps` writes `result.color` under a
  condition, which the rewrite turns into a `CMP` reading its destination,
  and ARB programs cannot read outputs: `igsNvProgram.c` now writes a
  fragment program's `OUTPUT`s in temporaries copied out at its end.
- `GrowMesh_*_cg_vs` (vertex texture fetch) are not supported, as for Speed
  Driver 3.

## Debugging

`IGS_TRACE`, `IGS_OGRE_LOG=1`, `IGS_SHOT`, `IGS_NV_DUMP` as for Speed
Driver 3 and 4. The loader reads the keyboard's state once a frame: a
scripted key press must be held (`xdotool keydown`, a short wait, `keyup`),
`xdotool key` is too short to be seen.
