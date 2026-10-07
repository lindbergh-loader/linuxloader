// The 64-bit games (see game64.h).
#define _GNU_SOURCE
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "game64.h"

static int isElf64(const char *path)
{
    unsigned char ident[EI_NIDENT];
    int fd = open(path, O_RDONLY | O_CLOEXEC), ok;
    if (fd < 0)
        return 0;
    ok = read(fd, ident, sizeof(ident)) == (ssize_t)sizeof(ident) && !memcmp(ident, ELFMAG, SELFMAG) &&
         ident[EI_CLASS] == ELFCLASS64;
    close(fd);
    return ok;
}

// A g7 executable in dir: game2 (the dump), else game.
static int g7In(const char *gameDir, const char *dir, Game64 *g)
{
    static const char *const names[] = {"game2", "game"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s%s%s", gameDir, dir, *dir ? "/" : "", names[i]);
        if (isElf64(path))
        {
            g->kind = GAME64_G7;
            snprintf(g->dir, sizeof(g->dir), "%s", dir);
            snprintf(g->name, sizeof(g->name), "%s", names[i]);
            return 1;
        }
    }
    return 0;
}

// The g7 layouts: the cabinet's pm/g7/<game>, g7/<game> at the root of the
// game's folder (which then stands for /pm), or the game at the root.
static int g7Find(const char *gameDir, Game64 *g)
{
    static const char *const parents[] = {"pm/g7", "g7"};
    for (size_t i = 0; i < sizeof(parents) / sizeof(parents[0]); i++)
    {
        char path[PATH_MAX];
        DIR *d;
        struct dirent *e;
        int found = 0;
        snprintf(path, sizeof(path), "%s/%s", gameDir, parents[i]);
        if (!(d = opendir(path)))
            continue;
        while (!found && (e = readdir(d)))
        {
            char dir[PATH_MAX];
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "dump"))
                continue;
            snprintf(dir, sizeof(dir), "%s/%s", parents[i], e->d_name);
            found = g7In(gameDir, dir, g);
        }
        closedir(d);
        if (found)
            return 1;
    }
    return g7In(gameDir, "", g);
}

// A Unity game: <name>_Data with its globalgamemanagers.
static int unityFind(const char *gameDir, Game64 *g)
{
    DIR *d = opendir(gameDir);
    struct dirent *e;
    int found = 0;
    if (!d)
        return 0;
    while (!found && (e = readdir(d)))
    {
        size_t n = strlen(e->d_name);
        char path[PATH_MAX];
        if (n <= 5 || n - 5 >= sizeof(g->name) || strcmp(e->d_name + n - 5, "_Data"))
            continue;
        snprintf(path, sizeof(path), "%s/%s/globalgamemanagers", gameDir, e->d_name);
        if (access(path, R_OK) != 0)
            continue;
        g->kind = GAME64_UNITY;
        g->dir[0] = 0;
        memcpy(g->name, e->d_name, n - 5);
        g->name[n - 5] = 0;
        found = 1;
    }
    closedir(d);
    return found;
}

Game64 game64Find(const char *gameDir)
{
    Game64 g = {GAME64_NONE, "", ""};
    if (!unityFind(gameDir, &g) && !g7Find(gameDir, &g))
        g.kind = GAME64_NONE;
    return g;
}
