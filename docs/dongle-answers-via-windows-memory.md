# Getting HASP dongle answers by reading a running TeknoParrot game's memory

A reusable method for Raw Thrills **g7** games (and similar HASP titles) whose
dongle `hasp_encrypt`/`hasp_decrypt` answers we can't synthesize. It worked for
Minecraft Dungeons Arcade (see [minecraft-dungeons-arcade.md](minecraft-dungeons-arcade.md))
after the cave/BudgieLoader-under-wine routes were dead ends.

## When to use this (vs. the cave)

Use the **cave** ([dongle-recording.md](dongle-recording.md)) when you can run the
instrumented dump under BudgieLoader and capture its output. Use **this** method
when:

- the game only runs under the **real TeknoParrot on bare-metal Windows** (e.g.
  TeknoParrot64.dll has an anti-VM that misfires under wine), and/or
- TeknoParrot rejects a **modified** dump with a "crc error" (filesystem
  integrity check), so you can't insert a cave, and
- the dongle `decrypt` is done **in place** in the process's memory — so once the
  real dongle emulation has run, the plaintext is just sitting at a fixed address.

You need a **legal TeknoParrot** setup and a **physical** (non-VM) Windows PC.

## Why it works

These games embed AES-encrypted license blobs in `.data`. At startup the game
calls the dongle to `hasp_decrypt` them **in place** (the buffer passed in is the
same `.data` address; the plaintext overwrites the ciphertext). The ELF is a
non-PIE `EXEC`, so BudgieLoader maps it at its link address (`0x400000`). Result:
after startup, address `X` that held ciphertext now holds the exact plaintext the
dongle would return — i.e. the answer. We just read it.

## Step 1 — find the unanswered calls and their addresses (on Linux)

1. Run the game under the native `linuxloader64`. `g7_rt.c`'s `haspCrypt` logs
   each call: `decrypt len N sha256 <H> -> no answer`. Note each `(len, sha256)`.
2. Capture the raw **input** (ciphertext): the loader has the
   `G7_DONGLE_DUMPDIR` hook — on a miss it writes the full input buffer:
   ```sh
   G7_DONGLE_DUMPDIR=/some/dir ./linuxloader64 -g '<game dir>' -c game.ini
   ```
   → `decrypt_<sha256>_lenN.in`.
3. Locate where that ciphertext lives in the dump and confirm it's decrypted in
   place: `grep` the dump for the first 16 bytes of the input →
   file offset `O`, VMA `0x400000 + O`. Cross-check in a disassembler that the
   `hasp_decrypt` caller passes that VMA as the buffer (for MCD the license
   routine `0x4a7850` decrypts the blob at its `.data` VMA in place). The VMA is
   the address to read later.

## Step 2 — run the original dump under real TeknoParrot (bare-metal Windows)

- Physical Windows, **Hyper-V / VBS / Memory-Integrity OFF** (`HypervisorPresent`
  must be False, or the anti-VM fires even on real hardware — see
  [minecraft-record-on-windows.md](minecraft-record-on-windows.md)).
- Install the same TeknoParrot build, activate with the valid key.
- Launch the **unmodified** `gamedump.exec` via TeknoParrotUi and get it to the
  attract/menu so all startup decrypts have run. (Modified dumps fail the "crc"
  integrity check, so do not patch the file.)

## Step 3 — read the plaintext out of the running process

Admin + `SeDebugPrivilege` lets you `ReadProcessMemory` the game process
(`BudgieLoader_x64.exe`) even from another session (e.g. over SSH). Tool:
`tools/dongle-recording/mcd-read-answers.ps1` (or the generic
`read-process-mem.ps1`): it enables `SeDebugPrivilege`, finds the process,
verifies the ELF magic `7f454c46` at `0x400000`, and dumps the blob addresses.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File read-process-mem.ps1 `
   -Proc BudgieLoader_x64 -Reads "0x16dee00:128,0x16dedc0:64" -OutDir C:\out
```

Sanity check: the bytes read must **differ** from the captured ciphertext (if
they're identical, the decrypt hasn't happened yet — let the game run further).

Driving it remotely: enable OpenSSH Server on the box; from Linux use an
`ssh`/`scp` password helper (see `scratchpad/wssh.py` pattern — a pty that answers
the password prompt). File data over `scp` is binary-safe; only the prompt goes
through the pty.

## Step 4 — install the answers for the native loader

`g7_rt.c` looks an answer up by the **sha256 of the input** (the file name) in the
game's **`pm/g7/dump/`** folder (sibling of the game folder — `g7Root/g7/dump`,
NOT `<game>/dump`). It falls back to a unique file of matching size. So:

```
pm/g7/dump/<sha256 of the input ciphertext>   <- contents = the plaintext you read
```

Each file exactly `len` bytes. This is the same convention every g7 game uses.

## Step 5 — verify on Linux

```sh
./linuxloader64 -g '<game dir>' -c game.ini
```

The calls now log `-> answered` and the game boots past the point it used to fail.
More per-file decrypts can appear during gameplay/level loads; capture each new
`-> no answer` the same way (read the in-place result from the Windows run) and
drop it in `pm/g7/dump/`.

## Tooling summary

| file | role |
|------|------|
| `src/loader/rawthrills/g7/g7_rt.c` | `G7_DONGLE_DUMPDIR` dumps missing inputs; `haspCrypt` logs sha256 |
| `tools/dongle-recording/mcd-read-answers.ps1` | Windows RPM reader (SeDebugPrivilege + ELF-base check + dump addresses) |
| `tools/dongle-recording/mcd-reconstruct-sections.py` | rebuild section headers if you ever need BudgieLoader to load a section-less dump |
| `scratchpad wssh.py` pattern | SSH/scp to a Windows box answering the password prompt |

## Gotchas learned

- **Bare metal only.** Hyper-V/VBS set the CPUID hypervisor bit even on real
  hardware → TeknoParrot64.dll's anti-VM fires. Disable them.
- **Don't modify the dump** for this method — TeknoParrot's integrity check
  rejects it ("crc error"). The whole point is reading from the *unmodified*
  game's memory.
- **ELF base.** Non-PIE `EXEC` dumps map at `0x400000`; confirm by reading the ELF
  magic there before trusting other addresses.
- **Answers dir is `pm/g7/dump`**, keyed by **input** sha256 — not the game
  folder, not keyed by output.
- **In-place decrypt assumption.** Verify the `hasp_decrypt` caller decrypts into
  the same `.data` buffer; if a game copies the plaintext elsewhere, read that
  destination instead.
- Reading `/proc/pid/mem` (Linux) or `ReadProcessMemory` (Windows) does **not**
  set a tracer, so anti-debug that checks `TracerPid`/`IsDebuggerPresent` doesn't
  fire — unlike attaching a debugger.
