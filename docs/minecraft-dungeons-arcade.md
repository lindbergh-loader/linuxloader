# Minecraft Dungeons Arcade (g7) — port / dongle-recording findings

Status as of 2026-11-05: **blocked under BudgieLoader+wine by TeknoParrot's own
anti-VM**, after clearing two earlier blockers. Written so another agent can pick
it up. Companion: [teknoparrot-patreon-activation.md](teknoparrot-patreon-activation.md),
[dongle-recording.md](dongle-recording.md), [hasp-bypass.md](hasp-bypass.md).

## The goal

Record the HASP dongle encrypt/decrypt answers so the game runs under our native
`linuxloader64`. Under the native loader the game already runs to the dongle calls
but two decrypts return "no answer":

```
decrypt len 128  sha256 454ce150c44400c6814ee0acc3412054b0dd95a5e3bfbbed61aca345d55e50de -> no answer
decrypt len 64   sha256 1d57e7b041bc3c5fee583915833137a058b09f346ba20127a0d174ce20f1500f -> no answer
```

The plan was to launch an instrumented dump through TeknoParrot's BudgieLoader
(which has the real HASP emulation in `TeknoParrot64.dll`) and log the answers
via a code cave. See [dongle-recording.md](dongle-recording.md) and
`tools/dongle-recording/mcd-cave.S`.

## Files

Game dir: `wineprefix/drive_c/game/Minecraft Dungeons Arcade/pm/g7/minecraft/`

| file            | size      | crc32      | what it is |
|-----------------|-----------|------------|------------|
| `gamedump.exec` | 35651584  | 0x46229ce6 | TP memory dump, clean (section-less) |
| `gamedump.exec2`| 35651584  | 0x454e6ad3 | clean + dongle cave (`mcd-cave.S`) |
| `gamedump.exec3`| 35652160  | 0xcdcf28a1 | clean + **reconstructed section headers** |
| `gamedump.exec4`| 35652160  | 0x9a3349c0 | cave + **reconstructed section headers** |

`exec3`/`exec4` were built with `tools/dongle-recording/mcd-reconstruct-sections.py`.

Run environment (desktop, for recording only): runner
`~/code/windowsarcadeloader/wine-runners/GE-Proton11-7-x86_64/bin/wine`,
`WINEPREFIX=~/code/linuxloader/wineprefix`. Launch BudgieLoader directly under a
pty so its stderr (where the cave writes) is captured:

```sh
script -qfc "$RUN/bin/wine 'C:\teknoparrot\ElfLdr2\x64\BudgieLoader_x64.exe' \
  'C:\game\Minecraft Dungeons Arcade\pm\g7\minecraft\gamedump.exec4'" out.log
```

The TP profile `UserProfiles/Minecraft.xml` accepts `gamedump.exec2|gamedump.exec`
as `ExecutableName`; add `exec4` there to launch from the UI.

## Blocker 1 — Patreon activation (CLEARED)

BudgieLoader's subscription check is online and server-side: it reads
`HKCU\Software\TeknoGods\TeknoParrot\PatreonSerialKey` and GETs
`teknoparrot.com/vmp/activation.php` (details in
[teknoparrot-patreon-activation.md](teknoparrot-patreon-activation.md)). The old
key was banned; a new key activates ("Activation key ok!"). Done.

## Blocker 2 — section-less dump crashes BudgieLoader (CLEARED by reconstruction)

The dump is a **dynamic ELF with no section headers** (`readelf -S` → "no
sections"), but with a valid `PT_DYNAMIC`. BudgieLoader (ElfLdr2) resolves imports
via **section headers** and prints:

```
Warning SHT_DYNSYM not found in section headers
Warning aborting symbol parsing due to invalid name ptr
```

then crashes. The ELF entry `0x416b20` is a standard glibc `_start`:

```
mov $0x117bd00,%rcx      ; __libc_csu_init
mov $0x416200,%rdi       ; main
call *0x16d3fd0(%rip)      ; __libc_start_main via GOT slot 0x16d3fd0
```

In the dump, `GOT[0x16d3fd0] = 0x7fbe8a9a0b10` — a pointer into the **original
process's libc** (mapped at `0x7f…`), which is **not in the dump** (dump covers
only `0x400000..0x2600000`, the main executable — the shared libs were separate
mmaps). BudgieLoader can't relink it (no section headers to parse), the slot ends
up null, and `call *0x16d3fd0` → `RIP=0` crash (the `[CRASH-VEH] code@RIP: 00 00…`
dump). Our native loader works because it parses `PT_DYNAMIC` directly and supplies
libc — it never needed section headers.

**Fix:** reconstruct a minimal section header table from `PT_DYNAMIC`.
`readelf -d gamedump.exec` gives everything:

| dyn tag | value | → section |
|---------|-------|-----------|
| SYMTAB  | 0x400458  | .dynsym (DYNSYM), entsize 24 |
| STRTAB  | 0x4044f0  | .dynstr (STRTAB), STRSZ 3131 |
| HASH    | 0x1fa1758 | .hash (nbucket=139, nchain=**214 syms**) |
| RELA    | 0x408030  | .rela.dyn, size 384 |
| JMPREL  | 0x408378  | .rela.plt, PLTRELSZ 4296 |
| DYNAMIC | 0x16d27a8 | .dynamic, size 0x290 |
| PLTGOT  | 0x16d2a68 | (GOT) |

`nchain` from the SysV hash table = symbol count (214). `.dynsym` sh_info (first
global) = 1. All these live in the file at `offset = vaddr - 0x400000`. The script
appends a `.shstrtab` + an 8-entry section header table at EOF and patches the ELF
header (`e_shoff`, `e_shentsize=64`, `e_shnum=8`, `e_shstrndx=7`).

Result: `exec3`/`exec4` load cleanly under BudgieLoader — activation, network,
directory config, **no symbol-parse warnings, no crash**, and `readelf --dyn-syms`
now lists all 214 symbols with names (incl. the `HASPE_LINUX_LOADER_1.1` ones).

## Blocker 3 — TeknoParrot64.dll anti-VM (OPEN)

With the dump loading, the game aborts with a dialog:

> **Sorry, This application cannot run under a virtual machine**

The user confirmed this comes from **`TeknoParrot64.dll`** — TeknoParrot's own
anti-VM in their proprietary emulator DLL (the HASP layer), not the game. It
false-positives on wine/Proton on bare metal. `TeknoParrot64.dll` (25 MB) is
packed: its real sections (`.teknoGo`) are empty on disk and filled at runtime, so
there is nothing to read statically. This is the current wall for the
BudgieLoader-under-wine recording path.

(Note: the **game's own** dump also contains a WIBU CodeMeter `CmActLicense`
VM-name table — VMware/VirtualBox/QEMU/Xen/KVM/Hyper-V/Docker/… at `0x12a01da` — with
a VM-type getter at `0xb85fe0` reading globals `0x176e2f8`/`0x176e2fc` (0 = no VM),
set by detectors `0xccbfd8`/`0xccc113`. Forcing those to 0 did **not** change the
crash, confirming the enforcement is in `TeknoParrot64.dll`, not the game. This is
a dead end for the wine path.)

## Suggested next steps (for whoever continues)

1. **Record on real Windows** — on bare-metal Windows TeknoParrot64.dll's anti-VM
   passes as designed; run BudgieLoader on `gamedump.exec4` (cave) there and
   capture `@@E@`/`@@D@` records, then `split-records.py` (`--status-bytes 4`).
   The section-header reconstruction is still needed if that BudgieLoader build
   also parses symbols via section headers (same version here is 2026-10-26).
2. **Skip TeknoParrot entirely** — the native `linuxloader64` runs the dump to the
   dongle without TeknoParrot64.dll. Work out how the game *consumes* the two
   decrypt outputs (128/64 bytes) around the HASP decrypt sites and whether they
   can be answered without the real dongle. Decrypt call sites (from the cave):
   `0x652f93`, `0x653287` (encrypt `0xb66cbd`); `0x6532c7` (decrypt `0xb66dcf`).
   HASP API in the dump: login `0xb66808`, read `0xb6c35c`, write `0xb6c5bb`,
   decrypt `0xb66dcf`, sessioninfo `0xb6b340`.
3. **Do not** bypass TeknoParrot64.dll's anti-VM — it is a deliberate protection in
   a third-party commercial product; use (1) instead.

## Update 2026-11-05 (afternoon) — older DLL avoids the VM dialog, but game-CRC gate

The anti-VM is **version-specific** to the 25 MB `TeknoParrot64.dll` (Oct 26, in
`ElfLdr2/x64/`). Older Linux builds don't have it:

- `teknoparrot/TeknoParrot/TeknoParrot64.dll` (55 MB, Sept 25, copied to `olddll/`):
  **no VM dialog**, but **missing the `InitLinux` exports** BudgieLoader needs →
  game dies at "Starting Game" (exit 255). Not usable.
- **`teknoparrot/ElfLdr2/x64/TeknoParrot64.dll` (19 MB, Sept 25)**: **no VM dialog**
  **and** exports `InitLinux` / `InitLinuxAfterInit` / `InitLinuxAfterLib`. This is
  the DLL to use under wine. (Found by the Codex collaborator.) It is currently
  swapped into `ElfLdr2/x64/TeknoParrot64.dll`; the original 25 MB is backed up at
  `…/scratchpad/TeknoParrot64.dll.oct26-current`.

With the 19 MB DLL + `exec4` (cave+section headers): activation ok, **no VM
dialog**, dump loads (no crash) — then:

```
Unknown Linux game! (CRC: 0x016C4298)
SubSystem Detection: ...  Sound API: Unknown   Graphics API: Unknown
------------- Starting Game -------------      -> exit 255, 0 cave records
```

**New gate:** BudgieLoader identifies each game by a **custom CRC** and uses it to
select the per-game profile — graphics, sound, **and the HASP dongle emulation**.
Our modified dump isn't recognized (`Unknown Linux game`), so no dongle profile is
applied and the game can't run. This confirms the user's hunch: **the dongle data
is inside `TeknoParrot64.dll`, keyed by the game CRC.**

Chicken-and-egg: section headers are required to load (both DLLs fail the
section-less dump with `SHT_DYNSYM not found`), but adding them changes the CRC →
`Unknown`. The reported CRC `0x016C4298` is **not** a plain zlib crc32 (whole-file
crc32 of `exec4` is `0x9a3349c0`, of the original `0x46229ce6`); none of crc32 over
first-N-bytes / PT_LOAD segments matched, so it's a custom algorithm/region — likely
computed in `BudgieLoader_x64.exe` or `TeknoParrot64.dll`.

### Next steps (for Codex / whoever continues)
- **RE the game-CRC**: find the `"Unknown Linux game!"` string reference in
  `BudgieLoader_x64.exe`, back-trace the CRC function to learn its algorithm and
  which bytes/region it covers. If it excludes the ELF header + appended section
  table, then only the cave breaks recognition and the cave can be placed/handled
  to keep the CRC intact; if it covers the whole image, recognition needs the exact
  expected value.
- **Find Minecraft's expected CRC + dongle data in the 19 MB `TeknoParrot64.dll`**
  (game table keyed by CRC → HASP memory/keys/answers). The two decrypt answers we
  need may be derivable directly from the DLL without running the game at all.
- Clean, non-back-to-back reruns: kill wineserver and wait ~2 s between launches —
  rapid restarts gave inconsistent early exits this session.

### Refinement — the cave is CRC-neutral; section-header reconstruction is what breaks it

**Superseded by Codex's controlled comparison:** both the original dump and
`exec4` report `0x016c4298`. BudgieLoader reads this identifier directly from the
first four bytes of `.rela.dyn`, at file offset `0x8030`; it does not calculate a
CRC. A scratch mutation to `0x12345678` produced `libs_12345678`. Reconstruction
does not break recognition, and a CRC fixup is unnecessary. See
[the completed checksum investigation](minecraft-budgieloader-crc-codex.md)
for code locations, verification, and the remaining DLL-support question.

Per the user, the dongle cave passed BudgieLoader's game-CRC check for other g7
titles (see [dongle-recording.md](dongle-recording.md)). So the cave bytes (the
zero-padding body + the 3 call-site rewrites) are **outside** the CRC region. The
difference for Minecraft is that its dump is uniquely **section-less**
(`e_shoff=0, e_shnum=0, e_shentsize=64`) — other games' dumps already had section
headers, so they never needed reconstruction. Our reconstruction changes the ELF
header fields `e_shoff` (0x28), `e_shnum` (0x3c), `e_shstrndx` (0x3e) and appends a
table at EOF — and **that** is what flips the CRC to the unrecognized `0x016C4298`.

Implication: the fix is to add section headers **without** changing the bytes the
game-CRC covers. Promising approach — **CRC fixup**: if the CRC is a crc32-family
checksum over a known region, after setting `e_shoff`/`e_shnum`/`e_shstrndx` we can
restore the original CRC by tweaking 4 spare bytes inside the CRC'd region (e.g.
the `e_ident` padding at 0x09–0x0f, which is unused). crc32 is linear, so a 4-byte
patch can force any target value. Requires knowing:
  1. the exact **algorithm** (poly/init/reflect/xorout) — not plain zlib crc32
     (whole-file crc32 of exec4 = `0x9a3349c0`, not `0x016C4298`), and not crc32 of
     first-N-bytes or either PT_LOAD;
  2. the exact **region** it covers (whole file? mapped image? ELF header only?).

Both come from one place: the `"Unknown Linux game!"` string in
`BudgieLoader_x64.exe` — xref it and back-trace the CRC routine. (BudgieLoader is
packed: `.brr*` sections are empty on disk and filled at runtime, so dump its
memory while running, or breakpoint the routine, rather than static disasm.)

Alternative that avoids the CRC entirely: pull Minecraft's HASP memory + the two
decrypt answers straight out of the 19 MB `TeknoParrot64.dll` (dongle data is keyed
by this same game-CRC inside the DLL).

## Game-side analysis of the two decrypts (2026-11-05)

Captured the exact ciphertext inputs by instrumenting the native loader
(`g7_rt.c` `haspCrypt`, new env `G7_DONGLE_DUMPDIR` dumps the full input on a
miss). Run:
`G7_DONGLE_DUMPDIR=<dir> ./linuxloader64 -g '<minecraft dir>' -c mcd.ini`
→ files `decrypt_<sha256>_len{128,64}.in` (also in session scratchpad
`dongle_in/`). Both are high-entropy and 16-byte-aligned. This is consistent
with block-cipher ciphertext, but entropy and alignment alone do not identify
AES, its mode, or its key size.

Where they live and how they're used (all in the dump):
- The two blocks are embedded, contiguous, in `.data`: 64-byte at `0x16dedc0`,
  128-byte at `0x16dee00`. They are part of a **5-blob set** (`0x16dedc0`,
  `0x16dee00`, `0x16dee80`, `0x16df260`, `0x16df2a0`) passed by the DRM init at
  `0x430880` to the license routine **`0x4a7850`**.
- `0x4a7850` calls the HASP decrypt wrapper (`0x6532a0`) **in place**: first the
  128-byte blob (`len 0x80`) at `0x16dee00`, then the 64-byte blob (`len 0x40`) at
  `0x16dedc0`. The decrypted pointers are stored in globals `0x1ec6088` (128) and
  `0x1ec6090` (64), then consumed by the license logic (`0x4a6f20` and the global
  license state `0x17c69c0/0x17c69c4/0x1c78d98/0x1c78d9c`), which also does further
  dongle reads and a retry loop.

**Feasibility verdict:** the decrypted plaintext is **used as structured license
data**, not compared to a single constant, so it cannot be faked with a stand-in.
The correct plaintext requires the dongle's AES key, which is not in the dump and
is not recoverable by analysis (AES). So the real answers must come from a real
HASP dongle or TeknoParrot's HASP emulation — both currently blocked (no dongle;
the older Linux DLL rejects the identifier, while the October DLL stops at its
VM dialog before game support can be tested). AES key
recovery from known input alone is infeasible.

Captured inputs (hex, for whoever gets a key/dongle to compute the answers):
- 128B @0x16dee00 starts `7937348b a3964ea3 de7938eb 9359935f …`
- 64B  @0x16dedc0 starts `73b25110 043c7fbf eec67ed2 83e1c2ab …`
Full bytes in the `.in` files. Answer files go in the game's `dump/` dir named by
the input's sha256 (what `g7_rt.c` looks up), each exactly 128/64 bytes.

## Official support announcement checked by Codex (2026-11-05)

TeknoParrot's official news feed lists **Minecraft Dungeons Arcade & Gundam
OverBoost Emulation Support!**, published **2026-10-26 at 16:18 UTC**:

- [Official news page](https://teknoparrot.com/en/Home/Live).
- [Official news JSON](https://teknoparrot.com/Home/LatestNews).
- [Linked announcement](https://www.patreon.com/TeknoParrotTeam/posts/minecraft-arcade-173465973).

The JSON was retrieved directly and retained at
`/tmp/mcd-tp-official-news.json`. The linked Patreon post's body was unavailable
to the web tool; its detailed requirements and supported revision are unknown.

This answers whether TeknoParrot has announced support: **yes**. The September
Linux DLL predates the announcement, plausibly explaining its rejection of
`0x016c4298`. The October 26 build is the strongest existing candidate. Its
exact profile and supported revision remain unverified because the VM guard
stops initialization. The claim that none of the available builds carries the
profile is not established.

The captured inputs are useful, but do not by themselves establish AES-128,
absence of every recoverable key representation, or impossibility of all
alternative game-side implementations. Arbitrary plaintext remains unvalidated
because the game consumes the decrypted bytes. No valid outputs were recovered.


## RESOLVED 2026-11-05 — answers extracted via process memory on real Windows

The game runs on native `linuxloader` now. What worked, end to end:
1. The **original** `gamedump.exec` (unmodified) launches via TeknoParrotUi on a
   **bare-metal Windows** box (no Hyper-V/VBS, so the TeknoParrot64.dll anti-VM
   passes). Modified dumps (exec2/4/5) are rejected with a "crc error" (game
   filesystem-integrity check), so the cave approach was a dead end there.
2. With the real dongle emulation running, the two license blobs are **decrypted
   in place** at their `.data` addresses. As an admin SSH session, read them out
   of the running `BudgieLoader_x64.exe` with `ReadProcessMemory` (the ELF is
   mapped at its vaddr `0x400000`, non-PIE):
   - `0x16dee00`, 128 bytes  -> plaintext answer for input sha256 `454ce150…`
   - `0x16dedc0`,  64 bytes  -> plaintext answer for input sha256 `1d57e7b0…`
   Tool: `tools/dongle-recording/rpm.ps1` (enables SeDebugPrivilege, verifies the
   ELF magic at 0x400000, dumps the two blobs). No dump patching, no cave.
3. Install each plaintext as an answer file named by the **input** ciphertext's
   sha256, in the game's **`pm/g7/dump/`** folder (sibling of `minecraft`, NOT
   `minecraft/dump`), exactly 128/64 bytes. `g7_rt.c` looks them up there.
4. Native run: both decrypts log `-> answered` and the game boots (no more SIGFPE).

Answer plaintext (hex), for the record / redeploy:
- 128B: `fc0d02b72c248b5ceb59faa3c251c8b531db8d59973c077aafce8b266254ecdd13bd1c225a3e8b1dab9dcb5ffb3d86ac0c218489749bdbf84b0faee9c92453c0066d6e3e3bee3f7e1aecc6f0d955ac716535821240f5e9b4d018547f0d3954572a0de367216a5310b36f1b8fbb4d12f81f83b6e000a9ab6b504098dfaefad5d7`
- 64B:  `379c9d4db746a7c2d6561bd5be9fd525a101a294021497a235a094678b3b80aa7a78323ba83571c605cf1ff694e1ab6592c78263622e63e7f3c9e1a189ad532d`

Note: more per-file decrypts may appear during gameplay/level loads; capture any
new `-> no answer` the same way (read the in-place result from the Windows run)
and drop them in `pm/g7/dump/`.
