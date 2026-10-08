# Superbikes 3

Raw Thrills' Superbikes 3, version 1.91 (64-bit, OS 03.12.11s, built
2021-10-01): a motorbike on a handlebar and a throttle, 1920x1080. Working on
the desktop: attract, races, test menu, keyboard; pads and wheels through
`[EVDEV]` are wired but not tested yet.

## What the dump is

The cabinet runs a **Linux** Unity 2018.4.3f1 build (`fnfmega_Data`, its
code Mono): the dump's `User/unityLog.txt` is the cabinet's own log (NVIDIA's
Linux driver). The dumps that circulate had their player swapped for Unity's
Windows one (`fnfmega.exe`, `UnityPlayer.dll`, `MonoBleedingEdge`), plus
JConfig and a BepInEx plugin (`SB3_JCPlugin`) patching the cabinet's I/O out
for Windows, and **lost the Linux plugins**: `fnfmega_Data/Plugins` holds only
32-bit Windows FMOD DLLs. None of that is used here: the game runs under
Unity's own Linux player, as Nerf Arcade does (see [nerf-arcade.md](nerf-arcade.md)).

The native plugins the game calls (`DllImport`):

| Plugin | What | Here |
|---|---|---|
| `fmod`, `fmodstudio` | FMOD Studio 1.10.16, the sound | FMOD's own 1.10 Linux libraries, with the game |
| `rio` | RIO1 board: switches, ADCs (handlebar, throttle), lamps | `rawthrills/nerf/nerfRio.c` |
| `RIO2` | RIO2 board, tried first | `sb3Rio2.c`: never connected, so the game takes the RIO1 |
| `UnityNatives`, `hasp_linux_23557` | the dongle, reboot info | `rawthrills/nerf/nerf_rt.c` |
| `wheel` | the handlebar's force feedback motor, on the parallel port | `sb3Plugins.c`: its force sent to the steering wheel |
| `WebCamCV` | the camera (photo screen, off by default) | `sb3Plugins.c`: none open |
| `fccore` | a developer's frame recorder | not provided, never called |

## Setting a dump up

1. Unity's 2018.4.3f1 Linux player, with the game:

       tools/unity-player.sh 2018.4.3f1 8a9509a5aff9 "<games>/SuperBikes 3/unity"

2. FMOD Studio API 1.10 for Linux (1.10.20 tested: 1.10.x read the game's
   1.10.16 banks), from fmod.com: its x86_64 `libfmod.so*` and
   `libfmodstudio.so*` in `fnfmega_Data/Plugins/x86_64`, where the cabinet
   had them:

       tar xzf fmodstudioapi11020linux.tar.gz
       mkdir -p "SuperBikes 3/fnfmega_Data/Plugins/x86_64"
       cp -a fmodstudioapi11020linux/api/lowlevel/lib/x86_64/libfmod.so* \
             fmodstudioapi11020linux/api/studio/lib/x86_64/libfmodstudio.so* \
             "SuperBikes 3/fnfmega_Data/Plugins/x86_64/"

## How it runs

As Nerf Arcade: `linuxloader` hands `fnfmega_Data` to `linuxloader64`, which
builds the run tree in `/tmp/linuxloader64-<uid>/fnfmega` and starts the
player with `linuxloader64.so` preloaded. For this game it also:

- links `linuxloader64.so` into `fnfmega_Data/Plugins/x86_64` under the
  plugin names the dump lacks (`librio.so`, `libRIO2.so`,
  `libUnityNatives.so`, `libhasp_linux_23557.so`, `libwheel.so`,
  `libWebCamCV.so`);
- puts that directory on `LD_LIBRARY_PATH` (`libfmodstudio.so` needs
  `libfmod.so.10`) and sets `GLIBC_TUNABLES=glibc.rtld.execstack=2`: FMOD
  1.10's libraries ask for an executable stack, which glibc 2.41 and later
  refuse to `dlopen` otherwise;
- runs in the C locale (as for every Unity game): Mono takes its culture
  from it, and the game's translation tables don't parse with a decimal comma.

## What linuxloader64.so does for it

- **Cabinet files**: `/pm/version.txt` and `/pm/original_version.txt` are the
  dump's own `version.txt`, `original_version.txt`; the rest of `/pm` (the
  updater's `workingAsExpected`, `audits/`) is `LocalData/system/pm`. The udev
  rule and the reboots are handled as for Nerf.
- **Dongle**: version 0 (the game wants <= 2), cabinet type 1 (standard; 2 is
  the motion cabinet), colour 1 (orange), kept in `LocalData/system/dongle`.
- **Calibration**: the dumps' preferences carry no handlebar range (their
  Windows player did without the RIO) and the game boots into its test menu
  uncalibrated. At the RIO's init the full scale is written through Mono:
  handlebar 0..4095 centred on 2048, throttle 0..4095, `CALIBRATED`.
- **The RIO1 handlebar**: the game maps it twice, the RIO1's ADC1 input left
  raw, then its Steer input given the calibrated range but overwritten with
  0..1 -> -1..1 whenever its preferences change (`PrefVals.CheckDirty`, from
  the first frame on). Made for the RIO2's 0..1, that reads the RIO1's raw
  value as full right. `sb3SteerRange` gives ADC1 the calibrated range to
  0..1 through Mono, again every second (a calibration changes it).
- **Keypad**: `RIO_Kpad_SW` answers no key (the game takes 0..11 as held).
- **Force feedback**: the game drives its handlebar motor through the
  parallel port (`libwheel.so`: a byte latched as the force, bit 7 its
  direction): a centering spring it computes from the handlebar's position,
  0.7 of full in a race, 0.2 in the menus, on from the first game's start.
  It is sent as a constant force to the device steering (`ANALOGUE_1`, else
  the first with force feedback; `input/evdevFfb.c`, as the 32-bit games'),
  if it is a wheel: a pad's rumble would buzz at every lean.

## Controls

The game's own keyboard map is its developers', locked (`RTInput`, unlocked
by LShift+RShift+U): the loader feeds the RIO instead.

Desktop (`INPUT_MODE` not 2), while the game's window has the keyboard:

| Key | |
|---|---|
| Left / Right | the handlebar, analog: further the longer held (full in 1 s), back to the middle in 0.3 s when let go |
| Up | throttle; in the test menu also up |
| Down | brake; in the test menu down (the brake is its back button) |
| Enter, S | start |
| 5 | coin |
| V / T | view / music |
| F2 (held) / F1 | test menu / service (back in the test menu) |
| Page Up / Down | volume |
| Esc | quit |

The mouse's buttons are the game's own: left throttle, right brake. The test
menu's calibration works with the keys too, but is never needed.

`[Input] INPUT_MODE = 2`, the `[EVDEV]` inputs as the generator writes them
for the driving games:

| Input | |
|---|---|
| `ANALOGUE_1` | handlebar (stick or wheel) |
| `ANALOGUE_2` | throttle (R2, pedal) |
| `ANALOGUE_3` | brake pedal (L2): the brake switch past half way |
| `PLAYER_1_BUTTON_1` | brake |
| `PLAYER_1_BUTTON_2` / `_3` | view / music |
| `PLAYER_1_BUTTON_START`, `PLAYER_1_COIN`, `PLAYER_2_COIN` | start, coins |
| `TEST_BUTTON`, `PLAYER_1_BUTTON_SERVICE` | test, service |
| `PLAYER_1_BUTTON_UP` / `_DOWN` | volume, the test menu's up and down |

Sources take the generator's forms: `<dev>:KEY:<code>`, `<dev>:ABS:<code>`,
`<dev>:ABS_NEG:<code>` (reversed), and `<dev>:ABS:<code>:MIN|MAX` for a switch
on an axis.

## Running it on the desktop

    GAMES_DIR=... T=600 scripts/linux/runsb3.sh

The game's logs: `User/game_log.txt`, `User/error_log.txt` (the Unity player's
own goes to stdout).

## Not done

- Force feedback on a wheel: checked on the desktop up to what the game
  sends (no wheel here), not yet on a wheel.
- Pads and wheels: wired as the generator writes them for the driving games
  (the rom's name holding "superbikes"), not tested on a cabinet setup yet.
