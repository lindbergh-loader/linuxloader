#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    int64_t targetFrameTime;
    int64_t frameStart;
    int64_t frameEnd;
    int64_t sleepTime;
    int64_t frameOverhead;
} FpsLimit;

void initFpsLimiter();
int64_t clockNow();
void fpsLimiter(FpsLimit *stats);
double calculateFps();
void frameTiming();

void fpsOverlayUpdateFps(double fps);
void fpsOverlayToggleVisibility(void);
bool fpsOverlayIsVisible(void);
void fpsOverlaySetPosition(int pos);
int fpsOverlayGetSlot(void);
