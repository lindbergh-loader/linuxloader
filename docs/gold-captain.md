# Gold Captain (IGS)

Native support targets GoldCaptian-E2000-v1-e65 (`GoldCaptain`, built March
12, 2009), a ticket redemption ship shooter by IGS (International Games
System, Taiwan) on its own Linux. Checked on the desktop (2026-11-11):
loading, attract, test menu and its I/O test, coins, start, the captain
select with the helm, boat runs and bosses (sea, desert, ice), the result,
tickets, game over and new games; the first chapters played by hand. Not on
Batocera.

## The disk

`GoldCaptain.img` is the whole hard disk (IGS-Linux, kernel `2.4.31-IGS_V1.0`):

| Partition | Content |
| --- | --- |
| hda1, ext2 | the kernel (`GoldCaptain-OS-e65`, LILO) |
| hda2 | the root, IGS "rofs": cramfs with its inode words reordered (`[mtime][namelen \| offset << 6][size][mode \| uid << 16][name]`) |
| hda3 | the game's data, IGS "SHFS": squashfs 2.0 behind a longer superblock (the IGS signature, a game type and version, obfuscated major/minor) |
| hda4, ext3 | the game's `var/` |

The kernel mounts hda3 on `/GoldCaptain` and hda4 on `/GoldCaptain/var`,
then runs `/bin/csh /lib/libdb0.a __KERNEL__ -no-print -GC` (`csh` is no
shell): `libdb0.a` is a small ELF whose 1 KiB head sits in hda1's free
blocks, and it rebuilds the game in a ramfs on `/Game`. The game's ELF is a 1
KiB head kept past the root filesystem's end on hda2, then the start of
`/lib/libdb4.a`, `7`, `6`, `3`, `2`, `5`, `1`, `8`, each 17 KiB shorter than
the one before (the first 20 KiB, the last 996240 bytes): 1506192 bytes.

`tools/igs-extract.py GoldCaptain.img "games/Gold Captain"` does all that
(it needs `unsquashfs` and squashfs-tools 4.x, which reads squashfs 2.0). It
also renames the ELF's `/lib/libcasmcard.so` to `libcasmcard.so` (the
absolute path would be the host's `/lib`), so the ELF the loader knows is
that one, and puts `libcasmcard.so` and `libpcsclite.so.0` (the card
reader's, from the root's `/usr/sbin/cardread/lib`) in the game's `lib/`.

- Executable: `GoldCaptain`, file CRC 0xd05b6728, loader ID 0x6d6ff885.
- Run from the game directory (the cabinet's `/GoldCaptain`):
  `scripts/linux/rungc.sh` (`GAMES_DIR`, `LIBS32` as for the other games).
- Loading takes about a minute (on the cabinet too: LZSS ROMs and textures).

## The hardware (`src/loader/igs/`)

- `/dev/roio`: the BIOS read through an IGS driver, matched against a table
  of the cabinets' motherboards. The check answers board 5 (i852-W83627HF,
  the E2000's).
- `/dev/pccard0`: opened only (else the game exits); `/dev/null`.
- Sound: SDL 1.2's OSS driver on the loader's `/dev/dsp` (the host has none:
  stat answers for it as a character device). `/dev/mixer` is the loader's
  too (its volumes kept and read back): the game only sets its volumes, but
  turns all sound off when it cannot open it.
- A PLX PCI9030 card (the PLX SDK, `/dev/plx/Pci9030...`): its find, open
  and BAR map are replaced, the BARs plain memory.
- The IGS036, the board's protected CPU, behind the card's shared RAM: the
  blocks are XORed with keys of their own, the rounds paced by a sequence
  number and a doorbell. The exchange is replaced (`igsIo.c`): each round
  fills the IGS036's block as the game reads it, decrypted, and lets the game
  handle it. The IGS036 does more than I/O, and the loader stands in for it:
  - the switches (left and right buttons, test, service), the helm (a
    potentiometer, 0..512), coins;
  - the operator's settings and bookkeeping (commands 0xd and 0xe), kept in
    `var/igs036.bin`;
  - the game's action groups (its animations' sets: allocated, freed, three
    values each, commands 6 to 10);
  - the stage flow (command 0x13: each stage ends with an event, the IGS036
    names the next stage). The table is rebuilt from what the stages do (see
    `stageAfter` in `igsIo.c`). A chapter is a boat run (stage 5), which
    keeps all it loaded, then the next fight's choice, which picks the boss
    (stage 7, on what 5 loaded, with the time 5 left), which frees it all
    and shows the result itself. A result between them plays over what is
    kept (the boat stays on it; the pool the game allocates from is wound
    back under the boss's sprites: crashes);
  - the stage's segments (command 5): the cabinet's are in the IGS036 (the
    SOD file's copy is blank). One segment from the start to past the
    farthest object stands in for them, the boat's speed set to get there in
    about 50 of the fight's 60 s;
  - the weapons' data (command 0xf: 168 types of 8 words: power, speed,
    range, lift, hit points, homing), in the IGS036's ROM, not on the disk:
    made up (`weaponsInit`), overridable by `var/igs036-weapons.txt`;
  - the ticket dispenser (0x15 pays, 0x14 owed, 0x16 stops; tickets counted
    as they come out, 5 a second).
- The ROM version shown in the test menu: I:S100 / O:S100.

## Software fixes

- A boat run whose time runs out leaves its loop without naming the next
  stage, and the game plays it again over what is still loaded: the time up
  ends the segments, as the game's debug key does (`timeUp`).
- The game's loader, on an action file whose id is still taken, closes the
  file and reads on (its error function is empty in this build). Any fight
  setup first frees a boat run still loaded, with the run's own teardown.

- The game's SDL 1.2 is static and loads libGL itself: GL is loaded for the
  loader's wrappers when its context is made current (`dlsym` of
  `glXMakeCurrent` hooked).
- That SDL's own Xlib extensions (VidMode, DGA, Xinerama) were built without
  Xlib's thread locks; today's libX11 always locks, and the first X call after
  them waits forever. The game's display goes unlocked.
- The game asks for full screen at 640x480 (SDL's VidMode switch of the X
  server, which today's drivers refuse): a 640x480 window, or with
  `[Display] FULLSCREEN` or a larger configured size full screen at the
  screen's size (`WIDTH` x `HEIGHT` when larger, else the X screen's), its
  640x480 frame scaled to it (letterboxed with `KEEP_ASPECT_RATIO`).
- Paletted textures (`GL_COLOR_INDEX8_EXT` with a shared palette), which
  drivers lack, are expanded to RGBA (`igsPalette.c`). Not seen used yet.

## Controls

| Cabinet | Desktop | Gamepad |
| --- | --- | --- |
| Helm | arrows (full turn) | left stick (d-pad: full turn) |
| Left button | mouse left, Space | A |
| Right button (start, confirm) | mouse right, `z`, `1` | B, Start |
| Coin | `5` | Back |
| Test | `F2` | R3 |
| Service | `F1` | L3 |

In the test menu, the left button selects and the right one confirms; both
together leave the I/O test. The helm is calibrated in Hardware Test → VR
Setting.

## Known issues

- The stage flow, the segments and the weapons' data are reconstructions:
  the IGS036's own are unknown. Fight types 1 and 3 (stages 6 and 9), the
  endings and the higher chapters were not seen in normal play.
- The game also reads a few keys of the desktop keyboard itself, its debug
  menus': `s`, `w`, `e`, `q` and `=` switch the cabinet's buttons to the
  keyboard's `n`/`m` until the game restarts.

## Debugging

- `IGS_TRACE=1`: the IGS036's commands and the switches as they change.
- `IGS_SHOT=<dir>`: a frame every 4 s (`IGS_SHOT_PERIOD`, fractions too)
  from `IGS_SHOT_FROM` seconds on, as a PPM, read back before the swap
  (gamescope's own capture is black, Xvfb's GLX refuses the game).
- `IGS_STAGE<event>=<stage>`: the stage played after that event (command
  0x13), e.g. `IGS_STAGE4=9` for a type 3 fight.
- gdb: no software breakpoints on hooked functions (the fight setups, the
  weapons' release): restoring the breakpoint's byte breaks the hook.
- `IGS_IN<n>=value` or `IGS_IN<n>=seconds:value,...`: the IGS036's word n
  forced (from that many seconds after the first round), to see what the
  game does with it; word 5 bit 1/2/3/4 is left/right/test/service.
