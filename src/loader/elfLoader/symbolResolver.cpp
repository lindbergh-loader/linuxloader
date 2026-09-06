#if defined(_WIN32) || defined(__MINGW32__)
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

// Declare Win7+ DLL search path APIs in case MinGW headers don't expose them
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
extern "C"
{
    typedef PVOID DLL_DIRECTORY_COOKIE;
    __declspec(dllimport) BOOL WINAPI SetDefaultDllDirectories(DWORD DirectoryFlags);
    __declspec(dllimport) DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(PCWSTR NewDirectory);
    __declspec(dllimport) BOOL WINAPI RemoveDllDirectory(DLL_DIRECTORY_COOKIE Cookie);
}
#endif
#include <winsock2.h>
#include <objbase.h>
#include <excpt.h>
#include <string.h>

#ifdef _WIN32
#include <dbghelp.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <io.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <direct.h>
#include <process.h>
#include <string>
#include <math.h>
#include <stdlib.h>
#include <fstream>
#include <crtdbg.h>
#include <algorithm>

#include "memoryManager.hpp"
#include "glHooks.hpp"
#include "libcBridge.hpp"
#include "loader/redirections/libcShared.h"
#include "pthread/pthreadEmu.hpp"
#include "symbolResolver.hpp"
#include "elfLoader.hpp"
#include "../log/log.h"
#include "../patching/patch.h"

extern "C" void __gmon_start__()
{
    log_debug("Dummy __gmon_start__ called");
}
extern "C" int __libc_start_main(int (*main_func)(int, char **, char **), int argc, char **argv, void (*init)(void), void (*fini)(void),
                                 void (*rtld_fini)(void), void *stack_end)
{
    log_info("Intercepted __libc_start_main called!");

    _CrtSetReportMode(_CRT_ASSERT, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);

    PthreadEmu::Initialize();
    log_info("PthreadEmu initialized");

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (init)
    {
        log_debug("Calling init from __libc_start_main");
        init();
    }

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    char **envp = argv + argc + 1;

    log_info("Transferring control to main()...");
    int exitCode = main_func(argc, argv, envp);
    log_info("Program main() exited with code: %d", exitCode);

    if (fini)
    {
        log_debug("Calling fini from __libc_start_main");
        fini();
    }

    PthreadEmu::Shutdown();
    log_info("PthreadEmu shutdown complete");
    exit(exitCode);
    return exitCode;
}

extern "C" int emuSemInit(void *sem, int pshared, unsigned int value);
extern "C" int emuSemDestroy(void *sem);
extern "C" int emuSemWait(void *sem);
extern "C" int emuSemTrywait(void *sem);
extern "C" int emuSemPost(void *sem);
extern "C" int emuSemGetValue(void *sem, int *sval);

SymbolResolver::SymbolResolver()
{
    m_VTables["__gmon_start__"] = reinterpret_cast<void *>(__gmon_start__);
    m_VTables["__libc_start_main"] = reinterpret_cast<void *>(__libc_start_main);

    m_VTables["malloc"] = reinterpret_cast<void *>(MemoryManager::customMalloc);
    m_VTables["mallinfo"] = reinterpret_cast<void *>(MemoryManager::customMallinfo);
    m_VTables["calloc"] = reinterpret_cast<void *>(MemoryManager::customCalloc);
    m_VTables["realloc"] = reinterpret_cast<void *>(MemoryManager::customRealloc);
    m_VTables["free"] = reinterpret_cast<void *>(MemoryManager::customFree);
    m_VTables["memmove"] = reinterpret_cast<void *>(MemoryManager::customMemmove);
    m_VTables["wmemmove"] = reinterpret_cast<void *>(MemoryManager::customWmemmove);
    m_VTables["memalign"] = reinterpret_cast<void *>(MemoryManager::customMemalign);
    m_VTables["posix_memalign"] = reinterpret_cast<void *>(MemoryManager::customPosixMemalign);
    m_VTables["strndup"] = reinterpret_cast<void *>(MemoryManager::customStrndup);
    m_VTables["strdup"] = reinterpret_cast<void *>(MemoryManager::customStrdup);
    m_VTables["__strdup"] = reinterpret_cast<void *>(MemoryManager::customStrdup);
    m_VTables["strcoll"] = reinterpret_cast<void *>(MemoryManager::customStrcoll);
    m_VTables["strxfrm"] = reinterpret_cast<void *>(MemoryManager::customStrxfrm);
    m_VTables["memcpy"] = reinterpret_cast<void *>(memcpy);
    m_VTables["memset"] = reinterpret_cast<void *>(memset);
    m_VTables["memcmp"] = reinterpret_cast<void *>(memcmp);
    m_VTables["memchr"] = reinterpret_cast<void *>(static_cast<void *(*)(const void *, int, size_t)>(memchr));
    m_VTables["strlen"] = reinterpret_cast<void *>(strlen);
    m_VTables["strcpy"] = reinterpret_cast<void *>(strcpy);
    m_VTables["strncpy"] = reinterpret_cast<void *>(sharedStrncpy);
    m_VTables["strcmp"] = reinterpret_cast<void *>(strcmp);
    m_VTables["strncmp"] = reinterpret_cast<void *>(strncmp);
    m_VTables["strchr"] = reinterpret_cast<void *>(static_cast<char *(*)(const char *, int)>(strchr));
    m_VTables["strrchr"] = reinterpret_cast<void *>(static_cast<char *(*)(const char *, int)>(strrchr));
    m_VTables["strstr"] = reinterpret_cast<void *>(static_cast<char *(*)(const char *, const char *)>(strstr));
    m_VTables["strtol"] = reinterpret_cast<void *>(strtol);
    m_VTables["strtod"] = reinterpret_cast<void *>(strtod);
    m_VTables["strerror"] = reinterpret_cast<void *>(strerror);
    m_VTables["strtok"] = reinterpret_cast<void *>(strtok);
    m_VTables["strtok_r"] = reinterpret_cast<void *>(strtok_r);
    m_VTables["strncat"] = reinterpret_cast<void *>(strncat);
    m_VTables["strcat"] = reinterpret_cast<void *>(strcat);
    m_VTables["strpbrk"] = reinterpret_cast<void *>(static_cast<char *(*)(const char *, const char *)>(strpbrk));
    m_VTables["strcspn"] = reinterpret_cast<void *>(strcspn);
    m_VTables["strspn"] = reinterpret_cast<void *>(strspn);
    m_VTables["strcasecmp"] = reinterpret_cast<void *>(strcasecmp);
    m_VTables["strncasecmp"] = reinterpret_cast<void *>(strncasecmp);

    m_VTables["mblen"] = reinterpret_cast<void *>(mblen); // mblen is multi-byte character length, doesn't use wchar_t
    m_VTables["putc"] = reinterpret_cast<void *>(putc);
    m_VTables["putchar"] = reinterpret_cast<void *>(putchar);
    m_VTables["fputc"] = reinterpret_cast<void *>(fputc);
}

void SymbolResolver::InitSearchPaths(const std::string &libraryPathParam, const std::string &gameElfPath)
{
    m_LibrarySearchPaths.clear();

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

    DLL_DIRECTORY_COOKIE depsCookie = nullptr;

    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH))
    {
        std::string exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            m_ExeDir = exeDir.substr(0, lastSlash);
            m_LibrarySearchPaths.push_back(m_ExeDir);
            log_info("Added library search path (exe dir): %s", m_ExeDir.c_str());

            std::string depsDir = m_ExeDir + "\\ll-deps";
            m_LibrarySearchPaths.push_back(depsDir);
            m_DllSearchBase = depsDir;
            log_info("Added library search path (deps dir): %s", depsDir.c_str());

            std::wstring depsDirW(depsDir.begin(), depsDir.end());
            depsCookie = AddDllDirectory(depsDirW.c_str());
        }
    }

    if (!libraryPathParam.empty())
    {
        m_LibrarySearchPaths.push_back(libraryPathParam);
        m_DllSearchBase = libraryPathParam;
        log_info("Added library search path (parameter): %s", libraryPathParam.c_str());
        if (depsCookie)
            RemoveDllDirectory(depsCookie);
        std::wstring libraryPathW(libraryPathParam.begin(), libraryPathParam.end());
        AddDllDirectory(libraryPathW.c_str());
    }

    if (!gameElfPath.empty())
    {
        std::string gameDir = gameElfPath;
        size_t lastSlash = gameDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            gameDir = gameDir.substr(0, lastSlash);
            m_LibrarySearchPaths.push_back(gameDir);
            log_info("Added library search path (game dir): %s", gameDir.c_str());
        }
    }

    // 4th: Location of the game folder + /lib
    if (!gameElfPath.empty())
    {
        std::string gameDir = gameElfPath;
        size_t lastSlash = gameDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            std::string gameLibDir = gameDir.substr(0, lastSlash) + "\\lib";
            m_LibrarySearchPaths.push_back(gameLibDir);
            log_info("Added library search path (game lib dir): %s", gameLibDir.c_str());

            std::string axaLibDir = gameDir.substr(0, lastSlash) + "\\..\\dso\\q2satl_lind\\release";
            m_LibrarySearchPaths.push_back(axaLibDir);
            log_info("Added library search path (q2satl lib dir): %s", axaLibDir.c_str());

            std::string phLibDir = gameDir.substr(0, lastSlash) + "\\data\\drv\\cri\\lindbergh\\libs";
            m_LibrarySearchPaths.push_back(phLibDir);
            log_info("Added library search path (Primeval Hunt lib dir): %s", phLibDir.c_str());

        }
    }
    log_info("Initialized %zu library search paths", m_LibrarySearchPaths.size());
    log_info("DLL search base set to: %s", m_DllSearchBase.c_str());
}

void SymbolResolver::AddDllSearchSubPath(const std::string &subPath)
{
    if (m_DllSearchBase.empty() || subPath.empty())
        return;

    std::string fullPath = m_DllSearchBase + "\\" + subPath;
    m_LibrarySearchPaths.push_back(fullPath);
    std::wstring fullPathW(fullPath.begin(), fullPath.end());
    AddDllDirectory(fullPathW.c_str());
    printf("[loader] Added subdirectory to DLL search path: %s\n", fullPath.c_str());
    log_info("Added subdirectory to DLL search path: %s", fullPath.c_str());
}

void SymbolResolver::RegisterLibrary(const std::string &linuxName, const std::string &windowsName)
{
    m_LibraryMap[linuxName] = windowsName;
    log_info("Registered library substitution: %s -> %s", linuxName.c_str(), windowsName.c_str());
}

void SymbolResolver::LoadNeededLibrary(const std::string &linuxName)
{
    std::string targetName = linuxName;
    auto it = m_LibraryMap.find(linuxName);
    bool isMapped = false;

    if (it != m_LibraryMap.end())
    {
        targetName = it->second;
        isMapped = true;
        if (targetName == "INTERNAL")
        {
            log_debug("Skipping internal library: %s", linuxName.c_str());
            return;
        }
    }

    bool tryAsDll = isMapped || (targetName.length() >= 4 && targetName.substr(targetName.length() - 4) == ".dll");

    if (tryAsDll)
    {
        for (const auto &searchPath : m_LibrarySearchPaths)
        {
            std::string candidatePath = searchPath + "\\" + targetName;
            std::ifstream testFile(candidatePath.c_str());
            if (testFile.good())
            {
                testFile.close();
                HMODULE handle = LoadLibraryExA(candidatePath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (handle)
                {
                    auto it = std::find(m_LoadedLibraries.begin(), m_LoadedLibraries.end(), reinterpret_cast<void *>(handle));
                    if (it == m_LoadedLibraries.end())
                    {
                        m_LoadedLibraries.push_back(reinterpret_cast<void *>(handle));
                    }
                    log_info("Successfully loaded DLL from search path: %s", candidatePath.c_str());
                    return;
                }
                log_warn("Failed to load DLL from path: %s", candidatePath.c_str());
            }
            testFile.close();
        }

        HMODULE handle = LoadLibraryA(targetName.c_str());
        if (handle)
        {
            auto it = std::find(m_LoadedLibraries.begin(), m_LoadedLibraries.end(), reinterpret_cast<void *>(handle));
            if (it == m_LoadedLibraries.end())
            {
                m_LoadedLibraries.push_back(reinterpret_cast<void *>(handle));
            }
            log_info("Successfully loaded DLL library: %s", targetName.c_str());
            return;
        }
        log_warn("Failed to load as DLL: %s. Will fallback to native ELF loader if possible.", targetName.c_str());
    }
    else
    {
        log_debug("Library is unmapped and not a .dll (%s). Directly routing to native Linux Shared Object loader.", targetName.c_str());
    }

    if (std::find(m_LoadedNativeNames.begin(), m_LoadedNativeNames.end(), targetName) != m_LoadedNativeNames.end())
    {
        log_debug("Linux SO %s is already natively loaded. Skipping.", targetName.c_str());
        return;
    }

    std::string fullPath = targetName;
    bool found = false;

    for (const auto &searchPath : m_LibrarySearchPaths)
    {
        std::string candidatePath = searchPath + "\\" + targetName;
        std::ifstream testFile(candidatePath.c_str());
        if (testFile.good())
        {
            testFile.close();
            found = true;
            fullPath = candidatePath;
            log_info("Found library %s in search path: %s", targetName.c_str(), searchPath.c_str());
            break;
        }
        testFile.close();
    }

    if (!found)
    {
        std::ifstream f(targetName.c_str());
        if (f.good())
        {
            f.close();
            found = true;
            fullPath = targetName;
        }
    }

    if (found)
    {
        ElfLoader *soLoader = new ElfLoader();
        soLoader->SetIsSharedObject(true);
        if (soLoader->Load(fullPath))
        {
            m_LoadedNativeNames.push_back(fullPath);
            m_NativeLoaders.push_back(soLoader);
            log_info("Successfully loaded and exported symbols for native Linux SO: %s", fullPath.c_str());
            m_PendingSOPatches.push_back({(uintptr_t)soLoader->GetBaseAddress(), fullPath});
        }
        else
        {
            log_error("Failed to natively load Linux SO: %s", fullPath.c_str());
            delete soLoader;
            soLoader = nullptr;
        }
    }
    else
    {
        if (targetName.find("libstdc++") != std::string::npos)
        {
            log_fatal("Critical library not found: %s. Ensure the native Linux .so file "
                      "is present in the game or library directory.",
                      targetName.c_str());
            exit(1);
        }
        log_error("Library file not found at path: %s (searched in %zu paths)", targetName.c_str(), m_LibrarySearchPaths.size());
    }
}

void SymbolResolver::RegisterNativeSymbol(const std::string &symbolName, void *symbolPtr)
{
    if (m_NativeSymbols.find(symbolName) == m_NativeSymbols.end())
    {
        m_NativeSymbols[symbolName] = symbolPtr;
        log_trace("Exported native symbol: %s at %p", symbolName.c_str(), symbolPtr);
    }
}

extern "C" void HandleUnresolvedSymbol(const char *symbolName, void *callerAddress)
{
    printf("\n*** FATAL ERROR ***\nUnresolved symbol called: %s\nCaller Address: %p\n*******************\n", symbolName, callerAddress);
    fflush(stdout);
    log_fatal("Unresolved symbol called: %s (Caller: %p)", symbolName, callerAddress);

    char msg[512];
    snprintf(msg, sizeof(msg), "Unresolved symbol called: %s\nCaller Address: %p\n\nThe loader will now terminate.", symbolName,
             callerAddress);
    MessageBoxA(NULL, msg, "Lindbergh Loader Error", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

static void *CreateUnresolvedStub(const std::string &symbolName)
{
    static uint8_t *currentBlock = nullptr;
    static size_t offset = 0;

    if (!currentBlock || offset + 18 > 4096)
    {
        currentBlock = reinterpret_cast<uint8_t *>(MemoryManager::GetInstance().AllocateExecutable(4096, nullptr));
        offset = 0;
    }

    if (!currentBlock)
    {
        return nullptr;
    }

    uint8_t *stub = currentBlock + offset;
    offset += 18;

    size_t nameLen = symbolName.length() + 1;
    char *namePtr = nullptr;

    if (offset + nameLen <= 4096)
    {
        namePtr = reinterpret_cast<char *>(currentBlock + offset);
        offset += nameLen;
    }
    else
    {
        currentBlock = reinterpret_cast<uint8_t *>(MemoryManager::GetInstance().AllocateExecutable(4096, nullptr));
        if (!currentBlock)
        {
            return nullptr;
        }
        stub = currentBlock + offset;
        offset = 18;
        namePtr = reinterpret_cast<char *>(currentBlock + offset);
        offset += nameLen;
    }

    strcpy(namePtr, symbolName.c_str());

    stub[0] = 0x8B;
    stub[1] = 0x04;
    stub[2] = 0x24;
    stub[3] = 0x50;
    stub[4] = 0x68;
    *reinterpret_cast<uint32_t *>(stub + 5) = reinterpret_cast<uint32_t>(namePtr);
    stub[9] = 0xE8;
    uint32_t callTarget = reinterpret_cast<uint32_t>(&HandleUnresolvedSymbol);
    uint32_t relOffset = callTarget - (reinterpret_cast<uint32_t>(stub) + 14);
    *reinterpret_cast<uint32_t *>(stub + 10) = relOffset;
    stub[14] = 0x83;
    stub[15] = 0xC4;
    stub[16] = 0x08;
    stub[17] = 0xC3;

    return stub;
}

bool loadingNeededLibrary = false;
void *SymbolResolver::ResolveSymbol(const std::string &symbolName, std::string *outModuleName)
{
    log_trace("Resolving symbol: %s", symbolName.c_str());
    void *resolvedAddr = nullptr;
    void *originalAddr = nullptr;

    if (strncmp(symbolName.c_str(), "__gmon_start__", 14) == 0 || strncmp(symbolName.c_str(), "_ITM_deregisterTMCloneTable", 27) == 0 ||
        strncmp(symbolName.c_str(), "_ITM_registerTMCloneTable", 27) == 0 || strncmp(symbolName.c_str(), "_Jv_RegisterClasses", 19) == 0 ||
        strncmp(symbolName.c_str(), "__cxa_finalize", 14) == 0)
    {
        log_info("Resolved weak symbol '%s' to NULL (Not Implemented/Required)", symbolName.c_str());
        if (outModuleName)
            *outModuleName = "WEAK_SYMBOL";
        return (void *)&LibcBridge::bridgeStubSuccess;
    }

    if (strncmp(symbolName.c_str(), "_Unwind_", 8) == 0 || symbolName == "__gxx_personality_v0")
    {
        HMODULE hMinGwGcc = GetModuleHandleA("libgcc_s_dw2-1.dll");
        if (hMinGwGcc)
        {
            void *proc = (void *)GetProcAddress(hMinGwGcc, symbolName.c_str());
            if (proc)
            {
                if (outModuleName)
                    *outModuleName = "libgcc_s_dw2-1.dll";
                return proc;
            }
        }
    }

    auto vtableIt = m_VTables.find(symbolName);
    if (vtableIt != m_VTables.end())
    {
        if (outModuleName)
            *outModuleName = "Internal VTable";
        resolvedAddr = vtableIt->second;
    }

    bool needsOriginal = !resolvedAddr || (m_OriginalSymbolPtrs.find(symbolName) != m_OriginalSymbolPtrs.end());

    if (needsOriginal)
    {
        if (!originalAddr)
        {
            auto nativeIt = m_NativeSymbols.find(symbolName);
            if (nativeIt != m_NativeSymbols.end())
            {
                if (!resolvedAddr && outModuleName)
                    *outModuleName = "Native Symbol";
                originalAddr = nativeIt->second;
            }
        }

        if (!originalAddr)
        {
            for (void *handle : m_LoadedLibraries)
            {
                if (!handle)
                    continue;

                void *symbolAddr = reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbolName.c_str()));
                if (symbolAddr)
                {
                    char path[MAX_PATH];
                    if (GetModuleFileNameA(reinterpret_cast<HMODULE>(handle), path, sizeof(path)))
                    {
                        char *basename = strrchr(path, '\\');
                        std::string modName = basename ? basename + 1 : path;
                        if (!resolvedAddr && outModuleName)
                            *outModuleName = modName;
                    }
                    else if (!resolvedAddr && outModuleName)
                    {
                        *outModuleName = "Loaded Library";
                    }
                    originalAddr = symbolAddr;
                    break;
                }
            }
        }

        if (!originalAddr)
        {
            if (strncmp(symbolName.c_str(), "gl", 2) == 0)
            {
                void *proc = GLHooks_GetProcAddress(symbolName.c_str());
                if (proc)
                {
                    if (!resolvedAddr && outModuleName)
                        *outModuleName = "GLHooks";
                    log_trace("Symbol %s resolved from GLHooks at 0x%p", symbolName.c_str(), proc);
                    originalAddr = proc;
                }
            }
        }
    }

    auto origPtrIt = m_OriginalSymbolPtrs.find(symbolName);
    if (origPtrIt != m_OriginalSymbolPtrs.end() && originalAddr)
    {
        *(origPtrIt->second) = originalAddr;
    }

    if (resolvedAddr)
    {
        log_trace("Symbol %s resolved at 0x%p (hooked)", symbolName.c_str(), resolvedAddr);
        return resolvedAddr;
    }

    if (originalAddr)
    {
        log_trace("Symbol %s resolved at 0x%p (original)", symbolName.c_str(), originalAddr);
        return originalAddr;
    }

    log_info("Symbol not found: %s. Generating crash stub.", symbolName.c_str());
    if (outModuleName)
        *outModuleName = "UNRESOLVED_STUB";
    return CreateUnresolvedStub(symbolName);
}

void *SymbolResolver::GetVTable(const std::string &className)
{
    auto it = m_VTables.find(className);
    if (it != m_VTables.end())
    {
        return it->second;
    }
    return nullptr;
}

void SymbolResolver::RegisterVTable(const std::string &className, void *vtablePtr, void **originalSymbolPtr)
{
    m_VTables[className] = vtablePtr;
    if (originalSymbolPtr)
    {
        m_OriginalSymbolPtrs[className] = originalSymbolPtr;
        *originalSymbolPtr = nullptr;
    }
    log_debug("Registered VTable for class: %s at %p", className.c_str(), vtablePtr);
}

void *bridgeResolveSymbol(const char *symbolName)
{
    std::string moduleName;
    return SymbolResolver::GetInstance().ResolveSymbol(symbolName, &moduleName);
}

void bridgeLoadNeededLibrary(const char *filename)
{
    loadingNeededLibrary = true;
    SymbolResolver::GetInstance().LoadNeededLibrary(filename);
    SymbolResolver::GetInstance().ProcessAllRelocations();
    SymbolResolver::GetInstance().PatchAllSOs();
    SymbolResolver::GetInstance().RunAllInits();
}

bool SymbolResolver::ProcessAllRelocations()
{
    for (auto it = m_NativeLoaders.rbegin(); it != m_NativeLoaders.rend(); ++it)
    {
        if (*it && !(*it)->ProcessRelocations())
        {
            return false;
        }
    }
    return true;
}

void SymbolResolver::PatchAllSOs()
{
    for (auto &patch : m_PendingSOPatches)
    {
        log_info("Applying deferred SO patch: base=0x%08X path=%s", (uint32_t)patch.first, patch.second.c_str());
        patchSO(patch.first, patch.second.c_str());
    }
    m_PendingSOPatches.clear();
}

bool SymbolResolver::RunAllInits()
{
    for (auto it = m_NativeLoaders.rbegin(); it != m_NativeLoaders.rend(); ++it)
    {
        if (*it && !(*it)->RunInit())
        {
            return false;
        }
    }
    return true;
}

void *SymbolResolver::ResolveSymbolInSharedLibs(const std::string &symbolName)
{
    for (ElfLoader *loader : m_NativeLoaders)
    {
        if (!loader)
            continue;
        void *addr = loader->FindExportedSymbol(symbolName);
        if (addr)
        {
            log_trace("ResolveSymbolInSharedLibs: '%s' found in native SO at %p", symbolName.c_str(), addr);
            return addr;
        }
    }

    for (void *handle : m_LoadedLibraries)
    {
        if (!handle)
            continue;
        void *symbolAddr = reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbolName.c_str()));
        if (symbolAddr)
        {
            log_trace("ResolveSymbolInSharedLibs: '%s' found in loaded DLL at %p", symbolName.c_str(), symbolAddr);
            return symbolAddr;
        }
    }

    auto vtableIt = m_VTables.find(symbolName);
    if (vtableIt != m_VTables.end())
    {
        log_info("ResolveSymbolInSharedLibs: '%s' found in internal VTable bridge at %p", symbolName.c_str(), vtableIt->second);
        return vtableIt->second;
    }

    log_info("ResolveSymbolInSharedLibs: '%s' not found in any shared library", symbolName.c_str());
    return nullptr;
}

#endif