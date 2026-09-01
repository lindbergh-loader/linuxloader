
#include <cstdlib>
#include <stddef.h>

namespace MathBridge
{
    void initBridges();

    extern "C" double bridge_atan(double x);
    extern "C" float bridge_atanf(float x);
    extern "C" double bridge_atan2(double y, double x);
    extern "C" float bridge_atan2f(float y, float x);
    extern "C" double bridge_fmod(double x, double y);
    extern "C" float bridge_fmodf(float x, float y);
    extern "C" double bridge_tan(double x);
    extern "C" double bridge_log(double x);
    extern "C" float bridge_logf(float x);
    extern "C" double bridge_log10(double x);
    extern "C" float bridge_log10f(float x);
    extern "C" double bridge_exp(double x);
    extern "C" float bridge_expf(float x);
    extern "C" double bridge_sin(double x);
    extern "C" double bridge_sinh(double x);
    extern "C" float bridge_sinf(float x);
    extern "C" float bridge_sinhf(float x);
    extern "C" double bridge_cos(double x);
    extern "C" double bridge_cosh(double x);
    extern "C" float bridge_cosf(float x);
    extern "C" float bridge_coshf(float x);
    extern "C" double bridge_tanh(double x);
    extern "C" float bridge_tanf(float x);
    extern "C" float bridge_tanhf(float x);
    extern "C" double bridge_asin(double x);
    extern "C" float bridge_asinf(float x);
    extern "C" double bridge_acos(double x);
    extern "C" float bridge_acosf(float x);
    extern "C" double bridge_pow(double x, double y);
    extern "C" float bridge_powf(float x, float y);
    extern "C" double bridge_hypot(double x, double y);
    extern "C" float bridge_hypotf(float x, float y);
    extern "C" double bridge_modf(double x, double *iptr);
    extern "C" float bridge_modff(float x, float *iptr);
    extern "C" double bridge_sqrt(double x);
    extern "C" float bridge_sqrtf(float x);

    extern "C" div_t bridge_div(int numerator, int denominator);
    extern "C" int bridge_finite(double x);
    extern "C" int bridge_finitef(float x);
    extern "C" double bridge_frexp(double x, int *exp);
    extern "C" float bridge_frexpf(float x, int *exp);
    extern "C" float bridge_ldexpf(float x, int exp);
    extern "C" float bridge_fabsf(float x);
}