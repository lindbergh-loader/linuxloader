# Wheel of Fortune (Raw Thrills, g3) — porting notes

State: **working (2026-10-02)**, played on Batocera with a DualSense: it boots
to the game, the RIO switches and the spinner work (see "Controls" below).
The sections after it are the porting notes as they were taken, kept for the
history: the crash and the input plan they describe were resolved by commit
e619beb (the HASP hooks one byte off, the dongle record, the GLUT game mode,
RIO and the spinner emulated in `rawthrills/rtWof.c`).

## Controls

- RIO switches (`wofRioSwitches`): PUSH = START or BUTTON_1, coins 1/2,
  service, test, volume up/down = P1 UP/DOWN.
- Spinner (`rtWof.c`): P1 LEFT/RIGHT (the d-pad) turn it slowly and
  steadily, 200 counts/s, to move through the menus; a stick on ANALOGUE_3
  spins it as fast as it is pushed (4000 counts/s at full deflection, 10 %
  deadzone), only when that channel is mapped (else it would read full
  left); the mouse's horizontal moves (ANALOGUE_1) too.
- Batocera: the generator's kind `wof` maps the pad's left stick to
  ANALOGUE_3, b/a to PUSH, the d-pad to the spinner (left/right) and the
  volume (up/down), and one mouse's moves (not a touchpad's, a position, nor
  its buttons) to ANALOGUE_1.

## 0. Scope decisions (user)
- First build = **boot + 8 cabinet switches from the desktop**. IR-gun
  aim/trigger + dial = second pass.
- Game dump: `<games>/Wheel of Fortune/`
  — stripped 32-bit ELF `game` + 43 MB `game.idb` (no custom names in the IDB:
  useless). No full data dump, no hardware yet; hardware test is on the user.
- TeknoParrot **cannot** feed input to WOF (its ElfLdr2/hints.dat is BBW-only;
  confirmed dead end). The linuxloader must feed input itself.

## 1. Game / binary facts
- Engine: **g3** (like T4/BBW), `rootPath = "/g3"`, glut-linked, **no libcsv**.
- **Fully stripped**: only `_init`/`_fini` dynamic exports, no `.symtab`.
  Every hook must use the `RtSymbol` name→address table (`.symbols`), like
  T4/BBW.
- ELF layout (vaddr = `0x804dcc0 + (foff − 0x5cc0)`):
  - `.text` vaddr `0x804dcc0`, file `0x5cc0`, size `0x2ffd5c`
  - `.rodata` `0x834da40` + `0x7a63f`
  - `.data` `0x83c99c0`–`0x84ec95c`; `.bss` from `0x84ec960`
  - `.plt` `0x804c32c`; `pthread_create@plt` = `0x804cefc`
  - phdr[2] = RX LOAD, vaddr `0x8048000`, file off `0`, flags 5.
- **Loader ID = `0x4a739bbd`** = CRC32 over the first `0x4000` bytes at
  `phdr[2].p_vaddr + 10` (i.e. file offset `0xa`). Verified by recomputation.
  Defined as `WHEEL_OF_FORTUNE_RT` in `config.h`.
- Version string: "Wheel Of Fortune v03.00 RELEASE, Build: Jun 3 2015".
- **WOF boots without any HASP dongle** (see §6): the game runs natively
  without one for minutes. HASP is only needed for the (here plaintext) data.

## 2. Code written (committed with this note)
- `src/loader/config/config.h`: `#define WHEEL_OF_FORTUNE_RT 0x4a739bbd`
- `src/loader/config/gameData.c`: entry
  `{WHEEL_OF_FORTUNE_RT, "Wheel of Fortune", "wheel-of-fortune", "Raw Thrills", "RT-WOF", "2019", "1366x768", WORKING, NO_JVS_IO, SHOOTING, 1366, 768, -1, 0,0,0,0,0,0, RED}`
  (year "2019" is a placeholder — binary build is 2015, game released ~2017-19; fix if known.)
- `src/loader/rawthrills/rtGames.c`: WOF section after the BBW arrays:
  - `wofSymbols[]` (name→addr for the stripped binary):
    ```
    hasp_login           0x08206800
    hasp_logout          0x082068a0
    hasp_encrypt         0x0820698c
    hasp_decrypt         0x08206a78
    hasp_read            0x082075f7
    hasp_write           0x082076c4
    hasp_get_sessioninfo 0x0820737f
    DongleEncrypt        0x082056b0
    DongleDecrypt        0x08205640
    TracerGuard          0x081e3ef8
    PostInputEvent       0x080500a0   (for the input pass)
    ```
    (Also known, not yet in the table: `hasp_update 0x82070d2`,
    `hasp_get_size 0x8207790`, `hasp_legacy_encrypt 0x8206b67`,
    `hasp_legacy_decrypt 0x8206c78`, `legacy_set_rtc 0x8206d88`,
    `legacy_set_idletime 0x8206faa`. **No `hasp_free` exists** → the loader
    logs a benign "not found" warning.)
  - `wofStubs[]`: `{TracerGuard, 1}` (it forks a ptrace parent; return 1).
  - `wofRootAliases[]`: `{"/wofuser", "wofuser"}`.
  - Descriptor: `crc32, envelopeSelfSlot=-1, symbols, haspFeature=0xffff0000,
    haspMemoryFileId=0xfff2, stubs, rootPath="/g3", rootAliases,
    sizeArgument="-r%dx%d"`. `haspAnswers = NULL` (WOF `.g3` data is
    plaintext — no hasp answers folder).
- **Deliberately NOT set** (second pass): `setModeSymbol`, `windowOpenSymbol`,
  any jamma/gun fields. WOF's `SetVideoMode` has a different signature than
  T4/BBW's (it `sscanf("r%dx%d")`s the command line) so the g3 setMode hook
  shape does not apply; the game's default 1366×768 fallback
  (`0x8076416`) + `sizeArgument` gives the right window for now.
- `scripts/linux/runwof.sh`: launch script (pattern of runb.sh, uses
  `build-bato/linuxloader.so`).
- Build dir that works: **`build-bato`** (Batocera i686 cross toolchain;
  `make linuxloader`). `build-linux/linuxloader.so` is stale (13:54, pre-WOF).
  Both .so are 32-bit i386.

## 3. HASP / dongle
- HASP HL API functions (prologues verified):
  ```
  hasp_login           0x8206800
  hasp_logout          0x82068a0
  hasp_encrypt         0x820698c
  hasp_decrypt         0x8206a78
  hasp_read            0x82075f7   (starts with 0x90 nop)
  hasp_write           0x82076c4
  hasp_get_sessioninfo 0x820737f   (starts with 0x90 nop)
  hasp_update          0x82070d2
  hasp_get_size        0x8207790
  hasp_legacy_encrypt  0x8206b67
  hasp_legacy_decrypt  0x8206c78
  legacy_set_rtc       0x8206d88
  legacy_set_idletime  0x8206faa
  DongleEncrypt        0x82056b0   (table at 0x8529d20)
  DongleDecrypt        0x8205640
  TracerGuard          0x81e3ef8   (prologue 55 89 e5 83 ec 38 e8 …)
  ```
- `haspFeature = 0xffff0000`, `haspMemoryFileId = 0xfff2`.
- Dongle status word `0x84fe260`.
- Loader detour names it resolves: hasp_login/logout/encrypt/decrypt/
  read/write/get_sessioninfo/free + DongleEncrypt/Decrypt (conditional) +
  DongleWriteLoop/DongleNoise/DongleWriteHLFeature (missing in WOF → warnings only).

## 4. Video
- `.sizeArgument = "-r%dx%d"` — verified: option loop `0x8075a80`,
  `SetVideoMode 0x80cd6c0` does `sscanf(str, "r%dx%d", w, h)`.
  Prologue `55 b8 ff ff ff ff 89 e5 53 83 ec 14` (11-byte boundary; starts
  `mov eax,-1` — **not** the T4 `int(int)` shape).
- Default resolution fallback `0x8076416` → 1366×768.
- Window-creation candidates `0x8077f55` / `0x8077305` both start with `e8`
  (call) = mid-function, **not** function starts. Real function starts must be
  located before `.windowOpenSymbol` can be set.
- `glutMainLoop` caller: `0x8076141`.
- "VidStart: glXGetVideoSyncSGI() not working" is a normal syslog line at boot.

## 5. Input (fully decoded, not yet wired)
### 5.1 Strategy
Drive cabinet switches by posting SET/CLEAR events through a detoured
`PostInputEvent` (`0x80500a0`, prologue `55 89 e5 57 56 53 83 ec 1c`, 11 B).
Bypasses the RIO board entirely. Alternate path: the game's own glut keyboard
handler `0x8077490` maps PC keys→events and works with a physical keyboard.

### 5.2 Event semantics
Events are SET/CLEAR pairs updating switch-state words `0x8527fc0` (SET:
`or eax`; CLEAR: `and ~bit`) + mirror `0x8527fbc` + per-switch array
`[switch*4+0x8527fa4]` (bit0). SET epilogue `0x8050348`, CLEAR epilogue
`0x80504e0`. `PostInputEvent(event, data)`: high byte flags bit0→0x4000,
bit1→0x10000, bit2→0x8000 in `0x8527fbc`; low byte 0x00–0x3f else returns 0.

### 5.3 Event→switch→bit map (complete)
```
0x02/0x03 → sw 0x09 bit 0x1      0x04/0x05 → sw 0x0a bit 0x2
0x06/0x07 → sw 0x0b bit 0x4      0x08/0x09 → sw 0x0c
0x0a/0x0b → sw 0x0d              0x0e/0x0f → sw 0x10
0x10/0x11 → sw 0x11              0x12/0x13 → sw 0x16
0x14/0x15 → sw 0x17              0x16/0x17 → sw 1 bit 0x20
0x18/0x19 → sw 2 bit 0x40        0x1a/0x1b → sw 3 bit 0x80
0x1c/0x1d → sw 4 bit 0x100       0x1e/0x1f → sw 5 bit 0x200
0x20/0x21 → sw 6 bit 0x400
0x3b      → sw 0x0e bit 0x1000 (CLEAR-only, mask 0xffffefff)
Analog: 0x0c (handler 0x80507b6): data[0]/data[1] → 0x8527fb0/0x8527fb4
        + bit 0x8 in 0x8527fc0.
        0x0d (0x805079c): adds data[0] to float 0x8527fb8 + bit 0x10 = DIAL.
```
Dispatch table `0x834db90` (64 entries): ev 0/1 → nop `0x8050118`;
0x02–0x20 → `0x8050865`…`0x805066a`; 0x21–0x38 → `0x8050a0e`…`0x8050934`;
0x39→`0x80504ce`, 0x3a→`0x8050338`, 0x3b→`0x8050656`, 0x3c→`0x8050223`,
0x3d→`0x8050134`, 0x3e→`0x80500e6` (switch release), 0x3f→`0x8050874`.
39 direct `PostInputEvent` callers known; none in the board region (the board
queues events, doesn't call directly).

### 5.4 Switch-word consumers
The switch words are read **only** inside the input subsystem
`0x8050000`–`0x8051300` (`0x8527fc0`: 18 refs; `0x8527fbc`: 21;
`0x8527fa4`: 12 — all in that region) → game logic consumes switch state via
getters in that region. The event-based strategy is therefore live.

### 5.5 Frame hub `0x8050d60` (prologue `55 89 e5 53 83 ec 14`, 7 B)
Clears `0x852822c`/`0x8528234`/`0x852823c`, `and [0x8527fbc], 0xfffe3fff`
(one-frame bits 0x4000/0x8000/0x10000), calls `0x8077460` (periodic event;
flag `0x8533b80`), `0x80746e0`, timer `0x84ec98c` (wraps at 0x1387), RIO
monitor `0x8074c70`; then a callback dispatch loop i=1..0x1e: cb
`[i*8+0x8528064]`, last-state `[i*8+0x8528060]`, state `[i*4+0x8527fe4]`,
arg2 `0x8527fa0` — **dormant** (state array never written, no cbs).
16-channel block `0x85281c0` mostly dormant except ch1 data `0x85281c8`
(consumed `0x805115d..0x80512e0`, `0x8052954..0x8052c66`).

### 5.6 Board = RIO serial, 2304 baud ASCII (fully decoded)
- Sole external API: `boardOp(int op, int argc, int *argv)` dispatcher
  `0x81286a3` (prologue `55 89 e5 53 83 ec 74`, 7 B; op&0xff ≤ 0x34, jump
  table `0x835b1cc`, 53 entries). Context ptr global `0x85569c0` (null-checked
  per op; error return `0xffffdc76`); board obj global `0x84f43e4`.
- **No pthread_create in the board region** — the board is not a thread; the
  read path is driven by `boardOp` calls from the game.
- `0x812bf58` = handshake loop: loop { `0x812be82` serial read, `0x812c480`
  parse pending, byte-parse; idle: every 1000th idle iter sends `"js\r\n\r"`
  + `"jv\r\n\r"` via `0x812bea9`; returns -1 after >100 rounds }.
- Connect fn `0x812c249` (tcsetattr raw, tcflush, run loop, retry once,
  cleanup `0x812c3fd`). Sole caller `0x812780e` (board init).
- **`0x812c480` = per-frame reader+parser**, called from boardOp at
  `0x812a0b6` (the per-frame poll op — **which op number it is: not yet
  identified**) and from the handshake loop: `read(fd,buf,0xff)` → ring
  buffer (base +0x1b8, size +0x1bc, write +0x1c0, read +0x1c4); byte state
  machine framing messages (in-progress flag +0x1cc, msg start +0x238,
  len +0x23c, first char +0x240, msg count +0x14); messages starting
  'd'/'D'/'g'/'G' get a `\r` inserted ('g' likely gun, 'd' likely dial);
  obj+0 bit 0x4 = raw passthrough.
- Handshake 's' frame: hex byte at buffer+9 via `0x812bb84`, debounced 10×;
  the stable value only feeds the "Found JAMMA (ver = 0x%04X, dip = 0x%02X)"
  log (`0x835b42c`) — it's the DIP, not the live switch word.
- Message queue: head `0x85571a0`, tail `0x85571a4`, count `0x812bcec`;
  boardOp 0x08 = send "js", 0x0a = queue count, 0x0d = set screen size
  (w/h from `0x8534640`/`0x8534642`), 0x10 = ctx bits 0x100/0x200 + flush,
  0x11 = flag + cmd 0x0a/0x0b, 0x15 = timers.
- High-level `0x804faa0(id 0-6)` = enable/disable reporting bits only;
  `0x8076b60` = LOCKUP reporter.
- All `pthread_create` sites (none in board): `0x8120148` (ALSA audio,
  entry `0x812045a`), `0x8130d00`, `0x8130d6e`, `0x81e39c7`, `0x8325e1d`,
  `0x8325e61`, `0x834b3f0`, `0x834b43d`.

### 5.7 The one missing piece: cabinet label → switch# map
8 cabinet switches, label table `0x845cde4..0x845cfc0` (0x44 stride, .data):
`"PUSH" BUTTON` (start), `COIN 1`, `COIN 2`, `VOLUME DOWN`, `VOLUME UP`,
`TEST`, `BILL`, `SERVICE`. Keyboard test-screen labels
`0x835282c..0x835287a`: `[ BACKSPACE ]`, `[ ENTER ]`, `[ +SHIFT ]`,
`[ -SHIFT ]`, `[ SPACE ]`, `START BUTTON`, `DASH TOP`, `DASH BOTTOM`.

Leads (in priority order):
1. Identify the boardOp op whose case is at `0x812a0b6` (per-frame parse) and
   trace how the game dequeues parsed messages (msg count `+0x14`, first char
   `+0x240`) — find the converter turning 's'/'g'/'d' messages into
   `PostInputEvent` events or direct switch-word writes; that converter IS the
   board-bit→switch/event map (= cabinet label order).
2. SWITCH TEST scene: fn start `0x80b0bc0` ("SWITCH TEST" ref `0x80b0c7a`)
   reads the board raw word, not `0x8527fc0` — its per-row state source is the
   definitive label→bit map.
3. Classify the switch-word getters inside `0x8050xxx` by behavior (coin
   credit, start, test menu, volume).

## 6. Boot test — 2026-09-30 (results)
### 6.1 Launch
- The game binary **lacked the execute bit** — `chmod +x game` done.
- Launch (see `scripts/linux/runwof.sh`):
  ```
  cd "<games>/Wheel of Fortune/"
  export LD_LIBRARY_PATH=<repo>/libs/linux_x86:\
<32-bit libs>:\
<TeknoParrot>/ElfLdr2/libs:\
<repo>/build-bato
  LD_PRELOAD=<repo>/build-bato/linuxloader.so ./game
  ```
- The loader identifies it: banner prints `GAME: Wheel of Fortune /
  GAME ID: RT-WOF`, and the loader runs a **copied+patched** game from
  `~/.cache/linuxloader/rawthrills/<hash>/game` (this run's dir: `c6b8dfa2`).
- Note: **no `Raw Thrills:` log lines appear on stdout** in the boot log —
  check where the loader's `log_warn`/`log_info` go before debugging hook
  installation (they may go to a log file, not stderr).

### 6.2 Crash (with our loader)
Game exits **255** (~1 s after start), right after audio init fails. The
game's own SIGSEGV handler catches the fault, dumps registers, and calls
exit(-1) → **no kernel core is produced** (the handler swallows the signal).
Handler dump (identical in two runs):
```
eax: 0      ebx: 08529D34   (0x8529d20+0x14 = DongleEncrypt/Decrypt table!)
edx: 0A63E0E0/0A1F30E0  ecx: 0  edi: FFAA807C  ebp: FFAA8098
eip: 08207380  (= hasp_get_sessioninfo + 1; that instruction is
                `83 ec 1c` = sub $0x1c,%esp — cannot fault → dump is odd)
cr2: 0x5657EFBD  (deterministic; a dangling heap-ish pointer)
```
Under **gdb** the *first* SIGSEGV is different (nondeterministic
manifestation): `eip 0xf7ffcfec` inside `/lib/ld-linux.so.2`, faulting
instruction `add %al,(%eax)` with `eax=9` → **store to address 0x9**
(near-null), `ebx=0x82059bf` (game code, near DongleEncrypt), caller return
address in the stack region, only 2 stack frames → smells like memory
corruption / a corrupted GOT or lazy-resolve path.

Interpretation: some loader interceptor corrupts memory (or installs a bad
pointer); the fault location varies with the address layout (gdb vs not).
Prime suspects: the `glXGetProcAddressARB` wrapper (`frameScaleInit`),
filesystem `open()` redirection, or the dongle detours.

### 6.3 Audio (environment issue, NOT the loader)
- Native ALSA `snd_pcm_open("default")` fails: the 32-bit pipewire plugin is
  missing (`/usr/lib32/alsa-lib/` does not exist; only the 64-bit
  `/usr/lib/alsa-lib/libasound_module_pcm_pipewire.so` is present).
- Workaround tried: `ALSA_CONFIG_PATH=/tmp/wof-asoundrc` with
  `pcm.!default { type hw card 0 device 0 }` → still fails ("No such file or
  directory" — the hw path is reached but something else is missing; not yet
  solved).
- Under BudgieLoader/wine, audio works (Cygwin's audio stack) — that's why
  earlier runs logged "PCM state: PREPARED".
- **The game survives audio failure** — it is not fatal by itself.

### 6.4 Critical control: native run without our loader
`./game` with only `LD_LIBRARY_PATH` (no LD_PRELOAD): audio fails but **the
game runs** (alive at 25 s and 75 s, logs "Spinner disconnected! Attempting
to reconnect…" — the WOF wheel = "Spinner"). Implication: **WOF does not
require the HASP dongle to boot** (at least in its first minute; data here is
plaintext). So the crash is caused by our loader's interception, not by
missing dongle/board.

### 6.5 Other observations
- With our loader the game's syslog lands at the **game root**
  (`Wheel of Fortune/syslog_wof.txt`), not `wofuser/log/` — path redirection
  is active; read the root file, not the wofuser one, when debugging.
- Older cores in coredumpctl from `~/.cache/linuxloader/rawthrills/c6b8dfa2/game`
  at 04:08–04:09 CEST (1× SIGABRT 372 MB, 3× SIGSEGV) — earlier attempts
  (possibly the user's morning runs); `b2452857` = another game.

## 7. Open items / next session
1. **Fix the loader-induced crash** (the blocker for boot). Plan:
   - find where `log_warn` output goes, rerun and read all loader warnings;
   - bisect the interceptors (e.g. temporarily disable the glX wrapper /
     filesystem redirection in a debug build) or use an LD_PRELOAD probe;
   - re-check the dongle detours (crash context points at the dongle table
     `0x8529d20` and `hasp_get_sessioninfo`).
2. **Audio environment**: get 32-bit ALSA working for native runs
   (install 32-bit pipewire/pulse plugin, or a working asoundrc), so boot
   tests match the BudgieLoader baseline.
3. **Cabinet switches**: resolve the label→switch# map (§5.7 lead 1 first),
   then wire desktop keys → `PostInputEvent` SET/CLEAR pairs (the event map
   in §5.3 is complete); add `boardOp`/`PostInputEvent`/frame-poll
   prologues to the descriptor.
4. **Video hooks** (if needed beyond `sizeArgument`): locate the real
   window-creation function start (candidates at `0x8077f55`/`0x8077305` are
   mid-function); note WOF's `SetVideoMode` is not T4-shaped.
5. **Second pass**: dial (ev 0x0d), IR guns (ev 0x0c position; trigger),
   then hand off for hardware test (user side).

## 8. Environment / tooling notes
- `/tmp/pltnames.json`: PLT vaddr→name map (406 entries).
- objdump quirk: `--start-address`/`--stop-address` need the `0x` prefix;
  functions separated by 2-byte `lea 0x0(%esi),%esi` pads; xref scan via
  python `data.find` over `.text` (call rel32 = `e8`+int32, target =
  `foff − 0x5cc0 + 0x804dcc0 + 5 + rel`).
- ptrace YAMA scope 1: we CAN ptrace/gdb a game we launch ourselves from the
  shell (used today successfully). The BudgieLoader/wine game is NOT our
  descendant → cannot be ptraced that way.
- TP launch reference (input via TP is a dead end, but it boots):
  `cd .../drive_c/teknoparrot && WINEPREFIX=<wineprefix>
  WINEDEBUG=-all wine ./TeknoParrotUi.exe --profile=WheelOfFortune.xml`.
- `game.idb`: no custom function names — do not expect name resolution there.
