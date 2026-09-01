#pragma once

#include <stdint.h>
#include <stddef.h>

enum
{
    _ISupper = 0x100,
    _ISlower = 0x200,
    _ISalpha = 0x400,
    _ISdigit = 0x800,
    _ISxdigit = 0x1000,
    _ISspace = 0x2000,
    _ISprint = 0x4000,
    _ISgraph = 0x8000,
    _ISblank = 0x1,
    _IScntrl = 0x2,
    _ISpunct = 0x4,
    _ISalnum = 0x8
};

namespace GccBridge
{
    void initBridges();

    // Ctype table accessors — used by libcBridge to populate fake locale structs
    // so GCC 3.x locale/ctype code gets valid table pointers instead of NULL.
    const unsigned short *GetCtypeBPtr();
    const int32_t *GetCtypeTolowerPtr();
    const int32_t *GetCtypeToUpperPtr();
} // namespace GccBridge


extern "C"
{
    double __strtod_internal(const char *n, char **e, int g);
    long __strtol_internal(const char *n, char **e, int b, int g);
    unsigned long __strtoul_internal(const char *n, char **e, int b, int g);
    float bridge__strtof_internal(const char *n, char **e, int g);
    long long __strtoll_internal(const char *n, char **e, int b, int g);

    const unsigned short **__ctype_b_loc(void);
    const int32_t **__ctype_tolower_loc(void);
    const int32_t **__ctype_toupper_loc(void);
    size_t __ctype_get_mb_cur_max();

    void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);
    int *__errno_location();
    void __libc_freeres();
}
