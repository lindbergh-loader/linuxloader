# West Trip envelope follow-up

West Trip's **game envelope remains unresolved**. This investigation recovered
the protected code/data of its bundled `hasplmd` for analysis, and tested
44,212,602 retained AES key candidates against the game's encrypted startup
state without a match. These are direct candidate-key tests, not exhaustive
AES key-space searches.

A subsequent [search of the eight supplied E2200 disks for retained licensing
state](igs-e2200-retained-license-search.md) found no usable saved state,
software-license artifact, or core dump. The previously omitted writable
partition yielded West Trip crash logs and black screenshots, rather than
process-memory captures.

The original game and daemon files are unchanged. No game or daemon startup
was executed. One isolated 32-bit probe called only the daemon wrapper's
generator read routine, with its decoded state.

## Game barrier and bounded key scan

Target: `games/West Trip/game/WestTrip`, SHA-256
`9e2c3a34fd0aa4da9755ea37d4c0e01317e9bb296c150a8da5bb6860d424ceff`.
The envelope protects 274 regions and advertises original entry `0x080540f0`.

At `0x08cbc790`–`0x08cbc7b8`, startup submits the 9,412-byte state at
`0x08e217d0` to licensing operation 2 and checks its status before copying
the result. The generator ready routine at `0x08cbeee0` explicitly requires
the decrypted state word at `+0x98` to equal 1.

[The scanner](../tools/igs-e2200-scan-state-keys.py) and its
[C helper](../tools/igs-e2200-scan-state-keys.c) test every contiguous
16-byte window, including unaligned and final windows, in raw and per-word
byte-reversed order. Each candidate is used directly as the bulk phase-1
AES reply key. Decrypting the block at encrypted-state `+0x90` and XORing
the preceding ciphertext block recovers the required word. This block is
well before the ciphertext-stealing tail.

Only the necessary ready-word condition is used for rejection; the scan
does not assume additional generator fields have particular values.
A ready-word match would still need independent validation.

| Sources | Candidate evaluations | Ready-word matches |
| --- | ---: | ---: |
| Recovered board firmware, encrypted ASIC image, original game, original `hasplmd`, `winehasp`, `aksusbd` | 37,861,604 | 0 |
| Recovered daemon code/data, decoded daemon state, daemon generator output | 6,350,998 | 0 |

Reports preserve source hashes and exact counts:
[original files](igs-e2200-west-trip-state-key-scan.json),
[recovered daemon](igs-e2200-west-trip-unpacked-daemon-key-scan.json).
Synthetic positive cases verify keys at the beginning, unaligned final
windows, and reversed-word storage; a negative case is also checked.
The counts include repeated key values at different offsets.

This excludes direct raw-key storage in those scanned bytes under the
validated bulk-buffer interpretation. It does not exclude key derivation,
noncontiguous storage, different session paths, or secrets held by hardware.

## Recovering the bundled daemon wrapper

Source: `games/West Trip/boot/rootfs/usr/sbin/hasplmd`, SHA-256
`d8b805d8a28643423ce2df73c2bfde9599f364d9be819d6849e4d473fb5930de`.

Its entry `0x08340bac` initializes a separate HASP SRM envelope before
jumping to the entry in metadata at `0x08345260`. Unlike the game's
licensing backend, this wrapper's provider uses local crypto:

- `0x08342160` initializes a local cipher from the configuration key.
- `0x083421b0` processes buffers with that local cipher.
- The AES-128 key at `0x0834def4` is
  `620bd0709fe77709f8b6371ef80d79e6`.
- Zero-IV AES-CBC decodes the full-block portion of the state at
  `0x0834ba30`; its fields at `+0x90,+0x94,+0x98` become `(0,0,1)`.
  The provider leaves the final four bytes unchanged.
- An isolated call to `0x08344740` produces 16 key bytes followed by fifteen
  separate four-byte selector draws. The derived XTEA key is
  `621469875992edf89043778d94952b63`.
- The threshold is `0x61861788`. Seven of fifteen regions take the local
  AES provider; eight take XTEA-CBC, with chaining preserved between those
  XTEA regions. The AES provider resets its CBC context per call and handles
  short buffers by XORing an AES-encrypted byte ramp.

This recovers 3,092,296 declared bytes across fifteen regions. Independent
structural checks recognize ordinary `_start` instructions, a PLT entry,
constructor sentinels, and readable version text:
`Compiled on Jun  9 2008, version 12.41.1.7725`.

Artifacts are under `games/West Trip/analysis/hasplmd/`. The analysis ELF
SHA-256 is `6f23c0c672cb758e6166edce67ac8b7d3c8afa945672f3d92cbb5f132d7e04ef`.
Its entry is reset to `0x0804a5e0` for analysis; import/dynamic metadata has
not been rebuilt into a runnable daemon. The recovery report includes all
148 import descriptors and per-region hashes.

Reproduction, using new output directories:

```sh
gcc -m32 -O2 -Wall -Wextra -o /tmp/e2200-daemon-prng \
  tools/dongle-recording/e2200-daemon-prng.c
python3 tools/igs-e2200-daemon-unpack.py \
  'games/West Trip/boot/rootfs/usr/sbin/hasplmd' \
  --export /tmp/west-daemon-state
/tmp/e2200-daemon-prng \
  'games/West Trip/boot/rootfs/usr/sbin/hasplmd' \
  /tmp/west-daemon-state/generator-state.bin /tmp/west-daemon-stream.bin
python3 tools/igs-e2200-daemon-unpack.py \
  'games/West Trip/boot/rootfs/usr/sbin/hasplmd' \
  --prng-stream /tmp/west-daemon-stream.bin --export /tmp/west-daemon-analysis
```

The native probe maps foreign code; it never calls the ELF entry point,
constructors, licensing initialization, or transport. This environment
required approved execution outside the sandbox because sandboxed i386
execution exited with SIGSYS.

## First recovered backend path

One recovered crypto path constructs command `0x44f` at `0x0804d647` and
calls through the driver-function slot `0x0816da24` at `0x0804d6d0`.
The slot initially points to `0x080ada90`. That function contains a
request/reply transport using `send`, `select`, and `recv`, resolved through
the wrapper's import descriptors. The crypto caller's failure branch logs
`crypt: call to driver failed with status 0x%x`.

This establishes a forwarding path in the recovered daemon. It does not
yet map West Trip's exact inner bulk request through all daemon dispatch
and device handling, or establish what every alternative branch does.
The follow-up below identifies that endpoint and follows command `0x44f`
to USB. The game still needs valid processed state and the responses for
regions assigned to licensing crypto.

## Transport follow-up: local socket to dongle USB requests

The recovered daemon constructs an `AF_UNIX`, `SOCK_STREAM` connection to
`/tmp/.aksusb` at `0x080addad`–`0x080ade16`. It forwards requests to the
bundled `aksusbd`, whose SHA-256 is
`8dce04d52c25d4abd46266c5a7359609990b0feec511744df58e81adf6ad6514`.

| Location | Observed behavior |
| --- | --- |
| `hasplmd:0x0804d647` | Builds crypto command `0x44f`. |
| `hasplmd:0x0804d6d0` | Calls driver slot `0x0816da24`, initially `0x080ada90`. |
| `hasplmd:0x080addad` | Opens the Unix socket to `/tmp/.aksusb`. |
| `aksusbd:0x080585b7` | Dispatches command `0x44f` to `0x08058682`. |
| `aksusbd:0x08058738` | Calls crypto phase dispatcher `0x080574d0`. |
| `aksusbd:0x08056320` | Serializes a device operation into request type `0x29`. |
| `aksusbd:0x080583d0` | Wraps that request and calls `0x0804f620` with control code `0x9c412638`. |
| `aksusbd:0x0804f831` | Selects the type-2 wrapper; requests above `0x1f` follow `0x0804fbcd`. |
| `aksusbd:0x0804fcf5`, `0x0804fd46` | Issues vendor USB OUT and IN control transfers through `0x0804da20`. |
| `aksusbd:0x0804dabc` | Calls imported `ioctl` with `0xc0105500`, the i386 `USBDEVFS_CONTROL` code. |

For request `0x29`, the USB OUT setup uses request type `0x40` and request
`0x29`; the matching IN uses type `0xc0` and request `0xa9`.
These transfer setup values are statically traced, not measured on hardware.
The older-command path through `0x0804eef0` is a separate branch and should
not be confused with this `0x29` path.

### Isolated native packet validation

[The native probe](../tools/dongle-recording/e2200-aksusb-probe.c) maps the
verified `aksusbd`, binds memory/allocation helpers, and calls only its
crypto dispatcher. It replaces session lookup and the device transport
entry with local callbacks. The real packet serializer executes; no daemon
startup, local socket, USB transfer, or dongle response occurs.

All ten cases pass [independent packet checks](../tools/dongle-recording/validate-e2200-aksusb.py):

| Phase | Input bytes | Operation, direction 0 | Operation, direction 1 | Synthetic reply bytes |
| --- | ---: | ---: | ---: | ---: |
| 0 | 16 | `0` | `1` | 16 |
| 0 | 32 | `6` | `7` | 32 |
| 1 | 32 | `2` | `3` | 16 |
| 2 | 48 | `4` | `5` | 16 |
| 3 | 16 | `10` | `11` | 32 |

The captured device-operation buffer is twelve header bytes followed by
the unchanged input bytes. With zero optional data and flags:

| Offset | Contents |
| --- | --- |
| `0..1` | Big-endian key slot, synthetic value `0x1234`. |
| `2..3` | Big-endian session index, synthetic value `0x3456`. |
| `4` | Operation from the table above. |
| `5..11` | Zero for these cases. |
| `12..` | Input buffer, byte-for-byte unchanged. |

The key slot is a runtime device/session value; it is **not** the game's
login feature number 4. Do not substitute that feature into the slot field.
The lower builder also increments the second word of its driver-handle
structure after success; the probe verifies this counter.

The phase-2 temporary reply capacity is 48 bytes, while the dispatcher
advertises a sixteen-byte reply. The probe deliberately supplies sixteen
bytes and checks the copy-back. Capacity and returned payload length must
be kept separate. The probe uses fixed synthetic replies and proves packet
construction/copying, not correctness of any real cryptographic reply.

```sh
gcc -m32 -O2 -Wall -Wextra -o /tmp/e2200-aksusb-probe \
  tools/dongle-recording/e2200-aksusb-probe.c
python3 tools/dongle-recording/validate-e2200-aksusb.py \
  'games/West Trip/boot/rootfs/usr/sbin/aksusbd' \
  --probe /tmp/e2200-aksusb-probe --report /tmp/west-aksusb-validation.json
```

[Validation report](igs-e2200-west-trip-aksusb-validation.json) ·
[Transport evidence](igs-e2200-west-trip-transport-evidence.json)

This explains where the missing bulk replies can be requested or recorded.
It does not supply them: the inspected phase-1/phase-2 path submits device
operations rather than computing replies from a retained host key. West
Trip's full session wrapping and its actual request reaching this handler
have not been replayed. Successful hardware/session evidence, or an already
decoded executable/state from the same build, remains the useful missing
input for removing the game's envelope.

[Recovery report](igs-e2200-west-trip-daemon-recovery.json) ·
[REA evidence](igs-e2200-west-trip-envelope-evidence.json) ·
[Main investigation](igs-e2200-hasp-codex.md)
