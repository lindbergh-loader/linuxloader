# Big Buck Hunter Pro (Raw Thrills)

Native support targets the v1.25 bundle's `game` executable, EXE v01.01.04,
built March 5, 2008. Native startup, gameplay, all desktop controls, scaling
and fullscreen were confirmed on 2026-10-14. Physical guns on Batocera still
need validation.

## Launch

Keep the untouched `game`, `bbh3data/`, `bbh3user/` and `env/` in the game
directory. No `/pm` directory, Wine, TeknoParrot or dongle recording is needed.
The native game links 32-bit GL, GLU and GLUT; `LIBS32` can provide libraries
the host does not carry.

```sh
GAME_DIR='/path/to/Big Buck Hunter Pro' \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/runbbhpro.sh
```

For fullscreen, put this in the game directory's `linuxloader.ini`:

```ini
[Display]
WIDTH = 640
HEIGHT = 480
FULLSCREEN = 1
KEEP_ASPECT_RATIO = 1

[Input]
INPUT_MODE = 1
```

GLUT game mode is replaced with a regular window at the configured size and
fullscreen follows `[Display] FULLSCREEN`. The shared frame hooks scale the
game to the window, preserve its 4:3 aspect ratio and draw Sinden borders
when enabled. On Batocera, the generator supplies the display size, gun
border and evdev mappings.

## Rockey boot envelope

The original ELF has no section headers. Its entry at `0x0b15d8b0` opens
`/dev/rockey`, exchanges ioctl requests and decrypts its code using a fixed
32-bit XOR mask. It then forks a dongle watchdog and jumps to the real C
runtime startup.

The launcher identifies the original by full CRC32 `0x0a5b0341`. In a cached
copy it decrypts `0x937c0` bytes at `0x0804ad80` with mask `0xf167eb84`,
validates the restored i386 startup sequence, points ELF entry directly at
`0x0804ad80`, and clears the Rockey marker from reserved ELF identification
padding, which current glibc rejects. The resulting partial CRC is
`0x5251b97d`. The original executable remains unchanged.

Starting at the restored entry avoids both the boot checks and watchdog.
This build does not require HASP answers or Rockey reply files: native
gameplay was confirmed without them.

## Cabinet input and paths

The game uses the same g3 JAMMA switch and gun events as Big Buck World.
The profile hooks `JammaPoll` and `JammaOp`, supplies switch counters and
posts both guns' positions and shot events through `PostInputEvent`.
`JammaLoop` is stubbed so the absent serial board does not cause repeated
reset attempts. Gun calibration requests use the shared JAMMA emulation.

- Desktop: mouse aims P1; left fires, right pumps, middle shoots offscreen.
  1/2 start, 5/6 coin, F1 service, F2 test and up/down navigate the menus.
  Esc or Alt+F4 exits.
- Evdev: `ANALOGUE_1/2` aim P1 and `ANALOGUE_3/4` aim P2.
  Each player's `BUTTON_1` fires, `BUTTON_2` pumps and `BUTTON_3` shoots
  offscreen. Cabinet switches use the standard Raw Thrills mappings.

Absolute `/bbh3user` paths are redirected to `bbh3user/` in the game
directory. The game's data paths are already relative to that directory.
