# Nerf Arcade

Raw Thrills' Nerf Arcade (2018), version 1.55 (OS 04.02c, built 2024-08-23):
two mounted blasters, 1920x1080. Working on the desktop: attract, game,
test menu, mouse and keys; two evdev guns are wired but not tested yet.

## What the dump is

The cabinet runs a **Linux** Unity 2018.2.21f1 build: the game's code is Mono
(`Nerf_Data/Managed/Assembly-CSharp.dll`) and its native plugins are 64-bit
Linux ELFs (`Nerf_Data/Plugins/x86_64`). The dumps that circulate had their
player swapped for Unity's Windows one (`Nerf.exe`, `UnityPlayer.dll`,
`MonoBleedingEdge/EmbedRuntime`), plus a BepInEx plugin (DemulShooter's)
patching the cabinet's I/O out for Windows. None of that is used here: the
game runs under Unity's own Linux player of its version, with the dump's
`Nerf_Data`.

The plugins the game drives the cabinet through:

| Plugin | What | Here |
|---|---|---|
| `librio.so` | RIO1 board: switches, the guns' potentiometers (12-bit ADCs), lamps, solenoids, meters | `nerfRio.c` |
| `libUnityNatives.so` | the dongle's identity (through delegates into the HASP library), tickets owed, coin audit backup, periodic reboot | `nerf_rt.c` |
| `libhasp_linux_23714.so` | Sentinel HASP | `nerf_rt.c` (never really called) |
| `libRIO2.so` | RIO2 board | not loaded, the game only uses RIO1 |
| `libPK-UnityPlugin.so` | PopcornFX particles | the game's own |
| `ScreenSelector.so` | Unity's resolution dialog | the player's own |

Do not load the dump's `libUnityNatives.so`: it CRC-checks the game's files
and runs obfuscated commands through `system()` when it finds them changed.

## How it runs

`linuxloader` sees a `*_Data` folder in the game's directory and hands the
game to `linuxloader64` (`src/loader/loader64`), which:

1. reads the Unity version from `Nerf_Data/globalgamemanagers`, and finds the
   player with the game, in its `unity/2018.2.21f1/` (or `unity/`), else in
   `unity/2018.2.21f1/` beside the loader (`$LINUXLOADER_UNITY` before both);
2. makes `/tmp/linuxloader64-<uid>/Nerf`: the player as `Nerf.x86_64`,
   `Nerf_Data` linked file by file with the player's `MonoBleedingEdge`,
   `Plugins/x86_64` with `librio.so`, `libUnityNatives.so` and
   `libhasp_linux_23714.so` linked to `linuxloader64.so`, and the rest of the
   game's directory linked (`LocalData`, the game's state, `version.txt`);
3. starts it there, `linuxloader64.so` preloaded, at 1920x1080.

The player is Unity's "Linux Build Support" for 2018.2.21f1, variation
`linux64_withgfx_nondevelopment_mono`:

    tools/unity-player.sh 2018.2.21f1 a122f5dc316d "<games>/Nerf Arcade/unity"

It goes with the game, on Batocera too: `Nerf Arcade/unity/2018.2.21f1` in
the rom's directory (or squashfs image), not in the loader's.

### 1920x1080 only

The shots go through the reticle's position on its 1920x1080 canvas, taken as
screen pixels (`PlayerReticle.LateUpdate`, `ScreenPointToRay`): at any other
size the reticle and the shots part. `linuxloader64` always asks for
1920x1080; fullscreen (`[Display] FULLSCREEN = 1`, what the generator sets)
lets Unity scale that to the screen. A screen that isn't 16:9 is untested
(Unity's black bars would need the gun aim mapped into the picture).

## What linuxloader64.so does for it

- **RIO1**: each switch reports a count that every press and release bumps,
  its low bit the state; the ADCs 0..4095, X from the left, Y from the bottom.
- **Calibration**: the game scales the ADCs by the range its test menu stored
  (preferences 67..74) and boots into the test menu without one. The full
  scale is written there through Mono's embedding API at the RIO's init, then
  `IOManager.GrabCalibrationFromPreferences()`.
- **Dongle**: version 0 (the game wants <= 0), serial 12345, cabinet type 0,
  template 1 (USA coin; `NERF_CAB_TEMPLATE` for another); what the game writes
  is kept in `LocalData/system/dongle`.
- **The cabinet's system**: at boot the game writes a udev rule
  (`/etc/udev/rules.d/10-usb-automount.rules`) and `/pm/FlushBuffers.sh`
  unless they exist, then reloads udev: both are made in `LocalData/system`
  and the paths mapped there. The periodic reboot never comes
  (`GetPeriodicRebootInfo`), and `shutdown`, `reboot`, `udevadm`, `mount`
  through `system()` are not run.
- **Mono and the hooks**: the player loads Mono with `RTLD_DEEPBIND`, which
  sends its libc calls past the preload: the flag is dropped.
- **Gun border** (`[Display] BORDER_ENABLED`, `WHITE_BORDER_PERCENTAGE`,
  `BLACK_BORDER_PERCENTAGE`): the player opens libGL itself and looks
  `glXSwapBuffers` up by `dlsym`; its own `dlsym` import slot is pointed at
  a wrapper handing it the loader's swap, which draws the border.

## Controls

`[Input] INPUT_MODE = 2`: the `[EVDEV]` inputs, as the generator writes them
for the gun games:

| | Player 1 | Player 2 |
|---|---|---|
| Aim X / Y | `ANALOGUE_1` / `ANALOGUE_2` | `ANALOGUE_3` / `ANALOGUE_4` |
| Trigger | `PLAYER_1_BUTTON_1` | `PLAYER_2_BUTTON_1` |
| Shoulder button | `PLAYER_1_BUTTON_2` | `PLAYER_2_BUTTON_2` |
| Start / coin | `PLAYER_1_BUTTON_START` / `PLAYER_1_COIN` | `PLAYER_2_BUTTON_START` / `PLAYER_2_COIN` |

plus `TEST_BUTTON`, `PLAYER_1_BUTTON_SERVICE` and `PLAYER_1_BUTTON_UP/DOWN`
(volume, the test menu's moves). An aim is `<device>:ABS:<code>` (a gun) or
`<device>:REL:<code>` (a mouse).

Otherwise the desktop: the mouse in the window is P1's gun (left button the
trigger, right the shoulder button), 5/6 coins, 1/2 starts, F2 test (held:
the test menu), F1 service, Page Up/Down volume. Esc quits.

## Running it on the desktop

    GAMES_DIR=... scripts/linux/runnerf.sh            # windowless test harness
    LD_LIBRARY_PATH=libs/linux_x86 build-linux/linuxloader -g "<dir>/Nerf Arcade" -c linuxloader.ini

Unity's own log: `~/.config/unity3d/RawThrills/Nerf/Player.log`.
