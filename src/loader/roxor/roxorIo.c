// The parallel port I/O board of the Roxor games (see roxorGame.h). Its
// functions, which use ioperm and in/out on the port, are replaced: the
// board is attached, its outputs (lamps, coin counter, dispenser) taken and
// dropped, and its two switch registers (address 0: the word's high byte,
// address 1: its low byte; a switch closed reads 0) read from the loader's
// input. The game polls them every 10 ms from a thread of its own, debounces
// them over three reads and does the rest (coins, credits, start).
//
// The input: with evdev (INPUT_MODE 2) the JVS state, else the desktop's
// keyboard (desktopInput.c: 1 start, 5 coin, F2 test, arrows). Player 1's
// start, left and right, the test button as the operator menu's, and the
// coins, counted, each held long enough for the debounce.

#include <stdint.h>

#include "roxor.h"
#include "../common/desktopInput.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../patching/flowControl.h"

// A coin's switch held, then released, for this many board updates (10 ms).
#define COIN_PULSE 8

static const RoxorGame *game;
static unsigned int (*bitFromPin)(const char *);

static struct
{
    int seen;       // the coin count read last
    int owed;       // coins not passed to the game yet
    int phase;      // updates left of the current press (> 0) or gap (< 0)
} coins[2];
static int countsRead;

static unsigned int pinBit(const char *pin)
{
    return pin && bitFromPin ? bitFromPin(pin) : 0;
}

// The switches closed, as the game's word (a bit per pin).
static unsigned int switches(void)
{
    static unsigned int coinBits[2], start, left, right, operatorMenu;
    static int bitsKnown;
    JVSIO *io = getConfig()->inputMode == 2 ? getJVSIO() : desktopInputState();
    unsigned int player = io->state.inputSwitch[PLAYER_1], word = 0;

    if (!bitsKnown)
    {
        coinBits[0] = pinBit(game->coin1);
        coinBits[1] = pinBit(game->coin2);
        start = pinBit(game->start);
        left = pinBit(game->left);
        right = pinBit(game->right);
        operatorMenu = pinBit(game->operatorMenu);
        bitsKnown = 1;
    }
    if (player & BUTTON_START)
        word |= start;
    if (player & BUTTON_LEFT)
        word |= left;
    if (player & BUTTON_RIGHT)
        word |= right;
    if (io->state.inputSwitch[SYSTEM] & BUTTON_TEST)
        word |= operatorMenu;
    for (int c = 0; c < 2; c++)
    {
        int count = io->state.coinCount[c];
        if (countsRead && count > coins[c].seen)
            coins[c].owed += count - coins[c].seen;
        coins[c].seen = count;
        if (coins[c].phase > 0 && --coins[c].phase == 0)
            coins[c].phase = -COIN_PULSE;
        else if (coins[c].phase < 0)
            coins[c].phase++;
        else if (coins[c].phase == 0 && coins[c].owed > 0)
        {
            coins[c].owed--;
            coins[c].phase = COIN_PULSE;
        }
        if (coins[c].phase > 0)
            word |= coinBits[c];
    }
    countsRead = 1;
    return word;
}

__attribute__((force_align_arg_pointer)) static int openIoBoard(void)
{
    *(volatile char *)(uintptr_t)game->ioBoardAttached = 1;
    *(volatile unsigned int *)(uintptr_t)game->ioBoardSwitchMask = 0xffffffff;
    return 1;
}

__attribute__((force_align_arg_pointer)) static int closeIoBoard(void)
{
    *(volatile char *)(uintptr_t)game->ioBoardAttached = 0;
    return 1;
}

__attribute__((force_align_arg_pointer)) static int checkIoBoardError(void)
{
    return 1;
}

__attribute__((force_align_arg_pointer)) static int ioOut(unsigned int address, unsigned int value)
{
    (void)address;
    (void)value;
    return 1;
}

// Address 1 is read first at each update: the switches are sampled then.
__attribute__((force_align_arg_pointer)) static int ioIn(unsigned int address, unsigned char *value)
{
    static unsigned int word;

    if ((address & 0xffff) == 1)
        word = switches();
    *value = (unsigned char)(~word >> ((address & 0xffff) == 0 ? 8 : 0));
    return 1;
}

void roxorIoInstall(const RoxorGame *g)
{
    game = g;
    *(void **)&bitFromPin = (void *)(uintptr_t)g->bitFromPin;
    detourFunction(g->openIoBoard, openIoBoard);
    detourFunction(g->closeIoBoard, closeIoBoard);
    detourFunction(g->checkIoBoardError, checkIoBoardError);
    detourFunction(g->ioIn, ioIn);
    detourFunction(g->ioOut, ioOut);
}
