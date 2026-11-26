// Space Invaders Frenzy uses a fixed 224x288 cabinet coordinate system.

#include "rawthrills.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"

void rtSifInstall(const RtGame *game)
{
    (void)game;
    if (getConfig()->keepAspectRatio)
        frameScaleSetAspect(7, 9);
}
