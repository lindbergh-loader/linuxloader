// Aladdin HASP HL dongle, emulated at the HASP API level: the game's own
// Dongle* layer runs unchanged on top of it.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rawthrills.h"
#include "../log/log.h"

#define HASP_STATUS_OK 0
#define HASP_FEATURE_NOT_FOUND 1
#define HASP_HASP_NOT_FOUND 7
#define HASP_TOO_SHORT 8

#define ANSWER_KEY_SIZE 16
#define MAX_ANSWER_KEY_SIZE 32
#define MEMORY_DUMP "hhl_mem.dmp"
// The dongle memory as the game last wrote it (its factory setup flag, ...),
// in the game directory: read over the recorded one.
#define MEMORY_SAVED "dongle_memory.dmp"
#define MEMORY_SIZE 128
#define HASP_SESSION_INFO "<haspid>287339817</haspid>"

// Dongle memory offsets the games read.
#define DONGLE_CAB_TYPE 0x1d
#define DONGLE_FLAG 0x1e
#define DONGLE_SERIAL 0x3c

#define CAB_TYPE 4
#define CAB_SERIAL 0xc0ffee

static uint32_t haspFeature;
static uint32_t haspMemoryFileId;
static char answersDir[PATH_MAX];
static uint32_t answerKeySize = ANSWER_KEY_SIZE;
static uint8_t memory[MEMORY_SIZE];
static int memoryLoaded;
static char memorySavedPath[PATH_MAX];

static int haspLogin(uint32_t featureId, const void *vendorCode, uint32_t *handle)
{
    (void)vendorCode;
    (void)handle;
    return featureId == haspFeature ? HASP_STATUS_OK : HASP_FEATURE_NOT_FOUND;
}

static int haspOk(void)
{
    return HASP_STATUS_OK;
}

// A dongle memory file, read whole into memory: 1 if it was.
static int readMemory(const char *path)
{
    uint8_t read[MEMORY_SIZE];
    FILE *f = fopen(path, "rb");
    int ok = f && fread(read, 1, sizeof(read), f) == sizeof(read);

    if (f)
        fclose(f);
    if (ok)
        memcpy(memory, read, sizeof(read));
    return ok;
}

// The dongle memory: the recorded one, else all zeros except the cabinet
// type, a flag and the cabinet serial number; then what the game wrote.
static void loadMemory(const RtGame *game)
{
    uint32_t serial = CAB_SERIAL;

    if (!memoryLoaded)
    {
        memory[DONGLE_CAB_TYPE] = CAB_TYPE;
        memory[DONGLE_FLAG] = 1;
        memcpy(memory + DONGLE_SERIAL, &serial, sizeof(serial));
        if (game->haspFactorySetupOffset && game->haspFactorySetupOffset < MEMORY_SIZE)
            memory[game->haspFactorySetupOffset] = 1;
    }
    snprintf(memorySavedPath, sizeof(memorySavedPath), "%s/" MEMORY_SAVED, rtGameDir());
    readMemory(memorySavedPath);
}

// Zeros past the memory's end.
static int haspRead(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, void *buffer)
{
    (void)handle;
    if (fileId != haspMemoryFileId)
        return HASP_STATUS_OK;
    memset(buffer, 0, length);
    if (offset < MEMORY_SIZE)
        memcpy(buffer, memory + offset, length < MEMORY_SIZE - offset ? length : MEMORY_SIZE - offset);
    return HASP_STATUS_OK;
}

// Kept, and saved for the next runs: the factory setup (from the test menu)
// sets its flag there, which the attract mode checks.
static int haspWrite(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, const void *buffer)
{
    FILE *f;
    uint32_t n = offset < MEMORY_SIZE && length > MEMORY_SIZE - offset ? MEMORY_SIZE - offset : length;

    (void)handle;
    // Saved when it changes.
    if (fileId != haspMemoryFileId || offset >= MEMORY_SIZE || !memcmp(memory + offset, buffer, n))
        return HASP_STATUS_OK;
    memcpy(memory + offset, buffer, n);
    if (!(f = fopen(memorySavedPath, "wb")))
    {
        log_warn("Raw Thrills: cannot save the dongle memory %s", memorySavedPath);
        return HASP_STATUS_OK;
    }
    fwrite(memory, 1, sizeof(memory), f);
    fclose(f);
    return HASP_STATUS_OK;
}

static int haspGetSessionInfo(uint32_t handle, const char *format, char **info)
{
    (void)handle;
    (void)format;
    *info = strdup(HASP_SESSION_INFO);
    return HASP_STATUS_OK;
}

static int haspFree(void *info)
{
    free(info);
    return HASP_STATUS_OK;
}

// hasp_encrypt/hasp_decrypt: the dongle's AES key never leaves it, so the
// results come from recorded answers, looked up by the first bytes of the
// input (the data is a hash or a secret, unique in those bytes). Some games
// also check the dongle by encrypting a buffer (which must change) and
// decrypting it back: those are not recorded, and are answered with a
// stand-in cipher that is its own inverse (an XOR keystream).
static void standInCrypt(uint8_t *buffer, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
        buffer[i] ^= 0xa5 ^ (uint8_t)(i * 0x3b);
}
static int haspCrypt(uint32_t handle, uint8_t *buffer, uint32_t length)
{
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/", answersDir);
    FILE *f;

    (void)handle;
    // RT_DONGLE_TRACE=1: each question on stderr (a game whose answers are
    // missing, see docs/dongle-recording.md).
    if (getenv("RT_DONGLE_TRACE"))
    {
        fprintf(stderr, "Raw Thrills: dongle crypt, %u bytes:", length);
        for (uint32_t i = 0; i < length && i < 32; i++)
            fprintf(stderr, " %02x", buffer[i]);
        fprintf(stderr, "\n");
    }
    if (length < ANSWER_KEY_SIZE)
        return HASP_TOO_SHORT;
    // Shorter than the recorded answers' names: never recorded.
    if (length < answerKeySize)
    {
        standInCrypt(buffer, length);
        return HASP_STATUS_OK;
    }
    for (uint32_t i = 0; i < answerKeySize && n + 2 < (int)sizeof(path); i++)
        n += snprintf(path + n, sizeof(path) - n, "%02x", buffer[i]);
    if (!(f = fopen(path, "rb")))
    {
        log_warn("Raw Thrills: no recorded dongle answer %s, using a stand-in", path);
        standInCrypt(buffer, length);
        return HASP_STATUS_OK;
    }
    uint8_t *answer = malloc(length);
    size_t got = answer ? fread(answer, 1, length, f) : 0;
    int extra = fgetc(f) != EOF;
    fclose(f);
    if (got != length || extra)
    {
        log_warn("Raw Thrills: recorded dongle answer %s is not %u bytes", path, length);
        free(answer);
        return HASP_HASP_NOT_FOUND;
    }
    memcpy(buffer, answer, length);
    free(answer);
    return HASP_STATUS_OK;
}

static void loadAnswers(const char *dir, int keySize)
{
    char path[PATH_MAX];

    if (keySize > 0 && keySize <= MAX_ANSWER_KEY_SIZE)
        answerKeySize = keySize;
    snprintf(answersDir, sizeof(answersDir), "%s/%s", rtGameDir(), dir);
    snprintf(path, sizeof(path), "%s/" MEMORY_DUMP, answersDir);
    if (!(memoryLoaded = readMemory(path)))
        log_warn("Raw Thrills: cannot read the dongle memory %s", path);
    rtDetour("hasp_encrypt", haspCrypt);
    rtDetour("hasp_decrypt", haspCrypt);
}

// The dumps ship with decrypted data, so DongleEncrypt/DongleDecrypt only
// need to report success (status field at +0x10 of the dongle object).
static int dongleCrypt(uint8_t *dongle)
{
    *(uint32_t *)(dongle + 0x10) = 0;
    return 0;
}

void rtInstallDongle(const RtGame *game)
{
    haspFeature = game->haspFeature;
    haspMemoryFileId = game->haspMemoryFileId;
    if (game->haspAnswers)
        loadAnswers(game->haspAnswers, game->haspAnswerKeySize);
    loadMemory(game);
    rtDetour("hasp_login", haspLogin);
    rtDetour("hasp_logout", haspOk);
    rtDetour("hasp_read", haspRead);
    rtDetour("hasp_write", haspWrite);
    rtDetour("hasp_get_sessioninfo", haspGetSessionInfo);
    rtDetour("hasp_free", haspFree);
    // Games with recorded answers get them through hasp_encrypt/decrypt.
    if (!game->haspAnswers)
    {
        rtDetour("DongleEncrypt", dongleCrypt);
        rtDetour("DongleDecrypt", dongleCrypt);
    }
    rtDetour("DongleWriteLoop", haspOk);
    rtDetour("DongleNoise", haspOk);
    rtDetour("DongleWriteHLFeature", haspOk);
    if (game->haspReadPatch)
        rtDetourAddress(game->haspReadPatch, haspRead);
    if (game->haspWritePatch)
        rtDetourAddress(game->haspWritePatch, haspWrite);
    if (game->haspSessionInfoPatch)
        rtDetourAddress(game->haspSessionInfoPatch, haspGetSessionInfo);
}
