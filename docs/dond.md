# Deal or No Deal (Raw Thrills / PlayMechanix, g3) — notes

Three builds, run with `scripts/linux/rundond.sh` (`DOND=us|uk|dlx`):

| Build | Version | Loader id | State |
|---|---|---|---|
| US ("Deal or Not Deal" dump) | 01.07.06, not stripped | `DEAL_OR_NO_DEAL_RT` 0x7d589b9a | working |
| UK | 01.06.06 UK, stripped | `DEAL_OR_NO_DEAL_UK_RT` 0x13c57d44 | working |
| Deluxe | 01.18.00.NJS, stripped, C++ | `DEAL_OR_NO_DEAL_DELUXE_RT` 0x0bb3d477 | working (started, cases picked; a whole game not played through) |

The UK build's functions were found by matching the US build's code (addresses
wildcarded); Deluxe's from their call sites (the mode table, the board's
switch requests 0x312/0x112). The Deluxe dump's `DondDlx_118_HD.7z` and
`_4K.7z` hold the same binary with a patched mode table entry (same loader id).

## Cabinet hardware (rtDond.c, rtGames.c)

- **JAMMA board**: the other g3 games' switches (start 5, coins 7/8, service
  10, test 11, volume 12/13), emulated by rtJamma.c. The poll is `InpLoop`.
- **Button panel on the parallel port** (0x378): 16 case buttons, DEAL, NO
  DEAL and the panel's start ("DOUBLE DEAL" on the title screen), switches
  0..18, and their lamps. The game does its own `in`/`out` (a dozen
  functions, threads too): they fault, and the loader's SIGSEGV handler
  (mainL.c) hands those on 0x378..0x37a to `rtDondPortIo`. The game's own
  SIGSEGV handler and `ioperm` are overridden.
- **Dongle**: US/UK, a Rockey (libusb) checked by a thread started from
  `BankerOfferInitialize` (without it the game loops forever clearing random
  stack bytes): stubbed. Deluxe, a HASP HL checked by `DongleCheck`, which
  also reads the bonus wheel type (1..5, 0: the game only offers its operator
  setup): answered with 4 (NJS, this build's), and the setup's write
  (`DongleWriteWheel`) kept in memory.

## Controls

- A pointer (mouse, gun: P1 ANALOGUE_1/2) moves a crosshair the loader draws
  (rtDondFrameDraw); BUTTON_1 (left button, trigger, Enter, Space) presses
  the case nearest to it, from each build's case-pick screen layout (US: the
  models' numbers, UK: the board of boxes; Deluxe uses the US table,
  checked).
- A pad's left stick moves the same crosshair at a speed, alongside the
  pointer: with evdev input from ANALOGUE_5/6, in the game window from the
  SDL gamepad (A presses, X DEAL, B NO DEAL, Y DOUBLE DEAL, Start START).
- BUTTON_2 / D: DEAL, BUTTON_3 / N: NO DEAL, BUTTON_4 / B: the panel's start
  (DOUBLE DEAL).
- 1 START, 5/6 coins, F1/P service, F2 test, arrows volume (as the other g3
  games).

## Deluxe's start

Deluxe starts a game from the JAMMA board's second start switch (START 2,
board switch 6, event slot 8): its title screen (mode 4, 0x86e7c00)
registers 0x810e210 on slot 8 only, which, with credits or free play, sets
the start flag (0x85b74f4). Nothing handles slot 7 (START 1) outside the
operator menus, and its back-to-attract (0x8118b00) registers only the coin
handlers (slots 25, 26, 30, 31); the US-like start handler (0x8117990) is
dead code. So Deluxe's P1 START is mapped to board switch 6
(dondDlxJammaSwitches). The panel's start (switch 18) is read only during a
game. The bonus wheel is a USB serial board on /dev/ttyACM0 (libdondw: it
only reports the end of a spin, 0x99); without it the game runs on.
The case-pick stage is the US one (its table checked on a Deluxe frame).

The Batocera generator gives these games their own kind, `dond` (by name):
one player; a gun or a mouse aims as in the gun games, and the first pad
joins it (left stick on ANALOGUE_5/6; b presses, y DEAL, a NO DEAL, x DOUBLE
DEAL, its buttons added to the gun's or mouse's), plus start, coin, test and
service.

## Batocera

Deployed 2026-10-02 as `/userdata/roms/rawthrills/Deal or No Deal.pc`,
`Deal or No Deal UK.pc` and `Deal or No Deal Deluxe.pc` (the dumps as they
are). Batocera has no `libtheora.so.0`: each game's `lib/` holds a copy of
its `ogglibs/libtheora.so.0.1.0` under that name. The US build runs there
(with a Sinden gun: 1920x1080, gun border on). One local run with those
display settings exited silently at START; not reproduced with either
setting alone, nor seen on the machine since.
