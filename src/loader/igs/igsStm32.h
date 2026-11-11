#ifndef IGS_STM32_H
#define IGS_STM32_H

#include <stdint.h>

// The Speed Rider cabinets' STM32 I/O controller (igsStm32.c), on one of
// the IGS036's Atmel USARTs: Speed Rider's USART0, Speed Rider 2's USART1.
// Its replies carry the loader's inputs: the pots (lean, throttle, brake),
// the coins and the switches.
typedef struct
{
    const char *name; // the log's prefix
    int trace;
    // The pots' raw readings (0..1023): VR1 lean left, centre, right; the
    // pedals' (VR2..VR4) released and full (the centre unused). Which of
    // them is the throttle (1..3), the others the brakes.
    uint16_t pots[4][3];
    int throttle;
    // the rider's sensor (switch bit 5) reports a rider
    int rider;
    // the USART: its enables and interrupt mask; paced (Speed Rider 2), a
    // byte sent leaves TXRDY low for a tick (igsStm32Tick, a byte's time)
    // and TXEMPTY for two, a byte read or the receiver enabled RXRDY for
    // one: the edges its interrupt is taken on
    unsigned mask, txOn, rxOn;
    int paced, txBusy, rxBusy;
    // the packets: the command being sent, the reply being read
    uint8_t tx[10], rx[11], lamps[10];
    unsigned txCount, rxCount, rxPos;
    // the inputs: pots, switch word, coins not yet reported
    unsigned vr[4], switches, coins[3];
    int lastCoins;
} IgsStm32;

void igsStm32Init(IgsStm32 *s, const char *name, int trace);
// The USART's registers, by offset. A write returns -1 on a packet the
// controller does not know (the firmware is then stopped).
uint32_t igsStm32Read(IgsStm32 *s, uint32_t offset);
int igsStm32Write(IgsStm32 *s, uint32_t offset, uint32_t value);
// The USART's interrupt line.
int igsStm32Interrupt(const IgsStm32 *s);
// Paced: a byte's time passed. Returns whether a byte is in flight.
int igsStm32Tick(IgsStm32 *s);
// An exchange under way: a command partly sent, or its reply partly read.
int igsStm32Busy(const IgsStm32 *s);
// The loader's inputs, for the next reply.
void igsStm32Sample(IgsStm32 *s);

#endif // IGS_STM32_H
