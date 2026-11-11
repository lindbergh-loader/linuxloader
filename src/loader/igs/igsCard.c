// Hero of Robots' card reader (libcasmcard, Castles' PC/SC wrapper over
// pcsclite; the player's card a Java or SLE4442 card): a reader there, no
// card in it. The game calls the library from its CardReaderMgr: it lists
// the readers (CasListReaders, twice: the size, then the names), connects to
// each (CasConnect) and polls them (CasStatus); a reader missing or
// unavailable (SCARD_E_NO_READERS_AVAILABLE, SCARD_E_READER_UNAVAILABLE)
// shows "Card reader not installed" and refuses coins.
//
// The reader also holds the operator's chip, a Java card of prepaid plays
// (SmartCard, its thread SmartCardProcess): the game checks it is in
// (CardReaderMgr::hasCard: "Operating chip not installed" else) and asks it
// for its credit, through an RSA challenge, debiting one per play
// (SmartCard::Authentication(debit): the credit, negative an error). Both
// stood in for: a chip there, its credit 9999 at start.
//
// Its card dispenser and its robot cards' QR scanner are on the PC's serial
// ports, through the game's RS232 class (an object: byte 0 open, byte 1 a
// command written, +8 the fd). RS232::initialize is stood in for: a
// socketpair, the game's end its fd, a thread of ours the device on the
// other end; which device, by the caller (CardDispenser's or QRCodeReader's
// code, igsGame.h's rs232Dispenser and rs232Qr).
//
// The dispenser (2 bytes each way): 01 37 its status (23 ready), 01 38 its
// sensor (30 no card at the exit, 31 one there), 01 39 its version (xx 14
// 01 16: 2014/01/16, the firmware the game ships: no update), 01 42 a card
// out (xx 50 done, 4b failed). The scanner: ESC 0 (stop, and the game's status ping) and ESC 1
// (scan), and its settings, each answered 06 (ACK); a card read, its QR's
// text and CR LF (igsCardScan, IGS_QR=code for tests).

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "igsBinder.h"
#include "igs.h"
#include "../common/importHook.h"
#include "../patching/flowControl.h"

#define SCARD_S_SUCCESS 0
#define SCARD_E_INSUFFICIENT_BUFFER 0x80100008u
#define SCARD_E_NO_SMARTCARD 0x8010000cu
#define SCARD_W_REMOVED_CARD 0x80100069u

static const char readers[] = "CASTLES EZ100PU 00 00\0";
static int trace;

static void say(const char *what, uint32_t result)
{
    if (trace)
        printf("IGS card: %s -> %08x\n", what, result);
}

__attribute__((force_align_arg_pointer)) static uint32_t casEstablishContext(uint32_t scope, const void *r1,
                                                                              const void *r2, uint32_t *context)
{
    (void)scope, (void)r1, (void)r2;
    if (context)
        *context = 0x1c0;
    say("CasEstablishContext", SCARD_S_SUCCESS);
    return SCARD_S_SUCCESS;
}

__attribute__((force_align_arg_pointer)) static uint32_t casReleaseContext(uint32_t context)
{
    (void)context;
    return SCARD_S_SUCCESS;
}

__attribute__((force_align_arg_pointer)) static uint32_t casListReaders(uint32_t context, const char *groups,
                                                                         char *names, uint32_t *length)
{
    uint32_t size = sizeof(readers); // (the list's final NUL included)

    (void)context, (void)groups;
    if (!length)
        return SCARD_E_INSUFFICIENT_BUFFER;
    if (names && *length < size)
    {
        *length = size;
        return SCARD_E_INSUFFICIENT_BUFFER;
    }
    if (names)
        memcpy(names, readers, size);
    *length = size;
    say("CasListReaders", SCARD_S_SUCCESS);
    return SCARD_S_SUCCESS;
}

// a reader with no card in it
__attribute__((force_align_arg_pointer)) static uint32_t casConnect(uint32_t context, const char *reader,
                                                                     uint32_t share, uint32_t protocols,
                                                                     uint32_t *card, uint32_t *active)
{
    (void)context, (void)reader, (void)share, (void)protocols, (void)active;
    if (card)
        *card = 0;
    say("CasConnect", SCARD_E_NO_SMARTCARD);
    return SCARD_E_NO_SMARTCARD;
}

__attribute__((force_align_arg_pointer)) static uint32_t casNoCard(void)
{
    return SCARD_W_REMOVED_CARD;
}

__attribute__((force_align_arg_pointer)) static uint32_t casDone(void)
{
    return SCARD_S_SUCCESS;
}

// the SLE4442 memory card's: none (their failure, -1)
__attribute__((force_align_arg_pointer)) static int sleFailed(void)
{
    return -1;
}

static int chipCredit = 9999;

__attribute__((force_align_arg_pointer)) static int chipThere(void *manager, int reader)
{
    (void)manager, (void)reader;
    return 1;
}

__attribute__((force_align_arg_pointer)) static int chipAuthentication(void *card, int debit)
{
    (void)card;
    if ((debit & 0xff) && chipCredit > 0)
        chipCredit--;
    say("SmartCard::Authentication", (uint32_t)chipCredit);
    return chipCredit;
}

// --- the serial devices ---

enum { DEVICE_DISPENSER = 1, DEVICE_QR };

static const IgsGame *cardGame;
static int qrFd = -1; // the scanner's end, once open
static pthread_mutex_t qrLock = PTHREAD_MUTEX_INITIALIZER;
static char qrPending[128];
static int qrTriggered;
static int dispensed; // cards given out

static void deviceWrite(int fd, const void *data, size_t size)
{
    while (size)
    {
        ssize_t n = write(fd, data, size);
        if (n <= 0 && errno != EINTR)
            return;
        if (n > 0)
        {
            data = (const char *)data + n;
            size -= (size_t)n;
        }
    }
}

static void qrSendPending(void)
{
    // (qrLock held)
    if (qrFd >= 0 && qrTriggered && qrPending[0])
    {
        deviceWrite(qrFd, qrPending, strlen(qrPending));
        deviceWrite(qrFd, "\r\n", 2);
        if (trace)
            printf("IGS card: QR %s scanned\n", qrPending);
        qrPending[0] = '\0';
    }
}

// A robot card held to the scanner: its QR's text, sent when the game scans.
void igsCardScan(const char *code)
{
    pthread_mutex_lock(&qrLock);
    snprintf(qrPending, sizeof(qrPending), "%s", code);
    qrSendPending();
    pthread_mutex_unlock(&qrLock);
}

int igsCardsDispensed(void)
{
    return dispensed;
}

int igsCardScanning(void)
{
    return qrFd >= 0 && qrTriggered;
}

int igsCardPending(void)
{
    pthread_mutex_lock(&qrLock);
    int pending = qrPending[0] != '\0';
    pthread_mutex_unlock(&qrLock);
    return pending;
}

static void *dispenserThread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    uint8_t command[2];
    size_t have = 0;
    time_t cardOutAt = 0;

    for (;;)
    {
        ssize_t n = read(fd, command + have, sizeof(command) - have);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        have += (size_t)n;
        if (have < 2)
            continue;
        have = 0;
        if (trace >= 2)
            printf("IGS card: %ld dispenser <- %02x %02x\n", (long)time(NULL) % 1000, command[0], command[1]);
        uint8_t reply[4] = {0x01, 0x23, 0, 0};
        size_t length = 2;
        switch (command[1])
        {
        case 0x37: // status: ready
            break;
        case 0x38: // sensor: a card given out waits at the exit about a
                   // second (31), then taken (30)
            reply[1] = time(NULL) - cardOutAt < 2 ? 0x31 : 0x30;
            break;
        case 0x39: // version: 2014/01/16
            reply[1] = 0x14, reply[2] = 0x01, reply[3] = 0x16;
            length = 4;
            break;
        case 0x42: // a card out (4b: failed, the game's CardDispenser::OutputCard)
            reply[1] = 0x50;
            cardOutAt = time(NULL);
            dispensed++;
            igsBinderDispensed();
            printf("IGS card: the dispenser gives out card %d\n", dispensed);
            break;
        default:
            if (trace)
                printf("IGS card: dispenser command %02x %02x\n", command[0], command[1]);
            break;
        }
        deviceWrite(fd, reply, length);
    }
    close(fd);
    return NULL;
}

static void *qrThread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    uint8_t buffer[256];

    pthread_mutex_lock(&qrLock);
    qrFd = fd;
    pthread_mutex_unlock(&qrLock);
    for (;;)
    {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        if (trace >= 2)
        {
            printf("IGS card: %ld QR <-", (long)time(NULL) % 1000);
            for (ssize_t i = 0; i < n && i < 24; i++)
                printf(" %02x", buffer[i]);
            printf("\n");
        }
        pthread_mutex_lock(&qrLock);
        for (ssize_t i = 0; i + 1 < n; i++)
            if (buffer[i] == 0x1b && (buffer[i + 1] == '0' || buffer[i + 1] == '1'))
                qrTriggered = buffer[i + 1] == '1';
        deviceWrite(fd, "\x06", 1); // every command acknowledged
        qrSendPending();
        pthread_mutex_unlock(&qrLock);
    }
    pthread_mutex_lock(&qrLock);
    if (qrFd == fd)
        qrFd = -1;
    pthread_mutex_unlock(&qrLock);
    close(fd);
    return NULL;
}

__attribute__((force_align_arg_pointer)) static int rs232Initialize(uint8_t *port, const char *name, int baud,
                                                                     char parity, char bits, char stop, int a, int b,
                                                                     int c)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int kind = caller >= cardGame->rs232Dispenser[0] && caller < cardGame->rs232Dispenser[1] ? DEVICE_DISPENSER
               : caller >= cardGame->rs232Qr[0] && caller < cardGame->rs232Qr[1]           ? DEVICE_QR
                                                                                            : 0;
    int fds[2];
    pthread_t thread;

    (void)baud, (void)parity, (void)bits, (void)stop, (void)a, (void)b, (void)c;
    if (trace)
        printf("IGS card: %s opened by %08lx: %s\n", name ? name : "?", (unsigned long)caller,
               kind == DEVICE_DISPENSER ? "the dispenser" : kind == DEVICE_QR ? "the QR scanner" : "nothing there");
    if (!kind || socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return 0;
    fcntl(fds[0], F_SETFL, O_NONBLOCK); // (as the game's own: O_NDELAY)
    port[0] = 1;
    port[1] = 0;
    memcpy(port + 8, &fds[0], 4);
    pthread_create(&thread, NULL, kind == DEVICE_DISPENSER ? dispenserThread : qrThread, (void *)(intptr_t)fds[1]);
    pthread_detach(thread);
    return 1;
}

// IGS_QR=seconds:code,... (tests): those cards scanned at those times
static void *qrTestThread(void *arg)
{
    const char *list = arg;
    struct timespec start, now;

    clock_gettime(CLOCK_MONOTONIC, &start);
    while (list && *list)
    {
        char *end;
        double at = strtod(list, &end);
        char code[128];
        size_t n = 0;
        if (*end != ':')
            break;
        for (end++; *end && *end != ',' && n < sizeof(code) - 1; end++)
            code[n++] = *end;
        code[n] = '\0';
        list = *end == ',' ? end + 1 : end;
        for (;;)
        {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - start.tv_sec + (now.tv_nsec - start.tv_nsec) / 1e9 >= at)
                break;
            usleep(100000);
        }
        igsCardScan(code);
    }
    return NULL;
}

void igsCardInstall(const IgsGame *game)
{
    static const char *const failing[] = {"SLE4442_Compare_Verification_Data", "SLE4442_Read_Main_Memory",
                                          "SLE4442_Read_Protection_Memory", "SLE4442_Update_Main_Memory",
                                          "SLE4442_Update_Security_Memory", "SLE4442_Write_Protection_Memory"};

    trace = getenv("IGS_TRACE") ? atoi(getenv("IGS_TRACE")) : 0;
    hookExecutableImport("CasEstablishContext", (void *)casEstablishContext);
    hookExecutableImport("CasReleaseContext", (void *)casReleaseContext);
    hookExecutableImport("CasListReaders", (void *)casListReaders);
    hookExecutableImport("CasConnect", (void *)casConnect);
    hookExecutableImport("CasStatus", (void *)casNoCard);
    hookExecutableImport("CasTransmit", (void *)casNoCard);
    hookExecutableImport("CasDisconnect", (void *)casDone);
    hookExecutableImport("CasGetRdrType", (void *)casDone);
    for (size_t i = 0; i < sizeof(failing) / sizeof(failing[0]); i++)
        hookExecutableImport(failing[i], (void *)sleFailed);
    if (game->casChip[0])
    {
        detourFunction(game->casChip[0], (void *)chipThere);
        detourFunction(game->casChip[1], (void *)chipAuthentication);
    }
    cardGame = game;
    if (game->rs232Initialize)
        detourFunction(game->rs232Initialize, (void *)rs232Initialize);
    if (getenv("IGS_QR"))
    {
        pthread_t thread;
        pthread_create(&thread, NULL, qrTestThread, (void *)getenv("IGS_QR"));
        pthread_detach(thread);
    }
}
