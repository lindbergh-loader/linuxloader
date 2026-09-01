#pragma once

#ifdef __cplusplus
extern "C" {
#endif



#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <_timeval.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>
#include <sys/types.h>
#include <windows.h>
#include <locale.h>

struct tm32
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    int32_t tm_gmtoff;
    uint32_t tm_zone;
};



struct linux_sigaction {
    void (*sa_handler)(int);
    uint32_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

namespace LibcBridge
{
    void initBridges();
    int bridgeSetjmp(int *env);

    // STDIO interceptions
    int bridgePrintf(const char *format, ...);
    int bridgePuts(const char *str);
    int bridgeFprintf(void *stream, const char *format, ...);
    int bridgeSprintf(char *buffer, const char *format, ...);
    int bridgeSnprintf(char *buffer, size_t count, const char *format, ...);
    int bridgeVprintf(const char *format, va_list args);
    int bridgeVfprintf(void *stream, const char *format, va_list args);
    int bridgeVsprintf(char *buffer, const char *format, va_list args);
    int bridgeVsnprintf(char *buffer, size_t count, const char *format, va_list args);
    int bridgeFscanf(FILE *stream, const char *format, ...);
    int bridgeSscanf(const char *str, const char *format, ...);
    char *bridgeIndex(const char *str, int c);
    int bridgeFnmatch(const char *pattern, const char *string, int flags);
    char *bridgeRealpath(const char *path, char *resolved_path);
    FILE *bridgePopen(const char *command, const char *type);
    int bridgePclose(FILE *stream);
    void bridgePerror(const char *s);


    // Time interceptions
    int32_t bridgeTime(int32_t *tloc);
    int bridgeGettimeofday(struct timeval *tv, void *tz);
    int bridgeSettimeofday(const struct timeval *tv, const void *tz);
    tm32 *bridgeLocaltime(const int32_t *timer);
    int bridgeUtime(const char *filename, const struct linux_utimbuf *times);
    int bridgeUsleep(uint32_t microseconds);
    int bridgeSleep(uint32_t seconds);
    int bridgeNanosleep(const struct timespec *req, struct timespec *rem);
    int bridgeClockGettime(int clk_id, struct timespec *tp);
    time_t bridgeMktime(struct tm *tm);
    struct tm32 *bridgeGmtime_R(const time_t *timep, struct tm32 *result);
    size_t bridgeStrftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr);
    void bridgeFtime(struct timeb *tp);

    void bridgeAbort();
    void bridgeExit(int status);
    int bridgeGetpagesize();
    void *bridgeMmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    int bridgeMunmap(void *addr, size_t length);

    int bridgeCxaAtexit(void (*func)(void *), void *arg, void *dso_handle);
    int bridgeCxaThreadAtexitImpl(void (*func)(void *), void *arg, void *dso_handle);
    void bridgeRegisterFrameInfoBases(void *begin, void *ob, void *tbase, void *dbase);
    void bridgeRegisterFrameInfo(void *begin, void *ob);
    void *bridgeDeregisterFrameInfo(void *begin);
    void bridgeCxaThrow(void *thrown_exception, void *tinfo, void (*dest)(void *));
    void *bridgeCxaAllocateException(size_t thrown_size);
    void bridgeCxaFreeException(void *thrown_exception);

    char *bridgeSetlocale(int category, const char *locale);
    void *bridgeNewlocale(int category_mask, const char *locale, void *base);
    void bridgeFreelocale(void *loc);
    void *bridgeUselocale(void *loc);
    struct lconv *bridgeLocaleconv(void);

    uint32_t bridgeWctypeL(const char *property, void *locale);
    int bridgeIswctypeL(int wc, uint32_t desc, void *locale);
    size_t bridgeMbsrtowcs(uint32_t *dst, const char **src, size_t len, void *ps);

    char *bridgeGettext(const char *msgid);
    char *bridgeDgettext(const char *domainname, const char *msgid);

    void *bridgeVectorData(void *vec_this);
    void *bridgeOstreamString(void *ostream_this, const char *str);

    int bridgeIsinf(double x);
    int bridgeIsnan(double x);
    int bridgeIsnanf(float x);
    int bridgeWcscoll_l(const uint32_t *s1, const uint32_t *s2, void *locale);
    size_t bridgeWcsxfrm_l(uint32_t *dst, const uint32_t *src, size_t n, void *locale);
    int bridgeTowlower_l(int wc, void *locale);
    int bridgeTowupper_l(int wc, void *locale);
    char *bridgeNlLanginfo(int item);


    int bridgeSysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
    int bridgeStubSuccess();

    int bridgeSyslog(int priority, const char *format, ...);
    void bridgeOpenlog(const char *ident, int option, int facility);
    void bridgeCloselog();

    int bridgeSystem(const char *command);

    int bridgeFork(void);
    int bridgeVfork(void);
    int bridgeDaemon(int nochdir, int noclose);
    int bridgeExeclp(const char *file, const char *arg, ...);
    int bridgeRand_r(unsigned int *seedp);
    int bridgeKill(int pid, int sig);
    int bridgeWait(int *wstatus);
    int bridgeRaise(int sig);
    int bridgePoll(struct pollfd *fds, int nfds, int timeout);

    void (*bridgeSignal(int signum, void (*handler)(int)))(int);
    int bridgeSigfillset(void *set);
    int bridgeSigemptyset(void *set);
    int bridgeSigaction(int signum, const struct linux_sigaction *act, struct linux_sigaction *oldact);
    int bridgeNice(int inc);

    // Wide char memory and string functions
    int bridgeWmemcmp(const uint32_t *s1, const uint32_t *s2, size_t n);
    uint32_t *bridgeWmemcpy(uint32_t *dest, const uint32_t *src, size_t n);
    uint32_t *bridgeWmemset(uint32_t *s, uint32_t c, size_t n);
    uint32_t *bridgeWmemchr(const uint32_t *s, uint32_t c, size_t n);
    size_t bridgeWcslen(const uint32_t *s);
    uint32_t *bridgeWcscpy(uint32_t *dest, const uint32_t *src);
    uint32_t *bridgeWcsncpy(uint32_t *dest, const uint32_t *src, size_t n);
    int bridgeWcscmp(const uint32_t *s1, const uint32_t *s2);
    int bridgeWcscoll(const uint32_t *s1, const uint32_t *s2);
    int bridgeWcsncmp(const uint32_t *s1, const uint32_t *s2, size_t n);
    uint32_t *bridgeWcschr(const uint32_t *s, uint32_t c);
    uint32_t *bridgeWcsrchr(const uint32_t *s, uint32_t c);
    uint32_t *bridgeWcsstr(const uint32_t *haystack, const uint32_t *needle);
    double bridgeWcstod(const uint32_t *nptr, uint32_t **endptr);
    long bridgeWcstol(const uint32_t *nptr, uint32_t **endptr, int base);
    int bridgeWctob(uint32_t c);
    uint32_t bridgeWctype(const char *property);
    size_t bridgeWcsrtombs(char *dst, const uint32_t **src, size_t len, void *ps);
    size_t bridgeWcrtomb(char *s, uint32_t wc, void *ps);
    size_t bridgeWcsftime(uint32_t *s, size_t maxsize, const uint32_t *format, const struct tm *timeptr);
    size_t bridgeWcsxfrm(uint32_t *dst, const uint32_t *src, size_t n);
    size_t bridgeWcstombs(char *dst, const uint32_t *src, size_t len);
    size_t bridgeMbrtowc(uint32_t *pwc, const char *s, size_t n, void *ps);
    int bridgeMbtowc(uint32_t *pwc, const char *s, size_t n);
    size_t bridgeMbstowcs(uint32_t *dest, const char *src, size_t n);
    uint32_t bridgeBtowc(int c);
    uint32_t bridgePutwc(uint32_t wc, FILE *stream);
    uint32_t bridgeGetwc(FILE *stream);
    uint32_t bridgeUngetwc(uint32_t wc, FILE *stream);

    int bridgeWaitpid(int pid, int *wstatus, int options);
    pid_t bridgeGetuid(void);

    void bridgeBzero(void *s, size_t n);
} // namespace LibcBridge

#endif
