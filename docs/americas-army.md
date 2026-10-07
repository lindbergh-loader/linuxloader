# America's Army (Global VR, 2007)

SW 1.0.1.082 (OS 20071005). `System/armyops-bin` is the Linux port of the PC
game (Unreal Engine 2, SDL 1.2, OpenGL, Creative's OpenAL-Sample as
`openal.so`), the arcade mode in UnrealScript (`AAA*.u`, maps `AAA_*.aao`).
The binary touches no cabinet hardware: its guns, buttons and coins come from
an I/O daemon over TCP.

## The I/O link (AAALink, `AAA_Core.u`)

An UnrealScript `TcpLink` connects to the host name's address (127.0.1.1),
port 8888, and trades comma-separated text lines. Decoded from the package's
bytecode (the source is stripped; this build renumbers the stock UE2 tokens).

From the daemon:

| Line | Meaning |
|---|---|
| `IN,p,x,y,trigger,altfire,reload,start` | player p's gun (0, 1), x/y in pixels of the 640x480 picture; alt fire is only logged |
| `EV,COIN,n` | n coins in |
| `EV,TEST` | the test button |
| `EV,VOL,game,attract` | volumes |
| `CFG,coinsPerPlay,coinsPerContinue,coinsDisplay,coinsToMoney,continueTimeout,lives,difficulty` | operator settings |
| `VER,n` | protocol version |

From the game: `ASK,CFG` at start, `EV,COIN` (the C key, Simulate_Coin: asks
for a coin), `EV,ATTRACT`, `EV,TEST`, `EV,PLAYER,p,START|CONTINUE|END`, and
`GUN,p,kick,flash,rate` (recoil, muzzle flash, fire rate).

The cabinet's test menu was the daemon's, not the game's: `EV,TEST` changes
nothing visible.

## In the loader

- `globalvr/gvrLink.c`: the game's `connect()` to port 8888 goes to a
  listener of the loader's on 127.0.0.1. P1's gun on ANALOGUE_1/2, P2's on
  ANALOGUE_3/4 (evdev), or the desktop's mouse over the picture for P1;
  BUTTON_1 fires, BUTTON_2 (or a shot off the screen) reloads, START, coins,
  TEST. Free play unless `[Emulation] FREEPLAY = false` (`CFG,0,0,...`).
  `GVR_LINK_TRACE=1` logs the lines.
- `globalvr/gvrCrosshair.c`: `[CrossHairs] ENABLE_CROSSHAIRS = true` draws a
  crosshair per gun into the picture at the game's `SDL_GL_SwapBuffers` (the
  arcade HUD shows none; `set AAAHUD bShowCrosshair True` does nothing).
- `HOME` is `home/` next to `System/`: `~/.armyops260/System/ArmyOps.ini`
  is made from `Default.ini` (the Windows viewport) and set to SDLDrv at
  640x480, fullscreen if the loader's config is (or asks for a larger
  picture), the mouse not captured. `User.ini` loses the cabinet's keys (F1,
  F2 start, C coin, O, T test). `~/.openalrc` picks OpenAL's OSS backend:
  the loader's `/dev/dsp`. Its capture open (voice chat, read-only) is
  refused: it had switched playback to 16 kHz mono.
- The launcher preloads the SDL 1.2 found in the library path (sdl12-compat,
  which scales the fullscreen 640x480 to the screen) in place of the game's
  own (RPATH `.`).

Desktop: `scripts/linux/runarmy.sh` with `LIBS32` holding sdl12-compat,
sdl2-compat and libstdc++.so.5. Keys: 5 coin, 1/2 start, F2 test, the mouse
is P1's gun.
