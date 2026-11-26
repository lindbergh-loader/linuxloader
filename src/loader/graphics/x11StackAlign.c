#ifdef __linux__

#include <dlfcn.h>
#include <stddef.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/xf86vmode.h>

#include "../log/log.h"

/*
 * Stack realignment shims for the Xlib entry points the games call directly.
 *
 * Same problem as graphics/glxStackAlign.c, one library along. The games do not
 * keep the 16-byte stack alignment the i386 SysV ABI requires, and libX11 is
 * built with SSE codegen that stores xmm registers to its own frames with
 * movaps -- 128 such instructions in the shipped 32-bit library. _XSend is the
 * one that bites first, reached from the game through XFlush, but the hazard is
 * transitive: any Xlib call can end up in a routine that does this, so every
 * entry point the games reach for needs a realigning frame in front of it.
 *
 * These are pure passthroughs and change no behaviour. The live XOpenDisplay in
 * x11Bridge.c forwards to the real Xlib, so the Display handle these receive is
 * a genuine one and handing it back to libX11 is exactly what already happens.
 * Entry points x11Bridge.c does interpose are not repeated here.
 *
 * The bodies below are generated from the system headers, which is why the
 * parameters are unnamed positionals -- the declarations have to match Xlib's
 * own prototypes exactly, and including the real headers makes the compiler
 * prove that they do.
 */

#define REALIGN __attribute__((force_align_arg_pointer))

static void *resolveNext(const char *name)
{
    void *fn = dlsym(RTLD_NEXT, name);

    if (fn == NULL)
        log_error("Stack alignment shim: could not resolve %s below the loader", name);

    return fn;
}

REALIGN XWMHints *XAllocWMHints(void)
{
    static XWMHints *(*real)(void) = NULL;
    if (real == NULL)
        real = resolveNext("XAllocWMHints");
    return real ? real() : NULL;
}

REALIGN int XAutoRepeatOn(Display *a0)
{
    static int (*real)(Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XAutoRepeatOn");
    return real ? real(a0) : 0;
}

REALIGN int XChangeKeyboardControl(Display *a0, long unsigned int a1, XKeyboardControl *a2)
{
    static int (*real)(Display *, long unsigned int, XKeyboardControl *) = NULL;
    if (real == NULL)
        real = resolveNext("XChangeKeyboardControl");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN int XChangeProperty(Display *a0, Window a1, Atom a2, Atom a3, int a4, int a5, const unsigned char *a6, int a7)
{
    static int (*real)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XChangeProperty");
    return real ? real(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
}

REALIGN int XChangeWindowAttributes(Display *a0, Window a1, long unsigned int a2, XSetWindowAttributes *a3)
{
    static int (*real)(Display *, Window, long unsigned int, XSetWindowAttributes *) = NULL;
    if (real == NULL)
        real = resolveNext("XChangeWindowAttributes");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN int XCloseDisplay(Display *a0)
{
    static int (*real)(Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XCloseDisplay");
    return real ? real(a0) : 0;
}

REALIGN Pixmap XCreateBitmapFromData(Display *a0, Drawable a1, const char *a2, unsigned int a3, unsigned int a4)
{
    static Pixmap (*real)(Display *, Drawable, const char *, unsigned int, unsigned int) = NULL;
    if (real == NULL)
        real = resolveNext("XCreateBitmapFromData");
    return real ? real(a0, a1, a2, a3, a4) : 0;
}

REALIGN Colormap XCreateColormap(Display *a0, Window a1, Visual *a2, int a3)
{
    static Colormap (*real)(Display *, Window, Visual *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XCreateColormap");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN Cursor XCreateFontCursor(Display *a0, unsigned int a1)
{
    static Cursor (*real)(Display *, unsigned int) = NULL;
    if (real == NULL)
        real = resolveNext("XCreateFontCursor");
    return real ? real(a0, a1) : 0;
}

REALIGN Cursor XCreatePixmapCursor(Display *a0, Pixmap a1, Pixmap a2, XColor *a3, XColor *a4, unsigned int a5, unsigned int a6)
{
    static Cursor (*real)(Display *, Pixmap, Pixmap, XColor *, XColor *, unsigned int, unsigned int) = NULL;
    if (real == NULL)
        real = resolveNext("XCreatePixmapCursor");
    return real ? real(a0, a1, a2, a3, a4, a5, a6) : 0;
}

REALIGN Pixmap XCreatePixmapFromBitmapData(Display *a0, Drawable a1, char *a2, unsigned int a3, unsigned int a4, long unsigned int a5,
                                           long unsigned int a6, unsigned int a7)
{
    static Pixmap (*real)(Display *, Drawable, char *, unsigned int, unsigned int, long unsigned int, long unsigned int, unsigned int) =
        NULL;
    if (real == NULL)
        real = resolveNext("XCreatePixmapFromBitmapData");
    return real ? real(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
}

REALIGN int XDefineCursor(Display *a0, Window a1, Cursor a2)
{
    static int (*real)(Display *, Window, Cursor) = NULL;
    if (real == NULL)
        real = resolveNext("XDefineCursor");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN int XDestroyWindow(Display *a0, Window a1)
{
    static int (*real)(Display *, Window) = NULL;
    if (real == NULL)
        real = resolveNext("XDestroyWindow");
    return real ? real(a0, a1) : 0;
}

REALIGN void _XEatData(Display *a0, long unsigned int a1)
{
    static void (*real)(Display *, long unsigned int) = NULL;
    if (real == NULL)
        real = resolveNext("_XEatData");
    if (real)
        real(a0, a1);
}

REALIGN int XEventsQueued(Display *a0, int a1)
{
    static int (*real)(Display *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XEventsQueued");
    return real ? real(a0, a1) : 0;
}

REALIGN XExtDisplayInfo *XextAddDisplay(XExtensionInfo *a0, Display *a1, const char *a2, XExtensionHooks *a3, int a4, XPointer a5)
{
    static XExtDisplayInfo *(*real)(XExtensionInfo *, Display *, const char *, XExtensionHooks *, int, XPointer) = NULL;
    if (real == NULL)
        real = resolveNext("XextAddDisplay");
    return real ? real(a0, a1, a2, a3, a4, a5) : NULL;
}

REALIGN XExtensionInfo *XextCreateExtension(void)
{
    static XExtensionInfo *(*real)(void) = NULL;
    if (real == NULL)
        real = resolveNext("XextCreateExtension");
    return real ? real() : NULL;
}

REALIGN XExtDisplayInfo *XextFindDisplay(XExtensionInfo *a0, Display *a1)
{
    static XExtDisplayInfo *(*real)(XExtensionInfo *, Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XextFindDisplay");
    return real ? real(a0, a1) : NULL;
}

REALIGN int XextRemoveDisplay(XExtensionInfo *a0, Display *a1)
{
    static int (*real)(XExtensionInfo *, Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XextRemoveDisplay");
    return real ? real(a0, a1) : 0;
}

REALIGN int XF86VidModeSetViewPort(Display *a0, int a1, int a2, int a3)
{
    static int (*real)(Display *, int, int, int) = NULL;
    if (real == NULL)
        real = resolveNext("XF86VidModeSetViewPort");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN void _XFlush(Display *a0)
{
    static void (*real)(Display *) = NULL;
    if (real == NULL)
        real = resolveNext("_XFlush");
    if (real)
        real(a0);
}

REALIGN int XFlush(Display *a0)
{
    static int (*real)(Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XFlush");
    return real ? real(a0) : 0;
}

REALIGN int XFree(void *a0)
{
    static int (*real)(void *) = NULL;
    if (real == NULL)
        real = resolveNext("XFree");
    return real ? real(a0) : 0;
}

REALIGN int XFreeColormap(Display *a0, Colormap a1)
{
    static int (*real)(Display *, Colormap) = NULL;
    if (real == NULL)
        real = resolveNext("XFreeColormap");
    return real ? real(a0, a1) : 0;
}

REALIGN int XFreeCursor(Display *a0, Cursor a1)
{
    static int (*real)(Display *, Cursor) = NULL;
    if (real == NULL)
        real = resolveNext("XFreeCursor");
    return real ? real(a0, a1) : 0;
}

REALIGN int XFreePixmap(Display *a0, Pixmap a1)
{
    static int (*real)(Display *, Pixmap) = NULL;
    if (real == NULL)
        real = resolveNext("XFreePixmap");
    return real ? real(a0, a1) : 0;
}

REALIGN int XGetErrorText(Display *a0, int a1, char *a2, int a3)
{
    static int (*real)(Display *, int, char *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XGetErrorText");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN int XGetGeometry(Display *a0, Drawable a1, Window *a2, int *a3, int *a4, unsigned int *a5, unsigned int *a6, unsigned int *a7,
                         unsigned int *a8)
{
    static int (*real)(Display *, Drawable, Window *, int *, int *, unsigned int *, unsigned int *, unsigned int *, unsigned int *) = NULL;
    if (real == NULL)
        real = resolveNext("XGetGeometry");
    return real ? real(a0, a1, a2, a3, a4, a5, a6, a7, a8) : 0;
}

REALIGN int XGrabKeyboard(Display *a0, Window a1, int a2, int a3, int a4, Time a5)
{
    static int (*real)(Display *, Window, int, int, int, Time) = NULL;
    if (real == NULL)
        real = resolveNext("XGrabKeyboard");
    return real ? real(a0, a1, a2, a3, a4, a5) : 0;
}

REALIGN int XInitThreads(void)
{
    static int (*real)(void) = NULL;
    if (real == NULL)
        real = resolveNext("XInitThreads");
    return real ? real() : 0;
}

REALIGN Atom XInternAtom(Display *a0, const char *a1, int a2)
{
    static Atom (*real)(Display *, const char *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XInternAtom");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN KeyCode XKeysymToKeycode(Display *a0, KeySym a1)
{
    static KeyCode (*real)(Display *, KeySym) = NULL;
    if (real == NULL)
        real = resolveNext("XKeysymToKeycode");
    return real ? real(a0, a1) : 0;
}

REALIGN void XLockDisplay(Display *a0)
{
    static void (*real)(Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XLockDisplay");
    if (real)
        real(a0);
}

REALIGN KeySym XLookupKeysym(XKeyEvent *a0, int a1)
{
    static KeySym (*real)(XKeyEvent *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XLookupKeysym");
    return real ? real(a0, a1) : 0;
}

REALIGN int XLookupString(XKeyEvent *a0, char *a1, int a2, KeySym *a3, XComposeStatus *a4)
{
    static int (*real)(XKeyEvent *, char *, int, KeySym *, XComposeStatus *) = NULL;
    if (real == NULL)
        real = resolveNext("XLookupString");
    return real ? real(a0, a1, a2, a3, a4) : 0;
}

REALIGN int XMissingExtension(Display *a0, const char *a1)
{
    static int (*real)(Display *, const char *) = NULL;
    if (real == NULL)
        real = resolveNext("XMissingExtension");
    return real ? real(a0, a1) : 0;
}

REALIGN int XParseColor(Display *a0, Colormap a1, const char *a2, XColor *a3)
{
    static int (*real)(Display *, Colormap, const char *, XColor *) = NULL;
    if (real == NULL)
        real = resolveNext("XParseColor");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN int XPeekEvent(Display *a0, XEvent *a1)
{
    static int (*real)(Display *, XEvent *) = NULL;
    if (real == NULL)
        real = resolveNext("XPeekEvent");
    return real ? real(a0, a1) : 0;
}

REALIGN int XQueryPointer(Display *a0, Window a1, Window *a2, Window *a3, int *a4, int *a5, int *a6, int *a7, unsigned int *a8)
{
    static int (*real)(Display *, Window, Window *, Window *, int *, int *, int *, int *, unsigned int *) = NULL;
    if (real == NULL)
        real = resolveNext("XQueryPointer");
    return real ? real(a0, a1, a2, a3, a4, a5, a6, a7, a8) : 0;
}

REALIGN int _XRead(Display *a0, char *a1, long int a2)
{
    static int (*real)(Display *, char *, long int) = NULL;
    if (real == NULL)
        real = resolveNext("_XRead");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN void _XReadPad(Display *a0, char *a1, long int a2)
{
    static void (*real)(Display *, char *, long int) = NULL;
    if (real == NULL)
        real = resolveNext("_XReadPad");
    if (real)
        real(a0, a1, a2);
}

REALIGN int XRefreshKeyboardMapping(XMappingEvent *a0)
{
    static int (*real)(XMappingEvent *) = NULL;
    if (real == NULL)
        real = resolveNext("XRefreshKeyboardMapping");
    return real ? real(a0) : 0;
}

REALIGN int _XReply(Display *a0, xReply *a1, int a2, int a3)
{
    static int (*real)(Display *, xReply *, int, int) = NULL;
    if (real == NULL)
        real = resolveNext("_XReply");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN int XSelectInput(Display *a0, Window a1, long int a2)
{
    static int (*real)(Display *, Window, long int) = NULL;
    if (real == NULL)
        real = resolveNext("XSelectInput");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN void _XSend(Display *a0, const char *a1, long int a2)
{
    static void (*real)(Display *, const char *, long int) = NULL;
    if (real == NULL)
        real = resolveNext("_XSend");
    if (real)
        real(a0, a1, a2);
}

REALIGN int XSetCloseDownMode(Display *a0, int a1)
{
    static int (*real)(Display *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XSetCloseDownMode");
    return real ? real(a0, a1) : 0;
}

REALIGN XErrorHandler XSetErrorHandler(XErrorHandler a0)
{
    static XErrorHandler (*real)(XErrorHandler) = NULL;
    if (real == NULL)
        real = resolveNext("XSetErrorHandler");
    return real ? real(a0) : 0;
}

REALIGN int XSetStandardProperties(Display *a0, Window a1, const char *a2, const char *a3, Pixmap a4, char **a5, int a6, XSizeHints *a7)
{
    static int (*real)(Display *, Window, const char *, const char *, Pixmap, char **, int, XSizeHints *) = NULL;
    if (real == NULL)
        real = resolveNext("XSetStandardProperties");
    return real ? real(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
}

REALIGN void XSetWMNormalHints(Display *a0, Window a1, XSizeHints *a2)
{
    static void (*real)(Display *, Window, XSizeHints *) = NULL;
    if (real == NULL)
        real = resolveNext("XSetWMNormalHints");
    if (real)
        real(a0, a1, a2);
}

REALIGN int XSetWMProtocols(Display *a0, Window a1, Atom *a2, int a3)
{
    static int (*real)(Display *, Window, Atom *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XSetWMProtocols");
    return real ? real(a0, a1, a2, a3) : 0;
}

REALIGN int XStoreName(Display *a0, Window a1, const char *a2)
{
    static int (*real)(Display *, Window, const char *) = NULL;
    if (real == NULL)
        real = resolveNext("XStoreName");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN int XStringListToTextProperty(char **a0, int a1, XTextProperty *a2)
{
    static int (*real)(char **, int, XTextProperty *) = NULL;
    if (real == NULL)
        real = resolveNext("XStringListToTextProperty");
    return real ? real(a0, a1, a2) : 0;
}

REALIGN int XSync(Display *a0, int a1)
{
    static int (*real)(Display *, int) = NULL;
    if (real == NULL)
        real = resolveNext("XSync");
    return real ? real(a0, a1) : 0;
}

REALIGN int XUndefineCursor(Display *a0, Window a1)
{
    static int (*real)(Display *, Window) = NULL;
    if (real == NULL)
        real = resolveNext("XUndefineCursor");
    return real ? real(a0, a1) : 0;
}

REALIGN int XUngrabKeyboard(Display *a0, Time a1)
{
    static int (*real)(Display *, Time) = NULL;
    if (real == NULL)
        real = resolveNext("XUngrabKeyboard");
    return real ? real(a0, a1) : 0;
}

REALIGN int XUngrabPointer(Display *a0, Time a1)
{
    static int (*real)(Display *, Time) = NULL;
    if (real == NULL)
        real = resolveNext("XUngrabPointer");
    return real ? real(a0, a1) : 0;
}

REALIGN void XUnlockDisplay(Display *a0)
{
    static void (*real)(Display *) = NULL;
    if (real == NULL)
        real = resolveNext("XUnlockDisplay");
    if (real)
        real(a0);
}

REALIGN int XWarpPointer(Display *a0, Window a1, Window a2, int a3, int a4, unsigned int a5, unsigned int a6, int a7, int a8)
{
    static int (*real)(Display *, Window, Window, int, int, unsigned int, unsigned int, int, int) = NULL;
    if (real == NULL)
        real = resolveNext("XWarpPointer");
    return real ? real(a0, a1, a2, a3, a4, a5, a6, a7, a8) : 0;
}

#endif
