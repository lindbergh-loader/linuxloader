#pragma once

#include <stdbool.h>
#include "x11types.h"

struct SDL_Window;

#ifdef _WIN32
#ifdef __cplusplus  
extern "C" {
#endif
    // --- Display & Window Management ---
    Display *bridgeXOpenDisplay(const char *name);
    int bridgeXCloseDisplay(Display *dpy);
    Window bridgeXCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                                unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                                XSetWindowAttributes *attributes);
    int bridgeXDestroyWindow(Display *dpy, Window win);
    int bridgeXConfigureWindow(Display *dpy, Window win, unsigned long value_mask, int *changes);
    void bridgeXMapWindow(Display *dpy, Window win);
    int bridgeXMoveWindow(Display *dpy, Window win, int x, int y);
    int bridgeXResizeWindow(Display *dpy, Window win, unsigned int width, unsigned int height);
    int bridgeXSync(Display *dpy, int discard);
    int bridgeXFlush(Display *dpy);
    void bridgeXLockDisplay(Display *dpy);
    void bridgeXUnlockDisplay(Display *dpy);
    void bridgeXSetErrorHandler(void *handler);
    int bridgeXGetErrorText(Display *dpy, int code, char *buffer, int length);

    int bridgeXDisplayWidth(void *dpy, int screen_number);
    int bridgeXDisplayHeight(void *dpy, int screen_number);
    int bridgeXDisplayWidthMM(void *dpy, int screen_number);
    int bridgeXDisplayHeightMM(void *dpy, int screen_number);
    Atom bridgeXInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist);
    int bridgeXSetStandardProperties(Display *dpy, Window win, const char *window_name, const char *icon_name, Pixmap icon_pixmap,
                                      char **argv, int argc, XSetWindowAttributes *attributes);

    Atom bridgeXCreateColormap(Display *dpy, Window w, Visual *visual, int alloc);
    int bridgeXFreeColormap(Display *dpy, Colormap colormap);
    void *bridgeXextFindDisplay(Display *dpy);

    // --- Event Handling ---
    int bridgeXPending(Display *dpy);
    void bridgeXNextEvent(Display *dpy, XEvent *event_return);
    void bridgeXSendEvent(Display *dpy, Window w, bool propagate, long event_mask, XEvent *event_send);
    
    int bridgeXCheckTypedEvent(Display *dpy, int type, XEvent *event_return);
    int bridgeXSelectInput(Display *dpy, Window win, long event_mask);
    int bridgeXSetCloseDownMode(Display *dpy, int close_mode);
    int bridgeXSetInputFocus(Display *dpy, Window win, int revert_to, Time time);
    int bridgeXAutoRepeatOff(Display *dpy);

    // --- Input (Mouse/Keyboard) ---
    int bridgeXQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                             int *win_x_return, int *win_y_return, unsigned int *mask_return);
    void bridgeXWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width,
                             unsigned int src_height, int dest_x, int dest_y);
    void bridgeXGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                             Window confine_to, Cursor cursor, Time time);
    void bridgeXUngrabPointer(Display *dpy, Time time);
    int bridgeXGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time);
    int bridgeXUngrabKeyboard(Display *dpy, Time time);
    int bridgeXAutoRepeatOn(Display *dpy);
    int bridgeXKeysymToKeycode(Display *dpy, int keysym);

    // --- Attributes, Properties & WM Hints ---
    int bridgeXGetWindowAttributes(Display *dpy, Window win, XWindowAttributes *window_attributes_return);
    int bridgeXChangeWindowAttributes(Display *dpy, Window w, unsigned long valuemask, XSetWindowAttributes *attributes);
    int bridgeXGetGeometry(Display *dpy, Drawable d, Window *root_return, int *x_return, int *y_return, unsigned int *width_return,
                            unsigned int *height_return, unsigned int *border_width_return, unsigned int *depth_return);

    Atom bridgeXInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist);
    int bridgeXStoreName(Display *dpy, Window win, const char *name);
    int bridgeXChangeProperty(Display *dpy, Window win, Atom property, Atom type, int format, int mode, const unsigned char *data,
                               int nelements);
    int bridgeXParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return);

    void bridgeXMapRaised(Display *dpy, Window w);
    void *bridgeXRRGetScreenResources(Display *dpy, Window window);
    void *bridgeXRRGetCrtcInfo(Display *dpy, int crtc);
    int bridgeXLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return);
    int bridgeXTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return,
                                     int *dest_y_return, Window *child_return);
    // Missing members added:
    int bridgeXSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count);
    int bridgeXSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                                XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints);
    void bridgeXSetWMNormalHints(Display *dpy, Window w, XSizeHints *hints);
    void bridgeXSetTransientForHint(Display *dpy, Window w, Window transient_for);
    int bridgeXStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return);

    // --- Pixmaps & Cursors ---
    Pixmap bridgeXCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                              unsigned long fg, unsigned long bg, unsigned int depth);
    Cursor bridgeXCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color,
                                      unsigned int x, unsigned int y);
    int bridgeXFreePixmap(Display *dpy, Pixmap pixmap);
    int bridgeXFreeCursor(Display *dpy, Cursor cursor);
    int bridgeXDefineCursor(Display *dpy, Window w, Cursor c);
    void bridgeXUndefineCursor(Display *dpy, Window w);
    Cursor bridgeXCreateFontCursor(Display *dpy, int shape);
    Pixmap bridgeXCreateBitmapFromData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height);

    // --- XF86VidMode Extension (Stubs) ---
    int bridgeXF86VidModeQueryExtension(Display *dpy, int *event_base_return, int *error_base_return);
    int bridgeXF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return);
    int bridgeXF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline);
    int bridgeXF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline);
    int bridgeXF86VidModeSetViewPort(Display *dpy, int screen, int x, int y);
    int bridgeXF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return);
    int bridgeXF86VidModeLockModeSwitch(Display *dpy, int screen, int lock);
    int bridgeXF86VidModeQueryVersion(Display *dpy, int *major_return, int *minor_return);

    // --- Misc / Legacy Stubs ---
    int bridgeXInitThreads();
    void bridgeXFree(void *data);
    int bridgeXReply(Display *dpy, int *rep, int size, int expect_reply);

    XWMHints *bridgeXAllocWMHints();
#ifdef __cplusplus
}
#endif
#endif