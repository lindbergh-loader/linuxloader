# Recording Minecraft Dungeons Arcade's dongle answers on real Windows

Why Windows: TeknoParrot's `TeknoParrot64.dll` has a VM check that misfires under
wine. On genuine bare-metal Windows it passes on its own, so the real HASP
emulation runs and the instrumented dump can record the two decrypt answers we
need. This is also the test of whether the current DLL build even carries this
game's dongle profile — if it does, this works; if it prints
`Unknown Linux game!`, that build doesn't support the game and no amount of setup
helps. Background: [minecraft-dungeons-arcade.md](minecraft-dungeons-arcade.md).

Goal: capture the plaintext for the two decrypts and drop them into the game's
`dump/` folder so the native `linuxloader64` on Linux/Batocera can answer them.

## 0. The machine must not look like a VM

- Use a **physical Windows PC** (not VMware/VirtualBox/Hyper-V/cloud).
- Turn off features that set the CPU "hypervisor present" bit even on bare metal,
  or the same dialog can appear:
  - Windows Features → remove **Hyper-V** and **Windows Hypervisor Platform**.
  - Settings → Core Isolation → **Memory Integrity** OFF.
  - Admin cmd: `bcdedit /set hypervisorlaunchtype off` then reboot.
- Verify after reboot: `systeminfo` "Hyper-V Requirements" should say a hypervisor
  was **not** detected, or Task Manager → Performance → CPU shows
  "Virtualization: Enabled" but **no** "Virtual machine: Yes".

## 1. Install TeknoParrot and activate

- Install the **same/latest TeknoParrot build** you use under wine (match
  `BudgieLoader_x64.exe` + `TeknoParrot64.dll`; the current one here is dated
  2026-10-26). Install the .NET runtime TeknoParrotUi needs.
- Launch TeknoParrotUi once and enter your **valid Patreon key** so activation
  succeeds (same online `activation.php` check, fine from Windows).

## 2. Copy the game + the instrumented dump

Copy the whole `Minecraft Dungeons Arcade` game folder to the Windows box,
including these prepared dumps from the Linux side
(`…/pm/g7/minecraft/`):

| file | what | crc32 |
|------|------|-------|
| `gamedump.exec4` | **use this** — dongle cave + reconstructed section headers | `0x9a3349c0` |
| `gamedump.exec2` | fallback — cave only (section-less) | `0x454e6ad3` |
| `gamedump.exec`  | pristine original (reference) | `0x46229ce6` |

Why `exec4`: BudgieLoader resolves imports through ELF **section headers**; this
dump is section-less, so without the reconstructed headers it crashes at startup
(`RIP=0`). That requirement is BudgieLoader's own logic, not a wine artifact, so
it applies on Windows too. `exec4` = the cave (records the dongle calls) **plus**
the section headers (so it loads). Keep `exec2` only as a fallback in case this
Windows build happens to parse `PT_DYNAMIC` directly.

## 3. Point the profile at exec4 (or run BudgieLoader directly)

In the game's TeknoParrot profile (`UserProfiles\Minecraft*.xml`) set
`GamePath` / `ExecutableName` to `gamedump.exec4`. Or skip the UI and run the
loader directly from a console — that's the most reliable way to capture output.

## 4. Capture the cave's records

The cave writes each dongle call as a binary record to **fd 2 (stderr)**:
`marker(4) | length(4 LE) | input(length) | status(4) | output(length)`, markers
`@@E@` (encrypt) and `@@D@` (decrypt). Run BudgieLoader from a console and
redirect stderr to a file:

```bat
cd \path\to\teknoparrot
ElfLdr2\x64\BudgieLoader_x64.exe "C:\...\pm\g7\minecraft\gamedump.exec4" 2> records.bin
```

Let it run through startup until it reaches the game (the two decrypts happen
during init). Success looks like: **no** "cannot run under a virtual machine"
dialog, **no** `Unknown Linux game!`, it reaches `Starting Game` and keeps going;
`records.bin` grows past a few KB.

If you see the VM dialog on real hardware → a hypervisor is still enabled (step 0).
If you see `Unknown Linux game!` → this DLL build lacks the game's profile; stop,
that's the "no build supports it" outcome.

## 5. Extract the two answers

Copy `records.bin` back to the Linux checkout and split it (Python works on
Windows too if you prefer):

```sh
python3 tools/dongle-recording/split-records.py records.bin out \
        --status-bytes 4 --marker @@D@ --marker @@E@
```

`--status-bytes 4` is required (this cave writes a 4-byte status between input and
output). You want the two **decrypt** records whose inputs match ours:

- 128-byte input, sha256 `454ce150c44400c6814ee0acc3412054b0dd95a5e3bfbbed61aca345d55e50de`
- 64-byte input,  sha256 `1d57e7b041bc3c5fee583915833137a058b09f346ba20127a0d174ce20f1500f`

The **output** half of each record is the real plaintext answer.

## 6. Install the answers for the native loader

`g7_rt.c` looks an answer up by the **sha256 of the input**, as the file name, in
the game's `dump/` folder (it falls back to a unique file of matching size). So
save each decrypt's output under its input's sha256:

```
<minecraft game dir>/dump/454ce150c44400c6814ee0acc3412054b0dd95a5e3bfbbed61aca345d55e50de   (128 bytes)
<minecraft game dir>/dump/1d57e7b041bc3c5fee583915833137a058b09f346ba20127a0d174ce20f1500f   (64 bytes)
```

(The dump folder is the `answers` path the loader logs: `.../pm/g7/dump`.)
Each file must be exactly 128 / 64 bytes. Capture any other `@@E@`/`@@D@` records
too and install them the same way — the game may ask more decrypts later (per-file
data), and the same sha256-named scheme covers them.

## 7. Verify on Linux

Re-run the native loader (no `G7_DONGLE_DUMPDIR` needed now):

```sh
cd build-linux
LINUXLOADER_CONFIG=mcd.ini ./linuxloader64 -g '<minecraft game dir>' -c mcd.ini
```

The two decrypts should now log `-> answered` instead of `-> no answer`, and the
game should get past the point where it previously SIGFPE'd. Remaining dongle
reads/answers may surface more records to capture — repeat steps 4–6 until it runs.

---

Notes for a Claude Code session on the Windows box:
- Run BudgieLoader from **native Windows** (PowerShell/cmd), not WSL (WSL is a VM
  and adds Hyper-V).
- You do not need to patch anything — on real hardware the checks pass as designed.
- If `Unknown Linux game!` appears, report it back; it means this TeknoParrot build
  does not carry this game's dongle profile, which is the real open question.
