# Gaelco (Tokyo Cop, Ring Riders, Gaelco Championship Tuning Race)

Native support for Gaelco's PC-based games: 32-bit Linux 2.4 executables
(X11/GLX, written for a GeForce 3/4/FX) on Gaelco's own Debian. The system
is `src/loader/gaelco/`; the games run on the loader's Lindbergh path (the
X11/GLX bridges stand in for their window, the JVS state is their input),
as the Teamplay games do. Played on the desktop 2026-11-14: Ring Riders and
Tuning Race working 100%, Tokyo Cop's steering confirmed.

| Game | Dump | Executable | File CRC | Loader ID | Frame |
| --- | --- | --- | --- | --- | --- |
| Tokyo Cop (2003) | `games/gaelco/Tokyo Cop/Tokyo Cop/` (its `salidas`) | `gameport` (= `car`) | 0x7a518e3b | 0x0bbe26aa | 640x480 |
| Ring Riders (2004) | `games/gaelco/Ring Raiders/Ring Riders/` (the whole disk) | `home/joc2001/joc2003/salidas/gameport` | 0x51ea930f | 0xeb10f0c5 | 640x480 |
| Gaelco Championship Tuning Race (2005) | `games/gaelco/Gaelco Championship Tuning Race/Gaelco Championship Tuning Race/` (the whole disk, its aux partition at the root) | `home/joc2001/joc2004/salidas/car` | 0x6f1e5179 | 0x66c6a2e5 | 720x540 |

Run from the game's `salidas` directory: `scripts/linux/runtc.sh`,
`scripts/linux/runrr.sh`, `scripts/linux/runtuning.sh` (`GAMES_DIR` or
`GAME_DIR`, `LIBS32` for a 32-bit `libglut.so.3`). Ring Riders and Tuning
Race link `libXmu.so.6` without calling it: their `salidas/lib/` needs a
stand-in (`gcc -m32 -shared -fPIC -Wl,-soname,libXmu.so.6 -o
lib/libXmu.so.6 empty.c`), as Rockin' Bowl-O-Rama's.

The games write their settings and records into the dump as on the
cabinet (`save0/`, `save1/` `switch.bin`, Tuning Race's records at the
disk's root); the Lindbergh set-up writes `eeprom.bin`/`sram.bin` into the
game directory.

## The cabinet's kernel module (`gaelco.c`, `gaelcoSound.c`, `gaelcoIo.c`)

`sndgaelco.o` puts its functions in the system call table (222-233); the
games reach them through wrappers of their own (`int 0x80`), which today's
kernels take for the xattr calls. The wrappers are replaced by name
(`iogaelco_*`, Tokyo Cop's `iolinux::*` methods), found in the games'
symbols. The module's source is in Tuning Race's dump (`sndio.c`,
`camera.c`, `iocommands.h`).

- Sound: a sampler. The game hands the module its sample ROM (`roms.new`)
  and commands (set a channel up with a sample, start, pitch, panning,
  volume ramps, key off), validated in batches; the module mixed 8-bit,
  16-bit and IMA ADPCM channels onto the AC97 codec, 4 channels at 48 kHz.
  The same sampler fills an SDL3 stream, folded into stereo.
- The I/O board: two switch bytes, a key encoder (a 4x4 matrix: service,
  test, volume up/down, the keypad 1-9 * 0 #; one key at a time), five
  potentiometers. Tokyo Cop reads them in the board's PCI configuration
  space (registers 0x40-0x47), the later games in its memory window
  (offsets 0-8). Coins are the module's counters.
- Tokyo Cop's module numbers its commands 0-11 (8, 9: port I/O, 11: the
  sound commands run); the later one's 8, 9 are the memory window, 11-15
  more ports, 21-23 Tuning Race's camera (no frame is ever captured: the
  player's photo is skipped).
- The vertical blank count: 60 Hz; the VGA retrace bit (port 0x3da) too.
- Ring Riders' motion drive (`gaelcoVariador.c`): the frequency drive
  tilting the bike, on the board's RS485 port, EI-Bisynch frames. The game
  programmed and read back about twenty parameters at boot, waiting 200 ms
  for each answer (the window frozen for 40 s): its platform set-up is
  replaced (no drive programming) and its string read returns at once; the
  emulated drive keeps what it is sent during the game. There is no
  motion. While the game loads, its reads handle the window's events (the
  desktop took it for hung).
- Tokyo Cop's motion platform: its loop (`ProcesaPlataforma`) self-tests
  the platform unless the test menu's "Motion system" setting is off
  (`switch.bin` offset 0x34): with no platform, "MOTION SYSTEM ERROR!"
  stayed on screen. The loader takes the setting's off path whatever is
  saved.
- Tuning Race's built-in VidMode client: every call fails, as on a server
  without the extension (Xwayland's refused its gamma ramp request, which
  ended the game).
- The potentiometers calibrate themselves as they go: the games' set-up
  of their controls (Tokyo Cop's at each menu and game) empties the
  ranges, which grow as the controls move, so the wheel is off-centre until
  it has been turned both ways. The set-up is hooked: the ranges are left
  as after a full turn. Tokyo Cop empties them a second time at its
  software's start (`IniPotes`, after `IniControllers`): hooked too (the
  car pulled to a side).

## Today's systems

- `errno`: the executables hold a copy of libc's `errno@GLIBC_2.0`, which
  glibc no longer exports (`gaelcoErrno.c` gives them one, versioned
  `GLIBC_2.0` by `gaelcoErrno.ver`: unversioned, it also took libm's
  thread-local `errno@GLIBC_PRIVATE`, for every game).
- Ring Riders and Tuning Race are built by Intel's C++ compiler:
  - it keeps the stack 4-byte aligned, and today's libraries use SSE on
    it: every import goes through a stub that aligns the stack
    (`alignExecutableImports`, `common/importHook.c`);
  - its CPU dispatch takes only Intel's CPUs of the time for SSE2 ones
    (`__intel_cpu_dispatch_fail` on others): the indicator is set to a
    Pentium 4's.
- Tuning Race lowers a thread's priority with `pthread_create`'s result for
  the thread (0): LinuxThreads answered ESRCH, today's glibc crashes.
- `findStaticFnAddr` reads the main program's file, not `/proc/self/exe`
  (the dynamic linker when a dump lost its executable mode).

## The GeForce's extensions (`common/nvGl.c`, `common/nvProgram.c`)

Mesa has none of NVIDIA's extensions the games use:

- NV_register_combiners (all lit or lightmapped surfaces): the combiners'
  state is kept, and an ARB fragment program doing the same is generated
  (and cached) at the draws while they are on.
- NV_vertex_program (Ring Riders' and Tuning Race's skinned and lit
  objects, Tuning Race's text): rewritten into ARB vertex programs at load;
  `c[]` is the program environment, `glTrackMatrixNV`'s matrices written
  there before each draw; texture coordinates the programs write `.xy` of
  start as (0, 0, 0, 1), as on NVIDIA's.
- NV_fragment_program (one FP1.0 in Tuning Race), NV_occlusion_query: ARB's.
- NV_vertex_array_range: plain memory.
- EXT_vertex_weighting (Tokyo Cop's characters, blended between two
  modelview matrices): the vertices and normals are blended on the CPU at
  each draw, into the first matrix's space.
- Display lists: Tuning Race's shaders compile their state (combiners,
  enables, program binds) into lists; our part of it is recorded and done
  again at each `glCallList`.

## Controls

| Cabinet | Keyboard | Gamepad |
| --- | --- | --- |
| Wheel / handlebar | Left, Right | left stick |
| Gas / throttle | Up | right trigger |
| Brake (Tokyo Cop: a switch) | Down | left trigger |
| Start (Tokyo Cop: the horn) | 1 | Start |
| View | V | Y |
| Siren (Tokyo Cop: the start button while racing, which START no longer is) | M | X |
| Gear up / down (Tuning Race) | A / Z | RB / LB |
| Coin | 5 | |
| Test / service | T / S | |

The arrows turn the wheel gradually (0.4 s to a lock, 0.15 s back to the
centre), as a hand would. Tuning Race's wheel speed (the module's steer
timer, read by its car physics) is always "still": the wheel's change
between the game's reads made the car spin round.

Ring Riders' test menu: START moves, twisting the throttle selects. Tuning
Race's: gear up/down move, START selects. Tokyo Cop's: the siren moves,
START selects.

## Batocera

The games live in the `gaelco` system (beside its MAME, Demul and Wine
games) as `<name> (linuxloader).squashfs` (`mksquashfs -comp zstd -b 131072
-all-root`): Tokyo Cop's salidas directory; Ring Riders' `home/`; Tuning
Race's `home/` and its aux files at the root (none of the disk's system
directories, swapfile or core dump). Each is set to linuxloader in
`batocera.conf` (`gaelco["<name> (linuxloader).squashfs"].emulator=linuxloader`).
The generator (kind `gaelco`, from the rom's name) drives them as the
driving games: left stick or wheel, R2 gas, L2 brake, b view, y siren,
R1/L1 gear up/down; Start, Select coin, R3 test, L3 service. The window is
the screen's size: the frame (640x480, 720x540) is scaled to it.

## Known issues

- No force feedback (the wheels' motors), lamps, motion seats.
- The keypad (record names, pin codes) and the volume buttons are not
  mapped.
- The cabinets' link (network races) is not emulated: Tuning Race tries
  its link at boot and plays alone.

## Debugging

- `GAELCO_IO_TRACE=1`: the board's registers as they change.
- `GAELCO_SCRIPT=seconds:switch0:switch1:key[:pot0:pot1:pot2:pot3],...`
  (hex, the registers as the game reads them): inputs from a time on (a
  coin chute 1 closed counts a coin).
- `GAELCO_SOUND_TRACE=1`, `GAELCO_SOUND_DUMP=file` (raw 48 kHz stereo).
- `GAELCO_DRIVE_TRACE=1`: Ring Riders' drive's parameters.
- `NV_GL_TRACE=1`: the programs made; `NV_GL_NO_COMBINERS=1`,
  `NV_GL_NO_VP=1`.
- Runs from under `/tmp` do not work (the Lindbergh `open` maps `/tmp`).
