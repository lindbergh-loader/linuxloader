#ifndef NV_GL_H
#define NV_GL_H

#include <stdint.h>

// NVIDIA's GeForce extensions some games draw with (the Gaelco games,
// Incredible Technologies'), which today's Mesa drivers do not have,
// emulated on ARB programs and core GL (nvGl.c): register combiners, vertex
// array range, fences, NV vertex and fragment programs, occlusion queries.
// The game executable's imports are hooked.
void nvGlInstall(void);
// A call of ours compiled into the display list being made (done again by
// each glCallList of it): 1 when it is only compiled, not to be done now.
int nvGlDefer(void (*apply)(const uint32_t *), const uint32_t *args, int count);
// The GL's glXGetProcAddressARB.
void *(*nvGlProcAddress(void))(const unsigned char *);

// NVIDIA's program extensions (nvProgram.c): the imports hooked with
// hook(name, replacement); an enable of GL_VERTEX_PROGRAM_NV or
// GL_FRAGMENT_PROGRAM_NV (1: not for the GL); before a draw (1: the game's
// fragment program is bound, in place of the combiners); the vertex
// program, off while the loader draws at the swap.
void nvProgramInstall(void (*hook)(const char *, void *));
int nvProgramEnable(unsigned int cap, int on);
int nvProgramBeforeDraw(void);
int nvVertexProgramOn(void);

#endif // NV_GL_H
