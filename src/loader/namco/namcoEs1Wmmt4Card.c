// WMMT4's Banapassport (Amusement IC) card reader, after the Pacloader fork
// (banapassport.cpp, nbgiCodec.cpp, es1Wmmt4Card.cpp). The game drives the
// reader through its BngRw library (linked in): its calls are answered here,
// their completion callbacks called at once, and a card is on the reader
// as soon as the game waits for one.
//
// The card: a FeliCa chip id (IDm and PMm, 32 hex digits) and an NBGIC
// serial, from which its 20-digit access code is computed, as the card's
// NBGIC header (Blowfish, the game's libcrypto) the game decodes it from.
// Made once per installation, kept in save/server/card.ini (ChipId,
// Serial): the game server (namcoEs1Wmmt4Rpc.c) knows the user by it.

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "namcoEs1Wmmt4Server.h"
#include "../../minhook/include/MinHook.h"
#include "../log/log.h"

#define CARD_FILE "save/server/card.ini"
#define RECORD_SIZE 168
#define IDM 0x1c
#define PMM 0x24
#define CHIP_ID 0x2c
#define ACCESS_CODE 0x50
#define NBGIC_HEADER 0x13f // in the auth request's buffer

#define BNG_OK 1
#define BNG_BAD_DEVICE (-100)
#define BNG_NO_CARD (-102)

// A real card's record ("NBGIC6" at 0x78 is the card type).
static uint8_t record[RECORD_SIZE] = {
    0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x2e, 0x58, 0x32, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x5c, 0x97, 0x44, 0xf0, 0x88, 0x04, 0x00, 0x43, 0x26,
    0x2c, 0x33, 0x00, 0x04, 0x06, 0x10, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4e, 0x42, 0x47, 0x49, 0x43, 0x36, 0x00, 0x00, 0xfa, 0xe9, 0x69, 0x00, 0xf6, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static uint8_t header[16];
static int cardPresent;

// ---------------------------------------------------------------------------
// NBGIC6: its data key (the header's Blowfish key), and the access code's
// key (prefix 307, a bit permutation and an addend).

static const uint8_t dataKey[56] = {
    0x4F, 0xFE, 0x5E, 0x7C, 0x5E, 0x3B, 0x70, 0x0D, 0xAE, 0xB6, 0x49, 0xDE, 0x37, 0x96, 0xB2, 0xEC, 0xB7, 0x13, 0xDE,
    0x8F, 0x43, 0x2E, 0xED, 0x77, 0xBA, 0x84, 0x96, 0x1D, 0xA7, 0xC0, 0x88, 0xA4, 0x6C, 0xFB, 0x5A, 0xC8, 0x2E, 0xE0,
    0xF0, 0xB4, 0xF3, 0xE8, 0x54, 0x69, 0x7E, 0x77, 0x20, 0xBE, 0x1A, 0x8C, 0x48, 0x6E, 0x40, 0x7C, 0x7D, 0xD6,
};
static const uint8_t permutation[56] = {
    0x32, 0x03, 0x30, 0x37, 0x16, 0x08, 0x29, 0x09, 0x05, 0x19, 0x13, 0x2E, 0x1D, 0x18, 0x23, 0x2B, 0x15, 0x2A, 0x17,
    0x0F, 0x0B, 0x06, 0x26, 0x14, 0x1E, 0x21, 0x04, 0x0D, 0x00, 0x02, 0x0C, 0x25, 0x34, 0x28, 0x07, 0x1C, 0x1A, 0x2D,
    0x2C, 0x0E, 0x24, 0x31, 0x1B, 0x10, 0x11, 0x2F, 0x27, 0x0A, 0x01, 0x1F, 0x20, 0x33, 0x36, 0x12, 0x35, 0x22,
};
#define ACCESS_ADDEND 0x5B0D3FEA400D5700ULL
#define ACCESS_PRODUCT 307

// The header: version 2.00, "NBGIC6", then (serial, unknown, flags, xor
// check) Blowfish-encrypted, each 32-bit half byte-swapped around it.
static int encodeHeader(uint32_t serial, uint16_t unknown, uint8_t flags)
{
    void *crypto = dlopen("libcrypto.so.0.9.8", RTLD_NOW | RTLD_NOLOAD);
    void (*setKey)(void *, int, const uint8_t *) = crypto ? dlsym(crypto, "BF_set_key") : NULL;
    void (*ecb)(const uint8_t *, uint8_t *, void *, int) = crypto ? dlsym(crypto, "BF_ecb_encrypt") : NULL;
    uint32_t schedule[18 + 4 * 256];
    uint8_t payload[8] = {(uint8_t)serial, (uint8_t)(serial >> 8), (uint8_t)(serial >> 16), (uint8_t)(serial >> 24),
                          (uint8_t)unknown, (uint8_t)(unknown >> 8), flags, 0},
            in[8], out[8];

    if (!setKey || !ecb)
        return 0;
    for (int i = 0; i < 7; i++)
        payload[7] ^= payload[i];
    setKey(schedule, sizeof(dataKey), dataKey);
    for (int half = 0; half < 2; half++)
        for (int b = 0; b < 4; b++)
            in[half * 4 + b] = payload[half * 4 + 3 - b];
    ecb(in, out, schedule, 1); // BF_ENCRYPT
    memset(header, 0, sizeof(header));
    header[0] = 0x00;
    header[1] = 0x02;
    memcpy(header + 2, "NBGIC6", 6);
    for (int half = 0; half < 2; half++)
        for (int b = 0; b < 4; b++)
            header[8 + half * 4 + b] = out[half * 4 + 3 - b];
    return 1;
}

static void setBit(uint8_t data[8], int bit, int value)
{
    uint8_t mask = (uint8_t)(1u << (7 - bit % 8));
    if (value)
        data[bit / 8 + 1] |= mask;
    else
        data[bit / 8 + 1] &= (uint8_t)~mask;
}

static void setBits(uint8_t data[8], int start, int bits, uint32_t value)
{
    for (int i = 0; i < bits; i++)
        setBit(data, start + i, (value >> (bits - 1 - i)) & 1);
}

// The access code: "307" and 17 digits.
static void encodeAccessCode(uint32_t serial, char code[21])
{
    uint32_t product = ACCESS_PRODUCT;
    uint8_t bcc = 0, data[8] = {0}, temp[8] = {0}, result[8], addend[8];
    uint32_t swapped = (serial >> 24) | ((serial >> 8) & 0xff00) | ((serial << 8) & 0xff0000) | (serial << 24);
    unsigned carry = 0;

    for (int i = 0; i < 4; i++)
        bcc ^= (uint8_t)(product >> (i * 8)) ^ (uint8_t)(serial >> (i * 8));
    product = ((product & 3) << 8) | (product >> 2);
    setBits(data, 0, 2, (bcc % 11) & 3);
    setBits(data, 2, 10, product);
    setBits(data, 12, 4, 0);
    setBits(data, 16, 32, swapped);
    setBits(data, 48, 8, bcc);
    for (int i = 0; i < 56; i++)
        if (data[i / 8 + 1] & (1u << (7 - i % 8)))
            setBit(temp, permutation[i] % 56, 1);
    // temp + the addend's bytes in memory order (little-endian), big-endian
    uint64_t a = ACCESS_ADDEND;
    memcpy(addend, &a, 8);
    for (int i = 7; i >= 0; i--)
    {
        unsigned sum = temp[i] + addend[i] + carry;
        result[i] = (uint8_t)sum;
        carry = sum >> 8;
    }
    // 17 decimal digits of the big-endian result
    char reversed[32];
    int count = 0;
    for (;;)
    {
        unsigned remainder = 0, nonzero = 0;
        for (int i = 0; i < 8; i++)
        {
            unsigned dividend = (remainder << 8) + result[i];
            result[i] = (uint8_t)(dividend / 10);
            remainder = dividend % 10;
            nonzero |= result[i];
        }
        if ((!nonzero && !remainder) || count == 31)
            break;
        reversed[count++] = (char)('0' + remainder);
    }
    while (count < 17)
        reversed[count++] = '0';
    memcpy(code, "307", 3);
    for (int i = 0; i < 17; i++)
        code[3 + i] = reversed[16 - i];
    code[20] = 0;
}

// ---------------------------------------------------------------------------
// The card's identity.

static int hexDigit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void loadCard(void)
{
    char chip[64] = "", line[128];
    unsigned serial = 0;
    FILE *f = fopen(CARD_FILE, "r");

    while (f && fgets(line, sizeof(line), f))
    {
        sscanf(line, "ChipId = %63s", chip);
        sscanf(line, "ChipId=%63s", chip);
        if (sscanf(line, "Serial = %x", &serial) != 1)
            sscanf(line, "Serial=%x", &serial);
    }
    if (f)
        fclose(f);
    if (strlen(chip) != 32 || !serial)
    {
        // A new card: a random IDm on the default PMm, a random serial.
        srand((unsigned)time(NULL) ^ (unsigned)getpid() << 8);
        snprintf(chip, sizeof(chip), "%08X%08X43262C3300040610", (unsigned)rand() ^ (unsigned)rand() << 16,
                 (unsigned)rand() ^ (unsigned)rand() << 16);
        serial = ((unsigned)rand() ^ (unsigned)rand() << 16) & 0x00ffffff;
        if (!serial)
            serial = 0x0069e9fa;
        mkdir("save", 0755);
        mkdir("save/server", 0755);
        if ((f = fopen(CARD_FILE, "w")))
        {
            fprintf(f, "; The Banapassport card the loader's reader holds (WMMT4): its FeliCa chip id\n"
                       "; (IDm then PMm) and its NBGIC serial (hex), which its access code is made from.\n"
                       "; The game server knows its user by them: keep this file with save/server/.\n"
                       "ChipId = %s\nSerial = %06X\n",
                    chip, serial);
            fclose(f);
        }
    }
    for (int i = 0; i < 8; i++)
    {
        record[IDM + i] = (uint8_t)(hexDigit(chip[2 * i]) << 4 | hexDigit(chip[2 * i + 1]));
        record[PMM + i] = (uint8_t)(hexDigit(chip[16 + 2 * i]) << 4 | hexDigit(chip[16 + 2 * i + 1]));
    }
    char code[21];
    encodeAccessCode(serial, code);
    memset(record + CHIP_ID, 0, 32);
    memcpy(record + CHIP_ID, chip, 32);
    memset(record + ACCESS_CODE, 0, 32);
    memcpy(record + ACCESS_CODE, code, 20);
    if (!encodeHeader(serial, 0x03f6, 0))
        log_error("WMMT4: the card's NBGIC header could not be made");
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 card: chip %s, access code %s\n", chip, code);
}

// ---------------------------------------------------------------------------
// The BngRw library's calls (two readers: 0 and 1).

typedef void (*Done)(int device, int status, void *user);
typedef void (*Touched)(int device, int status, void *record, void *user);

static int badDevice(int device)
{
    return device < 0 || device > 1;
}

static int complete(int device, Done done, void *user)
{
    if (badDevice(device))
        return BNG_BAD_DEVICE;
    if (done)
        done(device, 0, user);
    return BNG_OK;
}

__attribute__((force_align_arg_pointer)) static int readerReady(int device)
{
    return badDevice(device) ? BNG_BAD_DEVICE : BNG_OK;
}

__attribute__((force_align_arg_pointer)) static int reset(int device, Done done, void *user)
{
    return complete(device, done, user);
}

__attribute__((force_align_arg_pointer)) static int attach(int device, int a, int b, int c, Done done, void *user)
{
    (void)a;
    (void)b;
    (void)c;
    return complete(device, done, user);
}

__attribute__((force_align_arg_pointer)) static int request(int device, int value, Done done, void *user)
{
    (void)value;
    return complete(device, done, user); // LED, beep
}

// The card's authentication: its NBGIC header into the request's buffer.
__attribute__((force_align_arg_pointer)) static int auth(int device, int a, int b, const void *version,
                                                         const char *requestText, const char *extra, Done done,
                                                         void *user)
{
    (void)a;
    (void)b;
    (void)version;
    (void)requestText;
    (void)extra;
    if (badDevice(device))
        return BNG_BAD_DEVICE;
    if (!cardPresent || !user)
        return BNG_NO_CARD;
    memcpy((uint8_t *)user + NBGIC_HEADER, header, sizeof(header));
    return complete(device, done, user);
}

// Waiting for a card: there is one (it is put on the reader then).
__attribute__((force_align_arg_pointer)) static int waitTouch(int device, int a, int b, Touched touched, void *user)
{
    (void)a;
    (void)b;
    if (badDevice(device))
        return BNG_BAD_DEVICE;
    cardPresent = 1;
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT4 card: on reader %d\n", device);
    if (touched)
        touched(device, 0, record, user);
    return BNG_OK;
}

static void detour(uint32_t address, void *replacement)
{
    static const uint8_t entry[] = {0x55, 0x89, 0xe5};
    void *original;
    if (memcmp((const void *)(uintptr_t)address, entry, sizeof(entry)) != 0 ||
        MH_CreateHook((void *)(uintptr_t)address, replacement, &original) != MH_OK ||
        MH_EnableHook((void *)(uintptr_t)address) != MH_OK)
        log_error("WMMT4: card reader: cannot hook %#x", address);
}

void namcoEs1Wmmt4CardInstall(void)
{
    loadCard();
    MH_Initialize(); // already done is fine
    detour(0x08ac3e40, (void *)readerReady); // BngRw reader ready
    detour(0x08ac3182, (void *)readerReady); // reader start
    detour(0x08ac3ecc, (void *)attach);      // BngRwAttach
    detour(0x08ac39b0, (void *)reset);       // BngRwDevReset
    detour(0x08ac3774, (void *)request);     // BngRwReqLed
    detour(0x08ac3654, (void *)request);     // BngRwReqBeep
    detour(0x08ac3ad6, (void *)auth);        // BngRwReqAiccAuth
    detour(0x08ac387e, (void *)waitTouch);   // BngRwReqWaitTouch
}
