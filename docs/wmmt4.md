# Namco ES1 (Wangan Midnight Maximum Tune 4) — porting notes

State: **2026-10-27.** Plays on the desktop (AMD, Mesa): attract, menus and
races, confirmed on screen by the user ("in game it's perfect", the menus
after the NV_fence emulation). Online with the loader's own server: the
Banapassport card, new cars, story, time attack and ghost battles saved
and loaded again (driven on a virtual display). Not yet run on Batocera.

- Dump: `<games>/Wangan Midnight Maximum Tune 4/WMN4/`, the cabinet's
  `/opt/arcade/exec` (`WMN4r` at its top, `data/`, `save/`), 39 GB.
  `WMN4r` rev 1.10.18 (`WM410JPN10.18`), a stripped 32-bit ELF asking for
  `/opt/arcade/i686/lib/ld-linux.so.2`: run through the host's dynamic
  linker.
- Loader id (partial CRC) **`0x5031e0db`** (`WMMT4_ES1`), file CRC
  `0xf251b573`.
- The game's `lib/` holds what a current system lacks: `libprotobuf.so.7`
  (Debian `libprotobuf7` 2.4.1-3, i386), OpenSSL 0.9.8 (`libssl.so.0.9.8`,
  `libcrypto.so.0.9.8`, Debian squeeze 0.9.8o-4squeeze14, i386) and Cg 3.1
  (`libCg.so`, `libCgGL.so`, from `libs/csneo32/cg`: the loader's Cg shim
  lacks entry points it imports). The host gives 32-bit GL, GLU, X11 and
  libcurl.
- Desktop run: `scripts/linux/runwmmt4.sh` (windowed). `XVFB=1` runs it on
  a virtual display of its own (`:99`, silent, out of the desktop's way;
  keys can be sent with `DISPLAY=:99 xdotool`).
- Code: `namco/namcoEs1Wmmt4.c` (the game's hooks), `namcoEs1Wmmt4Terminal.c`
  (the terminal's heartbeat), `namcoEs1Wmmt4Net.c`, `namcoEs1Wmmt4Server.c`,
  `namcoEs1Wmmt4Rpc.c`, `namcoEs1Wmmt4Pb.c` (the server),
  `namcoEs1Wmmt4Card.c` (the card reader), the entry in `namcoEs1Games.c`;
  gp4 shaders in `namcoEs1ArbProgram.c`. The protocol, extracted from the
  descriptors in `WMN4r`: `docs/wmmt4-proto/`.

## Booting, in order

1. **Dongle.** The HASP HL is linked in (`hasp_login` `0x8921fd0`,
   `hasp_read` `0x8922dc8`...): the loader answers from a 3392-byte image
   (the points and their limits, big-endian with an MPEG-2 CRC32 each, and
   the system block with the cabinet's serial `267610069420`, its ALL.Net
   id `ABEN0069420`). Writes stay in memory.
2. **I/O board.** JVS on `/dev/ttyS2`; the dump's `DeviceSetting.lua` names
   `/dev/tnt0` (it was set up in a VM with tty0tty), so both are the
   loader's board (`jvsDeviceAlias`). The board is `NAMCO_ES1_JAMMA` under
   the name `namco ltd.;NA-JV;Ver4.00;JPN,Multipurpose.` (E0301 "board not
   found" otherwise, E0302 "Gout Update Timeout" with another board).
3. **Steering board** ("Str2", the game's STR400) on `/dev/ttyS0`, as
   Maximum Heat 3D's: reports on its own, volunteers its self-check, its
   motor power the GPO bit `0x80`.
4. **Root commands.** The game runs `sudo date`, `hwclock`, `reboot`,
   `ethtool`, `traceroute`... through `popen`: those of a Namco ES1 game are
   not run; `ethtool` answers "Link detected: yes" and `traceroute` one hop
   (the network checks below).
5. **Network.** eth0 must be `192.168.92.11/24`, gateway `.254` (E0502
   "Content Router mismatch" otherwise): the interface queries
   (`SIOCGIF*`, the game's `interfaceUpdate` `0x821d4f0`) get it whatever
   the host's interfaces. A cable once found stays plugged in.
6. **Terminal and cabinet LAN.** The game wants a Wangan terminal on the
   shop's LAN: multicast `225.0.0.1:50765`, a protobuf heartbeat every 4 ms
   (`01 04 <length>` header, MPEG-2 CRC32 big-endian at the end). The
   loader sends it (`namcoEs1Wmmt4Terminal.c`). The LAN is kept on the
   loopback interface (a firewall drops looped multicast on the default
   one); there every datagram comes from 127.0.0.1, so the source the game
   reads is rewritten to `192.168.92.11 + PCB id`, how it tells its peers
   (and itself) apart.

## Display

- **Window.** The game opens `":0.0"` whatever `$DISPLAY` says: it gets the
  display the loader runs on. It makes an override-redirect window and asks
  for `_NET_WM_STATE_FULLSCREEN`; windowed ([Display] FULLSCREEN 0) the
  window is a normal one at the configured size, and the root's size it
  reads is the window's (a black or offset picture otherwise).
- **Visual.** Mesa has no visual with aux/accumulation buffers: the loader's
  `glXChooseVisual` retries without them, then without multisampling.
- **Frame pacing.** `glXWaitVideoSyncSGI` (got through
  `glXGetProcAddress`) blocks for good on XWayland while the window is
  hidden: the loader's 60 Hz clock instead.
- **Shaders.** Cg programs in the `gp4` profiles (`NV_gpu_program4`) are
  rewritten as ARB programs on drivers without them
  (`namcoEs1Gp4Translate`): type suffixes dropped, `DIV`/`DP2`/`SLE`/`SGT`/
  `SEQ`/`SNE`/`TRUNC` emulated, `IF`/`ELSE` turned into selects and `REP`
  loops unrolled, the varyings `attrib17`/`18` mapped to texcoords. Mesa
  takes 911 of the 913; the 2 left (`lmem`) are the test menu's.
- **NV_fence.** The game syncs its texture uploads with NV fences; Mesa's
  are no-ops never completing, and the 2D layouts and menus came out as
  stale pieces of earlier frames. Emulated on GL sync objects
  (`bridgegl*FenceNV`), under a lock: the game's threads share it.

## Controls

The panel as the Pacloader fork maps it (`namcoEs1Wmmt4Panel`): button 1
Enter, 2 view change, 5 intrude, 3 and 4 shift a gear up and down. The
cabinet's shifter is an H pattern, its gears closing two switches each; the
loader works it as a sequential box, starting in neutral (shifting up first
selects first). Keyboard (INPUT_MODE ≠ 2): ←/→ steer, ↑ accelerates, ↓
brakes, space Enter, Z view, X/C shift up/down, V intrude, 1 start, 5 coin,
F1 service, F2 test.

Force feedback (the Pacloader fork's way): the game's force feedback object
hands its output to the steering board driver once a frame (`0x8366e30`:
force, spring, damper), played on the evdev wheel while the game has the
motor powered (`0x8367aa0`/`0x8367ac0`, the board's motor power too); its
self-check (`0x8367ee0`) completes the driver's. `NAMCO_FFB_TRACE=1` logs
the output every second.

## Online: the loader's server

The game is online when its three services answer (offline it shows "IC
card R/W" errors and plays without cards). The loader is all three, in
the process ([Network] `WMMT4_SERVER = 0` leaves the game offline):

- Names (`naominet.jp`, `v388-front.mucha-prd.nbgi-amnet.jp`, `*.loc`...)
  resolve to `192.168.92.254` (loopback addresses are refused by ALL.Net's
  client: init -97), whose connections go to the loader's listeners: plain
  HTTP (ALL.Net) and TLS. The TLS server uses the game's own OpenSSL 0.9.8
  (through `dlsym`) with an embedded self-signed certificate; the game's
  certificate check is off (`SSL_CTX_set_verify`).
- **ALL.Net** (`PowerOn`, base64 of a zlib stream of key=value, inflated by
  the game's zlib): `stat=1`, the game server's URI
  `https://192.168.92.254:9002`, place `JPN0FFF0`.
- **Mucha** (curl, HTTPS, key=value): answered at `curl_easy_perform`, the
  body read through the game's `CURLOPT_READFUNCTION`. `boardauth.do` and
  `updatacheck.do`; the game's downloader then logs `MUCHA_download_request():
  failed: -3` once, with no effect.
- **The game server** (protobuf RPCs `POST /method/<name>`, resources
  `GET /resource/<name>`), after Bayshore MT4's handlers
  (github.com/FestoROG-XS/bayshore-mt4), kept in `save/server/`:
  `user_<id>.pb`, `car_<id>.pb` (a `LoadCarResponse`), `team_<id>.pb`,
  `ta.pb` (time attack records), `ids.pb`. Deleting `save/server/` starts
  afresh. All the service's methods are answered: users and cars, game
  results (story, time attack, versus, ghost battles), time attack
  records and rankings, game history, car succession (a 3DX+ car), teams
  (create, search, join, approve, leave, dissolve, leader change), and
  neutral answers where no data exists (ghost competition closed, no car
  or team campaign). `WMMT4_SERVER_TRACE=1` logs each request and keeps the
  last request of each method in `save/server/trace/<method>.pb`
  (`protoc --decode_raw`).
- Settings: `mOfflineMode` and `mIcCardRw` are read as off and on (the
  generic setting readers `0x8144b90`/`0x8144600`); `Setting.lua` is not
  touched (its header carries a checksum).

### Banapassport card

The reader is the game's BngRw (`0x8ac3e40`...): attached, a card always
on it when the game waits for a touch. The card's identity, chip id and
serial, is drawn once and kept in `save/server/card.ini` (access code
`307...`); its NBGIC header is Blowfish-encrypted with the game's libcrypto.
A new card's player makes a car (maker, model, colour, AT/MT, name), kept
on the card from then on.

### Ghost battles

The game races other players' ghosts from the server, by ghost level, and
falls back on its default ghosts (`DEFAULT_GHOST_JP.BIN`) for a level
without one. That table is missing from this dump and loaded only by the
English and Chinese locales anyway (a Japanese server gives every level):
the JP locale crashed on the first empty level (`0x805aa0b`, a NULL
table). The loader loads it whatever the locale (`je` at `0x8432bce`
skipped) and reads `DEFAULT_GHOST.BIN`, same format, in its place
(`fileFallbacks`). `ghost_summary` lists a ghost per level 1..10 (the
cars kept here, under ids of their own): an empty list leaves levels with
no ghost at all (`std::out_of_range`). The opponents raced are the
default ones.

## Batocera

The rom goes in `namcoes3` as a squashfs of the `WMN4/` directory (its
`lib/` included). The generator classes it as `wmmt4` (a name with
"maximum tune 4", "wmmt4" or "wmn4"): left stick or wheel, R2 gas, L2
brake, b Enter, y view, x intrude, L1/R1 shift down/up. Batocera has no
32-bit libcurl: a shim beside linuxloader (`wmmt4/libcurl.so.4`, its
source and README in the rgs tree) is offered to this game only, the loader
answering the transfers itself.

## Debugging

- `NAMCO_GAME_LOG=1`: the game's own log (`logMes` `0x809ddb0`) on stderr.
- `WMMT4_SHOT=<directory>`: a frame every 4 s (`WMMT4_SHOT_PERIOD`, 0 for
  none) saved there as a PPM, read back before the swap; touching
  `<directory>/grab` saves the next one as `grab.ppm`. On a virtual
  display (software rendering) many frames come out black, read back or
  captured from the window: take several.
- `WMMT4_SERVER_TRACE=1`: the server's requests (above).
