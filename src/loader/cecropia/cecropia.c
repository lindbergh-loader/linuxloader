// Cecropia games (see cecropia.h): their cabinet's paths, its security
// board and the start-up they need.

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cecropia.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../../minhook/include/MinHook.h"
#include "../patching/flowControl.h"

#define SDL12_FULLSCREEN 0x80000000u
#define SDL12_HWSURFACE 0x00000001u
#define SDL12_DOUBLEBUF 0x40000000u

extern uint32_t gId;

// The cabinet's ~/dist/decaf: bin/ (where the game starts) is in it.
static char gameDir[PATH_MAX];

const CecropiaGame *cecropiaCurrentGame(void)
{
    static uint32_t cachedId;
    static const CecropiaGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = cecropiaGetGame(gId);
    }
    return game;
}

int isCecropiaGame(void)
{
    return cecropiaCurrentGame() != NULL;
}

const char *cecropiaRedirectPath(const char *path, char *buf, size_t size)
{
    const CecropiaGame *g = cecropiaCurrentGame();
    const char *name;

    if (!g || !path || !gameDir[0] || strcmp(path, g->statePath) != 0)
        return path;
    name = strrchr(g->statePath, '/') + 1;
    snprintf(buf, size, "%s/%s/%s", gameDir, g->stateDir, name);
    return buf;
}

// The release the game runs from: current (a link to it on the cabinet,
// often a plain file in copies), else the last release* directory.
static int findRelease(char *out, size_t size)
{
    struct dirent **names;
    struct stat st;
    int n, found = 0;

    snprintf(out, size, "%s/current", gameDir);
    if (stat(out, &st) == 0 && S_ISDIR(st.st_mode))
        return 1;
    if ((n = scandir(gameDir, &names, NULL, alphasort)) < 0)
        return 0;
    for (int i = n - 1; i >= 0; i--)
    {
        if (!found && !strncmp(names[i]->d_name, "release", 7))
        {
            snprintf(out, size, "%s/%s", gameDir, names[i]->d_name);
            found = stat(out, &st) == 0 && S_ISDIR(st.st_mode);
        }
        free(names[i]);
    }
    free(names);
    return found;
}

// The GIB board: there, answering its challenges. Its serial port is never
// opened.
__attribute__((force_align_arg_pointer)) static int gibTrue(void)
{
    return 1;
}

// Its flash version, four bytes the game prints.
__attribute__((force_align_arg_pointer)) static int gibGetVersion(unsigned char *version)
{
    version[0] = 1;
    version[1] = version[2] = version[3] = 0;
    return 1;
}

__attribute__((force_align_arg_pointer)) static int gibGetBoardId(char *id)
{
    strcpy(id, "00000000");
    return 1;
}

// The movie's path, made absolute while the game is in its sequence's
// directory: the demuxing thread opens it later, while the main thread may be
// in another directory for a moment (getFullPath and the art packs' loads
// chdir there and back).
static void *(*realMovieCtor)(void *, const char *, int);

__attribute__((force_align_arg_pointer)) static void *movieCtor(void *movie, const char *path, int flag)
{
    char cwd[PATH_MAX], full[PATH_MAX * 2];

    if (path && path[0] != '/' && getcwd(cwd, sizeof(cwd)) &&
        snprintf(full, sizeof(full), "%s/%s", cwd, path) < (int)sizeof(full))
        path = full;
    return realMovieCtor(movie, path, flag);
}

static int (*realSqliteOpen)(const char *, void **);

__attribute__((force_align_arg_pointer)) static int sqliteOpen(const char *path, void **db)
{
    char buf[PATH_MAX];

    return realSqliteOpen(cecropiaRedirectPath(path, buf, sizeof(buf)), db);
}

// The game asks for full screen (-f) or a window of its picture's size: it
// is full screen (sdl12-compat scales its 640x480 to the screen) as the
// loader's config says, or when it asks for a picture larger than the
// game's (Batocera's: the screen's size, not fullscreen).
static void *(*realSetVideoMode)(int, int, int, unsigned int);

__attribute__((force_align_arg_pointer)) static void *setVideoMode(int width, int height, int bpp, unsigned int flags)
{
    int fullscreen = getConfig()->fullscreen || getConfig()->width > width || getConfig()->height > height;

    flags &= ~(SDL12_FULLSCREEN | SDL12_HWSURFACE | SDL12_DOUBLEBUF);
    flags |= fullscreen ? SDL12_FULLSCREEN : SDL12_HWSURFACE | SDL12_DOUBLEBUF;
    return realSetVideoMode(width, height, bpp, flags);
}

int cecropiaInit(void)
{
    const CecropiaGame *g = cecropiaCurrentGame();
    char cwd[PATH_MAX], path[PATH_MAX];

    // Started in bin/: the game directory is its parent.
    if (!getcwd(cwd, sizeof(cwd)) || snprintf(path, sizeof(path), "%s/..", cwd) >= (int)sizeof(path) ||
        !realpath(path, gameDir))
        gameDir[0] = '\0';
    snprintf(path, sizeof(path), "%s/%s", gameDir, g->stateDir);
    mkdir(path, 0755);
    // The game runs from its release's directory (its scripts, movies and
    // config are "./"), as the cabinet's run_decaf_Linux starts it.
    if (!findRelease(path, sizeof(path)) || chdir(path) != 0)
        log_error("The Act: no release directory in %s", gameDir);

    *(void **)&realSqliteOpen = hookExecutableImport("sqlite3_open", (void *)sqliteOpen);
    *(void **)&realSetVideoMode = hookExecutableImport("SDL_SetVideoMode", (void *)setVideoMode);
    if (!realSqliteOpen || !realSetVideoMode)
        log_warn("The Act: sqlite3_open or SDL_SetVideoMode not imported");

    if (MH_CreateHook((void *)(uintptr_t)g->movieCtor, (void *)movieCtor, (void **)&realMovieCtor) != MH_OK)
        log_error("The Act: cannot hook the movies' paths");
    detourFunction(g->gibOpen, gibTrue);
    detourFunction(g->gibClose, gibTrue);
    detourFunction(g->gibReset, gibTrue);
    detourFunction(g->gibChallenge, gibTrue);
    detourFunction(g->gibGetVersion, gibGetVersion);
    detourFunction(g->gibGetBoardId, gibGetBoardId);
    cecropiaInputInstall(g);
    return 0;
}
