// Angry Birds Arcade (Raw Thrills, g6): the touch frame the light gun stands
// in for.
//
// The cabinet's slingshot fires a real ball at the screen, and an infrared
// touch frame in front of it (a Baanto, USB 2453:0100) reports where the
// ball hits: that hit is the shot. Here the shot is P1's gun (the desktop
// pointer): each pull of its trigger (BUTTON_1) is a hit where it aims.
//
// The frame's thread (0x8788bfb) opens a supported frame (0x8788d79: a table
// of five, the Baanto the last one, model 4), then reads its reports in a
// loop (0x87874d7, negative when the frame is gone): a touch down stores its
// position (0x8d49f60/64) and adds it to a ring of ten hits (0x8d49f80, x
// and y each, -1 for none; 0x8ed2ca4 counts them), which the game empties
// once a frame (0x8787cb9). Positions are the frame's, 0..0xfff, x from the
// right of the picture and y from its top. The open and the read are
// answered here: the frame is always there, and its reads add the gun's
// shots.
//
// The slingshot's position (RIO analog channels, the game's inputs 0x187
// and 0x188) follows the gun too. The game scales its raw values by the
// slingshot calibration's bounds (x min/max, y min/max at 0x8a184e0, floats,
// in raw units), which an operator sets by moving the slingshot to its
// limits: they are the gun's whole range instead, set each frame (saved
// bounds of a real slingshot, or none at all, would leave it still).

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "rawthrills.h"
#include "rtGame.h"
#include "../hardware/lindbergh/jvs.h"

#define TOUCH_MODEL ((volatile int32_t *)0x08a135ec)
#define TOUCH_CONNECTED ((volatile int32_t *)0x08d49b44)
#define TOUCH_PENDING ((volatile int32_t *)0x08d49b50) // set until the frame is first read
#define TOUCH_LAST ((volatile int32_t *)0x08d49f60)
#define TOUCH_RING ((volatile int32_t *)0x08d49f80)
#define TOUCH_COUNT ((volatile int32_t *)0x08ed2ca4)
#define TOUCH_RING_SIZE 10
#define SLING_BOUNDS ((volatile float *)0x08a184e0) // x min, x max, y min, y max
#define BAANTO_MODEL 4
#define TOUCH_MAX 0xfff

// Shots taken in the I/O loop (the main thread), handed to the frame's
// thread: positions in the frame's units.
#define QUEUE_SIZE 16
static int32_t queueX[QUEUE_SIZE], queueY[QUEUE_SIZE];
static atomic_uint queueHead, queueTail;

static int touchOpen(void)
{
    *TOUCH_MODEL = BAANTO_MODEL;
    *TOUCH_CONNECTED = 1;
    return 0;
}

// A read of the frame: the shots since the last one, as touch downs.
static int touchRead(void)
{
    usleep(2000);
    while (atomic_load(&queueTail) != atomic_load(&queueHead))
    {
        unsigned int i = atomic_load(&queueTail) % QUEUE_SIZE;
        int32_t n = *TOUCH_COUNT % TOUCH_RING_SIZE;

        TOUCH_LAST[0] = queueX[i];
        TOUCH_LAST[1] = queueY[i];
        TOUCH_RING[2 * n] = queueX[i];
        TOUCH_RING[2 * n + 1] = queueY[i];
        (*TOUCH_COUNT)++;
        atomic_fetch_add(&queueTail, 1);
        printf("Angry Birds: hit at %d, %d\n", queueX[i], queueY[i]);
    }
    *TOUCH_PENDING = 0;
    return 0;
}

static int32_t touchUnits(float v)
{
    int32_t t = (int32_t)(v * TOUCH_MAX + 0.5f);
    return t < 1 ? 1 : t > TOUCH_MAX - 1 ? TOUCH_MAX - 1 : t;
}

// Each frame: a pull of P1's trigger is a shot where the gun aims (none off
// the screen). With evdev input, pulls quicker than a frame are counted too.
void rtAbIoFrame(JVSIO *io)
{
    static int wasHeld;
    static unsigned int lastPresses;
    int held = (io->state.inputSwitch[PLAYER_1] & BUTTON_1) != 0;
    int shots = held && !wasHeld;
    float x, y;

    if (io == getJVSIO())
    {
        int taps = rtSwitchTaps(io, PLAYER_1, BUTTON_1, &lastPresses);
        if (taps > shots)
            shots = taps;
    }
    wasHeld = held;
    SLING_BOUNDS[0] = SLING_BOUNDS[2] = 0.f;
    SLING_BOUNDS[1] = SLING_BOUNDS[3] = (float)io->analogueMax;
    if (shots <= 0)
        return;
    if (!rtGunOnScreen(0, &x, &y))
    {
        printf("Angry Birds: shot off the screen\n");
        return;
    }
    for (; shots > 0; shots--)
    {
        unsigned int head = atomic_load(&queueHead);
        if (head - atomic_load(&queueTail) >= QUEUE_SIZE)
            break;
        queueX[head % QUEUE_SIZE] = touchUnits(1.f - x);
        queueY[head % QUEUE_SIZE] = touchUnits(y);
        atomic_store(&queueHead, head + 1);
        printf("Angry Birds: shot at %.3f, %.3f\n", x, y);
    }
}

void rtAbInstall(const RtGame *game)
{
    (void)game;
    rtDetourAddress(0x08788d79, (void *)touchOpen);
    rtDetourAddress(0x087874d7, (void *)touchRead);
}
