// The Speed Rider cabinets' STM32 I/O controller, on one of the IGS036's
// Atmel USARTs (see igsStm32.h).
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_gamepad.h>

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
    s->rxCount = sizeof(s->rx);
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
            return stm32Byte(s, (uint8_t)value);
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

// The firmware keeps the pots' calibration, the switches' debounce and key
// edges, and the credits.
void igsStm32Sample(IgsStm32 *s)
{
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
