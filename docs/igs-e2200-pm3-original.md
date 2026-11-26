# Percussion Master 3: original CHD comparison

Inspected 2026-11-24 from `games/e2200_percussionmaster3.chd`.
The original PM3 executable is recovered through its FPGA transform.
It has normal startup and no observed HASP envelope. This is static
recovery and comparison, not a successful linuxloader launch.

Output is under `games/Percussion Master 3 Original/`. The prepared
`games/Percussion Master 3/` package was not modified.

## Disk and root extraction

CHD logical size: 3,917,168,640 bytes. From `chdman info`:

```text
SHA1:      b19f169970760f580a3a82e570c45af5d8c43c43
Data SHA1: 23ba808748bab3a515096765b16e116f10cb3576
```

A separate complete CHD verification pass was not run.

| Partition | Start sector | Sector count | Content |
| --- | ---: | ---: | --- |
| hda1 | 63 | 24,129 | Boot partition containing the compressed kernel. |
| hda2 | 24,192 | 352,800 | `Game Image v2`, encrypted igs_rofs root. |
| hda3 | 3,307,248 | 4,343,472 | Plain ext filesystem at `/PM3`, with `ROMS/*.rom`. |
| hda4 | 376,992 | 2,930,256 | Plain ext filesystem at `/PM3/rewrite`. |

Unlike the earlier six dumps, hda3 is not IGS_SHFS. Its `.rom` files
are standard SquashFS 4.0 containers. The general E2200 extractor
stopped at its IGS_SHFS check; it has not been changed to claim PM3
support. Partition copies remain in `/tmp/e2200-pm3-original-work/`.

Kernel extraction and kallsyms decoding reused functions from
`igs-e2200-extract.py`. The `igs-rofs.c` helper then extracted the root
using PM3's own kernel driver: **330 directories, 3,183 files, 389 links,
zero reported errors**. This required approved 32-bit decoder execution.
No cabinet launcher or game executable was run.

```text
Linux version 2.6.32.8 (chinchenlin@PDS) (gcc version 3.3.1)
#1 SMP Wed Dec 7 20:01:54 CTS 2011
```

The cabinet's `etc/X11/IGS` mounts hda3/hda4, runs `/PM3.bin/PowerOn`,
starts `pcli`, then runs `/PM3.bin/pm3`. It does not start the HASP
daemons used by the five enveloped games. Kernel `init_post` at
`0xc100133a` invokes `/bin/uudecode` with `/PM3/PowerOn`,
`/PM3.bin/PowerOn`, and argument `Hello-Percussion`. This custom utility
uses `/dev/pccard0` and FPGA ioctls, rather than ordinary uuencoding.

## Executable recovery

`debugfs` extracted `/ROMS/exe.rom` from hda3. `unsquashfs` recovered
`pm3`, `pcli`, `wget`, and `checkUpdate`. Only `pm3` is FPGA encoded.

Its carrier starts with four bytes `d9e29e99` before the aligned payload.
Decoding at byte zero failed; decoding at byte four with the known ELF32
identification block recovered a valid ELF. The effective masks are:

```text
PM3:     3b0e2c44e579a0a91770dd107cbb2a19
PowerOn: 2a36323bfe91358845f1c3dfc53d9aa3
```

The transform is `plain = PERM(cipher XOR mask)`, as in the E2200 asset
tool. These masks are not recovered HASP secrets or original hardware
user keys. PowerOn has no four-byte prefix.

[igs-pm3-decode.py](../tools/igs-pm3-decode.py) derives the mask from the
ELF identification block, then checks independent header fields, table
bounds, segment bounds, and the executable entry mapping:

```sh
python3 tools/igs-pm3-decode.py \
  'games/Percussion Master 3 Original/game/exe/pm3.fpga-encoded' \
  /tmp/NEW-pm3-decoded
python3 tools/igs-pm3-decode.py \
  'games/Percussion Master 3 Original/boot/rootfs/PM3/PowerOn' \
  /tmp/NEW-poweron-decoded
```

Each command creates a new directory with `decoded.elf` and
`recovery.json`. Independent reruns reproduced the retained hashes.
All decoded payload bytes are kept. PM3 has 16 bytes after the ELF
section table; their checksum/footer meaning and the cabinet decoder's
exact final-length handling remain unverified. They were not removed.

## Comparison with the prepared package

| Property | Original decoded payload | Prepared package |
| --- | --- | --- |
| Size | 26,092,208 bytes, retained tail included | 26,091,136 bytes |
| Entry | `0x085530d0` | `0x08553150` |
| `main` | `0x085bd4b0` | `0x085bcd20` |
| Build ID | `0ba32a270f7a93d778e06428a5e3681ff3884e48` | `658c95a4000501f99010a3e38246a5178c6ac747` |
| `hasp_`, `HASP`, `THaspKeyPro`, `FAFC_` | Zero full-file matches | Zero full-file matches |

Original decoded SHA-256:
`828d49175dcc987b2a31cc87db1aa029b8299cc6a16d942d9a175a3e570ca57c`.
Prepared SHA-256:
`429cd794e89bd7d3997325d4310a2ee026de8f95c67f97a9f7f4738b6d746e46`.

REA confirms normal `_start -> __libc_start_main -> main` for both;
many dynamic symbols survive. They are different builds, so the prepared
ELF is not established as an exact plaintext counterpart at every offset.
No HASP-envelope unpacking was needed to expose original PM3 code.
Board/I/O, asset, and online dependencies remain separate concerns.

Decoded PowerOn SHA-256:
`065a8ee2810f580d09e3dd018430d3a6c3130c62aaf3e0d83b6dabc80ce4e7bf`.
Its strings/control flow identify `../exe/pm3`, `/PM3.bin/pm3`, FPGA
encryption/decryption, ROM mounts, and update management.

## Original embedded firmware remains unresolved

`Game::iIgs036_Init` constructs `Igs036P2A` at `0x085e3ba8`.
The constructor passes `A36ROM`, size `0x80000`, and string
`PM3FA Start Now!` to `P2A_LoaderAdv_iLoadDataPointer` at `0x08629035`.
The 512 KiB embedded object at `0x098a2ac0` is retained as
`game/roms/pm3_a36_embedded.bin`.

```text
Original embedded SHA-256:
885d3a6047a921c87d1893796991e792dd8f69850578ae52681dc1fe483ed47a
Prepared embedded SHA-256 (VA 0x098a16a0):
4b7a181af4663ab69cafc7f05d6f4a81b4b821546a78f8d7974a71fdbf7cce49
```

They differ in 522,396 bytes. That alone does not establish different
plaintext firmware versions: their transforms may differ too.

The IGS036 solver was tried with the original body and the prepared
plaintext ROM, PM2008, and MuziBox as references. Its proposed 1,088
key-bit constraints did not yield a matching embedded hash. Complementing
the fill-derived FPGA mask enabled reference alignment of 24,418 and
27,493 words in two passes, but the final hash still failed. Neither
candidate was installed as recovered firmware. Allowing reference votes
to correct initially fill-constrained key bits changed 52 bits, then
converged without further changes; its embedded hash still failed.
A direct FPGA-only transform guessed from the prepared ROM's first
16 bytes also failed to match its next 496 bytes (494 differences).
The upload stage and remaining key/mask constraints need further work.

## Evidence and scope

- [Recovery parameters and artifact hashes](igs-e2200-pm3-original-recovery.json)
- [REA observations](igs-e2200-pm3-original-evidence.json)
- [Prepared-package inventory](igs-e2200-pm3-inventory.json)

This pass extracted the original root and selected SquashFS containers,
decoded PM3/PowerOn, and compared their static structure. It did not
assemble every media mount, import cabinet rewrite data into a runnable
tree, port PM3 to linuxloader, or launch gameplay. PM3 adds a readable
E2200 protocol reference, but supplies no demonstrated missing HASP
replies for the five enveloped games.
