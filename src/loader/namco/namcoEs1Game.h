#ifndef NAMCO_ES1_GAME_H
#define NAMCO_ES1_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "namcoFfb.h"
#include "../hardware/lindbergh/jvs.h"

// Everything the loader needs to know about one Namco ES1 game. These are
// plain Linux executables with their symbols (not dumps): the functions are
// hooked by their addresses in the executable's .symtab.
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // Serial port of the JVS I/O board (n2Jvio library), and another name
    // a dump may give it (WMMT4's set up for a JVS emulator in a VM through
    // tty0tty: /dev/tnt0, in its data/Device/DeviceSetting.lua), or NULL.
    const char *jvsDevice, *jvsDeviceAlias;
    // Serial port of the steering wheel's force feedback board (clKickback),
    // or NULL: emulated by namcoEs1Kickback.c.
    const char *kickbackDevice;
    // How that board behaves (see namcoEs1Kickback.c): it reports on its own
    // (power, position) instead of only answering, and its power-on report
    // comes as soon as the port is opened.
    int kickbackReportsUnprompted, kickbackVolunteersSelfCheck;
    // The JVS general output bit (in output byte 0) that powers the board.
    uint8_t kickbackPowerGpo;
    // The calls of clKickback::requestSelfCheck(): through the loader, which
    // has the board report its self-check after the game's request.
    uint32_t selfCheckCall[2];
    // The wheel's force feedback, from clKickback (namcoFfb.c).
    NamcoFfb ffb;

    // HASP HL API (linked in): hasp_login, hasp_logout, hasp_read and
    // hasp_decrypt. The game logs in to haspFeature, reads the dongle's
    // serial number (encrypted) and decrypts it; the answers give it
    // dongleSerial (12 characters).
    uint32_t haspLogin, haspLogout, haspRead, haspDecrypt;
    uint32_t haspFeature;
    const char *dongleSerial;
    // Gundam's use of the same API: hasp_get_sessioninfo (the key's
    // <haspid> and the <featureid>, as XML the game frees with hasp_free),
    // hasp_get_size of the memory file (answered haspMemorySize), and its
    // memory read a block it checks: dongleSerial (12 digits, the first a
    // '0') then zeros, its last two bytes the sum of the others and its
    // complement (dongleBlockChecksum).
    uint32_t haspGetSessionInfo, haspFree, haspGetSize;
    uint32_t haspMemorySize;
    int dongleBlockChecksum;

    // The controls' calibration the game applies to the board's analog
    // inputs (clHandleSetting, int32 raw values, see NamcoEs1Calibration): the
    // loader's steering (0 left .. 0xffff right) and pedals (0 released ..
    // 0xffff) are put in its range, so that they work uncalibrated.
    uint32_t calibration;
    // The same, kept by the Dead Heat games in clInputDeviceJamma's statics
    // (int32, jammaCalibrationStride bytes apart, 16 if 0): the handle's
    // centre (sm_handle_center, this address), left max (1 stride on) and
    // right max (2), the accelerator's rest (5) and max (6), and on DHR the
    // brake's rest (7) and max (8); on Dead Heat the brake pedal's rest (7)
    // and range (8, m_left_pedal_std/_w).
    uint32_t jammaCalibration;
    uint32_t jammaCalibrationStride;
    // Tank! Tank! Tank!'s input axes: its 3 analog channels (steering, left
    // pedal, right pedal) are read as raw / 65536 (inverted where
    // axisInvert[channel] is set, a byte each), go to axis axisIndex[channel]
    // (an int each), and each axis is calibrated piecewise: 7 floats,
    // axisStride bytes apart from axisCalibration: inputs x0 < x1 <= x2 < x3,
    // outputs y0 (below x0), y1 (from x1 to x2), y2 (above x3).
    uint32_t axisCalibration, axisStride, axisIndex, axisInvert;
    // clSystemN2::init's load of its "use the JAMMA device" argument (from
    // config.csv's USE_JAMMA_DEVICE, FALSE in Dead Heat's dump), made a
    // "true": the controls come over the JVS board.
    uint32_t jammaForce;
    // The same setting as clSeqBootSteerDeviceThread reads it (its Run() and
    // the fiber's inlined copy: movzbl 0x1(%reg),%reg from clConfig), made a
    // "true" too: else the steering board (clKickback) is never created, and
    // the wheel gets no force.
    uint32_t steerDeviceForce[2];
    // The analog inputs as they are (WMMT4: handle, accelerator and brake on
    // channels 1 to 3, through the game's own calibration): the desktop
    // keys drive them too.
    int keyboardAnalog;
    // The player 1 switch that brakes (STR_BRAKE_DIGITAL, the cabinets'
    // brake button: DHR, Dead Heat, Maximum Heat 3D): the desktop keys' down
    // arrow presses it.
    int brakeSwitch;

    // DHR and the X11/GLX ES1 games: the HASP_OLD dongle is a USB presence
    // check, verified by clHASP::Check and re-checked on a clHaspChecker
    // thread (which quits the game if it is lost). All are detoured to
    // "no error" (see namcoEs1.c). Unlike the linked HASP HL above, these are
    // C++ member functions: the object pointer is the first stack argument.
    uint32_t haspCheck;       // clHASP::Check()             -> no-op
    uint32_t haspIsError;     // clHASP::IsError()           -> 0
    uint32_t haspErrorType;   // clHASP::GetErrorType()      -> 0
    uint32_t haspCheckThread; // clHaspChecker::ThreadFunc() -> end the thread
    uint32_t haspCheckRequest; // clHaspChecker::Check()     -> no-op

    // Tank! Tank! Tank!'s dongle layer (stripped; the scene release's
    // tank_emu.c names): its init, the dongle memory read and decoded (64
    // bytes, the serial in it: dongleSerial), its use, and the presence check.
    uint32_t dongleInit, dongleReadDecoded, dongleUse, donglePresent;

    // Maximum Heat 3D boots through a clSystemN2 (the N2 cabinets' system
    // object), whose error flags stay set without the N2's own board: its
    // isError() and isErrorConnectionCheck() answer "no error" (as the
    // Pacloader fork does).
    uint32_t systemIsError, systemIsErrorConnectionCheck;

    // The calls of the clTestMode constructor (Maximum Heat 3D): through the
    // loader, which zeroes its clock adjustments first (testModeClock, 0x1c
    // bytes): the game makes the object reachable before they are set, and
    // its clDateTime members then read heap garbage (test mode hangs). As the
    // Pacloader fork does; the save slots' clock_difference.bin that does not
    // hold a valid date is moved aside too (to save*/testmode/*.corrupt).
    uint32_t testModeCall[2];
    uint32_t testModeClock;

    // The boot's search for linked cabinets (clSeqBootNetThread::Run): its
    // "mov $3600,%edx", the frames (60 s) a lone cabinet counts down before
    // it plays alone; made 60 (1 s). A cabinet that answers only shortens it.
    uint32_t linkSearchMov;

    // The test mode's StopNet() (Maximum Heat 3D), made a no-op: it waits for
    // the LAN services to be running before it stops them, which they never
    // are behind the LAN boot gate (lanSessionStart). Its caller ignores its
    // result.
    uint32_t testModeStopNet;

    // The cabinet's network commands (ifconfig.pl, ifconfig eth0, arping,
    // pinger.pl) are answered rather than only dropped (namcoEs1Network.c).
    int networkCommands;

    // DHR's boot gate on the LAN (see namcoEs1.c): the service controler's
    // start, shared by the clLanServer and clLanClient wrappers.
    uint32_t lanSessionStart;

    // DHR's live camera (see namcoEs1.c): the nmUVCCamera* V4L2 driver speaks
    // the cabinet camera's ABI, which a standard uvcvideo device does not
    // answer; the manager is answered as a present, healthy camera instead.
    uint32_t camIsError;      // clCameraDeviceManager::IsError()  -> 0
    uint32_t camIsDevice;     // clCameraDeviceManager::IsDevice() -> 1
    uint32_t camInitProgress; // nmUVCCameraInitProgress()         -> done
    uint32_t camInitState;    // its state (9: done)
    int cameraWebcam;         // a webcam, when there is one, is the camera
    uint32_t cameraBufferCheck; // the driver's buffer size check (jbe)
    uint32_t cameraPresent;   // Tank: the game's camera flag (byte), cleared without a webcam

    // DHR's window size (see namcoEs1.c): InitSystem's two loads of the main
    // size (SCREEN_W, SCREEN_H) into the window's, and SCREEN_REAL_W (then
    // _H), the size of the output its last pass draws at.
    uint32_t windowSizeLoad[2];
    uint32_t screenReal;
    // DHR main's call of InitSystem(bool fullscreen, bool): through the
    // loader, which sets the output size and the fullscreen flag first.
    uint32_t initSystemCall;
    // Maximum Heat 3D: InitSystem is inlined in main, which fills an
    // stInitializeXSystemData (fullscreen byte at 0, width at 8, height at
    // 12) and calls InitializeXSystem with it: this call, through the
    // loader, which sets the output size and the fullscreen flag first.
    uint32_t initXSystemCall;

    int windowX11;           // the game makes its own X11/GLX window

    // The game's directory on the cabinet, which it names by its absolute
    // path (Tank! Tank! Tank!'s saves, /opt/arcade/exec/save00/...): mapped
    // to its directory here.
    const char *rootPath;
    // Other cabinet directories the game names by their absolute path
    // (Gundam's save disk, /live): the same path in the game's directory
    // (NULL-terminated list), and the directories it expects there, made at
    // start-up (relative to the game's directory, parents first).
    const char *const *cabinetRoots;
    const char *const *cabinetDirectories;
    // Files the dump lacks, read from others (pairs of the missing file's
    // path, from the game's directory, and the one read instead,
    // NULL-terminated): used only while the file is missing.
    const char *const *fileFallbacks;
    // Links the cabinet's storage script makes in the game's directory
    // (Gundam's save0..7 into its data partitions): pairs of link and
    // target, relative to the game's directory, NULL-terminated.
    const char *const *cabinetLinks;

    // Tank! Tank! Tank!'s window, chosen in main by defaults its command
    // line can change ("window"/"fullscreen", "rleft"/"rright"): the
    // "movl $imm,-x(%ebp)" setting fullscreen (1) and the picture's rotation
    // for the cabinet's turned monitor (2). See namcoEs1.c.
    uint32_t windowFullscreenMov, windowRotationMov;

    // Built with Intel's compiler for SSSE3 (Dead Heat): its startup check
    // reads __intel_cpu_indicator, which its own init sets only on Intel
    // CPUs; elsewhere the game exits "not built to run on the processor".
    uint32_t intelCpuIndicator;
    // Its code keeps a 4-byte-aligned stack: its imports go through stubs
    // that align it (see namcoEs1Align.c).
    int alignStack;
    // The sound driver nsAdrv.dll is the loader's (see namcoEs1Sound.c).
    int soundEmulation;
    // The game's arguments, as the cabinet's boot scripts give them (Gundam:
    // its display mode, "fullscreen xga"), or NULL.
    const char *arguments;
    // Its picture flickers under NVIDIA's OpenGL (Nirin), not Mesa's: on a
    // host running NVIDIA's driver it runs on Mesa's zink (Mesa's OpenGL on
    // the GPU's Vulkan driver). See namcoEs1Launch.c.
    int mesaOnNvidia;
    // The cabinet's panel from the loader's switches, before each JVS
    // packet's reply (WMMT4: its H-pattern shifter and buttons), or NULL.
    void (*panel)(JVSIO *io);
    // eth0's address and mask, a cabinet's on the shop's LAN (WMMT4's ALL.Net
    // library reads them): answered for a host without such an interface,
    // or NULL.
    const uint8_t *eth0;
    // The output of a root command the game reads (popen), answered instead
    // of running it, or NULL for nothing (WMMT4's network checks).
    const char *(*commandOutput)(const char *command);
    // What only this game needs, installed at the end of namcoEs1Init
    // (Wangan Midnight Maximum Tune 4: namcoEs1Wmmt4.c), or NULL.
    void (*install)(void);
} NamcoEs1Game;

const NamcoEs1Game *namcoEs1GetGame(uint32_t crc32);
const NamcoEs1Game *namcoEs1GetGameByFileCrc(uint32_t fileCrc32);

// Launcher: the games ask for the cabinet's dynamic linker by an absolute
// path (/opt/arcade/i686/lib/ld-linux.so.2); run them through the host's
// 32-bit one instead (see namcoEs1Launch.c). command is "./<elf>" optionally
// followed by " -t".
void namcoEs1PrepareCommand(char *command, size_t size, uint32_t fileCrc32);

#endif // NAMCO_ES1_GAME_H
