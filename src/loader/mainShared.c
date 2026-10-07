#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__

#include <ctype.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __linux__
#include <unistd.h>
#endif

#include "config/configIni.h"
#include "input/evdevInput.h"
#include "input/controlIniGen.h"
#include "input/sdlInput.h"
#include "log/log.h"
#include "mainShared.h"
#include "version.h"

#define LD_LIBRARY_PATH "LD_LIBRARY_PATH"
#define LD_PRELOAD "LD_PRELOAD"
#define PRELOAD_FILE_NAME "linuxloader.so"
#define PRELOAD_OPENAL "libopenal.so.0"
#define TEAM "bobbydilley, retrofan, dkeruza-neo"
#define COLLABORATORS "francesco, doozer, rolel, caviar-X, Tovarichtch, dmanlfc, lagswitch, Murray"
#define SPECIAL_THANKS "zExor (Extensive tests)"
#define LINUX_LOADER_CONFIG_PATH "LINUX_LOADER_CONFIG_PATH"
#define LINUX_LOADER_CONTROLS_PATH "LINUX_LOADER_CONTROLS_PATH"
#define LINUX_LOADER_CONTROLS_DB_PATH "LINUX_LOADER_CONTROLS_DB_PATH"
#define LINUX_LOADER_CURRENT_DIR "LINUX_LOADER_CURRENT_DIR"
#define DEFAULT_CONFIG_FILE "linuxloader.ini"
#define DEFAULT_CONTROLS_FILE "controls.ini"
#define DEFAULT_CONTROLS_DB_FILE "gamecontrollerdb.txt"

uint32_t elfCrc = 0;
char *controlsPath = NULL;
char *configPath = NULL;

// List of all linuxloader executables known, not including the test executables
char *games[] = {"a.elf",
                 "abc",
                 "apacheM.elf",
                 "chopperM.elf",
                 "drive.elf",
                 "dsr",
                 "gsevo",
                 "hod4M.elf",
                 "hodexRI.elf",
                 "hummer_Master.elf",
                 "id4.elf",
                 "id5.elf",
                 "Jennifer",
                 "lgj_final",
                 "lgjsp_app",
                 "main.exe",
                 "mj4",
                 "q2satl_lind",
                 "ramboM.elf",
                 "vf5",
                 "vsg",
                 "vt3",
                 "vt3_Lindbergh",
                 "END"};

/**
 * An array containin clean games elf's CRC32
 */
uint32_t cleanElfCRC32[] = {
    0x51C4D2F6, // DVP-0003A | hod4M.elf
    0x1348BCA8, // DVP-0003A | hod4testM.elf
    0x0AAE384E, // DVP-0003B | hod4M.elf
    0x352AA797, // DVP-0003B | hod4testM.elf
    0x42EED61A, // DVP-0003C | hod4M.elf
    0x6DA6E511, // DVP-0003C | hod4testM.elf
    0x0E4BF4B1, // DVP-0005  | vt3_Lindbergh
    0x9E48AB5B, // DVP-0005  | vt3_testmode
    0xE4C64D01, // DVP-0005A | vt3_Lindbergh
    0x9C0E77E5, // DVP-0005A | vt3_testmode
    0xA4BDB9E2, // DVP-0005B | vt3_Lindbergh
    0x74E25472, // DVP-0005B | vt3_testmode
    0x987AE3FF, // DVP-0005C | vt3_Lindbergh
    0x1E4271A4, // DVP-0005C | vt3_testmode
    0xD409B70C, // DVP-0008  | vf5
    0x08EBC0DB, // DVP-0008A | vf5
    0xA47FBA2D, // DVP-0008B | vf5
    0x8CA46167, // DVP-0008C | vf5
    0x75946796, // DVP-0008E | vf5
    0x2C8F5D57, // DVP-0009  | abc
    0x13D90755, // DVP-0009A | abc
    0x633AD6FB, // DVP-0009B | abc
    0xD39825A8, // DVP-0010  | hod4M.elf
    0x0745CF0A, // DVP-0010  | hod4testM.elf
    0x13E59583, // DVP-0010B | hod4M.elf
    0x302FEB00, // DVP-0010B | hod4testM.elf
    0x04E08C99, // DVP-0011  | lgj_final
    0x0C3D3CC3, // DVP-0011A | lgj_final
    0xD9660B2E, // DVP-0015  | JenTest
    0x821C3404, // DVP-0015  | Jennifer
    0x13AF8581, // DVP-0015A | JenTest
    0xB2CE9B23, // DVP-0015A | Jennifer
    0xCC32DEAE, // DVP-0018  | abc
    0x17114BC1, // DVP-0018A | abc
    0x22905D60, // DVP-0019A | id4.elf
    0x43582D48, // DVP-0019B | id4.elf
    0x2D2A18C1, // DVP-0019C | id4.elf
    0x9BFD0D98, // DVP-0019D | id4.elf
    0xB84D2D0E, // DVP-0019D | id4_serverbox.elf
    0x9CF9BBCC, // DVP-0019G | id4.elf
    0xDCAD8ABA, // DVP-0025H | q2satl_lind
    0xFA0F6AB0, // DVP-0027A | apacheM.elf
    0x5A7F315E, // DVP-0027A | apachetestM.elf
    0x9D414D18, // DVP-0029A | vsg
    0xC345E213, // DVP-0030B | id4.elf
    0x98E6A516, // DVP-0030C | id4.elf
    0xF67365C9, // DVP-0030D | id4.elf
    0x8BDD31BA, // DVP-0031  | abc
    0x3DF37873, // DVP-0031A | abc
    0xDD8BB792, // DVP-0036A | lgjsp_app
    0xB0A96E34, // DVP-0043  | vf5
    0xF99E5635, // DVP-0044  | drive.elf
    0x4143F6B4, // DVP-0048A | main.exe
    0x65489691, // DVP-0049  | mj4
    0x653BC83B, // DVP-0057  | a.elf
    0x04D88552, // DVP-0057B | a.elf
    0x089D6051, // DVP-0060  | dsr
    0x317F3B90, // DVP-0063  | hodexRI.elf
    0x3A5EEC69, // DVP-0063  | hodextestR.elf
    0x81E02850, // DVP-0069  | RAMBO_SBQLM.elf
    0xE4F202BB, // DVP-0070A | id5.elf
    0x400C09CD, // DVP-0070C | id5.elf
    0x2E6732A3, // DVP-0070F | id5.elf
    0xAEEE6BEF, // DVP-0070  | id5_serverbox.elf
    0xF99A3CDB, // DVP-0075  | id5.elf
    0xDCC1f8E7, // DVP-0078  | RAMBO_SBQLM.elf
    0x05647A8E, // DVP-0079  | hummer_Master.elf
    0x0AD7CF0F, // DVP-0081  | mj4
    0x4442EA15, // DVP-0083  | hummer_Master.elf
    0x8DF6BBF9, // DVP-0084  | id5.elf
    0x2AF8004E, // DVP-0084A | id5.elf
    0xCB663DD0, // DVP-0087D | q2satl_lind
    0xB95528F4, // DVP-5004  | vf5
    0x012E4898, // DVP-5004D | vf5
    0x74465F9F, // DVP-5004G | vf5
    0x75B48E22, // DVP-5007  | chopperM.elf
    0xFCB9D941, // DVP-5019A | vf5
    0xAB70901C, // DVP-5020  | vf5
    0x6BAA510D, // DVP-5020  | vf5 | Ver 6.000
};

int cleanElfCRC32Count = sizeof(cleanElfCRC32) / sizeof(uint32_t);

bool isIDGame()
{
    switch (elfCrc)
    {
        case 0x22905D60: // DVP-0019A | id4.elf
        case 0x43582D48: // DVP-0019B | id4.elf
        case 0x2D2A18C1: // DVP-0019C | id4.elf
        case 0x9BFD0D98: // DVP-0019D | id4.elf
        case 0x9CF9BBCC: // DVP-0019G | id4.elf
        case 0xC345E213: // DVP-0030B | id4.elf
        case 0x98E6A516: // DVP-0030C | id4.elf
        case 0xF67365C9: // DVP-0030D | id4.elf
        case 0xE4F202BB: // DVP-0070A | id5.elf
        case 0x400C09CD: // DVP-0070C | id5.elf
        case 0x2E6732A3: // DVP-0070F | id5.elf
        case 0xF99A3CDB: // DVP-0075  | id5.elf
        case 0x8DF6BBF9: // DVP-0084  | id5.elf
        case 0x2AF8004E: // DVP-0084A | id5.elf
        {
            return true;
        }
        default:
        {
            return false;
        }
    }
}

uint32_t calcCrc32(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++)
    {
        if (crc & 1)
        {
            crc = (crc >> 1) ^ 0xEDB88320;
        }
        else
        {
            crc = (crc >> 1);
        }
    }
    return crc;
}

uint32_t getCrc32Mem(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++)
    {
        crc = calcCrc32(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFF;
}

int calculateCRC32inChunks(const char *filename, uint32_t *crc)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        log_error("Could not open file to calculate the CRC32.");
        return EXIT_FAILURE;
    }

    // A byte at a time from a table: the games' files run to hundreds of MB.
    static uint32_t table[256];
    if (table[1] == 0)
        for (uint32_t i = 0; i < 256; i++)
            table[i] = calcCrc32(i, 0);

    *crc = 0xFFFFFFFF;
    static uint8_t buffer[1 << 16];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        for (size_t i = 0; i < bytesRead; i++)
        {
            *crc = (*crc >> 8) ^ table[(*crc ^ buffer[i]) & 0xFF];
        }
    }
    fclose(file);
    return 0;
}

int lookupCrcTable(uint32_t crc)
{
    for (int x = 0; x < cleanElfCRC32Count; x++)
    {
        if (cleanElfCRC32[x] == crc)
            return 1;
    }
    return 0;
}

bool fileExists(const char *path)
{
    struct stat buffer;
    return stat(path, &buffer) == 0;
}

char *myBasename(char *path)
{
    if (path == NULL || *path == '\0')
        return ".";

    char *last = path;
    for (char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p + 1;
    }
    return (*last != '\0') ? last : path;
}

bool isPathAbsolute(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    // POSIX absolute paths (/path) — also works on Windows with mingw/gcc
    if (path[0] == '/')
        return true;

#ifdef _WIN32
    // Windows rooted paths (\path or \\server\share)
    if (path[0] == '\\')
        return true;
    // Windows drive-letter absolute paths (C:\path or C:/path)
    if (isalpha((unsigned char)path[0]) && path[1] == ':')
        return true;
#endif

    return false;
}

bool dirExists(const char *path)
{
    struct stat buffer;
    return stat(path, &buffer) == 0 && S_ISDIR(buffer.st_mode);
}

void isCleanElf(char *command)
{
    char *lastSpace = strrchr(command, ' ');
    size_t length;
    if (lastSpace != NULL && strcmp(lastSpace, " -t") == 0)
        length = (size_t)(lastSpace - command);
    else
        length = strlen(command);

    char elfName[256];
    strncpy(elfName, command, length);
    elfName[length] = '\0';

    if (dirExists(elfName))
    {
        log_error("There is a folder named \'%s\' in the current directory instead of a game's ELF.", elfName + 2);
        exit(EXIT_FAILURE);
    }

    if (calculateCRC32inChunks(elfName, &elfCrc) != 0)
        return;

    elfCrc = elfCrc ^ 0xFFFFFFFF;
    if (!lookupCrcTable(elfCrc))
    {
        printf("\033[1;31m");
        printf("Warning: The ELF you are running is not Clean and might cause unwanted behavior.\n");
        printf("         Make sure you ELF and game dump are clean before reporting issues.\n");
        printf("         If you are sure the ELF is clean, please report it to us.\n");
        printf("\033[0m");
    }
}

void extractPathFromProg(const char *input, char *out_path, char *out_prog)
{
    char tmp1[MAX_PATH_LENGTH], tmp2[MAX_PATH_LENGTH];
    strncpy(tmp1, input, MAX_PATH_LENGTH);
    strncpy(tmp2, input, MAX_PATH_LENGTH);
    strncpy(out_prog, myBasename(tmp1), MAX_PATH_LENGTH);
    strncpy(out_path, dirname(tmp2), MAX_PATH_LENGTH);
}

void testModePath(char *program)
{
    if (strcmp(program, "hod4M.elf") == 0)
        strcpy(program, "hod4testM.elf");
    else if (strcmp(program, "hodexRI.elf") == 0)
        strcpy(program, "hodextestR.elf");
    else if (strcmp(program, "apacheM.elf") == 0)
        strcpy(program, "apachetestM.elf");
    else if (strcmp(program, "vt3_Lindbergh") == 0)
        strcpy(program, "vt3_testmode");
    else if (strcmp(program, "Jennifer") == 0)
        strcpy(program, "../JenTest/JenTest");
    else
        strcat(program, " -t");
}

char *findPreloadLibrary(const char *originalDir, const char *libraryPath, char *preloadFileName)
{
    static char result[MAX_PATH_LENGTH];
    char appImageLib[MAX_PATH_LENGTH];
    char llDepsPath[MAX_PATH_LENGTH];
    char libraryPathDir[MAX_PATH_LENGTH];
    snprintf(appImageLib, MAX_PATH_LENGTH, "%s/usr/lib32", getenv("APP_IMG_ROOT"));
    snprintf(llDepsPath, MAX_PATH_LENGTH, "%s/ll-deps", originalDir);

    if (strlen(libraryPath) > 0)
    {
        if (isPathAbsolute(libraryPath))
            snprintf(libraryPathDir, sizeof(libraryPathDir), "%s", libraryPath);
        else
            snprintf(libraryPathDir, sizeof(libraryPathDir), "%s%c%s", originalDir, PATH_SEPARATOR, libraryPath);
    }
    else
    {
        libraryPathDir[0] = '\0';
    }

    const char *folderCandidates[] = {
        "/app/lib32", appImageLib, libraryPathDir, llDepsPath, originalDir, "/usr/lib/i386-linux-gnu",
        "/usr/lib/i686-linux-gnu", "/usr/lib32", "/usr/lib", NULL};

    for (int i = 0; i < sizeof(folderCandidates) / sizeof(folderCandidates[0]); i++)
    {
        snprintf(result, MAX_PATH_LENGTH, "%s/%s", folderCandidates[i], preloadFileName);
        if (fileExists(result))
            return result;
    }

    if (fileExists(preloadFileName))
        return preloadFileName;

    return "";
}

bool hasSpaces(const char *path)
{
    if (strchr(path, ' '))
        return true;

    return false;
}

void printUsage(char *programName)
{
    printf("%s [GAME_PATH] [OPTIONS]\n", myBasename(programName));
    printf("Options:\n");
    printf("  --test             | -t  Runs the test mode\n");
    printf("  --segaboot         | -s  Runs segaboot\n");
#ifdef __linux__
    printf("  --zink             | -z  Runs with Zink\n");
    printf("  --nvidia           | -n  Runs with nVidia GPU when is as a secondary GPU in a laptop\n");
    printf("  --gdb                    Runs with GDB\n");
    printf("  --list-controllers | -l  Lists available controllers and inputs for EVDEV.\n");
#endif
    printf("  --list-guids       | -lg Lists available SDL controllers GUIDs.\n");
    printf("  --version          | -v  Displays the version of the loader and team's names\n");
    printf("  --help             | -h  Displays this usage text\n");
    printf("  --config           | -c  Specifies configuration ini file path\n");
    printf("  --controls         | -o  Specifies controls ini file path\n");
    printf("  --controllerdb     | -d  Specifies gamecontrollerdb.txt file path\n");
    printf("  --library-path     | -L  Specifies library path\n");
    printf("  --gamepath         | -g  Specifies game path without ELF name\n");
    printf("  --create           | -C  Creates a default config or controls file. Use '--create --help' for more info.\n");
}

void printCreateUsage(char *programName)
{
    printf("Usage for the --create option:\n");
    printf("  %s --create <sub-option> [output_path] [output_filename]\n\n", myBasename(programName));
    printf("Arguments:\n");
    printf("  sub-option          The type of file to create. Can be one of:\n");
    printf("                        config   (for lindbergh.ini)\n");
    printf("                        controls (for controls.ini)\n");
    printf("  output_path         (Optional) The directory where the file will be created.\n");
    printf("                      If not provided, the file is created in the current directory.\n");
    printf("  output_filename     (Optional) The name of the file to be created. Must end with .ini.\n");
    printf("                      If not provided, the default name will be used (lindbergh.ini or controls.ini).\n\n");
    printf("Examples:\n");
    printf("  %s --create config\n", myBasename(programName));
#ifdef __linux__
    printf("  %s --create controls ./my_game_config\n", myBasename(programName));
    printf("  %s --create config ./my_game_config myconfig.ini\n", myBasename(programName));
#else
    printf("  %s --create controls .\\my_game_config\n", myBasename(programName));
    printf("  %s --create config .\\my_game_config\\myconfig.ini\n", myBasename(programName));
#endif
    printf("  %s --create --help\n", myBasename(programName));
}

void printVersion()
{
    printf("linuxloader v%d.%d.%d\n", MAJOR_VERSION, MINOR_VERSION, UPDATE_VERSION);
    printf("Lead Programming/Reversing: %s\n", TEAM);
    printf("Collaborators: %s\n", COLLABORATORS);
    printf("Special Thanks: %s\n", SPECIAL_THANKS);
}

void setDbFileEnv(const char *contDbFilePath)
{
    if (strlen(contDbFilePath) > 0)
    {
        if (hasSpaces(contDbFilePath))
        {
            log_error("Controller database path '%s' contains spaces; additional game controller mappings will not be added.",
                      contDbFilePath);
            contDbFilePath = "";
        }
        else if (!fileExists(contDbFilePath))
        {
            log_error("Controller database file '%s' does not exist; additional game controller mappings will be omitted.", contDbFilePath);
            contDbFilePath = "";
        }
#ifdef __linux__
        setenv(LINUX_LOADER_CONTROLS_DB_PATH, contDbFilePath, 1);
#else
        _putenv_s(LINUX_LOADER_CONTROLS_DB_PATH, contDbFilePath);
#endif
    }
}

#ifdef __linux__
int pathsDiffer(const char *p1, const char *p2)
{
    char real1[4096], real2[4096];
    if (!realpath(p1, real1) || !realpath(p2, real2))
        return 1;
    return strcmp(real1, real2) != 0;
}

void setEnvironmentVariables(const char *ldLibPath, const char *originalDir, const char *gameDir, int zink, int nvidia,
                             const char *libraryPath, const char *confFilePath, const char *contFilePath,
                             const char *contDbFilePath, char *libOpenal)
{
    if (libOpenal != NULL)
    {
        char ldpreloadEnv[MAX_PATH_LENGTH];
        snprintf(ldpreloadEnv, sizeof(ldpreloadEnv), "%s %s", ldLibPath, libOpenal);
        setenv("LD_PRELOAD", ldpreloadEnv, 1);
    }
    else
    {
        setenv("LD_PRELOAD", ldLibPath, 1);
    }

    if (hasSpaces(ldLibPath))
    {
        log_error("The path \'%s\' where lindbergh.so is located cannot contain spaces.", ldLibPath);
        exit(EXIT_FAILURE);
    }

    char *currentLibraryPath = getenv("LD_LIBRARY_PATH");
    char newLdLibPath[MAX_PATH_LENGTH * 3] = "";

    char *appImgRoot = getenv("APP_IMG_ROOT");
    if (getenv("APP_IMG_ROOT") && isIDGame()) // If appimage and ID game.
    {
        strcat(newLdLibPath, appImgRoot);
        strcat(newLdLibPath, "/usr/lib32ID:");
        strcat(newLdLibPath, currentLibraryPath);
    }
    else if (currentLibraryPath && strlen(currentLibraryPath) > 0)
    {
        snprintf(newLdLibPath, sizeof(newLdLibPath), "%s", currentLibraryPath);
    }

    if (appImgRoot == NULL && originalDir != NULL && originalDir[0] != '\0')
    {
        char llDepsPath[MAX_PATH_LENGTH] = "";

        if (strlen(libraryPath) > 0)
        {
            if (isPathAbsolute(libraryPath))
                snprintf(llDepsPath, sizeof(llDepsPath), "%s", libraryPath);
            else
                snprintf(llDepsPath, sizeof(llDepsPath), "%s%c%s", originalDir, PATH_SEPARATOR, libraryPath);

            if (!dirExists(llDepsPath))
            {
                log_warn("Library path '%s' does not exist; falling back to default 'll-deps'.", llDepsPath);
                snprintf(llDepsPath, sizeof(llDepsPath), "%s/ll-deps", originalDir);
            }
        }
        else
        {
            snprintf(llDepsPath, sizeof(llDepsPath), "%s/ll-deps", originalDir);
        }

        if (strcmp(llDepsPath, "") != 0)
            setenv("LINUX_LOADER_DEPS_PATH", llDepsPath, 1);

        if (dirExists(llDepsPath))
        {
            if (newLdLibPath[0] != '\0')
                strcat(newLdLibPath, ":");
            // strcat(newLdLibPath, llDepsPath);
        }
    }

    char *ldLibPathCopy = strdup(ldLibPath);
    char *tmpLibPath = dirname(ldLibPathCopy);
    if (strcmp(tmpLibPath, ".") != 0)
    {
        strcat(newLdLibPath, ":");
        strcat(newLdLibPath, tmpLibPath);
    }

    if (originalDir != NULL && originalDir[0] != '\0' && pathsDiffer(originalDir, tmpLibPath) != 0)
    {
        strcat(newLdLibPath, ":");
        strcat(newLdLibPath, originalDir);
    }

    if (gameDir && gameDir[0] != '\0' && pathsDiffer(originalDir, gameDir) && pathsDiffer(tmpLibPath, gameDir))
    {
        char libDir[MAX_PATH_LENGTH];
        snprintf(libDir, sizeof(libDir), ":%s/%s",originalDir,gameDir);
        strcat(newLdLibPath, libDir);
    }

    free(ldLibPathCopy);

    strcat(newLdLibPath, ":.:lib:../lib");

    if (newLdLibPath[0] == ':')
        memmove(newLdLibPath, newLdLibPath + 1, strlen(newLdLibPath));

    if (elfCrc == 0x4143F6B4)
    {
        char libDir[MAX_PATH_LENGTH];
        snprintf(libDir, sizeof(libDir), ":../data/drv/cri/lindbergh/libs");
        strcat(newLdLibPath, libDir);
    }

    setenv("LD_LIBRARY_PATH", newLdLibPath, 1);

    if (strlen(confFilePath) > 0)
    {
        if (hasSpaces(confFilePath))
        {
            log_warn("Configuration file path '%s' contains spaces; the loader will use the default configuration.", confFilePath);
            confFilePath = "";
        }
        else if (!fileExists(confFilePath))
        {
            log_warn("Configuration file '%s' does not exist; the loader will use the default configuration.", confFilePath);
            confFilePath = "";
        }
        setenv(LINUX_LOADER_CONFIG_PATH, confFilePath, 1);
    }

    if (strlen(contFilePath) > 0)
    {
        if (hasSpaces(contFilePath))
        {
            log_warn("Controls file path '%s' contains spaces; the loader will use the default controls configuration.", contFilePath);
            contFilePath = "";
        }
        else if (!fileExists(contFilePath))
        {
            log_warn("Controls file '%s' does not exist; the loader will use the default controls configuration.", contFilePath);
            contFilePath = "";
        }
        setenv(LINUX_LOADER_CONTROLS_PATH, contFilePath, 1);
    }

    setDbFileEnv(contDbFilePath);

    if (zink && nvidia)
    {
        log_error("Cannot pass both, zink and nvidia options at the same time.");
        exit(EXIT_FAILURE);
    }

    if (zink)
    {
        setenv("LIBGL_KOPPER_DRI2", "1", 1);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
    }

    if (nvidia)
    {
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
    }

    char libglDriPath[MAX_PATH_LENGTH] = {0};
    if (appImgRoot != NULL)
    {
        if (isIDGame())
            snprintf(libglDriPath, sizeof(libglDriPath), "%s/usr/lib32ID/dri", appImgRoot);
        else
            snprintf(libglDriPath, sizeof(libglDriPath), "%s/usr/lib32/dri", appImgRoot);
    }

    if (libglDriPath[0] != '\0')
        setenv("LIBGL_DRIVERS_PATH", libglDriPath, 1);
}

int listEvDevControllers()
{
    Controllers controllers;

    ControllerStatus status = loadEvdevControllers(&controllers);

    if (status != CONTROLLER_STATUS_SUCCESS)
    {
        log_error("Failed to list controllers\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < controllers.count; i++)
    {
        Controller controller = controllers.controller[i];
        if (!controller.enabled)
            continue;

        printf("%s\n", controller.name);

        for (int i = 0; i < controller.inputCount; i++)
        {
            printf("  - %s\n", controller.inputs[i].inputName);
        }
    }

    stopEvdevControllers(&controllers);

    return EXIT_SUCCESS;
}
#endif

int parseArgs(int argc, char *argv[], char *command, char *originalDir, char *gameELF, char *libraryPath)
{
    controlsPath = "";
    configPath = "";

    if (argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0))
    {
        printVersion();
        return PARSE_ARGS_HELP;
    }

    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        printUsage(argv[0]);
        return PARSE_ARGS_HELP;
    }
#ifdef __linux__
    if (argc > 1 && (strcmp(argv[1], "--list-controllers") == 0 || strcmp(argv[1], "-l") == 0))
    {
        return listEvDevControllers();
    }
#endif

    if (argc > 1 && (strcmp(argv[1], "--list-guids") == 0 || strcmp(argv[1], "-lg") == 0))
    {
        listSdlControllers();
        return PARSE_ARGS_HELP;
    }

    if (argc > 1 && (strcmp(argv[1], "--create") == 0 || strcmp(argv[1], "-C") == 0))
    {
        if (argc < 3)
        {
            log_error("Missing arguments for --create. See usage below.\n");
            printCreateUsage(argv[0]);
            return EXIT_FAILURE;
        }

        if (strcmp(argv[2], "--help") == 0)
        {
            printCreateUsage(argv[0]);
            return PARSE_ARGS_HELP;
        }

        char *subOption = argv[2];
        char *path = ".";
        char *filename = NULL;
        char *argCopyForDirname = NULL;
        char *argCopyForBasename = NULL;

        if (strcmp(subOption, "config") != 0 && strcmp(subOption, "controls") != 0)
        {
            log_error("Invalid sub-option for --create: '%s'. Must be 'config' or 'controls'.\n", subOption);
            printCreateUsage(argv[0]);
            return EXIT_FAILURE;
        }

        if (argc > 3)
        {
            if (argc > 4)
            {
                path = argv[3];
                filename = argv[4];
            }
            else
            {
                char *arg = argv[3];
                const char *dot = strrchr(arg, '.');
                if (dot && strcmp(dot, ".ini") == 0)
                {
                    argCopyForDirname = strdup(arg);
                    argCopyForBasename = strdup(arg);
                    path = dirname(argCopyForDirname);
                    filename = myBasename(argCopyForBasename);
                }
                // if arg is a path
                else
                {
                    path = arg;
                }
            }
        }

        if (filename)
        {
            const char *dot = strrchr(filename, '.');
            if (!dot || strcmp(dot, ".ini") != 0)
            {
                log_error("Invalid filename: '%s'. Must have .ini extension.\n", filename);
                if (argCopyForDirname)
                    free(argCopyForDirname);
                if (argCopyForBasename)
                    free(argCopyForBasename);
                return EXIT_FAILURE;
            }
        }

        if (!dirExists(path))
        {
            log_error("Output directory does not exist: %s\n", path);
            if (argCopyForDirname)
                free(argCopyForDirname);
            if (argCopyForBasename)
                free(argCopyForBasename);
            return EXIT_FAILURE;
        }

        char finalPath[MAX_PATH_LENGTH];

        if (strcmp(subOption, "config") == 0)
        {
            if (!filename)
            {
                filename = "linuxloader.ini";
            }
            snprintf(finalPath, sizeof(finalPath), "%s%c%s", path, PATH_SEPARATOR, filename);
            if (createDefaultIni(finalPath))
            {
                printf("Successfully created configuration file at %s\n", finalPath);
            }
            else
            {
                log_error("Failed to create configuration file at %s\n", finalPath);
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(subOption, "controls") == 0)
        {
            if (!filename)
            {
                filename = "controls.ini";
            }
            snprintf(finalPath, sizeof(finalPath), "%s%c%s", path, PATH_SEPARATOR, filename);
            if (createDefaultControlsIni(finalPath))
            {
                printf("Successfully created controls file at %s\n", finalPath);
            }
            else
            {
                log_error("Failed to create controls file at %s\n", finalPath);
                return EXIT_FAILURE;
            }
        }

        if (argCopyForDirname)
            free(argCopyForDirname);
        if (argCopyForBasename)
            free(argCopyForBasename);

        return PARSE_ARGS_HELP;
    }

    char passedGamePath[MAX_PATH_LENGTH] = "";
    char forcedGamePath[MAX_PATH_LENGTH] = "";
    char forcedGameDir[MAX_PATH_LENGTH] = "";
    char extConfigPath[MAX_PATH_LENGTH] = "";
    char extControlsPath[MAX_PATH_LENGTH] = "";
    char extControlsDbPath[MAX_PATH_LENGTH] = "";
    bool gdb = false;
    bool testMode = false;
    bool segaboot = false;
    bool zink = false;
    bool nvidia = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0)
        {
            testMode = true;
            continue;
        }

        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--segaboot") == 0)
        {
            segaboot = true;
            continue;
        }
#ifdef __linux__
        if (strcmp(argv[i], "--gdb") == 0)
        {
            gdb = true;
            continue;
        }

        if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--zink") == 0)
        {
            zink = true;
            continue;
        }

        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--nvidia") == 0)
        {
            nvidia = true;
            continue;
        }
#endif
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 >= argc)
            {
                break;
            }
            strncpy(extConfigPath, argv[i + 1], MAX_PATH_LENGTH);
            i += 1;
            continue;
        }

        if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--library-path") == 0)
        {
            if (i + 1 >= argc)
            {
                break;
            }
            strncpy(libraryPath, argv[i + 1], MAX_PATH_LENGTH);
            libraryPath[MAX_PATH_LENGTH - 1] = '\0';
            i += 1;
            continue;
        }

        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--controls") == 0)
        {
            if (i + 1 >= argc)
            {
                break;
            }
            strncpy(extControlsPath, argv[i + 1], MAX_PATH_LENGTH);
            i += 1;
            continue;
        }

        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--controllerdb") == 0)
        {
            if (i + 1 >= argc)
            {
                break;
            }
            strncpy(extControlsDbPath, argv[i + 1], MAX_PATH_LENGTH);
            i += 1;
            continue;
        }

        if ((strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gamepath") == 0) && i + 1 < argc)
        {
            strncpy(passedGamePath, argv[i + 1], MAX_PATH_LENGTH);
            i++;
            continue;
        }
        strncpy(forcedGamePath, argv[i], MAX_PATH_LENGTH);
    }

    if (!getcwd(originalDir, MAX_PATH_LENGTH))
    {
        perror("getcwd");
        return EXIT_FAILURE;
    }

    char resolvedConfigPath[MAX_PATH_LENGTH] = "";

    if (strlen(extConfigPath) > 0)
    {
        const bool isPathAbs = isPathAbsolute(extConfigPath);

        if (!isPathAbs)
        {
            snprintf(resolvedConfigPath, sizeof(resolvedConfigPath), "%s%c%s",
                     originalDir, PATH_SEPARATOR, extConfigPath);
        }
        else
        {
            snprintf(resolvedConfigPath, sizeof(resolvedConfigPath), "%s", extConfigPath);
        }

        if (!fileExists(resolvedConfigPath))
        {
            log_warn("Configuration file '%s' does not exist, will try to load linuxloader.ini from the current folder.",
                     resolvedConfigPath);
            resolvedConfigPath[0] = '\0';
        }
    }

    if (strlen(resolvedConfigPath) == 0)
    {
        snprintf(resolvedConfigPath, sizeof(resolvedConfigPath), "%s%c%s", originalDir, PATH_SEPARATOR, DEFAULT_CONFIG_FILE);

        if (!fileExists(resolvedConfigPath))
        {
            resolvedConfigPath[0] = '\0';
            log_warn("Default configuration file '" DEFAULT_CONFIG_FILE "' does not exist in the current directory.");
            log_warn("Will try to load from the game folder or use built-in defaults.");
        }
    }

    strncpy(extConfigPath, resolvedConfigPath, MAX_PATH_LENGTH);
    extConfigPath[MAX_PATH_LENGTH - 1] = '\0';

    char resolvedControlsPath[MAX_PATH_LENGTH] = "";
    if (strlen(extControlsPath) > 0)
    {
        const bool isPathAbs = isPathAbsolute(extControlsPath);

        if (!isPathAbs)
        {
            snprintf(resolvedControlsPath, sizeof(resolvedControlsPath), "%s%c%s",
                     originalDir, PATH_SEPARATOR, extControlsPath);
        }
        else
        {
            snprintf(resolvedControlsPath, sizeof(resolvedControlsPath), "%s", extControlsPath);
        }

        if (!fileExists(resolvedControlsPath))
        {
            log_warn("Controls file '%s' does not exist, will try to load controls.ini from the current folder.",
                     resolvedControlsPath);
            resolvedControlsPath[0] = '\0';
        }
    }

    if (strlen(resolvedControlsPath) == 0)
    {
        snprintf(resolvedControlsPath, sizeof(resolvedControlsPath), "%s%c%s",
                 originalDir, PATH_SEPARATOR, DEFAULT_CONTROLS_FILE);

        if (!fileExists(resolvedControlsPath))
        {
            resolvedControlsPath[0] = '\0';
            log_warn("Default controls file '" DEFAULT_CONTROLS_FILE "' does not exist in the current directory.");
            log_warn("Will try to load from the game folder or use built-in defaults.");
        }
    }

    strncpy(extControlsPath, resolvedControlsPath, MAX_PATH_LENGTH);
    extControlsPath[MAX_PATH_LENGTH - 1] = '\0';

    char resolvedDbPath[MAX_PATH_LENGTH] = "";
    if (strlen(extControlsDbPath) > 0)
    {
        const bool isPathAbs = isPathAbsolute(extControlsDbPath);

        if (!isPathAbs)
        {
            snprintf(resolvedDbPath, sizeof(resolvedDbPath), "%s%c%s",
                     originalDir, PATH_SEPARATOR, extControlsDbPath);
        }
        else
        {
            snprintf(resolvedDbPath, sizeof(resolvedDbPath), "%s", extControlsDbPath);
        }

        strncpy(extControlsDbPath, resolvedDbPath, MAX_PATH_LENGTH);
        extControlsDbPath[MAX_PATH_LENGTH - 1] = '\0';
    }

    int useForceCommandPath = strlen(forcedGamePath) > 0;
    bool commandOnlyElf = useForceCommandPath && strchr(forcedGamePath, PATH_SEPARATOR) == NULL;

    if (useForceCommandPath && !commandOnlyElf)
    {
        if (!fileExists(forcedGamePath))
        {
            log_error("File does not exist: %s\n", forcedGamePath);
            return EXIT_FAILURE;
        }

        extractPathFromProg(forcedGamePath, forcedGameDir, gameELF);
#ifdef __linux__
        if (hasSpaces(forcedGameDir))
        {
            log_warn("The path contains spaces, this most likely will cause issues.");
            log_warn("Please, make sure you don't use spaces in the path.");
        }
#endif

        if (!dirExists(forcedGameDir))
        {
            log_error("Directory does not exist: %s\n", forcedGameDir);
            return EXIT_FAILURE;
        }
        chdir(forcedGameDir);
    }
    else if (strlen(passedGamePath) > 0)
    {
#ifdef __linux__
        if (hasSpaces(passedGamePath))
        {
            log_warn("The path contains spaces, this most likely will cause issues.");
            log_warn("Please, make sure you don't use spaces in the path.");
        }
#endif

        if (!dirExists(passedGamePath))
        {
            log_fatal("Directory does not exist: %s\n", passedGamePath);
            return EXIT_FAILURE;
        }

        chdir(passedGamePath);

        if (commandOnlyElf)
        {
            strncpy(gameELF, forcedGamePath, MAX_PATH_LENGTH);
            if (!fileExists(gameELF))
            {
                log_fatal("Program '%s' not found in %s\n", gameELF, passedGamePath);
                return EXIT_FAILURE;
            }
        }
        else
        {
            for (int i = 0; games[i] && strcmp(games[i], "END") != 0; i++)
            {
                if (fileExists(games[i]))
                {
                    strncpy(gameELF, games[i], MAX_PATH_LENGTH);
                    break;
                }
            }

            if (strlen(gameELF) == 0)
            {
                log_fatal("No known game file found in %s\n", passedGamePath);
                return EXIT_FAILURE;
            }
        }
    }
    else if (commandOnlyElf)
    {
        strncpy(gameELF, forcedGamePath, MAX_PATH_LENGTH);
        if (!fileExists(gameELF))
        {
            log_fatal("'%s' not found in current directory\n", gameELF);
            return EXIT_FAILURE;
        }
    }
    else
    {
        for (int i = 0; games[i] && strcmp(games[i], "END") != 0; i++)
        {
            if (access(games[i], F_OK) == 0)
            {
                strncpy(gameELF, games[i], MAX_PATH_LENGTH);
                gameELF[MAX_PATH_LENGTH - 1] = '\0';
                printf("Auto-detected ELF: %s\n", gameELF);
                break;
            }
        }

        if (gameELF[0] == '\0')
        {
            log_fatal("No game ELF found in current directory.\n");
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (testMode)
    {
        testModePath(gameELF);
    }

#ifdef __linux__
    char *targetedGameDir = strlen(forcedGameDir) ? forcedGameDir : passedGamePath;
    char *libPath = strdup(findPreloadLibrary(originalDir, libraryPath, PRELOAD_FILE_NAME));
    if (strcmp(libPath, "") == 0)
    {
        log_error("Error: %s not found in known locations.\n", PRELOAD_FILE_NAME);
        free(libPath);
        return 1;
    }

    char *libOpenal = NULL;
    if (strstr(gameELF, "q2satl_lind") != NULL)
    {
        libOpenal = strdup(findPreloadLibrary(originalDir, libraryPath, PRELOAD_OPENAL));
        if (strcmp(libOpenal, "") == 0)
        {
            printf("You might not get sound because libopenal.so.0 was not found.\n");
        }
    }
#endif

    if (segaboot)
    {
        strcpy(gameELF, "segaboot -t");
    }

    // Final command
    if (strcmp(gameELF, "../JenTest/JenTest") == 0)
    {
        snprintf(command, MAX_PATH_LENGTH, "%s", gameELF);
    }
    else
    {
        snprintf(command, MAX_PATH_LENGTH, "./%s", gameELF);
    }

    isCleanElf(command);

#ifdef __linux__
    setEnvironmentVariables(libPath, originalDir, targetedGameDir, zink, nvidia, libraryPath, extConfigPath, extControlsPath,
                            extControlsDbPath, libOpenal);

    free(libPath);
    if (libOpenal)
        free(libOpenal);

    if (gdb)
    {
        char temp[128];
        if (testMode)
            strcpy(temp, "gdb --args ");
        else
            strcpy(temp, "gdb ");
        strcat(temp, command);
        strcpy(command, temp);
    }
    setenv(LINUX_LOADER_CURRENT_DIR, originalDir, 1);
#else
    setDbFileEnv(extControlsDbPath);
    controlsPath = strdup(extControlsPath);
    configPath = strdup(extConfigPath);
#endif
    return 0;
}