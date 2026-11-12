# Speed Driver (IGS)

Native support targets Speed Driver (`sdlinux`, IGS 2004, its title screen
"Speed Driver Evolution"), a two-seat racing game by IGS: one PC drawing
both seats' views side by side (1280x480), two sound cards, and the A27
protection board, which runs the cars. Played on the desktop (2026-11-12,
working): attract, the test menu with its KEY TEST and driving calibration,
coins, start, car and stage select, races. Not played on Batocera yet.

## The dump

`games/Speed Driver/` is a TeknoRaven package (its `game.json`); its
emulator is not in it, only the game:

| Path | Content |
| --- | --- |
| `game/sdlinux` | the game (file CRC 0x2336a62b, loader ID 0x5beeb93f) |
| `game/sd/` | the cabinet's `/sd`: models, sounds, `config/` (settings, bookkeeping) |
| `game/roms/sd1-yueluo.bin` | the A27's external ROM (4 MiB; the firmware is its first 20 KiB) |
| `game/lib_extra/` | IGS's SDL_mixer (with its second card's `Mix_*2` calls), the card reader's `libcasmcard.so` and `libpcsclite.so.0`, Ogg Vorbis |

Run from `game/`: `scripts/linux/runsd.sh` (`GAMES_DIR`, `LIBS32` for SDL
1.2 as for the other games).

## The A27 board (`src/loader/igs/igsA27.c`)

An IGS027A: an ARM7TDMI with a 16 KiB internal ROM (not dumped) and
peripherals of its own, running the external ROM. Much of the game runs
there (each seat's car, from the PC's controls and its requests), so it is
emulated as it is: the CPU is interpreted (`igsArm7.c`, ARMv4T), the
IGS027A's timers and interrupts are MAME's (`src/mame/igs/igs027a.cpp`:
the timer at 4263 * (rate + 1) cycles of its 22 MHz, the pending bits read
and cleared at 0x70000200), and the board's own hardware is read from the
firmware:

| Address | |
| --- | --- |
| 0x08000000 | external ROM; its first table's third word is main, the internal ROM's IRQ goes to 0x0800002c |
| 0x10000000, 0x18000000 | internal and external RAM |
| 0x28000000, 0x28100000 | the controls' bus, high and low byte, inverted |
| 0x28200000 | its select: a seat's switches (`v`, 0x76) or its converter (`s`, 0x73), the nibbles swapped for seat 2; lamps |
| 0x28300000 | the converter's channel (bits 3-4): wheel, brake, gas (10 bits) |
| 0x38000000 | the PC's command (read); the board's answer (write: a counter, 0xf1 when the reply is ready) |
| 0x48000000 | RAM shared with the PC: the request (0x230 bytes) at 0, the reply (0x23c) at 8 + word 0 * 1024 + word 1 |
| 0x58000000 | read: the PC's interrupt acknowledged |

The game's side, the IGS driver's `/dev/pccard0`: `lseek` sets the next
command (the game's sequence number; 0xfe starts the board; the board takes
0xf0-0xff as commands of its own, which stop it: the others are kept below
0xf0), `write` hands the request over and raises the board's interrupt,
`read` waits for the board's 0xf1 and returns the reply. The board is
pipelined: a request is taken only once the last one's step is done, so
after each reply the board runs on for its step (2 ms of its time) before
the game's next request.

The board's clock runs at its 22 MHz while it waits for the game, and
faster when the game asks more often than its timer answers: timings the
board measures (a coin held 2 to 13 of its 20 polls a second) are counted
in its time.

## Controls

| Cabinet | Desktop | Gamepad | evdev |
| --- | --- | --- | --- |
| Wheel | arrows | left stick | ANALOGUE_1 (seat 2: 4) |
| Gas | Up | R2 | ANALOGUE_2 (5) |
| Brake | Down | L2 | ANALOGUE_3 (6) |
| START | `1` (seat 2: `2`) | Start | START |
| VIEW | `z` | Y | BUTTON_2 |
| TURBO | Space | A | BUTTON_1 |
| Gear down / up | `c` / `v` | L1 / R1 | BUTTON_4 / BUTTON_5 |
| Coin | `5` (seat 2: `6`) | Back | coins |
| Test, service | F2, F1 | R3, L3 | |

Seat 2 is the second gamepad (and WASD for its arrows). In the test menu,
GEAR UP and GEAR DOWN move, START selects. The pedals' and wheel's ends go
past the calibration the dump's settings keep (Driving Calibration in the
test menu).

The switches, as the board reads them (seat 1's word also has the
cabinet's): coin 1 0x0001, test 0x0002, service 1 0x0004, START 0x0100,
VIEW 0x0200, TURBO 0x0400, gear down 0x0800, gear up 0x1000, service 2
0x2000, coin 2 0x4000.

## Display

The game draws both seats' screens side by side (1280x480). By default the
window shows seat 1's only (640x480, scaled like the other IGS games'
frames); `[Display] SEATS = 2` shows both. sdl12-compat's own OpenGL scaling
is off (`SDL12COMPAT_OPENGL_SCALING=0`): its framebuffer would cover the
loader's scaled frame.

## Force feedback

The game asks the board for each seat's wheel motor in its requests (byte
0x10 seat 1, 0x12 seat 2): bit 7 on, bit 6 the direction (set: the MOTOR
TEST's +31, taken as pulling left), bits 1-5 the strength. Seat 1's is the
loader's force feedback device's constant force (evdevFfb: the steering
wheel with evdev input, else the first device with force feedback; a pad
rumbles as hard). Not felt yet: the direction is a guess from a race's
values.

## Other hardware

- The second sound card: IGS's SDL had a second audio device
  (`SDL_OpenAudio2` and the rest, `igsAudio2.c`): an SDL3 stream of its own
  on the host's output.
- `/dev/mem`: the game looks for its motherboard in the BIOS to pick its
  mixer settings (`umix`): `/dev/zero`, the default ones. Its shell
  commands (`umix`, `hwclock`) are not run.
- The card readers (SLE4442 player cards, through PC/SC): none; the game
  plays without cards.

## Batocera

The rom is the package's directory or a `.squashfs` of it, in `igspc`. The
generator (kind `speeddriver`, from the rom's name) starts the linuxloader
in its `game/` with `lib_extra/` in the library path and sdl12-compat's
`libSDL-1.2.so.0` from beside the linuxloader (Batocera has no SDL 1.2),
full screen at the screen's size. evdev, a seat per pad or wheel: left
stick or wheel, R2 gas, L2 brake (ANALOGUE_1-3, seat 2 4-6), b turbo, x
view, L1/R1 gear down/up, Start, Select coin, R3 test.

## Known issues

- A few textures are missing from the dump (`shanghai_r/Bgu03.pvr`,
  `k1pt5604.pvr`, `rmov5601.pvr`).
- The lamps (the board's outputs): not used.

## Debugging

- `IGS_TRACE=1..4`: the board's commands and answers, its interrupts,
  unknown accesses, every access of the board's hardware.
- `IGS_A27_BREAK=address[,...]`: the ARM's registers there.
- `IGS_A27_SWITCHES=seconds:seat:mask[,...]`, `IGS_A27_COINS=seconds:seat[,...]`,
  `IGS_A27_ANALOG=seat:channel:value[,...]` (hexadecimal): the controls
  forced (seat 0 or 1).
- `IGS_SHOT=<dir>` as for the other IGS games (the whole window).
