// Hero of Robots card binder: an overlay drawn over the game (Dear ImGui, at
// SDL_GL_SwapBuffers) where the player picks the cards held to the QR
// scanner. igsBinder.cpp draws it; igsStm32.c's I/O sampler passes it the
// buttons; igsCard.c's scanner reads the QR texts it hands over.
#ifndef IGS_BINDER_H
#define IGS_BINDER_H

#ifdef __cplusplus
extern "C" {
#endif

// The buttons it reads (igsBinderButtons' bits).
enum
{
    IGS_BINDER_UP = 1 << 0,
    IGS_BINDER_DOWN = 1 << 1,
    IGS_BINDER_LEFT = 1 << 2,
    IGS_BINDER_RIGHT = 1 << 3,
    IGS_BINDER_PICK = 1 << 4,   // red / Space / A: the card in or out of the hand
    IGS_BINDER_BACK = 1 << 5,   // blue / Z / B: close, the hand kept
    IGS_BINDER_TAB = 1 << 6,    // lever / X / X (west): the next tab
    IGS_BINDER_SCAN = 1 << 7,   // Return / Start: the hand to the scanner, close
    IGS_BINDER_CLEAR = 1 << 8,  // Backspace / Y (north): empty the hand
    IGS_BINDER_TOGGLE = 1 << 9, // Tab / LB: open, close
};

// The game's card table (media/localization/Card/QRCardRes.csv, from the
// game's directory: the current one) and its pictures.
void igsBinderInstall(void);
// The buttons held now (from the I/O sampler); 1 while the binder is open
// (the game then gets none of the players' buttons).
int igsBinderButtons(unsigned held);
// A card out of the dispenser: one picked for the player's collection.
void igsBinderDispensed(void);
// On the GL thread, before the frame is presented.
void igsBinderDraw(void);

// The QR scanner (igsCard.c): whether the game scans now, whether a code
// still waits to be read.
int igsCardScanning(void);
int igsCardPending(void);
void igsCardScan(const char *code);
// A word of the board's firmware (0x10000000 on), 0 before the board runs.
unsigned igsA36Rom32(unsigned address);

#ifdef __cplusplus
}
#endif

#endif
