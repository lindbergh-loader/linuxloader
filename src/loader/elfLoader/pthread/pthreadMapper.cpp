// ============================================================
// pthread_mapper.cpp - Object Mapper 
// ============================================================

#include "pthreadInternal.hpp"
#include "../linuxStack.hpp"
#include <unordered_map>

// ============================================================
// Static members
// ============================================================

SRWLOCK PthreadMapper::s_lock = SRWLOCK_INIT;
volatile bool PthreadMapper::s_initialized = false;

// Mapping tables
static std::unordered_map<void*, PthreadMutexInternal*>     s_mutex_map;
static std::unordered_map<void*, PthreadCondInternal*>      s_cond_map;
static std::unordered_map<void*, PthreadRwlockInternal*>    s_rwlock_map;
static std::unordered_map<void*, PthreadBarrierInternal*>   s_barrier_map;
static std::unordered_map<void*, PthreadSpinInternal*>      s_spin_map;
static std::unordered_map<uint32_t, PthreadThreadInternal*> s_thread_map;
static std::unordered_map<DWORD, uint32_t>                  s_wintid_to_linuxtid;

// Generates unique Linux thread IDs
static volatile uint32_t s_next_thread_id = 1000;

// ============================================================
// Initialize / Shutdown
// ============================================================

void PthreadMapper::Initialize() {
    if (s_initialized) return;
    
    InitializeSRWLock(&s_lock);
    s_initialized = true;
    
    uint32_t main_tid;
    PthreadThreadInternal* main_thread = CreateThread(&main_tid);
    if (main_thread) {
        main_thread->handle = GetCurrentThread();
        main_thread->win_thread_id = GetCurrentThreadId();
        main_thread->detached = 0;
        main_thread->exited = 0;
        
        AcquireSRWLockExclusive(&s_lock);
        s_wintid_to_linuxtid[main_thread->win_thread_id] = main_tid;
        ReleaseSRWLockExclusive(&s_lock);
    }
}

void PthreadMapper::Shutdown() {
    if (!s_initialized) return;
    
    AcquireSRWLockExclusive(&s_lock);
    
    // Cleanup mutexes
    for (auto& pair : s_mutex_map) {
        if (pair.second) {
            DeleteCriticalSection(&pair.second->cs);
            delete pair.second;
        }
    }
    s_mutex_map.clear();
    
    // Cleanup conds
    for (auto& pair : s_cond_map) {
        if (pair.second) {
            delete pair.second;
        }
    }
    s_cond_map.clear();
    
    // Cleanup rwlocks
    for (auto& pair : s_rwlock_map) {
        if (pair.second) {
            delete pair.second;
        }
    }
    s_rwlock_map.clear();
    
    // Cleanup barriers
    for (auto& pair : s_barrier_map) {
        if (pair.second) {
            DeleteCriticalSection(&pair.second->cs);
            delete pair.second;
        }
    }
    s_barrier_map.clear();
    
    // Cleanup spins
    for (auto& pair : s_spin_map) {
        if (pair.second) {
            delete pair.second;
        }
    }
    s_spin_map.clear();
    
    // Cleanup threads
    for (auto& pair : s_thread_map) {
        if (pair.second) {
            if (pair.second->handle && pair.second->handle != INVALID_HANDLE_VALUE) {
                // Don't close if it's the current thread's pseudo-handle
                if (pair.second->handle != GetCurrentThread()) {
                    CloseHandle(pair.second->handle);
                }
            }
            delete pair.second;
        }
    }
    s_thread_map.clear();
    s_wintid_to_linuxtid.clear();
    
    ReleaseSRWLockExclusive(&s_lock);
    s_initialized = false;
}

// ============================================================
// Mutex Mapping
// ============================================================

PthreadMutexInternal* PthreadMapper::GetOrCreateMutex(void* linux_mutex) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_mutex_map.find(linux_mutex);
    if (it != s_mutex_map.end() && it->second->initialized == MUTEX_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_lock);
        return it->second;
    }
    
    // Create new mutex
    PthreadMutexInternal* mutex = new PthreadMutexInternal;
    InitializeCriticalSection(&mutex->cs);
    mutex->type = LINUX_PTHREAD_MUTEX_DEFAULT;
    mutex->owner_thread = 0;
    mutex->lock_count = 0;
    mutex->initialized = MUTEX_INIT_MAGIC;
    
    s_mutex_map[linux_mutex] = mutex;
    
    ReleaseSRWLockExclusive(&s_lock);
    return mutex;
}

PthreadMutexInternal* PthreadMapper::FindMutex(void* linux_mutex) {
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_mutex_map.find(linux_mutex);
    PthreadMutexInternal* result = nullptr;
    if (it != s_mutex_map.end() && it->second->initialized == MUTEX_INIT_MAGIC) {
        result = it->second;
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroyMutex(void* linux_mutex) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_mutex_map.find(linux_mutex);
    if (it != s_mutex_map.end()) {
        if (it->second) {
            DeleteCriticalSection(&it->second->cs);
            it->second->initialized = 0;
            delete it->second;
        }
        s_mutex_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

// ============================================================
// Cond Mapping
// ============================================================

PthreadCondInternal* PthreadMapper::GetOrCreateCond(void* linux_cond) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_cond_map.find(linux_cond);
    if (it != s_cond_map.end() && it->second->initialized == COND_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_lock);
        return it->second;
    }
    
    PthreadCondInternal* cond = new PthreadCondInternal;
    InitializeConditionVariable(&cond->cv);
    cond->clock_id = 0;  // CLOCK_REALTIME
    cond->initialized = COND_INIT_MAGIC;
    
    s_cond_map[linux_cond] = cond;
    
    ReleaseSRWLockExclusive(&s_lock);
    return cond;
}

PthreadCondInternal* PthreadMapper::FindCond(void* linux_cond) {
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_cond_map.find(linux_cond);
    PthreadCondInternal* result = nullptr;
    if (it != s_cond_map.end() && it->second->initialized == COND_INIT_MAGIC) {
        result = it->second;
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroyCond(void* linux_cond) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_cond_map.find(linux_cond);
    if (it != s_cond_map.end()) {
        if (it->second) {
            it->second->initialized = 0;
            delete it->second;
        }
        s_cond_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

// ============================================================
// Rwlock Mapping
// ============================================================

PthreadRwlockInternal* PthreadMapper::GetOrCreateRwlock(void* linux_rwlock) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_rwlock_map.find(linux_rwlock);
    if (it != s_rwlock_map.end() && it->second->initialized == RWLOCK_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_lock);
        return it->second;
    }
    
    PthreadRwlockInternal* rwlock = new PthreadRwlockInternal;
    InitializeSRWLock(&rwlock->srw);
    rwlock->initialized = RWLOCK_INIT_MAGIC;
    
    s_rwlock_map[linux_rwlock] = rwlock;
    
    ReleaseSRWLockExclusive(&s_lock);
    return rwlock;
}

PthreadRwlockInternal* PthreadMapper::FindRwlock(void* linux_rwlock) {
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_rwlock_map.find(linux_rwlock);
    PthreadRwlockInternal* result = nullptr;
    if (it != s_rwlock_map.end() && it->second->initialized == RWLOCK_INIT_MAGIC) {
        result = it->second;
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroyRwlock(void* linux_rwlock) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_rwlock_map.find(linux_rwlock);
    if (it != s_rwlock_map.end()) {
        if (it->second) {
            it->second->initialized = 0;
            delete it->second;
        }
        s_rwlock_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

// ============================================================
// Barrier Mapping
// ============================================================

PthreadBarrierInternal* PthreadMapper::GetOrCreateBarrier(void* linux_barrier, unsigned int count) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_barrier_map.find(linux_barrier);
    if (it != s_barrier_map.end() && it->second->initialized == BARRIER_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_lock);
        return it->second;
    }
    
    PthreadBarrierInternal* barrier = new PthreadBarrierInternal;
    InitializeCriticalSection(&barrier->cs);
    InitializeConditionVariable(&barrier->cv);
    barrier->threshold = count;
    barrier->count = 0;
    barrier->generation = 0;
    barrier->initialized = BARRIER_INIT_MAGIC;
    
    s_barrier_map[linux_barrier] = barrier;
    
    ReleaseSRWLockExclusive(&s_lock);
    return barrier;
}

PthreadBarrierInternal* PthreadMapper::FindBarrier(void* linux_barrier) {
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_barrier_map.find(linux_barrier);
    PthreadBarrierInternal* result = nullptr;
    if (it != s_barrier_map.end() && it->second->initialized == BARRIER_INIT_MAGIC) {
        result = it->second;
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroyBarrier(void* linux_barrier) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_barrier_map.find(linux_barrier);
    if (it != s_barrier_map.end()) {
        if (it->second) {
            DeleteCriticalSection(&it->second->cs);
            it->second->initialized = 0;
            delete it->second;
        }
        s_barrier_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

// ============================================================
// Spin Mapping
// ============================================================

PthreadSpinInternal* PthreadMapper::GetOrCreateSpin(void* linux_spin) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_spin_map.find(linux_spin);
    if (it != s_spin_map.end() && it->second->initialized == SPIN_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_lock);
        return it->second;
    }
    
    PthreadSpinInternal* spin = new PthreadSpinInternal;
    spin->lock = 0;
    spin->initialized = SPIN_INIT_MAGIC;
    
    s_spin_map[linux_spin] = spin;
    
    ReleaseSRWLockExclusive(&s_lock);
    return spin;
}

PthreadSpinInternal* PthreadMapper::FindSpin(void* linux_spin) {
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_spin_map.find(linux_spin);
    PthreadSpinInternal* result = nullptr;
    if (it != s_spin_map.end() && it->second->initialized == SPIN_INIT_MAGIC) {
        result = it->second;
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroySpin(void* linux_spin) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_spin_map.find(linux_spin);
    if (it != s_spin_map.end()) {
        if (it->second) {
            it->second->initialized = 0;
            delete it->second;
        }
        s_spin_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

// ============================================================
// Thread Mapping
// ============================================================

PthreadThreadInternal* PthreadMapper::CreateThread(uint32_t* out_linux_tid) {
    AcquireSRWLockExclusive(&s_lock);
    
    uint32_t tid = InterlockedIncrement((volatile LONG*)&s_next_thread_id);
    
    PthreadThreadInternal* thread = new PthreadThreadInternal;
    memset(thread, 0, sizeof(PthreadThreadInternal));
    thread->linux_thread_id = tid;
    
    s_thread_map[tid] = thread;
    
    if (out_linux_tid) {
        *out_linux_tid = tid;
    }
    
    ReleaseSRWLockExclusive(&s_lock);
    return thread;
}

PthreadThreadInternal* PthreadMapper::FindThread(uint32_t linux_tid) {
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_thread_map.find(linux_tid);
    PthreadThreadInternal* result = (it != s_thread_map.end()) ? it->second : nullptr;
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

PthreadThreadInternal* PthreadMapper::FindThreadByWinId(DWORD win_tid) {
    AcquireSRWLockShared(&s_lock);
    
    PthreadThreadInternal* result = nullptr;
    auto it = s_wintid_to_linuxtid.find(win_tid);
    if (it != s_wintid_to_linuxtid.end()) {
        auto thread_it = s_thread_map.find(it->second);
        if (thread_it != s_thread_map.end()) {
            result = thread_it->second;
        }
    }
    
    ReleaseSRWLockShared(&s_lock);
    return result;
}

void PthreadMapper::DestroyThread(uint32_t linux_tid) {
    AcquireSRWLockExclusive(&s_lock);
    
    auto it = s_thread_map.find(linux_tid);
    if (it != s_thread_map.end()) {
        PthreadThreadInternal* thread = it->second;
        if (thread) {
            // Remove from wintid map
            s_wintid_to_linuxtid.erase(thread->win_thread_id);
            
            if (thread->handle && thread->handle != INVALID_HANDLE_VALUE &&
                thread->handle != GetCurrentThread()) {
                CloseHandle(thread->handle);
            }
            delete thread;
        }
        s_thread_map.erase(it);
    }
    
    ReleaseSRWLockExclusive(&s_lock);
}

uint32_t PthreadMapper::GetCurrentLinuxTid() {
    DWORD win_tid = GetCurrentThreadId();
    
    AcquireSRWLockShared(&s_lock);
    
    auto it = s_wintid_to_linuxtid.find(win_tid);
    uint32_t result = (it != s_wintid_to_linuxtid.end()) ? it->second : 0;
    
    ReleaseSRWLockShared(&s_lock);
    
    // If not found, this thread wasn't created via pthread_create
    // Create an entry for it
    if (result == 0) {
        // We commit the stack per thread to prevent other threads to reach the ELF threads.
        LinuxStack::CommitCurrentThreadStack();

        uint32_t new_tid;
        PthreadThreadInternal* thread = CreateThread(&new_tid);
        if (thread) {
            thread->handle = GetCurrentThread();
            thread->win_thread_id = win_tid;
            thread->detached = 1;  // Treat as detached
            
            AcquireSRWLockExclusive(&s_lock);
            s_wintid_to_linuxtid[win_tid] = new_tid;
            ReleaseSRWLockExclusive(&s_lock);
            
            result = new_tid;
        }
    }
    
    return result;
}
