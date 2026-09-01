#if defined(_WIN32) || defined(__MINGW32__)
#include <cstdlib>

#include <filesystem>
#include <winsock2.h>
#include <windows.h>
#include <libgen.h>

#include "glad/gl.h"
#include "elfLoader/alsa2sdlBridge.hpp"
#include "elfLoader/segaapiBridge.hpp"
#include "elfLoader/gccBridge.hpp"
#include "elfLoader/ipcBridge.hpp"
#include "elfLoader/elfLoader.hpp"
#include "elfLoader/filesystemBridge.hpp"
#include "elfLoader/libcBridge.hpp"
#include "elfLoader/gccBridge.hpp"
#include "elfLoader/graphicsBridge.hpp"
#include "elfLoader/pthreadBridge.hpp"
#include "elfLoader/regexBridge.hpp"
#include "elfLoader/termiosBridge.hpp"
#include "elfLoader/networkBridge.hpp"
#include "elfLoader/mathBridge.hpp"
#include "elfLoader/symbolResolver.hpp"
#include "elfLoader/pthread/pthreadEmu.hpp"
#include "loader/config/config.h"
#include "log/log.h"
#include "graphics/gpuVendor.h"
#include "init.h"
#include "patching/patch.h"
#include "input/sdlInput.h"
#include "mainShared.h"

std::string g_absoluteElfPath;
uint32_t partialElfCrc = 0;

extern SDLControllers sdlJoysticks;

extern int gGrp;

HMODULE hGlDLL = NULL;

LONG CALLBACK myVectoredHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    DWORD exceptionCode = ExceptionInfo->ExceptionRecord->ExceptionCode;
    PCONTEXT ctx = ExceptionInfo->ContextRecord;

    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION || exceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        uint8_t* pint = (uint8_t*)ctx->Eip;
        if (pint && !IsBadReadPtr(pint, 2) && pint[0] == 0xCD && pint[1] == 0x80) {        
            if (ctx->Eax == 0xE0) { 
                ctx->Eax = (DWORD)PthreadEmu::pthreadSelf();
                ctx->Eip += 2;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}   

void initBridges()
{
    FileSystemBridge::initBridges();
    GraphicsBridge::initBridges();
    LibcBridge::initBridges();
    PthreadBridge::initBridges();
    TermiosBridge::initBridges();
    GccBridge::initBridges();
    NetworkBridge::initBridges();
    IpcBridge::initBridges();
    SegaApiBridge::initBridges();
    Alsa2SdlBridge::initBridges();
    RegexBridge::initBridges();
    MathBridge::initBridges();
}

int main(int argc, char *argv[], char *envp[])
{
    logSetMinLevel(LOG_WARN);

    char command[MAX_PATH_LENGTH] = {0};
    char originalDir[MAX_PATH_LENGTH] = {0};
    char gameELF[MAX_PATH_LENGTH] = {0};
    char libraryPath[MAX_PATH_LENGTH] = {0};
    char gamePath[MAX_PATH_LENGTH] = {0};

    log_info("Parsing arguments...\n");
    if (parseArgs(argc, argv, command, originalDir, gameELF, libraryPath) != PARSE_ARGS_SUCCESS)
        return EXIT_SUCCESS;

    char elfPath[MAX_PATH_LENGTH];
    char elfArgs[MAX_PATH_LENGTH] = "";

    char *spacePos = strchr(gameELF, ' ');
    if (spacePos != NULL)
    {
        size_t elfPathLen = spacePos - gameELF;
        strncpy(elfPath, gameELF, elfPathLen);
        elfPath[elfPathLen] = '\0';
        strcpy(elfArgs, spacePos + 1);
    }
    else
    {
        strcpy(elfPath, gameELF);
    }

    ElfLoader::PreReserveAddressSpace(elfPath);

    log_info("Initializing library search paths...\n");
    GetCurrentDirectoryA(MAX_PATH_LENGTH, gamePath);
    SymbolResolver::GetInstance().InitSearchPaths(libraryPath, gamePath);

    // Install Exception Handler (BEFORE any C++ code runs)
    if (!AddVectoredExceptionHandler(1, myVectoredHandler))
        log_error("Failed to register MyVectoredHandler");

    log_info("Initializing bridges...\n");
    initBridges();

    if (!std::filesystem::exists("tmp"))
        std::filesystem::create_directory("tmp");
    if (!std::filesystem::exists("tmp\\segaboot"))
        std::filesystem::create_directory("tmp\\segaboot");

    log_info("Loading ELF file: %s\n", elfPath);
    ElfLoader loader;

    if (!loader.Load(elfPath))
    {
        log_fatal("Failed to load ELF file: %s", elfPath);
        return 1;
    }

    uint8_t *baseAddr = (uint8_t *)loader.GetBaseAddress();
    if (baseAddr)
        partialElfCrc = getCrc32Mem(baseAddr + 10, 0x4000);

    initConfig(configPath);

    if (partialElfCrc == PRIMEVAL_HUNT_SBPP && getConfig()->phUseZink)
    {
        SymbolResolver::GetInstance().AddDllSearchSubPath("zink");
        SetEnvironmentVariableA("GALLIUM_DRIVER", "zink");
        std::string zinkGlPath = SymbolResolver::GetInstance().GetDllSearchBase() + "\\zink\\opengl32.dll";
        hGlDLL = LoadLibraryA(zinkGlPath.c_str());
    }
    else
    {
        hGlDLL = LoadLibraryA("opengl32.dll");
    }

    if (!hGlDLL)
    {
        printf("Failed to load OpenGL!\n");
        return -1;
    }

    log_debug("Initializing main...");
    initMain(configPath, controlsPath);

    int final_argc = 0;
    char *final_argv_arr[10];

    final_argv_arr[final_argc++] = elfPath;

    if (strlen(elfArgs) > 0)
    {
        char *token = strtok(elfArgs, " ");
        while (token != NULL && final_argc < 9)
        {
            final_argv_arr[final_argc++] = token;
            token = strtok(NULL, " ");
        }
    }
    final_argv_arr[final_argc] = NULL;

    if (!loader.Execute(final_argc, final_argv_arr, envp))
    {
        log_error("Failed to execute ELF file.");
        return 1;
    }

    log_info("ELF Execution Finished");
    return 0;
}

#endif