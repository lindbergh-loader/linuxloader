# IGS E2200: HASP analysis and runtime recovery

Goal: get the IGS E2200 games' executables into a state linuxloader can run.

West Trip follow-up: [bundled daemon recovery and bounded key scans](igs-e2200-west-trip-envelope.md).
The daemon's fifteen protected regions are now readable for analysis;
44,212,602 direct retained-key candidates produced no match for the game's
startup state. West Trip's game envelope remains unresolved.
The follow-up now traces the daemon through `/tmp/.aksusb` and `aksusbd`
to USB vendor requests `0x29/0xa9`. Ten isolated synthetic cases validate
the phase-to-device-operation mapping and packet contents; no real dongle
reply has been recovered.

[Retained-state disk search](igs-e2200-retained-license-search.md): eight
supplied E2200 disks, 27,445 regular files, 5,624 archive members, and all
eight writable partitions were searched. No usable licensing state or core
dump was found. West Trip's recovered crash logs contain video errors and
memory statistics, not process memory.

The disks, the encrypted root and the boot FPGA cipher are already solved (see
[igs-e2200.md](igs-e2200.md)). Every executable below is the exact plaintext
the cabinet's FPGA produces at boot. Remaining work includes Sentinel HASP
protection and cabinet I/O. The runtime asset cipher and two board
firmwares have now been recovered independently of those remaining checks.

Initial handoff written 2026-11-24 (Claude), continued below by Codex.
The original Fish Hunter investigation reached `main` in a bounded debugger
probe. Subsequent loader work now supplies a descriptor and HASP replacements
using firmware-derived packet/checksum values; that separate implementation
does not provide the missing envelope responses for West Trip.

## Current handoff

Percussion Master 3 is an additional E2200 comparison target. Its original
CHD has now been inspected: the executable was recovered through the FPGA
transform and has accessible startup without an observed HASP envelope.
It differs from the prepared package. Original embedded firmware remains
unresolved. See [the original PM3 comparison](igs-e2200-pm3-original.md).
PM3 is excluded from the six-dump recovery counts below.

Updated 2026-11-24. Here, asset ROMs are the graphics/animation containers
ending in `.rom`; board ROMs are the IGS036 ARM firmware. Recovering one
does not imply that the other, or the protected x86 executable, is recovered.

| Game | Asset files structurally validated | Board firmware | Executable/startup |
| --- | --- | --- | --- |
| Fish Hunter SKL 2 | 81/81 | Recovered; embedded hash matched | No envelope; debugger reached `main` |
| Soul of Gun | 182/182 | External image unresolved | HASP envelope unresolved |
| Full Attack | 151/151 | No external image found | HASP envelope unresolved |
| Ocean King | 170/170 | No external image found | HASP envelope unresolved |
| Poseidon | 169/169 | No external image found | HASP envelope unresolved |
| West Trip | 305/306 | Recovered; embedded hash matched | HASP envelope unresolved |

Across all six games, **1,058/1,059 asset files** pass the structural
checks. West Trip's remaining sound appears truncated. Full DDS image
decodes were checked independently for every game; one Fish Hunter sound
was also loaded with Python's WAV reader. These checks do not constitute
gameplay validation or a byte-for-byte comparison with cabinet output.

The recovered asset masks reproduce the FPGA data transform. They are
**not recovered HASP AES secrets or original FAFC user keys**. The firmware
recovery files retain separate 256-byte IGS036 keys and equivalent FPGA
masks, verified by regenerating the recovered ROMs and matching their
embedded hashes.

Work products:

| File | Purpose |
| --- | --- |
| [igs-e2200-assets.py](../tools/igs-e2200-assets.py) | Discover asset masks, inspect layouts, and decode individual encrypted files. |
| [igs-e2200-envelope.py](../tools/igs-e2200-envelope.py) | Inspect envelope metadata and export encrypted state inputs; does not unpack code. |
| [Envelope inventory](igs-e2200-envelope-inventory.json) | Original executable hashes, all protected-region ranges, entry points, state hashes, and selectors for five games. |
| [igs036-firmware.py](../tools/igs036-firmware.py) | Recover IGS036 firmware; extended with `--body` and `--no-fpga` input modes. |
| [fhskl2-startup-probe.gdb](../tools/dongle-recording/fhskl2-startup-probe.gdb) | Stop Fish Hunter at `main`, report registers/backtrace, then terminate. |
| [Fish Hunter firmware parameters](fish-hunter-skl2-firmware-recovery.json) | Upload location, input/reference hashes, saved key/mask, ROM hash and embedded-hash result. |
| [Other-game recovery index](igs-e2200-other-games-recovery.json) | Five game masks, special cases, report paths, DDS checks, and West Trip firmware parameters. |
| [Fish Hunter asset report](fish-hunter-skl2-asset-validation.json) | Per-file hashes and structural validation results. |

Recovered board firmware artifacts are local, outside git:

- `games/Fish Hunter SKL 2/game/roms/fhskl2_eprom.bin`
- `games/West Trip/game/roms/westtrip_eprom.bin`

The assets were inspected in place, with sample decodes written under
`/tmp`; this work did not replace the encrypted game assets or unpack
the five HASP envelopes. All repository changes remain uncommitted.
The sections below give exact addresses, reproduction commands, evidence
links, failed approaches, and the next investigation steps.

## The files

All under `games/<Name>/` (not in git; rebuild one with
`tools/igs-e2200-extract.py games/e2200_<x>.chd "games/<Name>" <work dir>`):

- `game/<exe>`: the executable;
- `game/lib/`: the libraries a desktop lacks;
- `boot/rootfs/`: the cabinet's whole decrypted root, with its HASP
  runtime: `/usr/sbin/aksusbd`, `winehasp`, `hasplmd`, and
  `etc/udev/rules.d/49-hasp.rules`;
- `boot/rootfs/etc/X11/IGS`: the start script. It starts `aksusbd`, then
  `winehasp`, then `hasplmd -s`, then runs `./<exe>` from the game directory,
  with no arguments.

| Game | Executable | Bytes | md5 (12) | Entry | Kind |
| --- | --- | --- | --- | --- | --- |
| Soul of Gun | `game/SoulOfGun` | 10359376 | 3dd7a9c88336 | 0x88b273a (file 0x86a73a) | envelope |
| Fish Hunter SKL 2 | `game/FHSKLv2-8p` | 3403008 | 76e8343b23c4 | 0x80533b0 (file 0xb3b0) | **API only, no envelope** |
| Full Attack | `game/OceanKing` | 9836432 | 7fbc67f5d7fa | 0x88327ea (file 0x7ea7ea) | envelope |
| Ocean King | `game/OceanKing` | 10225552 | e14d95cdc20a | 0x88927ea (file 0x8497ea) | envelope |
| Poseidon | `game/OceanKing` | 10258320 | 00e515692204 | 0x889a7ea (file 0x8517ea) | envelope |
| West Trip | `game/WestTrip` | 14604176 | 44adaa3e89d3 | 0x8cbe7ea (file 0xc767ea) | envelope |

All six are i386 ELF EXEC files, interpreter `/lib/ld-linux.so.2`,
dynamically linked against SDL 1.2, SDL_mixer, GL/GLU, Xxf86vm, X11, zlib,
ncurses 5, pci 3, libstdc++ and libgcc_s.

## Fish Hunter SKL 2: no envelope, the easier one

- Plain glibc `_start` at the entry point, followed by `__libc_start_main`
  with `main` = 0x8053830.
- Section headers are intact (30 sections: `.text` 0x080533b0 size
  0x23152c, `.rodata` 0x08284900, `.data` 0x082d4c80, `.bss` 0x083807c0).
- Readable strings.
- `.dynsym` holds only imports. The game's own symbols are stripped.

It statically links the Sentinel HASP API (`hasp_*` strings, the
`<haspformat ...>` XML queries, `hasp_vtc_d`). It wraps the API in a C++
singleton, `THaspKeyPro` (RTTI `11THaspKeyPro`,
`11FrSingletonI11THaspKeyProE`).

The 192 KiB high-entropy patch at file 0x290000–0x2c0000 is in `.data`.
The later firmware recovery below identifies it as part of the embedded
576 KiB board upload image, not HASP library data.

This is the Raw Thrills situation. linuxloader already handles it by
replacing the HASP API inside the process; see [hasp-bypass.md](hasp-bypass.md)
and `src/loader/rawthrills/rtDongle.c`. Steps:

1. Find `hasp_login`, `hasp_logout`, `hasp_read`, `hasp_write`,
   `hasp_encrypt`, `hasp_decrypt` and `hasp_get_sessioninfo` in the
   stripped binary. Use the XML strings and the API's status codes, or match
   against a known libhasp build.
2. Find what `THaspKeyPro` asks for: the feature id, `hasp_read` offsets
   and lengths, and any encrypt/decrypt round-trips. Those determine the
   answers.
3. If it only checks login and memory contents, synthesised answers may be
   enough. If it decrypts data with the dongle, real answers are needed:
   recorded from a cabinet, or derived as in
   [dongle-recording.md](dongle-recording.md).

### Follow-up: Fish Hunter static trace (Codex, 2026-11-24)

Target SHA-256: `e3e69d2539b70d8101be590e41bca4e06cdf6de5d32936f255399e1d50fb67f1`.
REA/rizin evidence is saved in `fish-hunter-skl2-hasp-evidence.json`.
These are static observations, not a successful launch or dongle emulation.

- The `THaspKeyPro` vtable is at `0x08287388`; its worker is
  `0x0806a960`. Constructors `0x0806ab50` and `0x0806ac10` call the
  three-argument login entry `0x08214880` with feature **2**, vendor-code
  pointer `0x08364d40`, and an output handle at object+`0x14`.
- Destructor paths call the one-argument logout entry `0x08214820`.
- The five-argument memory-read entry is `0x08213750`; the corresponding
  write-shaped entry is `0x082136a0`. The worker's fixed reads use file
  **`0xfff5`**, not Raw Thrills' `0xfff2`.
- Two buffer crypto entries are `0x08214700` and `0x08214780`. They dispatch
  with respective direction flags 1 and 0 to the legacy backend
  `0x0821e5a0`. Their public encrypt/decrypt names have **not yet been
  independently verified**; keep the addresses and direction flags explicit.
- Object state at +`0x10` is 1 when idle, 2 when a request is pending,
  and 3 when complete; the API result is stored at +`0x24`.
- Wrapper `0x0806a8a0(this, destination, offset, length)` selects mode 13.
  The worker reads **40 bytes from file 0xfff5, offset 0**, into object+`0x28`,
  transforms all 40 through `0x08214700`, then copies the requested slice
  to the destination. Read or crypto errors skip the copy. A login-only
  replacement therefore does not reproduce this path's behavior.
- Wrapper `0x0806a900(this, destination, offset, length)` selects mode 1,
  which reads directly without that additional 40-byte transform.

Observed callers of the mode-13 wrapper:

| Call instruction | Requested offset | Length |
| --- | --- | --- |
| `0x08066c5d` | `0x18` | 4 |
| `0x08066eea` | `0x14` | 4 |
| `0x08066fca` | `0x10` | 4 |
| `0x0806d0d8` | `0x18` | 4 |
| `0x0806d364` | `0x24` | 4 |
| `0x0806d4d4` | 0 | 16 |
| `0x0806da8c` | `0x20` | 4 |
| `0x0806dab2` | `0x1c` | 4 |

At `0x0807eff3`, the direct wrapper reads one byte from offset
`0x28 + 5 * index + subindex`; the result is accepted only below 6,
otherwise it is reduced to zero (`0x0807f055`). The semantic names and
valid values of the transformed 40-byte fields remain to be established.

Next: verify the crypto entry names against their internal protocol, trace
the consumers of the 16-byte and four-byte slices, then instrument a bounded
launch. No game descriptor or loader patch has been added. A host `ldd`
attempt exited 159 without a dependency listing; it does not establish a
game-code failure.

Tooling mismatch found during this trace: Codex's REA registration pins
6.8.0, which rejects the installed Ghidra 12.2. Claude Code's registration
pins 6.10.1 and sets `GHIDRA_INSTALL_DIR=/opt/ghidra`. The Ghidra open failed
before analysis; the observations above use REA/rizin and a local objdump
bulk disassembly.

### Follow-up: consumers and startup (Codex, 2026-11-24)

Same target SHA-256 as above. Additional REA observations are retained in
[fish-hunter-skl2-hasp-followup-evidence.json](fish-hunter-skl2-hasp-followup-evidence.json).
Addresses and data flow below are static observations; the startup probe
only verifies arrival at `main`.

**The 16-byte slice becomes an FPGA AES user key.** On a successful
mode-13 read, `0x0806d592` copies bytes 0–15 to `0x0848b400`.
`0x0806d5b3` calls `0x080673d0` with that address; this copies the bytes
to the FAFC key buffer at `0x0848bcc9` and marks it initialized.
The null-pointer branch substitutes the literal `xxxxxxxxxxxxxxxx`, but
this startup path passes a non-null pointer even if the read failed.
The fallback string therefore does not establish the real dongle key.

The next three calls are significant:

1. `0x08068080` collects PCI hardware information into `0x0848c094`.
   It matches four class/vendor pairs from the table at `0x08364d20`
   and appends matching device IDs in PCI enumeration order, with a
   zero-fill path when fewer devices match. The table's raw pairs are
   `(0x0000, 0x11aa)`, `(0x0200, 0x10ec)`, `(0x0300, 0x10de)`, and
   `(0x0600, 0x8086)`.
2. `0x08067430`, identified by its diagnostic string as
   `FAFC_iCombineHWInfoKey`, **overwrites** key bytes 0–7 with those eight
   hardware bytes. Bytes 8–15 retain the transformed dongle contents.
   This is a copy, not XOR or a hash.
3. `0x08067fb0` opens `/dev/pccard0` and initializes the FPGA AES wrapper.
   It calls `FPGA_AES_iSetEncryptKey` (`0x08096d96`) and
   `FPGA_AES_iSetDecryptKey` (`0x08096cef`) with key length 128 and output
   structures at `0x0848bf38` and `0x0848bf4c`. Both setters merely copy
   16 user-key bytes after validating their arguments; they do not expand
   an AES schedule in software. The encrypt routine `0x08096e3d` passes
   the key and buffer descriptors to ioctl **`0xc0106610`** at
   `0x08096fb7`.

Consequently, replacing the HASP calls still leaves a runtime FPGA crypto
interface to implement. The executable's offline boot decryption does
not implement this interface. The subsequent asset recovery below verifies
that the asset cipher uses the boot cipher's permutation with a different
mask. For this setup
path, only the last eight bytes of the dongle-derived key survive hardware
combination; that does not establish that the first eight are unused
elsewhere.

**The four-byte slices have several distinct consumers.**

| Transformed offset | Observed use |
| --- | --- |
| `0x10` | Stored at `0x08488f18`; initializes the one-word XOR context at `0x0848b840` through `0x08069bd0` (`0x08067145`). |
| `0x14` | Stored at `0x0848b3b0`; initializes the one-word XOR context at `0x0848b420` (`0x0806716f`). |
| `0x18` | Stored at `0x0848b3c0` on success (`0x08066d1b`); used in additive packet/checksum state, for example at `0x08065158`. Its additional startup use remains to be traced. |
| `0x1c`, `0x20`, `0x24` | Checked in order against three board-response words at `0x0848b410`, `0x0848b414`, and `0x0848b418`. |

`0x08069bd0` builds a context containing a word count and the supplied
words. Its companion `0x08069c30` XORs input words against the context,
cycling through its words. Thus the `0x10` and `0x14` consumers are
one-word XOR masks, not AES key schedules.

The board-response words are populated by the packet handler at
`0x08065428`–`0x08065467`. The startup loop selects dongle offsets
`0x1c`, `0x20`, and `0x24` (`0x0806da96`, `0x0806da70`, and
`0x0806d348`), waits for each read, and compares it at `0x0806d419`.
It starts a remaining-match count at three and decrements it only for a
successful API result and equal words. Any unmatched field ends in the
infinite loop at **`0x0806d438`**. API errors also leave the corresponding
match unsatisfied. A replacement needs coherent board responses and
dongle contents; returning success alone will not pass this gate.

**Crypto direction is now mapped to legacy command numbers, but API names
remain unverified.** The backend `0x0821cd20` turns its nonzero direction
argument into command word `0x0140` and zero into `0x013f`, stores that
word at request+`0x18`, and dispatches through `0x082633b0`.
The previously identified public entries therefore map as follows:

| Public entry | Legacy direction | Command |
| --- | --- | --- |
| `0x08214700` (used for the 40-byte read) | 1 | `0x0140` |
| `0x08214780` | 0 | `0x013f` |

These command numbers establish the internal distinction, but neither
number alone independently establishes the public encrypt/decrypt name.
The 40-byte backend also has length-dependent processing; do not substitute
a bare 16-byte transform for the full API call without checking it.

**Bounded startup succeeds.** The earlier exit 159 is consistent with the
sandbox blocking i386 execution: the same linker check outside the sandbox
reports missing libraries normally. With `game/lib` supplied, the next
missing dependency was `libtinfo.so.5`, which is present in the decrypted
cabinet root as `lib/libtinfo.so.5.6` but absent from `game/lib`.
Copying just that library into a temporary search directory allows GDB to
stop at `main` (`0x08053830`), with `__libc_start_main` and `_start`
(`0x080533d1` return address) in the backtrace. No HASP request or board
interaction was exercised by this probe. The verified probe's output is
saved in [fish-hunter-skl2-startup-probe.txt](fish-hunter-skl2-startup-probe.txt).

Reproduce from the repository root (the i386 process/debugger may require
execution outside the sandbox):

```sh
mkdir -p /tmp/igs-e2200-fh-probe/lib
cp 'games/Fish Hunter SKL 2/boot/rootfs/lib/libtinfo.so.5.6' \
  /tmp/igs-e2200-fh-probe/lib/libtinfo.so.5
timeout 15 env \
  LD_LIBRARY_PATH="/tmp/igs-e2200-fh-probe/lib:$PWD/games/Fish Hunter SKL 2/game/lib:/usr/lib32" \
  gdb -q -nx -batch \
  -x tools/dongle-recording/fhskl2-startup-probe.gdb \
  --args "$PWD/games/Fish Hunter SKL 2/game/FHSKLv2-8p"
```

Current next steps: decode the board packet commands and required response
values; trace the remaining `0x18` consumer and runtime FPGA decrypt ioctl;
then instrument HASP requests beyond `main` in an isolated game directory.
Resolve the full 40-byte transform with real answers or further evidence
before adding a production dongle replacement. The five enveloped games
below have not been advanced by this Fish Hunter follow-up.

### Follow-up: asset cipher recovered without the user key

The runtime asset transform is now reproducible. Its **effective mask** is:

```text
ca0569fc51aef734ed0a08af338d26ef
```

This is not the 16-byte FAFC user key or the HASP AES key. It is sufficient
to decode the shipped Fish Hunter assets:

```text
plain_block = PERM(cipher_block XOR mask)
```

`PERM` is the same 128-bit permutation used by
`tools/igs-e2200-extract.py`. The frequently repeated aligned block in
`action/01_GoldNemo.rom` suggested the zero-block mask. Independently,
its known first 16 plaintext bytes, `ACT_ZL3D\0\0\0\0TGA_`, recover
exactly the same mask by inverse permutation and XOR. Validation then used
the rest of the files rather than assuming that all repeated blocks were
zero.

[igs-e2200-assets.py](../tools/igs-e2200-assets.py) implements inspection
and decoding. Its saved [validation report](fish-hunter-skl2-asset-validation.json)
records input SHA-256 hashes and passes **81/81 files**:

- All **26 action ROMs** have the expected `ACT_ZL3D` signature and
  bounded chunk sequences ending in `FILE_END`, with 0–15 padding bytes.
  The walker checks **26,437 chunk headers** and the metadata of **4,387
  textures**: 1,889 raw RGBA textures and 2,498 DXT5 DDS textures.
- All **55 sound files** have valid RIFF/WAVE chunk layouts and consistent
  PCM format fields and lengths.
- A full decode of `06_sailfish.rom` supplied a DDS texture that Pillow
  independently loaded as a 56×196 RGBA image. A full decode of
  `37_Alarm.wav` loaded with Python's `wave` module as mono 8-bit PCM,
  22,050 Hz, 52,992 frames.

The structural inspection reads every chunk header and relevant metadata;
it does not independently validate every pixel or sound sample. Full sample
decodes provide a separate check of the transform.

```sh
python3 tools/igs-e2200-assets.py inspect 'games/Fish Hunter SKL 2/game' \
  --report /tmp/fhskl2-assets.json
python3 tools/igs-e2200-assets.py decode \
  'games/Fish Hunter SKL 2/game/action/06_sailfish.rom' /tmp/sailfish.plain.rom
```

Output paths must be new; the tool refuses to overwrite files. It shares
the extractor's permutation and does not modify the game directory.

The game's own reader at `0x080678c0` calls the function diagnosed as
`FPGA_AES_iEncrypt` (`0x08096e3d`) to turn files into plaintext.
The group parser at `0x080618c0` then checks `ACT_ZL3D` and processes
`TGA_OPGL`/`DDS_OPGL` chunks. This establishes the direction used for
asset reads despite the API's name. Evidence is retained in
[fish-hunter-skl2-fpga-evidence.json](fish-hunter-skl2-fpga-evidence.json).

Kernel inspection also found a second, conditional hardware binding:
`FPGA_Security_iRehashKey` (`0xc1026ddb`) MD5-hashes selected PCI
vendor/device IDs followed by user-key bytes when the driver flag at
`0xc194a734` is enabled. Ioctls `0x6612` and `0x6613` enable and disable
that flag. Both `0xc0106610` and `0xc0106611` ultimately transfer data
through `FPGA_Security_iCryptoReadBackSRAM` (`0xc1026f5f`), which writes
16 effective key bytes to I/O offsets `0x20`–`0x2f` and controls the
hardware operation at offset `0x40`. These paths do not reveal the
hardware's secret key-to-mask mapping. The recovered asset mask avoids
needing that mapping for the observed files.

### Follow-up: embedded IGS036 firmware recovered

The startup call at `0x08066e6e` passes **VA `0x082d4d20`, length
`0x90000`** to the board uploader `0x08095940`. The bytes start at ELF
file offset **`0x28cd20`**: a 64 KiB loader header followed by a 512 KiB
IGS036 ROM. This explains the previously unexplained encrypted `.data`
region.

The existing `tools/igs036-firmware.py` recovered the ROM using the local
PM2008, Pirate Revenge, and MuziBox firmware references. Repeated fill
blocks constrain 1,192 of the 2,048 IGS036 key bits; the reference passes
resolve the remaining code sufficiently for the embedded hash to match:

```text
ROM SHA-256: 9fb6b74a57e9214dabe02db668643de02867ecf43e08c0de0dff8498dba2c8f5
Embedded hash: c6812399; magic at ROM offset 0x19c30; shifts 6 and 2
Fill starts at ROM offset 0x186b0
```

The recovered local artifact is
`games/Fish Hunter SKL 2/game/roms/fhskl2_eprom.bin`.
It has a coherent ARM vector table and the project identifiers
`FishHunterSKL28P` and `FishSKL28P_E2200`. Absolute code/string pointers
use ROM base **`0x10000000`**; REA's raw-file offsets need that base added
when comparing them with firmware virtual addresses.

[fish-hunter-skl2-firmware-recovery.json](fish-hunter-skl2-firmware-recovery.json)
saves the 256-byte IGS036 key, an equivalent firmware FPGA mask, input and
reference digests, and the hash result. Regenerating the ROM from that saved
pair reproduces the recovered file byte-for-byte and passes the embedded
hash again. These are firmware recovery parameters, separate from the
runtime asset mask and the unavailable HASP key.

```sh
python3 tools/igs036-firmware.py \
  --elf 'games/Fish Hunter SKL 2/game/FHSKLv2-8p' \
  --at 0x082d4d20 --size 0x90000 \
  --ref 'games/Percussion Master 2008/game/roms/PM2008_EPROM.dec.bin' \
  --ref 'games/Pirate Revenge/game/roms/pr_eprom.bin' \
  --ref 'games/MuziBox/game/roms/muzibox_eprom.bin' \
  /tmp/fhskl2_eprom.bin
```

REA observations for the uploader and recovered ARM code are saved in
[fish-hunter-skl2-firmware-evidence.json](fish-hunter-skl2-firmware-evidence.json).
Next: trace the firmware's packet responses to the three startup comparison
words, then combine that board behavior with a narrow HASP replacement and
the verified asset transform. No full game launch or production loader
patch has been validated.

## The five enveloped executables

Soul of Gun, the three "OceanKing" builds and West Trip share the same
layout:

- **Encrypted body.** Uniform entropy 8.0 from file 0x10000 to the stub
  (Soul of Gun 0x10000–0x810000, West Trip 0x10000–0xc10000, the others
  similar). It covers nearly all of LOAD #1 (the code) and most of LOAD #2.
  No `_start` or libc code is readable there. The first 64 KiB (headers,
  `.interp`, `.dynsym`/`.dynstr`, `.dynamic`) is plain, so the import list
  is readable: `readelf -d` works, `objdump -p` does not.
- **Plain stub.** The entry point is at the end of LOAD #2, not in the code
  segment. It is a trampoline:

  ```
  sub $4,%esp; push %eax; push %edx; call <stub main>; mov %eax,8(%esp); pop %edx; pop %eax; ret
  ```

  That is, call the unpacker, then "return" into the address it gives back
  (the original entry). Around it is a plain HASP runtime of about 1 MiB:
  - `Unknown Sentinel HASP Error`;
  - `<hasp type="HASP-HL" />`, `<haspformat ...>`;
  - `hasp_datetime_to_hasptime`, `/dev/Hardlock`;
  - `/tmp/.aksusb`, built on the stack;
  - repeated `OEFI*TD**..*` markers in Soul of Gun.

  The stub table at stub+0x16 walks 32 slots of 0x58 bytes (Soul of Gun:
  base = `ebx-0x14c` after the GOT fixup).
- **Same envelope build.** The OceanKing builds and West Trip share the
  entry offset `...7ea` within the page, and their trampolines are
  byte-identical. Soul of Gun's sits at `...73a`.

Expected mechanism, by analogy with Cruis'n Blast 2.31, a Raw Thrills
Linux HASP envelope from 2022 that was analysed in depth (see
[cruisn-blast-231-envelope-followup.md](cruisn-blast-231-envelope-followup.md)
and [cruisn-blast-231.md](cruisn-blast-231.md)):

- the stub logs in to the dongle;
- it gets key material by `hasp_decrypt` or a challenge/response, or reads
  it from dongle memory;
- it decrypts the protected regions in place (Cruis'n Blast's software
  cipher turned out to be XTEA, with a generator-state blob run through the
  licensing dispatcher);
- it restores imports and jumps to the original entry.

These 2012 IGS envelopes are older HASP HL (not LDK/SRM) and may be simpler.
Unverified.

What would help, roughly in order:

1. **Find the region table and the cipher in the stub.** Look for the
   protected-region records (VA, size, flags), the block cipher (XTEA, AES,
   or the older HASP envelope cipher) and where its key comes from. If any
   key part is in the file rather than in the dongle, the body may decrypt
   offline. Known plaintext helps: the body must start with glibc's `_start`
   (`31 ed 5e 89 e1 83 e4 f0 50 54 52 68 ...`), and Fish Hunter's plaintext
   `.text` shows what IGS's code, libc and libstdc++ look like.
2. **Compare the five stubs.** Three builds share one envelope version, and
   one is from a different game family. Diffs in their data show what is
   per-game (keys, vendor code, feature id) and what is shared.
3. **Watch what the stub asks the dongle.** Under linuxloader with the HASP
   API replaced, it asks for a login, possibly memory and decrypts. Each
   query that needs a real answer shows what real hardware would have to
   supply.

What does not work:

- running the stub without a dongle: it will fail its login;
- stock AES or XTEA guesses without the key's source;
- Fish Hunter's dongle answers are not established as usable for another
  feature. The backend follow-up below found a shared vendor-data prefix
  across the five enveloped games, with different feature IDs; that does
  not establish interchangeable cryptographic responses.

## Other E2200 games: asset and board ROM recovery (Codex, 2026-11-24)

The same asset transform works on the other five games. Each mask was
derived from a candidate known ROM header and then checked against the
complete chunk layout of every available ROM and sound file:

| Game | ROM files validated | Sounds validated | Board firmware |
| --- | --- | --- | --- |
| Soul of Gun | 39/39 | 143/143 | External image found; decryption unresolved |
| Full Attack | 37/37 | 114/114 | No external image found; executable still enveloped |
| Ocean King | 47/47 | 123/123 | No external image found; executable still enveloped |
| Poseidon | 46/46 | 123/123 | No external image found; executable still enveloped |
| West Trip | 36/36 | 269/270 | **Recovered; embedded hash matched** |

Thus **977/978 asset files** pass structural validation. As with Fish
Hunter, this checks chunk boundaries, texture metadata, and PCM metadata,
not every pixel/sample. Pillow independently decoded one complete DDS
texture from each of the five games.

| Game | Default asset mask, before permutation |
| --- | --- |
| Soul of Gun | `590d4076d44dbcdcf3b4694fb9fa9ca3` |
| Full Attack | `5a5ed9b4b1758de03232965d0dddc484` |
| Ocean King | `7e1ed9a4b17595e8b23b965509cdc486` |
| Poseidon | `7e1ed9a4b17595e8b23b965509cdc486` |
| West Trip | `cf31323ab5ca23ef54b4e46f3040877f` |

Soul of Gun has two exceptions:
`sound/C_ mushroom_jump.wav` and `sound/Fish_ watersplash.wav` use
`3f932ea38f57215fabe20286d5437dff`. Their masks are recoverable by
trying the 16 possible RIFF padding lengths and requiring a complete,
consistent chunk layout; both yield the same unique mask.
Its root-level `Award9.rom` is already plaintext and requires no transform.

West Trip's `sound/Sound_CrazySay01.wav` is only **49,152 bytes**, while
its correctly decoded RIFF header declares **393,958 bytes**. The format
and data headers decode coherently with the game's default mask, but the
file cannot pass the bounds check. Treat it as an apparent truncated
source asset rather than claiming full validation.

The asset tool now supports `--auto-mask`: it finds a structurally valid
ROM seed, derives the game mask, recognizes plaintext assets, and recovers
exceptional sound masks. Reports record the mask used for each file:

```sh
python3 tools/igs-e2200-assets.py inspect 'games/Soul of Gun/game' \
  --auto-mask --report /tmp/soul-of-gun-assets.json
python3 tools/igs-e2200-assets.py --mask 590d4076d44dbcdcf3b4694fb9fa9ca3 \
  decode 'games/Soul of Gun/game/action/A01_AirFish.rom' /tmp/airfish.plain.rom
```

The report exits nonzero for a damaged/unvalidated asset, including the
West Trip sound above. Successful results remain in the report.
Full per-file evidence is in `e2200-<game>-asset-validation.json` beside
this document; the exact paths, masks, exceptional assets, independent DDS
checks, and board-ROM recovery parameters are indexed by
[igs-e2200-other-games-recovery.json](igs-e2200-other-games-recovery.json).

**West Trip board firmware.** Its `game/asic36.bin` is exactly 512 KiB
and has no 64 KiB loader header. Unlike the Fish Hunter embedded upload,
it is already past the FPGA permutation stage. The initial attempt that
applied that permutation again failed; skipping it yields coherent ARM
vectors and lets the IGS036 recovery tool match the embedded hash.
`tools/igs036-firmware.py` now accepts `--body` for headerless carriers
and `--no-fpga` for this input stage:

```sh
python3 tools/igs036-firmware.py \
  --body 'games/West Trip/game/asic36.bin' --no-fpga \
  --ref 'games/Fish Hunter SKL 2/game/roms/fhskl2_eprom.bin' \
  --ref 'games/Percussion Master 2008/game/roms/PM2008_EPROM.dec.bin' \
  --ref 'games/MuziBox/game/roms/muzibox_eprom.bin' \
  /tmp/westtrip_eprom.bin
```

The saved local ROM is
`games/West Trip/game/roms/westtrip_eprom.bin`:

```text
SHA-256: d6f2ebb6879d19569a1bdc86c0384095246896acf4337fa7e2e4a1fa9eb49d78
Embedded hash: fb3973c2; magic at offset 0x57470; shifts 4 and 12
Fill starts at offset 0x18a90
```

Repeated fill supplies 1,140 IGS036 key bits; local firmware references
refine the remaining code. The retained 256-byte key regenerates the ROM
byte-for-byte and passes the embedded hash again. The solved FPGA mask is
zero for this input stage. REA confirms coherent ARM vectors and CP15
setup; observations are retained in
[igs-e2200-other-games-firmware-evidence.json](igs-e2200-other-games-firmware-evidence.json).

**Limits for the other board ROMs.** Soul of Gun also has a 512 KiB
`asic36.bin`, but all 32,768 aligned 16-byte blocks are unique. The
repeated-fill solver obtains zero key-bit constraints and its attempted
decryption fails the embedded hash. Whether an additional protection layer
or a different layout accounts for this remains unresolved; the failed
candidate has not been installed as a recovered ROM. Full Attack, Ocean
King, and Poseidon have no external board image in their extracted game
directories. Locating and recovering any embedded image currently requires
further work on their protected executable bodies.

Asset recovery and West Trip's board-ROM recovery do not unpack the five
HASP envelopes or establish that any of these games run under linuxloader.

## Envelope removal follow-up

**Status: no unwrapped executable has been produced.** Removing the HASP
envelope is a separate task from recovering asset masks and board firmware.
The five protected executables have accessible startup stubs, but their
protected code still needs cryptographic output from the licensing path.
Patching a successful login return alone does not supply that output.

Read-only REA inspection identified two metadata layouts. Full Attack,
Ocean King, Poseidon, and West Trip use 64-bit address/count fields and
12-byte region records (64-bit address followed by 32-bit length). Soul
of Gun uses 32-bit fields and 8-byte records. All five contain a 9,412-byte
encrypted state blob, with a different SHA-256 for each executable.

| Game | Metadata VA | Original entry VA | Protected regions | Sum of region lengths | Selector threshold |
| --- | --- | --- | ---: | ---: | --- |
| Full Attack | `0x088bc9c0` | `0x080533f0` | 271 | 7,869,893 | `0x03574790` |
| Ocean King | `0x0891c9c0` | `0x08053430` | 271 | 8,259,418 | `0x03574790` |
| Poseidon | `0x089249c0` | `0x08053430` | 271 | 8,293,089 | `0x03574790` |
| West Trip | `0x08d489c0` | `0x080540f0` | 274 | 12,633,374 | `0x03547fd8` |
| Soul of Gun | `0x0893c540` | `0x08053440` | 271 | 8,398,026 | `0x0502ec20` |

The lengths are sums of declared region sizes, not a measurement of unique
encrypted bytes. All declared ranges map into file-backed ELF load segments.

### Full Attack startup dependency

The entry trampoline at `0x088327ea` calls the envelope stub at
`0x08830c40`, which initializes protection through `0x08830470`.
The metadata points to the state blob at `0x089957d0`, length `0x24c4`.
The following dependencies are visible in the startup code:

1. Licensing dispatcher `0x08835ba0`, operation 1, initializes the session
   using the embedded provider configuration.
2. At `0x08830790`–`0x088307b8`, operation 2 processes the state blob in
   place. Startup checks its status before copying the resulting state.
3. The copied generator state supplies a 16-byte cipher key and subsequent
   4-byte region selectors. Its initialization constant `0x6996c53a` is
   consistent with SOBER-128; no independent generator implementation was
   validated in this follow-up.
4. Cipher setup at `0x08832910` and its block routines implement 32-round
   XTEA: the schedule uses delta `0x9e3779b9`, 16-byte keys, and 8-byte
   blocks. The CBC wrapper is `0x08832bb0`.
5. Predicate `0x088301b0` compares the generated word and threshold as
   unsigned integers. A word below the threshold selects another licensing
   operation 2 on that region; otherwise startup uses XTEA-CBC on the
   block-aligned portion. The CBC context is initialized before the region
   loop. Region order and evolving generator/CBC state must be preserved.

Operation 2 reaches `0x08835280`, then the session-handle buffer API at
`0x088377e0`. This branches into the provider backend, including
`0x08839300` for one handle type and `0x08841c20` for the legacy path.
These are actual buffer transformations, not merely a Boolean presence check.
Soul of Gun shows the corresponding state-processing call at `0x088b0728`
and region loop at `0x088b09d6`–`0x088b0a9a`, including its 8-byte record
stride and the same choice between licensing and local block decryption.

The current missing evidence is the **successfully processed state blob**
and the **successful buffer responses for any regions selected for direct
licensing cryptography**. The state is needed even to discover the actual
selected region list. A working original environment could provide those
responses, or a memory dump after envelope initialization could provide
the decrypted regions directly. Neither has been captured for these games.
The recovered asset masks do not supply these missing values.

### Reproduce the metadata export

```sh
python3 tools/igs-e2200-envelope.py 'games/Full Attack/game/OceanKing' \
  --export /tmp/full-attack-envelope-inspect
```

The destination must be new. It receives `envelope.json` and
`licensed-state.encrypted.bin`; the latter is still encrypted. The tool
validates the entry/GOT stub signatures, distinguishes the two metadata
layouts, checks upper address words and file-backed bounds, and reports
the original executable hash. It is a metadata inspector, not a universal
envelope detector or decryptor.

The inspector successfully parsed all five protected executables and
rejected the plain Fish Hunter executable. Before/after hashes confirmed
all five originals stayed unchanged. Local exports are under
`/tmp/e2200-envelope-<game>/`. All region records and blob hashes are saved
in [igs-e2200-envelope-inventory.json](igs-e2200-envelope-inventory.json);
the retained REA observations are in
[igs-e2200-envelope-evidence.json](igs-e2200-envelope-evidence.json).
No protected executable was launched or patched during this follow-up.

### Backend follow-up: validated bulk decoder, still missing real replies

The next pass traced the bulk-buffer backend and replayed its actual code
in isolation. **All five bulk routines match the existing offline decoder**
in [cruisn-blast-buffer.py](../tools/cruisn-blast-buffer.py) for the tested
cases. This establishes a shared buffer format; it does not establish a
shared key or a successful licensed startup.

| Game | Bulk decoder VA | Envelope feature ID |
| --- | --- | ---: |
| Full Attack | `0x08893a90` | 17 |
| Ocean King | `0x088f3a90` | 6 |
| Poseidon | `0x088fba90` | 9 |
| West Trip | `0x08d1fa90` | 4 |
| Soul of Gun | `0x08914210` | 8 |

The feature IDs are read at provider configuration offset `0xc64`.
In Full Attack, login helper `0x08834f50` passes that field to
`0x088364a0`, alongside the configuration base as vendor data and the
scope at configuration `+0x1127e`. The first `0xc64` configuration bytes
are identical across the five files, SHA-256
`d8218def7b340159a97b4c6b5ec6e714d6b5dfe87aa4e7100fc01d4835c83a07`.
This corrects the earlier assumption that each of these five games has a
different vendor code. Their different feature IDs and input buffers
still prevent assuming that one feature's answers work for another.
Fish Hunter's vendor argument has a different representation; its
interchangeability with this configuration has not been established.

For a buffer of at least 32 bytes, the validated bulk routine does this:

1. Let `C0` be its first 16 bytes and `Cbody` the remaining bytes. Send
   `C0 || RIPEMD160(Cbody)[:16]` to callback operation 1, phase 1.
2. Use the first 16 bytes returned by that callback as an AES-128 key.
   Decode the body locally with zero-IV CBC and ciphertext stealing for
   an incomplete final block.
3. Send `C0 || RIPEMD160(Cbody)[:16] || RIPEMD160(Pbody)[:16]` to
   callback operation 1, phase 2. Its first 16 response bytes supply the
   separately protected first plaintext block.

Thus a **16-byte phase-1 reply** would let us decode and inspect the
remaining **9,396 bytes** of that game's startup state. The first 16
plaintext bytes still need phase 2 or independent recovery. The complete
state is needed for the envelope generator, and direct-licensed regions
remain an additional dependency.

For Full Attack's real startup blob, the first inner callback request is:

```text
76e0972e0df8f49aeead7ff13fd0912ce
03347d9d0f5c77cb48188c12face47a
```

Both lines together form one 32-byte request. The other games' exact
requests are retained in each inventory entry's
`licensing_blob.bulk_stage1_request` field. None is a recovered key or
a complete packet: session wrapping and feature context are additional.
The envelope exporter now also writes `bulk-stage1-request.bin`.
The existing offline decoder can test a supplied response without running
the game:

```sh
python3 tools/cruisn-blast-buffer.py \
  /tmp/full-attack-envelope-inspect/licensed-state.encrypted.bin \
  --export /tmp/full-attack-buffer-request
```

With no key argument this exports only the request. With `--key HEX` it
decodes an explicitly **unverified** candidate body; this does not
authenticate the supplied response.

**Full Attack's backend boundary.** Its general buffer path calls the
bulk routine with callback `0x08839dc0`. That callback records phases
1/2 and lengths 32/48, serializes and wraps the request, then calls
`0x08838fa0` at `0x08839f88`. The send helper `0x08838cc0` reaches imported
`send` at `0x08838d36`. This traced path crosses a socket boundary and
does not supply an embedded stand-alone content key. The separate legacy
route and the session-dependent alternative at `0x088394a5` are not fully
resolved by this pass; their existence prevents a claim that every
software recovery avenue has been exhausted.

The context lookup at `0x088946d0` walks up to 512 records, stride
`0x3c8`, starting at `0x0891b300`. All 495,616 file-backed bytes of this
initial cache are zero in Full Attack. This rules out retained records in
that specific on-disk cache; it says nothing about a running process.

A bounded filename/extension search of the five extracted game trees
found the bundled licensing daemons and udev rules, with no named dongle
capture or core dump. Their `var.tar.gz` archives each contain 31 regular
files; the ordinary system logs are empty, while ConsoleKit history is
present. No saved licensed response was identified by this search. Its
inventory is retained below; arbitrary unnamed buffers, disk slack, and
unextracted partitions were not searched.

**Validation.** The native probe maps private copies of the original ELF
segments, binds memory primitives, and calls only the selected bulk
routine. Its backend callback returns synthetic fixture values; game
startup and licensing transport are never invoked. The validation driver
checks an exact original SHA-256 before execution. All five routines
returned the expected plaintext and both exact callback requests at sizes
32, 33, 47, 48, 49, 64, 209, and 9,412: **40 successful native cases**.
The probe compiled with `-Wall -Wextra` without warnings. Original
executable hashes still match the earlier inventory.

```sh
gcc -m32 -O2 -Wall -Wextra -o /tmp/e2200-buffer-probe \
  tools/dongle-recording/e2200-buffer-probe.c
python3 tools/dongle-recording/validate-e2200-buffer.py \
  'games/Full Attack/game/OceanKing' /tmp/NEW-e2200-buffer-validation \
  --native-probe /tmp/e2200-buffer-probe
```

Native execution required sandbox escalation for 32-bit code, which was
approved for these isolated synthetic checks. This is validation of the
decoder algorithm, **not successful game decryption or HASP key recovery**.

- [Native replay results](igs-e2200-buffer-validation.json)
- [Backend disassembly evidence](igs-e2200-backend-evidence.json)
- [Bounded local capture inventory](igs-e2200-local-capture-inventory.json)
- [Updated envelope and request inventory](igs-e2200-envelope-inventory.json)

The next static lead is the unresolved legacy/session-dependent route;
the next useful runtime evidence is a successful phase-1/phase-2 reply
for an exact request above, or decrypted memory after envelope startup.

## Tools and conventions in this repo

The user-supplied `haspemu/` archives were reviewed separately. They
implement older HASP4/HardLock profiles and some Target Terror-specific
replay logic, rather than the modern E2200 bulk-reply backend. Their
default AES-field constant failed a direct phase-1 candidate check for
all five state blobs. See [the source review](igs-e2200-haspemu-review.md)
for exact boundaries, profile inventory, and candidate-test results.

- Disassembly: the REA MCP server first (Ghidra and rizin; see AGENTS.md),
  objdump as fallback. REA prints every result twice, so prefer objdump for
  bulk reads.
- 32-bit test builds: `gcc -m32 -fno-pie -no-pie`. `tools/igs-e2200/igs-rofs.c`
  shows how to map a binary at its own addresses and call its code with
  regparm(3) hooks. The same trick can run an envelope stub's cipher
  routine in isolation, with its HASP calls hooked.
- HASP replacement and recording infrastructure: `src/loader/rawthrills/rtDongle.c`,
  `tools/dongle-recording/`, [dongle-recording.md](dongle-recording.md),
  [dongle-answers-via-windows-memory.md](dongle-answers-via-windows-memory.md).
- Branch `rawthrills`: one commit per game or system, and no Claude
  co-author trailer.
