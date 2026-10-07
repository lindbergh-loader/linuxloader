# Namco ES1 (Dead Heat) — porting notes

State: **2026-10-01.** Boots to its attract mode on the desktop (confirmed on
screen), with sound through the loader's emulation of its sound driver; the
controls come over the emulated JVS board, the handle and pedals at their
calibrated rest (read live). Not yet driven with real controls.

Dead Heat (the dump's `info`: `US DRIVE (revision 6273)`) is the older
sibling of [Dead Heat Riders](dhriders.md), and sits between it and
[Nirin](namco-es1.md): Nirin's platform (an SDL 1.2 window, the linked HASP
HL `hasp_*`) with DHR's boot sequence (LAN gate, live camera, the JAMMA
input device). Its own twist: it was built with **Intel's compiler**.

- Dump: `<games>/Dead Heat/` (from
  `Dead Heat.wsquashfs`), flat as DHR's; started with `-full` by
  `us_drive_exec`. A scene release: `dh_emu.c`/`dh_emu.so` (a preload with
  HASP detours, not used) and its `README`; `config.csv` was edited for a PC
  (`USE_JAMMA_DEVICE FALSE`, `VIDEO_*SIZE` 1920x1080).
- Plain 32-bit ELF, not stripped, with debug info; interpreter
  `/lib/ld-linux.so.2` (no linker redirect needed).
- Loader id (partial CRC) **`0x7a3a5554`** (`DEADHEAT_ES1`), file CRC
  `0x7a18beb3`.
- Desktop run: `scripts/linux/rundh.sh` (windowed; `FULL=1` for `-full`).

## Booting, in order

1. **Intel compiler CPU check.** `main` first calls
   `__intel_new_proc_init_T`, which wants `__intel_cpu_indicator`
   (`0x92ff24c`) at the SSSE3 level (`0x1000`); the compiler's runtime sets
   it from cpuid on "GenuineIntel" CPUs only and leaves others at "generic"
   (1): "This program was not built to run on the processor in your
   system". On another vendor's CPU with SSSE3 the loader raises it to
   `0x1000` (`intelCpuIndicator`), which the dispatched memcpy/memset are
   also fine with.
2. **Stack alignment.** ICC's code keeps a 4-byte-aligned stack (`main`
   aligns to 128, then calls with a `push`), while today's 32-bit libraries
   assume the i386 ABI's 16 bytes and spill SSE/AVX values with aligned
   moves: `std::istream::seekg` faulted on `vmovdqa` in the first CSV read
   (and "stack smashing detected" before that). Every import of the game,
   and of the libraries it `dlopen`s (its sound driver, built the same way),
   goes through a stub that aligns the stack, copies 16 words of arguments
   and calls the real function (`namco/namcoEs1Align.c`, `alignStack`). The
   stub handles callee-popped struct-return pointers and carries CFI for
   C++ exceptions; `setjmp`-like functions stay direct. Lazy slots are
   bound by the loader first (the executable's own PLT entries, for
   functions whose address it takes, are skipped).
3. **Sound.** `CbnusCoreDevice::Init` `dlopen`s `./nsAdrv.dll` and
   `dlsym`s its 7 entry points; the real driver wants the cabinet's ALSA
   "surround51" and fails (`cannot initialize hardware parameter
   structure`), and the game exits. The game's `dlsym` is hooked and gets
   the loader's driver instead (`namco/namcoEs1Sound.c`, `soundEmulation`):
   `nsAdrv_init("default", "surround51", 6 ch, 32 bits, 48000 Hz, 256-frame
   periods, mixer state)`, then a worker loop of `nsAdrv_wait` (free
   frames), `nsAdrv_write` (256 frames, S32, ALSA 5.1 order, remapped to
   SDL's), `nsAdrv_start` once, `nsAdrv_mixup`. It plays on an SDL3 stream;
   the hardware mixer reports none (`nsAdrv_mixsts` 0). With no audio
   device the game runs silent at the same pace.
4. **Shaders.** The sources go through `glShaderSourceARB` (now patched
   like `glShaderSource`), and have no `#version` line (GLSL 1.10, with the
   game's `#define LDR 1`): `namcoEs1PatchShader` puts the sized `gl_TexCoord`
   declaration at the top for them. All compile.
5. **Boot gates**, as DHR's (same code, other addresses): the LAN start
   (`clLanBasicSessionControler::start` `0x80fecf0`, status at `+0x6c`)
   and the camera (`IsError` `0x8087370`, `IsDevice` `0x80869d0`,
   `nmUVCCameraInitProgress` `0x80a6570`, its state `0x9116be0`, 9 = done).
   The linked HASP HL is Nirin's (`hasp_login` with `0xffff0000`), and its
   `clHaspChecker` thread then runs as is.
6. **Scale and display.** As DHR: `MAIN_*SIZE` 1360x768 (the layout) vs
   `VIDEO_*SIZE` (the output, `SCREEN_REAL_W/H` at `0x87e50c4`), the last
   pass scaling the picture to the output. The loader sets the output to
   [Display] WIDTH x HEIGHT (1360x768 without a configuration) in its
   `SDL_SetVideoMode` hook (config.csv is read by then) and opens the window
   at that size. Verified at 1280x720: the whole attract loop, at scale.

## Webcam

The live camera (the "Your faces in the game!" photo, the faces on the
cars) works with a PC webcam, as the scene release's `DH_CAMERAFIX` hinted:
the game's `nmUVCCamera*` driver is plain V4L2 (QUERYCAP, CROPCAP, S_CROP,
S_FMT 640x480 YUYV, REQBUFS/QUERYBUF of 2 mmap buffers, QBUF/DQBUF,
STREAMON/OFF). Three things for a webcam (`cameraWebcam`):

- `/dev/video0` (the only node it opens) is the first colour camera: the
  loader's webcam filter (`common/webcam.c`, Cruis'n Blast's) now covers the
  Namco games, hides the infrared and metadata nodes and holds the camera
  open.
- `VIDIOC_S_CROP` is answered with success (a webcam may not crop; the
  whole picture is used).
- The driver wants a capture buffer *larger* than a 640x480 YUYV picture
  (`cmp $0x96000; jbe`, `0x80a685f`; the cabinet camera's were padded); a
  webcam's is exactly that size: `jbe` becomes `jb`. (The scene patch
  accepted any size.)

Without a webcam, the boot gate's stand-in (a healthy camera with no
picture) is used as before. Verified: 640x480 YUYV streamed, the user's
face in the game.

## Controls

The same JAMMA device as DHR (`clInputDeviceJamma::update`, raw analog
inputs at `0x9033540`/`42`/`44`, player switches `0x9033480`), with two
differences:

- `config.csv` here has `USE_JAMMA_DEVICE FALSE`, so `clSystemN2::init`
  never opened the JVS board. The loader turns its argument load
  (`0x808bf5d`, `movzbl 0x8(%esp),%eax`) into `mov $1,%eax`
  (`jammaForce`): the JVS board is used whatever the file says.
- The calibration statics are 4 bytes apart (`jammaCalibrationStride`),
  with no brake statics (the brake is a switch, `STR_BRAKE_DIGITAL`). The
  dump was never calibrated: `setTestModeSetting()` applies zeros. When
  the live calibration has no range, the loader puts back the game's
  built-in defaults (centre 28600, left 4000, right 55000, accel
  5900..60000), saved before `main`.

- The brake: with `STR_BRAKE_DIGITAL` (TRUE in this dump) it is the switch
  `0x80` of the player word, P1 button 3: the cabinet's brake button, as
  played (2026-10-26). The loader leaves it so (`brakeSwitch`: the
  keyboard's ↓ presses it). Turning that setting's load (`0x8079d32`) into
  a "false" made the brake the pedal on channel 2 (scaled by the fixed
  `m_left_pedal_std`/`_w`, 29000, width 20480) and left no brake button.

Verified live: the JVS traffic runs (reset, addressing, polling), the raw
handle reads 28600 (centre), the accelerator 5900 (rest), the brake 29000
(rest).

The cabinet's mapping, as played (2026-10-26): wheel channel 0
(`ANALOGUE_1`), gas 1 (`ANALOGUE_2`); Enter P1 button 1, brake P1 button 3,
nitrous P1 button 4, view P1 button 5, menu up/down P1 up/down, shift
up/down P2 up/down. TeknoParrot's `DeadHeat.xml` has View on button 2,
Nitrous on 3 and an analog brake on channel 2 (and an H shifter on P2
buttons 1-4).

Without evdev (the desktop keys): ←/→ steer (full lock in a quarter of a
second, back to the centre as fast), ↑ accelerates, ↓ brakes; space is
button 1 (Enter), Z/X/C/V buttons 2-5 (X the brake, C nitrous, V the
view); 1 start, 5 coin, F1 service, F2 test. The same holds for DHR (whose ↓ also presses its brake switch).

On Batocera the generator (`linuxloaderGenerator.py`, kind `deadheat`)
maps the left stick or wheel, R2 gas, L2 and x the brake button (button 3),
b Enter, y nitrous (4), a view (5), L1/R1 (paddles) shift down/up, up/down
the menus (Maximum Heat 3D the same); the rom is
the dump's directory (`a.elf` at its top) with `lib/libSDL-1.2.so.0`
(sdl12-compat, as Nirin's). DHR is kind `dhriders`: stick/wheel, R2 gas,
L2 its brake switch (P1 button 3), b Enter, y nitrous (4), a view (5).

## Steering force feedback

The steering board (clKickback) is only created by
`clSeqBootSteerDeviceThread` when config.csv's `USE_JAMMA_DEVICE` is set
(clConfig + 1, read there itself, not through `jammaForce`'s load): FALSE in
this dump, so the game had no board and set no force at all. Both its loads
(`Run()` and the fiber's inlined copy) are made a "true" (`steerDeviceForce`)
and the board is emulated as Maximum Heat 3D's (`/dev/ttyS1`, GOUT0 power,
the self-check answered).

The force (`namco/namcoFfb.c`, shared with DHR, Maximum Heat 3D and WMMT3)
is read from the clKickback object every 8 ms: spring, viscosity and
reflect as the game's setters left them (`+0x48..0x4a`, the centre offset
at `+0x2c`), not from the board's command, which `exec()` scales by the
board's power-on ramp (0 in DHR, which has no board). They go to the evdev
wheel (`input/evdevFfb.c`) as FFB Arcade Plugin's DeadHeat.cpp has them,
with two changes for a direct drive wheel: the viscosity is a damper, not a
friction (which buzzes at rest), and the spring and viscosity are / 254
(the Pacloader fork's scale), not / 63, the 0x1f the game sets from
`onTrq()` being a full spring and friction there. Verified on Batocera with
a Logitech PRO Racing Wheel. `NAMCO_FFB_TRACE=1`: the values on stderr.

## Open items

1. Real controls (evdev wheel/pedals/pad) and the button mapping in the I/O
   test; the Batocera deployment (generator in the local rgs tree, not yet
   on the machine).
