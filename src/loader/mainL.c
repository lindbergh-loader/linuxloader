#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __i386__
#define __i386__
#endif
#undef __x86_64__

#include "rawthrills/rawthrills.h"
#include <stdbool.h>
#include <ctype.h>
#include <link.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/limits.h>

#include "init.h"
#include "input/evdevInput.h"
#include "log/log.h"
#include "patching/patch.h"
#include "hardware/lindbergh/securityBoard.h"



uint32_t partialElfCrc = 0;

char *configFolder = {0};

static int callback(struct dl_phdr_info *info, size_t size, void *data);

uint16_t basePortAddress = 0xFFFF;

/**
 * @brief Signal handler for SIGSEGV.
 *
 * This function handles segmentation faults (SIGSEGV) and attempts to recover
 * from certain types of memory access errors, particularly those related to I/O ports.
 */
/**
 * Signal handler for the SIGSEGV signal, which is triggered when a process tries to access an illegal memory location.
 * @param signal
 * @param info
 * @param ptr
 */
static void handleSegfault(int signal, siginfo_t *info, void *ptr)
{
    ucontext_t *ctx = ptr;

    // Get the address of the instruction causing the segfault
    // uint8_t *code = (uint8_t *)ctx->uc_mcontext.gregs[REG_EIP];
    greg_t eip_value = ctx->uc_mcontext.gregs[REG_EIP];
    uint8_t *code = (uint8_t *)(size_t)eip_value; // Use uintptr_t to ensure proper alignment

    // A call through a bad function pointer: there is no code to read at
    // EIP, and reading it here would fault again and kill the game without
    // a word. Say where it came from (the return address on the stack) and
    // let the fault kill it.
    if ((uintptr_t)info->si_addr == (uintptr_t)eip_value)
    {
        uintptr_t from = *(uintptr_t *)(size_t)ctx->uc_mcontext.gregs[REG_ESP];
        Dl_info where = {0};
        dladdr((void *)from, &where);
        fprintf(stderr, "linuxloader: jump to bad address 0x%08x from 0x%08x (%s+0x%x %s)\n", (uint32_t)eip_value,
                (uint32_t)from, where.dli_fname ? where.dli_fname : "?",
                (uint32_t)(from - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "");
        struct sigaction dfl = {.sa_handler = SIG_DFL};
        sigaction(SIGSEGV, &dfl, NULL);
        return;
    }

    switch (*code)
    {
        case 0xED: // IN
        {
            // Get the port number from the EDX register
            uint16_t port = ctx->uc_mcontext.gregs[REG_EDX] & 0xFFFF;

            // The first port called is usually random, but everything after that
            // is a constant offset, so this is a hack to fix that.
            // When run as sudo it works fine!?

            if (basePortAddress == 0xFFFF)
                basePortAddress = port;

            // Adjust the port number if necessary
            if (port > 0x38)
                port = port - basePortAddress;

            // Call the security board input function with the port number and data
            securityBoardIn(port, (uint32_t *)&(ctx->uc_mcontext.gregs[REG_EAX]));

            ctx->uc_mcontext.gregs[REG_EIP]++;
            return;
        }
        break;

        case 0xE7: // OUT IMMEDIATE
        {
            // Increment the instruction pointer by two to skip over this instruction
            ctx->uc_mcontext.gregs[REG_EIP] += 2;
            return;
        }
        break;

        case 0xE6: // OUT IMMEDIATE
        {
            // Increment the instruction pointer by two to skip over this instruction
            ctx->uc_mcontext.gregs[REG_EIP] += 2;
            return;
        }
        break;

        case 0xEE: // OUT
        {
            uint16_t port = ctx->uc_mcontext.gregs[REG_EDX] & 0xFFFF;
            uint8_t data = ctx->uc_mcontext.gregs[REG_EAX] & 0xFF;
            ctx->uc_mcontext.gregs[REG_EIP]++;
            return;
        }
        break;

        case 0xEF: // OUT
        {
            uint16_t port = ctx->uc_mcontext.gregs[REG_EDX] & 0xFFFF;
            ctx->uc_mcontext.gregs[REG_EIP]++;
            return;
        }
        break;

        default:
            // repeat_printf("Skipping SEGFAULT %X\n", *code);
            log_warn("Skipping SEGFAULT %X\n", *code);
            ctx->uc_mcontext.gregs[REG_EIP]++;
            // abort();
    }
}

uint32_t getCrc32(const char *s, ssize_t n)
{
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < n; i++)
    {
        char ch = s[i];
        for (size_t j = 0; j < 8; j++)
        {
            uint32_t b = (ch ^ crc) & 1;
            crc >>= 1;
            if (b)
                crc = crc ^ 0xEDB88320;
            ch >>= 1;
        }
    }
    return ~crc;
}

int setConfigFolder()
{
    char *home = getenv("HOME");
    if (!home)
    {
        fprintf(stderr, "HOME environment variable not set.\n");
        return 1;
    }

    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/.config/linuxloader", home);
    configFolder = strdup(config_path);

    char config_base[PATH_MAX];
    snprintf(config_base, sizeof(config_base), "%s/.config", home);
    mkdir(config_base, 0755);

    if (mkdir(config_path, 0755) == -1)
    {
        if (errno != EEXIST)
            return 1;
    }

    return 0;
}

/**
 * @brief Trims leading and trailing whitespace and quotes from a string.
 *
 * This function removes leading and trailing whitespace characters, as well as
 * single and double quotes, from the input string.
 * @param str The string to trim.
 */
char *trimOS_ID(char *str)
{
    if (!str)
        return str;

    char *end;
    while (isspace((unsigned char)*str) || *str == '"' || *str == '\'')
        str++;

    if (*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while (end > str && (isspace((unsigned char)*end) || *end == '"' || *end == '\''))
        end--;

    *(end + 1) = '\0';

    return str;
}

/**
 * @brief Checks if the operating system is Debian-based.
 *
 * This function reads the /etc/os-release file to determine if the current
 * operating system is Debian or Ubuntu, or a derivative of them.
 */
bool checkOS_ID()
{
    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp)
    {
        return false;
    }

    char line[256];
    bool found = false;

    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "ID=", 3) == 0)
        {
            char *value = trimOS_ID(line + 3);
            if (strcmp(value, "debian") == 0 || strcmp(value, "ubuntu") == 0)
            {
                found = true;
                break;
            }
        }
        else if (strncmp(line, "ID_LIKE=", 8) == 0)
        {
            char value_part[256];
            strncpy(value_part, line + 8, sizeof(value_part) - 1);
            value_part[sizeof(value_part) - 1] = '\0';

            char *value_trimmed = trimOS_ID(value_part);
            char *token = strtok(value_trimmed, " \t\n");

            while (token != NULL)
            {
                char *clean_token = trimOS_ID(token);
                if (strcmp(clean_token, "debian") == 0 || strcmp(clean_token, "ubuntu") == 0)
                {
                    found = true;
                    break;
                }
                token = strtok(NULL, " \t\n");
            }

            if (found)
                break;
        }
    }

    fclose(fp);
    return found;
}

/**
 * @brief Initialization function for the hook library.
 *
 * This function is called automatically when the library is loaded. It sets up
 * signal handlers, initializes configuration, and performs other setup tasks.
 */
void __attribute__((constructor)) hook_init()
{
    logSetMinLevel(LOG_ERROR);
    // Launched from a frontend, stdout is a pipe: line-buffer it, or what the
    // game printed before a crash is lost with it.
    setvbuf(stdout, NULL, _IOLBF, 0);
    // We force x11 for SDL so the window do not scale in wayland.
    setenv("SDL_VIDEODRIVER", "x11", 1);

    // Get offsets of the Game's ELF and calculate CRC32.
    dl_iterate_phdr(callback, NULL);

    // Implement SIGSEGV handler
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handleSegfault;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);

    // setConfigFolder();

    if (!checkOS_ID())
    {
        log_warn("Seems like you're not in debian-like system. There might be unexpected issues.");
    }

    initMain("", "");
}

static int callback(struct dl_phdr_info *info, size_t size, void *data)
{
if ((info->dlpi_phnum >= 3) && (info->dlpi_phdr[2].p_type == PT_LOAD) && (info->dlpi_phdr[2].p_flags == 5))
    {
        size_t a = (size_t)(info->dlpi_addr + info->dlpi_phdr[2].p_vaddr + 10);
        printf("%p\n", (void *)a);
        partialElfCrc = getCrc32((void *)(size_t)(info->dlpi_addr + info->dlpi_phdr[2].p_vaddr + 10), 0x4000);
    }
    // Raw Thrills dumps map their code RWE; identify them over the same range.
    else if ((info->dlpi_phnum >= 3) && (info->dlpi_phdr[2].p_type == PT_LOAD) && (info->dlpi_phdr[2].p_flags & PF_X))
    {
        uint32_t crc = getCrc32((void *)(size_t)(info->dlpi_addr + info->dlpi_phdr[2].p_vaddr + 10), 0x4000);
        if (rtGetGame(crc))
            partialElfCrc = crc;
    }
    return 1; 
}

#endif