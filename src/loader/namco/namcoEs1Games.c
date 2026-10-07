// Namco ES1 game descriptors (see namcoEs1Game.h).

#include "namcoEs1Game.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"

static const NamcoEs1Game games[] = {
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
