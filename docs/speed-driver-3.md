# Speed Driver 3 (IGS, 2010)

Status (2026-11-12): **working; confirmed by desktop playtesting.**
Verified the attract sequence, coin/start, no-card selection, car/transmission
selection and a Shanghai race; holding the keyboard accelerator reached
79 km/h. A three-minute automated run completed without a Lua abort.
The user confirmed the game working after the rendering, steering and FFB
changes. Gamepad/evdev mappings are implemented but not hardware-tested. Physical card
reading/saving and LAN remain unverified or unimplemented. Force feedback
uses the shared evdev backend; physical wheel feel remains untested.
Not tested on Batocera.
This file is also the hand-off for whoever continues: everything found so
far is here.

## Running it

- Dump: `games/Speed Driver 3/` (TeknoRaven's package: `game/`, `data/`,
  `game.json`). The game runs from `game/`; `game/sd3` is unstripped with
  debug info (loader ID `0xb5771886`, file CRC `0x760014aa`).
- `scripts/linux/runsd3.sh` (`GAME_DIR` or `GAMES_DIR`, `LIBS32` with
  `/usr/lib32` and `/home/aderumier/code/jp-native/lib32`).
- Test copy used for this continuation: `/tmp/linuxloader-sd3-test` (reflink
  copy of the earlier `/home/aderumier/code/sd3-test`). The dump is unchanged.
- The launcher now supplies the required firmware script sizes
  `24836 7056 3728` (also specified in `game.json`). Without these, the
  firmware's boot Lua is not loaded and `update` is nil.
- A missing execute bit is handled through the host's 32-bit dynamic linker.
  `runsd3.sh` stages the six flattened library symlinks in a temporary
  directory, removes it on normal exit, and leaves the dump unchanged.
- Host dependencies still include 32-bit `libXmu.so.6` and `libXpm.so.4`.
  The test copy has these in `game/lib_extra/`, copied from Kitsune's
  `runtime/x11/`. They are not bundled by this change.
- Headless test:
  `GAME_DIR=/home/aderumier/code/sd3-test/game LIBS32=/usr/lib32:/home/aderumier/code/jp-native/lib32 timeout -s INT 90 gamescope --backend headless -w 1360 -h 768 -- script -qfc scripts/linux/runsd3.sh /tmp/log`
  (`script` makes the output line-buffered; otherwise it is lost on kill).
- `game/ogre.cfg` is windowed 1360x768.
- Visible desktop launch used for playtesting:
  `GAME_DIR=/tmp/linuxloader-sd3-test/game LIBS32=/usr/lib32:/home/aderumier/code/jp-native/lib32 gamescope --backend wayland -w 1360 -h 768 -W 1360 -H 768 -- scripts/linux/runsd3.sh`.
  Plain launch on the host display stalled before showing a window; the
  working desktop runs use Gamescope's nested Xwayland server.

## Debugging aids

- `IGS_TRACE=1`: failed protected Lua calls (including handled errors), board messages (`IGS A36:`), the firmware's console
  (its USART), every PC<->board interrupt with offsets and reply data.
  `=2`: board status each second, timer writes, the game's side of each
  interrupt. `=3`: every unknown board access, a backtrace of each
  command, the game's `__stack_chk_fail` caller.
- `IGS_A27_BREAK=addr,...`: ARM registers printed at those PCs (A36 too).
- `IGS_PATCH=addr:hexbytes,...`: patch the game's code at start. Ogre's
  log to stdout at full detail:
  `IGS_PATCH=085887b9:01000000,085887e2:03000000`.
- `IGS_NV_DUMP=<dir>`: each Cg-compiled program (`NNNN.txt`) and its ARB
  rewrite (`NNNN.arb`).
- `IGS_FFB_TRACE=1`: SD3 motor direction, raw magnitude and normalized force
  when the command changes, including when no FFB device is connected.
- The loader logs only errors inside the game process: use `printf`
  (`say()` in the IGS files) for traces.
- gdb: the launcher forks; run `./sd3` directly from `game/` under gdb with
  `set environment LD_PRELOAD=<repo>/build-linux/linuxloader.so`,
  `LD_LIBRARY_PATH` as runsd3.sh, and the `MESA_EXTENSION_OVERRIDE` of
  igsNvProgram.c.
- Offline shader check (scratch tools, rewrite if needed): a C harness that
  `#include`s `igsNvProgram.c` and rewrites dumped `.txt` files, and a
  Python/ctypes EGL-surfaceless script that loads each `.arb` into Mesa
  and prints `GL_PROGRAM_ERROR_STRING_ARB` (Mesa reports one line late).

## The A36 board (`src/loader/igs/igsA36.c`)

IGS E-3000: an IGS036 (ARM946E-S, ARMv5TE) with a 16 KiB internal ROM (not
dumped) plus an FPGA on the PCI side. SD3's firmware
`game/roms/sd3_eprom.bin` (512 KiB, only the first 0x6100 bytes are code,
the rest random padding; -O0 IGS "E-3000 ASIC2PC sample code") runs at
0x10000000 from reset and **never calls the internal ROM** (it installs its
own service table at 0x020026a0, magic 0x50505050). Emulated as is:

| Address | |
| --- | --- |
| 0x00000000 | internal ROM: our 8 vectors jump to the firmware's (0x10000000 + n*4) |
| 0x02000000 | SRAM 64 KiB (stacks at 0x0200f600-0x0200fe00) |
| 0x10000000 | the firmware |
| 0x30000000 | data window = the PC's BAR0 (512 KiB mapped by the PC) |
| 0x40000000 | command registers = the PC's BAR1 (word n = BAR1 + n*4) |
| 0x50000000 | external SRAM 512 KiB |
| 0x60000000 | window owner (0 board, 1 PC); 0x60000004 write 1 = interrupt to the PC |
| 0xfffa0000 | timers (MAME igs036.cpp), AIC sources 6-8 |
| 0xfffa4000, 0xfffa8000 | Atmel USARTs: the firmware's console (USART0) |
| 0xfffff000 | AIC (MAME atmel_arm_aic.cpp); the PC's interrupt = source 12 |
| 0xfffff200 | debug UART (console too); 0xfffffa08/0c as MAME |

Runs in its own thread at 100 M instructions/s kept to the wall clock,
sleeping in the firmware's idle loop (0x10002798-0x100027ac).

PC side, the IGS driver's `/dev/pccard0` (hooks on the game's `open`,
`close`, `ioctl`, `mmap` imports): ioctl 0x6600 reset (ignored), 0x6601
interrupt the board, 0x40046602 select the BAR the next mmap maps,
0x80046603 read "board interrupted the PC" (the game's thread polls it
every ms), 0x40046604 acknowledge, 0xc0106610 the FPGA cipher.

The game uploads the firmware through the internal ROM's loader
(`Real36::loadBinTo36("../sd3.bin")` -> `P2A_Loader*`): `P2A_Loader_iInit`,
`iReset`, `iUnInit` detoured to return 0, `P2A_LoaderAdv_iLoadData` starts
the board with the dump's ROM instead.

### Protocol (firmware 0x1000222c, game `Real36`/`A36Interface`)

- Command slot in BAR1: function, command, count, result, checksum;
  advances 5 words per command, wraps at 0x3c (game: commandPtr
  0x097bf590, actionPtr 0x097bf58c; firmware 0x0200002c). Wrong checksum:
  the firmware hangs.
- Input data in the window from word offset 0 (firmware 0x02000030),
  replies from 0xc000 (firmware 0x0200677c, game 0x09755338), reset to
  0xc000 when past 0x18000. Each word XORed with
  (offset + count + 0x5cfa37e9) + n, n counted from the last word
  (firmware 0x10001ff0, game `waitCommandData`).
- Handlers (table at 0x02002740): 0 0x10000a98, 1 0x10000ab8, 2 0x10000bd0,
  3 0x100012ec, 4 0x1000179c (FRAM: command -1 read, -2 write; data at
  0x50000000), 5 0x100019e4, 6 0x10001b2c (the key: 8 constant words
  `98d5674e 6f6a3ee7 cd871d67 7cb213a7 32febdca 3e4e927c 59376f6a 33777461`),
  7 0x10001c44 (info string "S-100CN"; TeknoRaven patches the ASIC version
  at 0x10001ccc), 8 0x10001df8, 9 0x10001e38.
- Firmware watchdog (timer task 0x10002070): if no command for 0x3aa ticks
  it rewinds the reply offset; ticks are ~0.2 s, so it should not fire.

### Fixes in the game (descriptor fields in `igsGames.c`)

- `a36ReplyPublish` 0x08b9ccb1: `A36Interface::dispatchAction` clears the
  waiter's flag before writing function/count/result; a waiter on another
  core read count 0, left the reply unread, and every later reply (the
  key) was read one word off. The 7 instructions are reordered.
- `oisScanJoys` 0x08ccb0c0: OIS `LinuxJoyStick::_scanJoys` overflows its
  evdev capability buffers on today's kernels (stack smashed); returns an
  empty vector (controls are the RS232 I/O board's anyway).
- `luabindGetErrorCallback` / `luaToLString`: print Lua errors. Hooking the
  getter survives `lua::State::open`, which replaces the callback for every
  Lua state. `luaPCall` additionally logs failed protected calls with
  `IGS_TRACE` enabled, without changing their status or stack.

## The FPGA cipher (`cipher()` in igsA36.c)

The game's "FPGA_AES" is not AES: per 16-byte block, bit i moves to bit
perm[i] (LSB first in each byte), then the block is XORed with a mask
chosen by the key. perm[] is in igsA36.c (same table in TeknoRaven's DLL at
0x68f8eb70 and Kitsune's core at file offset 0x792220). SD3's key (command
6, words 0-3 little-endian) takes mask `f187831641973171de7f762b95333c5a`
(Kitsune, profile 1; TeknoRaven's table mask for that key is wrong for
SD3). The game decodes with the direction flag 0 (the "encrypt" mapping).
Decrypted files end in [checksum][length] (checked by `FpgaBufferDecode`).

## Shaders (`src/loader/igs/igsNvProgram.c`)

The materials use Cg profiles vp40/fp40 (NV only). `MESA_EXTENSION_OVERRIDE`
advertises the NV program extensions (Mesa appends unknown names, with a
warning), Ogre and Cg pick vp40/fp40, and each compiled program is
rewritten into plain ARB assembly at `cgGetProgramString` (a filter hook
added to `src/loader/graphics/shaderPatches.c`, `cgCompiledProgramFilter`).
The rewrite handles precision suffixes, condition codes, conditional writes
and KIL, |abs| sources, vertex _SAT, SEQ/SNE/SGT/SLE/SFL/STR/SSG, DIV, NRM,
DP2, scalar sources, the 4-component address register, vertex.weight ->
vertex.attrib[1]; TXL -> TEX (lod lost), DDX/DDY -> 0. Programs with
branches are left as they are (they fail): none seen yet. All 40 programs
the first scene loads now load in Mesa.

Turbo also loads `Shimmer_distort2_VP` on its first activation. Its vertex
`SIN_SAT` and `COS` instructions used to reject the entire rewrite, leaving
Mesa with an unsupported NV program and turning the race scene almost black.
Vertex SIN/COS now use range reduction to [-pi, pi] and a degree-13 sine
polynomial; saturation still goes through the existing vertex clamp path.
The actual shimmer shader compiles in a Mesa EGL context. A GPU check of
both operations over [-100, 100] matches sine/cosine within framebuffer
colour quantization (maximum error 0.000974).
An automated Shanghai race reproduced the blackout before this change;
with the rewrite, repeated three-second Space presses retain the scene,
boost flames and distortion, without Mesa program errors.

TeknoRaven left its own translator in the dump: `game/nv_program_inspect`
(i386 PIE, **not stripped**, `sd3_nv_program_parse`,
`sd3_nv_program_to_native_arb`, `sd3_nv_program_to_glsl`; output uses
`uniform vec4 sd3_c[]`, `sd3_tex%u`, `sd3_attrib%u`, `sd3_weight`): the
reference if a GLSL path is needed (branches, TXL with lod). Also in the
dump from TeknoRaven: `shader_patch/` (patched Cg/materials),
`scene_patch/*.zip`, `local_config.lua` (offline database, card reader type
B).

## Desktop startup and controls (continuation)

The original Lua failure is fixed. `speed::initialize` expects three firmware
script lengths on the command line (or in `../filesize.txt`, absent here).
The launcher supplies the dump's `24836 7056 3728`. GDB traced the original
failure to `speed::run` calling the undefined global `update`.

The next failure was `game/db.lua:176`: `mac2uuid` parses the first line of
`io.popen("ifconfig eth1")`, even with the database disabled. `igs.c` supplies
legacy net-tools output with a stable locally administered MAC
`02:00:00:00:00:01`, and returns empty output for the boot script's two
`./curl` queries. These are memory streams, with matching `pclose` handling.
No host interface is created or changed. The identity is currently shared
between instances, so LAN/multiple-cabinet identities still need work.

`igsSd3.c` emulates the serial cabinet at the `GameIO` boundary instead of
opening a host serial port. `GameIO::initialize`, `GameIO::read`,
`input::VR::setFFB` and `GameIO::setBound` are descriptor addresses.
`read` fills exactly the first 0x4c bytes of the
object: three 32-bit ADC values (steering, brake, throttle), then two groups
of eight switches. The game's VR calibration, buffering and logical input
processing remain active. Steering follows `setBound`, with the ADC increasing
from left to right (corrected after desktop playtesting reported reversed
race steering; menu switch directions were already correct). Pedal ranges use
the dump's defaults (throttle 68..800, brake 42..370).

### Force feedback

`input::VR::setFFB` now routes the game's motor command to `evdevFfbConstant`.
The device is selected by the shared backend: with `INPUT_MODE 2`, the
controller mapped to `ANALOGUE_1`; otherwise the first evdev FFB device.
Wheels receive signed constant force and rumble-only devices receive its
magnitude as vibration. No device is required to play.

Binary observations: `speed::OutputJob::execute` (0x08613490) encodes the
physics force into `Motor[0]` (1 for negative, 0 for positive) and `Motor[1]`
(absolute force times the game's configured motor scale, default 63).
`GameIO::setMotor` (0x08aa9040) saturates the magnitude at 127. The loader
preserves that saturation and divides by 127 for the backend's [-1, 1] range.
The game already combines steering torque, conditional forces and transient
effects in `FFBMgr::update(float, Vehicle*)` (0x08b02650); the loader does
not add a separate artificial centering spring or damper. Zero commands
stop the force, including when the game disables FFB. A library destructor
also sends zero on normal shutdown; evdev removes effects when its fd closes.

The encrypted `config.lua` in this dump has `config.io.enableFFB = false`.
The original `input::VR::setFFB` (0x08aaa8b0) consequently replaces even
nonzero forces with an all-zero `Motor` before calling `GameIO::setMotor`.
Hooking the VR method preserves the game's calculated forces without
changing encrypted resources. Game-level FFB enable/disable in `OutputJob`
still controls whether force is generated.

Protocol checks cover both signs, partial/full force, saturation, zero,
unrelated lamp bytes, shutdown and operation without a device. Physical
wheel direction and feel still need hardware playtesting.
An automated Shanghai race with left/right steering and four turbo presses
was tested against a uinput FF_CONSTANT wheel: all 1,554 force changes
matched the kernel uploads exactly (levels -11094..16254), with zero force
and an effect-stop event at exit. The virtual device was removed afterward.

| Desktop key | Control |
| --- | --- |
| 5 / U | Coin |
| 1 / I | Start / confirm |
| Left / Right | Steering and menu arrows |
| Up / Down | Accelerator / brake and menu arrows |
| Space | Boost |
| Z | View |
| C / V | Gear up / down |
| F1 / P | Service |
| F2 | Test |

The shared desktop gamepad path supplies Start/Back, stick steering, trigger
pedals and face/shoulder buttons. Evdev uses the existing JVS state with
analogue channels 1/2/3 for steering/accelerator/brake. Neither was tested
with physical hardware. Card switches stay inactive; the no-card route was
tested (select the left option on the card-login screen, then Start).

Ogre's swaps now call `igsBeforeSwap` through the GLX bridge, so `IGS_SHOT`
also works for SD3. Verification artifacts from this session:

- `/tmp/sd3-race.log`, `/tmp/sd3-race-shots/`: 180-second run, coin/start,
  menus, Shanghai race and throttle. Frames 15 and 21 show the race.
- `/tmp/sd3-layout.log`: startup with flattened links and no execute bit;
  launcher used `/lib/ld-linux.so.2 ./sd3 24836 7056 3728` successfully.
- Full `cmake --build build-linux --parallel 4` passed. Existing unrelated
  compiler warnings remain. `bash -n scripts/linux/runsd3.sh` passed.

### Attract video / menu flat-colour fix

The intermittent white/gray UI and flashing plain colours in attract mode
were reproduced and traced with a temporary OpenGL logging shim. Ogre passed
valid identity texture matrices to `glLoadMatrixf`, then multiplied them by
heap garbage (including NaNs) from the GL renderer's automatic texture matrix.
The resulting GL texture matrix contained NaNs. This affected fixed-function
video/menu texture coordinates; shader-driven race materials rendered correctly.

The supplied `RenderSystem_GL.so` constructors at module offsets 0x23970 and
0x23e40 do not initialise the automatic texture-transform state. Its flag is
at object offset 0x4c0 and its 16-float matrix at 0x4c4. `igs.c` now interposes
`Ogre::Root::setRenderSystem` and initialises that state to inactive/identity
before rendering. The descriptor supplies the offset, and the hook checks the
renderer vtable belongs to `RenderSystem_GL.so`. Other renderer state and
valid later texture-transform updates are left to Ogre.

Verification: `/tmp/sd3-fixed-attract.log` and `/tmp/sd3-fixed-attract/`
show the warning/menu graphics and Theora intro rendering correctly, whereas
the unfixed `/tmp/sd3-video-shots/` contained uniform-colour frames.
`/tmp/sd3-fixed-race.log` and `/tmp/sd3-fixed-race/` also verify that the
integrated fix still reaches the Shanghai race with correct scene/HUD rendering.
The diagnostic shim and its logs (`/tmp/sd3-multmatrix.log`) remain scratch
artifacts, outside the loader. The integrated fix needs no extra preload.
The dump's patched materials also produce Ogre parser warnings.

Remaining work: verify steering/brake, gamepads and evdev under sustained
play; card emulation/persistence; LAN and
per-cabinet identity; sound verification; physical force-feedback testing; Batocera generator
(game in `game/`) and dependency packaging. The existing local_config.lua
still selects offline database and type-B/no-sensor card reader operation.

REA/Ghidra cold analysis timed out at its 330-second startup limit; no
complete decompilation evidence was obtained. Startup diagnosis instead used
local objdump/nm, decrypted scripts and GDB plus the measured runs above.

## References used

- MAME: `src/mame/igs/igs036.cpp`, `igs_m036.cpp`, `pgm2.cpp`,
  `src/devices/machine/atmel_arm_aic.cpp`, `src/devices/cpu/arm7/*`
  (commit 05957bcf6dfa adds the IGS036 device).
- TeknoRaven IGS core (user's): github hoyosec/teknoraven-releases,
  `core.igs.win-x86-0.9.879.zip` (`platforms/igs/TeknoRaven.dll`; SD3 hook
  list around 0x68b27f00, IROM_SIM, the cipher at 0x68b6a6e0).
- Kitsune (user's): github tsukina123/kitsune-releases
  `core-igspc-v0.1.10` (`kitsune-igspc-*-linux-x86_64.kcore` is a zip;
  `bin/kitsune_igs_core` x86-64 ELF: the SD3 cipher mask selection at
  0x1a54ee; `runtime/bridge/libGL.so.1` forwards GL to the core, not usable
  alone).

## Speed Driver 2

Not started. Its firmware `roms/SD2_EPROM.dec.bin` (already decrypted)
calls ~29 internal-ROM services (veneers at 0x1003050c+); Kitsune runs it
"with decrypted EROM and IROM services" and TeknoRaven has an SD2 IROM
compat layer: one of them is the reference for those services. The A36
board code here should mostly carry over (different memory sizes:
TeknoRaven's sd2.json `sram_ext_size` 0x1000000).
