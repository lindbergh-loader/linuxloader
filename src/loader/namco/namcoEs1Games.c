// Namco ES1 game descriptors (see namcoEs1Game.h).

#include "namcoEs1Game.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"

// Gundam's save disk (/live, the cabinet's arcadedisk volumes, set up by its
// ngun_storage script): its crash log (save_nosafe/alert_battle_log) is
// mapped at boot, which fails without the directory, and its settings live
// in the data partitions, linked into its directory as save0..7 (E[19-12],
// "settings reset", at each boot without them).
static const char *const gundamRoots[] = {"/live", NULL};
static const char *const gundamDirectories[] = {
    "live", "live/disk", "live/disk/save_nosafe", "live/disk/save_nosafe/save_nosafe", "live/disk/maint",
    "live/disk/maint/seal_debug", "live/disk/maint/seal_debug/relay", "live/disk/maint/update",
    "live/disk/data0", "live/disk/data0/save0", "live/disk/data1", "live/disk/data1/save1",
    "live/disk/data2", "live/disk/data2/save2", "live/disk/data3", "live/disk/data3/save3",
    "live/disk/data4", "live/disk/data4/save4", "live/disk/data5", "live/disk/data5/save5",
    "live/disk/data6", "live/disk/data6/save6", "live/disk/data7", "live/disk/data7/save7",
    "live/disk/work", "live/disk/work/work", "live/disk/arcade0", "live/disk/arcade1", "live/image",
    "live/image/arcade", NULL};
static const char *const gundamLinks[] = {
    "maint", "live/disk/maint", "save0", "live/disk/data0/save0", "save1", "live/disk/data1/save1",
    "save2", "live/disk/data2/save2", "save3", "live/disk/data3/save3", "save4", "live/disk/data4/save4",
    "save5", "live/disk/data5/save5", "save6", "live/disk/data6/save6", "save7", "live/disk/data7/save7",
    "save_nosafe", "live/disk/save_nosafe/save_nosafe", "work", "live/disk/work/work", NULL};

// WMMT4's cabinet on the shop's LAN: 192.168.92.11/24 (the content router
// check wants that network).
static const uint8_t wmmt4Eth0[] = {192, 168, 92, 11, 255, 255, 255, 0};

static const NamcoEs1Game games[] = {
    {
        // Mobile Suit Gundam: Bonds of the Battlefield (Senjo no Kizuna,
        // REV3.57.07), the POD's station program: an X11/GLX window,
        // stripped. Its boot scripts start it for the T250 projector
        // (1024x768); libarcaderegistry is the loader's (its registry skips
        // the projector check).
        .crc32 = GUNDAM_KIZUNA_ES1,
        .fileCrc32 = 0x84777366,
        .jvsDevice = "/dev/ttyS2",
        .soundEmulation = 1,
        // The linked HASP HL (stripped: found by its trace strings).
        .haspLogin = 0x0876bb40,
        .haspLogout = 0x0876bbe0,
        .haspRead = 0x0876c938,
        .haspDecrypt = 0x0876bdb8,
        .haspGetSessionInfo = 0x0876c6c0,
        .haspFree = 0x0876c3fc,
        .haspGetSize = 0x0876cad0,
        .haspFeature = 0xffff0000,
        .haspMemorySize = 0xe00,
        .dongleSerial = "012345678901",
        .dongleBlockChecksum = 1,
        .rootPath = "/opt/arcade/exec",
        .cabinetRoots = gundamRoots,
        .cabinetDirectories = gundamDirectories,
        .cabinetLinks = gundamLinks,
        .windowX11 = 1,
        .arguments = "window xga",
    },
    {
        // Nirin (NRN100-4-NA-DAT0-A37)
        .crc32 = NIRIN_ES1,
        .fileCrc32 = 0x6d238b08,
        .jvsDevice = "/dev/ttyS2",
        .haspLogin = 0x083455c0,
        .haspLogout = 0x08345660,
        .haspRead = 0x083463b8,
        .haspDecrypt = 0x08345838,
        .haspFeature = 0xffff0000, // clHASP::check()
        .dongleSerial = "280911000001",
        .calibration = 0x09126698,
        .mesaOnNvidia = 1,
    },
    {
        // Tank! Tank! Tank! (tank_us_100226_rev1.03.14): an X11/GLX window
        // (XF86VidMode), stripped, a portrait game.
        .crc32 = TANKTANKTANK_ES1,
        .fileCrc32 = 0x66136ee9,
        .jvsDevice = "/dev/ttyS2",
        .soundEmulation = 1,
        .dongleInit = 0x080ac440,
        .dongleReadDecoded = 0x080ac3a0,
        .dongleUse = 0x080ac2e0,
        .donglePresent = 0x080ac340,
        .dongleSerial = "280911000001",
        .rootPath = "/opt/arcade/exec",
        .axisCalibration = 0x0ed7da50, // the input device's axes (0xed7da20 + 0x30)
        .axisStride = 0x3c,
        .axisIndex = 0x0863e280,
        .axisInvert = 0x0863e260,
        .windowFullscreenMov = 0x0804ff20, // movl $0x1,-0x74(%ebp)
        .windowRotationMov = 0x0804ff27,   // movl $0x2,-0x60(%ebp)
        .windowX11 = 1,
        // The boot's camera check waits for the nmUVCCamera driver, which
        // never finishes without a device: without a webcam the game is told
        // it has no camera (it skips the check and the photos), the driver
        // ends "no device", and the error check the game's sequences still
        // make (CAMERA ERROR) answers no error.
        .camIsError = 0x080f6180,     // the camera's error check (1: error)
        .camInitProgress = 0x080f5920, // nmUVCCameraInitProgress()
        .camInitState = 0x0f098ee0,
        .cameraWebcam = 1,
        .cameraPresent = 0x087e0ac6,
    },
    {
        // Dead Heat (US DRIVE, rev 6273). Nirin's platform (SDL 1.2 window,
        // the linked HASP HL), with DHR's boot sequence.
        .crc32 = DEADHEAT_ES1,
        .fileCrc32 = 0x7a18beb3,
        .jvsDevice = "/dev/ttyS2",
        .haspLogin = 0x084b9fd0,
        .haspLogout = 0x084ba070,
        .haspRead = 0x084badc8,
        .haspDecrypt = 0x084ba248,
        .haspFeature = 0xffff0000, // clHASP::Check()
        .dongleSerial = "280911000001",
        // Its steering board as Maximum Heat 3D's (which has the same).
        .kickbackDevice = "/dev/ttyS1",
        .kickbackReportsUnprompted = 1,
        .kickbackPowerGpo = 0x80,     // GOUT0 STR PCB POWER
        .selfCheckCall = {0x08267b0d, 0x08269d31}, // clSeqBootSteerDeviceThread
        .jammaCalibration = 0x087e5abc, // clInputDeviceJamma::sm_handle_center
        .jammaCalibrationStride = 4,
        .jammaForce = 0x0808bf5d,     // movzbl 0x8(%esp),%eax
        .steerDeviceForce = {0x08267ac5, 0x08269cdc}, // movzbl 0x1(%edx),%ecx; 0x1(%ecx),%eax
        .brakeSwitch = BUTTON_3,      // STR_BRAKE_DIGITAL: P1 button 3
        .lanSessionStart = 0x080fecf0, // clLanBasicSessionControler::start()
        .camIsError = 0x08087370,     // clCameraDeviceManager::IsError()
        .camIsDevice = 0x080869d0,    // clCameraDeviceManager::IsDevice()
        .camInitProgress = 0x080a6570, // nmUVCCameraInitProgress()
        .camInitState = 0x09116be0,
        .cameraWebcam = 1,
        .cameraBufferCheck = 0x080a685f, // cmp $0x96000; jbe
        .screenReal = 0x087e50c4,     // SCREEN_REAL_W (SCREEN_REAL_H after)
        .intelCpuIndicator = 0x092ff24c,
        .alignStack = 1,
        .soundEmulation = 1,
        .ffb = {.instance = 0x09035980, .effectsField = 0x48, .centerOffsetField = 0x2c, .springRange = 254, .viscosityRange = 254, .reflectRange = 63},
    },
    {
        // Maximum Heat 3D (US DRIVE, rev 8807): Dead Heat's later build, with
        // DHR's X11/GLX window instead of SDL, and still the linked HASP HL
        // and Intel's compiler.
        .crc32 = MAXIMUMHEAT3D_ES1,
        .fileCrc32 = 0x6e2be119,
        .jvsDevice = "/dev/ttyS2",
        .kickbackDevice = "/dev/ttyS1",
        .kickbackReportsUnprompted = 1,
        .kickbackPowerGpo = 0x80,     // GOUT0 STR PCB POWER
        .ffb = {.instance = 0x0917a6c0, .effectsField = 0x48, .centerOffsetField = 0x2c, .springRange = 254, .viscosityRange = 254, .reflectRange = 63},
        .networkCommands = 1,
        .linkSearchMov = 0x082bf531,  // mov $0xe10,%edx (3600 frames)
        .testModeCall = {0x08055d89, 0x0837f8a4}, // main, clTestMode::clTestMode (C2)
        .testModeClock = 0x4d8,
        .selfCheckCall = {0x082c1cce, 0x082c4147}, // clSeqBootSteerDeviceThread
        .systemIsError = 0x080afc30,  // clSystemN2::isError()
        .systemIsErrorConnectionCheck = 0x080afc20, // clSystemN2::isErrorConnectionCheck()
        .haspLogin = 0x08592bf0,
        .haspLogout = 0x08592c90,
        .haspRead = 0x085939e8,
        .haspDecrypt = 0x08592e68,
        .haspFeature = 0xffff0000, // clHASP::Check()
        .dongleSerial = "880700000001",
        .jammaCalibration = 0x0891e120, // clInputDeviceJamma::sm_handle_center
        .jammaCalibrationStride = 4,
        .jammaForce = 0x080afc4d,     // movzbl 0x8(%esp),%eax
        .brakeSwitch = BUTTON_3,      // STR_BRAKE_DIGITAL: P1 button 3
        .lanSessionStart = 0x08120900, // clLanBasicSessionControler::start()
        .testModeStopNet = 0x0836a850, // clSeqTestModeThread::StopNet()
        .camIsError = 0x0808ee00,     // clCameraDeviceManager::IsError()
        .camIsDevice = 0x0808e460,    // clCameraDeviceManager::IsDevice()
        .camInitProgress = 0x080c4f90, // nmUVCCameraInitProgress()
        .camInitState = 0x0925b920,
        .cameraWebcam = 1,
        .cameraBufferCheck = 0x080c527f, // cmp $0x96000; jbe
        .screenReal = 0x0891d708,     // SCREEN_REAL_W (SCREEN_REAL_H after)
        .initXSystemCall = 0x08054f0e, // main: call InitializeXSystem
        .intelCpuIndicator = 0x09444d2c,
        .alignStack = 1,
        .soundEmulation = 1,
        .windowX11 = 1,
    },
    {
        // Dead Heat Riders (DH RIDERS, rev 1247). No SDL: the game makes its
        // own X11/GLX window, and the HASP_OLD dongle is verified by
        // clHASP::Check (a USB presence check) instead of the linked hasp_*.
        .crc32 = DHRIDERS_ES1,
        .fileCrc32 = 0xda52fa88,
        .jvsDevice = "/dev/ttyS2",
        .haspCheck = 0x08092480,      // clHASP::Check()
        .haspIsError = 0x08092350,    // clHASP::IsError()
        .haspErrorType = 0x08092320,  // clHASP::GetErrorType()
        .haspCheckThread = 0x08092ca0, // clHaspChecker::ThreadFunc(void*)
        .haspCheckRequest = 0x080930d0, // clHaspChecker::Check()
        .lanSessionStart = 0x080ea120, // clLanBasicSessionControler::start()
        .camIsError = 0x0808db20,     // clCameraDeviceManager::IsError()
        .camIsDevice = 0x0808dd70,    // clCameraDeviceManager::IsDevice()
        .camInitProgress = 0x084b7270, // nmUVCCameraInitProgress()
        .camInitState = 0x090fdbe4,
        .cameraWebcam = 1,
        .jammaCalibration = 0x086a9320, // clInputDeviceJamma::sm_handle_center
        .brakeSwitch = BUTTON_3,      // STR_BRAKE_DIGITAL: P1 button 3
        .windowSizeLoad = {0x08054f02, 0x08054f0b}, // mov SCREEN_W/H,%edx
        .screenReal = 0x086a9288,     // SCREEN_REAL_W (SCREEN_REAL_H after)
        .initSystemCall = 0x080552db, // call InitSystem(bool, bool)
        .ffb = {.instance = 0x08f9e920, .effectsField = 0x48, .centerOffsetField = 0x2c, .springRange = 500, .viscosityRange = 63, .reflectRange = 63},
        .soundEmulation = 1,
        .windowX11 = 1,
    },
    {
        // Wangan Midnight Maximum Tune 4 (WMN4r, rev 1.10.18): stripped, its
        // own X11/GLX window, the HASP HL, JVIO and network diagnostic linked
        // in (namcoEs1Wmmt4.c).
        .crc32 = WMMT4_ES1,
        .fileCrc32 = 0xf251b573,
        .jvsDevice = "/dev/ttyS2",
        .jvsDeviceAlias = "/dev/tnt0",
        // Its steering board ("Str2", the game's STR400), at 19200 baud: as
        // Maximum Heat 3D's, reporting on its own, self-check included.
        .kickbackDevice = "/dev/ttyS0",
        .kickbackReportsUnprompted = 1,
        .kickbackVolunteersSelfCheck = 1,
        .kickbackPowerGpo = 0x80,     // cMOTOR_BIT
        .rootPath = "/opt/arcade/exec",
        .keyboardAnalog = 1,
        .soundEmulation = 1,
        .windowX11 = 1,
        .install = namcoEs1Wmmt4Install,
        .panel = namcoEs1Wmmt4Panel,
        .commandOutput = namcoEs1Wmmt4CommandOutput,
        .eth0 = wmmt4Eth0,
    },
};

const NamcoEs1Game *namcoEs1GetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const NamcoEs1Game *namcoEs1GetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
