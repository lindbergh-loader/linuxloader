# Deal or No Deal (Raw Thrills / PlayMechanix, g3) — notes

Three builds, run with `scripts/linux/rundond.sh` (`DOND=us|uk|dlx`):

| Build | Version | Loader id | State |
|---|---|---|---|
| US ("Deal or Not Deal" dump) | 01.07.06, not stripped | `DEAL_OR_NO_DEAL_RT` 0x7d589b9a | working |
| UK | 01.06.06 UK, stripped | `DEAL_OR_NO_DEAL_UK_RT` 0x13c57d44 | working |
| Deluxe | 01.18.00.NJS, stripped, C++ | `DEAL_OR_NO_DEAL_DELUXE_RT` 0x0bb3d477 | boots, attract, operator menus; **no way to start a game yet** |

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
  models' numbers, UK: the board of boxes; Deluxe uses the US table, not
  checked).
- BUTTON_2 / D: DEAL, BUTTON_3 / N: NO DEAL, BUTTON_4 / B: the panel's start
  (DOUBLE DEAL).
- 1 START, 5/6 coins, F1/P service, F2 test, arrows volume (as the other g3
  games).

## Open: Deluxe does not start a game

In attract (free play), START (board switch 5) reaches the game: its poll
posts the start event to slot 7, but in attract slot 7's callback
(0x80dcfb0, re-registered every frame) is only the operator menus' "select"
(it acts on the current menu, 0x86cbaec). The US build's start handler has a
Deluxe twin (0x8117990) that nothing references. The panel's start (switch
18, B) arrives too (its switch-hit at 0x8112acb, queued to 0x85c2f18) but
does not start either. Slot 8 (START 2) gets a callback (0x810e210) from
0x810405a once; key 2 was not seen on the board in the test (to recheck).

Next: find what starts a game in Deluxe's attract (the consumer of the panel
queue at 0x85c2f18, the slot 8 callback 0x810e210), with gdb on the game
(`ptrace_scope` 1: launch it under gdb; `handle SIGSEGV nostop noprint pass`
for the panel's port faults; no breakpoint on hooked functions' first bytes).
Then the Deluxe case-pick screen positions.

The Batocera generator classifies these games as `gun` (by name): guns and
mice pick cases and fire DEAL with their second button; pads only get start,
coin, test and service.

## Batocera

Deployed 2026-10-02 as `/userdata/roms/rawthrills/Deal or No Deal.pc`,
`Deal or No Deal UK.pc` and `Deal or No Deal Deluxe.pc` (the dumps as they
are). Batocera has no `libtheora.so.0`: each game's `lib/` holds a copy of
its `ogglibs/libtheora.so.0.1.0` under that name. The US build runs there
(with a Sinden gun: 1920x1080, gun border on). One local run with those
display settings exited silently at START; not reproduced with either
setting alone, nor seen on the machine since.
