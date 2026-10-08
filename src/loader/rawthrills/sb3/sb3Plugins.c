// Superbikes 3's other cabinet plugins, which linuxloader64.so stands in
// for under their names (see rawthrills/nerf/nerf.h):
//
// - libwheel.so: the handlebar's force feedback motor, driven through the
//   PC's parallel port (SetIOPerm, then a byte on the data lines latched by
//   a strobe of the control lines). The ports are taken (the game then sends
//   its force) and the force goes to the steering wheel: sb3FfbUpdate().
// - libWebCamCV.so: the cabinet's camera (the photo screen, off by
//   default): none is ever open.
#include <stdint.h>
#include <stdio.h>
#include "../nerf/nerf.h"
#include "../../input/evdevFfb.h"

// WheelIO: SetData(force), then SetControl 0, 8, 0 latches it as the force
// (bit 7 the direction, bits 0..6 its strength); SetControl 0, 2, 0 latches
// a command (0xc0 motor off, 0xc2 on, 0xe0..0xe3 its watchdog).
static uint8_t data, control;
static float force;
static int motorOn;

int SetIOPerm(void) { return 0; }

void SetData(uint8_t value) { data = value; }

void SetControl(uint8_t value)
{
    if (value == 8 && control != 8)
        force = (data & 0x7f) / 127.f * (data & 0x80 ? -1.f : 1.f);
    else if (value == 2 && control != 2 && (data == 0xc0 || data == 0xc2))
        motorOn = data == 0xc2;
    control = value;
}

// The force the game asks of the handlebar, -1..1 (0 with the motor off):
// a centering spring it computes from where the handlebar is (positive to
// the left, as evdevFfbConstant's), on from the first game's start. Sent to
// the device steering (ANALOGUE_1), else the first with force feedback, if
// it is a wheel: a pad's rumble would buzz at every lean.
void sb3FfbUpdate(void)
{
    static int opened;
    if (!opened)
    {
        const char *path = nerfAnalogDevice(0);
        char scan[32];
        opened = 1;
        if (path && !evdevFfbOpenPath("sb3", path))
            nerfLog("%s steers but has no force feedback\n", path);
        for (int i = 0; !path && i < 64; i++)
        {
            snprintf(scan, sizeof(scan), "/dev/input/event%d", i);
            if (evdevFfbOpenPath("sb3", scan))
                break;
        }
    }
    if (evdevFfbWheel())
        evdevFfbConstant(motorOn ? force : 0.f);
}

typedef void (*WebCamCVLog)(const char *);
int WebCamCV_SetLog(WebCamCVLog cb) { (void)cb; return 0; }
void WebCamCV_PushLogMessagesToUnity(void) {}
void WebCamCV_Create(void) {}
void WebCamCV_Destroy(void) {}
void WebCamCV_Start(void) {}
void WebCamCV_Stop(void) {}
int WebCamCV_IsOpen(int placement) { (void)placement; return 0; }
int WebCamCV_GetWidth(int placement) { (void)placement; return 0; }
int WebCamCV_GetHeight(int placement) { (void)placement; return 0; }
int WebCamCV_GetBuf(int placement, uint8_t *buf, int len) { (void)placement; (void)buf; (void)len; return 0; }
float WebCamCV_GetBrightness(int p) { (void)p; return 0.f; }
void WebCamCV_SetBrightness(int p, float v) { (void)p; (void)v; }
float WebCamCV_GetContrast(int p) { (void)p; return 0.f; }
void WebCamCV_SetContrast(int p, float v) { (void)p; (void)v; }
float WebCamCV_GetSaturation(int p) { (void)p; return 0.f; }
void WebCamCV_SetSaturation(int p, float v) { (void)p; (void)v; }
float WebCamCV_GetHue(int p) { (void)p; return 0.f; }
void WebCamCV_SetHue(int p, float v) { (void)p; (void)v; }
void WebCamCV_SetSwap(int v) { (void)v; }
