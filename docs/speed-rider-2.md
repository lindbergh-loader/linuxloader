# Speed Rider 2 (IGS)

Native support targets Speed Rider 2 (`speedrider2`, IGS 2011, M-101CN /
S-101CN), the leaning-bike sequel to [Speed Rider](speed-rider.md): one
seat, lean, throttle and two brake levers, Speed Driver 3's A36 board
behind `/dev/pccard0`, and the STM32 cabinet controller on the board's
USART1. Desktop (headless Gamescope/Mesa, 2026-11-12): loading, attract,
coins, start, the no-card route, mode / chapter / bike selection, the story
cutscene and a full race to time-up and the continue screen. Played on
the desktop by the user (2026-11-12): working 100%, after the bike-paint
shader fix. Not played on Batocera yet.

## The dump

`games/Speed Rider 2/` is a TeknoRaven package (its `game.json`):

| Path | Content |
| --- | --- |
| `game/bin_linux/speedrider2` | the game, stripped (file CRC 0xf10a20c0, loader ID 0x7964eca9) |
| `game/bin_linux/libso/` | Ogre 1.6.1, its plugins, Cg, libcasmcard |
| `game/roms/sr2_eprom_final.bin` | the board's decrypted firmware (512 KiB) |
| `game/data/` | models, scripts (`*.lua.enc`), `general/plugins.cfg` |
| `game/rewrite/` | the game's writable files (bookkeeping, settings, ranks, ghosts) |

The game runs from `game/bin_linux` (`../data`, `../rewrite`): use
`scripts/linux/runsr2.sh` (`GAMES_DIR` or `GAME_DIR`; `LIBS32` for the
host's 32-bit libraries). `build-linux/igs-sr2-libs/` holds what neither
the dump nor the host has: libXaw, libXt, libSM, libICE, libXmu, libXpm,
libpcsclite, libogg and libvorbis (copied from Speed Driver 3's dump; local
build artifacts, not committed). `bin_linux/ogre.cfg` asks for full screen
1920x1080: on the desktop run it inside Gamescope
(`gamescope -w 1920 -h 1080 -W 1280 -H 720 -- scripts/linux/runsr2.sh`).

The game uploads its own encrypted copy of the firmware (embedded in the
executable) through the internal ROM's loader: as for Speed Driver 3 the
`P2A_Loader*` calls are stood in for, and the board runs the dump's
decrypted ROM. `../data/general/plugins.cfg` names the cabinet's directory
for Ogre's plugins: the game reads a copy naming `libso/` (`ogrePlugins`).

## Controls

| Keyboard | Cabinet input |
| --- | --- |
| 5 / U | Coin (2 coins a credit by default) |
| 1 / I / Space | Start / confirm / turbo |
| Left / Right | Lean, and the menus' left/right |
| Up / Down | Throttle / both brakes |
| F1 / P | Service |
| F2 | Operator test menu |

The desktop gamepad uses its left stick to lean and its triggers for
throttle (RT) and the brakes (LT). Evdev uses analogue channels 1/2/3 for
lean, throttle and brake. The card prompt's right-hand choice plays
without a card. The rider's seat sensor ("infrared sensing" in the
operator menu) always reports a rider.

## The board (`src/loader/igs/igsA36.c`)

Speed Driver 3's A36 emulation (MAME's IGS036 AIC and timers), with what
Speed Rider 2's firmware (IGS's "E-3000 ASIC2PC sample code" of 2010/05/05
plus its GameIO) needs besides:

- **Internal-ROM services.** The firmware calls the internal ROM through a
  table of seven function pointers at SRAM `0x0200f000`, filled by the
  missing internal ROM at boot (an empty table reboots the firmware in a
  loop). The loader fills it with addresses in its internal ROM page,
  handled by `romService`: 0 interrupts the PC (`A2P_Comm_iSendInterrupt`),
  1 hands the data window over (bus owner), 3/4 remainder/divide (the
  USART baud divisor). 2, 5 and 6 are not seen called. Arguments go
  through the firmware's wrappers at `0x10004f8c` (globals `0x0200ec00`,
  `0x0200ec04`, result `0x0200e800`).
- **FIQ.** USART1's interrupt is AIC source 5, made a fast interrupt
  (FFER), positive edge, priority 7. The ARM core (`igsArm7.c`) now takes
  FIQs (vector `0x1c`); FVR reads clear edge-triggered fast sources.
- **The STM32 on USART1** (`0xfffa8000`, `igsStm32.c`, shared with Speed
  Rider). Every 50 ms the firmware's timer-1 handler parses the last
  eleven-byte reply (`0x44`, `0x100042f8`), sends a ten-byte `0x18` command
  (lamps; no handshake, unlike Speed Rider) and waits for its transmission.
  Its FIQ handler moves one byte per interrupt and clears the interrupt
  (ICCR) on its way out, so the USART is paced: TXRDY drops for one
  byte-time after each byte (`STM32_BYTE` cycles, about 10 us), TXEMPTY
  for two (the last step of a command waits for it), RXRDY for one after
  each byte read and after the receiver is enabled; a byte's time only
  passes outside the FIQ handler. Without that the edges are lost and the
  timer handler spins in its wait (and starves the PC's commands).
- **Pots.** VR1 lean, VR2 and VR4 the brakes, VR3 the throttle. The
  firmware reads the pedals as `1000 - raw * 1000 / 1024` and lean as
  `raw * 2000 / 1024 - 1000`; as TeknoRaven does, lean is `512 +/- 500`
  and each pedal reads 1023 released and 0 fully on (`a36Stm32Pots`,
  `a36Stm32Throttle`). The switch word is Speed Rider's.
- **Idle.** The firmware's main loop is `0x10002e0c..0x10002e5c`; the board
  thread uses about 9 % of a core.
- Writes to `0x60000008` (around each STM32 packet) and reads of
  `0x60000042/44` (at start) are accepted and ignored.

## The game (`igsGames.c`)

- The FPGA cipher: the key is Speed Driver 3's (firmware command 6), but
  the scripts take another mask, `f2145280628f0d3ab5f014e8e506b838`
  (`a36CipherMask`), found by frequency analysis of the encrypted Lua
  scripts (`GamePath.lua.enc` then decrypts to clean Lua).
- `a36ReplyPublish` `0x0890fc59`: `A36Interface::dispatchAction`'s reply
  race, as in Speed Driver 3 (here with `%ebx`); unfixed, startup can read
  a shifted reply and jump into garbage.
- `A36Loader` (iInit, iReset, iUnInit, iLoadData):
  `0x09091250 0x09091490 0x090912d0 0x09091b90` (found from
  `Real36::loadBinTo36` at `0x0890e8f0`, called with `"sr2.bin"`).

## Graphics

Ogre 1.6.1's GL render system on Mesa; the vp40/fp40 Cg programs go
through `igsNvProgram.c` as for Speed Driver 3. The bikes' and cars' paint
(`all_purpose_car_ps`) and `DeferredShadow` have IF/ELSE/ENDIF blocks:
left as they were, the bikes' fairings drew black or not at all (user
screenshots, 2026-11-12). The rewriter now predicates them: both sides
run, and each write inside a block goes through the block's mask (`CMP` in
fragment programs, so the other side's infinities cannot leak in). All 105
programs of a session (and SD3's 60, Speed Rider's 20) load in Mesa. The
blur pair (`HBlur_FP`, `VBlur_FP`) loops up to 255 times on a uniform
count and stays as it is.

## Debugging

`IGS_TRACE=1` logs switch changes and the firmware's console, `=2` adds
board status each second, timer writes and STM32 lamp commands, `=3`
services and every unknown access. `IGS_A27_BREAK=address[,...]` prints
the registers at ARM addresses. `IGS_SHOT=<dir>` / `IGS_SHOT_PERIOD` /
`IGS_SHOT_FROM` capture frames.

TeknoRaven's `TeknoRaven.dll` (core.igs 0.9.879) has an SR2 layer
(`[SR2/IO] TX10 validated; RX11 reply`, `SR2_USART1`, `[SR2/IROM]`); its
reply builder at `0x68cc0f3e` gave the pot encoding.
