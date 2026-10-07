// MotoGP (Raw Thrills, g6): the cabinet's wheel force feedback, driven on the
// parallel port.
//
// The game sends its commands to the wheel's LPT by a function (0x8411f20)
// that takes the state's pointer and a command. The command's bytes are laid
// out in the state's +0x134 entry (its +0x10 count, its +0x14 data, its
// +0x8 stride, two's complement). Each frame (four times) the game sends the
// force as its signed bytes (a 0x3c4 command, its sign and direction after),
// and a race's start and end are a 0x3ce command (the motor on and off).
// The force is the entry's first two bytes, -32768 to 32767, positive turning
// the wheel left.
//
// The function is replaced: the force is sent to the evdev force feedback
// (positive pulling to the left, as the loader speaks it), and at the end of
// a race the force is zeroed (its conditions are, as Cruis'n Blast's are at
// its race's start).
//
// The address and its layout are those of TeknoParrot's "game0" (0x8411f20),
// which "game" shares: if they move, the install fails on the game's
// prologue (0x8b 0x44 0x24 0x5c).

#include <stdint.h>

#include "rawthrills.h"
#include "../input/evdevFfb.h"
#include "../log/log.h"

#define MGP_FFB_PROLOGUE 0x08411f20
// The command's start of the data (byte 0 of its count).
#define MGP_FFB_CMD_START 0x0138

// The commands: the force's data (a new force), the motor on and off, the
// force's sign and its stop (the game's own, answered nothing).
#define FFB_FORCE 0x03c4
#define FFB_MOTOR_ON 0x03ce
#define FFB_MOTOR_OFF 0x03ce
#define FFB_FORCE_SIGN 0x03c6
#define FFB_FORCE_STOP 0x03c7

static int mgpFfbHook(int state, int cmd)
{
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)(state + MGP_FFB_CMD_START);

    if (cmd == FFB_MOTOR_ON || cmd == FFB_FORCE)
    {
        // The motor's enabled, or a new force: its first two bytes, signed.
        int16_t force = (int16_t)(data[0] | (data[1] << 8));
        evdevFfbConstant(force / 32768.f);
    }
    else if (cmd == FFB_MOTOR_OFF)
    {
        // The race's end: the force zeroed, its conditions off.
        evdevFfbConstant(0.f);
        evdevFfbSpring(0.f);
        evdevFfbDamper(0.f);
    }
    return 0;
}

void rtMgpFfbInstall(void)
{
    uint8_t *target = (uint8_t *)(uintptr_t)MGP_FFB_PROLOGUE;

    if (target[0] != 0x8b || target[1] != 0x44 || target[2] != 0x24 || target[3] != 0x5c)
    {
        log_warn("Raw Thrills: MotoGP's force feedback at a different place, not installed");
        return;
    }
    evdevFfbOpen("Raw Thrills");
    rtDetourAddress(MGP_FFB_PROLOGUE, (void *)mgpFfbHook);
}
