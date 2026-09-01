#if defined(_WIN32) || defined(__MINGW32__)
#include "gccBridge.hpp"
#include "../log/log.h"
#include "symbolResolver.hpp"
#include <cstdlib>
#include <cctype>
#include <windows.h>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

static unsigned short g_ctype_b[384]; // -128 to 255
static const unsigned short *g_ctype_b_ptr = g_ctype_b + 128;

static int32_t g_ctype_tolower[384];
static const int32_t *g_ctype_tolower_ptr = g_ctype_tolower + 128;

static int32_t g_ctype_toupper[384];
static const int32_t *g_ctype_toupper_ptr = g_ctype_toupper + 128;

static bool g_ctype_initialized = false;

static void InitLinuxCtype();

namespace GccBridge
{
    void initBridges()
    {
        log_info("Initializing GCC Bridges...");

        MAP("__ctype_b_loc", __ctype_b_loc);
        MAP("__ctype_tolower_loc", __ctype_tolower_loc);
        MAP("__ctype_toupper_loc", __ctype_toupper_loc);
        MAP("__ctype_get_mb_cur_max", __ctype_get_mb_cur_max);

        MAP("__strtod_internal", __strtod_internal);
        MAP("__strtol_internal", __strtol_internal);
        MAP("__strtoul_internal", __strtoul_internal);
        MAP("__strtof_internal", bridge__strtof_internal);
        MAP("__strtoll_internal", __strtoll_internal);

        MAP("__assert_fail", __assert_fail);
        MAP("__errno_location", __errno_location);
        MAP("__libc_freeres", __libc_freeres);

        MAP("tolower", tolower);
        MAP("toupper", toupper);
        MAP("towlower", towlower);
        MAP("towupper", towupper);

        InitLinuxCtype();

        MAP("__ctype_tolower", &g_ctype_tolower_ptr);
        MAP("__ctype_toupper", &g_ctype_toupper_ptr);
        MAP("__ctype_b", &g_ctype_b_ptr);

        MAP("isprint", isprint);
        MAP("isupper", isupper);
        MAP("islower", islower);
        MAP("isdigit", isdigit);
        MAP("isalnum", isalnum);
        MAP("isspace", isspace);
        MAP("iscntrl", iscntrl);
        MAP("ispunct", ispunct);
        MAP("isxdigit", isxdigit);
        MAP("isblank", isblank);
        MAP("iswprint", iswprint);
        MAP("iswupper", iswupper);
        MAP("iswlower", iswlower);
        MAP("iswdigit", isdigit);
        MAP("iswalnum", iswalnum);
        MAP("iswspace", iswspace);
        MAP("iswcntrl", iswcntrl);
        MAP("iswpunct", iswpunct);
        MAP("iswxdigit", iswxdigit);
        MAP("iswblank", iswblank);
        MAP("iswctype", iswctype);
        MAP("isalpha", isalpha);
    }

    extern "C"
    {
        double __strtod_internal(const char *n, char **e, int g)
        {
            return strtod(n, e);
        }
        long __strtol_internal(const char *n, char **e, int b, int g)
        {
            return strtol(n, e, b);
        }
        unsigned long __strtoul_internal(const char *n, char **e, int b, int g)
        {
            return strtoul(n, e, b);
        }

        float bridge__strtof_internal(const char *n, char **e, int g)
        {
            return strtof(n, e);
        } 

        long long __strtoll_internal(const char *n, char **e, int b, int g)
        {
            return strtoll(n, e, b);
        }

        const unsigned short **__ctype_b_loc(void)
        {
            InitLinuxCtype();
            return &g_ctype_b_ptr;
        }

        const int32_t **__ctype_tolower_loc(void)
        {
            InitLinuxCtype();
            return &g_ctype_tolower_ptr;
        }
        const int32_t **__ctype_toupper_loc(void)
        {
            InitLinuxCtype();
            return &g_ctype_toupper_ptr;
        }
        size_t __ctype_get_mb_cur_max()
        {
            return 1;
        }

        void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function)
        {
            fprintf(stderr, "ASSERTION FAILED: %s at %s:%d (%s)\n", assertion, file, line, function);
            TerminateProcess(GetCurrentProcess(), 1);
        }

        void __libc_freeres()
        {
        }

        int *__errno_location()
        {
            return _errno();
        }
    }

    const unsigned short *GetCtypeBPtr()
    {
        InitLinuxCtype();
        return g_ctype_b_ptr;
    }

    const int32_t *GetCtypeTolowerPtr()
    {
        InitLinuxCtype();
        return g_ctype_tolower_ptr;
    }

    const int32_t *GetCtypeToUpperPtr()
    {
        InitLinuxCtype();
        return g_ctype_toupper_ptr;
    }

} // namespace GccBridge

static void InitLinuxCtype()
{
    if (g_ctype_initialized)
        return;

// GLIBC internally utilizes byte-swapped layout for its short array on x86 little-endian
#define GLIBC_IS_BIT(bit) ((unsigned short)((bit) < 8 ? ((1U << (bit)) << 8) : ((1U << (bit)) >> 8)))
    const unsigned short _ISupper = GLIBC_IS_BIT(0);
    const unsigned short _ISlower = GLIBC_IS_BIT(1);
    const unsigned short _ISalpha = GLIBC_IS_BIT(2);
    const unsigned short _ISdigit = GLIBC_IS_BIT(3);
    const unsigned short _ISxdigit = GLIBC_IS_BIT(4);
    const unsigned short _ISspace = GLIBC_IS_BIT(5);
    const unsigned short _ISprint = GLIBC_IS_BIT(6);
    const unsigned short _ISgraph = GLIBC_IS_BIT(7);
    const unsigned short _ISblank = GLIBC_IS_BIT(8);
    const unsigned short _IScntrl = GLIBC_IS_BIT(9);
    const unsigned short _ISpunct = GLIBC_IS_BIT(10);
    const unsigned short _ISalnum = GLIBC_IS_BIT(11);

    // Initialize the ctype tables
    for (int i = -128; i < 256; i++)
    {
        int c = i;
        unsigned short flags = 0;

        if (i >= -1 && i <= 255)
        {
            // ASCII character range
            if (isupper(c))
                flags |= _ISupper | _ISalpha | _ISalnum | _ISprint | _ISgraph;
            if (islower(c))
                flags |= _ISlower | _ISalpha | _ISalnum | _ISprint | _ISgraph;
            if (isdigit(c))
                flags |= _ISdigit | _ISalnum | _ISprint | _ISgraph;
            if (isspace(c))
                flags |= _ISspace;
            if (isprint(c))
                flags |= _ISprint;
            if (isgraph(c))
                flags |= _ISgraph;
            if (iscntrl(c))
                flags |= _IScntrl;
            if (ispunct(c))
                flags |= _ISpunct | _ISprint | _ISgraph;
            if (isxdigit(c))
                flags |= _ISxdigit;
            if (c == ' ' || c == '\t')
                flags |= _ISblank;
        }

        g_ctype_b[128 + i] = flags;
        g_ctype_tolower[128 + i] = tolower(c);
        g_ctype_toupper[128 + i] = toupper(c);
    }
    g_ctype_initialized = true;
    log_debug("[GccBridge] Initialized Linux Ctype table.");
}

#endif