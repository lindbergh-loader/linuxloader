// The Namco N2 cabinet's shell commands (system()). None is run: the cabinet
// runs as root with sudo and has its own disks, so the ones the game needs
// are answered here, as the Pacloader fork does (n2SystemCommand.cpp and
// n2Wmmt3.cpp); its /tmp is the game directory's tmp/ (namcoN2RedirectPath).

#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "namcoN2.h"
#include "namcoEs1.h"
#include "../log/log.h"

static int startsWith(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// A cabinet path (/tmp/... or relative) in the game directory.
static const char *local(const char *path, char *buf, size_t size)
{
    return namcoN2RedirectPath(path, buf, size);
}

static void makeDirs(const char *path)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/')
        {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    mkdir(buf, 0755);
}

static int copyFile(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out;
    char buf[65536];
    size_t n;

    if (!in)
        return -1;
    if (!(out = fopen(to, "wb")))
    {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

static int endsWith(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static int compareNames(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// "find <dir> ... >/tmp/find.txt": the directory's files of one kind, one
// per line ("<dir>/<name>"), sorted.
static int findFiles(const char *command)
{
    static const struct
    {
        const char *prefix, *directory, *extension;
    } finds[] = {
        {"find /tmp/data/target/", "/tmp/data/target", ".target.gz"},
        {"find data/target/jp", "data/target/jp", ".target.gz"},
        {"find data/target/us", "data/target/us", ".target.gz"},
        {"find /tmp/data/ranking/", "/tmp/data/ranking", ".rank"},
        {"find /tmp/data/maxicoin/", "/tmp/data/maxicoin", ".maxicoin"},
        {"find /tmp/data/joinstar/", "/tmp/data/joinstar", ".joinstar"},
    };
    char dirBuf[PATH_MAX], outBuf[PATH_MAX];

    for (size_t i = 0; i < sizeof(finds) / sizeof(finds[0]); i++)
    {
        if (!startsWith(command, finds[i].prefix))
            continue;
        char *names[4096];
        int count = 0;
        DIR *dir = opendir(local(finds[i].directory, dirBuf, sizeof(dirBuf)));
        struct dirent *e;
        while (dir && (e = readdir(dir)) && count < 4096)
            if (e->d_type == DT_REG && endsWith(e->d_name, finds[i].extension))
                names[count++] = strdup(e->d_name);
        if (dir)
            closedir(dir);
        qsort(names, count, sizeof(names[0]), compareNames);
        FILE *out = fopen(local("/tmp/find.txt", outBuf, sizeof(outBuf)), "w");
        for (int j = 0; j < count; j++)
        {
            if (out)
                fprintf(out, "%s/%s\n", finds[i].directory, names[j]);
            free(names[j]);
        }
        if (!out)
            return 1;
        fclose(out);
        return 0;
    }
    return -1;
}

// "cp -f data/target/*.target.gz /tmp/data/target/".
static int copyTargets(void)
{
    char dirBuf[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    const char *target = local("/tmp/data/target", dirBuf, sizeof(dirBuf));
    DIR *dir = opendir("data/target");
    struct dirent *e;

    makeDirs(target);
    while (dir && (e = readdir(dir)))
        if (e->d_type == DT_REG && endsWith(e->d_name, ".target.gz"))
        {
            snprintf(from, sizeof(from), "data/target/%s", e->d_name);
            snprintf(to, sizeof(to), "%s/%s", target, e->d_name);
            copyFile(from, to);
        }
    if (dir)
        closedir(dir);
    return 0;
}

// "perl prepend-n2.pl": the cabinet's start-up script mounts the work disk
// and lays out /tmp; here only the layout.
static int prepend(void)
{
    static const char *const dirs[] = {
        "/tmp/data/target", "/tmp/data/target/old", "/tmp/data/tournament", "/tmp/data/ranking",
        "/tmp/data/maxicoin", "/tmp/data/joinstar", "/tmp/data/card", "/tmp/data/etc", "/tmp/data2/tournament",
    };
    char buf[PATH_MAX];

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        makeDirs(local(dirs[i], buf, sizeof(buf)));
    copyFile("data/sound/bgm/maxi3/sys_04.wav", local("/tmp/sys_04.wav", buf, sizeof(buf)));
    copyFile("data/sprite/Full_white.png", local("/tmp/joinshot.png", buf, sizeof(buf)));
    log_info("Namco N2: the work disk's directories laid out in %s/tmp", namcoN2GameDir());
    return 0;
}

// "... > file": the file, in the game directory.
static FILE *redirected(const char *command)
{
    char path[PATH_MAX], buf[PATH_MAX];
    const char *r = strrchr(command, '>');
    size_t n;

    if (!r)
        return NULL;
    for (r++; *r == ' '; r++)
        ;
    n = strcspn(r, " \t\n");
    if (n == 0 || n >= sizeof(path))
        return NULL;
    memcpy(path, r, n);
    path[n] = '\0';
    return fopen(local(path, buf, sizeof(buf)), "w");
}

// "sudo perl etc/usbsize.pl > file": the work disk's size in 1K blocks (the
// second column of busybox df), the game directory's volume.
static int workDiskSize(const char *command)
{
    struct statvfs fs;
    unsigned long long kb = 0;
    FILE *out = redirected(command);

    if (statvfs(".", &fs) == 0)
        kb = (unsigned long long)fs.f_blocks * fs.f_frsize / 1024;
    if (!out)
        return 1;
    fprintf(out, "%llu\n", kb);
    fclose(out);
    return 0;
}

static int privileged(const char *command)
{
    const char *p = command + 5; // after "sudo "
    char buf[PATH_MAX];

    if (startsWith(p, "busybox "))
        p += 8;
    if (startsWith(p, "date ") || startsWith(p, "hwclock") || startsWith(p, "umount ") ||
        startsWith(p, "route ") || startsWith(p, "sh -c 'echo"))
        return 0;
    if (startsWith(p, "ifconfig "))
    {
        namcoEs1NetworkCommand(command);
        return 0;
    }
    if (startsWith(p, "perl etc/usbsize.pl"))
        return workDiskSize(command);
    if (startsWith(p, "mkdir -p "))
    {
        makeDirs(local(p + 9, buf, sizeof(buf)));
        return 0;
    }
    // No USB medium is emulated: mounting it, and copying to or from it, fail.
    if (startsWith(p, "mount ") || startsWith(p, "cp ") || startsWith(p, "rm "))
        return 1;
    log_warn("Namco N2: not run: %s", command);
    return 1;
}

int namcoN2Command(const char *command, int *status)
{
    int r;

    if (!isNamcoN2Game() || !command)
        return 0;
    if (startsWith(command, "perl prepend-n2.pl"))
        r = prepend();
    else if (startsWith(command, "find ") && strstr(command, "/tmp/find.txt") && (r = findFiles(command)) >= 0)
        ;
    else if (startsWith(command, "cp -f data/target/*.target.gz /tmp/data/target/"))
        r = copyTargets();
    else if (startsWith(command, "perl etc/ifconfig.pl") && strchr(command, '>'))
    {
        namcoEs1NetworkCommand(command);
        r = 0;
    }
    else if (startsWith(command, "stty "))
        r = 0;
    else if (startsWith(command, "sudo "))
        r = privileged(command);
    else
    {
        log_warn("Namco N2: not run: %s", command);
        r = 0;
    }
    if (getenv("NAMCO_N2_TRACE"))
        fprintf(stderr, "Namco N2: system(\"%s\") -> %d\n", command, r);
    *status = r << 8; // as system() reports an exit status
    return 1;
}
