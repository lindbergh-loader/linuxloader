#define _CRT_SECURE_NO_WARNINGS

#include "sdlCalls.h"
#include "x11Bridge.h"
#include "../log/log.h"
#include "../config/config.h"

#include <stdlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_stdinc.h>


// Windows API
#ifdef   __linux__
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <X11/extensions/xf86vmode.h> 
#else 
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int bridgeXInitThreads()
{
    return 1;
}

static char s_dummyDisplay[4096] = {0};
static char s_commandBuffer[8192] = {0};
static char s_dummyScreens[4096] = {0};

Display *bridgeXOpenDisplay(const char *name)
{
    log_debug("XOpenDisplay(\"%s\")\n", name ? name : "NULL");
    *(int *)(s_dummyDisplay + 0x84) = 0;                                           // Offset 0x84: Default Screen (int)
    *(void **)(s_dummyDisplay + 0x8C) = s_dummyScreens;                            // Offset 0x8C: Screens pointer
    *(Window *)(s_dummyScreens + 0x08) = 0;                                        // Offset 0x08 : window ID
    *(int *)(s_dummyScreens + 0x0C) = getConfig()->width;                          // Offset 0x0C: Width
    *(int *)(s_dummyScreens + 0x10) = getConfig()->height;                         // Offset 0x10: Height
    *(char **)(s_dummyDisplay + 0x6C) = s_commandBuffer;                           // Offset 0x6C: Current buffer pointer??
    *(char **)(s_dummyDisplay + 0x70) = s_commandBuffer + sizeof(s_commandBuffer); // Offset 0x70: bufmax??
    return (Display *)&s_dummyDisplay;
}

int bridgeXCloseDisplay(Display *dpy)
{
    return 0;
}

Window bridgeXCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                           unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                           XSetWindowAttributes *attributes)
{
    log_debug("XCreateWindow: %ux%u\n", getConfig()->width, getConfig()->height);

    if (!getSDLWindow())
    {
        startSDL();
    }

    return (Window)getSDLWindow();
}

int bridgeXDestroyWindow(Display *dpy, Window win)
{
    return 1;
}

int bridgeXConfigureWindow(Display *dpy, Window w, unsigned long value_mask, int *changes)
{
    log_debug("XConfigureWindow\n");
    return 1;
}

void bridgeXMapWindow(Display *dpy, Window win)
{
    return;
}

int bridgeXReply(Display *dpy, int *rep, int extra, int discard)
{
    if (rep)
    {
        memset(rep, 0, 32);
        ((char *)rep)[0] = 1; // type = X_Reply

        unsigned short *p = (unsigned short *)((char *)rep + 4); // offset 4 and 6 for version??
        p[0] = 2;                                                // Major
        p[1] = 2;                                                // Minor
    }
    return 1;
}

int bridgeXMoveWindow(Display *dpy, Window win, int x, int y)
{
    return 1;
}

int bridgeXResizeWindow(Display *dpy, Window win, unsigned int width, unsigned int height)
{
    return 1;
}

int bridgeXSync(Display *dpy, int discard)
{
    return 1;
}

int bridgeXFlush(Display *dpy)
{
    return 1;
}

void bridgeXLockDisplay(Display *dpy)
{
}

void bridgeXUnlockDisplay(Display *dpy)
{
}

void bridgeXSetErrorHandler(void *handler)
{
}

int bridgeXGetErrorText(Display *dpy, int code, char *buffer, int length)
{
    if (buffer && length > 0)
        buffer[0] = '\0';
    return 0;
}

int bridgeXDisplayWidth(void *dpy, int screen_number)
{
    return getConfig()->width;
}

int bridgeXDisplayHeight(void *dpy, int screen_number)
{
    return getConfig()->height;
}

int bridgeXDisplayWidthMM(void *dpy, int screen_number)
{
    return (int)((getConfig()->width * 25.4f) / 96.0f);
}

int bridgeXDisplayHeightMM(void *dpy, int screen_number)
{
    return (int)((getConfig()->height * 25.4f) / 96.0f);
}

Atom bridgeXInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist)
{
    return 1;
}

static int s_dummyExtCodes[2] = {0, 128};
static void *s_dummyExtData[4] = {(void *)1, (void *)1, (void *)s_dummyExtCodes, (void *)1};
void *bridgeXextFindDisplay(Display *dpy)
{
    return s_dummyExtData;
}

int bridgeXPending(Display *dpy)
{
    return 0;
}

void bridgeXNextEvent(Display *dpy, XEvent *event_return)
{
    return;
}

void bridgeXSendEvent(Display *dpy, Window w, bool propagate, long event_mask, XEvent *event_send)
{
    return;
}

XWMHints *bridgeXAllocWMHints()
{
    return (XWMHints *)calloc(1, sizeof(XWMHints));
}

int bridgeXCheckTypedEvent(Display *dpy, int type, XEvent *event_return)
{
    return 0;
}

int bridgeXSelectInput(Display *dpy, Window win, long event_mask)
{
    return 0;
}
int bridgeXSetCloseDownMode(Display *dpy, int close_mode)
{
    return 1;
}

int bridgeXSetInputFocus(Display *dpy, Window win, int revert_to, Time time)
{
    return 1;
}

int bridgeXAutoRepeatOff(Display *dpy)
{
    return 1;
}

int bridgeXKeysymToKeycode(Display *dpy, int keysym)
{
    return 0;
}

int bridgeXTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                                Window *child_return)
{
    SDL_Window *srcWin = getSDLWindow();
    SDL_Window *destWin = getSDLWindow();

    int srcWinX = 0;
    int srcWinY = 0;
    int destWinX = 0;
    int destWinY = 0;

    if (srcWin)
        SDL_GetWindowPosition(srcWin, &srcWinX, &srcWinY);
    if (destWin)
        SDL_GetWindowPosition(destWin, &destWinX, &destWinY);

    int globalX = src_x + srcWinX;
    int globalY = src_y + srcWinY;

    if (dest_x_return)
        *dest_x_return = globalX - destWinX;
    if (dest_y_return)
        *dest_y_return = globalY - destWinY;
    if (child_return)
        *child_return = 0;
    return 1;
}

int bridgeXQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                        int *win_x_return, int *win_y_return, unsigned int *mask_return)
{
    return 0;
}

void bridgeXWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height,
                        int dest_x, int dest_y)
{
}

void bridgeXGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                        Window confine_to, Cursor cursor, Time time)
{
}

void bridgeXUngrabPointer(Display *dpy, Time time)
{
}

int bridgeXGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time)
{
    return 0;
}

int bridgeXUngrabKeyboard(Display *dpy, Time time)
{
    return 0;
}

int bridgeXAutoRepeatOn(Display *dpy)
{
    return 1;
}

int bridgeXGetWindowAttributes(Display *dpy, Window win, XWindowAttributes *attr)
{
    if (!attr)
        return 0;
    memset(attr, 0, sizeof(XWindowAttributes));
    attr->x = 0;
    attr->y = 0;
    attr->width = getConfig()->width;
    attr->height = getConfig()->height;
    attr->depth = 24;
    attr->visual = (void *)1;
    attr->map_state = 2;
    return 1;
}

int bridgeXChangeWindowAttributes(Display *dpy, Window w, unsigned long valuemask, XSetWindowAttributes *attributes)
{
    return 1;
}

int bridgeXGetGeometry(Display *dpy, Drawable d, Window *root_return, int *x_return, int *y_return, unsigned int *width_return,
                       unsigned int *height_return, unsigned int *border_width_return, unsigned int *depth_return)
{
    if (root_return)
        *root_return = 0;
    if (x_return)
        *x_return = 0;
    if (y_return)
        *y_return = 0;
    if (width_return)
        *width_return = getConfig()->width;
    if (height_return)
        *height_return = getConfig()->height;
    if (depth_return)
        *depth_return = 24;
    if (border_width_return)
        *border_width_return = 0;
    return 1;
}

int bridgeXSetStandardProperties(Display *dpy, Window win, const char *window_name, const char *icon_name, Pixmap icon_pixmap, char **argv,
                                 int argc, XSetWindowAttributes *attributes)
{
    return 1;
}

int bridgeXChangeProperty(Display *dpy, Window win, Atom property, Atom type, int format, int mode, const unsigned char *data,
                          int nelements)
{
    return 1;
}

int bridgeXParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return)
{
    return 1;
}

void bridgeXMapRaised(Display *dpy, Window w)
{
    SDL_Window *sdlWin = getSDLWindow();
    if (sdlWin)
        SDL_RaiseWindow(sdlWin);
}

static char s_dummyScreenResources[1024] = {0};
static char s_dummyCrtcs[1024] = {0};

void *bridgeXRRGetScreenResources(Display *dpy, Window window)
{
    *(void **)(s_dummyScreenResources + 0x0C) = s_dummyCrtcs;
    return s_dummyScreenResources;
}

static char s_dummyCrtcInfo[1024] = {0};

void *bridgeXRRGetCrtcInfo(Display *dpy, int crtc)
{
    *(unsigned int *)(s_dummyCrtcInfo + 0x0C) = getConfig()->width;
    *(unsigned int *)(s_dummyCrtcInfo + 0x10) = getConfig()->height;
    return s_dummyCrtcInfo;
}

int bridgeXLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return)
{
    return 0;
}

int bridgeXStoreName(Display *dpy, Window win, const char *name)
{
    SDL_Window *sdlWin = getSDLWindow();
    if (sdlWin && name)
        SDL_SetWindowTitle(sdlWin, name);
    return 0;
}

int bridgeXSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count)
{
    return 1;
}

int bridgeXSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                           XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return 1;
}

void bridgeXSetWMNormalHints(Display *dpy, Window w, XSizeHints *hints)
{
}

void bridgeXSetTransientForHint(Display *dpy, Window w, Window transient_for)
{
}

int bridgeXStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return)
{
    if (text_prop_return && count > 0 && list && list[0])
    {
        size_t len = strlen(list[0]);
        text_prop_return->value = (unsigned char *)malloc(len + 1);
        if (text_prop_return->value)
            strcpy((char *)text_prop_return->value, list[0]);
        text_prop_return->encoding = 31;
        text_prop_return->format = 8;
        text_prop_return->nitems = len;
        return 1;
    }
    return 0;
}

void bridgeXFree(void *data)
{
    if (data)
        free(data);
}

Pixmap bridgeXCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                         unsigned long fg, unsigned long bg, unsigned int depth)
{
    return 2001;
}

Cursor bridgeXCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color, unsigned int x,
                                 unsigned int y)
{
    return 1001;
}

int bridgeXFreePixmap(Display *dpy, Pixmap pixmap)
{
    return 1;
}

int bridgeXFreeCursor(Display *dpy, Cursor cursor)
{
    return 1;
}

int bridgeXDefineCursor(Display *dpy, Window w, Cursor c)
{
    // if (getConfig()->crc32 == QUIZ_AXA_SBMS || getConfig()->crc32 == QUIZ_AXA_SBUR_LIVE)
    return 0;
}

void bridgeXUndefineCursor(Display *dpy, Window w)
{
}

Cursor bridgeXCreateFontCursor(Display *dpy, int shape)
{
    return 1001;
}

Pixmap bridgeXCreateBitmapFromData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height)
{
    return 2002;
}

int bridgeXF86VidModeQueryExtension(Display *dpy, int *event_base_return, int *error_base_return)
{
    if (event_base_return)
        *event_base_return = 0;
    if (error_base_return)
        *error_base_return = 0;
    return 1;
}

int bridgeXF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return)
{
    if (modecount_return)
        *modecount_return = 1;

    if (modesinfo_return)
    {
        XF86VidModeModeInfo **modes = (XF86VidModeModeInfo **)malloc(sizeof(XF86VidModeModeInfo *));

        modes[0] = (XF86VidModeModeInfo *)malloc(sizeof(XF86VidModeModeInfo));
        memset(modes[0], 0, sizeof(XF86VidModeModeInfo));

        modes[0]->hdisplay = (unsigned short)getConfig()->width;
        modes[0]->vdisplay = (unsigned short)getConfig()->height;
        modes[0]->dotclock = 60000; // Fake 60Hz dotclock
        modes[0]->htotal = modes[0]->hdisplay + 20;
        modes[0]->vtotal = modes[0]->vdisplay + 10;

        *modesinfo_return = modes;
    }
    return 1;
}

int bridgeXF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline)
{
    if (dotclock_return)
        *dotclock_return = 0;
    return 1;
}

int bridgeXF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline)
{
    return 1;
}

int bridgeXF86VidModeSetViewPort(Display *dpy, int screen, int x, int y)
{
    return 1;
}

int bridgeXF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return)
{
    if (x_return)
        *x_return = 0;
    if (y_return)
        *y_return = 0;
    return 1;
}

int bridgeXF86VidModeLockModeSwitch(Display *dpy, int screen, int lock)
{
    return 1;
}

int bridgeXF86VidModeQueryVersion(Display *dpy, int *major_return, int *minor_return)
{
    if (major_return)
        *major_return = 0;
    if (minor_return)
        *minor_return = 0;
    return 1;
}

Atom bridgeXCreateColormap(Display *dpy, Window w, Visual *visual, int alloc)
{
    return 1;
}

int bridgeXFreeColormap(Display *dpy, Colormap colormap)
{
    return 1;
}
#endif

//////////////////////////////////////////////////////////////////////

#ifdef __linux__
extern Display *x11Display;
extern Window x11Window;
extern bool gettingGPUVendor;
extern bool creatingWindow;

Window window;

/**
 * Stop the house of the dead games turning keyboard repeating off.
 */
int XAutoRepeatOff(Display *display)
{
    return 0;
}

Display *XOpenDisplay(const char *display_name)
{
    Display *(*_XOpenDisplay)(const char *display_name) = dlsym(RTLD_NEXT, "XOpenDisplay");

    if (gettingGPUVendor || creatingWindow)
    {
        return _XOpenDisplay(display_name);
    }
    else
    {
        startSDL();
        return x11Display;
    }
}

Window XCreateWindow(Display *display, Window parent, int x, int y, unsigned int width, unsigned int height, unsigned int border_width,
                     int depth, unsigned int class, Visual *visual, unsigned long valueMask, XSetWindowAttributes *attributes)
{
    Window (*_XCreateWindow)(Display *display, Window parent, int x, int y, unsigned int width, unsigned int height,
                             unsigned int border_width, int depth, unsigned int class, Visual *visual, unsigned long valueMask,
                             XSetWindowAttributes *attributes) = dlsym(RTLD_NEXT, "XCreateWindow");

    if ((gettingGPUVendor || creatingWindow))
        window = _XCreateWindow(display, parent, x, y, width, height, border_width, depth, class, visual, valueMask, attributes);
    else
        window = x11Window;

    return window;
}

// Here we prevent the games to change the window properties
void XSetWMProperties(Display *display, Window w, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                      XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return;
}

int XMapWindow(Display *display, Window window)
{
    return 0;
}

int XPending(Display *display)
{
    return 0;
}

int XGrabPointer(Display *display, Window grab_window, Bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                 Window confine_to, Cursor cursor, Time time)
{
    return 0;
}

int XSetInputFocus(Display *display, Window window, int revert_to_window, Time time)
{
    return 0;
}

Bool XTranslateCoordinates(Display *display, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                           Window *child_return)
{
    return true;
}

int XMoveWindow(Display * display, Window w, int x, int y)
{
    return 0;
}

Bool XF86VidModeSwitchToMode(Display *display, int screen, XF86VidModeModeInfo *modesinfo)
{
    return 0;
}

Bool XF86VidModeGetViewPort(Display *display, int screen, int *x_return, int *y_return)
{
    return 0;
}

Bool XF86VidModeGetModeLine(Display *display, int screen, int *dotclock_return, XF86VidModeModeLine *modeline)
{
    return 0;
}

int XF86VidModeGetAllModeLines(Display *display, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo)
{
    int (*_XF86VidModeGetAllModeLines)(Display *display, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo) =
        dlsym(RTLD_NEXT, "XF86VidModeGetAllModeLines");

    if (_XF86VidModeGetAllModeLines(display, screen, modecount_return, modesinfo) != 1)
    {
        printf("Error: Could not get list of screen modes.\n");
        exit(1);
    }
    else
    {
        XF86VidModeModeInfo **modes = *modesinfo;
        modes[0]->hdisplay = getConfig()->width;
        modes[0]->vdisplay = getConfig()->height;
    }
    return true;
}

void glGenFencesNV(int n, uint32_t *fences)
{
    static unsigned int curf = 1;
    while (n--)
    {
        *fences++ = curf++;
    }
    return;
}

void glDeleteFencesNV(int a, const uint32_t *b)
{
    return;
}


#endif

/*
#ifdef __cplusplus
extern "C"
{
#endif

int XInitThreads()
{
    return bridgeXInitThreads();  
}

Display *XOpenDisplay(const char *name)
{
    return bridgeXOpenDisplay(name);    
}

int XCloseDisplay(Display *dpy)
{
    return bridgeXCloseDisplay(dpy);
}

Window XCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                   unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                   XSetWindowAttributes *attributes)
{
    return bridgeXCreateWindow(dpy, parent, x, y, width, height, border_width, depth, class_type, visual, valuemask, attributes);
}

int XDestroyWindow(Display *dpy, Window win)
{
    return bridgeXDestroyWindow(dpy, win);
}

void XMapWindow(Display *dpy, Window win)
{
    bridgeXMapWindow(dpy, win);
}

int _XReply(Display *dpy, int *rep, int extra, int discard)
{
    return bridgeXReply(dpy, rep, extra, discard);
}

int XMoveWindow(Display *dpy, Window win, int x, int y)
{
    return bridgeXMoveWindow(dpy, win, x, y);
}

int XResizeWindow(Display *dpy, Window win, unsigned int width, unsigned int height)
{
    return bridgeXResizeWindow(dpy, win, width, height);
}

int XSync(Display *dpy, int discard)
{
    return bridgeXSync(dpy, discard);
}

int XFlush(Display *dpy)
{
    return bridgeXFlush(dpy);
}

    void XLockDisplay(Display *dpy)
{
    bridgeXLockDisplay(dpy);
}

void XUnlockDisplay(Display *dpy)
{
    bridgeXUnlockDisplay(dpy);
}

void XSetErrorHandler(void *handler)
{
    bridgeXSetErrorHandler(handler);
}

int XGetErrorText(Display *dpy, int code, char *buffer, int length)
{
    return bridgeXGetErrorText(dpy, code, buffer, length);    
}

int XDisplayWidth(void *dpy, int screen_number)
{
    return bridgeXDisplayWidth(dpy, screen_number);
}

int XDisplayHeight(void *dpy, int screen_number)
{
    return bridgeXDisplayHeight(dpy, screen_number);
}

int XDisplayWidthMM(void *dpy, int screen_number)
{
    return bridgeXDisplayWidthMM(dpy, screen_number);
}

int XDisplayHeightMM(void *dpy, int screen_number)
{
    return bridgeXDisplayHeightMM(dpy, screen_number);
}

Atom XInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist)
{
    return bridgeXInternAtom(dpy, atom_name, create_if_not_exist);
}

void *XextFindDisplay(Display *dpy)
{
    return bridgeXextFindDisplay(dpy);
}

int XPending(Display *dpy)
{
    return bridgeXPending(dpy);
}

void XNextEvent(Display *dpy, XEvent *event_return)
{
    bridgeXNextEvent(dpy, event_return);
}

void XSendEvent(Display *dpy, Window w, bool propagate, long event_mask, XEvent *event_send)
{
    bridgeXSendEvent(dpy, w, propagate, event_mask, event_send);
}

int XCheckTypedEvent(Display *dpy, int type, XEvent *event_return)
{
    return bridgeXCheckTypedEvent(dpy, type, event_return);
}

int XSelectInput(Display *dpy, Window win, long event_mask)
{
    return bridgeXSelectInput(dpy, win, event_mask);
}

int XSetCloseDownMode(Display *dpy, int close_mode)
{
    return bridgeXSetCloseDownMode(dpy, close_mode);
}

int XSetInputFocus(Display *dpy, Window win, int revert_to, Time time)
{
    return bridgeXSetInputFocus(dpy, win, revert_to, time);
}

int XAutoRepeatOff(Display *dpy)
{
    return bridgeXAutoRepeatOff(dpy);
}

int XKeysymToKeycode(Display *dpy, int keysym)
{
    return bridgeXKeysymToKeycode(dpy, keysym);
}

int XTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                                Window *child_return)
{
    return bridgeXTranslateCoordinates(dpy, src_w, dest_w, src_x, src_y, dest_x_return, dest_y_return, child_return);
}

int XQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                        int *win_x_return, int *win_y_return, unsigned int *mask_return)
{
    return bridgeXQueryPointer(dpy, win, root_return, child_return, root_x_return, root_y_return, win_x_return, win_y_return, mask_return);
}

void XWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height,
                        int dest_x, int dest_y)
{
    bridgeXWarpPointer(dpy, src_w, dest_w, src_x, src_y, src_width, src_height, dest_x, dest_y);
}

void XGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                        Window confine_to, Cursor cursor, Time time)
{
    bridgeXGrabPointer(dpy, win, owner_events, event_mask, pointer_mode, keyboard_mode, confine_to, cursor, time);
}

void XUngrabPointer(Display *dpy, Time time)
{
    bridgeXUngrabPointer(dpy, time);
}

int XGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time)
{
    return bridgeXGrabKeyboard(dpy, win, owner_events, pointer_mode, keyboard_mode, time);
}

int XUngrabKeyboard(Display *dpy, Time time)
{
    return bridgeXUngrabKeyboard(dpy, time);
}

int XAutoRepeatOn(Display *dpy)
{
    return bridgeXAutoRepeatOn(dpy);
}

int XGetWindowAttributes(Display *dpy, Window win, XWindowAttributes *attr)
{
    return bridgeXGetWindowAttributes(dpy, win, attr);
}

int XGetGeometry(Display *dpy, Drawable d, Window *root_return, int *x_return, int *y_return, unsigned int *width_return,
                       unsigned int *height_return, unsigned int *border_width_return, unsigned int *depth_return)
{
    return bridgeXGetGeometry(dpy, d, root_return, x_return, y_return, width_return, height_return, border_width_return, depth_return);
}

int XSetStandardProperties(Display *dpy, Window win, const char *window_name, const char *icon_name, Pixmap icon_pixmap, char **argv,
                                 int argc, XSetWindowAttributes *attributes)
{
    return bridgeXSetStandardProperties(dpy, win, window_name, icon_name, icon_pixmap, argv, argc, attributes);
}

int XChangeProperty(Display *dpy, Window win, Atom property, Atom type, int format, int mode, const unsigned char *data,
                          int nelements)
{
    return bridgeXChangeProperty(dpy, win, property, type, format, mode, data, nelements);
}

int XParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return)
{
    return bridgeXParseColor(dpy, colormap, spec, color_return);
}

void XMapRaised(Display *dpy, Window w)
{
    bridgeXMapRaised(dpy, w);
}

void *XRRGetScreenResources(Display *dpy, Window window)
{
    return bridgeXRRGetScreenResources(dpy, window);
}


void *XRRGetCrtcInfo(Display *dpy, int crtc)
{
    return bridgeXRRGetCrtcInfo(dpy, crtc);
}

int XLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return)
{
    return bridgeXLookupString(key_event, buffer_return, buffer_size, keysym_return, status_return);
}

int XStoreName(Display *dpy, Window win, const char *name)
{
    return bridgeXStoreName(dpy, win, name);
}

int XSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count)
{
    return bridgeXSetWMProtocols(dpy, win, protocols, count);
}

int XSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                           XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return bridgeXSetWMProperties(dpy, win, window_name, icon_name, argv, argc, normal_hints, wm_hints, class_hints);
}

void XSetWMNormalHints(Display *dpy, Window w, XSizeHints *hints)
{
    bridgeXSetWMNormalHints(dpy, w, hints);
}

void XSetTransientForHint(Display *dpy, Window w, Window transient_for)
{
    bridgeXSetTransientForHint(dpy, w, transient_for);
}

int XStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return)
{
    return bridgeXStringListToTextProperty(list, count, text_prop_return);
}

void XFree(void *data)
{
    bridgeXFree(data);
}

Pixmap XCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                         unsigned long fg, unsigned long bg, unsigned int depth)
{
    return bridgeXCreatePixmapFromBitmapData(dpy, drawable, data, width, height, fg, bg, depth);
}

Cursor XCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color, unsigned int x,
                                 unsigned int y)
{
    return bridgeXCreatePixmapCursor(dpy, source, mask, foreground_color, background_color, x, y);
}

int XFreePixmap(Display *dpy, Pixmap pixmap)
{
    return bridgeXFreePixmap(dpy, pixmap);
}

int XFreeCursor(Display *dpy, Cursor cursor)
{
    return bridgeXFreeCursor(dpy, cursor);
}

int XDefineCursor(Display *dpy, Window w, Cursor c)
{
    return bridgeXDefineCursor(dpy, w, c);
}

Cursor XCreateFontCursor(Display *dpy, int shape)
{
    return bridgeXCreateFontCursor(dpy, shape);
}

Pixmap XCreateBitmapFromData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height)
{
    return bridgeXCreateBitmapFromData(dpy, drawable, data, width, height);
}

int XF86VidModeQueryExtension(Display *dpy, int *event_base_return, int *error_base_return)
{
    return bridgeXF86VidModeQueryExtension(dpy, event_base_return, error_base_return);
}

int XF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return)
{
    return bridgeXF86VidModeGetAllModeLines(dpy, screen, modecount_return, modesinfo_return);
}

int XF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline)
{
    return bridgeXF86VidModeGetModeLine(dpy, screen, dotclock_return, modeline);
}

int XF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline)
{
    return bridgeXF86VidModeSwitchToMode(dpy, screen, modeline);
}

int XF86VidModeSetViewPort(Display *dpy, int screen, int x, int y)
{
    return bridgeXF86VidModeSetViewPort(dpy, screen, x, y);
}

int XF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return)
{
    return bridgeXF86VidModeGetViewPort(dpy, screen, x_return, y_return);
}

int XF86VidModeLockModeSwitch(Display *dpy, int screen, int lock)
{
    return bridgeXF86VidModeLockModeSwitch(dpy, screen, lock);
}

int XF86VidModeQueryVersion(Display *dpy, int *major_return, int *minor_return)
{
    return bridgeXF86VidModeQueryVersion(dpy, major_return, minor_return);
}

Atom XCreateColormap(Display *dpy, Window w, Visual *visual, int alloc)
{
    return bridgeXCreateColormap(dpy, w, visual, alloc);
}

#ifdef __cplusplus
}
#endif

*/