# Namco System ES1 / N2 — external research notes

Reference material gathered from two public reverse-engineering write-ups,
distilled for the linuxloader Namco ES1 port (Nirin, Dead Heat, Dead Heat
Riders). These are third-party observations, not verified against our dumps;
use them as leads, then confirm against the ELF / machine.

Sources:

- **ValdikSS**, *"Researching protection and recovering Namco System ES1
  arcades"*, Medium / habrahabr, 2016 (updated 2018).
  <https://medium.com/@ValdikSS/researching-protection-and-recovering-namco-system-es1-arcades-1f8423fdeb3b>
  (Russian original: <https://habrahabr.ru/post/304014/>)
- **TheArcadeGuy (dropkickshell)**, *"System N2 Exposed — A Tale of Reversing
  an NVIDIA/Namco Arcade Platform"*, Medium, 2016.
  <https://medium.com/@dropkickshell/system-n2-exposed-5395436c824d>

The first is about **System ES1** (the platform DHR / Nirin / Dead Heat run
on). The second is about **System N2** (a different, earlier NVIDIA-based
platform, Wangan Midnight), useful mostly for the shared HASP-dongle and JVS
approaches.

---

## 1. System ES1 hardware

DHR runs on **System ES1**, a near-stock Intel PC:

- Motherboard: Supermicro C2SBM-Q (Intel **Q35** + ICH9DO)
- CPU: Intel Core 2 Duo E8400 @ 3.00 GHz
- RAM: 2 × 512 MB DDR2 800
- Video: NVIDIA GeForce **9600 GT** 512 MB GDDR3
- HDD: 160 GB (Seagate Barracuda 7200.12 / Hitachi Deskstar 7K1000)
- OS: **Arcade Linux** (Debian 4.0 based)

There are 9 ES1 titles; the non-JP ones seen in the wild include **Tank! Tank!
Tank!**, **Dead Heat**, **Dead Heat Riders** and **Nirin** (the game we have
already ported).

> The ES1 is *not* the N2. N2 (below) is the nForce2 / Wangan platform. Our
> DHR dump (`a.elf`, flat cwd = `/opt/arcade/exec`) is ES1.

## 2. ES1 protection — three stages

1. **Trusted Boot + TPM 1.2** (soldered to the board). Game files are
   AES-256-CBC encrypted via the loop-AES kernel module, with the key bound
   to the TPM **PCR** values. Any change to BIOS / bootloader / kernel /
   command line changes the PCRs and breaks decryption. There is also an
   encrypted **LUKS** partition holding the game, updates and save data.
2. **HDD copy protection** — the MBR **Disk Signature field is zeroed**.
   Attach the drive to Windows and it "helpfully" writes a random signature,
   which changes the measured MBR → PCR mismatch → decrypt fails. (Every
   Linux partitioning tool writes a random signature, so a normal image copy
   breaks it too.)
3. **HASP HL Max USB dongle** — the game "only checks its presence". ValdikSS
   calls it "stupid, wasteful" and says it "could be mitigated by literally
   **one patch, or even by modifying configuration game file**."

> **Relevant to us:** this directly confirms the DHR dongle strategy in
> `docs/dhriders.md` — the `HASP_OLD` check is a presence check, not a
> crypto gate. Detouring `clHASP::Check()` to "no error" (and no-op'ing the
> `clHaspChecker` thread) is the "one patch" he describes.

### Recovery / dump methods (context, not needed for the port)

- **[res1gn](https://github.com/shizmob/res1gn)** (WTFPL, archived 2025;
  migrated to
  [codeberg.org/shiz/res1gn](https://codeberg.org/shiz/res1gn)) — a
  **pure-software** exploit for the ES1's TPM sealing, no hardware needed.
  The initrd is stock Debian **Casper**, which union-mounts *every* image in
  `/live/`, not just the measured `filesystem.squashfs`: drop a
  `res1gn.[34].squashfs` (or `res1gn.dir`) that sorts after it in `/live/`
  and its files silently override the original rootfs **without changing any
  PCR**. The PoC replaces `/sbin/init` to dump the game key from the TPM and
  decrypt the `.apps` container, then re-triggers the ES1's own recovery
  process to re-seal a drive "like in the factory". It also documents the
  full measured boot chain (BIOS PCR0-3, GRUB-IMA PCR4/5, kernel/initrd/cmdline
  + rootfs PCR8, `config`+`partab` PCR9; ES1(A2) adds `arcadeboot` PCR8 and a
  GPG key PCR15) and Namco's 2011 fix (Arcade Linux ≥ 0.8.5 hardcodes the
  image list when a rootfs digest is passed). Useful as the definitive map of
  what the PCRs actually protect.
- **FireWire DMA attack** (Inception) to dump RAM, then find the AES key with
  `aeskeyfind` (Princeton) and decrypt the loop-AES squashfs.
- **Fix a bricked drive** by zeroing the 4-byte MBR signature at offset
  `0x1B8`: `dd if=/dev/zero of=/dev/sdX bs=1 count=4 seek=440`.
- **Re-seal**: delete the encrypted `*.apps` + `sealkey` from the first
  partition's `arcade/` dir, drop an empty `RECOVERY` file, reboot → the OS
  re-encrypts.
- **Nirin-specific (2018 update):** Nirin was the first ES1 game and is the
  weak one — it does **not** use TPM-bound encryption; it uses the
  **SHA1-hash of the Trusted-Boot PCR state** as the file key. It can be
  dumped by editing `filesystem.squashfs` to save
  `/sys/kernel/security/tpm0/binary_bios_measurements` and
  `/sys/class/tpm/tpm0/pcrs` and reconstructing the PCR state — no DMA attack
  needed. (Tool: `ValdikSS/binary_bios_measurements_parser`.)
- BIOS/GRUB passwords reportedly `016ystn` or `arcade`.

## 3. System N2 (Wangan platform) — shared techniques

N2 is a different machine (nForce2, MSI K7N2GM-IL board, IDE HDD, no
bootloader on the MBR) but shares the two tricks we care about.

### Disk layout (the N2 game root, `v337`)

- `data/` — game data
- `etc/` — hasp daemon, perl scripts, file-checker, restore script
- `ext/` — ruby scripts, partition manager
- `libso/` — shared objects (libCg, boost, …)
- `modules/` — kmods (forcedeth, nvidia, nvsound, r8169, xr17c15x)
- `main` — **the game executable (28 MB, unstripped)**
- `.exerc` — startup script
- `achemy.ini` — game engine config file

### Window / graphics

> "it uses a wrapper around glx libraries and inits vt0 and tries to create
> its own screen… no SDL or X11 stuff here (**common in the later games**)."

The parenthetical is the useful bit: the *later* platform (ES1, i.e. DHR)
moved to real X11/GLX. This matches our disassembly of DHR's
`InitializeXSystem` (raw `XOpenDisplay` / `glXChooseVisual` /
`glXCreateContext` / `XCreateWindow` / `XMapWindow`) — DHR creates its own
X11/GLX window, so the loader's Nirin `SDL_SetVideoMode` hook does **not**
apply; the window path for DHR is the open item in `docs/dhriders.md`.

### JVS I/O board

- The N2 board is the **NAMCO FCA-1 JVS** (Namco later switched to the
  **NA-JV**). It behaves like a standard JVS I/O board with extra commands
  for PL devices and MagCard / hoppers.
- Connected over a USB-A port but exposed in Linux as a **serial** device,
  usually `/dev/ttyS2` or `/dev/ttyS4` ("games may vary their endpoints").
- "can be emulated fairly easily if you have a generic JVS emulator and can
  fill out some of the namco specifics."
- [OpenJVS](https://github.com/openjvs/openjvs) is a JVS I/O board emulator
  (C, GPL-3) for USB-RS485 / HAT hardware: Naomi, Triforce, Chihiro,
  Lindbergh, System 22/23, exA-Arcadia... Its `src/jvs/` is the same
  Lindbergh-derived JVS dialect as our `hardware/lindbergh/jvs.c` (same
  commands, same checksum), a useful cross-reference for the packet layer —
  but it targets those boards' JVS, **not** Namco's FCA-1/NA-JV, whose
  Namco-specific bits (the `0x70` commands, the board's expectations of the
  master) still have to be worked out against a real ES1 master.

> **Relevant to us:** DHR's `clSystemN2::initSystemN2` calls the `n2Jvio*`
> API and there is an `open "/dev/ttyS2"` string — i.e. the **same n2Jvio
> JVS-on-serial model** as Nirin. The loader's existing JVS emulation
> (`hardware/lindbergh/jvs.c`) should apply, selected via the `gameData`
> entry's `jvsIOType` (Nirin uses `NAMCO_NA_JV`), with the port set in the
> `NamcoEs1Game.jvsDevice` field.

### HASP dongle (N2)

- A **HASP HL MAX** (or HASP RTC) dongle, used to decrypt the one AE-v3 file
  (`aeprofile`) and holding R/W memory for card stats / region / cabinet type.
- "trivial as well via **shared library injection** if you know the few items
  they read from it."

> Same shape as ES1: a presence/serial check we bypass by detour, not by
> emulating the crypto.

## 4. Bottom line for the DHR port

| Concern | Evidence | Action |
|---|---|---|
| Dongle | ES1: "only checks presence… one patch / config change"; N2: "shared lib injection" | Detour `clHASP::Check()` (clear error `this+0x24`) + no-op `clHaspChecker::ThreadFunc` |
| JVS board | N2 FCA-1→NA-JV on `/dev/ttyS2/4`; DHR uses `n2Jvio*` + `open "/dev/ttyS2"` | Reuse `jvs.c` JVS emulation; set `jvsIOType` (NA-JV) via the `gameData` entry, port in `NamcoEs1Game.jvsDevice` |
| Window | N2 game makes its own screen, "no SDL/X11 (common in later games)"; DHR `InitializeXSystem` is raw X11/GLX | Do **not** hook `SDL_SetVideoMode`; let DHR make its own X11/GLX window — window/GL-interposer behaviour is the open item |
| Dump origin | ES1: TPM + zeroed-MBR + loop-AES; Nirin: SHA1-of-PCR only | Our `a.elf` is already decrypted/unpacked; identification is by code-segment CRC, not file name |
