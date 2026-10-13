# Ghostbusters Arcade (Raw Thrills g6)

Status: **native gameplay, mouse input and both Sinden guns confirmed; captured HASP answers are installed**. Tested on
2026-10-13 with v01.17, built Feb 5 2019. Detection, cached ELF repair,
cabinet path redirection and HASP hooks work. A copy instrumented with the
Ghostbusters cave ran under BudgieLoader and produced four complete HASP
records; the two decrypt keys match the startup requests seen in the native
Linux run. The four answers are installed in `hasp/`. The game has no captured
`hhl_mem.dmp`, so the loader synthesizes its memory image. With these answers,
an isolated native launch passed RTDT and localization initialization and
reached gameplay. Player 1 mouse aim and gun buttons were confirmed by the
user after enabling the IR input backend. Player 2 mouse aim and gun buttons
were also confirmed with explicit EVDEV mappings and recovery from a collapsed
calibration range. Both Sinden players' aim, firing and gun/beam animations
were confirmed on Batocera on 2026-10-14. The initial minimal
test tree lacked cabinet utility files; linking the installed utilities
resolved those integrity failures.

## Launch

Run from the folder containing `game` and `g6/ghostb`:

The game directory can be flat; a physical `pm/` subdirectory is unnecessary.
Move the contents of the dump's `pm/` into the game directory so its layout
is `game`, `g6/`, `utils/`, `env/` and `hasp/`. The loader redirects the
cabinet's `/pm` paths to this directory. The successful native tests used
this flat layout. If retaining `pm/`, set `GAME_DIR` to that subdirectory
and place `hasp/` there beside `game`.

```sh
GAME_DIR='/path/to/Ghostbusters Arcade' \
LIBS32='/path/to/native/32-bit/libraries' \
bash scripts/linux/rungb.sh
```

`GAMES_DIR` can supply the parent directory instead of `GAME_DIR`. `TP_LIBS`
can supply additional native ELF libraries. This is a Linux launch; Wine is
not used. The executable needs 32-bit GL, GLU, GLUT, libcsv, libesd and its
other `DT_NEEDED` dependencies. The launcher shortens `/usr/lib/libcsv.so`
to `libcsv.so` in a cached copy and clears eight undefined symbol values.
The original executable is preserved.

For a native Linux run, place compatible dongle data under `hasp/` in the game directory:
`hhl_mem.dmp` (128 bytes) plus recorded crypt answers named by the first
16 input bytes in lowercase hexadecimal. See [dongle recording](dongle-recording.md).
The isolated startup test requested these four answers:

| Filename | Answer size |
| --- | ---: |
| `da13ebd00f64e039302c43304e9afc74` | 512 bytes |
| `60bed7be0a6b1489249f6658ff28dd6c` | 256 bytes |
| `224da88a0eb06072c78be967e95f6742` | 32 bytes |
| `95badfda6f41b0fe3959f91fa2b1433b` | 16 bytes |

Additional requests may appear once these are available. `RT_DONGLE_TRACE=1`
prints the questions. The fallback cipher cannot decrypt the assets.

## Controls and display

The profile uses the default IR gun input map. It emulates the camera
manager's connection, aim and button reports. The cabinet's `-mg` mounted
gun mode is not selected.

This build requests input backends `0x482`, which omits the IR backend
(`0x4`) even though the default game map reads IR gun slots. The profile
hooks `InputInit` to include that flag (`0x486`) and stubs `IrInit` because
the loader supplies the camera reports. Without this change, no `IrGun*`
hooks are called and calibration remains at X/Y zero with inactive buttons.
Ghostbusters also interprets IR X from right to left; the profile reverses
the supplied X coordinate for desktop and relative mouse input. Absolute
evdev gun mappings (`:ABS:` or `:ABS_NEG:`), including Sinden guns, apply an
additional X and Y orientation correction. This is selected per player and
axis, so a relative mouse used alongside a gun keeps its existing orientation.
Both Sinden players' axis directions were confirmed on Batocera.

When no recorded dongle memory is present, the synthesized memory starts
with the factory setup flag at byte `0x39` set to 1. The factory setup menu
writes that byte and attract mode reads it; leaving it zero causes the
"factory setup has not been completed" warning. Recorded `hhl_mem.dmp` and
saved `dongle_memory.dmp` override this default. Changes made through the
test menu continue to use the normal dongle-memory persistence.

## Boot integrity test

The supplied encrypted manifest also lists
`data/prod/shaders/shader7_sk/vert.gls.bak` (752 bytes, SHA-1
`53ada9efbfb5103dfbdc101531b7c515920acee9`), but the dump omits this unused
backup. Its active `vert.gls` passes validation. The profile exempts only a
checksum-only request for that exact backup when it is absent. A present
backup and all other files still use the original integrity checker. No
asset or encrypted manifest is changed. With this exception and the complete
utilities directory, the native boot log reports no file-test failures;
startup reached game initialization in about six seconds rather than the
previous run's 24 seconds including the error-screen delay.

- Desktop: mouse aims P1; left/right/middle buttons provide the gun's
  trigger, secondary button and third button. Cabinet keys use the shared
  desktop input: 1/2 start, 5/6 coins, F1 service, F2 test, up/down volume
  and menu navigation.
- Evdev (`[Input] INPUT_MODE = 2`): `ANALOGUE_1/2` aim P1,
  `ANALOGUE_3/4` aim P2; each player's `BUTTON_1/2/3` feed gun buttons.
  Start, service, test, coins and P1 up/down feed the cabinet switches.
- `[Display] WIDTH`, `HEIGHT` and `FULLSCREEN` control the game window.
  The cabinet launch script uses 1280x720.

With no physical guns, the Batocera generator assigns physical mice before
streaming mice: the local mouse is P1 and a Sunshine/Moonlight mouse can be P2.
Virtual keyboard aggregators, touchscreens and pen devices are excluded.

The game's saved mounted-gun calibration can still reverse the aim after
the loader supplies IR coordinates. For example, the original player 1
records had `y_orient=1` and Y limits `847` to `80`, while the confirmed
mouse test used `y_orient=0` and limits `181` to `740`. Reusing a different
`pmuser/` can therefore change axis direction. Calibration has three copies
at `pmuser/aud/save/mountedGun0Calibration.{0,1,2}.xml`; they are checksummed
RTDT containers, so preserve complete records when backing up or restoring
them rather than editing their XML payload alone.

A calibration saved while the gun had no input can contain identical minimum
and maximum values (the supplied P2 record had X `956..956` and Y `542..542`).
That puts the normalized aim outside the screen and can suppress gun buttons.
For a collapsed axis, the loader uses the emulated camera's `0..800` X or
`0..600` Y range in memory. Valid ranges keep their calibration; the saved
RTDT records are preserved until the operator recalibrates through the game.

Backwards saved bounds are also normalized in memory. On the Sinden cabinet,
P1 had X `1364..521` and Y `554..325`; P2 had X `649..-1` and Y `228..-1`.
Those ranges produced correct normalized aim but always set the IR offscreen
flag, freezing the `GUN_FOLLOW` gun models and beams. Sorting the bounds and
recomputing raw coordinates keeps normalized aim and firing unchanged while
letting the engine clear the offscreen flag for on-screen positions.

## Executable identification

- Build ID: `e35529888ac920e45039eca47b8a286c945745c2`.
- Original file CRC32: `0x774ad438`.
- Original partial CRC32: `0x344ca1b3`.
- Prepared partial CRC32 / `GHOSTBUSTERS_RT`: `0xcaeea463`.
- Partial CRC range: file offset `0xa`, length `0x4000`.
- Cabinet root: `/pm`, mapped to the launch directory.
- HASP feature: `0xffff0000`; memory file: `0xfff2`.

The IR backend has two 0x34-byte analog slots and three 0x2c-byte digital
slots per gun (0xec bytes total). Its button area starts at 0x68. These
offsets differ from Aliens Armageddon's; the profile specifies them explicitly.

## Validation

Both 32-bit Linux targets (`linuxloader_exe` and `linuxloader`) built
successfully. Startup tests used a copied executable and copied save data in
`/tmp`, asset symlinks, and a temporary empty libesd library (the ELF imports
no esd symbols; the host lacks that dependency). The earlier run without
answers observed the four HASP requests and asset definition failures. After
installing the BudgieLoader answers, the isolated native run loaded the RTDT
definitions and reached gameplay. Live tracing confirmed the IR backend
was enabled and received mouse coordinates; the user confirmed mouse gun
input worked. The test did not
modify the original executable or save data.
The user also confirmed the factory-setup warning disappeared. A later
native boot with the missing-backup exception produced no integrity errors.
