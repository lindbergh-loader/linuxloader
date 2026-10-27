#ifndef NAMCO_FFB_H
#define NAMCO_FFB_H

#include <stdint.h>

// The steering wheel's force, as the game's clKickback holds it (see
// namcoFfb.c). Shared by the ES1 and N2 games.
typedef struct
{
    // clKickback::sm_instance, by address (the ES1 games; the N2 ones find
    // it by name), 0: no force feedback.
    uint32_t instance;
    // Offsets in the object: the spring (setSpring(unsigned char)), then the
    // viscosity (setViosity) and the reflect (setReflect(char), signed) in
    // the next bytes; the centre offset (setCenterOffset(int)).
    uint32_t effectsField;
    uint32_t centerOffsetField;
    // The spring value taken as full strength. FFB Arcade Plugin's 63 is a
    // full spring at the 0x1f the games set from onTrq(), which shakes a
    // direct drive wheel: 254, the Pacloader fork's (/127 times the game's
    // default torque scale, 0.5); Dead Heat Riders keeps the plugin's 500.
    int springRange;
    // The same for the viscosity (a damper): 254 too, the plugin's 63 for
    // Dead Heat Riders (only set in a race there).
    int viscosityRange;
    // The reflect (and the centre offset) taken as full force: the plugin's
    // 63, 40 for WMMT3, whose reflect swings about 35 in a race (the
    // fork's measure): its road at 63 is hardly felt.
    int reflectRange;
} NamcoFfb;

// Starts sending the force to the evdev force feedback device, if there is
// one; instance is clKickback::sm_instance.
void namcoFfbStart(const NamcoFfb *ffb, void *const *instance, const char *who);

#endif // NAMCO_FFB_H
