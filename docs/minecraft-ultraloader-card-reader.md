# Minecraft Dungeons Arcade: how Ultra Loader handles the card reader

Source: `ultraloader/` (Ultra Loader v1.7.5, 2026-11-22). Analysed 2026-11-24.
Extraction tool: `tools/ufw-extract.py`; extracted files in `ultraloader-re/`.

## The .ufw container

`minecraft-dungeons-arcade.ufw` is not encrypted. Layout:

- `"ULTRAFW\0"`, u16 version=1, u16 count=5, then 0x60 bytes (signature/hash, not checked here)
- from 0x70: 5 entries of 0x30 bytes: `u16 type, u16 ver, u32 offset, u32 csize, u32 rsize, 32-byte hash`
- each payload is zlib (`78 da`) unless csize == rsize

| # | type | contents |
|---|------|----------|
| 0 | 0x01 | manifest text (identity, notes) |
| 1 | 0x02 | `ultra_rawthrills_g7.dll`, the Windows board module |
| 2 | 0x04 | default settings |
| 3 | 0x0a | `UFWC` signature block ("release-2026-09") |
| 4 | 0x0d | zip: the Linux rootfs + `ul_engine/soda64.exe` + `ul_env.conf` + `ul_mcd/` (binder art, pmuser seed) + `pm/g7/dump/` (the same two dongle answers we have) |

`soda64.exe` is their ELF-on-Windows engine. It is **not stripped and has DWARF**
(`I:\ArcadeWork\MCD_20261119\dump-engine-build`, `src/core/mcd_cardsys.c`,
`mcd_disp_hook.c`, `src/host/gl/glx64.c`), so `nm`/`addr2line` give the real names.

## How it works: no reader emulation, just call the game's card handler

The real cabinet reads cards as **QR codes through webcams** (zbar). Ultra does not
emulate the webcam or PC/SC. It skips the camera and puts the decoded card code
straight into the game.

### Game side (C83 gamedump.exec, non-PIE)

- **Webcam thread `0x429ab0`**: zbar processor, `Y800` frames. It loops until
  `0x4199c0()` (webcam auto-ID done, byte `0x178a5d0`) and `byte[0x178cb10]`
  (card scanning enabled) are both set. Then, for each player 0..3 with
  `0x4a3790(p)` (that player's webcam is present), it grabs a frame
  (`0x4a3ad0`), runs `zbar_scan_image`, takes the first symbol's data, and under
  mutex `0x1c6f120 + p*0x28` fills the **scan slot**:
  ```
  slot = 0x178cae0 + p*0xc
  +0  char code[8]   // first 8 bytes of the QR text, e.g. "a0010000", "c01A1000"
  +8  0
  +9  pending = 1
  +a  notify  = 1
  +b  force   = 0
  ```
- **Main-loop consumer `0x44ced0`**: for each p, trylocks the same mutex. If
  `slot.pending`, it clears it and calls `0x42abf0(p, slot, notify, force)`.
- **`0x42abf0(p, code, notify, force)`**: the gate. If `!force`, it requires
  `byte[0x178cb10] != 0` and `0x444c40(p)` (player p is in a state that accepts
  a scan, using `0x17a9b60 + p*0xb60`). Then it calls **`0x42a7e0(p, code, notify)`**.
- **`0x42a7e0`**: first checks the code against a 300-entry special-code table at
  `0x179bd38` (stride 0xb0), else hands it to `0x42a5b0` (the normal card apply).
  It shows `P%u_SKIN_SCANNED`, `hud_p%u_already_scanned`, `CARD_SCAN_TIMEOUT`.
- `byte[0x178cb10]` is set by script binding `0x42ce90` (and setter `0x42ce80`),
  meaning the game is on a scan screen.

### Ultra side (soda64.exe)

- Per-frame hook inside `glx64_SwapBuffers` (so it runs on the game's GL thread,
  around `0x1c0051550` / `0x1c0051fbb`). Every 250 ms, if picks are queued
  (`mcd_cardsys_pending_picks`):
  1. It checks once that the game is the C83 build: bytes at `0x42a7e0` ==
     `41 57 41 56 49 89 f7` and at `0x444c40` == `31 c0 83 ff 03 76 09 c3`.
     Logs "scan bridge ready (C83 card handler verified)".
  2. If `byte[0x178cb10] && 0x444c40(0)`, it calls
     **`((int(*)(int,const char*,int))0x42a7e0)(0, code, 1)`** once per pick (up to 5,
     16-byte strings from `mcd_cardsys_take_picks`), and only for P1.
     Otherwise it logs "N pick(s) waiting: the game is not taking card scans right now".
- Picks come from an on-screen "binder" overlay (F3 / Select+Start). The card catalog
  is baked in: `mcd_heroes` (57 entries, ids 99.., codes `c01A1000`…) and `mcd_games`
  (212 item cards, codes `a0000000`…). Dumped to `ultraloader-re/mcd_cards.csv`.

### The "authorized to dispense more cards" fix: virtual dispenser

`mcd_disp_hook` is a detour on the game's dispenser update **`0x41b010`** (called
from `0x41b6b2`). The original 13 bytes are checked (`k_orig`), and the trampoline
starts `41 57 41 56 48 bf 5b c6 17 01 00 00 00 00 41 55…`. Before it calls the
original, it walks the 4 dispenser request queues at **`0x178a600 + i*0x40`**
(`+0` head, `+8` tail, `+0x10` count, `+0x28` busy). Each queued request whose item
(`node+0x10`) has bytes `+7 == 0` and `+8 == 0` is unlinked and the count is
decremented, so the game thinks the card was dispensed. Then it credits
`mcd_disp_credit[item+4 (player)]`, and `mcd_dispenser_drain` puts the card in the
binder. This is enabled by an env var (`MCD_DISPENSE_TO_BINDER`).

## `ul_env.conf` SODA_PATCH list (addr: new/old)

These are applied to the dump in memory. The card and setup patches:

| addr | patch | meaning |
|------|-------|---------|
| `0x4199c0` | `mov eax,1; ret` | webcam auto-ID done (`0x178a5d0`) |
| `0x4a3790` | `mov eax,1; ret` | player p webcam present |
| `0x439620` | `mov eax,1; ret` | factory setup done (`0x17a8ce0`, FactorySetupProc) |
| `0x430cf0` | `mov eax,1; ret` | flag `0x17a8b58` (default 1, used by CardDispenserProc / RegSyncProc) |
| `0x17c69e0`, `0x17c69ec` | `1` | license state (the decrypt-consumer state) |
| `0x6532a0`, `0x6530e0`, `0x653120` | `xor eax,eax; ret` | license decrypt wrappers skipped (blobs pre-patched with the plaintext at `0x16dedc0`/`0x16dee00`) |
| `0x653040`, `0x653090`, `0xb6c35c` (hasp_read), `0xb6c5bb` (hasp_write) | jmp to soda | dongle |
| `0x658b40`..`0x659370` | stubs / jmp to soda | the game's SDL joystick list (`0x187fc14` count, `0x187fc20` list): count forced to 4, virtual stations. This fixes the P2/P3/P4 joystick warnings. |
| `0x656480` | `xor eax,eax; ret` | udevadm SERIAL_SHORT / ID_INPUT_MOUSE probe |
| `0x430977`, `0x540523` | call → `0x70000000` / `0x70000300` | license init bridge (`mcd-v5`) |
| `0x78d230` | jmp soda | code-hash routine over `entry..0x117bdb9` (patches would break it) |
| `0x4d9659..0x4d96d0`, `0x16de184/8` | 0x2000→0x8000 sizes | engine allocator tuning, not needed for us |

## What this means for our loader (g7Minecraft.c)

The PC/SC scaffolding is the wrong path. The cards are webcam QR. Minimal port:

1. Patch `0x4199c0` and `0x4a3790` to return 1 (or leave the webcam thread starved
   and inject from our own thread).
2. Inject a scan: write `code[8]`, `+8=0`, `+a=1`, `+b=0`, then `+9=1` into
   `0x178cae0 + p*0xc` while holding the pthread mutex at `0x1c6f120 + p*0x28`.
   The game's own main loop then runs the normal gate (`0x42abf0`) and works for all
   4 players, with no cross-thread call. Ultra's direct `0x42a7e0` call is the
   alternative.
3. Virtual dispenser: drain the `0x178a600` queues before `0x41b010` (and give the
   card to the player's binder), or trace why the modal shows and patch that.
4. Codes: `ultraloader-re/mcd_cards.csv` (8-char codes; heroes `c01?1000`, items `a0??0000`).

## Implemented in our loader (2026-11-25): the game plays

### The code hash keys the scene data

The game hashes its own code (`0x78d230`, `0x416b20..0x117bdb9`) and uses the
result as a key: `0x5169b2` and `0x51911c` pass the hash, with the blobs at
`0x16de1c0..0x16dec00`, to `0xb1d5f0` to decrypt its scene data. The dump has two
branches made six NOPs in that range:

- `0x4a4b37`, the integrity check's failure screen (original `jne +0x2b6`, `0f 85 b6 02 00 00`);
- `0x4b1e6b`, the controller check (original `jne +0xa7`, `0f 85 a7 00 00 00`), Ultra's
  "guest controller bypass".

With the NOPs in the hash, the key is wrong and the scenes run without their data:
animations stay on their first frame, transitions and the level select never end,
a WeChat login wait appears, the scan screen shows one box stuck in the middle, and
the game reports "ALL DISPENSERS REPORT ERROR". The loader hashes a copy of the code
read from `gamedump.exec` with both branches put back, while the running code keeps
both skipped, as Ultra does. With both put back, `0x400000..+0x2200000` of the dump
hashes to Ultra's C83 identity (sha256 `c83f6ef2…`).

### Cabinet

- **Joysticks:** SDL's Linux joystick driver in the game is replaced by four virtual
  stations, each with 2 axes and 3 buttons.
- **RIO switches:** the raw records come from `0x638500`, the digital getter. START is
  board 0 switch `4*s`, and the station's buttons are switches `4*s+1..3`. There are
  two cabinet coins, plus test and service.
- **Dongle:** the memory image is `dump/dongle_memory.bin`, holding the card counters.
- **Dispenser:** virtual; owed cards are drained.
- **Allocator:** sized as Ultra does it.

### Keyboard: MAME's default layout

| | move | buttons 1/2/3 | start | coin |
|---|---|---|---|---|
| P1 | arrows | LCtrl / LAlt / Space | 1 | 5 |
| P2 | R F D G | A / S / Q | 2 | 6 |
| P3 | I K J L | RCtrl / RShift / Enter | 3 | 7 |
| P4 | keypad 8 2 4 6 | keypad 0 / . / Enter | 4 | 8 |

- **Coins:** 5 or 6 credit the P1/P2 cabinet, 7 or 8 the P3/P4 one.
- **Service and test:** 9 (or F1) is service, F2 is test.
- **Pads:** each pad drives its own station: stick or d-pad, A/B/X for the buttons,
  Start for START.

### The card binder

F3..F6 open it for P1..P4, or Select+Start on a pad. It is drawn over the game with
Dear ImGui (`src/imgui`, MIT) through `g7OverlayDraw`, in an Odin-style look with the
Cozette font. The hand goes into the station's webcam scan slot, one card after
another, and only while the game is on a scan screen. The card art is read from
`dump/binder_art/`, the 373x526 PNGs from Ultra's package.

### Test switches

- `MCD_TRACE_EVENTS`, `MCD_TRACE_CHECKS`, `MCD_TRACE_CMDS`: log the scene events, the
  polled flags, and the script commands after a transition.
- `MCD_TEST_BUTTON`, `MCD_TEST_RAWS`, `MCD_TEST_JOY`, `MCD_AUTOSCAN`, `MCD_CARDS`,
  `MCD_BINDER_OPEN`, `MCD_NO_INPUT`: drive the game in headless runs.
