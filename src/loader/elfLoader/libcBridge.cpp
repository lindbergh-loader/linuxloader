#if defined(_WIN32) || defined(__MINGW32__)
#include "../redirections/filesystemShared.h"
#include "memoryManager.hpp"
#include "libcBridge.hpp"
#include "../redirections/libcShared.h"
#include "../log/log.h"
#include "gccBridge.hpp"
#include "symbolResolver.hpp"
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <setjmp.h>
#include <process.h>
#include <sys/time.h>
#include <sys/utime.h>
#include <wctype.h>
#include <windows.h>
#include <time.h>
#include <unistd.h>
#include <cmath>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

extern "C"
{
    // libgcc / compiler-rt internal integer division/conversion compiler builtins
    long long __divdi3(long long a, long long b);
    unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
    unsigned long long __umoddi3(unsigned long long a, unsigned long long b);
    long long __moddi3(long long a, long long b);
    unsigned long long __fixunsdfdi(double a);
    unsigned long long __fixunssfdi(float a);

    // glibc internal string to number conversion functions
    double __strtod_internal(const char *nptr, char **endptr, int group);
    long int __strtol_internal(const char *nptr, char **endptr, int base, int group);
    unsigned long int __strtoul_internal(const char *nptr, char **endptr, int base, int group);
}

namespace LibcBridge
{
    char bridgeLibcSingleThreaded = 1; // 1 = true (skip pthread locks), 0 = false

    FILE *native_stdin = stdin;
    FILE *native_stdout = stdout;
    FILE *native_stderr = stderr;

    void initBridges()
    {
        log_info("Initializing Libc Bridges...");

        // printf family
        MAP("printf", bridgePrintf);
        MAP("puts", bridgePuts);
        MAP("fprintf", bridgeFprintf);
        MAP("sprintf", bridgeSprintf);
        MAP("snprintf", bridgeSnprintf);
        MAP("vprintf", bridgeVprintf);
        MAP("vfprintf", bridgeVfprintf);
        MAP("vsprintf", bridgeVsprintf);
        MAP("vsnprintf", bridgeVsnprintf);
        MAP("fscanf", bridgeFscanf);
        MAP("sscanf", bridgeSscanf);
        MAP("index", bridgeIndex);
        MAP("fnmatch", bridgeFnmatch);

        MAP("stdin", &native_stdin);
        MAP("stdout", &native_stdout);
        MAP("stderr", &native_stderr);

        // some static libraries refer to glibc's internal IO structs
        MAP("_IO_2_1_stdin_", stdin);
        MAP("_IO_2_1_stdout_", stdout);
        MAP("_IO_2_1_stderr_", stderr);

        // time functions
        MAP("time", bridgeTime);
        MAP("gettimeofday", bridgeGettimeofday);
        MAP("settimeofday", bridgeSettimeofday);
        MAP("localtime", bridgeLocaltime);
        MAP("utime", bridgeUtime);
        MAP("usleep", bridgeUsleep);
        MAP("sleep", bridgeSleep);
        MAP("nanosleep", bridgeNanosleep);
        MAP("localtime_r", sharedLocaltime_R);
        MAP("clock_gettime", bridgeClockGettime);
        MAP("mktime", bridgeMktime);
        MAP("gmtime_r", bridgeGmtime_R);
        MAP("strftime", bridgeStrftime);
        MAP("ftime", bridgeFtime);

        // abort/exit
        MAP("abort", bridgeAbort);
        MAP("exit", bridgeExit);
        MAP("_exit", bridgeExit);

        // C++ ABI functions
        MAP("__cxa_atexit", bridgeCxaAtexit);
        MAP("__cxa_thread_atexit_impl", bridgeCxaThreadAtexitImpl);
        MAP("__register_frame_info_bases", bridgeRegisterFrameInfoBases);
        MAP("__register_frame_info", bridgeRegisterFrameInfo);

        MAP("__libc_single_threaded", &bridgeLibcSingleThreaded);

        // locale functions
        MAP("setlocale", bridgeSetlocale);
        MAP("newlocale", bridgeNewlocale);
        MAP("__newlocale", bridgeNewlocale);
        MAP("freelocale", bridgeFreelocale);
        MAP("__freelocale", bridgeFreelocale);
        MAP("uselocale", bridgeUselocale);
        MAP("__uselocale", bridgeUselocale);
        MAP("localeconv", bridgeLocaleconv);
        MAP("__localeconv", bridgeLocaleconv);

        // wide character functions
        MAP("__wctype_l", bridgeWctypeL);
        MAP("__iswctype_l", bridgeIswctypeL);
        MAP("mbsrtowcs", bridgeMbsrtowcs);

        // gettext
        MAP("gettext", bridgeGettext);
        MAP("dgettext", bridgeDgettext);

        // Stubs
        MAP("sysctl", bridgeSysctl);
        MAP("iopl", bridgeStubSuccess);

        // From other loader
        MAP("__divdi3", __divdi3);
        MAP("__udivdi3", __udivdi3);
        MAP("__umoddi3", __umoddi3);
        MAP("__moddi3", __moddi3);
        MAP("__fixunsdfdi", __fixunsdfdi);
        MAP("__fixunssfdi", __fixunssfdi);
        MAP("_setjmp", bridgeSetjmp);

        // system
        MAP("system", bridgeSystem);

        MAP("getpid", _getpid);
        MAP("getuid", bridgeGetuid);
        MAP("waitpid", bridgeWaitpid);
        MAP("fork", bridgeFork);
        MAP("vfork", bridgeVfork);
        MAP("daemon", bridgeDaemon);
        MAP("execlp", bridgeExeclp);
        MAP("kill", bridgeKill);
        MAP("wait", bridgeWait);
        MAP("getenv", sharedGetenv);
        MAP("setenv", sharedSetenv);
        MAP("unsetenv", sharedUnsetenv);

        MAP("syslog", bridgeSyslog);
        MAP("openlog", bridgeOpenlog);
        MAP("closelog", bridgeCloselog);

        MAP("rand", rand);
        MAP("random", rand);
        MAP("rand_r", bridgeRand_r);
        MAP("srand", srand);
        MAP("signal", bridgeSignal);
        MAP("raise", bridgeRaise);
        MAP("sigfillset", bridgeSigfillset);
        MAP("sigemptyset", bridgeSigemptyset);
        MAP("sigaction", bridgeSigaction);
        MAP("alarm", bridgeStubSuccess);
        MAP("qsort", qsort);
        MAP("poll", bridgePoll);
        MAP("bsearch", bsearch);
        MAP("realpath", bridgeRealpath);
        MAP("popen", bridgePopen);
        MAP("pclose", bridgePclose);
        MAP("perror", bridgePerror);

        MAP("isinf", bridgeIsinf);
        MAP("isnan", bridgeIsnan);
        MAP("__isnanf", bridgeIsnanf); 
        MAP("wcscoll_l", bridgeWcscoll_l);
        MAP("wcsxfrm_l", bridgeWcsxfrm_l);
        MAP("towlower_l", bridgeTowlower_l);
        MAP("towupper_l", bridgeTowupper_l);
        MAP("nl_langinfo", bridgeNlLanginfo);

        // Math
        MAP("atoi", atoi);
        MAP("atof", atof);

        // Memory
        MAP("getpagesize", bridgeGetpagesize);
        MAP("mmap", bridgeMmap);
        MAP("munmap", bridgeMunmap);

        MAP("nice", bridgeNice);

        // Library handles
        MAP("dlopen", sharedDlopen);
        MAP("dlsym", sharedDlsym);
        MAP("dlclose", sharedDlclose);
        MAP("dlerror", sharedDlerror);

        MAP("kswap_collect", sharedKswap_collect);
        MAP("bzero", bridgeBzero);

        // Wide char string functions
        MAP("wmemcmp", bridgeWmemcmp);
        MAP("wmemcpy", bridgeWmemcpy);
        MAP("wmemset", bridgeWmemset);
        MAP("wmemchr", bridgeWmemchr);
        MAP("wcslen", bridgeWcslen);
        MAP("wcscpy", bridgeWcscpy);
        MAP("wcsncpy", bridgeWcsncpy);
        MAP("wcscmp", bridgeWcscmp);
        MAP("wcscoll", bridgeWcscoll);
        MAP("wcsncmp", bridgeWcsncmp);
        MAP("wcschr", bridgeWcschr);
        MAP("wcsrchr", bridgeWcsrchr);
        MAP("wcsstr", bridgeWcsstr);
        MAP("wcstod", bridgeWcstod);
        MAP("wcstol", bridgeWcstol);
        MAP("wctob", bridgeWctob);
        MAP("wctype", bridgeWctype);
        MAP("wcsrtombs", bridgeWcsrtombs);
        MAP("wcrtomb", bridgeWcrtomb);
        MAP("wcsftime", bridgeWcsftime);
        MAP("wcsxfrm", bridgeWcsxfrm);
        MAP("wcstombs", bridgeWcstombs);
        MAP("mbrtowc", bridgeMbrtowc);
        MAP("mbtowc", bridgeMbtowc);
        MAP("mbstowcs", bridgeMbstowcs);
        MAP("btowc", bridgeBtowc);
        MAP("putwc", bridgePutwc);
        MAP("getwc", bridgeGetwc);
        MAP("ungetwc", bridgeUngetwc);
        MAP("fgetwc", bridgeGetwc);
        MAP("fputwc", bridgePutwc);
    }

    int __attribute__((naked)) bridgeSetjmp(int *env)
    {
        asm volatile("mov 4(%esp), %edx\n\t"
                     "mov %ebx, (%edx)\n\t"
                     "mov %esi, 4(%edx)\n\t"
                     "mov %edi, 8(%edx)\n\t"
                     "mov %ebp, 12(%edx)\n\t"
                     "lea 4(%esp), %ecx\n\t"
                     "mov %ecx, 16(%edx)\n\t"
                     "mov (%esp), %eax\n\t"
                     "mov %eax, 20(%edx)\n\t"
                     "xor %eax, %eax\n\t"
                     "ret\n\t");
    }

    void bridgeAbort()
    {
        log_fatal("abort() called by ELF!");
        ::abort();
    }

    void bridgeExit(int status)
    {
        log_fatal("exit(%d) called by ELF!", status);
        ::exit(status);
    }

    int bridgeCxaAtexit(void (*func)(void *), void *arg, void *dso_handle)
    {
        log_debug("__cxa_atexit called");
        return 0;
    }

    int bridgeCxaThreadAtexitImpl(void (*func)(void *), void *arg, void *dso_handle)
    {
        log_debug("__cxa_thread_atexit_impl called");
        return 0;
    }

    void bridgeRegisterFrameInfoBases(void *begin, void *ob, void *tbase, void *dbase)
    {
    }

    void bridgeRegisterFrameInfo(void *begin, void *ob)
    {
    }

    void *bridgeDeregisterFrameInfo(void *begin)
    {
        return nullptr;
    }

    void *bridgeCxaAllocateException(size_t thrown_size)
    {
        return MemoryManager::customMalloc(thrown_size);
    }

    void bridgeCxaFreeException(void *thrown_exception)
    {
        MemoryManager::customFree(thrown_exception);
    }

    char *bridgeSetlocale(int category, const char *locale)
    {
        return setlocale(category, locale);
    }

    struct FakeLocaleStruct
    {
        void *localeData[13];         // __locale_data* per LC_* category (unused, keep NULL)
        const unsigned short *ctypeB; // == __ctype_b
        const int32_t *ctypeTolower;  // == __ctype_tolower
        const int32_t *ctypeToUpper;  // == __ctype_toupper
        const char *names[13];        // locale category name strings (unused)
    };

    static FakeLocaleStruct g_FakeClassicLocale = {};
    static bool g_FakeLocaleInitialized = false;

    static FakeLocaleStruct *GetFakeLocale()
    {
        if (!g_FakeLocaleInitialized)
        {
            memset(&g_FakeClassicLocale, 0, sizeof(g_FakeClassicLocale));
            g_FakeClassicLocale.ctypeB = GccBridge::GetCtypeBPtr();
            g_FakeClassicLocale.ctypeTolower = GccBridge::GetCtypeTolowerPtr();
            g_FakeClassicLocale.ctypeToUpper = GccBridge::GetCtypeToUpperPtr();
            g_FakeLocaleInitialized = true;
        }
        return &g_FakeClassicLocale;
    }

    void *bridgeNewlocale(int category_mask, const char *locale, void *base)
    {
        log_debug("bridgeNewlocale: category_mask=%d, locale=%s", category_mask, locale ? locale : "(null)");
        return GetFakeLocale();
    }

    void bridgeFreelocale(void *loc)
    {
    }

    void *bridgeUselocale(void *loc)
    {
        return GetFakeLocale();
    }

    struct lconv *bridgeLocaleconv(void)
    {
        static struct lconv l;
        static bool initialized = false;
        if (!initialized)
        {
            l.decimal_point = (char *)".";
            l.thousands_sep = (char *)",";
            l.grouping = (char *)"";
            l.int_curr_symbol = (char *)"";
            l.currency_symbol = (char *)"";
            l.mon_decimal_point = (char *)".";
            l.mon_thousands_sep = (char *)",";
            l.mon_grouping = (char *)"";
            l.positive_sign = (char *)"";
            l.negative_sign = (char *)"";
            l.int_frac_digits = 127;
            l.frac_digits = 127;
            l.p_cs_precedes = 127;
            l.p_sep_by_space = 127;
            l.n_cs_precedes = 127;
            l.n_sep_by_space = 127;
            l.p_sign_posn = 127;
            l.n_sign_posn = 127;
            initialized = true;
        }
        return &l;
    }

    uint32_t bridgeWctypeL(const char *property, void *locale)
    {
        return (uint32_t)wctype(property);
    }

    int bridgeIswctypeL(int wc, uint32_t desc, void *locale)
    {
        return iswctype(wc, (wctype_t)desc);
    }

    size_t bridgeMbsrtowcs(uint32_t *dst, const char **src, size_t len, void *ps)
    {
        if (!src || !*src)
            return (size_t)-1;

        const char *s = *src;
        size_t count = 0;
        mbstate_t localState;
        memset(&localState, 0, sizeof(localState));

        if (dst == nullptr)
        {
            while (*s)
            {
                wchar_t wc;
                size_t nb = mbrtowc(&wc, s, MB_CUR_MAX, &localState);
                if (nb == (size_t)-1 || nb == (size_t)-2)
                    return (size_t)-1;
                if (nb == 0)
                    break;
                s += nb;
                count++;
            }
            return count;
        }

        while (count < len)
        {
            if (*s == '\0')
            {
                dst[count] = 0;
                *src = nullptr;
                return count;
            }

            wchar_t wc;
            size_t nb = mbrtowc(&wc, s, MB_CUR_MAX, &localState);
            if (nb == (size_t)-1 || nb == (size_t)-2)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }

            dst[count] = (uint32_t)wc;
            s += (nb == 0) ? 1 : nb;
            count++;
        }

        *src = s;
        return count;
    }

    int bridgeIsinf(double x)
    {
        return std::isinf(x) ? 1 : 0;
    }

    int bridgeIsnan(double x)
    {
        return std::isnan(x) ? 1 : 0;
    }

    int bridgeIsnanf(float x)
    {
        return std::isnan(x) ? 1 : 0;
    }

    int bridgeWcscoll_l(const uint32_t *s1, const uint32_t *s2, void *locale)
    {
        size_t len1 = 0, len2 = 0;
        while (s1[len1])
            len1++;
        while (s2[len2])
            len2++;

        wchar_t *w1 = (wchar_t *)alloca((len1 + 1) * sizeof(wchar_t));
        wchar_t *w2 = (wchar_t *)alloca((len2 + 1) * sizeof(wchar_t));

        for (size_t i = 0; i <= len1; i++)
            w1[i] = (wchar_t)s1[i];
        for (size_t i = 0; i <= len2; i++)
            w2[i] = (wchar_t)s2[i];

        return wcscoll(w1, w2);
    }

    size_t bridgeWcsxfrm_l(uint32_t *dst, const uint32_t *src, size_t n, void *locale)
    {
        size_t srcLen = 0;
        while (src[srcLen])
            srcLen++;

        wchar_t *wSrc = (wchar_t *)alloca((srcLen + 1) * sizeof(wchar_t));
        for (size_t i = 0; i <= srcLen; i++)
            wSrc[i] = (wchar_t)src[i];

        if (dst == nullptr || n == 0)
            return wcsxfrm(nullptr, wSrc, 0);

        wchar_t *wDst = (wchar_t *)alloca((n + 1) * sizeof(wchar_t));
        size_t result = wcsxfrm(wDst, wSrc, n);

        size_t copyLen = (result < n) ? result : n - 1;
        for (size_t i = 0; i < copyLen; i++)
            dst[i] = (uint32_t)wDst[i];
        if (n > 0)
            dst[copyLen] = 0;

        return result;
    }

    int bridgeTowlower_l(int wc, void *locale)
    {
        return (int)towlower((wint_t)wc);
    }

    int bridgeTowupper_l(int wc, void *locale)
    {
        return (int)towupper((wint_t)wc);
    }

#define LINUX_CODESET 14

    char *bridgeNlLanginfo(int item)
    {
        static char codeset[] = "ANSI_X3.4-1968";
        static char empty[] = "";

        if (item == LINUX_CODESET)
            return codeset;
        return empty;
    }

    char *bridgeGettext(const char *msgid)
    {
        return const_cast<char *>(msgid);
    }

    char *bridgeDgettext(const char *domainname, const char *msgid)
    {
        return const_cast<char *>(msgid);
    }

    int bridgePrintf(const char *format, ...)
    {
        va_list args;
        va_start(args, format);

        log_trace("Intercepted printf");
        int ret = ::vprintf(format, args);

        va_end(args);
        return ret;
    }

    int bridgePuts(const char *str)
    {
        log_trace("Intercepted puts");
        return ::puts(str);
    }

    int bridgeFprintf(void *stream, const char *format, ...)
    {
        va_list args;
        va_start(args, format);

        log_trace("Intercepted fprintf");
        int ret = ::vfprintf((FILE *)stream, format, args);

        va_end(args);
        return ret;
    }

    int bridgeSprintf(char *buffer, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        log_trace("Intercepted sprintf");
        int ret = ::vsprintf(buffer, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSnprintf(char *buffer, size_t count, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        log_trace("Intercepted snprintf");
        if (format == NULL)
            return 0;
        int ret = _vsnprintf(buffer, count, format, args);
        va_end(args);
        return ret;
    }

    int bridgeVprintf(const char *format, va_list args)
    {
        log_trace("Intercepted vprintf");
        return ::vprintf(format, args);
    }

    int bridgeVfprintf(void *stream, const char *format, va_list args)
    {
        log_trace("Intercepted vfprintf");
        if (stream == nullptr)
        {
            stream = stdout;
        }
        return ::vfprintf((FILE *)stream, format, args);
    }

    int bridgeVsprintf(char *buffer, const char *format, va_list args)
    {
        log_trace("Intercepted vsprintf");
        return ::vsprintf(buffer, format, args);
    }

    int bridgeVsnprintf(char *buffer, size_t count, const char *format, va_list args)
    {
        log_trace("Intercepted vsnprintf");
        return ::vsnprintf(buffer, count, format, args);
    }

    int bridgeFscanf(FILE *stream, const char *format, ...)
    {
        if (!stream)
            return 0;

        va_list args;
        va_start(args, format);
        int ret = 0;
        ret = vfscanf(stream, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSscanf(const char *str, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        int ret = vsscanf(str, format, args);
        va_end(args);
        return ret;
    }

    char *bridgeIndex(const char *str, int c)
    {
        log_trace("Intercepted index");
        return strchr(str, c);
    }

#define FNM_NOMATCH 1
#define FNM_NOESCAPE 0x01
#define FNM_PATHNAME 0x02
#define FNM_PERIOD 0x04

    int bridgeFnmatch(const char *pattern, const char *string, int flags)
    {
        const char *p = pattern, *n = string;

        if ((flags & FNM_PERIOD) && *n == '.' && *p != '.')
            return FNM_NOMATCH;

        while (*p)
        {
            if (*p == '*')
            {
                while (*p == '*')
                    p++;
                if (!*p)
                    return 0;
                while (*n)
                {
                    if (bridgeFnmatch(p, n, flags & ~FNM_PERIOD) == 0)
                        return 0;
                    if ((flags & FNM_PATHNAME) && *n == '/')
                        break;
                    n++;
                }
                return FNM_NOMATCH;
            }
            else if (*p == '?')
            {
                if (!*n || ((flags & FNM_PATHNAME) && *n == '/'))
                    return FNM_NOMATCH;
                p++;
                n++;
            }
            else if (*p == '[')
            {
                if (!*n || ((flags & FNM_PATHNAME) && *n == '/'))
                    return FNM_NOMATCH;
                p++;
                bool negate = (*p == '!' || *p == '^');
                if (negate)
                    p++;
                bool match = false;
                while (*p && *p != ']')
                {
                    if (p[1] == '-' && p[2] && p[2] != ']')
                    {
                        if (*n >= *p && *n <= p[2])
                            match = true;
                        p += 3;
                    }
                    else
                    {
                        if (*n == *p)
                            match = true;
                        p++;
                    }
                }
                if (!*p)
                    return FNM_NOMATCH;
                p++;
                if (match == negate)
                    return FNM_NOMATCH;
                n++;
            }
            else
            {
                if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
                    p++;
                if (*p != *n)
                    return FNM_NOMATCH;
                p++;
                n++;
            }
        }
        return *n ? FNM_NOMATCH : 0;
    }

    int32_t bridgeTime(int32_t *tloc)
    {
        log_trace("Intercepted time");
        time_t t = time(NULL);
        if (tloc)
        {
            *tloc = (int32_t)t;
        }
        return (int32_t)t;
    }

    int bridgeUtime(const char *filename, const struct linux_utimbuf *times)
    {
        log_trace("Intercepted utime");
        char winPath[MAX_PATH];
        ConvertPath(winPath, filename, MAX_PATH);
        return _utime(winPath, (struct _utimbuf *)times);
    }

    int bridgeGettimeofday(struct timeval *tv, void *tz)
    {
        log_trace("Intercepted gettimeofday");
        if (tv)
        {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            unsigned __int64 t = (unsigned __int64)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
            t -= 116444736000000000ULL;
            t /= 10;
            tv->tv_sec = (int32_t)(t / 1000000);
            tv->tv_usec = (int32_t)(t % 1000000);
        }
        return 0;
    }

    int bridgeSettimeofday(const struct timeval *tv, const void *tz)
    {
        log_trace("Intercepted settimeofday");
        // Convert Windows FILETIME to seconds since epoch
        ULARGE_INTEGER uli;
        uli.QuadPart = ((unsigned __int64)tv->tv_sec * 10000000 + (unsigned __int64)tv->tv_usec * 10) + 116444736000000000ULL;

        FILETIME ft;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;

        SYSTEMTIME st;
        if (FileTimeToSystemTime(&ft, &st))
        {
            SetSystemTime(&st);
            return 0;
        }
        return -1;
    }

    int bridgeUsleep(uint32_t microseconds)
    {
        log_trace("Intercepted usleep");
        Sleep(microseconds / 1000);
        return 0;
    }

    int bridgeSleep(uint32_t seconds)
    {
        log_trace("Intercepted sleep");
        Sleep(seconds * 1000);
        return 0;
    }

    int bridgeNanosleep(const struct timespec *req, struct timespec *rem)
    {
        long long duration_ns = (long long)req->tv_sec * 1000000000LL + req->tv_nsec;
        if (duration_ns <= 0)
            return 0;

        long long freq;
        QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
        long long wait_ticks = (duration_ns * freq) / 1000000000LL;

        long long start, current;
        QueryPerformanceCounter((LARGE_INTEGER *)&start);

        while (true)
        {
            QueryPerformanceCounter((LARGE_INTEGER *)&current);
            long long elapsed = current - start;
            if (elapsed >= wait_ticks)
                break;

            long long remaining_us = ((wait_ticks - elapsed) * 1000000LL) / freq;

            if (remaining_us > 2000)
            {
                Sleep(1);
            }
            else if (remaining_us > 100)
            {
                Sleep(0);
            }
        }

        return 0;
    }

    tm32 *bridgeLocaltime(const int32_t *timer)
    {
        static tm32 t32;
        time_t t = (time_t)*timer;
        struct tm *tm_ptr = localtime(&t);

        if (tm_ptr)
        {
            t32.tm_sec = tm_ptr->tm_sec;
            t32.tm_min = tm_ptr->tm_min;
            t32.tm_hour = tm_ptr->tm_hour;
            t32.tm_mday = tm_ptr->tm_mday;
            t32.tm_mon = tm_ptr->tm_mon;
            t32.tm_year = tm_ptr->tm_year;
            t32.tm_wday = tm_ptr->tm_wday;
            t32.tm_yday = tm_ptr->tm_yday;
            t32.tm_isdst = tm_ptr->tm_isdst;
            t32.tm_gmtoff = 0;
            t32.tm_zone = 0;
            return &t32;
        }
        return nullptr;
    }

    int bridgeClockGettime(int clk_id, struct timespec *tp)
    {
        log_trace("Intercepted clock_gettime");
        if (clk_id == CLOCK_REALTIME)
        {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            unsigned __int64 t = (unsigned __int64)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
            t -= 116444736000000000ULL;
            t /= 10;
            tp->tv_sec = (long)(t / 1000000);
            tp->tv_nsec = (long)(t % 1000000) * 1000;
            return 0;
        }
        return -1;
    }

    time_t bridgeMktime(struct tm *tm)
    {
        log_trace("Intercepted mktime");
        return mktime(tm);
    }

    struct tm32 *bridgeGmtime_R(const time_t *timep, struct tm32 *result)
    {
        log_trace("Intercepted gmtime_r");
        time_t t = *timep;
        struct tm *tm_ptr = gmtime(&t);
        if (tm_ptr)
        {
            result->tm_sec = tm_ptr->tm_sec;
            result->tm_min = tm_ptr->tm_min;
            result->tm_hour = tm_ptr->tm_hour;
            result->tm_mday = tm_ptr->tm_mday;
            result->tm_mon = tm_ptr->tm_mon;
            result->tm_year = tm_ptr->tm_year;
            result->tm_wday = tm_ptr->tm_wday;
            result->tm_yday = tm_ptr->tm_yday;
            result->tm_isdst = tm_ptr->tm_isdst;
            return result;
        }
        return nullptr;
    }

    size_t bridgeStrftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr)
    {
        log_trace("Intercepted strftime");
        return strftime(s, maxsize, format, timeptr);
    }

    void bridgeFtime(struct timeb *tp)
    {
        log_trace("Intercepted ftime");
        ftime(tp);
    }

    int bridgeSysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
    {
        log_trace("Intercepted sysctl");
        return -1;
    }

    int bridgeStubSuccess()
    {
        log_trace("Intercepted stub success");
        return 0;
    }

    int bridgeSystem(const char *command)
    {
        log_debug("system(\"%s\")", command);
        if (strcmp(command, "touch /var/tmp/mwlogo") == 0)
            command = "type nul > .\\tmp\\mwlogo";

        if (strcmp(command, "cd /tmp/segaboot > /dev/null") == 0)
            return -1;

        if (strcmp(command, "mkdir /tmp/segaboot > /dev/null") == 0)
            command = "md .\\tmp\\segaboot > nul";

        if (strcmp(command, "touch /tmp/segaboot/test") == 0)
            command = "type nul > .\\tmp\\segaboot\\test";

        if (strcmp(command, "touch /var/tmp/atr_init") == 0)
            command = "type nul > .\\tmp\\atr_init";

        if (strcmp(command, "touch /var/tmp/atr_err") == 0)
            command = "type nul > .\\tmp\\atr_err";

        if (strncmp(command, "touch /var/tmp/warning", 20) == 0)
            command = "type nul > .\\tmp\\warning";

        if (strncmp(command, "ifconfig eth0", 11) == 0)
            return 0;

        log_info("Intercepted system: %s", command ? command : "(null)");
        return system(command);
    }

    int bridgeSyslog(int priority, const char *format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        log_info("syslog: %s", buffer);
        return 0;
    }

    void bridgeOpenlog(const char *ident, int option, int facility)
    {
        log_info("openlog: %s %d %d", ident ? ident : "NULL", option, facility);
    }

    void bridgeCloselog()
    {
        log_info("closelog");
    }

    int bridgeWaitpid(int pid, int *wstatus, int options)
    {
        log_info("Intercepted waitpid(%d, %p, %d)", pid, wstatus, options);
        // For now, we just return a dummy status.
        // In a real implementation, you might want to hook into the Windows process
        // management to get the actual exit status.
        if (wstatus)
            *wstatus = 0; // 0 indicates normal termination
        return pid;       // Return the PID as if it terminated normally
    }

    pid_t bridgeGetuid(void)
    {
        log_info("Intercepted getuid");
        return 0;
    }

    int bridgeFork(void)
    {
        log_debug("fork() called - returning fake parent PID 1000");
        return 1000;
    }

    int bridgeVfork(void)
    {
        log_debug("vfork() called - returning fake parent PID 1000");
        return bridgeFork();
    }

    int bridgeDaemon(int nochdir, int noclose)
    {
        log_debug("daemon(%d, %d) called - stubbed success", nochdir, noclose);
        return 0;
    }

    int bridgeExeclp(const char *file, const char *arg, ...)
    {
        log_info("execlp(\"%s\", \"%s\", ...)", file, arg);
        return 0;
    }

    int bridgeRand_r(unsigned int *seedp)
    {
        log_debug("rand_r() called");
        *seedp = rand();
        return *seedp;
    }

    void (*bridgeSignal(int signum, void (*handler)(int)))(int)
    {
        log_debug("signal() called");

        // Map Linux signals to Windows signals where possible
        int win_sig = -1;
        switch (signum)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 5:                // SIGTRAP
                win_sig = SIGABRT; // Map to SIGABRT as Windows lacks SIGTRAP
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_info("signal: unsupported signal %d", signum);
                return (void (*)(int))-1; // SIG_ERR
        }

        return signal(win_sig, handler);
    }

    int bridgeRaise(int sig)
    {
        log_info("raise(%d) called", sig);
        int win_sig = -1;
        switch (sig)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 5: // SIGTRAP
                // Windows doesn't have a direct equivalent in csignal, mapping to SIGABRT
                win_sig = SIGABRT;
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_info("raise: unsupported signal %d", sig);
                return -1;
        }
        return ::raise(win_sig);
    }

    int bridgeSigfillset(void *set)
    {
        log_debug("sigfillset() called");
        memset(set, 0xFF, sizeof(uint32_t));
        return 0;
    }

    int bridgeSigemptyset(void *set)
    {
        log_debug("sigemptyset() called");
        memset(set, 0, sizeof(uint32_t));
        return 0;
    }

    int bridgeSigaction(int signum, const struct linux_sigaction *act, struct linux_sigaction *oldact)
    {
        log_debug("sigaction(%d) called", signum);

        int win_sig = -1;
        switch (signum)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 5:                // SIGTRAP
                win_sig = SIGABRT; // Map to SIGABRT
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_info("sigaction: unsupported signal %d", signum);
                return -1;
        }

        void (*old_handler)(int) = SIG_DFL;

        if (act)
        {
            old_handler = signal(win_sig, act->sa_handler);
            if (old_handler == SIG_ERR)
                return -1;
        }
        else if (oldact)
        {
            old_handler = signal(win_sig, SIG_DFL);
            signal(win_sig, old_handler);
        }

        if (oldact)
        {
            memset(oldact, 0, sizeof(struct linux_sigaction));
            oldact->sa_handler = old_handler;
        }

        return 0;
    }

    int bridgeKill(int pid, int sig)
    {
        int my_pid = _getpid();
        log_info("kill(%d, %d) called", pid, sig);

        if (pid == my_pid || pid == 0)
        {
            // Signal 0 is existence check
            if (sig == 0)
                return 0;

            // SIGKILL (9) or SIGTERM (15) -> Terminate self
            if (sig == 9 || sig == 15)
            {
                log_info("kill: Process requested self-termination");
                exit(0);
            }
        }

        return 0;
    }

    int bridgeWait(int *wstatus)
    {
        log_info("wait() called: No child processes to wait for. Returning ECHILD.");

        Sleep(10);

        if (wstatus)
            *wstatus = 0;
        errno = ECHILD;
        return -1;
    }

    int bridgeGetpagesize()
    {
        log_trace("Intercepted getpagesize");
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return sysInfo.dwPageSize;
    }

#define L_MAP_FAILED ((void *)-1)
#define L_MAP_ANONYMOUS 0x20
#define L_PROT_EXEC 0x4

    void *bridgeMmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
    {
        log_trace("Intercepted mmap: addr=%p, len=%zu, prot=%d, flags=%d, fd=%d, offset=%ld", addr, length, prot, flags, fd, (long)offset);

        if (flags & L_MAP_ANONYMOUS)
        {
            DWORD winProt = PAGE_READWRITE;
            if (prot & L_PROT_EXEC)
                winProt = PAGE_EXECUTE_READWRITE;

            void *ret = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, winProt);
            if (!ret)
                return L_MAP_FAILED;
            return ret;
        }

        // Basic fallback for file mapping
        void *ret = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (ret && fd >= 0)
        {
            log_info("mmap: Emulating file mapping by reading into VirtualAlloc memory (fd=%d)", fd);
            _lseek(fd, offset, SEEK_SET);
            _read(fd, ret, length);
        }
        return ret ? ret : L_MAP_FAILED;
    }

    int bridgeMunmap(void *addr, size_t length)
    {
        log_trace("Intercepted munmap: addr=%p, len=%zu", addr, length);
        if (VirtualFree(addr, 0, MEM_RELEASE))
        {
            return 0;
        }
        return -1;
    }

    int bridgeNice(int inc)
    {
        log_trace("Intercepted nice: inc=%d", inc);
        return 0;
    }

    int bridgePoll(struct pollfd *fds, int nfds, int timeout)
    {
        log_trace("Intercepted poll");

        for (int i = 0; i < nfds; i++)
        {
            fds[i].revents = 0;
        }
        return 0;
    }

    void bridgeBzero(void *s, size_t n)
    {
        log_debug("Intercepted bzero");
        memset(s, 0, n);
    }

    int bridgeWmemcmp(const uint32_t *s1, const uint32_t *s2, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (s1[i] < s2[i])
                return -1;
            if (s1[i] > s2[i])
                return 1;
        }
        return 0;
    }

    uint32_t *bridgeWmemcpy(uint32_t *dest, const uint32_t *src, size_t n)
    {
        for (size_t i = 0; i < n; i++)
            dest[i] = src[i];
        return dest;
    }

    uint32_t *bridgeWmemset(uint32_t *s, uint32_t c, size_t n)
    {
        for (size_t i = 0; i < n; i++)
            s[i] = c;
        return s;
    }

    uint32_t *bridgeWmemchr(const uint32_t *s, uint32_t c, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (s[i] == c)
                return (uint32_t *)(s + i);
        }
        return nullptr;
    }

    size_t bridgeWcslen(const uint32_t *s)
    {
        size_t len = 0;
        while (s[len])
            len++;
        return len;
    }

    uint32_t *bridgeWcscpy(uint32_t *dest, const uint32_t *src)
    {
        size_t i = 0;
        while ((dest[i] = src[i]) != 0)
            i++;
        return dest;
    }

    uint32_t *bridgeWcsncpy(uint32_t *dest, const uint32_t *src, size_t n)
    {
        size_t i;
        for (i = 0; i < n && src[i] != 0; i++)
            dest[i] = src[i];
        for (; i < n; i++)
            dest[i] = 0;
        return dest;
    }

    int bridgeWcscmp(const uint32_t *s1, const uint32_t *s2)
    {
        while (*s1 && (*s1 == *s2))
        {
            s1++;
            s2++;
        }
        return (*s1 > *s2) - (*s1 < *s2);
    }

    int bridgeWcscoll(const uint32_t *s1, const uint32_t *s2)
    {
        return bridgeWcscmp(s1, s2); // Basic fallback
    }

    int bridgeWcsncmp(const uint32_t *s1, const uint32_t *s2, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (s1[i] != s2[i])
                return (s1[i] > s2[i]) - (s1[i] < s2[i]);
            if (s1[i] == 0)
                break;
        }
        return 0;
    }

    uint32_t *bridgeWcschr(const uint32_t *s, uint32_t c)
    {
        while (*s != c)
        {
            if (!*s++)
                return nullptr;
        }
        return (uint32_t *)s;
    }

    uint32_t *bridgeWcsrchr(const uint32_t *s, uint32_t c)
    {
        const uint32_t *last = nullptr;
        do
        {
            if (*s == c)
                last = s;
        } while (*s++);
        return (uint32_t *)last;
    }

    uint32_t *bridgeWcsstr(const uint32_t *haystack, const uint32_t *needle)
    {
        if (!*needle)
            return (uint32_t *)haystack;
        for (; *haystack; haystack++)
        {
            if (*haystack == *needle)
            {
                const uint32_t *h = haystack, *n = needle;
                while (*h && *n && *h == *n)
                {
                    h++;
                    n++;
                }
                if (!*n)
                    return (uint32_t *)haystack;
            }
        }
        return nullptr;
    }

    double bridgeWcstod(const uint32_t *nptr, uint32_t **endptr)
    {
        char buf[256];
        size_t i = 0;
        while (nptr[i] && i < 255)
        {
            buf[i] = (char)nptr[i];
            i++;
        }
        buf[i] = 0;
        char *end = nullptr;
        double res = strtod(buf, &end);
        if (endptr)
            *endptr = (uint32_t *)(nptr + (end - buf));
        return res;
    }

    long bridgeWcstol(const uint32_t *nptr, uint32_t **endptr, int base)
    {
        char buf[256];
        size_t i = 0;
        while (nptr[i] && i < 255)
        {
            buf[i] = (char)nptr[i];
            i++;
        }
        buf[i] = 0;
        char *end = nullptr;
        long res = strtol(buf, &end, base);
        if (endptr)
            *endptr = (uint32_t *)(nptr + (end - buf));
        return res;
    }

    int bridgeWctob(uint32_t c)
    {
        return (c < 128) ? (int)c : -1;
    }

    uint32_t bridgeWctype(const char *property)
    {
        return (uint32_t)wctype(property);
    }

    size_t bridgeWcsrtombs(char *dst, const uint32_t **src, size_t len, void *ps)
    {
        if (!src || !*src)
            return (size_t)-1;
        const uint32_t *s = *src;
        size_t count = 0;
        if (!dst)
        {
            while (*s)
            {
                if (*s > 255)
                {
                    errno = EILSEQ;
                    return (size_t)-1;
                }
                count++;
                s++;
            }
            return count;
        }
        while (count < len)
        {
            if (*s == 0)
            {
                dst[count] = 0;
                *src = nullptr;
                return count;
            }
            if (*s > 255)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }
            dst[count++] = (char)*s++;
        }
        *src = s;
        return count;
    }

    size_t bridgeWcrtomb(char *s, uint32_t wc, void *ps)
    {
        if (!s)
            return 1;
        if (wc > 255)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        *s = (char)wc;
        return 1;
    }

    size_t bridgeWcsftime(uint32_t *s, size_t maxsize, const uint32_t *format, const struct tm *timeptr)
    {
        char fmt[256];
        char out[256];
        size_t i = 0;
        while (format[i] && i < 255)
        {
            fmt[i] = (char)format[i];
            i++;
        }
        fmt[i] = 0;
        size_t cap = (maxsize < 256) ? maxsize : 256;
        size_t res = strftime(out, cap, fmt, timeptr);
        for (i = 0; i < res; i++)
            s[i] = (uint32_t)out[i];
        if (maxsize > res)
            s[res] = 0;
        return res;
    }

    size_t bridgeWcsxfrm(uint32_t *dst, const uint32_t *src, size_t n)
    {
        size_t i = 0;
        while (src[i] && i < n)
        {
            if (dst)
                dst[i] = src[i];
            i++;
        }
        if (dst && i < n)
            dst[i] = 0;
        return bridgeWcslen(src);
    }

    size_t bridgeWcstombs(char *dst, const uint32_t *src, size_t len)
    {
        log_trace("Intercepted wcstombs(dst=%p, src=%p, len=%zu)", dst, src, len);
        if (!src)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        size_t count = 0;
        if (!dst)
        {
            // Count only: determine how many bytes would be written
            while (src[count])
            {
                if (src[count] > 255)
                {
                    errno = EILSEQ;
                    return (size_t)-1;
                }
                count++;
            }
            return count;
        }
        while (count < len && src[count])
        {
            if (src[count] > 255)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }
            dst[count] = (char)src[count];
            count++;
        }
        if (count < len)
            dst[count] = 0;
        return count;
    }

    size_t bridgeMbrtowc(uint32_t *pwc, const char *s, size_t n, void *ps)
    {
        if (!s)
            return 0;
        if (n == 0)
            return (size_t)-2;
        if (pwc)
            *pwc = (uint32_t)(unsigned char)*s;
        return (*s == 0) ? 0 : 1;
    }

    int bridgeMbtowc(uint32_t *pwc, const char *s, size_t n)
    {
        if (!s)
            return 0;
        if (n == 0)
            return -1;
        if (pwc)
            *pwc = (uint32_t)(unsigned char)*s;
        return (*s == 0) ? 0 : 1;
    }

    size_t bridgeMbstowcs(uint32_t *dest, const char *src, size_t n)
    {
        if (!src)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        size_t i = 0;
        if (!dest)
            return strlen(src);
        for (; i < n && src[i]; i++)
        {
            dest[i] = (uint32_t)(unsigned char)src[i];
        }
        if (i < n)
            dest[i] = 0;
        return i;
    }

    uint32_t bridgeBtowc(int c)
    {
        if (c == EOF)
            return (uint32_t)-1;
        return (uint32_t)(unsigned char)c;
    }

    uint32_t bridgePutwc(uint32_t wc, FILE *stream)
    {
        int r = fputc((int)(wc & 0xFF), stream);
        return (r == EOF) ? (uint32_t)-1 : wc;
    }

    uint32_t bridgeGetwc(FILE *stream)
    {
        int c = fgetc(stream);
        return (c == EOF) ? (uint32_t)-1 : (uint32_t)c;
    }

    uint32_t bridgeUngetwc(uint32_t wc, FILE *stream)
    {
        int r = ungetc((int)(wc & 0xFF), stream);
        return (r == EOF) ? (uint32_t)-1 : wc;
    }

    FILE *bridgePopen(const char *command, const char *type)
    {
        log_info("Intercepted popen: %s %s", command, type);
        return nullptr;
    }

    int bridgePclose(FILE *stream)
    {
        log_info("Intercepted pclose: %p", stream);
        return -1;
    }

    void bridgePerror(const char *s)
    {
        log_info("Intercepted perror: %s", s);
    }

    char *bridgeRealpath(const char *path, char *resolved_path)
    {
        if (!path || !resolved_path)
            return nullptr;

        char winPath[MAX_PATH];
        ConvertPath(winPath, path, MAX_PATH);

        char *result = _fullpath(resolved_path, winPath, MAX_PATH);
        if (!result)
            return nullptr;

        // Convert backslashes to forward slashes for Linux compatibility
        for (char *p = result; *p; p++)
        {
            if (*p == '\\')
                *p = '/';
        }

        return result;
    }

} // namespace LibcBridge

#endif