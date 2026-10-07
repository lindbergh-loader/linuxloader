# Bypassing the HASP dongle

Raw Thrills' Linux games are protected by an Aladdin/Sentinel **HASP HL** USB
dongle. Without it a game refuses to start, and on most titles its data files
cannot even be read: they are encrypted with keys only the dongle produces.

The loader does not emulate the USB device. It replaces the **HASP API** inside
the game process, so the game's own `Dongle*` layer above it runs unchanged.
Everything below lives in `src/loader/rawthrills/rtDongle.c`, driven by the
per-game fields in `rtGames.c`.

## What the protection actually does

A protected game does four things, and all four have to be answered:

1. **Logs in to a feature.** `hasp_login(feature, vendorCode, &handle)`. A
   failed login stops the game immediately.
2. **Reads the dongle's memory.** `hasp_read(handle, fileId, offset, length,
   buffer)`. This is where the cabinet's identity lives: cabinet type, a
   configured flag, the serial number. It is *not* a formality — see below.
3. **Asks the dongle to encrypt or decrypt.** `hasp_encrypt` / `hasp_decrypt`,
   or the game's own `DongleEncrypt` / `DongleDecrypt` wrappers. The data files
   hang off these.
4. **Queries the session.** `hasp_get_sessioninfo` returns XML with the dongle
   id in it.

## What the loader replaces

`rtInstallDongle()` detours these by name. For a stripped binary the names come
from the game's `symbols` table (`RtSymbol`), which maps a name to an address.

| Function | Replacement |
|---|---|
| `hasp_login` | `HASP_STATUS_OK` for the game's `haspFeature`, `HASP_FEATURE_NOT_FOUND` otherwise |
| `hasp_logout`, `hasp_write` | `HASP_STATUS_OK` |
| `hasp_read` | synthesised or recorded dongle memory |
| `hasp_get_sessioninfo` | a fixed `<haspid>` XML string |
| `hasp_free` | frees what `get_sessioninfo` handed out |
| `hasp_encrypt`, `hasp_decrypt` | recorded answers, when the game has a `hasp` folder |
| `DongleEncrypt`, `DongleDecrypt` | a no-op that clears the status word, when it has not |
| `DongleWriteLoop`, `DongleNoise`, `DongleWriteHLFeature` | `HASP_STATUS_OK` |

Rejecting logins for any feature other than the configured one matters. Big
Buck HD Wild walks a *table* of candidate features and takes whichever
succeeds, so a login that says yes to everything lets it settle on the wrong
one.

## The dongle memory is not a formality

`hasp_read` answers from a 128-byte image. With no recording, the loader
synthesises one that is all zeros apart from three fields:

```c
#define DONGLE_CAB_TYPE 0x1d
#define DONGLE_FLAG     0x1e
#define DONGLE_SERIAL   0x3c
```

Those offsets are the g3/g5/g6 layout; a 64-bit g7 game such as Halo uses its
own (see below). **Returning zeros is not safe.** The cabinet type selects
which cabinet the game believes it is running on, and games branch hard on it.
On Halo a zeroed dongle reports cabinet type 0 ("Unknown"), so the conf-group
registration skips `MODEL_CONFS`, `GUNS`, `HITFX_DAMAGE_TEMPLATES` and
`PROGRAM` entirely; `GameLoadConf()` then fails and the game takes itself down
in its own shutdown path. The symptom — a segfault in `pthread_timedjoin_np`
— looks nothing like a dongle problem.

So: find the cabinet type the game expects, and return it.

## Encrypted data files: recorded answers

Where the data files are encrypted, a stand-in cannot work — the game needs the
*real* ciphertext transform. The loader replays recordings instead. Set
`haspAnswers = "hasp"` on the game and put a folder of that name beside the
game binary:

- `hhl_mem.dmp` — a 128-byte dump of the real dongle's memory, used verbatim by
  `hasp_read`.
- one file per recorded call, named after the **first `answerKeySize` bytes of
  the input buffer, hex-encoded** (16 by default, up to 32 via
  `haspAnswerKeySize`). The file's contents are the answer and must be exactly
  as long as the call's buffer.

An unrecorded call falls back to a stand-in transform and logs
`no recorded dongle answer <path>, using a stand-in`. Big Buck World's folder
has around 3400 of these.

Where the recordings come from — patching a copy of the game to log every
dongle call and running it under TeknoParrot's BudgieLoader — is in
[dongle-recording.md](dongle-recording.md).

## Finding the functions in a stripped binary

Three methods, cheapest first. Whichever you use, **verify** — an address that
merely looks plausible will fail silently and cost hours.

### 1. The library's own trace strings

The HASP HL runtime carries strings like `enter hasp_login\n`. Where they are
still referenced, the reference sits inside the function that logs it, so
walking back from it to the prologue gives the entry point. Note they are often
dead data — present but unreferenced — in which case this yields nothing.

### 2. Cross-match against a game you have already done

The runtime is linked in statically and is often *the same build* across
titles, so the whole library sits at a constant offset. Take a function whose
address you know from another game, find its bytes in the new one, and the
delta maps every other function.

This is how Big Buck HD Wild was done against The Walking Dead: the delta is
`+0x1b4300`, and all seven functions landed on it.

Verify it rather than trusting the delta. Each function diverges from its twin
after a dozen bytes at a `push imm32` — and the immediate is the address of its
own trace string. Follow that pointer in both binaries and check the text
matches:

```
hasp_login   TWD 0x0837bbc0 -> HD Wild 0x0852fec0   diverges at 13, push -> "enter hasp_login\n" in both
hasp_read    TWD 0x0837c9b8 -> HD Wild 0x08530cb8   diverges at 12, push -> "enter hasp_read\n"  in both
```

That is identification, not resemblance.

### 3. The game's call sites

Arguments passed as literals can be read straight off the call site, which is
the most reliable source for the ids:

```
8267553:  movl $0x4,0xc(%esp)        ; length
826755b:  movl $0xfff2,0x4(%esp)     ; fileId  -> haspMemoryFileId
826757e:  call 8530cb8               ; hasp_read
```

Arguments computed at run time cannot be read this way. Big Buck HD Wild's
feature id is one of those: it comes from a table the game walks, so only a run
would show which one wins.

## Worked example: Big Buck HD Wild (32-bit, g5)

```c
static const RtSymbol bbhdSymbols[] = {
    {"hasp_login", 0x0852fec0},          {"hasp_logout", 0x0852ff60},
    {"hasp_encrypt", 0x0853004c},        {"hasp_decrypt", 0x08530138},
    {"hasp_get_sessioninfo", 0x08530a40},{"hasp_read", 0x08530cb8},
    {"hasp_write", 0x08530d84},          {NULL, 0},
};
/* ... */
    .symbols = bbhdSymbols,
    .haspFeature = 0xffff0000,   /* the legacy program-number feature */
    .haspMemoryFileId = 0xfff2,  /* read off the call site above */
    .haspAnswers = "hasp",       /* four recorded startup answers */
```

Its data files are encrypted, so it needs the recorded answers like The Walking
Dead: four files in `hasp/` beside the binary (the efilemaps' AES key and IV,
and two file-key secrets). Without them the loader's no-op `DongleEncrypt`
leaves the derived key unchanged, the def files fail to decrypt, the
adjustment table comes up empty, and the game segfaults in `Currency_Init` —
a crash that looks like a data problem and is a dongle problem. The capture is
in [dongle-recording.md](dongle-recording.md).

## Worked example: Pink Panther Jewel Heist (32-bit, g6)

Its trace strings are dead data (nothing pushes their address), so method 1
yields nothing. Method 2 does: Big Buck HD Wild's first 12 bytes of each
function land at a single delta, `-0x225050`, and each candidate pushes its
own `enter hasp_*` string at the point of divergence. The call sites give
the rest, exactly as in Big Buck HD Wild: `hasp_login` with `0xffff0000` as
a literal (`0x819dfa9`), memory reads from `0xfff2`. The wrappers are the
single callers of `hasp_encrypt` (`0x819eda0`) and `hasp_decrypt`
(`0x819ef20`).

Its four startup answers came with the game as TeknoParrot pairs rather than
a capture: `10q`/`10a`, `20q`/`20a`, `100q`/`100a` and `200q`/`200a`, named by
their length in hex (16, 32, 256 and 512 bytes), `q` the input and `a` the
answer. They become the loader's `hasp/` folder with no parsing: name each
answer after the first 16 bytes of its question.

```sh
for n in 10 20 100 200; do cp ${n}a hasp/$(head -c16 ${n}q | xxd -p); done
```

## Worked example: Aliens Armageddon (32-bit, g6)

Its trace strings are live, and method 2 lands on the first try against
Big Buck World (not Terminator Salvation, whose build differs): all eight
functions sit at `+0xa5fb0`, each pushing its own `enter hasp_*` string.
`hasp_free` pushes none; it is the one called on the session info after
`strtol`, as in Big Buck World. The call sites give `0xffff0000`
(`0x81be928`) and `0xfff2` (`0x81be47e`).

Its recording came with the game as a TeknoParrot `hasp/` folder in
Terminator Salvation's format (16-byte names, about 14,700 answers, and
`hhl_mem.dmp`), used as is. The memory decides the guns: `GameInputMaps`
reads byte `0x3a` of it, and 4 there would select a second set of gun
slots rather than the IR camera's. The recording has 0, which is right.

## Worked example: Halo Fireteam Raven (64-bit, g7)

Halo is a 64-bit memory dump and does not go through this framework; it has its
own preload, `src/loader/rawthrills/halo/halo_rt.c`. The principle is the same,
the mechanics differ:

- Functions are detoured by **address**, writing `jmp *0(%rip)` plus an 8-byte
  target over the entry point, because there are no symbols at all.
- The dongle memory layout is its own: byte 40 game code, **41 cabinet type**,
  42 region, 43..46 serial. The cabinet types this game accepts are 3 (Super
  Deluxe), 4 (Mounted Gun), 8 (55" Dedicated) and 9 (Dual Screen); anything
  else and it will not load its configuration.
- `hasp_decrypt` answers are replayed from files named after the first 32 bytes
  of the input, exactly as the framework does it.

## Checklist for a new game

1. Locate the seven HASP functions and put them in the game's `RtSymbol` table.
2. Read `haspMemoryFileId` off a `hasp_read` call site.
3. Set `haspFeature` — `0xffff0000` on every Raw Thrills title so far.
4. Start the game. If it refuses to load its data, it wants real dongle
   answers: add a `hasp` folder and `haspAnswers`.
5. If it starts but behaves as though it is the wrong machine, the cabinet type
   in the dongle memory is wrong.
