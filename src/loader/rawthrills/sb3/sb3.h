#ifndef SB3_H
#define SB3_H

// Superbikes 3 (fnfmega_Data): its module's parts (see sb3.c).
#include "../unity/unity.h"

// The calibration the RIO's scale needs, forced into the game's
// preferences (sb3Mono.c): called from the main thread.
void sb3ForceCalibration(void);
// The game shows its test menu (sb3Mono.c): from the main thread.
int sb3InTestMenu(void);
// The handlebar's range on the RIO1 (sb3Mono.c): from the main thread,
// again now and then (a calibration changes it).
void sb3SteerRange(void);
// The handlebar motor on the steering wheel (sb3Plugins.c): from the main
// thread, once a frame.
void sb3FfbUpdate(void);

#endif // SB3_H
