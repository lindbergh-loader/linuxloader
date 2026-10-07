# Recording the dongle's answers

[hasp-bypass.md](hasp-bypass.md) explains how the loader answers the HASP API.
Most of it can be synthesised — a login status, a cabinet type, a session-info
string. One part cannot: the **encryption**. Where a game's data files are
encrypted, only the real dongle can produce the transform, so the answers have
to be captured once from a setup where the dongle does answer, and replayed
afterwards.

That setup is TeknoParrot's ELF loader, **BudgieLoader** (`ElfLdr2/`), which
runs these same Linux game binaries under Wine with the dongle answered. The
recording is done by running the game there with a patched copy that logs every
dongle call, and keeping the input/output pairs.

## The idea

Redirect the game's call to its dongle decrypt wrapper into a small stub of our
own. The stub writes the call's arguments out, calls the real function, writes
the result out, and returns the real status. The game runs exactly as before
and never notices; stderr gains a transcript of every dongle transaction.

Four things are needed inside the binary:

1. **The call site** to patch — a `call` to the dongle encrypt/decrypt wrapper.
   Only the relative displacement changes, so the patch is 3–4 bytes.
2. **Free space** for the stub. The tail of the code segment is usually padded;
   Halo had about 2 KB spare at `0x1461094`.
3. **A way to write.** Rather than link anything, reuse the game's own `write`
   through its import table — for Halo, the slot at `0x1691cb0`.
4. **A single writer.** If the game forks — Raw Thrills' anti-debug guard
   does — the child inherits the same stdout and its records interleave with
   the parent's. Stub the guard in the recording copy, as the loader does at
   run time.

Patch a **copy**, never the original. The games hash their own executable as
part of the file-integrity check, and the loader maps the check back to the
untouched file (`RtPathAlias`, e.g. `game` → `game_ori`).

## The stub, concretely

Halo's is **`tools/dongle-recording/halo-cave.S`**. It wraps
`hasp_decrypt` at `0xb2e1aa` and writes through the game's own `write` import
at `0x1691cb0`:

```
    push  %r15 ; sub $0x10,%rsp
    mov   %edi,%r12d         ; handle
    mov   %rsi,%r13          ; buffer
    mov   %edx,%r14d         ; length
    mov   %r14d,(%rsp)

    write(2, "@@D@", 4)      ; record marker, so records can be found in the noise
    write(2, &length, 4)     ; length of this call
    write(2, buffer, length) ; the input  -- ciphertext

    call  0xb2e1aa           ; the real hasp_decrypt, in place
    mov   %eax,%r15d         ; keep its status

    write(2, buffer, length) ; the output -- plaintext

    mov   %r15d,%eax         ; return the real status untouched
    ret
```

Each `write` is an indirect call through the import slot, `call *0x1691cb0`.
A record on stderr is therefore:

```
"@@D@" | length (4 bytes, LE) | input (length bytes) | output (length bytes)
```

Keep the marker distinctive: the game writes plenty of its own noise to the
same stream.

## Building and injecting it

Assemble at the address it will live at — it is not position independent, and
`lea tag(%rip)` has to resolve:

```sh
as --64 halo-cave.S -o halo-cave.o
ld -Ttext=0x1461100 -e _start -o halo-cave.elf halo-cave.o
objcopy -O binary -j .text halo-cave.elf halo-cave.bin    # 160 bytes for Halo's
```

Then, on a **copy** of the game: write `halo-cave.bin` at the file offset of
`0x1461100` (here `0x1061100`, the segment maps at `0x400000` with no skew),
and repoint the call so it lands on the cave instead of the real function:

```
vaddr 0x6398e7   e8 <rel32>   0xb2e1aa  ->  0x1461100
```

Only the 4-byte displacement changes; the `e8` stays. To check a build, rebuild
the stub and compare it against the copy — the bytes should be identical, and
the call's target should resolve to the cave:

```sh
cmp <(objcopy -O binary -j .text halo-cave.elf /dev/stdout) \
    <(dd if=game_dongle_log bs=1 skip=$((0x1061100)) count=160 2>/dev/null)
```

## Running it under BudgieLoader

```sh
export WINEPREFIX=/path/to/wineprefix WINEDEBUG=-all
cd /path/to/teknoparrot
wine ElfLdr2/x64/BudgieLoader_x64.exe 'C:\game\...\game_dongle_log' \
     > dongle.bin 2>&1 < /dev/null &
sleep 60
wineserver -k
```

Redirect both streams into one binary file and let it run long enough for the
game to reach the calls you are after — the startup secrets come early, but
per-file decryptions only happen as the data is loaded. Split the records out
of `dongle.bin` afterwards on the marker.

## Turning the log into answer files

`rtDongle.c` looks an answer up by the **first `answerKeySize` bytes of the
input buffer, hex-encoded**, in the folder named by `haspAnswers`. The file
contains the answer and must be exactly as long as the call's buffer.

- default key size is 16 bytes (32 hex characters); set `haspAnswerKeySize` to
  override — Terminator Salvation uses 32
- the dongle's memory goes in the same folder as `hhl_mem.dmp`, 128 bytes

So for each record: take the input, hex the first *N* bytes for the name, write
the output as the contents. `tools/dongle-recording/split-records.py` does
that, carving the records out of the capture on the marker:

```sh
# Halo: 32-byte key, no status field between input and output
split-records.py tpdongle.bin pm/g7/dump --key-size 32

# The Walking Dead and Big Buck HD Wild: 16-byte key, 4-byte status
split-records.py donglelog.bin pm/hasp --status-bytes 4
```

`--status-bytes` has to match the stub. Halo's writes input then output;
`twd-cave.S` and `bbhd-cave.S` write the call's 4-byte status between them.
Getting it wrong yields files with the right *names* and shifted contents,
which then fail silently at replay — so check a known answer after splitting.

The Walking Dead's folder is four files, because only a handful of secrets are
fetched at startup:

```
hasp/060248dc2e6d7a9ab28c56d9e075cce5   256 bytes
hasp/9003615195afab6405f7554eacedcb38    32
hasp/e32652bf0d238b2201d09a4fb70d4806    16
hasp/f3348bcd54b4ed02b1a40225b8cff7d7   512
```

Big Buck World's has around 3400, because it decrypts per data file. How many
you need depends on what the game encrypts, not on the method.

## Worked example: Big Buck HD Wild (32-bit, g5)

Same shape as The Walking Dead: four startup calls, a handful of answer files.
The differences are worth recording, because each one cost a run.

**The call sites.** `DongleEncrypt` (0x8266cb0) and `DongleDecrypt` (0x8266c40)
each call the HASP layer exactly once, so those two calls are the choke point:

```
8266cfc:  call 853004c    ; DongleEncrypt -> hasp_encrypt
8266c8f:  call 8530138    ; DongleDecrypt -> hasp_decrypt
```

**The free space.** This binary's `.text` has no 152-byte free run (the largest
is 63 bytes), and its data segment is `RW`, not executable, so the stub cannot
live there the way TWD's does. The first `LOAD` segment is `R E` and spans the
whole file up to `.data`, so `.rodata` is executable too. An unreferenced
542-byte zero run at `0x8afb422` (file `0xab3422`) works: check it is not
pointed at by scanning the data segments for pointers into the range.

**The trampoline offset.** The stub's encrypt trampoline is 12 bytes
(`push imm32; push imm32; jmp rel8`), so the decrypt trampoline is at
`CAVE + 0xc`, not `CAVE + 0xa`. Pointing the decrypt call at `CAVE + 0xa` lands
mid-instruction and the game dies with `STATUS_ACCESS_VIOLATION at eip=0` in
`DongleDecrypt` — a null call, which looks like a loader problem and is not.

**The fork.** The anti-debug guard (0x84338e6) forks; the child inherits the
capture's stdout and its records interleave with the parent's, corrupting both.
In the recording copy, overwrite its entry with `mov eax,1; ret`
(`b8 01 00 00 00 c3`) — the same stub the loader installs at run time.

The result is four records, parsed with `--key-size 16 --status-bytes 4`:

```
hasp/0f8b5fdb2accd0ddea416a7f1d4b0c48    256 bytes
hasp/18ee0ba087dff2926434aad7f52a9ba5     16
hasp/3afb28b9b837b0f9d3c96373602d0d9e    512
hasp/6913e4d8580002ea242fb767e4b1e48c     32
```

The 32-byte and 16-byte encrypts are the efilemaps' AES key and IV; the 256- and
512-byte decrypts are the file-key secrets. With them in place the loader
replays all four, the def files decrypt, and the game reaches the coin page.

## What is in the tree

Instrumented copies are kept beside the originals, same size, differing only in
the patched call and the injected stub:

| file | patched call | stub | what it logs |
|---|---|---|---|
| `The Walking Dead/pm/game_dongle_log` | 0x18f24f, 0x18f3d2 | 0x9dd900, 0x9dd918 | dongle encrypt/decrypt |
| `The Walking Dead/pm/game_keydump` | 0x9a310 | 0x9dd800, 0x9dd860 | the derived keys |
| `The Walking Dead/pm/game_aim_log` | 0x3d1768 | 0x9dda00 | gun aim, same technique |
| `Big Buck HD Wild/game_dongle_log` | 0x21ecfc, 0x21ec8f | 0xab3422 (vaddr 0x8afb422) | dongle encrypt/decrypt |
| `Halo .../pm/g7/halo/game_dongle_log` | 0x2398e8 (vaddr 0x6398e8) | 0x1061100 (vaddr 0x1461100) | dongle decrypt |

Every stub source is in `tools/dongle-recording/`: `halo-cave.S`,
`twd-cave.S` (recovered by disassembling `game_dongle_log`) and `bbhd-cave.S`
(the same generic body retargeted at BBHDW's addresses). The Walking Dead's `game_keydump`
and `game_aim_log` sources were not kept either, so they were recovered the
same way and are `twd-keydump.S` and `twd-aim.S`; each reassembles to the bytes
in its copy exactly.

Those two are a different shape from the wrappers above, worth knowing when
there is no convenient call to repoint: a **detour**. The patch overwrites
instructions at the site with a jump to the cave, and the cave logs,
re-executes the instructions it displaced, then jumps back. `twd-keydump.S`
displaces 8 bytes (`mov %ebx,(%esp); call ...`) and pads with three nops;
`twd-aim.S` displaces 6 and pads with one. Nothing about it is dongle
specific — `twd-aim.S` points the same technique at the gun-aim call.

(The offsets above are file offsets; The Walking Dead's scratch region is
around `0x9dd800`–`0x9dda40`.)

Halo's captured answers live in `pm/g7/dump/`, named by the first 32 bytes of
the input:

```
93b321f9dbfb5ca174d6dc8974be46623100d97525e2441287c72a2e9884dad4   128 bytes
9e6dac279ad0a5d8bcdd14fd81b4067767bafc26dfe3b68e11431b1bc1e355ce    64 bytes
```

## Reading a recording back

Worth checking a capture before trusting it: decrypt a file the game reads
early and see whether it comes out as something sensible. Halo's `gameconf.txt`
decrypts to plain text (`bullet_initial_z`, `player_health`, ...), which
confirms both the capture and the replay in one step — and rules the dongle out
when something else is wrong.

The same technique works for anything else worth watching: `game_aim_log` is
the identical patch applied to the gun-position call rather than the dongle.
