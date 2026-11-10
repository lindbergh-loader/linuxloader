# Star Trek: Voyager (Teamplay / Monaco, 2002)

A Teamplay game like Crossfire Maximum Paintball (`src/loader/teamplay/`):
a glut game for a medium-resolution monitor (512x384), on the loader's glut
bridge, with the MegaJamma board for the switches and the guns and the
iButton dongle. The dump is version 0.1.0e4 (Apr 2 2004, `src-e4`).

## Files

- `trek`: the game; `stvserv`: its service mode; `stvsnd`: the sound
  daemon. All 32-bit, not stripped. The dump lost their executable mode: the
  loader runs them through the host's `/lib/ld-linux.so.2` (`hostLinker.c`,
  and the `./stvsnd` the game starts with `popen`).
- `ssv`: the cabinet's driver (run by its `sx` script from
  `/home/jacko/stv`): runs `./trek`, and `./stvserv` when the game exits with
  0x44 (the test switch), then the game again. The loader does that itself
  (below); `ssv` is not run.
- `trek.*`, `stvserv.*`, `ssv.*`: other builds left on the disk (e1, e3, f,
  d2, 010d); `trek.e4` is `trek`.
- `stv/`: the cabinet's read-only `/data/stv`. `stv.settings`,
  `stv.hiscores`, `stv.audits`, `stv.log`, `stvserv.log`: its `/var/stv`,
  written by the game. `pt2/` is Police Trainer 2's data, unused.

## What the loader does

- Paths: `/home/jacko/stv` and `/data` are the game's directory, `/var/stv`
  too. The dump has names in another case than the game's lists
  (`Hlk_advance_hit_050` is `hlk_advance_hit_050.bhs`): a missing file is
  looked up case-insensitively in its directory.
- iButton: `verify_ib` (0x8056530) does `iopl(3)` as root, else opens
  `/dev/p37c`, and exits when that fails; detoured to return 1 (when it
  fails the game only draws a message over itself).
- `/proc/cpuinfo`: the clock is the TSC divided by the "cpu MHz", read by two
  parsers: PT2's (512 bytes, at most 100 words) and one that bails out of
  500..3000 MHz (`read_cpu_MHz`, its jbe at 0x809d605 made a jmp). The
  loader answers with PT2's Pentium 4 at the host's measured TSC rate.
- Frame pacing: the game waits in a busy loop for the nvidia driver's
  vblank count, from `/proc/interrupts` (`get_frame_number`, 0x8094268), 3
  vblanks a frame (2 with the "TURBO" game speed); the game runs per frame.
  Detoured to a 60 Hz count that sleeps until the next vblank.
- `DISPLAY`: the game does `putenv("DISPLAY=:0.0")`; ignored, as `setenv`.
- Video: it never calls `glGenTextures` and binds its own names from 1,
  which took the loader's frame texture (only a 256x256 corner showed); its
  names stand for names of the GL's own (`shaderPatches.c`). Its
  `glut_reshape` is called once with 512x384: it sets its viewport there and
  gives the height to the gun board, which takes only 240 or 384 lines.
- Board: `/dev/mjf`, the older driver: packets of 0xa8 bytes (0xb8 on
  `/dev/mjg`), the same layout and switches. Its error ioctl (0x40044d07)
  reads 0, or the game reopens the device every frame.
- Guns: x * 0.6931 - 150, y - 25 (384 lines, 0x80980a7), plus the gun
  calibration from the settings (`get_calib_dx/dy`), which the loader takes
  out of its samples so that a shot lands under the pointer.
- Sound: `./stvsnd /data/stv/arom/` started with `popen`, fed sound codes;
  told by its CRC like PT2's (its code segment is shorter than the 16 KiB
  the loader hashes: the ID covers the start of its data page).
- Service mode: when the game exits with 0x44 (test), the loader's `exit`
  lets its atexit handlers run, then execs `./stvserv` in the same process;
  when `stvserv` exits (Exit, 0x43; 'x', 99), `./trek` again. The sound
  daemon of the program that went sees its pipe close and exits. The
  loader's own exits (quit key) quit.

## Running

`scripts/linux/runstv.sh` (`GAMES_DIR`, `LIBS32` for the loader's 32-bit
glut/GLU, `T` seconds); `runstv.sh ./stvserv` starts the service mode.
Controls are the Lindbergh path's, as on Crossfire: 5 coin, 1 / 2 start,
T test, mouse = P1 gun. Two credits start a game. In the service mode
Start 1 moves to an item, Start 2 selects it.
