// Minecraft Dungeons Arcade card binder: an overlay drawn over the game (Dear
// ImGui, in the loader's SwapBuffers) where the player picks the cards a
// station scans.  g7McdBinder.cpp draws it; g7Minecraft.c reads the keyboard
// and the pads and fills the game's scan slots.
#ifndef G7_MCD_BINDER_H
#define G7_MCD_BINDER_H

#ifdef __cplusplus
extern "C" {
#endif

#define MCD_STATIONS 4
#define MCD_HAND_MAX 8

// What the binder is told, whatever pressed it.
enum
{
    MCD_IN_UP,
    MCD_IN_DOWN,
    MCD_IN_LEFT,
    MCD_IN_RIGHT,
    MCD_IN_PICK,     // A / Space: the card under the frame in or out of the hand
    MCD_IN_BACK,     // B / Backspace: close, the hand kept (Escape quits the game)
    MCD_IN_PLAYER,   // Y / X / C / V: next station
    MCD_IN_TAB_PREV, // LB / Q
    MCD_IN_TAB_NEXT, // RB / E
    MCD_IN_SCAN,     // Start / Enter: scan the hand, close
    MCD_IN_CLEAR,    // Select / Tab: empty the hand
    MCD_IN_P1,       // 1..4: that station
    MCD_IN_P2,
    MCD_IN_P3,
    MCD_IN_P4,
    MCD_IN_COUNT
};

// From the input thread.
void mcdBinderOpen(int station);
void mcdBinderButton(int button, int down, int fromPad);
int mcdBinderIsOpen(void);
// Close it (from the input thread when its key is pressed again).
void mcdBinderClose(void);
// The hand of a station: set from a list of codes (MCD_CARDS), or read.
void mcdBinderSetHand(int station, const char *codes);
int mcdBinderHand(int station, char out[][9], int max);

// From the GL thread, before the frame is presented.
void mcdBinderDraw(void *display, unsigned long drawable, int width, int height);

// Supplied by g7Minecraft.c: send those cards to the station's scan slot one
// after another; whether the game is on a scan screen; how many cards of a
// station still wait.
void mcdPresent(int station, const char (*codes)[9], int count);
int mcdScanScreen(void);
int mcdWaiting(int station);

#ifdef __cplusplus
}
#endif

#endif
