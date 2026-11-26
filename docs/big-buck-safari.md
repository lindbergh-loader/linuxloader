# Big Buck Safari (Raw Thrills)

Native support targets the `game` executable of the 2010 restore disc
(`Big_Buck_Safari_1.02.23_deluxe.iso`), EXE v01.02.23.US.DLX, built July 29,
2010, file CRC32 `0x1cca4025`. Native startup and play were confirmed on the
desktop on 2026-11-20. Physical guns on Batocera still need validation.

## The dump

The ISO is a SLAX disc that restores the cabinet's disk with DFSee from
`boot/rawsrc/part{0,1,2}.imz` (DFSee's compressed images) and `lvm.pd1` (the
partition table). DFSee itself (`boot/rawsrc/dfsee`, Linux i386, UPX) does the
job outside the cabinet:

- unpacked (`upx -d`), with its `open()` wrapper (file offset `0xd5200`) given
  `O_LARGEFILE`: `part1.imz` is over 2 GiB;
- run in a private mount namespace whose `/dev` holds only `hda`, a loop
  device on a sparse 20 GB file, so it can see no other disk:
  `dfsee -P- -Q run -B -p:1 <script>` with `fdisk prestore 1 .../lvm`, then
  `disk 1` and `wrim .../part1` per image (each image lands at sector 0).

Partitions 1 and 2 (`/` and `/g3`) are ReiserFS 3.6, which current kernels no
longer have: the disc's own 2.6.27 kernel (`boot/vmlinuz`) in QEMU, with an
initramfs of the disc's busybox and SLAX 6's GNU tar, mounts them and writes a
tar to a third disk.

The game directory is `/g3`'s content (`game`, `bbsdata/`, `bbh-patch`,
`irgun/`, `version.txt`) with an empty `bbsuser/` (the game's `/bbsuser`) and
`lib/` (`libstdc++.so.5`, `libtheora.so.0`, `libogg.so.0` from the cabinet's
root).

## Data

Safari's data files are encrypted with keys from its HASP dongle, whose
answers are not recorded. Big Buck World bundles Safari: its `bbwdata/bbs2`
is Safari's data, and its `hasp/` holds its recorded answers.
`tools/rt-bbs-data.py` rebuilds Safari's data from them:

```sh
tools/rt-bbs-data.py <Big Buck World's game dir> <Big Buck Safari's game dir> <dir for Safari's own data>
```

Both games derive a file's key from the two secrets the dongle decrypts at
startup (512 and 256 bytes) and the file's name: `.g3` and `.tga` keys are
`kdf(s512, name)`, `.cf` keys are the dongle's encryption of it (World's
32-byte answers), the IV is `kdf(s256, name)`. A file is a zlib stream,
Blowfish-CBC in World (2011) and AES-256-CBC in Safari. The tool answers
Safari's two startup questions with World's secrets, so Safari derives
World's keys, and re-encrypts World's files with AES. Sounds are plain files.

World's `bbs2` is its edition of the game, Big Buck Safari Outback (more
animals, Australia and India sites), with World's screens in it. The tool
puts Safari's attract and credits movies back in `title.cf` and `at.cf`
(World's play `atmovieWorld` and `credmovieWorld`), and rebuilds the "please
stand by" screen shown after the gun calibration (`standby.g3`, World's logo)
with the Safari banner of World's game select screen; it needs Pillow.

World's versions of the `.cf` files are newer than the checksum and SHA-1
table in Safari's game: its per-file check (`0x80df580`) and its check of all
of them at startup (`0x80ac520`, the "data files corrupted" warning) are
stubbed.

## Launch

```sh
GAME_DIR='/path/to/Big Buck Safari' \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/runbbs.sh
```

`LIBS32` provides a 32-bit `libglut.so.3`. The game passes GLUT
`-display :0.0`: the loader drops it, so a test run's `$DISPLAY` is used.

Controls are Big Buck Hunter Pro's (the same g3 JAMMA board and guns, see
[big-buck-hunter-pro.md](big-buck-hunter-pro.md)).

## Deluxe cabinet: IR guns, calibration, lease

The dump is the Deluxe (IR camera gun) cabinet. Without the IR camera:

- Its IR gun calibration takes no shot (the IR one and the two targets one
  both read the camera's points), and the start runs it while the cabinet
  is a fresh install. The loader makes that start's test (`0x809b306`) a
  jump past it. The guns are calibrated from Big Buck Hunter Pro's
  calibration instead: copy its `bbh3user/aud/GunCal.000.aud` to
  `bbsuser/aud/GunCal.000.aud` (`jammaGunCalFile`; the game itself sends
  the board an empty one).
- `bbsuser/aud/IRGunCal.{000,001,002}.aud`, all -1 in a fresh install,
  keeps "WARNING: GUNS NOT CALIBRATED!" on screen. Written with the game's
  own defaults (per gun 445, -35, gains 1.0; the PMAD store's checksum is
  the CRC-32 of its 0x20 byte record), the guns count as calibrated. The
  game still aims through the board positions.
- The camera's gun connected status (`0x837010f`) is answered 1: without
  both guns the Deluxe cabinet starts no game (Start only cycles the
  attract mode).

Online: the lease (`0x818eee0`, the operator's fee record) is answered not
in force, else the unregistered cabinet locks itself after the start
("PLEASE STAND BY / Game must be registered / machine is undergoing system
maintenance"). The start's registration request (`0x80997bf`) is jumped
past: left unregistered, the game stays offline (a registered one connects
to Raw Thrills' servers at each start, "Connecting..." until a 30 s
timeout).

The picture is 4:3: black bars at the sides of a wide screen, and the light
gun border around the picture (as Big Buck Hunter Pro).
