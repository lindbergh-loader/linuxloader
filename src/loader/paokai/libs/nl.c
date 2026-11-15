// libNL.so.1.6 (scripts/linux/build-gogostrike-libs.sh): HawkNL 1.6's API as
// Go Go Strike calls it, without a network. The lanes' link to their server
// (a linked cabinet's scores, its setup) has no peer: sockets do not open.
// Threads and mutexes are real (pthreads).

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char NLboolean;
typedef int NLint;
typedef int NLenum;
typedef int NLsocket;
typedef void *NLthreadID;
typedef void *(*NLThreadFunc)(void *data);
typedef pthread_mutex_t *NLmutex;
typedef struct
{
    unsigned char addr[32];
    NLboolean valid;
} NLaddress;

#define NL_INVALID (-1)
#define NL_NO_NETWORK 0x5001
#define NL_INVALID_SOCKET 0x5004

static NLenum error;

NLboolean nlInit(void)
{
    return 1;
}

void nlShutdown(void)
{
}

NLboolean nlSelectNetwork(NLenum network)
{
    (void)network;
    return 1;
}

const char *nlGetString(NLenum name)
{
    (void)name;
    return "HawkNL 1.6 (linuxloader, no network)";
}

NLenum nlGetError(void)
{
    return error;
}

const char *nlGetErrorStr(NLenum err)
{
    (void)err;
    return "no network";
}

NLint nlGetSystemError(void)
{
    return 0;
}

const char *nlGetSystemErrorStr(NLint err)
{
    (void)err;
    return "no network";
}

NLsocket nlOpen(unsigned short port, NLenum type)
{
    (void)port;
    (void)type;
    error = NL_NO_NETWORK;
    return NL_INVALID;
}

static NLboolean noSocket(void)
{
    error = NL_INVALID_SOCKET;
    return 0;
}

NLboolean nlClose(NLsocket socket)
{
    (void)socket;
    return noSocket();
}

NLboolean nlListen(NLsocket socket)
{
    (void)socket;
    return noSocket();
}

NLsocket nlAcceptConnection(NLsocket socket)
{
    (void)socket;
    noSocket();
    return NL_INVALID;
}

NLboolean nlConnect(NLsocket socket, const NLaddress *address)
{
    (void)socket;
    (void)address;
    return noSocket();
}

NLint nlRead(NLsocket socket, void *buffer, NLint nbytes)
{
    (void)socket;
    (void)buffer;
    (void)nbytes;
    noSocket();
    return NL_INVALID;
}

NLint nlWrite(NLsocket socket, const void *buffer, NLint nbytes)
{
    (void)socket;
    (void)buffer;
    (void)nbytes;
    noSocket();
    return NL_INVALID;
}

// Addresses: the text form kept as is, none of them reachable.
char *nlAddrToString(const NLaddress *address, char *string)
{
    strcpy(string, address && address->valid ? (const char *)address->addr : "0.0.0.0:0");
    return string;
}

NLboolean nlStringToAddr(const char *string, NLaddress *address)
{
    memset(address, 0, sizeof(*address));
    strncpy((char *)address->addr, string ? string : "", sizeof(address->addr) - 1);
    address->valid = 1;
    return 1;
}

NLboolean nlGetRemoteAddr(NLsocket socket, NLaddress *address)
{
    (void)socket;
    memset(address, 0, sizeof(*address));
    return noSocket();
}

NLboolean nlGetLocalAddr(NLsocket socket, NLaddress *address)
{
    return nlGetRemoteAddr(socket, address);
}

NLaddress *nlGetAllLocalAddr(NLint *count)
{
    static NLaddress local = {"127.0.0.1:0", 1};

    *count = 1;
    return &local;
}

NLboolean nlSetLocalAddr(const NLaddress *address)
{
    (void)address;
    return 1;
}

// Groups: empty, polls find nothing.
NLint nlGroupCreate(void)
{
    static NLint next;

    return next++;
}

NLboolean nlGroupDestroy(NLint group)
{
    (void)group;
    return 1;
}

NLboolean nlGroupAddSocket(NLint group, NLsocket socket)
{
    (void)group;
    (void)socket;
    return 1;
}

NLboolean nlGroupDeleteSocket(NLint group, NLsocket socket)
{
    (void)group;
    (void)socket;
    return 1;
}

NLint nlPollGroup(NLint group, NLenum name, NLsocket *sockets, NLint number, NLint timeout)
{
    (void)group;
    (void)name;
    (void)sockets;
    (void)number;
    (void)timeout;
    return 0;
}

NLthreadID nlThreadCreate(NLThreadFunc func, void *data, NLboolean joinable)
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, func, data))
        return NULL;
    if (!joinable)
        pthread_detach(thread);
    return (NLthreadID)thread;
}

void nlThreadYield(void)
{
    sched_yield();
}

NLboolean nlThreadJoin(NLthreadID threadID, void **status)
{
    return !pthread_join((pthread_t)threadID, status);
}

NLboolean nlMutexInit(NLmutex *mutex)
{
    *mutex = malloc(sizeof(**mutex));
    return *mutex && !pthread_mutex_init(*mutex, NULL);
}

NLboolean nlMutexLock(NLmutex *mutex)
{
    return !pthread_mutex_lock(*mutex);
}

NLboolean nlMutexUnlock(NLmutex *mutex)
{
    return !pthread_mutex_unlock(*mutex);
}

NLboolean nlMutexDestroy(NLmutex *mutex)
{
    pthread_mutex_destroy(*mutex);
    free(*mutex);
    *mutex = NULL;
    return 1;
}
