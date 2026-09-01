#include <string.h>
#if defined(_WIN32) || defined(__MINGW32__)

#include "symbolResolver.hpp"
#include "mathBridge.hpp"

#include <math.h>
#include <cmath>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace MathBridge
{
    void initBridges()
    {
        MAP("atan", bridge_atan);
        MAP("atan2", bridge_atan2);
        MAP("atanf", bridge_atanf);
        MAP("atan2f", bridge_atan2f);
        MAP("tan", bridge_tan);
        MAP("log", bridge_log);
        MAP("logf", bridge_logf);
        MAP("log10", bridge_log10);
        MAP("log10f", bridge_log10f);
        MAP("exp", bridge_exp);
        MAP("expf", bridge_expf);
        MAP("asin", bridge_asin);
        MAP("asinf", bridge_asinf);
        MAP("sin", bridge_sin);
        MAP("sinh", bridge_sinh);
        MAP("sinf", bridge_sinf);
        MAP("sinhf", bridge_sinhf);
        MAP("acos", bridge_acos);
        MAP("acosf", bridge_acosf);
        MAP("cos", bridge_cos);
        MAP("cosh", bridge_cosh);
        MAP("cosf", bridge_cosf);
        MAP("coshf", bridge_coshf);
        MAP("tan", bridge_tan);
        MAP("tanh", bridge_tanh);
        MAP("tanf", bridge_tanf);
        MAP("tanhf", bridge_tanhf);
        MAP("pow", bridge_pow);
        MAP("powf", bridge_powf);
        MAP("hypot", bridge_hypot);
        MAP("hypotf", bridge_hypotf);
        MAP("fmod", bridge_fmod);
        MAP("fmodf", bridge_fmodf);
        MAP("modf", bridge_modf);
        MAP("modff", bridge_modff);
        MAP("sqrt", bridge_sqrt);
        MAP("sqrtf", bridge_sqrtf);
        MAP("div", bridge_div);
        MAP("finitef", bridge_finitef);
        MAP("finite", bridge_finite);
        MAP("frexp", bridge_frexp);
        MAP("frexpf", bridge_frexpf);
        MAP("ldexpf", bridge_ldexpf);
        MAP("fabsf", bridge_fabsf);
    }
}   

extern "C" double bridge_log(double x)
{
    return ::log(x);
}
extern "C" float bridge_logf(float x)
{
    return ::logf(x);
}
extern "C" double bridge_log10(double x)
{
    return ::log10(x);
}
extern "C" float bridge_log10f(float x)
{
    return ::log10f(x);
}
extern "C" double bridge_exp(double x)
{
    return ::exp(x);
}
extern "C" float bridge_expf(float x)
{
    return ::expf(x);
}
extern "C" double bridge_asin(double x)
{
    return ::asin(x);
}
extern "C" float bridge_asinf(float x)
{
    return ::asinf(x);
}
extern "C" double bridge_sin(double x)
{
    return ::sin(x);
}
extern "C" double bridge_sinh(double x)
{
    return ::sinh(x);
}
extern "C" float bridge_sinf(float x)
{
    return ::sinf(x);
}
extern "C" float bridge_sinhf(float x)
{
    return ::sinhf(x);
}
extern "C" double bridge_acos(double x)
{
    return ::acos(x);
}
extern "C" float bridge_acosf(float x)
{
    return ::acosf(x);
}
extern "C" double bridge_cos(double x)
{
    return ::cos(x);
}
extern "C" double bridge_cosh(double x)
{
    return ::cosh(x);
}
extern "C" float bridge_cosf(float x)
{
    return ::cosf(x);
}
extern "C" float bridge_coshf(float x)
{
    return ::coshf(x);
}
extern "C" double bridge_atan(double x)
{
    return ::atan(x);
}
extern "C" double bridge_atan2(double y, double x)
{
    return ::atan2(y, x);
}
extern "C" float bridge_atanf(float x)
{
    return ::atanf(x);
}
extern "C" float bridge_atan2f(float y, float x)
{
    return ::atan2f(y, x);
}
extern "C" double bridge_tan(double x)
{
    return ::tan(x);
}
extern "C" double bridge_tanh(double x)
{
    return ::tanh(x);
}
extern "C" float bridge_tanf(float x)
{
    return ::tanf(x);
}
extern "C" float bridge_tanhf(float x)
{
    return ::tanhf(x);
}
extern "C" double bridge_pow(double x, double y)
{
    return ::pow(x, y);
}
extern "C" float bridge_powf(float x, float y)
{
    return ::powf(x, y);
}
extern "C" double bridge_hypot(double x, double y)
{
    return ::hypot(x, y);
}
extern "C" float bridge_hypotf(float x, float y)
{
    return ::hypotf(x, y);
}
extern "C" double bridge_fmod(double x, double y)
{
    return ::fmod(x, y);
}
extern "C" float bridge_fmodf(float x, float y)
{
    return ::fmodf(x, y);
}
extern "C" double bridge_modf(double x, double *iptr)
{
    return ::modf(x, iptr);
}
extern "C" float bridge_modff(float x, float *iptr)
{
    return ::modff(x, iptr);
}
extern "C" double bridge_sqrt(double x)
{
    return ::sqrt(x);
}
extern "C" float bridge_sqrtf(float x)
{
    return ::sqrtf(x);
}
extern "C" div_t bridge_div(int numerator, int denominator)
{
    return ::div(numerator, denominator);
}
extern "C" int bridge_finite(double x)
{
    return std::isfinite(x) ? 1 : 0;
}
extern "C" int bridge_finitef(float x)
{
    return std::isfinite(x) ? 1 : 0;
}
extern "C" double bridge_frexp(double x, int *exp)
{
    return ::frexp(x, exp);
}
extern "C" float bridge_frexpf(float x, int *exp)
{
    return ::frexpf(x, exp);
}
extern "C" float bridge_ldexpf(float x, int exp)
{
    return ::ldexpf(x, exp);
}

extern "C" float bridge_fabsf(float x)
{
    return ::fabsf(x);
}

#endif
