#ifndef EVDEV_FFB_H
#define EVDEV_FFB_H

// Force feedback on one evdev device, for the games whose wheel motor the
// loader drives itself (Raw Thrills, Namco). Straight on evdev, not SDL3's
// haptics (the Lindbergh games' forceFeedback.c): these games link SDL 1.2,
// whose SDL_Init() & co. shadow SDL3's.
//
// The effects are those of the Lindbergh driveboard's backend: a constant
// force, a damper (or friction) and a centering spring. A wheel (FF_CONSTANT)
// gets them all, its own centering spring off; a pad (FF_RUMBLE) rumbles as
// hard as the constant force pushes, and ignores the conditions.

// Opens the device: with evdev input (INPUT_MODE 2) the one steering
// (ANALOGUE_1), else the first one with force feedback. who names the caller
// in the log. 1 when there is one. (evdevInput.c: the 32-bit loader's.)
int evdevFfbOpen(const char *who);

// Opens path for force feedback, if it has any and none is open yet: 1 when
// one is open.
int evdevFfbOpenPath(const char *who, const char *path);

// The device open is a wheel (FF_CONSTANT), not a pad that rumbles.
int evdevFfbWheel(void);

// Constant force, -1 to 1, positive pulling to the left.
void evdevFfbConstant(float force);

// Centering spring, 0 (off) to 1: the device's FF_SPRING, else its
// autocenter.
void evdevFfbSpring(float strength);

// Resistance to turning, 0 (off) to 1: the device's FF_DAMPER (as fast as
// the wheel turns), else FF_FRICTION. A friction resists even at rest,
// flipping with the noise of the wheel's speed: a direct drive wheel buzzes.
void evdevFfbDamper(float strength);

#endif // EVDEV_FFB_H
