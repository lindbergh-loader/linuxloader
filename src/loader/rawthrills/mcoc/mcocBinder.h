// Marvel Contest of Champions card binder: an overlay drawn over the game
// (Dear ImGui, in the player's glXSwapBuffers, unityVideo.c) where a player
// picks the hero cards to scan. mcocBinder.cpp draws it; mcocPlugins.c feeds
// it the players' inputs and hands the cards picked to the scan screen.
#ifndef MCOC_BINDER_H
#define MCOC_BINDER_H

#ifdef __cplusplus
extern "C" {
#endif

#define MCOC_PLAYERS 2
#define MCOC_HAND_MAX 3 // a team

// What the binder is told, from the player's stick and buttons.
enum
{
    MCOC_IN_UP,
    MCOC_IN_DOWN,
    MCOC_IN_LEFT,
    MCOC_IN_RIGHT,
    MCOC_IN_PICK,  // light attack: the card under the frame in or out of the hand
    MCOC_IN_BACK,  // medium: close, the hand kept
    MCOC_IN_CLEAR, // heavy: empty the hand
    MCOC_IN_TAB,   // special: the other tab
    MCOC_IN_SCAN,  // Start: scan the hand, close
    MCOC_IN_COUNT
};

// From the game's main thread.
void mcocBinderOpen(int player);
void mcocBinderButton(int button, int down);
void mcocBinderClose(void);
// The player it is open for, else -1.
int mcocBinderOwner(void);
// Whether the scan screen shows (the binder hints at itself there).
void mcocBinderScanScreen(int shown);

// From the GL thread, before the frame is presented.
void mcocBinderDraw(void *display, unsigned long drawable, int width, int height);

// Supplied by mcocPlugins.c: those cards scanned for the player one after
// another; how many still wait; whether the player has joined and scans.
void mcocPresent(int player, const char (*codes)[9], int count);
int mcocWaiting(int player);
int mcocScanning(int player);

#ifdef __cplusplus
}
#endif

#endif
