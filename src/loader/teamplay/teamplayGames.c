// Teamplay game descriptors (see teamplayGame.h).

#include <stddef.h>

#include "teamplayGame.h"
#include "../config/config.h"

// Police Trainer 2 keeps its settings, hiscores, audits and log on the
// cabinet's /var/pt2; the dump has them in audit/.
static const TeamplayRootAlias pt2RootAliases[] = {
    {"/var/pt2", "audit"},
    {NULL, NULL},
};

// Police Trainer 2 takes its clock's rate from the "cpu MHz" of
// /proc/cpuinfo, out of its first 512 bytes split in at most 100 words: a
// recent CPU has more ("Too many args. load failed") and no rate is read.
// A Pentium 4's, with the host's TSC rate (see teamplay.c).
static const char pt2Cpuinfo[] =
    "processor\t: 0\n"
    "vendor_id\t: GenuineIntel\n"
    "cpu family\t: 15\n"
    "model\t\t: 2\n"
    "model name\t: Pentium 4 (Mobile)\n"
    "stepping\t: 9\n"
    "cpu MHz\t\t: %.3f\n"
    "cache size\t: 512 KB\n"
    "fpu\t\t: yes\n"
    "fpu_exception\t: yes\n"
    "cpuid level\t: 2\n"
    "wp\t\t: yes\n"
    "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ht tm pbe\n";

static const TeamplayGame games[] = {
    {
        // Crossfire Maximum Paintball, med_pball (the medium-resolution
        // monitor's build, 512x384; std_pball is the standard one's).
        .crc32 = CROSSFIRE_TEAMPLAY,
        .fileCrc32 = 0xc2aff8d4,
        .rootPath = "/home/pball",
        .dataPath = "/data",
        .kernelVersion = "Linux version 2.4.22 (root@midas) (gcc version 3.2.3) #6 Tue Sep 2 17:43:01 PDT 2003\n",
        .iButtonCheck = 0x0805e7ca,
        .diskSerialCheck = 0x08056ec7,
        .xScale = 0.6946107745f, // 0x3f31d203
        .xOffset = -170.0f,
        .yScale = 1.0f,
        .yOffset = -25.0f,
        .width = 512,
        .height = 384,
    },
    {
        // Police Trainer 2, pt2s_g11. Its own X11/GLX window rather than
        // glut, the MegaJamma board and iButton as on Crossfire; the iButton
        // init (it exits when the chip is missing) passes. Its sound is
        // pt2snd, a daemon it starts with popen("./pt2snd /data/pt2/aud/")
        // and feeds sound codes on the pipe; the daemon plays them on
        // /dev/dsp.
        .crc32 = POLICE_TRAINER_2_TEAMPLAY,
        .fileCrc32 = 0x3dcf6b66,
        .soundDaemonCrc32 = 0xaac0a000, // pt2snd.f7
        .rootPath = "/home/rocky/pt2",
        .dataPath = "/data",
        .rootAliases = pt2RootAliases,
        .cpuinfo = pt2Cpuinfo,
        .cpuRangeCheck = 0x8052821, // 500 <= cpu MHz <= 3000
        .iButtonCheck = 0x808cfa4,
        // The board opened for 360x240 (0x8053919), its 240-line constants
        // (0x8055288); the game adds its gun calibration (settings) to them,
        // and takes a shot past x 340 or y 240 for one off the screen.
        .xScale = 0.5346784592f, // 0x3f08e0b0
        .xOffset = -50.0f,
        .yScale = 1.0f,
        .yOffset = -20.0f,
        .width = 360,
        .height = 240,
    },
};

const TeamplayGame *teamplayGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const TeamplayGame *teamplayGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}

const TeamplayGame *teamplayGetGameBySoundDaemonCrc(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].soundDaemonCrc32 && games[i].soundDaemonCrc32 == crc32)
            return &games[i];
    return NULL;
}
