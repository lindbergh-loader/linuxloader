# Rockin' Bowl-O-Rama (Cosmodog)

Native support targets Rockin' Bowl-O-Rama gold (`bowlgold`, its 2.2.1),
Cosmodog's trackball bowling game (2007, distributed by Namco): a 32-bit
Linux glut game on the cabinet's own Gentoo. Played on the desktop
(2026-11-12, working). Not played on Batocera yet.

## The dump

`games/Rockin Bowl-O-Rama/` is the cabinet's `/bowl` (TeknoParrot's dump):
`bowlgold` (and the older `bowl-2.x.y` builds), `data/` (art, sounds,
scripts; `data/game/` is the cabinet's `/data/game`, the settings and
bookkeeping), `runbowl` (the cabinet's watchdog, not used).

- Executable: `bowlgold`, file CRC 0x7db5f008, loader ID 0x93675fe0. It is
  unstripped: its sources' names are in it.
- `lib/libXmu.so.6`: a stand-in (the game links libXmu but calls none of
  it; no 32-bit libXmu on today's systems): `gcc -m32 -shared -fPIC
  -Wl,-soname,libXmu.so.6 -o lib/libXmu.so.6 empty.c`.
- Run from the game directory: `scripts/linux/runbowl.sh` (`GAMES_DIR`,
  `LIBS32` for libglut.so.3; put the host's `/usr/lib32` first for its ALSA
  plugins).

## The cabinet (`src/loader/cosmodog/`)

- The I/O controller, on the serial port (`/dev/ttyS0`, 115200 baud): its
  packets are 7-bit, CRC-checked and keyed (a key made of the MAC address
  and the disk's serial). The loader stands in at the packets
  (`IOPACKET_SendMessage`, `IOPACKET_GetMessage`, `cosmodogIo.c`): the
  version, ready, the serial number, the poll (switches, coin counters 1-3,
  the trackball's counts) and the licences (`IO_FindAuth`: every one
  asked for is found, its answer the SHA-1 the game checks, through the
  game's own functions: the in-house and on-line tournaments, the sixties
  lane).
- The image's files (`FILESIG_DataValid` and `data/signature.dat`: 2044
  files, Gentoo's, the wireless card's driver, ImageMagick among them):
  valid.
- The game keeps its Gentoo itself, as root: it remounts its disks, makes a
  RAM disk, its card reader's node (`/dev/hiddev0`), writes the network's,
  NTP's and GRUB's settings and sets the clock. Its commands (`system`,
  `popen`, answered with what the cabinet's would say), mounts, device
  nodes and clock do nothing; the system files it writes are the game
  directory's `env/` (`/etc/conf.d`, `/etc/hosts`, `/etc/ntp.conf`,
  `/boot`...).
- Sound: ALSA's `plughw:0,0` is the default device.

## Controls

| Cabinet | Desktop | Gamepad |
| --- | --- | --- |
| Trackball | the mouse (captured) | left stick |
| Start | `1` | Start |
| Replay | Space | A |
| Scoresheet | `z` | X |
| Jukebox | `x` | Y |
| Coins 1, 2 | `5`, `6` | Back |
| Menu (operator) | F2 | R3 |
| Service credit | F1 | L3 |
| Volume up / down | Page Up / Down | |

With evdev, the mice's motion is the trackball, the JVS buttons as the
desktop's (START, BUTTON_1 replay, 2 scoresheet, 3 jukebox).

## Batocera

The rom is the cabinet's `/bowl` or a `.squashfs` of it (`arcadepc` or
`rawthrills`, the linuxloader emulator chosen for it). The generator (kind `bowl`,
from the rom's name) leaves the input to the linuxloader's own (the mice
and the SDL pad, its stick the trackball: evdev mode has no stick
trackball) and asks for the game's 640x480 full screen: at the screen's
size the game copies 640x480 of its back buffer over the frame.

## Known issues

- The magnetic card reader (MAGTEK, `/dev/hiddev0`), the network
  (tournaments, the server) and the lamps and ticket dispenser: none.

## Debugging

- `COSMODOG_SHOT=<dir>`: a frame every `COSMODOG_SHOT_PERIOD` seconds.
- `COSMODOG_SWITCHES=seconds:mask[,...]` (the poll's switch bits:
  start 0x8000, replay 0x1000, scoresheet 0x2000, jukebox 0x4000, menu
  0x0080), `COSMODOG_COINS=seconds[,...]`,
  `COSMODOG_BALL=seconds:dx:dy[,...]` (counts a poll).
