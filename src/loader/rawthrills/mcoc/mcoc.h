#ifndef MCOC_H
#define MCOC_H

// Marvel Contest of Champions (mcoc2_Data): its module's parts (see
// mcocGame.c).
#include "../unity/unity.h"

// Once a frame from the game's main thread (the RIO2's update,
// mcocPlugins.c).
void mcocFrame(void);
// The webcams the game is offered (mcocWebcam.c): 2 (one maybe blank), or 0.
int mcocWebcamCount(void);
// The test menu's scanner assignment, set to them unless it names them.
void mcocWebcamAssign(void);

// The players' inputs as the game gets them (mcocGame.c): a player's hidden
// while an overlay of the loader takes them (mcocBinder), the others as
// they are (unityInputSample: all of them).
void mcocInputSample(UnityInput *in);
void mcocInputHidePlayer(int player, int hidden);

#endif // MCOC_H
