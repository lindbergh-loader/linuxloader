# Cruis'n Blast 2.31 — decryption status and handoff

> Follow-up: [direct envelope analysis](cruisn-blast-231-envelope-followup.md)
> supersedes the mechanism and impossibility claims below. The software cipher
> is XTEA, its saved generator-state blob passes through the licensing
> dispatcher first, and declared protected regions also occupy LOAD #2.
> A decrypted executable has not yet been recovered.

Goal: get Cruis'n Blast **2.31** (2022) running under linuxloader. The currently
supported, working version is **1.25** (2017). This note records what was
checked on 2026-11-13 so the work can continue (e.g. with Codex AI).

## Files / versions

| Version | Where | `game` CRC32 | State |
|---|---|---|---|
| 1.25, OS 01.13, Aug 2017 | `roms/rawthrills/Cruis'n Blast.squashfs` (NFS) → `squashfs-root/game` | `0xb919dbd6` | **decrypted** memory dump, runs (loader id `CRUISN_BLAST_RT 0xddaf18ba`) |
| 2.31, OS 03.12.12u, Jun 2022 | `games/Cruisn Blast (2.31)/game` | file CRC `0xc19fbb51`, zlib `0xc19fbb51` (md5 `ff964889ab1144b007bf4f11a3dc7530`) | **encrypted** envelope original, does not run |

The 2.31 file is the same one examined on 2026-10-02 (see memory
`cruisn-blast-231-encrypted`).

## What is encrypted

Only the executable **`game`** is protected; every other file (the
`g5/race/data` content, libs, scripts) is ordinary. The protection is a
**SafeNet / Sentinel HASP "envelope"** (strings in the binary: `haspscope`,
`safenet-inc.com`, `Sentinel License Manager`, `/var/hasplm`).

ELF layout of 2.31 `game` (32-bit i386 EXEC, interp `/lib/ld-linux.so.2`,
stripped):

- Entry point **`0x9f1cf04`** — this is the envelope's decrypt/unpack stub,
  not the game's `_start`.
- No normal sections; a single empty `protect` section + `.shstrtab`. readelf
  reports "no .dynamic section in the segment" — the real dynamic info is
  hidden by the envelope.
- **LOAD #1 (code+rodata)**: file off `0x1000`, size ~10.9 MB (`0xa6d687`),
  vaddr `0x08048000`. Entropy **7.69** → **encrypted** (the CPU instructions).
- **LOAD #2 (data)**: file off `0xa6df90`, size ~24.7 MB, vaddr `0x08ab6f90`.
  Entropy **3.04** → **cleartext** (not encrypted).
- The **symbol-name table** (~`0x92000`, inside LOAD #1) stays readable
  (entropy 5.32), which is why names like `GameInputMaps`, `io_rio_loop`,
  `Wheel_Set`, `_ZN11RaceManager9RemoveCarEP5RTCar` show up as strings. It is a
  name list only — no code.

So the single blocker is the **~10.9 MB code segment** of `game`. Everything
else is already in the clear.

1.25 `game` for comparison: plain decrypted ELF, whole-file entropy 2.72,
normal `.text`/`.data`, contains the `programs_enc` string. It is the *output*
of decryption (a cabinet memory dump), not an envelope file.

## Three encryption layers (don't confuse them)

1. **Binary envelope** — the `game` code segment (LOAD #1, ~10.9 MB), SafeNet
   Sentinel HASP. This is layer the whole doc is about.
2. **Script encryption** — inside `g5/race/data/programs_enc`, files are a
   *mix*: in 2.31, **486 of 619 are already plaintext** (`//\nTRANS ...`,
   entropy ~4.7), **133 are encrypted** with the dongle's AES (`hasp_decrypt`),
   entropy >7. Same pattern in 1.25 (e.g. `andrew_level/clapmedium.txt` is
   encrypted in both). The cipher is real AES, not a weak XOR: XORing two
   encrypted scripts gives non-text (≈48% printable), so no reused keystream to
   exploit offline.
3. **Plain data** — everything else (the 24.7 MB data segment, all other
   `g5/race/data`, the 486 plaintext scripts) is already cleartext.

The loader never decrypts scripts. `rtRedirectPath` (`rawthrills.c:58`)
**redirects** reads of `encryptedScripts` (`g5/race/data/programs_enc`) to
`decryptedScripts` (`programs_dec`), which exists only in a memory **dump**
(1.25's squashfs ships 661 `programs_dec` files). On a real cabinet the game
decrypts `programs_enc` itself via the dongle.

All three layers lean on the **same dongle secret**. A running-cabinet memory
dump of 2.31 delivers both the decrypted code and the `programs_dec` copies at
once — that is the single unlock.

### Script layer is already solved for 2.31 (comparison result, 2026-11-13)

Compared `g5/race/data/programs_enc` between 1.25 and 2.31 (both 619 files):

- All **133 encrypted scripts are byte-identical** between the two versions
  (same ciphertext, 0 differ, 0 new). The encrypted content did not change.
- 1.25's dump ships the decrypted copies at `programs_dec/` (squashfs root, 619
  files). Because the ciphertext is identical, **1.25's `programs_dec` is a
  drop-in decrypted set for 2.31** — layer 2 needs no dongle work.
- The cipher was tested on 75 clean plaintext/ciphertext pairs (1.25
  `programs_dec` plaintext vs the identical 2.31/1.25 ciphertext):
  **not a fixed XOR keystream**, and **not plain ECB** (150 identical 16-byte
  plaintext blocks map to differing ciphertext) → chained/IV'd AES. Known
  plaintext does not recover the key.
- Identical plaintext→identical ciphertext across a 2017 and a 2022 build shows
  the **dongle vendor key is stable across versions** (useful, not a break).

Note: in 1.25 `programs_dec`, 50 of the 133 are still ciphertext (never
decrypted in that dump / binary blobs); 83 are real plaintext, 75 usable as
equal-length pairs.

Net: layer 2 is handled for 2.31 by reusing 1.25 `programs_dec`. Only layer 1
(the binary envelope) remains.

## Can the key be derived from 1.25? — No

- The dongle/AES key is **not** present in the 1.25 dump. The dump is the
  decrypted plaintext; the key was used inside the dongle and never stored.
- 2.31 is wrapped under its **own** embedded key, so a 1.25 key would not fit.
- Even if someone found the **encrypted 1.25** envelope (giving a matched
  plaintext+ciphertext pair for 1.25): AES is not broken by known-plaintext, so
  no key comes out. If the envelope uses a stream/XOR mode, `plaintext XOR
  ciphertext` yields 1.25's *keystream* only — tied to 1.25's key+counter, it
  decrypts nothing in 2.31.
- An encrypted 1.25 pair is still useful as a **known-answer test vector** for
  reverse-engineering the envelope stub (confirm cipher, mode, section layout,
  where the key blob sits). It does not by itself recover a usable key.

## Can 1.25's binary just run 2.31's data? — No

Compared via the two `racecrc.txt` manifests:

- 2136 files share a name; **2057 are byte-identical**, 79 differ, 28 new in
  2.31, 11 only in 1.25. ~96% of content is common.
- But the files that changed are the **code-coupled** ones:
  `g5/race/src/src/player_tbl.h`, `g5/g5engine/src/g5/game_core.h`,
  `core_rtdt_datatypes.h` — struct/layout headers. The 2.31 engine reads the
  data with a different in-memory format, and the engine lives *inside* the
  encrypted code. Dropping 2.31 data on the 1.25 binary = 2017 code vs 2022
  assets with mismatched layouts + tracks it doesn't know → crashes / still
  effectively 1.25.

The common-file overlap is useful for **staging** (ship only the ~100
changed/new files on top of a 1.25 tree), not for decryption.

## The one question that decides viability

Does the 2.31 entry stub (`0x9f1cf04`) derive the code-decrypt key from:

- **(a)** an **embedded** value inside the file (weak envelope config) → then the
  key can be extracted statically from 2.31 alone (and an encrypted 1.25 pair
  would only help verify it); **or**
- **(b)** a **dongle round-trip** (strong config) → then static decryption is
  not possible; need a **process-memory dump of 2.31 running on a real cabinet
  with its dongle**, same method that produced 1.25.

### Verdict: case (b), dongle round-trip (disassembled 2026-11-13)

The stub at `0x9f1cf04` was disassembled. It is the SafeNet Sentinel envelope
runtime stub (lives in the cleartext LOAD #2, decrypts LOAD #1 in place):

```
0x09f1cf04 entry0: call 0x9ef1163            ; thunk
0x09f1cf50 push ebp; ... sub esp,0xec
0x09f1cf5c call 0x9f1cf61 ; pop ebx          ; PIC base
0x09f1cf62 add ebx, 0x2e4df3
0x09f1cf9a mov ecx,[ebx+edx*4 - class.AtCamDisp]
0x09f1cfa1 mov eax, 0x5a9f4aa5 ; xor ecx,eax ; pointer de-obfuscation
```

Evidence it uses the dongle, not an embedded key:

- `hasp_decrypt` is a **linked symbol in the game binary** (dynsym name table,
  file off `0xabac7`, in the readable symbol region of LOAD #1). The Sentinel
  runtime — including the dongle AES decrypt call — is compiled in. The code
  key is unwrapped by `hasp_decrypt`, which only the physical dongle performs
  (`rtDongle.c:138`: the dongle's AES key never leaves it).
- `rtGames.c` detours `hasp_decrypt` at fixed addresses for the other envelope
  games, and every RT game here ships as a **memory dump** with `rtDump.c`
  rebuilding the GOT. If the key were embedded (case a), static unpack would
  suffice and no dumps would be needed. The dump-based design proves (b).

**Conclusion: no static decryption of 2.31 is possible.** The only route is a
process-memory dump of 2.31 running on a cabinet with its dongle — the same
method that produced the supported 1.25. Then rebuild its GOT (`rtDump.c`),
reuse 1.25 `programs_dec` for the scripts, and port the 1.25 descriptor in
`rtGames.c` to the new 2.31 addresses.

## How to reproduce the checks

```sh
# mount: //192.168.178.31/roms at ~/nfs/roms (cifs autofs)
# extract 1.25 for comparison
unsquashfs -d /dev/shm/cb125 -f "$HOME/nfs/roms/rawthrills/Cruis'n Blast.squashfs" \
    game version.txt racecrc.txt racefiles.txt

# 2.31 under analysis
ls "games/Cruisn Blast (2.31)/game"
```

Entropy by segment and the racecrc diff were computed with small Python
snippets (see session transcript 2026-11-13).

## Loader reference

- Game descriptor: `src/loader/rawthrills/rtGames.c` (`CRUISN_BLAST_RT`,
  `.fileCrc32 = 0xb919dbd6`).
- Dongle emulation (HASP API level, 128-byte memory, not a key oracle):
  `src/loader/rawthrills/rtDongle.c`.
- GOT/memory-dump rebuild for envelope games: `src/loader/rawthrills/rtDump.c`.

## Backporting the new 2.31 cars to 1.25 — WORKS (2026-11-13)

Since 2.31 won't run, the next idea: 2.31 only adds **cars** over 1.25 — can the
new cars be moved onto the working 1.25 engine? **Yes.** The 2.31 `.g5` models
load and render fine in the 1.25 engine; the only obstacle is a **naming**
convention, not a format change. Demonstrated on screen.

> Earlier in the session this section concluded "incompatible format / does not
> work". That was **wrong** — it came from a flawed control (a headless run that
> never reached car-select). The corrected finding is below.

### What 2.31 adds (all cleartext)

Six new cars: **camaro, corvette_59, stingray, nissan370z, eldorado**
(+ `stingray_upgrade`). Each is three plain files:

- `g5/race/data/g5/<car>.g5` — the model (header `LB02 … (C) 2010 PlayMechanix`)
- `conf/car/cars/<car>.txt` — paint-color list (all 11 colors map to materials
  already present in 1.25)
- a name in `conf/perm_assets.txt` (boot preload list)

The roster is a **compiled C enum** `CAR_TYPE_*` in `g5/race/src/src/game.h`:
2.31 appends indices 23–27 and bumps `NUM_CAR_TYPES` 23→28; `aud_game.h` adds
20 audio-event enum entries. No data file maps a car to model/physics/select —
that wiring is compiled in. So the *roster* is not data-driven, but the *model*
is an ordinary asset.

### The model format is the SAME — the problem is the node names

The `.g5` is a tree of fixed-size records (name fields on a **108-byte stride**,
null-terminated). Each car model holds a **bare root display node named after
the car** (`corvette\0`, `stingray\0`) plus parts `"<car>_nocolor"`,
`"<car>_brakelights"`, … The structure of a 2017 model (`corvette.g5`) and a
2022 model (`stingray.g5`) is byte-for-byte the same shape.

The engine loads a car by resolving its display object **by the slot name**:
`ObjCreateDisplayResAssert` (`0x082ee520`) does
`ResFindSubNth(0, "<slot>", 0x14, 0)` and asserts at `g5/obj.c:4183` if no
type-`0x14` node of that name exists. So dropping `stingray.g5` onto `lambo.g5`
fails **only because the model's root node is named `stingray`, not `lambo`** —
not because the bytes are unparseable.

### The fix: rename the model's nodes to the slot name (offset-safe)

All `"stingray"` tokens are node names (each followed by `_` or NUL), and names
sit in fixed-width fields, so renaming never moves a byte:

- same length (e.g. `stingray`→`corvette`, 8→8): a blind byte replace is exact.
- shorter (e.g. `stingray`→`lambo`): rewrite each name token in place and
  null-fill the leftover tail; the 108-byte stride is untouched.

### Demonstrated (local run, gamescope on the AMD GPU)

1.25 runs locally from the extracted squashfs via `scripts/linux/runcb.sh`
(`GAME_DIR` = squashfs-root, `LIBS32=build-linux/ll32lib` for 32-bit
`libglut.so.3`). It auto-reaches the attract car-select screen.

- `lambo.g5 ← raw 2.31 stingray.g5` → crash:
  `g5/obj.c:4183 Could not create object name 'lambo'` →
  `car_sel2.cpp:1813 switchCarSel`.
- `lambo.g5 ← stingray.g5 with nodes renamed stingray→lambo` → **0 asserts**,
  car-select renders.
- `huracan.g5 ← stingray.g5 renamed stingray→huracan` → the centered default
  car is **visibly the Corvette Stingray** (2.31 model), blue paint applied,
  labeled "Lamborghini Huracán" (the slot keeps its UI name/logo/stats).
  User-confirmed by screenshot.

### Verdict and the two real routes

The 2.31 car models are **compatible** with the 1.25 engine. No converter and no
dongle dump are needed to *use the models*.

- **Replace an existing car (works today, no binary patch):** rename the new
  model's nodes to the target slot name, copy its `conf/car/cars/<slot>.txt`
  colors. The slot keeps its name/logo/physics/select position; only the body
  (and paint set) change. Caveat: you lose the original car in that slot, and
  the UI still shows the old car's name — a cosmetic string patch in `game`
  could fix the label, since it's plaintext.
- **Add as genuinely new cars (needs a 1.25 binary patch):** keep the real
  names and add roster slots — bump `NUM_CAR_TYPES`, extend the compiled
  `CAR_TYPE_*` tables (model path, physics, select-screen slot, pricing, audio).
  This is the larger `game`-patching job, but it is now **only** the roster
  work: the models themselves already load.

Repro: models/libs under `/dev/shm/cb125full` (unsquashfs of
`Cruis'n Blast.squashfs`); 2.31 data under `games/Cruisn Blast (2.31)`. Rename
helper used: replace `stingray` tokens in `stingray.g5`, write over
`<slot>.g5`.

## Final working solution — the "hybrid" build (deployed 2026-11-13)

All 5 new cars, every one raceable and full size, keeping 5 premium cars:
`tools/cruisn-blast-backport-cars.py --preset hybrid`.

- **3 new cars replace redundant Lambos in the MAIN carousel**
  (Sting Ray→countach, '59 Corvette→diablo, Eldorado→veneno), named via
  `translation.csv`. Main slots are full-data normal cars: these work 100%.
- **2 new cars go on the Bonus page's only real-car slots**: `jeep`(2) and
  `london_taxi`(3) (Camaro, 370Z). jeep renders at 1.0; the taxi's 0.85
  showroom scale is lifted to 1.0 by the loader (see below).
- Keeps Aventador, Corvette Z06, GTR, Hummer, Huracan.

### Why only jeep + taxi on the Bonus page
The 8 bonus slots are joke vehicles, and their behaviour follows them **into the
race**, not just the car-select showroom:
- `cop_car`/POLICE (0x16): unchecked `<car>_glow_N` prop -> SEGV in car-select.
- `london_bus`(4)/`madagascar_bus1`(5): scaled down AND carry crowd passengers
  in the race — a sports car shows small with people around it.
- `trcratops`(0x14): animated creature — crashes when selected/raced.
- `madagascar_carriage`(7), `heli`(0x15): characters / rotor + no model.
Only `jeep` and `london_taxi` are real cars everywhere. The hidden types
(raptor/essence/ssc/wrangler/kia) are NOT set up as player cars — no
`CarParamsListFor` entry -> car-select crash — so they can't be used either.
That caps the Bonus page at 2 clean cars; the other 3 go in the main carousel.

### Loader-side scale (do NOT patch the game binary)
The taxi slot scales its car to 0.85 in `CarSel_CreateCar3D` (the three
`[ebx+0xb0/b4/b8]` stores at `0x817be94`). The loader lifts it to 1.0 **in
memory** (`cbCodePatches` in `rtGames.c`, applied from the
`parseCommandLineArgs` hook in `rtFrame.c`), gated on a `bonus_cars.flag` marker
file so the stock image is untouched. Patching the dump's bytes on disk runs on
the desktop but **crashes the cabinet loader at startup** (SIGSEGV in early
lib-init, before the game's own handler) — the in-memory patch keeps the dump
original (CRC `0xb919dbd6`, which the cabinet runs cleanly). Do not re-introduce
an `altFileCrc32` / modified-dump build.

### Deployment
- Loader: `make bato` -> `build-bato/linuxloader.so` -> rgs
  `system/rgs/emulators/linuxloader/` -> Batocera
  `/userdata/system/rgs/emulators/linuxloader/`. Backward compatible (stock CB
  unaffected: no marker -> no patch).
- Game: `--preset hybrid` on the unpacked squashfs root, repack
  (`mksquashfs ... -comp zstd -b 131072`), ship as a SEPARATE image so the stock
  one stays.
- ES: a new `.squashfs` rom defaults to the system's **wine** emulator and
  fails ("no <gameid>.windowsloader file"); add a gamelist entry pinning
  `<emulator>linuxloader</emulator>` (or set it once in the game's ES options).
- Reach the Bonus cars in-game: the `m` key / TUNES button (`cbExtraMaps`); the
  carousel is steered by the wheel axis, not the arrow keys.
