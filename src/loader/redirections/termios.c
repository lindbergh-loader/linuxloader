#ifdef __linux__
#include <termios.h>
#include <dlfcn.h>
#include <string.h>

#include "../config/config.h"
#include "termios.h"
#include "filesystemShared.h"
#include "../namco/namcoEs1.h"

#define REAL_FUNC(name) dlsym(RTLD_NEXT, #name)

extern DeviceType hooks[5];

static int (*_tcgetattr)(int fd, struct termios *termios_p) = NULL;
int tcgetattr(int fd, struct termios *termios_p)
{
    if (_tcgetattr == NULL)
        _tcgetattr = REAL_FUNC(tcgetattr);

    if (fd == hooks[SERIAL0] && getConfig()->emulateDriveboard == 1)
        return 0;

    if (namcoEs1JvsIsFd(fd) || namcoEs1KickbackIsFd(fd))
    {
        memset(termios_p, 0, sizeof(*termios_p));
        return 0;
    }

    return _tcgetattr(fd, termios_p);
}

static int (*_tcsetattr)(int fd, int optional_actions, const struct termios *termios_p) = NULL;
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    if (_tcsetattr == NULL)
        _tcsetattr = REAL_FUNC(tcsetattr);

    if (fd == hooks[SERIAL0] && getConfig()->emulateDriveboard == 1)
        return 0;

    if (namcoEs1JvsIsFd(fd) || namcoEs1KickbackIsFd(fd))
        return 0;

    return _tcsetattr(fd, optional_actions, termios_p);
}

static speed_t (*_cfgetispeed)(const struct termios *termios_p) = NULL;
speed_t cfgetispeed(const struct termios *termios_p)
{
    if (_cfgetispeed == NULL)
        _cfgetispeed = REAL_FUNC(cfgetispeed);

    if (getConfig()->emulateDriveboard == 1)
        return B9600;

    return _cfgetispeed(termios_p);
}

static speed_t (*_cfgetospeed)(const struct termios *termios_p) = NULL;
speed_t cfgetospeed(const struct termios *termios_p)
{
    if (_cfgetospeed == NULL)
        _cfgetospeed = REAL_FUNC(cfgetospeed);

    if (getConfig()->emulateDriveboard == 1)
        return B9600;

    return _cfgetospeed(termios_p);
}

static int (*_cfsetispeed)(struct termios *termios_p, speed_t speed) = NULL;
int cfsetispeed(struct termios *termios_p, speed_t speed)
{
    if (_cfsetispeed == NULL)
        _cfsetispeed = REAL_FUNC(cfsetispeed);

    if (getConfig()->emulateDriveboard == 1)
        return 0;

    return _cfsetispeed(termios_p, speed);
}

static int (*_cfsetospeed)(struct termios *termios_p, speed_t speed) = NULL;
int cfsetospeed(struct termios *termios_p, speed_t speed)
{
    if (_cfsetospeed == NULL)
        _cfsetospeed = REAL_FUNC(cfsetospeed);

    if (getConfig()->emulateDriveboard == 1)
        return 0;

    return _cfsetospeed(termios_p, speed);
}

#endif