#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "../config/config.h"
#include "../graphics/fpsLimiter.h"
#include "../graphics/overlayMsgs.h"

FpsLimit fpsLimit;
double lastTime = 0.0;
int frameCount = 0;
double fps = 0.0;

static int gFpsSlot = -1;
static int gFpsPosition = 0;
static bool gFpsVisible = false;

void initFpsLimiter()
{
    /* Read overlay config: position from config, visibility from config */
    gFpsPosition = getConfig()->fpsOverlayPosition;
    if (gFpsPosition < 0) gFpsPosition = 0;
    if (gFpsPosition > 3) gFpsPosition = 3;

    gFpsVisible = (getConfig()->fpsOverlayEnabled != 0);

    if (getConfig()->fpsLimiter == 1 && getConfig()->fpsTarget > 0)
    {
        fpsLimit.targetFrameTime = 1000000 / getConfig()->fpsTarget;
        fpsLimit.frameEnd = clockNow();
    }
}

double getTimeInMilliseconds()
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return (time.tv_sec * 1000.0) + (time.tv_usec / 1000.0);
}

double getTimeInSeconds()
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return (double)time.tv_sec + (double)time.tv_usec / 1000000.0;
}

double calculateFps()
{
    double currentTime = getTimeInSeconds();
    double deltaTime = currentTime - lastTime;
    frameCount++;
    if (deltaTime >= 1.0)
    {
        fps = frameCount / deltaTime;
        frameCount = 0;
        lastTime = currentTime;
    }
    return fps;
}

// Microseconds, in 64 bits: a 32-bit long (i686) overflowed every 71 minutes,
// and the first frame then slept for up to 35 of them. Monotonic, so a clock
// set by NTP does not stall a frame either.
int64_t clockNow()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

void frameTiming()
{
    // Paths that never ran initMain's initFpsLimiter (Raw Thrills): start
    // timing here.
    if (!fpsLimit.targetFrameTime)
    {
        initFpsLimiter();
        if (!fpsLimit.targetFrameTime)
            return;
    }
    fpsLimit.frameStart = clockNow();
    fpsLimiter(&fpsLimit);
    fpsLimit.frameEnd = clockNow();
}

void fpsLimiter(FpsLimit *stats)
{
    stats->sleepTime = stats->targetFrameTime - (stats->frameStart - stats->frameEnd);

    if (stats->sleepTime > stats->frameOverhead)
    {
        int64_t adjustedSleep = stats->sleepTime - stats->frameOverhead;

        usleep(adjustedSleep);

        stats->frameOverhead = (clockNow() - stats->frameStart) - adjustedSleep;

        if (stats->frameOverhead > stats->targetFrameTime / 2)
        {
            stats->frameOverhead = 0;
        }
    }
}

void fpsOverlayUpdateFps(double fps)
{
    char fpsStr[64];
    snprintf(fpsStr, sizeof(fpsStr), "FPS: %.2f", fps);

    if (gFpsSlot < 0)
    {
        int pos = gFpsPosition;
        if (pos < 0) pos = 0;
        if (pos > 3) pos = 3;
        gFpsSlot = overlayShowMessage(fpsStr, 0, (OverlayPosition)pos);

        if (!gFpsVisible && gFpsSlot >= 0)
            overlaySetMessageVisible(gFpsSlot, false);
    }
    else
    {
        overlayUpdateMessageText(gFpsSlot, fpsStr);
    }
}

void fpsOverlayToggleVisibility(void)
{
    if (gFpsSlot < 0)
    {
        gFpsVisible = !gFpsVisible;
        return;
    }
    gFpsVisible = !gFpsVisible;
    overlaySetMessageVisible(gFpsSlot, gFpsVisible);
}

bool fpsOverlayIsVisible(void)
{
    return gFpsVisible;
}

void fpsOverlaySetPosition(int pos)
{
    gFpsPosition = pos;
}

int fpsOverlayGetSlot(void)
{
    return gFpsSlot;
}