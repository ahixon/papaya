/* @LICENSE(NICTA_CORE) */

/*
  Author: Xi (Ma) Chen
  Description:
    Provides implementations for common maths functions
    in libc.
*/

#ifndef _MATH_H_
#define _MATH_H_

#ifndef FLT_EVAL_METHOD
    #define FLT_EVAL_METHOD 0
#endif /* FLT_EVAL_METHOD */

#if FLT_EVAL_METHOD == 0
    typedef float float_t;
    typedef double double_t;
#elif FLT_EVAL_METHOD == 1
    typedef double float_t;
    typedef double double_t; 
#elif FLT_EVAL_METHOD == 2
    typedef long double float_t;
    typedef long double double_t; 
#endif /* FLT_EVAL_METHOD */

/* --- Symbolic Constants --- */

#define M_E         (2.7182818284590452353602874713526624977572)
#define M_LOG2E     (1.4426950408889634073599246810018921374266)
#define M_LOG10E    (0.4342944819032518276511289189166050822944)
#define M_LN2       (0.6931471805599453094172321214581765680755)
#define M_LN10      (2.3025850929940456840179914546843642076011)
#define M_PI        (3.1415926535897932384626433832795028841971)
#define M_PI_2      (1.5707963267948966192313216916397514420985)
#define M_PI_4      (0.7853981633974483096156608458198757210492)
#define M_1_PI      (0.3183098861837906715377675267450287240689)
#define M_2_PI      (0.6366197723675813430755350534900574481379)
#define M_2_SQRTPI  (1.1283791670955125738961589031215451716881)
#define M_SQRT2     (1.4142135623730950488016887242096980785697)
#define M_SQRT1_2   (0.7071067811865475244008443621048490392848)

/* --- Maths Functions --- */

double      fabs(double);

float       fabsf(float);

double      exp(double);

float       expf(float);

double      fmod(double, double);

float       fmodf(float, float);

double      log(double);

float       logf(float);

double      log10(double);

float       log10f(float);

double      log2(double);

float       log2f(float);

double      pow(double, double);

float       powf(float, float);

double      cos(double);

float       cosf(float);

double      sin(double);

float       sinf(float);

double      tan(double);

float       tanf(float);


#endif /* _MATH_H_ */
