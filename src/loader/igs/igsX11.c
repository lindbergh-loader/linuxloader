// The IGS games' X display unlocked, and the screen's size (see igs.c):
// apart, Xlib's internal header and MinHook's both define BOOL and INT32.

#include <X11/Xlibint.h>

#include "igs.h"
#include "../graphics/x11Bridge.h"

void igsUnlockDisplay(void *display)
{
    Display *dpy = display;

    dpy->lock_fns = NULL;
    dpy->lock = NULL;
}

int igsScreenSize(int *width, int *height)
{
    Display *dpy = XOpenDisplay(NULL);

    if (!dpy)
        return 0;
    *width = DisplayWidth(dpy, DefaultScreen(dpy));
    *height = DisplayHeight(dpy, DefaultScreen(dpy));
    XCloseDisplay(dpy);
    return *width > 0 && *height > 0;
}

// The Ogre games' window as the game sees it: the cabinet's monitor's size
// when the window manager made it larger (Batocera's openbox makes every
// window full screen). Ogre would follow the window, and Scaleform's menus
// keep their layout of the cabinet's size: the game draws at that size, in
// the window's bottom left, which igs.c stretches to the window at each swap.
static int frameWidth, frameHeight, windowWidth, windowHeight;

static void windowAttributes(unsigned long window, void *attributes)
{
    XWindowAttributes *attr = attributes;

    if (window != attr->root && attr->width >= frameWidth && attr->height >= frameHeight)
    {
        windowWidth = attr->width;
        windowHeight = attr->height;
        attr->width = frameWidth;
        attr->height = frameHeight;
    }
}

void igsWindowAttributesInstall(int width, int height)
{
    frameWidth = width;
    frameHeight = height;
    x11WindowAttributesFilter = windowAttributes;
}

int igsWindowSize(int *width, int *height)
{
    if (windowWidth == frameWidth && windowHeight == frameHeight)
        return 0;
    *width = windowWidth;
    *height = windowHeight;
    return windowWidth > 0;
}
