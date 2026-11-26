#include <math.h>
// The Speed Rider cabinets' STM32 I/O controller, on one of the IGS036's
// Atmel USARTs (see igsStm32.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL_gamepad.h>

#include "igsBinder.h"
#include "igsStm32.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

void igsStm32Init(IgsStm32 *s, const char *name, int trace)
{
    memset(s, 0, sizeof(*s));
    s->name = name;
    s->trace = trace;
    s->lastCoins = -1;
    for (int i = 0; i < 4; i++)
        s->fishCoins[i] = -1;
    // VR4 (Speed Rider 2's right brake) unused: 0
    for (int i = 0; i < 3; i++)
    {
        s->pots[i][0] = 0;
        s->pots[i][1] = 512;
        s->pots[i][2] = 1023;
    }
    s->throttle = 1;
    s->vr[0] = 512;
}

// A pot's raw reading at value (lean: -1..1, a pedal: 0..1).
static unsigned pot(const uint16_t *range, double value, int centred)
{
    if (!centred)
        return (unsigned)(range[0] + (range[2] - range[0]) * value + 0.5);
    if (value < 0)
        return (unsigned)(range[1] + (range[1] - range[0]) * value + 0.5);
    return (unsigned)(range[1] + (range[2] - range[1]) * value + 0.5);
}

// The firmware sends ten-byte commands, the controller answers each with
// eleven bytes. Speed Rider first shakes hands in two steps (55 aa 11 /
// aa 55 22, 66 99 aa / 99 66 bb); Speed Rider 2 does not. Commands (0x18)
// carry the lamps.
static int stm32Byte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *tx = s->tx, *rx = s->rx, sum = 0;

    tx[s->txCount++] = byte;
    if (s->txCount != 10)
        return 0;
    s->txCount = 0;
    memset(rx, 0, sizeof(s->rx));
    if (tx[0] == 0x55 && tx[1] == 0xaa && tx[2] == 0x11)
    {
        rx[0] = 0xaa; rx[1] = 0x55; rx[2] = 0x22;
    }
    else if (tx[0] == 0x66 && tx[1] == 0x99 && tx[2] == 0xaa)
    {
        rx[0] = 0x99; rx[1] = 0x66; rx[2] = 0xbb;
    }
    else
    {
        for (int i = 0; i < 9; i++)
            sum += tx[i];
        if (tx[0] != 0x18 || sum != tx[9])
        {
            log_error("%s: unsupported STM32 packet %02x %02x %02x", s->name, tx[0], tx[1], tx[2]);
            return -1;
        }
        if (s->trace >= 2 && memcmp(s->lamps, tx, 10))
            printf("%s: STM32 lamps %02x %02x %02x %02x %02x %02x %02x %02x\n", s->name,
                   tx[1], tx[2], tx[3], tx[4], tx[5], tx[6], tx[7], tx[8]);
        memcpy(s->lamps, tx, 10);
        // Four 10-bit pots (VR1 lean, VR2 throttle, VR3 brake), coins since
        // the last reply (five bits for coin 1, six for coin 2, two for
        // coin 3), the ten-bit switch word.
        unsigned c1 = s->coins[0] > 0x1f ? 0x1f : s->coins[0];
        unsigned c2 = s->coins[1] > 0x3f ? 0x3f : s->coins[1];
        unsigned c3 = s->coins[2] > 3 ? 3 : s->coins[2];
        s->coins[0] -= c1;
        s->coins[1] -= c2;
        s->coins[2] -= c3;
        rx[0] = 0x44;
        rx[1] = s->vr[0] >> 2;
        rx[2] = (s->vr[0] << 6) | ((s->vr[1] >> 4) & 0x3f);
        rx[3] = (s->vr[1] << 4) | ((s->vr[2] >> 6) & 0xf);
        rx[4] = (s->vr[2] << 2) | ((s->vr[3] >> 8) & 3);
        rx[5] = s->vr[3];
        rx[6] = (c1 << 2) | (c2 >> 4);
        rx[7] = (c2 << 4) | (c3 << 2);
        rx[8] = s->switches >> 2;
        rx[9] = s->switches << 6;
        for (int i = 0; i < 10; i++)
            rx[10] += rx[i];
    }
    s->rxCount = 20; // (the reply's 11 bytes, zeros after)
    s->rxPos = 0;
    return 0;
}

// IGS_FISH_TAP=bit,... (debugging): those input bits set 0.3 s every 3 s;
// IGS_FISH_HOLD: held from 10 s on.
void igsDebugBits(uint8_t *inputs, int bits)
{
    struct timespec ts;
    static time_t start;
    const char *tap = getenv("IGS_FISH_TAP"), *hold = getenv("IGS_FISH_HOLD");

    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!start)
    {
        struct timespec wall;
        start = ts.tv_sec;
        clock_gettime(CLOCK_REALTIME, &wall);
        if (getenv("IGS_FISH_SEQ") || getenv("IGS_FISH_SCAN"))
            printf("IGS: debug inputs' clock from %.3f\n", wall.tv_sec - ts.tv_nsec / 1e9 + wall.tv_nsec / 1e9);
    }
    // IGS_FISH_SCAN=seconds,first[,skip]: from then on, every 4 s, the next
    // bit (from first, but skip) held for 2 s
    const char *scan = getenv("IGS_FISH_SCAN");
    if (scan && ts.tv_sec - start >= atoi(scan))
    {
        const char *comma = strchr(scan, ',');
        const char *skipAt = comma ? strchr(comma + 1, ',') : NULL;
        long t = ts.tv_sec - start - atoi(scan);
        int bit = (int)(t / 4) + (comma ? atoi(comma + 1) : 0);
        if (skipAt && bit >= atoi(skipAt + 1))
            bit++;
        static int shown = -1;
        if (bit < bits && t % 4 < 2)
        {
            inputs[bit >> 3] |= (uint8_t)(1 << (bit & 7));
            if (shown != bit)
            {
                shown = bit;
                struct timespec wall;
                clock_gettime(CLOCK_REALTIME, &wall);
                printf("IGS: scan bit %d at %ld s (%ld)\n", bit, (long)(ts.tv_sec - start), (long)wall.tv_sec);
                fflush(stdout);
            }
        }
    }
    // IGS_FISH_SEQ=seconds:bit[/length],... : each bit tapped 0.3 s (IGS_FISH_SEQ_LENGTH) at its time
    const char *seq = getenv("IGS_FISH_SEQ");
    double length = getenv("IGS_FISH_SEQ_LENGTH") ? atof(getenv("IGS_FISH_SEQ_LENGTH")) : 0.3;
    double t = (double)(ts.tv_sec - start) + ts.tv_nsec / 1e9;
    for (const char *p = seq; p && *p;)
    {
        char *end;
        double at = strtod(p, &end);
        if (end == p || *end != ':')
            break;
        long bit = strtol(end + 1, &end, 0);
        double held = *end == '/' ? strtod(end + 1, &end) : length;
        if (bit >= 0 && bit < bits && t >= at && t < at + held)
            inputs[bit >> 3] |= (uint8_t)(1 << (bit & 7));
        p = *end == ',' ? end + 1 : end;
    }
    for (int pass = 0; pass < 2; pass++)
    {
        const char *p = pass ? tap : hold;
        if (!p || (pass ? !(ts.tv_sec % 3 == 0 && ts.tv_nsec < 300000000) : ts.tv_sec - start < 10))
            continue;
        while (*p)
        {
            char *end;
            long bit = strtol(p, &end, 0);
            if (end == p)
                break;
            if (bit >= 0 && bit < bits)
                inputs[bit >> 3] |= (uint8_t)(1 << (bit & 7));
            p = *end == ',' ? end + 1 : end;
        }
    }
}

// The E2200 fish tables' control board: the firmware sends a command byte,
// its data (0xc2: 7 bytes, 0xc4 and 0xc6: none, the others: 8) and their
// sum; the board answers 0xc1 and 14 input bytes (active low) to the polls,
// 0xc5 and 9 bytes ("Fish", two 16-bit numbers, flags) to 0xc4 and
// 0xc6, a byte to 0xc2, each with its sum (firmware 0x1000e0f4, 0x1000de58).
// The fish table's seats of the loader's players 1 to 4 (fishSample).
// Demon Hunter's (E2100, fish 5): the same board, 10 input bytes (firmware
// 0x1000d6e4), its four seats bytes 0 to 3 left to right.
static const int fishSeats[4] = {7, 2, 3, 0};
static const int demonHunterSeats[4] = {0, 1, 2, 3};
// Scratch's (fish 6): Demon Hunter's board; bit 5 takes a ticket, no free
// play then.
#define SEATS(s) ((s)->fish >= 5 ? demonHunterSeats : fishSeats)

static int fishByte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *tx = s->tx, *rx = s->rx;
    unsigned command = tx[0], data, length;

    if (s->txCount < sizeof(s->tx))
        tx[s->txCount] = byte;
    s->txCount++;
    command = tx[0];
    data = command == 0xc2 ? 7 : command == 0xc4 || command == 0xc6 ? 0 : 8;
    if (s->txCount < data + 2)
        return 0;
    s->txCount = 0;
    if (s->trace >= 3)
    {
        printf("%s: control board command", s->name);
        for (unsigned i = 0; i < data + 2 && i < sizeof(s->tx); i++)
            printf(" %02x", tx[i]);
        printf("\n");
    }
    memset(rx, 0, sizeof(s->rx));
    unsigned polled = s->fish >= 5 ? 10 : 14;
    length = command == 0xc2 ? 1 : command == 0xc4 || command == 0xc6 ? 9 : polled;
    rx[0] = length == polled ? 0xc1 : length == 9 ? 0xc5 : 0xc3;
    if (length == polled)
    {
        uint8_t inputs[14];
        memcpy(inputs, s->inputs, sizeof(inputs));
        // the coins: each closed for 4 of these packets, open for 4 (the
        // firmware scans the switches as it gets them: a coin is 2 to 20
        // scans closed, longer a jammed one, E-01)
        for (int p = 0; p < 4; p++)
        {
            if (s->fishCoinPhase[p])
                s->fishCoinPhase[p]--;
            else if (s->fishCoinPending[p])
            {
                s->fishCoinPending[p]--;
                s->fishCoinPhase[p] = 8;
            }
            if (s->fishCoinPhase[p] > 4)
                inputs[SEATS(s)[p]] |= 1u << 6;
        }
        igsDebugBits(inputs, (int)polled * 8);
        for (unsigned i = 0; i < polled; i++)
            rx[1 + i] = (uint8_t)~inputs[i];
    }
    else if (length == 9)
        memcpy(rx + 1, "Fish", 4); // which the firmware checks (0x1000c888)
    for (unsigned i = 0; i <= length; i++)
        rx[length + 1] += rx[i];
    s->rxCount = length + 2;
    s->rxPos = 0;
    return 0;
}

// The E2100 fish tables' control board (Ocean Party, fish 4): as the
// E2200's, bigger: the firmware sends a command byte, its data (0xc2, the
// lamps: 7 bytes; the others, the polls: 50), their sum and a 0; the board
// answers the polls with 0xc1, 25 input bytes (200 switches, active high,
// the firmware's bit n byte n / 8) and their sum, 0xc2 with 0xc3, a byte
// and their sum (firmware 0x100176b0, 0x1001738c). (A 0 where a command
// starts is passed over: each command ends with one.)
static int opByte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *rx = s->rx;
    unsigned data, length, polled = 25;

    if (s->txCount == 0 && byte == 0)
        return 0;
    if (s->txCount == 0)
        s->tx[0] = byte;
    if (s->txCount < sizeof(s->opTx))
        s->opTx[s->txCount] = byte;
    s->txCount++;
    data = s->tx[0] == 0xc2 ? 7 : 50;
    if (s->txCount < data + 2)
        return 0;
    s->txCount = 0;
    if (s->trace >= 3)
        printf("%s: control board command %02x\n", s->name, s->tx[0]);
    // IGS_OP_OUT (debugging): the polls' data shown as it changes
    if (getenv("IGS_OP_OUT") && memcmp(s->opLast, s->opTx, data + 1))
    {
        printf("%s: %02x", s->name, s->opTx[0]);
        for (unsigned i = 1; i <= data; i++)
            printf(i % 8 == 1 ? " | %02x" : " %02x", s->opTx[i]);
        printf("\n");
        memcpy(s->opLast, s->opTx, data + 1);
    }
    memset(rx, 0, sizeof(s->rx));
    length = s->tx[0] == 0xc2 ? 1 : polled;
    rx[0] = length == polled ? 0xc1 : 0xc3;
    if (length == polled)
    {
        uint8_t inputs[25];
        memcpy(inputs, s->inputs, sizeof(inputs));
        igsDebugBits(inputs, (int)polled * 8);
        memcpy(rx + 1, inputs, polled);
    }
    for (unsigned i = 0; i <= length; i++)
        rx[length + 1] += rx[i];
    s->rxCount = length + 2;
    s->rxPos = 0;
    return 0;
}

// Hero of Robots' I/O board (fish 7): the firmware relays the game's 12-byte
// packets to it, after a 0x29 of its own (0x29, the lamps, their sum:
// IOBoard::MakeSendDataChecksum)
// and its answers back (12 bytes: a header 0x2a, the keys, the others 0,
// their sum: IOBoard::CheckRecvDataChecksum, GetKeyInData). The firmware
// packs them for the game, which reads its keys as byte 1 | byte 2 << 4 |
// Crazy Shoot's I/O board (fish 8): the firmware sends 10-byte frames (0xc0,
// 8 bytes, their sum), the board answers 12 (0xc1, 10 input bytes, the sum
// of the 11 before: firmware 0x10008d40).
static int crazyShootByte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *rx = s->rx;

    if (s->txCount == 0 && byte != 0xc0)
        return 0;
    if (s->txCount < sizeof(s->opTx))
        s->opTx[s->txCount] = byte;
    if (++s->txCount < 10)
        return 0;
    s->txCount = 0;
    static uint8_t last[8];
    if (s->trace >= 2 && memcmp(last, s->opTx + 1, 8))
    {
        memcpy(last, s->opTx + 1, 8);
        printf("%s: I/O board outputs %02x %02x %02x %02x %02x %02x %02x %02x\n", s->name, s->opTx[1], s->opTx[2],
               s->opTx[3], s->opTx[4], s->opTx[5], s->opTx[6], s->opTx[7], s->opTx[8]);
    }
    uint8_t inputs[25];
    memcpy(inputs, s->inputs, sizeof(inputs));
    igsDebugBits(inputs, 80);
    memset(rx, 0, sizeof(s->rx));
    rx[0] = 0xc1;
    for (int i = 0; i < 10; i++)
        rx[1 + i] = (uint8_t)~inputs[i]; // (active low)
    for (int i = 0; i < 11; i++)
        rx[11] += rx[i];
    s->rxCount = 12;
    s->rxPos = 0;
    return 0;
}

// byte 3 << 8 (robotsSample's bits).
static int robotsByte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *rx = s->rx;

    if (s->txCount < 2 && byte != 0x29)
    {
        s->txCount = 0;
        return 0;
    }
    if (s->txCount < sizeof(s->opTx))
        s->opTx[s->txCount] = byte;
    if (++s->txCount < 13)
        return 0;
    s->txCount = 0;
    if (s->trace >= 3)
        printf("%s: I/O board packet %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", s->name,
               s->opTx[1], s->opTx[2], s->opTx[3], s->opTx[4], s->opTx[5], s->opTx[6], s->opTx[7], s->opTx[8],
               s->opTx[9], s->opTx[10], s->opTx[11], s->opTx[12]);
    uint8_t inputs[25];
    memcpy(inputs, s->inputs, sizeof(inputs));
    igsDebugBits(inputs, 16);
    memset(rx, 0, sizeof(s->rx));
    rx[0] = 0x2a;
    // (active high; the firmware also counts pulses of its own on byte 1
    // bits 0 to 2 and byte 2 bit 1, 0x100026f8, the game none of them)
    rx[1] = inputs[0] & 15;
    rx[2] = inputs[0] >> 4;
    rx[3] = inputs[1];
    for (int i = 0; i < 11; i++)
        rx[11] += rx[i];
    s->rxCount = 12;
    s->rxPos = 0;
    return 0;
}

// MuziBox's I/O board (fish 2): the firmware sends a type, a count, that
// many bytes and their sum (07 04 and the lamps, every other tick); the
// board answers likewise, its first four bytes the switches (active high,
// the firmware's key word: igsStm32.c's muziboxSample), the fifth kept by
// the firmware (firmware 0x100014fc, 0x100029e8).
static int muziboxByte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *tx = s->tx, *rx = s->rx;

    if (s->txCount < sizeof(s->tx))
        tx[s->txCount] = byte;
    s->txCount++;
    if (s->txCount < 2 || s->txCount < (unsigned)tx[1] + 3)
        return 0;
    s->txCount = 0;
    static int shown;
    if (s->trace >= 2 && shown++ < 30)
    {
        printf("%s: I/O board packet", s->name);
        for (unsigned i = 0; i < (unsigned)tx[1] + 3 && i < sizeof(s->tx); i++)
            printf(" %02x", tx[i]);
        printf("\n");
        fflush(stdout);
    }
    if (tx[1] + 3 <= (int)sizeof(s->tx))
        memcpy(s->lamps, tx + 2, tx[1] < sizeof(s->lamps) ? tx[1] : sizeof(s->lamps));
    uint8_t inputs[4];
    memcpy(inputs, s->inputs, sizeof(inputs));
    igsDebugBits(inputs, 32);
    memset(rx, 0, sizeof(s->rx));
    rx[0] = tx[0];
    rx[1] = 5;
    memcpy(rx + 2, inputs, 4);
    rx[6] = s->lamps[3];
    for (int i = 0; i < 7; i++)
        rx[7] += rx[i];
    s->rxCount = 8;
    s->rxPos = 0;
    return 0;
}

// Percussion Master 3's I/O board (a36FishBoard 3): the board sends
// a5, five lamp bytes and their sum (8 bits, a5 included); the reply is
// 19 bytes, the last the sum of the others: its switches active low in
// bytes 1..4 and 5 (inputs[0..4] here, active high), bytes 6..17 the
// twelve drums' hit levels (inputs[5..16]).
static int pm3Byte(IgsStm32 *s, uint8_t byte)
{
    uint8_t *tx = s->tx, *rx = s->rx;

    if (s->txCount == 0 && byte != 0xa5)
        return 0;
    tx[s->txCount++] = byte;
    if (s->txCount < 7)
        return 0;
    s->txCount = 0;
    static int shown;
    if (s->trace >= 2 && shown++ < 30)
    {
        printf("%s: I/O board packet %02x %02x %02x %02x %02x %02x %02x\n", s->name, tx[0], tx[1], tx[2], tx[3],
               tx[4], tx[5], tx[6]);
        fflush(stdout);
    }
    memcpy(s->lamps, tx + 1, 5);
    uint8_t inputs[17];
    memcpy(inputs, s->inputs, sizeof(inputs));
    igsDebugBits(inputs, 136);
    memset(rx, 0, sizeof(s->rx));
    rx[0] = 0xa5;
    for (int i = 0; i < 5; i++)
        rx[1 + i] = (uint8_t)~inputs[i];
    for (int i = 5; i < 17; i++)
        rx[1 + i] = inputs[i];
    for (int i = 0; i < 18; i++)
        rx[18] += rx[i];
    s->rxCount = 19;
    s->rxPos = 0;
    return 0;
}

// TXEMPTY, TXRDY, RXRDY.
static uint32_t usartStatus(const IgsStm32 *s)
{
    return (s->txOn && !s->txBusy ? 0x200 : 0) | (s->txOn && s->txBusy < 2 ? 2 : 0) |
           (s->rxOn && !s->rxBusy && s->rxPos < s->rxCount);
}

// The USART's registers (offsets from its base). TXRDY/TXEMPTY read 0
// while the transmitter is disabled, the reply reaches RHR only while the
// receiver is enabled: the firmware's handler tests CSR without IMR, for
// both directions.
uint32_t igsStm32Read(IgsStm32 *s, uint32_t offset)
{
    switch (offset)
    {
    case 0x10: // IMR
        return s->mask;
    case 0x14: // CSR
        return usartStatus(s);
    case 0x18: // RHR
        if (!s->rxOn || s->rxPos >= s->rxCount)
            return 0;
        s->rxBusy = s->paced;
        return s->rx[s->rxPos++];
    }
    return 0;
}

int igsStm32Write(IgsStm32 *s, uint32_t offset, uint32_t value)
{
    switch (offset)
    {
    case 0x00: // CR
        if (value & 0x08) s->txCount = 0;
        if (value & 0x10)
        {
            s->rxOn = 1;
            s->rxBusy = s->paced; // the next byte still on its way
        }
        if (value & 0x20) s->rxOn = 0;
        if (value & 0x40) s->txOn = 1;
        if (value & 0x80) s->txOn = 0;
        break;
    case 0x08: // IER
        s->mask |= value;
        break;
    case 0x0c: // IDR
        s->mask &= ~value;
        break;
    case 0x1c: // THR
        if (s->txOn)
        {
            s->txBusy = s->paced ? 2 : 0;
            return s->fish == 8   ? crazyShootByte(s, (uint8_t)value)
                   : s->fish == 7 ? robotsByte(s, (uint8_t)value)
                   : s->fish == 4 ? opByte(s, (uint8_t)value)
                   : s->fish == 3 ? pm3Byte(s, (uint8_t)value)
                   : s->fish == 2 ? muziboxByte(s, (uint8_t)value)
                   : s->fish ? fishByte(s, (uint8_t)value) : stm32Byte(s, (uint8_t)value);
        }
        break;
    }
    return 0;
}

int igsStm32Interrupt(const IgsStm32 *s)
{
    return (usartStatus(s) & s->mask & 0x203) != 0;
}

int igsStm32Tick(IgsStm32 *s)
{
    int busy = s->txBusy || s->rxBusy;

    if (s->txBusy)
        s->txBusy--;
    s->rxBusy = 0;
    return busy;
}

int igsStm32Busy(const IgsStm32 *s)
{
    return s->txBusy || s->rxBusy || s->txCount || s->rxPos < s->rxCount;
}

static double unit(double value)
{
    return value < 0 ? 0 : value > 1 ? 1 : value;
}

// The fish table's seats: each its byte of the control board's inputs (bit
// 0, 1: up, down; 2, 3: the cannon turned; 4: a button; 5: fire; 6: coin),
// seat 0 top left, 2, 7, 3 the bottom row left to right. The loader's
// players: 1 the bottom middle, 2 the bottom left, 3 the bottom right, 4 the
// top left. Players 3 and 4 on the keyboard: J L turn, K fire, H the button,
// 7 coin; the keypad's 4 6 turn, 5 fire, 0 the button, 8 coin.

static void fishSample(IgsStm32 *s)
{
    static const unsigned long keys[] = {0x6a, 0x6c, 0x6b, 0x68, 0x37, 0xffb4, 0xffb6, 0xffb5, 0xffb0, 0x38};
    static int keysSet;
    int jvs = getConfig()->inputMode == 2;
    JVSIO *io = jvs ? getJVSIO() : desktopKeyboardState();
    struct timespec ts;
    double now;

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }
    unsigned extra = jvs ? 0 : desktopKeyboardExtra();
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    memset(s->inputs, 0, sizeof(s->inputs));
    for (int p = 0; p < 4; p++)
    {
        unsigned buttons = io->state.inputSwitch[PLAYER_1 + p], bits = 0;
        int coins = io->state.coinCount[p];
        if (!jvs && p >= 2)
        {
            // the keyboard's players 3 and 4 (their coin keys counted here)
            static int coinKey[2], keyCoins[2];
            unsigned k = extra >> (5 * (p - 2));
            buttons = (k & 1 ? BUTTON_LEFT : 0) | (k & 2 ? BUTTON_RIGHT : 0) | (k & 4 ? BUTTON_1 : 0) |
                      (k & 8 ? BUTTON_2 : 0);
            if ((k & 16) && !coinKey[p - 2])
                keyCoins[p - 2]++;
            coinKey[p - 2] = (k & 16) != 0;
            coins = keyCoins[p - 2];
        }
        if (!jvs)
        {
            // the pads' sticks and triggers too (pad n: player n)
            if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) < -16000) buttons |= BUTTON_LEFT;
            if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) > 16000) buttons |= BUTTON_RIGHT;
            if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) buttons |= BUTTON_1;
            if (p >= 2)
            {
                // (players 1 and 2's buttons come with the keyboard's state)
                if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) buttons |= BUTTON_LEFT;
                if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= BUTTON_RIGHT;
                if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= BUTTON_1;
                if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_START)) buttons |= BUTTON_START;
                if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST)) buttons |= BUTTON_2;
            }
        }
        if (buttons & BUTTON_UP) bits |= 1u << 0;
        if (buttons & BUTTON_DOWN) bits |= 1u << 1;
        // (bit 2 turns Fish Hunter's bottom seats' cannons left, Demon
        // Hunter's right, moves Scratch's ticket right)
        if (buttons & BUTTON_LEFT) bits |= 1u << (s->fish >= 5 ? 3 : 2);
        if (buttons & BUTTON_RIGHT) bits |= 1u << (s->fish >= 5 ? 2 : 3);
        if (buttons & BUTTON_2) bits |= 1u << 4;
        if (buttons & (BUTTON_1 | BUTTON_START)) bits |= 1u << 5;
        // A coin: given in the control board's input packets (fishByte).
        if (s->fishCoins[p] >= 0 && coins != s->fishCoins[p])
            s->fishCoinPending[p] += (coins - s->fishCoins[p]) & 0xff;
        s->fishCoins[p] = coins;
        // IGS_FISH_COIN=seconds:player,... (debugging): a coin for that
        // player (1 to 4) at that time
        static double debugStart;
        if (!debugStart)
            debugStart = now;
        for (const char *c = getenv("IGS_FISH_COIN"); c && *c;)
        {
            char *end;
            double at = strtod(c, &end);
            if (*end != ':')
                break;
            long who = strtol(end + 1, &end, 10);
            static double done[4];
            if (who == p + 1 && now - debugStart >= at && done[p] < at)
            {
                done[p] = at;
                s->fishCoinPending[p]++;
            }
            c = *end == ',' ? end + 1 : end;
        }
        // free play: firing puts a coin in (one each 0.3 s while held)
        if (s->fish != 6 && (bits & (1u << 5)) && !s->fishCoinPending[p] && !s->fishCoinPhase[p] &&
            now >= s->fishFreeNext[p])
        {
            s->fishCoinPending[p] = 1;
            s->fishFreeNext[p] = now + 0.3;
        }
        s->inputs[SEATS(s)[p]] = (uint8_t)bits;
    }
}

// The firmware keeps the pots' calibration, the switches' debounce and key
// edges, and the credits.
// MuziBox's switches (its key word, active high; from its I/O test): the
// ten piano keys bits 14, 15, 0..7 (left to right), the headphone jack 8,
// confirm 9, left 10, right 11, test 12, service 13, the coin 16 (held a
// tenth of a second: the firmware counts its edges). The desktop's: the
// piano A S D F G H J K L ; (the pad's L2 L1 X Y A B R1 R2, its d-pad down
// and up the outer two), confirm 1, Enter or Start, left and right the
// arrows or the d-pad, F2 test, F1 service, 5 coin.
static const int muziboxPiano[10] = {14, 15, 0, 1, 2, 3, 4, 5, 6, 7};

static void muziboxSample(IgsStm32 *s)
{
    static const unsigned long keys[] = {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 0xff0d /* Return */};
    static int keysSet;
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopKeyboardState();
    unsigned player = io->state.inputSwitch[PLAYER_1], system = io->state.inputSwitch[SYSTEM], piano = 0;
    struct timespec ts;
    double now;
    uint32_t bits = 0;

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }
    unsigned extra = getConfig()->inputMode == 2 ? 0 : desktopKeyboardExtra();
    piano = extra & 0x3ff;
    // the JVS buttons 1..10 (evdev's PLAYER_1_BUTTON_n) the keys too
    static const unsigned jvsPiano[10] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4, BUTTON_5,
                                          BUTTON_6, BUTTON_7, BUTTON_8, BUTTON_9, BUTTON_10};
    if (getConfig()->inputMode == 2)
        for (int i = 0; i < 10; i++)
            if (player & jvsPiano[i])
                piano |= 1u << i;
    static const int padPiano[10] = {SDL_GAMEPAD_BUTTON_DPAD_DOWN, -1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                                     SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_SOUTH,
                                     SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1,
                                     SDL_GAMEPAD_BUTTON_DPAD_UP};
    for (int i = 0; i < 10; i++)
        if (padPiano[i] >= 0 && desktopGamepadButton(padPiano[i]))
            piano |= 1u << i;
    if (desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
        piano |= 1u << 1;
    if (desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
        piano |= 1u << 8;
    for (int i = 0; i < 10; i++)
        if (piano & (1u << i))
            bits |= 1u << muziboxPiano[i];
    if ((player & BUTTON_START) || (extra & (1u << 10)))
        bits |= 1u << 9;
    if (player & BUTTON_LEFT)
        bits |= 1u << 10;
    if (player & BUTTON_RIGHT)
        bits |= 1u << 11;
    if (system & BUTTON_TEST)
        bits |= 1u << 12;
    if (player & BUTTON_SERVICE)
        bits |= 1u << 13;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (s->fishCoins[0] >= 0 && io->state.coinCount[0] != s->fishCoins[0])
        s->fishCoinUntil[0] = now + 0.1;
    s->fishCoins[0] = io->state.coinCount[0];
    if (now < s->fishCoinUntil[0])
        bits |= 1u << 16;
    for (int i = 0; i < 4; i++)
        s->inputs[i] = (uint8_t)(bits >> (8 * i));
}

// Percussion Master 3: each player's drums (the I/O board's level
// channels: small left and right, rim left, face left and right, rim
// right) and 12-key keypad (its switch bits: 1..9, *, 0, #; the menus move
// on 2, 4, 6, 8 and take # or a face hit).
static const int pm3Keypad[2][12] = {{12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
                                     {16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11}};

static void pm3Sample(IgsStm32 *s)
{
    // keys: the drums (w e a s d f, n m h j k l), the keypad (keypad 0..9,
    // *, Enter; Return #)
    static const unsigned long keys[] = {'w', 'e', 'a', 's', 'd', 'f', 'n', 'm', 'h', 'j', 'k', 'l',
                                         0xffb0, 0xffb1, 0xffb2, 0xffb3, 0xffb4, 0xffb5, 0xffb6, 0xffb7,
                                         0xffb8, 0xffb9, 0xffaa, 0xff8d, 0xff0d};
    static int keysSet;
    static double drumUntil[12];
    static int drumHeld[12];
    int jvs = getConfig()->inputMode == 2;
    JVSIO *io = jvs ? getJVSIO() : desktopKeyboardState();
    struct timespec ts;
    double now;
    uint32_t bits = 0;
    int drums = 0;

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }
    unsigned extra = jvs ? 0 : desktopKeyboardExtra();
    drums = extra & 0xfff;
    // the keypad's digits (keypad 0..9 at 12..21), * and #
    static const int keypadKey[12] = {13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 12, 23};
    for (int i = 0; i < 12; i++)
        if (extra & (1u << keypadKey[i]))
            bits |= 1u << pm3Keypad[0][i];
    if (extra & (1u << 24))
        bits |= 1u << pm3Keypad[0][11];

    static const unsigned jvsDrums[6] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4, BUTTON_5, BUTTON_6};
    for (int p = 0; p < 2; p++)
    {
        unsigned player = io->state.inputSwitch[p ? PLAYER_2 : PLAYER_1];
        if (jvs)
            for (int i = 0; i < 6; i++)
                if (player & jvsDrums[i])
                    drums |= 1 << (6 * p + i);
        // the pads: LB RB small drums, LT RT rims, X B the face
        static const int padDrums[6] = {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1,
                                        SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_EAST, -1};
        for (int i = 0; i < 6; i++)
            if (padDrums[i] >= 0 && desktopPlayerGamepadButton(p, padDrums[i]))
                drums |= 1 << (6 * p + i);
        if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
            drums |= 1 << (6 * p + 2);
        if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
            drums |= 1 << (6 * p + 5);
        // the joystick (arrows, d-pad) the keypad's 2 8 4 6, Start (and A) #
        if (player & BUTTON_UP)
            bits |= 1u << pm3Keypad[p][1];
        if (player & BUTTON_DOWN)
            bits |= 1u << pm3Keypad[p][7];
        if (player & BUTTON_LEFT)
            bits |= 1u << pm3Keypad[p][3];
        if (player & BUTTON_RIGHT)
            bits |= 1u << pm3Keypad[p][5];
        if ((player & BUTTON_START) || (!jvs && desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH)))
            bits |= 1u << pm3Keypad[p][11];
    }
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        bits |= 1u << 24;
    if (io->state.inputSwitch[PLAYER_1] & BUTTON_SERVICE)
        bits |= 1u << 25;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (s->fishCoins[0] >= 0 && io->state.coinCount[0] != s->fishCoins[0])
        s->fishCoinUntil[0] = now + 0.1;
    s->fishCoins[0] = io->state.coinCount[0];
    if (now < s->fishCoinUntil[0])
        bits |= 1u << 26;

    memset(s->inputs, 0, sizeof(s->inputs));
    for (int i = 0; i < 4; i++)
        s->inputs[i] = (uint8_t)(bits >> (8 * i));
    // a drum's hit: its piezo's level, a short pulse from the press
    for (int i = 0; i < 12; i++)
    {
        int held = (drums >> i) & 1;
        if (held && !drumHeld[i])
            drumUntil[i] = now + 0.06;
        drumHeld[i] = held;
        if (now < drumUntil[i])
            s->inputs[5 + i] = 0xff;
    }
}

// Percussion Master 2008 and Rock Fever 5 (igsA36.c's switch matrix): the
// same controls as Percussion Master 3's, its drums switches here (each
// player's small left and right, rim left, face left and right, rim right:
// its matrix bits), coin 29, service 30, test 31; the menus move on the 1P
// small drums and take its face.
void igsPm2008Sample(uint32_t switches[2])
{
    static const int drumBits[2][6] = {{5, 6, 7, 0, 1, 2}, {8, 9, 10, 11, 12, 13}};
    static const unsigned long keys[] = {'w', 'e', 'a', 's', 'd', 'f', 'n', 'm', 'h', 'j', 'k', 'l', 0xff0d};
    static int keysSet, lastCoins = -1;
    static double coinUntil;
    int jvs = getConfig()->inputMode == 2;
    JVSIO *io = jvs ? getJVSIO() : desktopKeyboardState();
    struct timespec ts;
    uint32_t bits = 0;
    unsigned drums = 0;

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }
    unsigned extra = jvs ? 0 : desktopKeyboardExtra();
    drums = extra & 0xfff;
    static const unsigned jvsDrums[6] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4, BUTTON_5, BUTTON_6};
    for (int p = 0; p < 2; p++)
    {
        unsigned player = io->state.inputSwitch[p ? PLAYER_2 : PLAYER_1];
        if (jvs)
            for (int i = 0; i < 6; i++)
                if (player & jvsDrums[i])
                    drums |= 1u << (6 * p + i);
        static const int padDrums[6] = {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1,
                                        SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_EAST, -1};
        for (int i = 0; i < 6; i++)
            if (padDrums[i] >= 0 && desktopPlayerGamepadButton(p, padDrums[i]))
                drums |= 1u << (6 * p + i);
        if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
            drums |= 1u << (6 * p + 2);
        if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
            drums |= 1u << (6 * p + 5);
        // the joystick's up and down the small drums, Start (and A) the face
        if (player & BUTTON_UP)
            drums |= 1u << (6 * p);
        if (player & BUTTON_DOWN)
            drums |= 1u << (6 * p + 1);
        if ((player & BUTTON_START) || (!jvs && desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH)) ||
            (!p && (extra & (1u << 12))))
            drums |= 1u << (6 * p + 3);
    }
    for (int i = 0; i < 12; i++)
        if (drums & (1u << i))
            bits |= 1u << drumBits[i / 6][i % 6];
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        bits |= 1u << 31;
    if (io->state.inputSwitch[PLAYER_1] & BUTTON_SERVICE)
        bits |= 1u << 30;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (lastCoins >= 0 && io->state.coinCount[0] != lastCoins)
        coinUntil = now + 0.05; // (longer: a jam, at the 2008 boards' timer)
    lastCoins = io->state.coinCount[0];
    if (now < coinUntil)
        bits |= 1u << 29;
    switches[0] = bits;
    switches[1] = 0;
}

// Rock Fever 5's drum kits: per player two red cymbals, four blue pads,
// the pedal, Start (the matrix's bits: 1P 5 6 / 7 0 1 2 / pedal 4 / Start
// 26; 2P 8 9 / 10..13 / 3 / 27; coin 29, service 30, test 31). Menus: the
// red ones up and down, Start to confirm. Keys: w e, a s d f, x (1P);
// n m, h j k l, b (2P); Return 1P Start. Pads: LB RB, X A B Y, LT, Start.
void igsRf5Sample(uint32_t switches[2])
{
    static const int kitBits[2][8] = {{5, 6, 7, 0, 1, 2, 4, 26}, {8, 9, 10, 11, 12, 13, 3, 27}};
    static const unsigned long keys[] = {'w', 'e', 'a', 's', 'd', 'f', 'x', 0xff0d,
                                         'n', 'm', 'h', 'j', 'k', 'l', 'b', 0};
    static const int padKit[7] = {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                                  SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
                                  SDL_GAMEPAD_BUTTON_NORTH, -1};
    static const unsigned jvsKit[7] = {BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4, BUTTON_5, BUTTON_6, BUTTON_7};
    static int keysSet, lastCoins = -1;
    static double coinUntil;
    int jvs = getConfig()->inputMode == 2;
    JVSIO *io = jvs ? getJVSIO() : desktopKeyboardState();
    struct timespec ts;
    uint32_t bits = 0;

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]) - 1);
        keysSet = 1;
    }
    unsigned kit = jvs ? 0 : desktopKeyboardExtra();
    for (int p = 0; p < 2; p++)
    {
        unsigned player = io->state.inputSwitch[p ? PLAYER_2 : PLAYER_1];
        for (int i = 0; i < 7; i++)
            if ((jvs && (player & jvsKit[i])) ||
                (!jvs && padKit[i] >= 0 && desktopPlayerGamepadButton(p, padKit[i])))
                kit |= 1u << (8 * p + i);
        if (!jvs && desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
            kit |= 1u << (8 * p + 6);
        if (player & BUTTON_UP)
            kit |= 1u << (8 * p);
        if (player & BUTTON_DOWN)
            kit |= 1u << (8 * p + 1);
        if (player & BUTTON_START)
            kit |= 1u << (8 * p + 7);
    }
    for (int i = 0; i < 16; i++)
        if (kit & (1u << i))
            bits |= 1u << kitBits[i / 8][i % 8];
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        bits |= 1u << 31;
    if (io->state.inputSwitch[PLAYER_1] & BUTTON_SERVICE)
        bits |= 1u << 30;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (lastCoins >= 0 && io->state.coinCount[0] != lastCoins)
        coinUntil = now + 0.05; // (longer: a jam, at the 2008 boards' timer)
    lastCoins = io->state.coinCount[0];
    if (now < coinUntil)
        bits |= 1u << 29;
    switches[0] = bits;
    switches[1] = 0;
}

// Ocean Party's six seats (fish 4): seat n's 19 buttons are input bits
// 8 + 24 n on (n 0 1P bottom left, 1 2P bottom right, 2 3P right, 3 4P top
// right, 4 5P top left, 5 6P left), bit 0 the test switch. A seat's buttons
// (as its key test shows them): the 8 fish bets (the last one the menus'
// enter), the shark, no-shell x2, shell x2, bet switch (the menus' exit),
// cancel bet, repeat bet, coin out (cancel bet and coin out move in the
// menus), hide score, key in (credit), key out, troubleshoot.
enum
{
    OP_FISH = 0, // 0..7
    OP_SHARK = 8,
    OP_NO_SHELL = 9,
    OP_SHELL = 10,
    OP_BET_SWITCH = 11,
    OP_CANCEL = 12,
    OP_REPEAT = 13,
    OP_COIN_OUT = 14,
    OP_HIDE = 15,
    OP_KEY_IN = 16,
    OP_KEY_OUT = 17,
};

static void opPress(IgsStm32 *s, int seat, int button)
{
    int bit = 8 + 24 * seat + button;
    s->inputs[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

// The keyboard plays 1P: 1 to 8 the fish, 9 no-shell, 0 the shark, - shell,
// space repeat, Backspace cancel, Tab bet switch, C key in (credits), V key
// out, O coin out, H hide score; F2 the test switch. Pads 1 to 4 play 1P to
// 4P: the d-pad up, right, down, left and Y, B, A, X the fish, LB no-shell,
// RB shell, RT the shark, LT repeat, Back key in, Start bet switch, L3
// cancel, R3 coin out.
static void opSample(IgsStm32 *s)
{
    static const unsigned long keys[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x30, 0x39, 0x2d,
                                         0xff09, 0xff08, 0x20, 0x6f, 0x68, 0x63, 0x76};
    static const int pad[][2] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, OP_FISH + 0},     {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, OP_FISH + 1},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, OP_FISH + 2},   {SDL_GAMEPAD_BUTTON_DPAD_LEFT, OP_FISH + 3},
        {SDL_GAMEPAD_BUTTON_NORTH, OP_FISH + 4},       {SDL_GAMEPAD_BUTTON_EAST, OP_FISH + 5},
        {SDL_GAMEPAD_BUTTON_SOUTH, OP_FISH + 6},       {SDL_GAMEPAD_BUTTON_WEST, OP_FISH + 7},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, OP_NO_SHELL}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, OP_SHELL},
        {SDL_GAMEPAD_BUTTON_BACK, OP_KEY_IN},          {SDL_GAMEPAD_BUTTON_START, OP_BET_SWITCH},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, OP_CANCEL},    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, OP_COIN_OUT},
    };
    static int keysSet;
    int jvs = getConfig()->inputMode == 2;
    JVSIO *io = jvs ? getJVSIO() : desktopKeyboardState();

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }
    memset(s->inputs, 0, sizeof(s->inputs));
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        s->inputs[0] |= 1;
    if (jvs)
        return;
    unsigned extra = desktopKeyboardExtra();
    for (int k = 0; k < (int)(sizeof(keys) / sizeof(keys[0])); k++)
        if (extra & (1u << k))
            opPress(s, 0, k < 8 ? OP_FISH + k : k == 8 ? OP_SHARK : k == 9 ? OP_NO_SHELL : k == 10 ? OP_SHELL :
                          k == 11 ? OP_BET_SWITCH : k == 12 ? OP_CANCEL : k == 13 ? OP_REPEAT :
                          k == 14 ? OP_COIN_OUT : k == 15 ? OP_HIDE : k == 16 ? OP_KEY_IN : OP_KEY_OUT);
    for (int p = 0; p < 4; p++)
    {
        for (int i = 0; i < (int)(sizeof(pad) / sizeof(pad[0])); i++)
            if (desktopPlayerGamepadButton(p, pad[i][0]))
                opPress(s, p, pad[i][1]);
        if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
            opPress(s, p, OP_SHARK);
        if (desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
            opPress(s, p, OP_REPEAT);
    }
}

// Hero of Robots: each player's red button (confirm, start), blue one
// (skip, cancel) and lever (select), the game's key word bits 0-2 and 4-6
// (the board's answer bytes 1 and 2); byte 3: 9 down, 10 up (the operator's
// menus), 11 test, 12 service, 13 the coins' pulses (Game's
// iIgs036_RecvCmdPrcess, Credit::_vProcess).
static void robotsSample(IgsStm32 *s)
{
    // the card binder's keys: Tab, Return, Backspace
    static const unsigned long keys[] = {0xff09, 0xff0d, 0xff08};
    static int keysSet;
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopKeyboardState();
    uint32_t bits = 0;
    struct timespec ts;

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }

    for (int p = 0; p < 2; p++)
    {
        unsigned player = io->state.inputSwitch[PLAYER_1 + p], shift = 4 * (unsigned)p;
        if ((player & (BUTTON_1 | BUTTON_START)) || desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH) ||
            desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_START))
            bits |= 1u << shift;
        if ((player & BUTTON_2) || desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST))
            bits |= 2u << shift;
        if ((player & (BUTTON_3 | BUTTON_LEFT | BUTTON_RIGHT)) ||
            desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST) ||
            desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_NORTH) ||
            desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ||
            desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
            bits |= 4u << shift;
    }
    // the card binder (igsBinder.cpp): while open, the players' buttons its
    unsigned extra = getConfig()->inputMode == 2 ? 0 : desktopKeyboardExtra(), binder = 0;
    for (int p = 0; p < 2; p++)
    {
        unsigned player = io->state.inputSwitch[PLAYER_1 + p];
        if (player & BUTTON_UP) binder |= IGS_BINDER_UP;
        if (player & BUTTON_DOWN) binder |= IGS_BINDER_DOWN;
        if (player & BUTTON_LEFT) binder |= IGS_BINDER_LEFT;
        if (player & BUTTON_RIGHT) binder |= IGS_BINDER_RIGHT;
        if ((player & BUTTON_1) || desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH))
            binder |= IGS_BINDER_PICK;
        if ((player & BUTTON_2) || desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST))
            binder |= IGS_BINDER_BACK;
        if ((player & BUTTON_3) || desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST) ||
            desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
            binder |= IGS_BINDER_TAB;
        if ((player & BUTTON_START) || desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_START))
            binder |= IGS_BINDER_SCAN;
        if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_NORTH))
            binder |= IGS_BINDER_CLEAR;
        if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
            binder |= IGS_BINDER_TOGGLE;
    }
    if (extra & 1) binder |= IGS_BINDER_TOGGLE;
    if (extra & 2) binder |= IGS_BINDER_SCAN;
    if (extra & 4) binder |= IGS_BINDER_CLEAR;
    if (igsBinderButtons(binder))
        bits = 0;
    unsigned player = io->state.inputSwitch[PLAYER_1];
    if (player & BUTTON_DOWN)
        bits |= 1u << 9;
    if (player & BUTTON_UP)
        bits |= 1u << 10;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        bits |= 1u << 11;
    if (player & BUTTON_SERVICE)
        bits |= 1u << 12;
    // each coin a 100 ms pulse, 100 ms apart
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    for (int c = 0; c < 2; c++)
    {
        if (s->fishCoins[c] >= 0 && io->state.coinCount[c] != s->fishCoins[c])
            s->robotsCoins += (unsigned)(io->state.coinCount[c] - s->fishCoins[c]) & 0xff;
        s->fishCoins[c] = io->state.coinCount[c];
    }
    if (now >= s->fishCoinUntil[0] + 0.1 && s->robotsCoins)
    {
        s->robotsCoins--;
        s->fishCoinUntil[0] = now + 0.1;
    }
    if (now < s->fishCoinUntil[0])
        bits |= 1u << 13;
    memset(s->inputs, 0, sizeof(s->inputs));
    s->inputs[0] = (uint8_t)bits;
    s->inputs[1] = (uint8_t)(bits >> 8);
}

// Crazy Shoot: three stations, each three cannons (shoot 1 to 3) and a coin
// slot; the input bits as the game's IO_KEY names them (A36.IO_KEY): station
// n's shoot 1 and 2 at 4 + 8 n and 5 + 8 n, its shoot 3 at 28, 29, 59, its
// coin at 6 + 8 n; 48 service, 78 setting, 79 record. Keys: station 1 Z X C
// (Space the middle), 5 coin; station 2 A S D, 6; station 3 J K L, 7; F1
// service, F2 setting, F3 record; a click fires the cannon aimed nearest it
// (the mouse; light guns 1-3, evdev). Pads 1-3: X A B (LT, RT the sides), Back
// coin.
static void crazyShootSample(IgsStm32 *s)
{
    static const unsigned long keys[] = {'a', 's', 'd', 'j', 'k', 'l', '7', 0xffbf /* F2 */, 0xffc0 /* F3 */};
    static const int shoot[3][3] = {{4, 5, 28}, {12, 13, 29}, {20, 21, 59}};
    static int keysSet, padCoin[3];
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopKeyboardState();
    unsigned extra = 0, player = io->state.inputSwitch[PLAYER_1];
    uint8_t bits[10] = {0};
    struct timespec ts;
#define CS_SET(b) (bits[(b) >> 3] |= (uint8_t)(1 << ((b) & 7)))

    if (!keysSet)
    {
        desktopKeyboardExtraKeys(keys, sizeof(keys) / sizeof(keys[0]));
        keysSet = 1;
    }
    if (getConfig()->inputMode != 2)
        extra = desktopKeyboardExtra();
    // A click (the mouse: player 1; light guns 1 to 3, evdev, on their
    // trigger) fires the cannon whose aim is nearest across: the cannons
    // shoot straight at their aims (AimGroup's [Left] [Center] [Right]), at
    // these fractions of the picture's width.
    static const float aimX[9] = {0.120f, 0.205f, 0.292f, 0.414f, 0.499f, 0.585f, 0.706f, 0.792f, 0.877f};
    int evdev = getConfig()->inputMode == 2;
    int mouse = !evdev && (desktopPointerButtons() & DESKTOP_POINTER_LEFT);
    for (int g = 0; g < 3; g++)
    {
        int x = io->state.analogueChannel[ANALOGUE_1 + 2 * g];
        int fired = evdev ? (io->state.inputSwitch[PLAYER_1 + g] & BUTTON_1) != 0 : g == 0 && mouse;
        if (!fired || x <= 0 || io->analogueMax <= 0)
            continue;
        float fx = (float)x / (float)io->analogueMax, best = 2;
        int pick = 0;
        for (int a = 0; a < 9; a++)
            if (fabsf(fx - aimX[a]) < best)
            {
                best = fabsf(fx - aimX[a]);
                pick = a;
            }
        CS_SET(shoot[pick / 3][pick % 3]);
    }
    // station 1 on the keyboard's player 1 (Z X C, Space)
    if (player & BUTTON_2) CS_SET(shoot[0][0]);
    if ((player & BUTTON_3) || ((player & BUTTON_1) && !mouse && !evdev)) CS_SET(shoot[0][1]);
    if (player & BUTTON_4) CS_SET(shoot[0][2]);
    for (int k = 0; k < 6; k++)
        if (extra & (1u << k))
            CS_SET(shoot[1 + k / 3][k % 3]);
    for (int p = 0; p < 3; p++)
    {
        if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST) ||
            desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
            CS_SET(shoot[p][0]);
        if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH))
            CS_SET(shoot[p][1]);
        if (desktopPlayerGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST) ||
            desktopPlayerGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
            CS_SET(shoot[p][2]);
    }
    if (player & BUTTON_SERVICE) CS_SET(48);
    if ((io->state.inputSwitch[SYSTEM] & BUTTON_TEST) || (extra & (1u << 7))) CS_SET(78);
    if (extra & (1u << 8)) CS_SET(79);

    // each coin a 100 ms pulse, 100 ms apart, on its station's slot: the
    // keyboard's coin 1 and 2, its 7, each pad's Back
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    for (int c = 0; c < 3; c++)
    {
        int count = c < 2 ? io->state.coinCount[c] : 0;
        if (c < 2 && s->fishCoins[c] >= 0 && count != s->fishCoins[c])
            s->csCoins[c] += (unsigned)(count - s->fishCoins[c]) & 0xff;
        if (c < 2)
            s->fishCoins[c] = count;
        int pad = desktopPlayerGamepadButton(c, SDL_GAMEPAD_BUTTON_BACK) || (c == 2 && (extra & (1u << 6)));
        if (pad && !padCoin[c])
            s->csCoins[c]++;
        padCoin[c] = pad;
        if (now >= s->csCoinUntil[c] + 0.1 && s->csCoins[c])
        {
            s->csCoins[c]--;
            s->csCoinUntil[c] = now + 0.1;
        }
        if (now < s->csCoinUntil[c])
            CS_SET(6 + 8 * c);
    }
    // the ticket dispensers' sensors always pulsing (50 ms each 200 ms): a
    // ticket out each time the firmware runs a dispenser, the tickets won paid
    // out at once (a ticket counted only while its motor runs)
    if (fmod(now, 0.2) < 0.05)
    {
        CS_SET(49);
        CS_SET(52);
        CS_SET(55);
    }
#undef CS_SET
    memset(s->inputs, 0, sizeof(s->inputs));
    memcpy(s->inputs, bits, sizeof(bits));
}

void igsStm32Sample(IgsStm32 *s)
{
    if (s->fish == 4)
    {
        opSample(s);
        return;
    }
    if (s->fish == 7)
    {
        robotsSample(s);
        return;
    }
    if (s->fish == 8)
    {
        crazyShootSample(s);
        return;
    }

    if (s->fish == 3)
    {
        pm3Sample(s);
        return;
    }
    if (s->fish == 2)
    {
        muziboxSample(s);
        return;
    }
    if (s->fish)
    {
        fishSample(s);
        return;
    }
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopKeyboardState();
    unsigned player = io->state.inputSwitch[PLAYER_1], switches = 0;
    double lean = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX) / 32768.0;
    double gas = desktopGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0;
    double brake = desktopGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0;
    if (getConfig()->inputMode == 2 && io->analogueMax > 0)
    {
        lean = io->state.analogueChannel[ANALOGUE_1] * 2.0 / io->analogueMax - 1;
        gas = io->state.analogueChannel[ANALOGUE_2] / (double)io->analogueMax;
        brake = io->state.analogueChannel[ANALOGUE_3] / (double)io->analogueMax;
    }
    if (player & BUTTON_LEFT) lean = -1;
    if (player & BUTTON_RIGHT) lean = 1;
    if (player & BUTTON_UP) gas = 1;
    if (player & BUTTON_DOWN) brake = 1;
    if (s->lastCoins >= 0 && io->state.coinCount[0] != s->lastCoins)
        s->coins[0] += (unsigned)(io->state.coinCount[0] - s->lastCoins) & 0xff;
    s->lastCoins = io->state.coinCount[0];
    // The switch word's bits, as Speed Rider's input printout (0x10002f14)
    // names them: bit 5 is the rider's sensor.
    if (player & (BUTTON_START | BUTTON_1)) switches |= 1u << 0; // start/turbo
    if (player & BUTTON_LEFT) switches |= 1u << 1;
    if (player & BUTTON_RIGHT) switches |= 1u << 2;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST) switches |= 1u << 3;
    if (player & BUTTON_SERVICE) switches |= 1u << 4;
    if (s->rider) switches |= 1u << 5;
    if (player & BUTTON_DOWN) switches |= 1u << 6;
    if (player & BUTTON_UP) switches |= 1u << 7;
    if (s->trace && switches != s->switches)
        printf("%s: switches %03x\n", s->name, switches);
    s->switches = switches;
    s->vr[0] = pot(s->pots[0], unit((lean + 1) / 2) * 2 - 1, 1);
    for (int i = 1; i < 4; i++)
        s->vr[i] = pot(s->pots[i], unit(i == s->throttle ? gas : brake), 0);
}
