// Cabinet paths of the PC-based arcade systems (Raw Thrills, Namco ES1 and
// N2, Teamplay, Global VR, Roxor), mapped to the host by the running game's system:
// the loader's file interposers (redirections/filesystemShared.c) and the
// libc path calls below.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

#include "gamePath.h"
#include "../rawthrills/rawthrills.h"
#include "../namco/namcoEs1.h"
#include "../namco/namcoN2.h"
#include "../teamplay/teamplay.h"
#include "../globalvr/gvr.h"
#include "../roxor/roxor.h"

const char *gameRedirectPath(const char *path, char *buf, size_t size)
{
    if (!path)
        return path;
    if (isRawThrillsGame())
        return rtRedirectPath(path, buf, size);
    if (isTeamplayGame())
        return teamplayRedirectPath(path, buf, size);
    if (isGvrGame())
        return gvrRedirectPath(path, buf, size);
    if (isRoxorGame())
        return roxorRedirectPath(path, buf, size);
    if (isNamcoEs1Game())
        return namcoEs1RedirectPath(path, buf, size);
    if (isNamcoN2Game())
        return namcoN2RedirectPath(path, buf, size);
    return path;
}

#define REDIRECT(path)                                                                                                \
    char redirectBuf[PATH_MAX];                                                                                       \
    path = gameRedirectPath(path, redirectBuf, sizeof(redirectBuf))

int access(const char *path, int mode)
{
    static int (*real)(const char *, int);
    if (!real)
        real = dlsym(RTLD_NEXT, "access");
    REDIRECT(path);
    return real(path, mode);
}

int rename(const char *from, const char *to)
{
    static int (*real)(const char *, const char *);
    char toBuf[PATH_MAX];
    if (!real)
        real = dlsym(RTLD_NEXT, "rename");
    REDIRECT(from);
    return real(from, gameRedirectPath(to, toBuf, sizeof(toBuf)));
}

int unlink(const char *path)
{
    static int (*real)(const char *);
    if (!real)
        real = dlsym(RTLD_NEXT, "unlink");
    REDIRECT(path);
    return real(path);
}

int chdir(const char *path)
{
    static int (*real)(const char *);
    if (!real)
        real = dlsym(RTLD_NEXT, "chdir");
    REDIRECT(path);
    return real(path);
}

// Old glibc stat entry points: only versioned compat symbols remain.
static void *nextCompat(const char *name)
{
    void *p = dlvsym(RTLD_NEXT, name, "GLIBC_2.0");
    return p ? p : dlsym(RTLD_NEXT, name);
}

int __xstat(int ver, const char *path, struct stat *st)
{
    static int (*real)(int, const char *, struct stat *);
    if (!real)
        real = nextCompat("__xstat");
    REDIRECT(path);
    return real(ver, path, st);
}

int __lxstat(int ver, const char *path, struct stat *st)
{
    static int (*real)(int, const char *, struct stat *);
    if (!real)
        real = nextCompat("__lxstat");
    REDIRECT(path);
    return real(ver, path, st);
}

int __lxstat64(int ver, const char *path, struct stat64 *st)
{
    static int (*real)(int, const char *, struct stat64 *);
    if (!real)
        real = nextCompat("__lxstat64");
    REDIRECT(path);
    return real(ver, path, st);
}
