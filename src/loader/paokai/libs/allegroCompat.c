// Built into liballeg.so.4.0 (scripts/linux/build-gogostrike-libs.sh): what
// the game takes from the cabinet's Allegro and this build leaves out.

#include <allegro.h>

// DGA 1's bank switch, which the game's static half (xwins.s) calls: no DGA
// here, never called.
unsigned long _xdga_switch_bank(BITMAP *bmp, int line)
{
    return (unsigned long)bmp->line[line];
}

// Data the game copies at its start (copy relocations): the VBE/AF console
// driver's (vbeafs.s, in the game: unused under X), and the cabinet's PS/2
// mouse driver's, the trackball's packets (the loader fills them, see
// src/loader/paokai/paokaiIo.c).
void *_accel_driver, *_accel_set_bank, *_accel_idle;
int _accel_active, _vbeaf_selector;
int ps2mouseidx, ps2mousepx[256], ps2mousepy[256];
