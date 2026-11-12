# Speed Driver 4 (IGS)

Native support targets Speed Driver 4 World Fever (`olympus`, IGS 2012), the
E3000 sequel to [Speed Driver 3](speed-driver-3.md): the same cabinet
(serial wheel and pedals board, force feedback) and A36 board. Desktop
(headless Gamescope/Mesa, 2026-11-12): loading, attract (videos, demo
races), coins, start, the no-card route, car selection, transmission and
mode choice, and a solo race on the Mediterranean course. Played on the
desktop by the user (2026-11-12): working 100%, after the UI shader fix.
Not on Batocera yet.

## The dump

`games/Speed Driver 4/` is built from the cabinet's disk
(`e3000_speeddriver4.chd`) by `tools/igs-e3000-extract.py`; how the disk
and the board's firmware are protected is in [IGS E3000 dumps](igs-e3000.md).

| Path | Content |
| --- | --- |
| `game/` | the cabinet's `/SD4/readwrite`: `olympus`, its libraries (Ogre 1.8, Scaleform, Cg), settings, logs |
| `game/olympus` | UPX unpacked (file CRC 0x7c88fcdb, loader ID 0xee0de033), with its symbols; `olympus.upx` the original |
| `game/roms/sd4_eprom.bin` | the board's firmware, decrypted from olympus (`tools/igs036-firmware.py`) |
| `data/` | the cabinet's `/SD4/data`: models, tracks, sounds, videos |
| `readwrite` | a link to `game`: the force feedback's effects are named `../readwrite/physics/ffb/...` |

The game runs from `game/` (`../data`) as `./olympus 24636 9344 2016`
(its `go_product`): `scripts/linux/runsd4.sh` (`GAMES_DIR` or `GAME_DIR`;
`LIBS32` for the host's 32-bit libraries, e.g. the loader's `libglut`).
`build-linux/igs-sd4-libs/` holds what neither the dump nor the host has
(local, not committed): `libjpeg.so.62` and `libpng12.so.0` (from Speed
Rider 2's dump), `libtheoradec.so.1` (Speed Driver 3's) and an empty
`libtiff.so.3` (needed, no symbol used: `gcc -m32 -shared
-Wl,-soname,libtiff.so.3`). `build-linux/igs-sr2-libs/` is on the path too
(libSM, libICE, libXt, libXaw, libogg, libvorbis, libpcsclite).
`ogre.cfg` asks for a 1366x768 window.

The game writes its logs to `game/log/` and settings beside them, as the
cabinet does.

## Controls

Speed Driver 3's (`igsSd3.c`, its `GameIO` stand-in: `GameIO::read` keeps
SD3's layout):

| Keyboard | Cabinet input |
| --- | --- |
| 5 / U | Coin |
| 1 / I | Start / confirm |
| Left / Right | Steering, and the menus |
| Up / Down | Throttle / brake, and the menus |
| Space | Boost |
| Z | View |
| C / V | Gear up / down |
| F1 / P | Service |
| F2 | Test |

The desktop gamepad steers with its left stick, RT throttle, LT brake; the
force feedback goes to an evdev wheel as Speed Driver 3's (`IGS_FFB_TRACE`).
The card prompt's no-card choice plays without one.

## The board (`src/loader/igs/igsA36.c`)

Speed Driver 3's A36 emulation, with:

- **Internal-ROM services** as Speed Rider 2's, another SRAM table:
  `0x0200f300` (arguments `0x0200f200/4`, result `0x0200f100`). Without it
  the firmware's baud-rate division jumps to 0 and it reboots in a loop.
- **Idle** `0x100029f0..0x10002a04` (it polls `0x02006784`). The board
  thread now runs in steps of 2000 cycles when an idle range is known, so
  it sees the loop soon after it starts: about 5 % of a core instead of 80.
- **The upload**: `P2A_LoaderAdv_iLoadDataPointer` (the firmware embedded
  in olympus at 0x09020600) in place of Speed Driver 3's `iLoadData`.
- `A36Interface::dispatchAction`'s reply race (`0x08719301`): SD4 clears
  the wait before the reply's last word; the patch now moves the wait's
  `movb` after the stores, whatever their layout (SD3, SR2, SD4).

## The FPGA cipher (scripts)

SD3's cipher with SD4's mask, `d05e9abfd7d2b815921d588563b0e49b` (found
from the scripts), and a new rule: **the mask changes every 64 KiB** of a
file, the same sequence in every file (`sd4ChunkMasks`, 16 masks from the
largest script, `local_rank.lua.enc`, 0.95 MiB). The game sends a file as
64 KiB requests (the last shorter) or, with its "CBC mode" (ioctls
0x6614/0x6615, accepted), as one: each 64 KiB takes the next mask, from the
first again after a file's last request.

## Graphics

Ogre 1.8 GL on Mesa, the vp40/fp40 programs through `igsNvProgram.c`:

- **gp4 profiles.** The car paint (`all_purpose_car_ex`) is for
  `gp4fp`/`gp4vp` (NV_gpu_program4), its Direct3D twin for ps_3_0: Ogre is
  told the extension is there and Cg compiles the gp4 programs for
  fp40/vp40 (`cgCreateProgramFilter`), with the twin's `-DCENTRIOD=0`.
  Without it the cars draw as their red occlusion silhouette.
- **`Material::getBestTechnique`.** IGS's Ogre takes the default scheme's
  slot unchecked when a material has no technique for the active scheme:
  with a material whose supported techniques are all in other schemes it
  crashed on the car selection. When that slot is empty the first supported
  technique is used (`igs.c`, the original bytes checked).
- **The UI** (Scaleform GFx 4, `libgfxrender_gl`): its filter shaders
  (blur, glow, shadow, Scaleform's shader types 230 and up) call
  `texture2DLod` in fragment shaders, which NVIDIA's GLSL takes and Mesa's
  only with `GL_ARB_shader_texture_lod` enabled. One failed shader and
  `HAL::InitHAL` fails: no UI at all, no menus nor HUD (the screens looked
  frozen: only the 3D behind them moved). After `Extensions::Init` its
  `glShaderSource` pointer is replaced by one adding the extension to such
  fragment shaders (`igs.c`).
- `GrowMesh_*_cg_vs` (vertex texture fetch, `arbvp1`) are not supported,
  as on the cabinet's GL; the blur programs (loops) stay as they are.

## Debugging

`IGS_TRACE` as Speed Driver 3's (its own addresses' traces now keyed to its
file CRC); `IGS_OGRE_LOG=1` prints Ogre's own log (unsupported techniques,
Cg errors), which the game does not write; `IGS_SHOT` frames. The board
thread is named `igs-a36`.
