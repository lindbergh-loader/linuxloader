#pragma once

#ifdef __linux__
/**
 *   Returns a trampoline that calls fn with a 16-byte aligned stack. Meant for
 *   the GL entry points the games resolve through glXGetProcAddressARB and call
 *   straight out of their own frames, which are not ABI-aligned. Returns fn
 *   unchanged if a trampoline could not be built.
 */
void *glRealignWrap(void *fn);

/**
 *   libGLX's own glXGetProcAddressARB. The loader exports a realigning
 *   glXGetProcAddressARB (glxStackAlign.c), so loader code that wants the
 *   driver's unwrapped entry point must call this instead.
 */
void *glXGetProcAddressReal(const unsigned char *procName);
#endif
