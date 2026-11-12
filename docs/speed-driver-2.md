# Speed Driver 2 (IGS, 2007)

Native Linux support targets the supplied M207CNC executable and decrypted
S207CNC external firmware. The original executable and ROM remain unchanged.
The loader runs the firmware, including its game flow and car physics, using
its ARMv5TE interpreter and a compatibility implementation of the missing
internal-ROM services. No Kitsune or TeknoRaven process is needed at runtime.

## Running

Build with `cmake -S . -B build-linux` and
`cmake --build build-linux --parallel 4`, then:

```sh
GAMES_DIR="$PWD/games" \
LIBS32=/usr/lib32:/home/aderumier/code/jp-native/lib32 \
  scripts/linux/runsd2.sh
```

`GAME_DIR` overrides the complete game directory. It should contain `sd2`,
`data/`, `lib_extra/`, and `roms/SD2_EPROM.dec.bin` (512 KiB). The launcher
recognises the original executable (file CRC `2593d7f6`, code ID `2602638a`).
It stages a temporary copy with `/lib/libcasmcard.so` resolved by SONAME,
executes it through the host's 32-bit linker, and removes it on normal exit.
The script similarly stages the dump's flattened GLU symlink. Cabinet
`/data` and `//data` paths resolve inside the game directory; bookkeeping
and operator settings use the game's own files there.

Dependencies include 32-bit SDL 1.2 / sdl12-compat, OpenGL/GLU,
`libstdc++.so.5`, `libncurses.so.5` and `libtinfo.so.5`. Set `LIBS32` to
additional library directories. On this machine the three legacy libraries
are staged in `build-linux/igs-sd2-libs/`, which the script also searches;
these are local build artifacts, not committed dependencies. Do not add an
old runtime's entire libc directory to the library path.

## Controls

| Keyboard | Cabinet input |
| --- | --- |
| 5 / U | Coin |
| 1 / I | Start / confirm |
| Left / Right | Steering / menu selection |
| Up / Down | Accelerator / brake |
| Space | Turbo |
| Z | View |
| C / V | Gear up / down |
| F1 / P | Service coin |
| F2 | Operator test menu |

The desktop gamepad uses its left stick, trigger pedals and the shared
button mappings. Evdev uses analogue channels 1/2/3 for steering,
accelerator and brake, with the shared start/service/test/button mappings.
Physical gamepads and wheels remain untested. Digital controls enter the
firmware through the PC switch words (the cabinet's `io.inf` indices);
analogue controls enter its serial receive buffer, with the original packet
checksum, decoding, debounce and calibration retained. Keyboard Left/Right
also feed the firmware's digital wheel inputs (io.inf indices 58/22), which
the selection menus require. Physical cards and LAN are not implemented.

Force feedback uses the firmware's outgoing I/O-board motor packets and
the shared evdev backend: constant force on a wheel, or rumble on a pad.
The motor byte contains enable/direction flags and a bit-reversed six-bit
magnitude; the firmware supplies the mixed steering and road effects.
Packets are checked for length and checksum, and normal shutdown stops
the motor. With evdev input, FFB follows the device assigned to steering;
otherwise it uses the first FFB-capable device. Set `IGS_FFB_TRACE=1` to
log changes in the motor output.

Menus use the accelerator (Up) to confirm team, car, transmission and
mode/course choices. Start confirms the card prompt; choose the no-card
option to play without a card reader. A coin during the initial warning or
logo may be ignored; insert after the title screen appears.

## Implementation

`src/loader/igs/igsSd2.c` implements SD2's PLX9030 boundary and board. The
PC's mapped BAR2 is 128 KiB shared RAM; BAR3 contains its command writes.
The board's command output is a separate latch: PC writes must not change
the last reply. Each exchange has two sequence increments, with bit 7 set;
publication updates both the output latch and shared offset `0x3000`.

The firmware is mapped at `0x10000000` (also `0x30000000`), internal SRAM
at `0x02000000`, external SRAM at `0x20000000`, and shared RAM at
`0x40000000`. The compatibility bootstrap starts at `0x10000000` with
CPSR `0x53` and banked stacks at `0x2001f600` through `0x2001fe00`.
This is not an authentic internal-ROM reset implementation.

The EROM's veneers at `0x1003050c..0x100305fc` name the missing services:
interrupt setup/acknowledgement, timers, GPIO, command latches, mailbox
status, random numbers and board identification. Unknown services stop the
CPU. IRQ 3 handles the PC doorbell; IRQs 6 and 7 run the firmware's timers,
using its 65 MHz timer clock. CPU execution is synchronous at the PLX
boundary, avoiding concurrent PC/ARM writes to the shared window.

`IGS_TRACE=1` logs switch changes, `=2` adds periodic board/sequence status,
and `=3` includes service calls and MMIO. `IGS_SHOT=<directory>` saves
frames using the shared IGS capture path. `IGS_SHOT_PERIOD` controls the
capture interval in seconds.

## Validation

Verified on the desktop host through headless Gamescope/Mesa: warning,
logos, animated title, coins/start, no-card play, team/car/transmission and
story selection, then a rendered race. Accelerator reached 113 km/h and
turbo reached 184 km/h. The run completed more than three minutes without
an ARM halt or game crash. A follow-up run verified keyboard Right changes
Future Driver to Sirius and Left changes it back. A virtual evdev FFB wheel
received changing positive/negative constant forces during a race, plus
zero-force commands. Physical wheel feel remains untested.
Race steering was confirmed by the user; braking, the operator menu, audio,
physical controllers and Batocera still need playtesting. Test runs use
`/tmp/linuxloader-sd2-test`, leaving the supplied dump unchanged. Logs and
captures are in `/tmp/linuxloader-sd2-race.log` and
`/tmp/linuxloader-sd2-race-shots/`.

The standalone regression check uses the supplied firmware without
redistributing it:

```sh
cc -O2 -ffunction-sections -fdata-sections \
  -Isrc/SDL3 -Isrc/minhook/include \
  tools/tests/igs-sd2.c src/loader/igs/igsArm7.c \
  -Wl,--gc-sections -o /tmp/igs-sd2-check
/tmp/igs-sd2-check 'games/Speed Driver 2/roms/SD2_EPROM.dec.bin'
```

It checks boot, mask-ROM service ABI/interworking, directional latches,
timer interrupts, all 256 motor-byte values, packet checksum rejection,
motor shutdown, 140 real firmware exchanges including sequence wrap, and
rejection of unknown internal-ROM services. The final rejection deliberately
prints an error before reporting success.

Reference binaries inspected: the user's Kitsune IGS core 0.1.10
(`Sd2EromBoard` service dispatcher at `0x387ab0`) and
`core.igs.win-x86-0.9.879.zip` supplied by the user (TeknoRaven.dll SHA-256
`3f6fab804a708766f658338396af589e70d99dfec6fb5c69f2edc099af0e3478`).
The supplied SD2 shim's named input handlers also establish the PC switch
indices. REA/Hopper inspection of the original executable identifies the
PLX initialisation at `0x0810fe68`; ordinary disassembly and measured game
runs establish the remaining implementation details.
