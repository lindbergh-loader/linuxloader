# Namco ES1 (Dead Heat Riders) — porting notes

State: **2026-10-01.** Boots to its attract mode and shows it, in a desktop
window or fullscreen (`-full`), at the right scale (confirmed on the
desktop). The controls arrive over the JVS board (the game's "JAMMA"
device) through the loader's calibration mapping; the handle and pedals
read their calibrated rest. No sound yet (the `nsAdrv` stub). DHR is a
*different* ES1 platform from Nirin — an X11+GLX window instead of SDL, and
a `HASP_OLD` dongle instead of the linked `hasp_*` — so it takes a separate
branch in `namcoEs1Init` (see "How it differs from Nirin").

External research on the ES1 platform (TPM/MBR protection, the HASP "presence
check", the JVS board, the N2 comparison) is in
[namco-es1-research.md](namco-es1-research.md).

## The platform

Like Nirin, **Dead Heat Riders** (the dump's `info` reads `DH RIDERS
(revision 1247)`) is a **plain 32-bit Linux executable** (`a.elf`), not a
dump: dynamically linked (GLX, GLU, GL, libX11, libusb-1.0, libstdc++), with
its `.symtab` (every function named) and debug info. **No SDL, no OpenAL.**
The interpreter is `/opt/arcade/i686/lib/ld-linux.so.2` (the cabinet's
linker).

Hardware the game talks to (from its imports and symbols):

| What | How | Loader |
|---|---|---|
| Window | X11 + GLX directly (`XOpenDisplay`, `XCreateWindow`, `glXChooseVisual`, `glXCreateContext`, `glXMakeCurrent`, `glXSwapBuffers`) | the game makes its own X11/GLX window (no SDL hook); the loader's `glX*` interposers pass through to the real GLX for it and its GL calls bind to the loader's wrappers — confirmed, see "Booting, in order" |
| Input | the player controls are `clInputDeviceJamma` (`USE_JAMMA_DEVICE`), read from the **n2Jvio** (JVS) board on `/dev/ttyS2` (`clSystemN2`), as in Nirin; the USB "PinKey" device (`clInputDevicePinKey`, libusb) is presumably the key pad board, off in `config.csv` (`USE_KEY_PAD_BOARD_DEVICE FALSE`) | the JVS board is emulated; the analog inputs are put in the game's calibration (see "Controls") |
| Dongle | `HASP_OLD`: `clHASP::Check` (a libusb presence check) + a `clHaspChecker` thread; `hasp_old::vendor_code`, `GetSerialNoString`; `GetUseDongleDevice` is a config toggle | detoured: `clHASP::Check`/`IsError`/`GetErrorType` and `clHaspChecker::ThreadFunc` all answer "no error" |
| Sound | custom `nsAudio` over the `nsAdrv` device driver: it is `dlopen`'d as `./nsAdrv.dll` and its 7 `nsAdrv_*` entry points `dlsym`'d from that handle; the hw-mixer funcs (`nsAdrv_getHwMixerVol`, ...) are compiled into the ELF | the real driver is ALSA-based and wants the cabinet's "surround51" device: absent on a PC, `nsAdrv_init` fails and the game `exit(1)`. **Dump fix:** `nsAdrv.dll` is a no-op stub (the 7 exports return 0), the original kept as `nsAdrv.dll.orig` — no sound |
| Webcam | `clCameraDeviceManager` + the `nmUVCCamera*` V4L2 driver on `/dev/video%d` | a PC webcam plays the cabinet camera (step 9); without one, a present, healthy camera with no picture |
| Network | `eth0`, TSO off, `arping` (link play); the boot's net thread gates start-up on the LAN | the LAN failure path is triggered at the service start (`clLanBasicSessionControler::start`), so the boot passes without a network; `SIOCGIFHWADDR` answered (as for Nirin) |
| Admin | `sudo`/`su` in the scripts, and possibly the game | not run (`namcoEs1DropCommand`) |

Configuration is CSV-driven (`data/csv/config.csv`, `game.csv`, ...):
`GetStrSteeringMargin`, `GetStrAccelMargin`, `GetStrBrakeDigital`,
`GetUseDongleDevice` are `clConfig` getters over it.

## Game dump layout

Flat (no `exec/` subdir as in Nirin); the top level is the game's cwd
(`/opt/arcade/exec` on the cabinet):

```
DeadHeatRiders/
  a.elf       the game (10.9 MB)
  init        cabinet start: `cd /opt/arcade/exec; ./dhr_exec`
  dhr_exec    storage, `xrandr -s 1360x768`, eth0/TSO, `dpkg -i ../deb/*`, `exec ./a.elf -full`
  dhr_storage arcadedisk mounts: maint, save0/save1 (= data0/data1)
  data/       csv/, model/, anime/draw/sprite/shader (textures), sound/, camera/,
              line/ (the courses: chicago, london, sanfrancisco, newyork), testmode/, ...
  save0/, save1/  ghost/, testmode/ (coin*, clock_difference, error_log, ...),
              tmp/najv, usb_csv/
  env/
  nsAdrv.dll  the device driver's Windows name; on Linux the `nsAdrv_*` funcs are in the ELF
  info, locale, postmount, preumount, av0023-arcade.gpg, copyright.sh
  teknoparrot.ini, webcam.log   (left by a TeknoParrot run)
```

The game is started with **`-full`** (fullscreen), not Nirin's `-netuse`;
`dhr_exec` also pins the resolution with `xrandr -s 1360x768` (the cabinet's
mode). The `../deb` packages (`libnet1`, `arping`) are a sibling of the game
directory and are not in the dump.

## Launch

Same interpreter redirect as Nirin (`namco/namcoEs1Launch.c` runs it through
the host's 32-bit `ld-linux.so.2`; LD_PRELOAD still applies). The dump
identifies it; `phdr[2]` is the R+X code segment, the same layout as Nirin,
so the standard partial-CRC id fires:

- Loader id (partial CRC, over `phdr[2].p_vaddr + 10` for `0x4000`):
  **`0x5d3df1cd`**
- File CRC (zlib, launcher / clean-dump table): **`0xda52fa88`**

## How it differs from Nirin

DHR is a *different* ES1 platform; the Nirin path does not carry over.

| | Nirin | DHR |
|---|---|---|
| Window | SDL 1.2 (`SDL_SetVideoMode` hooked) | X11 + GLX, no SDL |
| Input | JVS board (n2Jvio) + SDL joystick | JVS board (n2Jvio), the game's "JAMMA" device |
| Dongle | HASP HL, linked `hasp_*` detoured by address | `HASP_OLD`, `clHASP::Check`, `GetUseDongleDevice` toggle |
| Sound | OpenAL 0.0.8 (OSS `/dev/dsp`) | custom `nsAudio`, hw mixer compiled in |
| Webcam | — | `/dev/video%d` (emulation fallback) |
| Launch arg | `-netuse` | `-full` |

Consequences for the loader:

- `namcoEs1CurrentGame()` / `isNamcoEs1Game()` fire on the id, so `init.c` takes
  the Namco branch (`initJVS` + `namcoEs1Init`). `namcoEs1Init` now branches on
  `windowX11`: Nirin keeps the `SDL_SetVideoMode` hook and the `hasp_*`
  detours; DHR skips the SDL hook and detours the `HASP_OLD` functions
  instead. The shared parts — the JVS board, the evdev/desktop input, the
  `/dev/dsp` audio, `namcoEs1HwAddr` and `namcoEs1DropCommand` — are
  `isNamcoEs1Game`-gated, so DHR inherits them.
- The window: the game makes its own X11/GLX window (no SDL hook).
  Verified on the machine: the game's GL calls forward through the
  loader's wrappers (glad loaded in the `glXMakeCurrent` pass-through)
  and it renders into its own window (`glXGetCurrentDrawable()` is the
  game's GL window, and the main thread swaps through the real GLX).
  The loader's `XMapWindow` used to swallow the map (see step 10).
- The controls come over the emulated JVS board, as for Nirin, with the
  calibration in other variables (see "Controls").
- The dongle is a different HASP API (`HASP_OLD`); it is a presence check,
  detoured to "no error".

## Booting, in order (verified on the machine)

The order the blockers came up, and what each took:

1. Interpreter redirect (as for Nirin) — the game runs through the host
   32-bit linker; `su -c` cabinet commands are not run
   (`namcoEs1DropCommand`).
2. Sound: `nsAudio` `dlopen`s `./nsAdrv.dll` and `dlsym`s its entry points
   from that handle (a preload cannot intercept it). The real driver is
   ALSA-based and opens the cabinet's "surround51" device; on a PC
   `nsAdrv_init` returns < 0 and `CbnusCoreDevice::Init` does `exit(1)`.
   **Dump fix:** the file is a no-op stub (the 7 exports return 0), the
   original kept as `nsAdrv.dll.orig`. No sound.
3. Window: `InitializeXSystem` opens the X window and creates a direct
   GLX context. The loader's `glXCreateContext` returned the (nonexistent)
   SDL context — it now falls back to the real GLX when there is no SDL
   context; `glXSwapBuffers` passes through the same way (a windowX11 game
   manages its own window and context — the pass-through is by state,
   `getSDLWindow()` NULL, not by game id).
4. GL: the game's GL calls bind to the loader's wrappers
   (`glEnable`, `glBindTexture`, ...), which forward to glad's entry
   points — unloaded, because that happens after `SDL_SetVideoMode`, which
   DHR never calls: the first `glEnable` in `InitSystem` (the FSAA path)
   jumped to NULL. glad is now loaded in the loader's `glXMakeCurrent`
   pass-through, when the game's context becomes current
   (`gladLoadGL` needs a current context for its `GL_VERSION` query).
5. Shaders: `clShaderManager` builds 49 shader objects and `exit(0)`s if
   one is not open. 21 would not compile on Mesa (NVIDIA accepted both):
   `texture2DLod()` — not in GLSL 1.20 — and `gl_TexCoord[]` indexed by a
   variable (the built-in is an unsized array). Both are rewritten in the
   loader's `glShaderSource` interposer (`namcoEs1PatchShader`):
   `texture2DLod(s, c, l)` → `texture2D(s, c)`, and a sized
   `gl_TexCoord` redeclaration after the `#version` line (the game
   prepends `#version 120` and `#define LDR 1` when it reads the files, so
   every source has one). All 70 shaders compile.
6. Dongle: `clHASP::Check` and friends detoured to "no error" — it
   passes. The re-check thread (`clHaspChecker::ThreadFunc`) must also be
   *completed*, not just skipped: it is the only code that clears the
   "check pending" flag the boot sequence busy-waits on
   (`clSeqBootThread::Run` spins in `IsCheckEnd()`). `dhrHaspCheckThread`
   reproduces its completion path (no error, check done, under the pimpl
   mutex at `arg+0x0c`).
7. JVS: `clSystemN2::initSystemN2` on `/dev/ttyS2` (the NA-JV emulation,
   as for Nirin) — the game gets through it. The board traffic was decoded
   with `NAMCO_JVS_TRACE`: byte-identical to Nirin's (the same NA-JV board),
   so the JVS path is exonerated and needs no change.
8. Network: the boot fiber ends only when its net thread
   (`clSeqBootNetThread`) dies with the "connect end" flag clear. That flag
   is set by *every* link outcome — a connect **and** the 60-second
   no-machines timeout alike — so a lone cabinet never passes the gate.
   The one clean exit is the LAN *failure* path: a server/client error
   status makes the net thread restart the service, count down 60 frames,
   stop the network and end. `dhrLanSessionStart` triggers it: on
   `clLanBasicSessionControler::start` (the service start shared by the
   `clLanServer`/`clLanClient` wrappers) it never launches the thread — it
   reports the error status on the first start per service ("stopped" on
   the restart) — so the game's own failure handling ends the net thread a
   beat after boot's signal. (The game logs a `NETWORK ERROR` to
   `save0/testmode/error_log.txt` on this path; that is expected.)
9. Camera: the boot records a `CAMERA ERROR` without a camera. With a
   webcam (`cameraWebcam`, as Dead Heat's, see [deadheat.md](deadheat.md)):
   the `nmUVCCamera*` driver is plain V4L2 and runs as is once
   `VIDIOC_S_CROP` is answered (an earlier reading of these notes, that it
   spoke an ABI no stock driver answers, was wrong); `/dev/video%d` gets
   the webcam filter. It opens `/dev/video0` read-write thousands of times
   as a presence check; the loader holds the camera open, so they do not
   wake it. Verified: 640x480 YUYV streamed, the user's face in the game.
   Without a webcam, the manager is answered as a present, healthy camera
   (`IsError`→0, `IsDevice`→1, `nmUVCCameraInitProgress` ends its state
   machine at 9, no device): the preview draws empty, nothing touches a
   device.
10. Window not shown: the loader's `x11Bridge.c` interposes `XMapWindow`
    (and `XPending`) as no-ops for the Lindbergh games, whose window is the
    loader's SDL one, so the game's map requests never reached the server
    and its windows stayed unmapped (the earlier "IsUnviewable" readings
    were a tool printing map states 1 and 2 the wrong way round). Both now
    pass through when the loader made no SDL window (`gettingGPUVendor`
    still set — the same test `XOpenDisplay`/`XCreateWindow` already used).
    The game also polls its own X events through them.
11. Black screen after the publisher's logo: the attract's live-camera demo
    (`clSeqSeqLiveCameraDemoThread::Run`) starts by asking for a dongle
    re-check (`clHaspChecker::Check`, which only sets "check pending" for
    the checker thread) and waits on `IsCheckEnd()`. The thread is replaced
    (step 6) and gone by then, so the flag stayed set. `clHaspChecker::Check`
    is now a no-op too: the last check ("no error") stands.
12. Scale: `data/csv/config.csv` gives `MAIN_*SIZE` 1360x768 (the window
    and the 2D layout) and `VIDEO_*SIZE` 1920x1080 (the output). When they
    differ, the game renders at the main size and its last pass
    (`clGraphics::flipSequence`) scales it into an output-sized viewport —
    which needs an output-sized window, while `InitSystem` opens one of the
    main size: the picture was zoomed and shifted right/down. The two loads
    in `InitSystem` (`0x8054f02`, `0x8054f0b`) are re-pointed from
    `SCREEN_W/H` to `SCREEN_REAL_W/H`, so the window is opened at the output
    size (`windowAtOutputSize`). The output size is the loader's
    [Display] WIDTH/HEIGHT (step 13).

13. Display: main's call of `InitSystem(bool fullscreen, bool)`
    (`0x80552db`) goes through the loader (`initSystemCall`), which sets
    the output size (`SCREEN_REAL`) to [Display] WIDTH x HEIGHT (1360x768
    without a configuration) — the window follows, see step 12 — and
    passes fullscreen with `-full` or [Display] FULLSCREEN. Verified at
    1280x720.
14. Sound: the same driver interface as Dead Heat's; the loader's SDL3
    emulation (`soundEmulation`, see [deadheat.md](deadheat.md)) answers
    the game's `dlsym`s, whatever `nsAdrv.dll` is (the dump's no-op stub
    included): a 6-channel 32-bit 48 kHz stream plays.

## Running on the desktop

`scripts/linux/rundhr.sh` runs it in a window (the game without `-full`
asks for a normal, managed window); `FULL=1` passes the cabinet's `-full`,
which covers the whole screen with an override-redirect window (nothing
else is visible, as on the cabinet). The process shows as `ld-linux.so.2`,
not `a.elf`. `/proc/sys/kernel/yama/ptrace_scope` is 1 here: debug it as a
child of gdb (`run ./a.elf` on `/lib/ld-linux.so.2`), not by attaching.

## Controls

`clInputDeviceJamma::update` reads the board's state from `clSystemN2`:

- Handle: analog channel 0 (`0x8f9c4a0`, 16-bit); accelerator: channel 1
  (`0x8f9c4a2`); brake: channel 2 (`0x8f9c4a4`) — but `config.csv` sets
  `STR_BRAKE_DIGITAL TRUE`, so the brake is the switch `0x80` of the
  player word (P1 button 3) instead.
- Calibration: `clInputDeviceJamma::sm_*` statics (16 bytes apart from
  `sm_handle_center` at `0x86a9320`: left max, right max, ..., accel
  rest/max at +0x50/+0x60, brake at +0x70/+0x80), loaded from the save.
  The descriptor's `jammaCalibration` points there and `calibrate()` maps
  the loader's steering/pedals into it, as for Nirin's `clHandleSetting`.
  Verified live: raw handle 33024 = the saved centre, accel at its rest.
- Switches: the player word `0x8f9c3e0` is `(byte0 << 8) | byte1` of the
  JVS player 1 switches; the game maps 10 of its bits (table `0x84b95a0`)
  and the system byte's test bit.
- TeknoParrot's profile (`GameProfiles/DeadHeatRiders.xml`): Test, Service,
  Coin, Enter = P1 Button1, Menu Up/Down = P1 Up/Down, Brake = P1 Button3,
  Nitrous = P1 Button4, View Change = P1 Button5, wheel = analog 0, gas =
  analog 1 (TP's "Analog2").

So the loader mapping is Nirin's (`ANALOGUE_1` handle, `ANALOGUE_2` gas)
plus `PLAYER_1_BUTTON_3` (brake), `_4` (nitro), `_5` (view). Without evdev
the desktop keys drive it: ←/→ steer, ↑ accelerates, ↓ brakes (its brake
switch), space Enter, Z/X/C/V buttons 2-5, 1 start, 5 coin, F1 service, F2
test. On Batocera the generator's kind `dhriders` maps a pad or wheel (see
[deadheat.md](deadheat.md)).

## Open items

1. Drive it with real controls (evdev wheel/pedals or a pad) and confirm
   the buttons' mapping in the game's I/O test; deploy the generator to
   the Batocera machine.

The `HASP_OLD` dongle is detoured (it is a presence check — "one patch, or
even a config change", per [namco-es1-research.md](namco-es1-research.md)),
and the descriptor + `gameData` entry + init path are in place, so those are
no longer open.
