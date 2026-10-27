// The N2 cabinet's two HASP dongles (clHasp, clHasp2), as the Pacloader
// fork answers them (n2Hasp.cpp): the HASP HL API linked in, and the classes
// that find the dongles in /proc/bus/usb/devices before using it.

#include <stdint.h>
#include <string.h>

#include "namcoN2.h"
#include "../log/log.h"

#define HASP_DATA_SIZE 0xd40

// A cabinet never has two dongles with one serial, and the attract screen
// prints both.
static const char dongleId[] = "000001000001", dongle2Id[] = "000001000002";
static uint8_t haspData[HASP_DATA_SIZE];
static uint32_t nextHandle = 1;

static uint32_t serialPart(const char *digits)
{
    uint32_t value = 0;
    for (int i = 0; i < 6; i++)
        value = value * 10 + (uint32_t)(digits[i] - '0');
    return value;
}

// A 16-byte card-state record at 0 (none stored), its checksum and the
// checksum's complement at 14 and 15; the 64-byte identity block at 0xd00
// (the serial), its checksum and complement at 0xd3e and 0xd3f.
static void initData(void)
{
    static int done;
    uint8_t sum = 0;

    if (done)
        return;
    done = 1;
    for (int i = 0; i < 14; i++)
        sum += haspData[i];
    haspData[14] = sum;
    haspData[15] = sum ^ 0xff;
    memcpy(haspData + 0xd00, dongleId, 12);
    sum = 0;
    for (int i = 0; i < 0x3e; i++)
        sum += haspData[0xd00 + i];
    haspData[0xd3e] = sum;
    haspData[0xd3f] = sum ^ 0xff;
    log_info("Namco N2: virtual dongles %.6s-%.6s and %.6s-%.6s", dongleId, dongleId + 6, dongle2Id, dongle2Id + 6);
}

static int one(void)
{
    return 1;
}

static int zero(void)
{
    return 0;
}

static int count(void)
{
    return 2;
}

static uint32_t serialHi(void)
{
    return serialPart(dongleId);
}

static uint32_t serialLo(void)
{
    return serialPart(dongleId + 6);
}

static uint32_t serial2Hi(void)
{
    return serialPart(dongle2Id);
}

static uint32_t serial2Lo(void)
{
    return serialPart(dongle2Id + 6);
}

// clHasp::open(): its state (the object's first member) as an opened
// dongle's: no error, a handle, no low battery, the serial, the record.
static void openHasp(void *object)
{
    uint8_t *state = object ? *(uint8_t **)object : NULL;
    if (!state)
        return;
    initData();
    *(int32_t *)(state + 0x00) = 0;
    *(uint32_t *)(state + 0x04) = nextHandle++;
    state[0x08] = 0;
    *(uint32_t *)(state + 0x0c) = serialHi();
    *(uint32_t *)(state + 0x10) = serialLo();
    memcpy(state + 0x14, haspData, 16);
}

// clHasp2::open(): the same, laid out without the battery flag.
static void openHasp2(void *object)
{
    uint8_t *state = object ? *(uint8_t **)object : NULL;
    if (!state)
        return;
    initData();
    *(int32_t *)(state + 0x00) = 0;
    *(uint32_t *)(state + 0x04) = nextHandle++;
    *(uint32_t *)(state + 0x08) = serial2Hi();
    *(uint32_t *)(state + 0x0c) = serial2Lo();
    memcpy(state + 0x10, haspData, 16);
}

static int haspLogin(int feature, int vendorCode, uint32_t *handle)
{
    (void)feature;
    (void)vendorCode;
    initData();
    if (handle)
        *handle = nextHandle++;
    return 0;
}

static int haspGetSize(int handle, int fileId, int *size)
{
    (void)handle;
    (void)fileId;
    if (size)
        *size = HASP_DATA_SIZE;
    return 0;
}

static int haspRead(int handle, int fileId, int offset, int length, uint8_t *buffer)
{
    (void)handle;
    (void)fileId;
    if (!buffer || offset < 0 || length < 0 || offset > HASP_DATA_SIZE || length > HASP_DATA_SIZE - offset)
        return 1;
    initData();
    memcpy(buffer, haspData + offset, length);
    return 0;
}

static int haspWrite(int handle, int fileId, int offset, int length, const uint8_t *buffer)
{
    (void)handle;
    (void)fileId;
    if (!buffer || offset < 0 || length < 0 || offset > HASP_DATA_SIZE || length > HASP_DATA_SIZE - offset)
        return 1;
    initData();
    memcpy(haspData + offset, buffer, length);
    return 0;
}

void namcoN2HaspInit(void)
{
    static const struct
    {
        const char *name;
        void *replacement;
    } hooks[] = {
        {"hasp_cleanup", zero},
        {"hasp_decrypt", zero},
        {"hasp_encrypt", zero},
        {"hasp_free", zero},
        {"hasp_get_rtc", zero},
        {"hasp_get_sessioninfo", zero},
        {"hasp_get_size", haspGetSize},
        {"hasp_login", haspLogin},
        {"hasp_logout", zero},
        {"hasp_read", haspRead},
        {"hasp_write", haspWrite},
        {"_ZNK6clHasp7isAvailEv", one},
        {"_ZNK7clHasp27isAvailEv", one},
        {"_ZNK6clHasp8getCountEv", count},
        {"_ZNK7clHasp28getCountEv", count},
        {"_ZN6clHasp4openEv", openHasp},
        {"_ZN7clHasp24openEv", openHasp2},
        {"_ZN6clHasp4testEv", openHasp},
        {"_ZN7clHasp24testEv", openHasp2},
        {"_ZNK6clHaspcvbEv", one},
        {"_ZNK7clHasp2cvbEv", one},
        {"_ZNK6clHasp8getErrorEv", zero},
        {"_ZNK7clHasp28getErrorEv", zero},
        {"_ZNK6clHasp12isLowBatteryEv", zero},
        {"_ZNK6clHasp11getSerialHiEv", serialHi},
        {"_ZNK6clHasp11getSerialLoEv", serialLo},
        {"_ZNK7clHasp211getSerialHiEv", serial2Hi},
        {"_ZNK7clHasp211getSerialLoEv", serial2Lo},
    };
    int n = 0;

    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
        n += namcoN2Hook(hooks[i].name, hooks[i].replacement);
    log_info("Namco N2: %d dongle hooks", n);
}
