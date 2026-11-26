# Police Trainer 2 (Teamplay, 2003)

A Teamplay game like Crossfire Maximum Paintball (`src/loader/teamplay/`):
the same MegaJamma board (`/dev/mjg`) for the switches and the guns, the
same iButton dongle. Unlike Crossfire it opens its own X11/GLX window
(no glut), so it runs on the loader's X11 bridge.

## Files

- `pt2s_g11`: the game (32-bit, libGL + libX11). The dump lost its
  executable mode: the launcher runs it through the host's
  `/lib/ld-linux.so.2` (`hostLinker.c`).
- `pt2snd` -> `pt2snd.f7`: the sound daemon (see below). In the dump
  `pt2snd` was a flattened symlink and `pt2snd.f7` not executable; fix both
  once: `chmod +x pt2snd.f7 && ln -sf pt2snd.f7 pt2snd`.
- `data/`: the cabinet's read-only `/data`; `audit/`: its `/var/pt2`
  (settings, hiscores, audits, log), written by the game.

## What the loader does

- Paths: `/home/rocky/pt2` (the game's directory on the cabinet, from its
  `sx` script) and `/data` are the game's directory, `/var/pt2` is
  `audit/`.
- iButton: the init at 0x808cfa4 does `iopl(3)` as root, else opens
  `/dev/p37c`, and exits when that fails; detoured to return 1.
- `/proc/cpuinfo`: the game's clock is the TSC divided by the "cpu MHz" it
  reads there (in ms), out of the file's first 512 bytes split in at most
  100 words. A recent CPU's has more ("Too many args. load failed"), so the
  loader answers with a Pentium 4's, its "cpu MHz" the host's TSC rate,
  measured at start-up.
- Sound: the game starts `./pt2snd /data/pt2/aud/` with `popen` and writes
  sound codes to its stdin; the daemon plays them on `/dev/dsp` (the
  loader's OSS emulation). The daemon is a process of its own, which the
  loader tells by its code's CRC (`soundDaemonCrc32`): it gets the
  cabinet's paths and `/dev/dsp`, none of the game's checks. Its
  `system("mixer vol 100")` / `"mixer pcm 100"` are not run (it exits if
  `mixer` is missing). It exits ("Parent gone") when the game does.

- Guns: the game opens the board for a 360x240 gun space and turns a
  gun's averaged samples into it as x * 0.5347 - 50, y - 20 (its 240-line
  constants, 0x8055288), plus its gun calibration from the settings; past
  x 340 or y 240 a shot is off the screen. The loader's samples are built
  for that (`xScale`... in `teamplayGames.c`).

## Running

`scripts/linux/runpt2.sh` (`GAMES_DIR`, `LIBS32` for the loader's 32-bit
glut/GLU, `T` seconds). Controls are the Lindbergh path's, as on Crossfire.
