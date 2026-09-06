// ============================================================
// pthread_thread.cpp - Thread Emulation
// ============================================================

#include "pthreadEmu.hpp"
#include "pthreadInternal.hpp"
#include "../linuxStack.hpp"
#include "../../log/log.h"
#include <process.h>

// ============================================================
// Thread Local Storage for cancel state
// ============================================================

static DWORD s_tls_cancel_state = TLS_OUT_OF_INDEXES;
static DWORD s_tls_cancel_type = TLS_OUT_OF_INDEXES;
static DWORD s_tls_cancel_pending = TLS_OUT_OF_INDEXES;

struct ThreadCancelState
{
    int state;   // ENABLE or DISABLE
    int type;    // DEFERRED or ASYNCHRONOUS
    int pending; // Cancel request pending
};

static void InitCancelTLS()
{
    if (s_tls_cancel_state == TLS_OUT_OF_INDEXES)
    {
        s_tls_cancel_state = TlsAlloc();
        s_tls_cancel_type = TlsAlloc();
        s_tls_cancel_pending = TlsAlloc();
    }
}

static void SetupCancelState()
{
    InitCancelTLS();
    TlsSetValue(s_tls_cancel_state, (LPVOID)LINUX_PTHREAD_CANCEL_ENABLE);
    TlsSetValue(s_tls_cancel_type, (LPVOID)LINUX_PTHREAD_CANCEL_DEFERRED);
    TlsSetValue(s_tls_cancel_pending, (LPVOID)0);
}

// ============================================================
// Thread Entry Point Wrapper
// ============================================================

struct ThreadStartContext
{
    void *(*start_routine)(void *);
    void *arg;
    uint32_t linux_tid;
    PthreadThreadInternal *thread_info;
};

__attribute__((force_align_arg_pointer)) static unsigned __stdcall ThreadEntryPoint(void *param)
{
    ThreadStartContext *ctx = (ThreadStartContext *)param;

    // We commit the whole stack before any ELF code
    // runs on this thread, matching the main stack from LinuxStack::Setup.
    LinuxStack::CommitCurrentThreadStack();

    void *(*start_routine)(void *) = ctx->start_routine;
    void *arg = ctx->arg;
    PthreadThreadInternal *thread_info = ctx->thread_info;

    delete ctx; // Free context

    // Setup cancel state for this thread
    SetupCancelState();

    // Call the actual thread function
    void *retval = start_routine(arg);

    // Store return value
    if (thread_info)
    {
        thread_info->retval = retval;
        thread_info->exited = 1;
    }

    return 0;
}

// ============================================================
// Thread Attribute Functions
// ============================================================

int PthreadEmu::pthreadAttrInit(void *attr)
{
    if (!attr)
        return LINUX_EINVAL;

    PthreadAttrInternal *a = new PthreadAttrInternal;
    a->detach_state = LINUX_PTHREAD_CREATE_JOINABLE;
    a->stack_size = 0; // Default
    a->sched_policy = 0;
    a->sched_priority = 0;

    *(void **)attr = a;
    return 0;
}

int PthreadEmu::pthreadAttrDestroy(void *attr)
{
    if (!attr)
        return LINUX_EINVAL;

    PthreadAttrInternal *a = *(PthreadAttrInternal **)attr;
    if (a)
    {
        delete a;
        *(void **)attr = nullptr;
    }
    return 0;
}

int PthreadEmu::pthreadAttrSetdetachstate(void *attr, int detachstate)
{
    if (!attr)
        return LINUX_EINVAL;

    PthreadAttrInternal *a = *(PthreadAttrInternal **)attr;
    if (!a)
        return LINUX_EINVAL;

    if (detachstate != LINUX_PTHREAD_CREATE_JOINABLE && detachstate != LINUX_PTHREAD_CREATE_DETACHED)
    {
        return LINUX_EINVAL;
    }

    a->detach_state = detachstate;
    return 0;
}

int PthreadEmu::pthreadAttrGetdetachstate(const void *attr, int *detachstate)
{
    if (!attr || !detachstate)
        return LINUX_EINVAL;

    PthreadAttrInternal *a = *(PthreadAttrInternal **)attr;
    if (!a)
        return LINUX_EINVAL;

    *detachstate = a->detach_state;
    return 0;
}

int PthreadEmu::pthreadAttrSetstacksize(void *attr, size_t stacksize)
{
    if (!attr)
        return LINUX_EINVAL;

    PthreadAttrInternal *a = *(PthreadAttrInternal **)attr;
    if (!a)
        return LINUX_EINVAL;

    a->stack_size = stacksize;
    return 0;
}

int PthreadEmu::pthreadAttrGetstacksize(const void *attr, size_t *stacksize)
{
    if (!attr || !stacksize)
        return LINUX_EINVAL;

    PthreadAttrInternal *a = *(PthreadAttrInternal **)attr;
    if (!a)
        return LINUX_EINVAL;

    *stacksize = a->stack_size;
    return 0;
}

// ============================================================
// Thread Functions
// ============================================================

int PthreadEmu::pthreadCreate(void *thread, const void *attr, void *(*start_routine)(void *), void *arg)
{
    if (!thread || !start_routine)
        return LINUX_EINVAL;

    // Create thread info
    uint32_t linux_tid;
    PthreadThreadInternal *thread_info = PthreadMapper::CreateThread(&linux_tid);
    if (!thread_info)
        return LINUX_ENOMEM;

    // Setup context
    ThreadStartContext *ctx = new ThreadStartContext;
    ctx->start_routine = start_routine;
    ctx->arg = arg;
    ctx->linux_tid = linux_tid;
    ctx->thread_info = thread_info;

    thread_info->start_routine = start_routine;
    thread_info->arg = arg;

    // Get stack size from attributes
    size_t stack_size = 0;
    int detach_state = LINUX_PTHREAD_CREATE_JOINABLE;

    if (attr)
    {
        PthreadAttrInternal *a = *(PthreadAttrInternal **)attr;
        if (a)
        {
            stack_size = a->stack_size;
            detach_state = a->detach_state;
        }
    }

    thread_info->detached = (detach_state == LINUX_PTHREAD_CREATE_DETACHED);

    // Create the thread
    unsigned thread_id;
    HANDLE handle = (HANDLE)_beginthreadex(NULL,                 // Security
                                           (unsigned)stack_size, // Stack size
                                           ThreadEntryPoint,     // Entry point
                                           ctx,                  // Argument
                                           0,                    // Creation flags
                                           &thread_id            // Thread ID
    );

    if (!handle)
    {
        delete ctx;
        PthreadMapper::DestroyThread(linux_tid);
        return LINUX_EAGAIN;
    }

    thread_info->handle = handle;
    thread_info->win_thread_id = thread_id;

    // Register Windows TID -> Linux TID mapping
    // (done internally in mapper)

    // Return Linux thread ID to caller
    *(uint32_t *)thread = linux_tid;

    return 0;
}

int PthreadEmu::pthreadJoin(uint32_t thread, void **retval)
{
    PthreadThreadInternal *thread_info = PthreadMapper::FindThread(thread);
    if (!thread_info)
    {
        return LINUX_ESRCH;
    }

    if (thread_info->detached)
    {
        return LINUX_EINVAL;
    }

    // Check for self-join
    if (GetCurrentThreadId() == thread_info->win_thread_id)
    {
        return LINUX_EDEADLK;
    }

    // Wait for thread to exit
    DWORD result = WaitForSingleObject(thread_info->handle, INFINITE);
    if (result != WAIT_OBJECT_0)
    {
        return LINUX_EINVAL;
    }

    // Get return value
    if (retval)
    {
        *retval = thread_info->retval;
    }

    // Cleanup
    PthreadMapper::DestroyThread(thread);

    return 0;
}

int PthreadEmu::pthreadDetach(uint32_t thread)
{
    PthreadThreadInternal *thread_info = PthreadMapper::FindThread(thread);
    if (!thread_info)
    {
        return LINUX_ESRCH;
    }

    if (thread_info->detached)
    {
        return LINUX_EINVAL; // Already detached
    }

    thread_info->detached = 1;

    // If thread already exited, clean up now
    if (thread_info->exited)
    {
        PthreadMapper::DestroyThread(thread);
    }

    return 0;
}

void PthreadEmu::pthreadExit(void *retval)
{
    uint32_t linux_tid = PthreadMapper::GetCurrentLinuxTid();
    PthreadThreadInternal *thread_info = PthreadMapper::FindThread(linux_tid);

    if (thread_info)
    {
        thread_info->retval = retval;
        thread_info->exited = 1;

        // If detached, clean up
        if (thread_info->detached)
        {
            PthreadMapper::DestroyThread(linux_tid);
        }
    }

    _endthreadex(0);
}

uint32_t PthreadEmu::pthreadSelf()
{
    return PthreadMapper::GetCurrentLinuxTid();
}

int PthreadEmu::pthreadEqual(uint32_t t1, uint32_t t2)
{
    return (t1 == t2) ? 1 : 0;
}

// ============================================================
// Thread Cancel Functions (Limited Support)
// ============================================================

int PthreadEmu::pthreadCancel(uint32_t thread)
{
    PthreadThreadInternal *thread_info = PthreadMapper::FindThread(thread);
    if (!thread_info)
    {
        return LINUX_ESRCH;
    }

    // Set cancel pending flag
    // Note: This is a simplified implementation
    // Full implementation would require more complex handling

    // For now, just return success without actually canceling
    // Real cancellation is very difficult to implement safely on Windows
    return 0;
}

int PthreadEmu::pthreadSetcancelstate(int state, int *oldstate)
{
    InitCancelTLS();

    if (state != LINUX_PTHREAD_CANCEL_ENABLE && state != LINUX_PTHREAD_CANCEL_DISABLE)
    {
        return LINUX_EINVAL;
    }

    if (oldstate)
    {
        *oldstate = (int)(intptr_t)TlsGetValue(s_tls_cancel_state);
    }

    TlsSetValue(s_tls_cancel_state, (LPVOID)(intptr_t)state);
    return 0;
}

int PthreadEmu::pthreadSetcanceltype(int type, int *oldtype)
{
    InitCancelTLS();

    if (type != LINUX_PTHREAD_CANCEL_DEFERRED && type != LINUX_PTHREAD_CANCEL_ASYNCHRONOUS)
    {
        return LINUX_EINVAL;
    }

    if (oldtype)
    {
        *oldtype = (int)(intptr_t)TlsGetValue(s_tls_cancel_type);
    }

    TlsSetValue(s_tls_cancel_type, (LPVOID)(intptr_t)type);
    return 0;
}

void PthreadEmu::pthreadTestcancel()
{
    InitCancelTLS();

    int state = (int)(intptr_t)TlsGetValue(s_tls_cancel_state);
    int pending = (int)(intptr_t)TlsGetValue(s_tls_cancel_pending);

    if (state == LINUX_PTHREAD_CANCEL_ENABLE && pending)
    {
        // Cancel was requested and we're at a cancellation point
        PthreadEmu::pthreadExit((void *)-1); // PTHREAD_CANCELED
    }
}

// ============================================================
// Scheduling Functions (Limited Support)
// ============================================================

int PthreadEmu::pthreadSetSchedparam(uint32_t thread, int policy, const void *param)
{
    PthreadThreadInternal *thread_info = PthreadMapper::FindThread(thread);
    if (!thread_info)
    {
        return LINUX_ESRCH;
    }

    // Windows thread priority mapping is approximate
    // For now, accept but mostly ignore
    return 0;
}

int PthreadEmu::pthreadGetSchedparam(uint32_t thread, int *policy, void *param)
{
    PthreadThreadInternal *thread_info = PthreadMapper::FindThread(thread);
    if (!thread_info)
    {
        return LINUX_ESRCH;
    }

    if (policy)
    {
        *policy = 0; // SCHED_OTHER equivalent
    }

    return 0;
}

int PthreadEmu::schedYield()
{
    SwitchToThread();
    return 0;
}

// ============================================================
// Concurrency Functions
// ============================================================

static int s_concurrency_level = 0;

int PthreadEmu::pthreadGetConcurrency()
{
    return s_concurrency_level;
}

int PthreadEmu::pthreadSetConcurrency(int new_level)
{
    if (new_level < 0)
    {
        return LINUX_EINVAL;
    }
    s_concurrency_level = new_level;
    return 0;
}
