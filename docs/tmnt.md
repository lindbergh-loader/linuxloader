# Teenage Mutant Ninja Turtles

Raw Thrills' Teenage Mutant Ninja Turtles arcade game, a 64-bit Unity
2017.4.17f1 title. The available dump contains a Windows Unity player in
place of the cabinet's Linux player. Its Mono assembly still has Linux
cabinet code (`Sys_Linux`), the RIO2 API, and HASP and `UnityNatives`
interfaces. The dump has only Windows `OrenVid.dll` and
`PK-UnityPlugin.dll` in `TMNT_Data/Plugins/x86_64`; the cabinet's Linux
plugins are absent.

## Linux Unity player

Install the matching Linux player alongside the game:

    tools/unity-player.sh 2017.4.17f1 05307cddbb71 "<games>/Teenage Mutant Ninja Turtles/unity"

`linuxloader64` detects `TMNT_Data/globalgamemanagers`, reads the Unity
version, and launches this player with the game's assets and Mono assembly.
It creates stand-ins for `libRIO2.so`, `libUnityNatives.so`, and
`libhasp_linux_31655.so` under the names the game imports. The existing RIO2
stand-in reports a virtual connected board and feeds the game's four player
switch matrix from linuxloader keyboard input or `[EVDEV]` bindings. The
other two use the shared Raw Thrills dongle and cabinet integration.

## Current support

The keyboard layout is WASD plus J/K/L for player one, arrow keys plus
Return/Right Shift/Slash for player two, I/J/K/L plus U/O/P for player
three, and T/F/G/H plus R/Y/U for player four. Keys 5-9 add credits for
players one through five; keys 1-4 start players one through four. `[EVDEV]` button names follow the game's five player
groups, such as `PLAYER_1_BUTTON_1`, `PLAYER_1_BUTTON_2`,
`PLAYER_1_BUTTON_3`, and `PLAYER_1_BUTTON_START`.

The dump's Linux-specific USB mount setup is handled by linuxloader's cabinet
system-call guard, so it cannot alter the host's udev rules or mount USB
devices. The game-specific math helpers imported from `UnityNatives` are
compatibility approximations and still need gameplay validation. The dump
contains only the Windows `OrenVid.dll` and `PK-UnityPlugin.dll`; an OrenVideo
stub keeps optional videos from throwing errors, but playback is unavailable.
PopcornFX may also need a Linux plugin if the game requires it.
