# Power Truck S (IGS)

Native support targets Power Truck S ("Power Truck Special", `pt`, IGS,
built 2013), [Power Truck](power-truck.md)'s newer version: the same cabinet
(wheel, pedals, horn, view, four-position gear lever), A36 board and
controls, with a card screen (no card: cardless mode), a Story mode and new
trucks. Desktop (headless Gamescope/Mesa, 2026-11-13): warning, attract,
coins, start, the card screen's no-card route, truck and transmission
selection, Story mode, stage selection and a race on Downtown with force
feedback. Played on the desktop by the user (2026-11-13): working 100%.

## The dump and its game

`games/Power Truck S/` is a TeknoRaven package laid out as Power Truck's;
`game/roms/pts_eprom.bin` is the board's firmware, `game/readwrite/` a copy
of `game/`.

TeknoRaven's `pt` (file CRC 0xab3da4e1) is not IGS's file but a rebuild
for its Windows loader ("PATCHED_WITH_HASPE_LINUX_LOADER_1.1"): no section
headers, every segment RWE, its PLT's imports stripped (1141 of 1430 names
gone from the dynamic symbols, 102 relocations left), the GOT filled with
0xf000f000, the stubs pushing 0xdeadbeef, and the imports whose address the
game takes pointing into a table TeknoRaven appended. The dynamic linker
binds the libraries' own references to those values (libGLX's `strcmp`
jumped to 0xf000f000). The game's code and data are IGS's, untouched (bss
zero, no call into the appended data).

`tools/igs-pts-repair.py <pt> <out>` makes it a standard ELF again: a new
segment with the dynamic symbols (theirs and the missing imports), strings,
versions (TeknoRaven's version needs, which keep IGS's), SysV hash and a
`.rel.plt` for every stub; the stubs push their relocation again, the GOT
holds their lazy-binding entries, `__gmon_start__`'s slot 0, the segments
their permissions as linked. The import names
(`tools/igs-pts-imports.txt`, in slot order, with their versions, weak and
address-taken flags) were found by matching Power Truck's `pt` (the same
code base, with its symbols): the PLT calls of the functions both have
(1170), the PLT's order between them (237), and for the 23 new ones their
callers (`InstancedGeometry`'s instancing, Theora's auto restart, the
`istreambuf_iterator` string helpers, `__printf_chk`) and the stubs the
vtables point at. The repaired `pt` (file CRC 0xb450a566, loader ID
0x197a141e) replaces the dump's, TeknoRaven's kept as `pt.teknoraven`.

Run it with `GAME_DIR=".../Power Truck S/game" scripts/linux/runpt.sh`.

## The board and the scripts

Power Truck's A36 port with this firmware's own layout: another build
(optimised), Speed Driver 4's internal-ROM table (`0x0200f300`), idle loop
`0x1000122c..0x10001234` (it polls `0x02006b7c`). The scripts' mask,
`d69ca46f7038c55fd62b3b941321b63c`, found by frequency analysis as Power
Truck's. The lamps' script has Power Truck's nil at exactly `timer % 2 ==
1` (`luabindByteCastFailed`). Its `resources.cfg` no longer shares the
trucks' directories between groups: Power Truck's archive fix is not needed.
