#if defined(_WIN32) || defined(__MINGW32__)
#include "ipcBridge.hpp"
#include "../log/log.h"
#include "symbolResolver.hpp"
#include "libcBridge.hpp"

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace IpcBridge
{

    void initBridges()
    {
        log_info("Initializing IPC Bridges...");

        MAP("shmget", bridgeShmget);
        MAP("shmat", bridgeShmat);
        MAP("shmctl", bridgeShmctl);
        MAP("shmdt", bridgeShmdt);

        // System V semaphores: only segaboot uses them, posting before it waits on the same
        // thread, so a wait never really blocks; succeeding at once behaves the same.
        MAP("semget", LibcBridge::bridgeStubSuccess);
        MAP("semop", LibcBridge::bridgeStubSuccess);
        MAP("semtimedop", LibcBridge::bridgeStubSuccess);
        MAP("semctl", LibcBridge::bridgeStubSuccess);
    }
}

extern "C"
{
    int bridgeShmget(int key, size_t size, int shmflg)
    {
        log_debug("shmget(key=%d, size=%zu, flags=0x%X)", key, size, shmflg);

        if (g_shmMap.find(key) != g_shmMap.end())
        {
            log_trace("shmget: Returning existing key %d", key);
            return key;
        }

        char mapName[64];
        sprintf(mapName, "Global\\Windy_SHM_%d", key);

        HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)size, mapName);

        if (!hMap)
        {
            log_error("shmget failed: CreateFileMapping error %lu", GetLastError());
            return -1;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            log_trace("shmget: Opened existing mapping for key %d", key);
        }

        ShmInfo info;
        info.hMap = hMap;
        info.pMem = NULL;
        info.size = size;
        info.key = key;

        g_shmMap[key] = info;

        log_debug("shmget: Created mapping for key %d, size %zu", key, size);
        return key;
    }

    void *bridgeShmat(int shmid, const void *shmaddr, int shmflg)
    {
        log_trace("shmat(id=%d, addr=%p, flags=0x%X)", shmid, shmaddr, shmflg);

        if (g_shmMap.find(shmid) == g_shmMap.end())
        {
            log_error("shmat failed: Invalid shmid %d", shmid);
            return (void *)-1;
        }

        ShmInfo &info = g_shmMap[shmid];

        if (info.pMem)
        {
            return info.pMem;
        }

        info.pMem = MapViewOfFile(info.hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

        if (!info.pMem)
        {
            log_error("shmat failed: MapViewOfFile error %lu", GetLastError());
            return (void *)-1;
        }

        log_debug("shmat: Attached id=%d at %p", shmid, info.pMem);
        return info.pMem;
    }

    int bridgeShmctl(int shmid, int cmd, void *buf)
    {
        if (cmd == 0)
        { // IPC_RMID
            log_debug("shmctl: Removing id=%d", shmid);

            if (g_shmMap.find(shmid) != g_shmMap.end())
            {
                ShmInfo &info = g_shmMap[shmid];

                if (info.pMem)
                {
                    UnmapViewOfFile(info.pMem);
                }

                if (info.hMap)
                {
                    CloseHandle(info.hMap);
                }

                g_shmMap.erase(shmid);
            }
        }

        return 0;
    }

    int bridgeShmdt(const void *shmaddr)
    {
        log_trace("shmdt(%p)", shmaddr);
        return 0;
    }
}   

#endif