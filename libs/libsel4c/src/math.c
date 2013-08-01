/* @LICENSE(NICTA_CORE) */

#include "math.h"

double
fabs(double x)
{
    return x > 0.0 ? x : -x;
}

float
fabsf(float x)
{
    return x > 0.0 ? x : -x;
}

static double
exp_approx(double x)
{
    /* Approximate fractional part of exponent for exp. */
    double x2 = (x *x ), x3 = (x2*x ), x4 = (x2*x2), x5 = (x3*x2), x6 = (x3*x3),
           x7 = (x5*x2), x8 = (x4*x4), x9 = (x5*x4), x10= (x5*x5), x11= (x6*x5);
    return (1.0 + x + 0.5*x2 +
            0.16666666666666666666666666666666666666666666666667 * x3 +
            0.04166666666666666666666666666666666666666666666667 * x4 + 
            0.00833333333333333333333333333333333333333333333333 * x5 +
            0.00138888888888888888888888888888888888888888888889 * x6 +
            0.00019841269841269841269841269841269841269841269841 * x7 +
            0.00002480158730158730158730158730158730158730158730 * x8 +
            0.00000275573192239858906525573192239858906525573192 * x9 +
            0.00000027557319223985890652557319223985890652557319 * x10 +
            0.00000002505210838544171877505210838544171877505211 * x11);
}

double
exp(double x)
{
    /* Square-exponentiate integer part. */
    char recp = 0;
    if ( x < 0 ) {
        recp = 1;
        x = -x;
    }
    unsigned long e = (unsigned long) x;
    double r = 1.0, b = M_E;
    while (e) {
        if (e & 1) r *= b;
        b *= b;
        e >>= 1;
    }
    double v = r * exp_approx(x - ((unsigned long)x));
    return recp ? (1.0 / v) : v;
}

double
fmod(double n, double d)
{
    return n - (double)(((long long)(n / d)) * d);
}

float
fmodf(float n, float d)
{
    return n - (float)(((long long)(n / d)) * d);
}

double
log(double x)
{
    return ((double)log2f(x)) * M_LN2;
}

float
logf(float x)
{
    return log2f(x) * M_LN2;
}

double
log10(double x)
{
    return (double)log2f(x) * M_LN10;
}

float
log10f(float x)
{
    return log2f(x) * M_LN10;
}

double
log2(double x)
{
    return (double)log2f(x);
}

float
log2f(float v) {
    /* Source: http://www.flipcode.com/archives/Fast_log_Function.shtml */
    int* exp_ptr = ((int*)(&v));
    int x = *exp_ptr;
    const int log_2 = ((x >> 23) & 255) - 128;
    x &= ~(255 << 23);
    x += 127 << 23;
    (*exp_ptr) = x;
    v = ((-1.0f/3) * v + 2) * v - 2.0f/3;
    return (v + log_2);
}

double
pow(double b, double x)
{
    return exp(x * log(b));
}

float
powf(float b, float x)
{
    return (float)pow(b, x);
}

double
cos(double x)
{
    x = fmod(fabs(x) + M_PI, M_PI * 2.0) - M_PI;
    double x2 = (x*x),   x4 = (x2*x2), x6 = (x4*x2),
           x8 = (x4*x4), x10 =(x6*x4), x12 =(x6*x6);
    return (1.0 - 0.5 * x2 +
            0.04166666666666666666666666666666666666666666666667 * x4 -
            0.00138888888888888888888888888888888888888888888889 * x6 +
            0.00002480158730158730158730158730158730158730158730 * x8 -
            0.00000027557319223985890652557319223985890652557319 * x10 +
            0.00000000208767569878680989792100903212014323125434 * x12);
}

float
cosf(float x)
{
    return (float)(cos((double)(x)));
}

double
sin(double x)
{
    x = x < 0.0 ? -x + M_PI : x;
    x = fmod(x + M_PI, M_PI * 2.0) - M_PI;
    double x2 = (x*x),   x3 = (x2*x),  x5 = (x3*x2),
           x7 = (x5*x2), x9 = (x7*x2), x11 = (x9*x2);
    return (x - 
            0.16666666666666666666666666666666666666666666666667 * x3 +
            0.00833333333333333333333333333333333333333333333333 * x5 -
            0.00019841269841269841269841269841269841269841269841 * x7 +
            0.00000275573192239858906525573192239858906525573192 * x9 -
            0.00000002505210838544171877505210838544171877505211 * x11);
}

float
sinf(float x)
{
    return (float)(sin((double)(x)));
}

double
tan(double x)
{
    double c = cos(x);
    return (sin(x) / c);
}

float
tanf(float x)
{
    return (float)(tan((double)(x)));
}


