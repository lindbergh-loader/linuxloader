# Namco ES1 (Nirin) — porting notes

State: **2026-10-07: 100% working** (user-confirmed). Nirin boots with the
I/O board linked and the dongle answered, plays from attract to races with
the picture right (the original shaders, see "Shaders": no flicker), sound (an
SDL stream with real audio), coin and start, the window at [Display] WIDTH x
HEIGHT and FULLSCREEN. The analog inputs (handle, accelerator, brake) are
wired for the desktop keys and for evdev, run through the cabinet's
calibration (see "Inputs").

## The platform

Namco ES1 games are **plain 32-bit Linux executables** (`exec/a.elf`), not
dumps: dynamically linked (SDL 1.2, OpenAL, GLU, GL, libstdc++), with their
`.symtab` (every function has its name). None of the Raw Thrills dump
machinery (import rebuilding, BSS clearing) applies. TeknoParrot runs them
with the same BudgieLoader as the Raw Thrills games (`ElfLdr2`).

Hardware the game talks to:

| What | How | Loader |
|---|---|---|
| I/O board | JVS on `/dev/ttyS2`, 115200 8N1, `n2Jvio*` library | `namco/namcoEs1.c` on `hardware/lindbergh/jvs.c` |
| Dongle | HASP HL API linked in (`hasp_*`), `/dev/Hardlock` | detoured by address |
| Sound | OpenAL 0.0.8 (shipped), OSS backend: `/dev/dsp` | `common/ossDsp.c` |
| Network | UDP/TCP link play, MAC of `eth0`/`eth1` | `SIOCGIFHWADDR` answered |
| Admin | `su -c` hwclock/date/ifconfig/sysctl/arping | not run |

## Game dump layout

```
Nirin/
  exec/      a.elf, data/, save0/, save1/ (cwd of the game), init, info
  i686/      the cabinet's userland (glibc 2.7, NVIDIA 177.82, ...): do not use
  deb/       arping packages
```

`exec/init` is the cabinet's start script: `cd /opt/arcade/exec; ./a.elf -netuse`.
The only library worth taking from `i686/lib` is `libopenal.so.0.0.0`
(OpenAL 0.0.8, libc/libm/libdl/libpthread only) — the same one Batocera's
Lindbergh package ships as `libopenal.so.0`.

## Launch

The ELF's interpreter is `/opt/arcade/i686/lib/ld-linux.so.2`, so the kernel
cannot start it. `namco/namcoEs1Launch.c` reads `PT_INTERP`, and when it is
missing runs the game through the host's 32-bit `ld-linux.so.2`
(`/lib`, `/lib32`, `/usr/lib32`, `/usr/lib/i386-linux-gnu`). LD_PRELOAD still
applies. The launcher prints `Namco: ./a.elf runs through /lib/ld-linux.so.2`.

- Loader id (partial CRC, `NIRIN_ES1`): `0x71e72ff6`
- File CRC (launcher, clean-dump table): `0x6d238b08`
- Test script: `scripts/linux/runnirin.sh` (`T=<s>`, `NAMCO_JVS_TRACE=1`,
  `NIRIN_LIBS=<dir with libopenal.so.0>`)

## Boot, in order (each was a blocker)

1. `su -c '...'` from `main`/`clAppSystem::system` → `system()` drops any
   `su -c` / `sudo` command (`namcoEs1DropCommand`).
2. `can't get macaddress!!` → `l_getMacAddr` (regparm, not detoured) asks
   `SIOCGIFHWADDR` for `eth0` then `eth1`; the loader answers with the first
   real interface's address (`namcoEs1HwAddr`), else `02:00:00:00:00:01`.
3. `hasp_login error!! : 14` → HASP detoured (below).
4. `JV I/O ERROR` on the boot screen → JVS on ttyS2 (below).
5. Crash in `clFrameBufferObject` → the loader's GL interposers
   (`glEnable`, `glBindTexture`, `glTexImage2D`...) call glad pointers that
   were never loaded. The game's `SDL_SetVideoMode` import (its GOT slot, not
   a global symbol: Raw Thrills detours that name) loads glad after it.
6. Crash in `libopenal.so.0` (`pthread_mutex_lock(NULL)`) at `seqAttract` →
   no `/dev/dsp`: the Raw Thrills OSS emulation now serves Namco games too.

## Video

`InitSystem(param, fullscreen)` calls `SDL_SetVideoMode(1360, 768, 0,
SDL_OPENGL [| SDL_FULLSCREEN])`. `SCREEN_W`/`SCREEN_H` are constants, but
only the scene is drawn at that size (into the game's own framebuffer
objects): the last pass is drawn at the window's size, so the game fills any
window by itself. The loader's hook of the `SDL_SetVideoMode` import opens
it at [Display] WIDTH x HEIGHT, fullscreen as [Display] FULLSCREEN says.

Do not use the Raw Thrills frame scaler (`frameScale`) here: it treats the
window's frame as 1360x768 and crops the picture to its bottom left corner.

A 16:10 screen stretches the 16:9 picture; KEEP_ASPECT_RATIO is not
implemented yet.

## JVS I/O board

`n2JvioInit` opens `/dev/ttyS2` (`O_RDWR|O_NOCTTY|O_NONBLOCK`), `tcgetattr`/
`tcsetattr` (must succeed), then speaks standard JVS: SYNC `e0`, escape `d0`.
Replies are polled with `FIONREAD` then `read`. The sense line is `TIOCMGET`,
CTS inverted: CTS set = the addressed board is the last one.

Connection (`n2JvioConnection` state machine): reset ×2, `f1` address,
sense, then `11 12 13` revisions, `10` ID, `14` features, `15` main ID
(`namco ltd.;N2;Ver0.50;JPN`), `70 18 'P' 'L' a b` (Namco-specific, wants
`01 01`), then every frame `20 02 02 21 02 22 08 32 02 00 00`.

A board does not reply to a reset: the reply queue is dropped then (the
game's next read would take it for the address ack).

`n2JvioAckTxIoId` checks the ID: field 0 must be `namco ltd.`, field 1 is
looked up in the game's board table (`0x838c880`: NA-JV, FCB, MINI-JV,
I/O CYBER LEAD, ASCA-1/3/5, TSS-I/O, MIU-I/O, TMIU-I/O, RAYS PCB, Jamma I/O,
EM I/O1-01/02, FCA-1, P-DRIVE PCB, DE-JV PCB), field 3 searched for
`SlotLink`. The loader's `NAMCO_NA_JV` board:
`namco ltd.;NA-JV;Ver4.00;JPN,Multipurpose + Rotary Encoder`, 2 players ×
16 switches, 2 coins, 8 analog inputs (16 bits), 16 outputs.

### Inputs

`clInputDevice` = `clInputDeviceJamma` (the board) **plus** an SDL 1.2
joystick device (`SDL_JoystickOpen(0)`): handle, accel and brake are the
sum of both. So a pad works through the game's own SDL, with no loader help
— without evdev input only: with `INPUT_MODE 2` the loader hooks the game's
`SDL_NumJoysticks` (0) and `SDL_JoystickOpen` (NULL), as the pad mapped in
`[EVDEV]` would otherwise reach the game twice, under the game's own button
numbering too (one pad button was both Enter and View).

- Analog: channel 0 handle, 1 accel, 2 brake (raw 16-bit, read at
  `0x91ad0e8`), scaled by the calibration in `clHandleSetting`
  (`save0/testmode/handle_setting.lua`; globals `0x91266b0` handle centre,
  `ac` left max, `a8` right max, `a4`/`a0` brake centre/max,
  `9c`/`98` accel centre/max). The dump's calibration is a real cabinet's:
  handle centre 40768, left 61056, right 18112 (inverted).
- The loader's analogs (ANALOGUE_1 steering, 0 left .. 0xffff right;
  ANALOGUE_2 accelerator and ANALOGUE_3 brake, 0 released .. 0xffff) are
  put in the game's calibration range at each packet (`calibrate()` in
  `namcoEs1.c`, from the `clHandleSetting` globals), so a pad or wheel works
  without calibrating in the test menu, whatever the saved calibration.
- Without evdev input (INPUT_MODE ≠ 2) the switches come from the desktop
  keys (`desktopInputState()`: 1 start, 5 coin, F1 service, F2 test, space), and
  the arrows drive as in Dead Heat (`keyboardAnalog()`): ←/→ steer (full lock
  in a quarter of a second, back to the centre as fast), ↑ accelerates, ↓
  brakes, through the calibration above; in the test menu they are the
  up/down switches again. The game's own SDL joystick still adds to them.
  The keys are read from the X server's key map whatever window has the
  focus (as for the Raw Thrills games): keys typed elsewhere reach the game.
- The switches, as played on Batocera (2026-10-26): Enter P1 button 1, View
  P1 button 2, shift up / down P1 buttons 3 / 4, menu up/down P1 up/down
  (space, Z, X, C and the arrows on the keyboard). TeknoParrot's profile
  (`Nirin.xml`) has View and Transmission on buttons 2 and 3 and the shift on
  P2 up/down, which does not shift. In the test menu the keyboard's F1
  (service) is Enter too; a pad's service button stays the service.
- `q` (key up) or the window's close quits the game itself; Esc/Alt+F4 come
  from the loader's quit watch.

### Real controls (evdev)

`INPUT_MODE 2` and a `controls.ini` (in the game's cwd, `exec/`, or `-o`) with
an `[EVDEV]` section. Each entry is `LOGICAL = /dev/input/eventN:TYPE:code`,
`TYPE` being `KEY`, `ABS` or the axis reversed as `ABS_NEG`. The board is
16-bit (`analogueInBits` 16, so `jvsBits` 16 and no `analogueRestBits` shift),
and `calibrate()` puts the loader's values in the cabinet's range, so nothing
is calibrated in the test menu.

| Control | `[EVDEV]` logical | Device |
|---|---|---|
| Steering | `ANALOGUE_1` | wheel axis (rest = middle = 0x8000) |
| Accelerator | `ANALOGUE_2` | gas pedal (rest 0) |
| Brake | `ANALOGUE_3` | brake pedal (rest 0) |
| Menu up / down | `PLAYER_1_BUTTON_UP` / `_DOWN` | pad / buttons |
| Enter / View | `PLAYER_1_BUTTON_1` / `_2` | pad buttons |
| Shift up / down | `PLAYER_1_BUTTON_3` / `_4` | pad buttons, paddles |
| Start / Coin / Test / Service | `PLAYER_1_BUTTON_START`, `PLAYER_1_COIN`, `TEST_BUTTON`, `PLAYER_1_BUTTON_SERVICE` | — |

The wheel's evdev axis is signed, so its rest normalises to 0.5 (0x8000,
`handleCenter`) and no `_NEG` is needed unless it turns the wrong way; the
pedals rest at 0 (mapped to `accelRest` / `brakeRest`). The channels rest at
those values before a device drives them (and with none connected), so the
handle does not sit full left. Deadzones are
`ANALOGUE_DEADZONE_<n> = start middle end` (percent) as for the other games;
a small middle deadzone on `ANALOGUE_1` squares the wheel's rest to the
calibration's centre. The game's own SDL joystick is hidden in this mode
(see "Inputs"): only the `[EVDEV]` mapping drives.

## Dongle

The only user is `clHASP::check()`: `hasp_login(0xffff0000)`, then
`hasp_read(fileId 0xfff0, offset 0xd00, 0x40 bytes)` and `hasp_decrypt` of
that buffer, whose C string gives the 12-character serial (`XXXXXX-XXXXXX`
once shown). No game data is encrypted, no recording needed. The loader
detours `hasp_login` (`0x83455c0`), `hasp_logout` (`0x8345660`),
`hasp_read` (`0x83463b8`, returns the serial in the clear) and
`hasp_decrypt` (`0x8345838`, leaves it). `data/lua/config.lua` also has
`USE_DONGLE`.

## Batocera

The rom is `/userdata/roms/rawthrills/Nirin.pc` (or a `.squashfs` of it):
`exec/` (the dump's) and `lib/libSDL-1.2.so.0` (sdl12-compat, as Pink
Panther's: Batocera has no 32-bit SDL 1.2; the loader adds `../lib` to the
library path). OpenAL 0.0.8 is `/lib32/extralibs/libopenal.so.0`. The
generator (`linuxloaderGenerator.py`) finds `exec/a.elf`, and gives the
`nirin` roms the `bike` controls: left stick or wheel steers, R2 gas, L2
brake, b Enter, y view, L1/R1 (paddles) shift up/down, up/down in the
menus; Start, Select coin, R3 test, L3 service as for all.

## Open items

1. Real controls, on the cabinet: the evdev path (wheel/pedals to
   `ANALOGUE_1`/`2`/`3`, a pad, the menu/gear buttons — see "Real controls
   (evdev)") is implemented and builds; it is not yet driven by real
   hardware. A pad drives all of it (Batocera, 2026-10-26); confirm the
   wheel and pedals feel right.
2. `/dev/mixer` is missing (the game logs it, harmless so far).
3. KEEP_ASPECT_RATIO on screens that are not 16:9.
4. Outputs (lamps: `32` GPO and `70 18 PL`) are acknowledged and dropped.
5. Batocera: see below; test on the machine.
6. Other ES1 games (TeknoParrot lists more with the same BudgieLoader):
   they should share this path, with their own descriptor in `namcoEs1Games.c`.

## Shaders: use the original ones, not the "flicker fix"

The dumps around are usually TeknoParrot's with Nezarn's Windows "Nirin
flicker fix" (2022) applied to `data/shader/` and `data/draw/shader/`. It
renames `half` (a GLSL reserved word) to `f_half`, unrolls the shadow/neon
loops over `gl_TexCoord[2 + loop]` into `if(loop == N)` chains and adds
`GL_ARB_shader_texture_lod`. On Linux this flickers. The original shaders
(from the V3.51 cabinet image, `Nirin.US-V3.51.VM`; a.elf identical, CRC
`0x6d238b08`) work once the loader's shader fixups (`namcoEs1PatchShader`)
rename `half` and size `gl_TexCoord` for the `[2 + loop]` index too.

The V3.51 image is a disk recovery: many files carry an extra suffix
(`0000.frag.txt`, `*.anm.db`, `*.pickle`...); strip it. Its contents are
otherwise identical to the TP dump, except that it lacks the six
`shader/multi_sample_*` files: take them from the TP dump (the fix's four
`.vert` ones only rename `half` to `halved`, harmless).
