# Pirate Revenge (IGS)

Native support targets Pirate Revenge M-101CN (`sship`, built August 4,
2008), a ticket redemption ship shooter by IGS and Gold Captain's
predecessor: the same engine, the same IGS036 behind a PLX PCI9030 card,
the same stage numbers (see [gold-captain.md](gold-captain.md)). Checked
headless on the desktop (2026-11-11): attract, coins, start, the mode and
area select, the training and chapter 1 fights, the continue screen, the
stage result, a boss, game over and new games. Not played by hand yet, not
on Batocera.

## The disk

`Pirate Revenge (M-101CN).img` is the whole hard disk (IGS-Linux of 2008):

| Partition | Content |
| --- | --- |
| hda1, ext2 | the kernel (`PirateRevenge-OS-57P`, LILO) |
| hda2 | the root, IGS "rofs" (as Gold Captain's) |
| hda3 | the game's data, IGS "SHFS": squashfs 2.1, its superblock's words reordered (its major and minor 32-bit), 0x43 bytes, its directories unsorted |
| hda4, ext3 | `/fec_data` (the dumped cabinet's `gameset.bin`) |

The kernel mounts hda3 on `/pr/linux`; `/etc/X11/IGS` starts X and runs
`./sship` there in a loop. The ELF is on hda3 as it is (dynamic, with its
symbols). `/pr/exec/sship` on the root is another program: IGS's launcher,
which wipes the disk when tampered with. `Reset27` resets the PC card.

`tools/igs-extract.py "Pirate Revenge (M-101CN).img" "games/Pirate Revenge"`
reads hda3 itself (today's unsquashfs refuses its unsorted directories),
renames the ELF's `/lib/libcasmcard.so` to `libcasmcard.so` and puts
`libcasmcard.so` and `libpcsclite.so.0` in the game's `lib/`. hda4 is not
copied: the settings are the IGS036's (`var/igs036.bin`).

- Executable: `sship`, file CRC 0x16f1257a, loader ID 0x322ab98e.
- Run from the game directory (the cabinet's `/pr/linux`):
  `scripts/linux/runpr.sh` (`GAMES_DIR`, `LIBS32` as for the other games).

No IGS036 data is on the disk (nor Gold Captain's): the weapons' data
package (command 0xf) is in its ROM here too, and is made up the same way.

## Differences from Gold Captain

- The switch word (IGS036 word 5): bit 0 right button, 1 left button, 2
  start (the helm's centre button: starts, confirms, the special attack), 5
  test, 6 service (3, 4 the coin switches, 7 the ticket sensor: not sent).
- The helm: a 10-bit potentiometer (word 6: 0..1023, read as 100 -
  value / 5.12, -100..100 %; Gold Captain's 0..511), turning the other way
  (0 is the right). VR Setting in the test
  menu asks for one side (above 25 %), the other (below -25 %), then the
  centre, each confirmed with the start button. The game's defaults leave
  it at 0 (no steering): with no settings kept, the loader fills in the
  full range (100, -99, 0, as VR Setting measures it).
- The dodge: the game takes a fast turn of the helm for one (its position
  in 13 steps; 2 steps in a frame, 3 in 3, 4 in 4 or 5 in 6, table
  0x817eb08). The arrows, the d-pad and a stick get there in a frame, so
  every turn dodged: the loader turns the helm no faster than lock to lock
  in 25 of the game's frames (`helmSpeed`, counted at its buffer swaps: the
  bosses run below 60 frames a second), and the dodge buttons turn it all
  the way at once.
- `/dev/mixer`: the game reads its device mask and exits without one
  (`SystemExit:read SOUND_MIXER_DEVMASK`); the loader's mixer answers it.
- The PLX card is found through the game's `SelectDevice` (the SDK's
  `PlxPciDeviceFind` then checks the ids).
- Each game starts with stage event 2 (to stage 1, the start; Gold Captain
  starts with 3).
- Each fight frees what it loaded: none of Gold Captain's fight fixes. A
  fight whose time runs out and is continued plays again.
- The stage's segments are the IGS036's here too (command 5): the same
  stand-in, from the SOD objects (`LoadedObjects`).
- It has a race (stage 6) and a staff fight (stage 9).
- The game reads the desktop keyboard itself, besides the cabinet's
  switches (space the start button, `n` and `m` the left and right ones,
  `a` and `d` the helm, and debug keys: `5`, `t`, digits and letters in the
  area select pick debug options and stages): its key reads (`KeyDown`,
  `KeyPress`, `KeyUp`) answer no key, the controls are the loader's.

## Controls

| Cabinet | Desktop | Gamepad |
| --- | --- | --- |
| Helm | arrows (full turn) | left stick (d-pad: full turn) |
| Left button | mouse left, Space | A |
| Right button | mouse right, `z` | B |
| Start (special attack) | mouse middle, `x`, `1` | X, Start |
| Dodge left / right | `c` / `v` | LB / RB |
| Coin | `5` | Back |
| Test | `F2` | R3 |
| Service | `F1` | L3 |

## Known issues

- Not played by hand: the helm, the weapons' made-up data and the race and
  staff fights are unverified.
