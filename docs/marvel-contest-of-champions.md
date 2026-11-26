# Marvel Contest of Champions

Raw Thrills' Marvel Contest of Champions arcade (Kabam's game), version
MCoC2 2.062 (OS 04.08, built 2025-09-23): a two-player fighting game, a
stick and four attack buttons (light, medium, heavy, special) per player.
Players scan their hero cards with a camera, and each game gives out a card
from a dispenser. Working on the desktop: attract, coins, the upgrade screen,
the champions' selection, quests and fights.

## The dump

The dump is a disk image in HDD Raw Copy Tool's compressed format,
`marvels_coc_2.062_no_luks_no_dongle_almost.imgc`, of the cabinet's 128 GB SSD.
The cabinet's LUKS layer had already been removed: a GPT disk with BIOS boot,
EFI and an ext4 root. The game is the root's `/pm`:

    cc -O2 -o imgc-extract tools/imgc-extract.c
    ./imgc-extract marvels_coc_2.062_no_luks_no_dongle_almost.imgc marvels_coc.img   # sparse, 18 GB used
    debugfs -R "rdump /pm ." "marvels_coc.img?offset=$((1054720 * 512))"
    mv pm "Marvel Contest of Champions"

The cabinet runs the game's **Linux** Unity 2021.3.23f1 build: Mono
(`mcoc2_Data/Managed`, `MonoBleedingEdge`), Unity's launcher
`mcoc2.x86_64` beside `UnityPlayer.so`. The executable, `ul` (the cabinet's
launcher) and several plugins are wrapped in Sentinel LDK envelopes asking for
the cabinet's HASP dongle. The dumper ("almost") put Unity's plain launcher in
place of the game's executable (kept as `mcoc2.x86_64_orig`) and a plain
`libUnityNatives.so` from another Raw Thrills game (keeping
`libUnityNatives_orig.so`), and got it to attract on a PC without the I/O.

## How it runs

As the other Raw Thrills Unity games (see [nerf-arcade.md](nerf-arcade.md)):
`linuxloader` hands `mcoc2_Data` to `linuxloader64`, which builds the run tree
in `/tmp/linuxloader64-<uid>/mcoc2` and starts the player with
`linuxloader64.so` preloaded. This dump carries its own Linux player, which is
used: no Unity player to fetch. The cabinet's plugins the library stands in
for, linked under their names in the tree's `mcoc2_Data/Plugins`:

| Plugin | What | Here |
|---|---|---|
| `libRIO2.so` | the RIO2 I/O board: switches, the sticks' switch matrix, lamps, coin meter, the card dispensers' signals | `rawthrills/unity/unityRio2.c`: a virtual board (pins and matrix: `rawthrills/mcoc/mcocGame.c`), connected |
| `libUnityNatives.so` | the dongle: serial number, cabinet type, cards the dispensers may give out, the operator's smart cards' encryption | `rawthrills/mcoc/mcocPlugins.c`, kept in `LocalData/system/mcoc-dongle` |
| `libhasp_linux_x86_64_25328.so` | the HASP library (one dummy `hasp_login`) | `rawthrills/unity/unityRt.c` |
| `libUnitySCard.so`, `libSCard.so` | the operator's smart card reader (card authorisation, in the test menu) | `mcocPlugins.c`: no card |
| `libWebCamRT.so` | the two cameras reading the hero cards | `rawthrills/mcoc/mcocWebcam.c`: the webcams, over V4L2 (below); and a card binder (`mcocBinder.cpp`) |
| `librio.so` | the RIO1, which this game never calls | `rawthrills/unity/unityRio.c` |
| `libOrenVid.so`, `ScreenSelector.so`, `lib_burst_generated.so` | video, Unity's | the game's own |
| `libWeChatCURL.so` | WeChat login (China) | the game's own, which does not load: an exception at boot, as on the cabinet, harmless |

The `*_orig.so` copies, still enveloped, are left out of the tree: Unity loads
every plugin there at start.

## What linuxloader64.so does for it

- **Cabinet paths**: `/pm` is the game's directory (the run tree); the offline
  cache the game copies at its first boot to `/root/.config/unity3d/Kabam/Champions`
  goes to the user's (`$XDG_CONFIG_HOME` or `~/.config`, where Unity keeps
  the game's data). The USB stick's udev rule is kept in `LocalData/system`.
- **A saved language**: a new install has no PlayerPrefs `user_language`; the
  game's boot then waits for Kabam's mobile logo flow, which the arcade never
  runs, and the quest board fails to load after the card scan (the game hangs,
  `NullReferenceException` in `QuestMode.Load`). English is written to
  `<unity3d>/unknown/unknown/prefs` before the player starts when it is missing.
- **Cabinet commands** (Mono's `Process.Start`): `reboot` (the game reboots the
  cabinet after a day up, and after a factory restore) ends the game instead;
  `udevadm`, `systemctl`, `mount`, `umount`, `mountpoint` and `mkdir /mnt/...`
  are not run. The game may run as root on Batocera.
- **The window**: the game asks for its cabinet's 1920x1080 window at boot; the
  size of `[Display]` (`WIDTH` x `HEIGHT`, else 1280x720, and `FULLSCREEN`) is
  asked again through Mono (`Screen.SetResolution`) whenever it differs.
- **The cameras**: the webcams (below); with none, the developers'
  `Config.NoWebCam` is set, else the game shows both scanners disconnected.
- **Pricing**: the dump's settings take card swipes; Currency Input is set to
  coins when found on card swipes.
- **The dongle**: serial 12345, the standard cabinet (type 1 is China's),
  security version 1, and cards enough for the dispensers (each game gives one
  out); what the test menu's factory setup writes is kept.
- **The dispensers**: an Asahi dispenser (the test menu's default, Use Creator
  Dispenser off) ready to give a card, never empty, jammed or low. With the
  Creator dispenser selected the game would see them jammed.

## The hero cards and webcams

Each card has a QR code, which the cabinet's cameras read on the scan screen.
The cabinet's camera library only captures: it hands the game greyscale
frames, and the game decodes the codes itself (ZXing, `CameraManager.ScanThread`)
and looks them up in its own table of the 120 cards' codes. `mcocWebcam.c`
stands in for it over V4L2:

- the cameras: the colour webcams (capture devices offering YUYV, UYVY,
  NV12, YU12; a laptop's IR face-login camera, GREY only, is left out), P1's
  first, at 640x480; with one, P2's is a blank camera (the game sees both
  scanners connected); with none, no camera;
- hold a card up to the webcam, its code facing it and filling a good part
  of the picture:
  the champion goes into the next slot, with the game's scan sound; a card
  is read again once taken away for a second;
- the test menu's scanner assignment (which camera is whose, preferences 90
  and 91) is set to these unless it names them (a swap made there is kept).

No setting: the webcams are used whenever there are. The webcam has to
focus at a card's distance (most do; fixed-focus ones may not), and the user
needs access to it (the `video` group). For tests, `MCOC_TEST_CAMERA` names
a still picture (a binary PGM) as P1's camera, shown 2 s out of 4, as a card
held up and taken away.

## Picking champions with the stick

With or without cards and webcams, the champions can also be picked on the
scan screen with the stick alone:

- each player's stick, left / right, shows the previous / next champion in
  the player's next slot (held, it runs through them); Start takes the one
  shown, and the stick then picks the next slot's;
- Start with none shown fills the team with random champions, as on the
  cabinet;

The stick presses the game's developers' debug inputs for this (next and
previous champion, F1 to F4 on their keyboard map): the game reads them
through its I/O manager, as a RIO2 switch count newly odd (`IOManager.ButtonNewlyPressed`),
and the RIO2 maps no switch to them; `mcocPlugins.c` sets their counts
through Mono, on the scan screen only (`GameFlow.GetGameStateCurEnum`).

## The card binder

Without the cards, or with them but no webcam, the players can pick their
cards from a binder drawn over the game (`mcocBinder.cpp`, Dear ImGui in the
player's swap, the look of Minecraft Dungeons' binder):

- on the scan screen, a player's stick **down** opens their binder (a word
  in each bottom corner says so);
- stick: move; light attack: the card in or out of the hand (a team, 3);
  heavy: empty the hand; special: the other tab; medium: close; Start: scan
  the hand. The mouse works too;
- the cards scanned go to the scan screen one after another, as the
  camera's codes do (`ScanScreen.OnPlayerCodeEvent`: the game's scan sound,
  duplicates rejected, the audits counted), once the player has joined
  (pressed Start): cards scanned before wait for it;
- while it is open, and until they are let go, that player's stick, attacks
  and Start are hidden from the game; the hand is kept for the next game.

Its tabs: the 100 Series 2 cards, in the order of their fronts, and the
game's 20 other codes (no scan of those cards). The art is read from
`dump/binder_art` in the game's directory (PNGs named by card code, or
`MCOC_CARD_ART`); without it the cards are drawn tiles with their names.
From scans of the fronts (`MCoC_Fronts_Page_001.png` .. `_100.png`):

    tools/mcoc-binder-art.sh "<scans>/Series 2" "<games>/Marvel Contest of Champions"

The names and the order are in `mcocCards.inc`, from the game's table of
codes (`CameraManager.SetupBarcodeMap`): the fronts follow it, Iron Fist
moved to his alphabetical place.

## Controls

linuxloader feeds the RIO2 from the keyboard, MAME's default layout, and
from `[EVDEV]`; the game's own keyboard map (its developers': WASD, the
arrows...) is turned off (`Config.NoKeyboard`), else it took the arrows for P2:

| | P1 | P2 |
|---|---|---|
| stick | arrows | R F D G (up, down, left, right) |
| light, medium, heavy, special | Left Ctrl, Left Alt, Space, Left Shift | A, S, Q, W |
| start | 1 | 2 |
| coin | 5 | 6 |

F2: test button (held 2 s, the test menu, which the sticks, Start and the
attacks drive); 9: service; Page Up / Down: volume; Esc quits (linuxloader's).

`[Input] INPUT_MODE = 2`, `[EVDEV]`: `PLAYER_<n>_BUTTON_UP`, `_DOWN`, `_LEFT`,
`_RIGHT`, `_BUTTON_1` .. `_BUTTON_4` (light, medium, heavy, special),
`_BUTTON_START`, `PLAYER_<n>_COIN`, `TEST_BUTTON`, `PLAYER_1_BUTTON_SERVICE`,
`VOLUME_UP`, `VOLUME_DOWN`. The Batocera generator (kind `mcoc`, a name with
"contestofchampions" or "mcoc") maps a pad's d-pad and left stick, y x a b to
the four attacks, start, select (coin), and runs it full screen.

## Running it

    GAMES_DIR=<games> scripts/linux/runmcoc.sh       # T=<seconds>, 60 by default

## Not done

- The cameras' settings in the test menu (brightness, contrast...): left to
  the webcam's own automatic ones.
- WeChat login (China) and the operator's smart cards.
- Tested on the desktop only: pads through `[EVDEV]` and Batocera are untested.
