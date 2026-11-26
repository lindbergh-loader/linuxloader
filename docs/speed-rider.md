# Speed Rider (IGS)

Native support targets Speed Rider (`SR`, IGS 2009, M-109CNC / S-100CNC),
a motorbike racing game by IGS: one seat, a leaning bike with throttle and
brake levers, an IGS036 protection board behind a PLX9030, and an STM32
cabinet controller. Played on the desktop (2026-11-12, user: working 100%):
warning, attract, coins, start, mode / course / bike select, Story races.
Not played on Batocera yet.

## The dump

`games/Speed Rider/` is a TeknoRaven package (its `game.json`):

| Path | Content |
| --- | --- |
| `game/SR` | the game (file CRC 0x2770c27e, loader ID 0xdc9fbc3b) |
| `game/libso/` | the era's Ogre 1.4.9, its plugins, Cg, OIS and X11 libraries |
| `game/roms/sr1_eprom.bin` | the board's decrypted external ROM (2 MiB) |
| `game/config/nvram_sr1.bin` | the board's NVRAM (0x1d00 bytes, settings and calibration) |
| `data/` | models, sounds, `zip/*.zip` (Ogre's `zipsep` archives), `Ogre.cfg` |

Run from `game/`: `scripts/linux/runsr.sh` (`GAMES_DIR` or `GAME_DIR`;
`LIBS32` for further 32-bit libraries). The script repairs `libso/`'s
flattened SONAME links in a temporary directory, leaving the dump as it
is. `build-linux/igs-sr-libs/` holds what neither the dump nor the host
has: `libstdc++.so.5`, `libXmu.so.6` and `libglut.so.3` (local build
artifacts, not committed). `Ogre.cfg` gives the 848x480 window.

The loader writes the NVRAM back (atomically, every two seconds when it
changed, and at exit). Without a valid NVRAM the game stops on "values not
set: VR calibration, link setting"; TeknoRaven's file has both set.

## Controls

| Keyboard | Cabinet input |
| --- | --- |
| 5 / U | Coin (2 coins a credit, 1 credit a game by default) |
| 1 / I / Space | Start / turbo |
| Left / Right | Lean, and the menus' left/right |
| Up / Down | Throttle / brake (and their switches) |
| F1 / P | Service (a service credit) |
| F2 | Operator test menu |

The desktop gamepad uses its left stick to lean and its triggers for
throttle (RT) and brake (LT). Evdev uses analogue channels 1/2/3 for lean,
throttle and brake. The cabinet has no force feedback: its operator menu
tests inputs and lamps only, and the board's outputs during a race are
lamp bits.

## The board (`src/loader/igs/igsSr1.c`)

The board is Speed Driver 2's (`igsSd2.c`): the firmware runs in the
loader's ARMv5TE interpreter (`igsArm7.c`), mapped at `0x10000000` (also
`0x30000000`), internal SRAM at `0x02000000`, external SRAM at
`0x20000000`, the PC's shared RAM (PLX BAR2) at `0x40000000`. The PLX
library calls and the game's doorbell/command-latch functions are
detoured, and the board runs synchronously at the PLX boundary. The
missing internal ROM is represented by its services (veneers at
`0x10008168..0x1000823c`): interrupts, three timers (IRQs 6-8, 65 MHz),
GPIO, the command latches, division, random numbers. Unknown services
stop the CPU.

The STM32 is on USART0 (IRQ 4; `igsStm32.c`, shared with
[Speed Rider 2](speed-rider-2.md)). The firmware's handler reads CSR without
IMR, so the transmitter and receiver enables are modelled: TXRDY/TXEMPTY
only while the transmitter is enabled, the reply only once the receiver is
(otherwise the firmware re-sends bytes and loses the reply's first one).
After a two-packet handshake (`55 aa 11` / `aa 55 22`, `66 99 aa` /
`99 66 bb`), each ten-byte command (`0x18`, lamps, byte sum) gets an
eleven-byte reply (`0x44`, decoded at `0x10004782`):

| Bytes | Field |
| --- | --- |
| 1-5 | four 10-bit pots: VR1 lean, VR2 throttle, VR3 brake, VR4 unused |
| 6-7 | coins since the last reply: coin 1 (5 bits), coin 2 (6), coin 3 (2) |
| 8-9 | the 10-bit switch word, active high |
| 10 | byte sum |

The switch word's bits, as the firmware's input printout names them
(`0x10002f14`): 0 start/turbo, 1 left, 2 right, 3 test, 4 service,
5 sensor, 6 down, 7 up. The firmware turns them into key events and
XOR-keyed state words in shared RAM for the PC; it owns credits and the
pots' calibration.

## Graphics

Ogre 1.4.9's GL render system on the host's Mesa. The package's
`Resource.cfg` adds TeknoRaven's `data/sr1_render/` (GLSL programs, unused
by the original materials). The bike and rider materials (`moto.material`,
`damnit_shader.program`) are Cg programs for vp40/fp40 only: without
NVIDIA's program extensions they drew untextured. As for Speed Driver 3,
`igsNvProgram.c` advertises the extensions to Mesa and rewrites Cg's
NV-option ARB assembly into plain ARB programs; Speed Rider's fragment
programs needed its `SHORT OUTPUT` declarations handled. Ogre's plugins
named `./Plugin_*.so` load from `libso/` (`igsRedirectPath`). OIS's
joystick scan is skipped (its evdev structures predate modern kernels;
input comes through the board).

## Debugging

`IGS_TRACE=1` logs switch changes, `=2` adds periodic board status and
the STM32 lamp commands, `=3` service calls and MMIO.
`IGS_SR1_BREAK=address[,...]` prints the registers at up to eight ARM
addresses. `IGS_SHOT=<dir>` / `IGS_SHOT_PERIOD` capture frames,
`IGS_NV_DUMP=<dir>` the compiled programs and their rewrites.

Earlier attempts crashed once in a session (an Ogre `Cannot create GL
vertex buffer` exception, then a segfault at shutdown), before Start
worked; it has not recurred.
