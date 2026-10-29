#ifndef _MATH_H
#define _MATH_H
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define HUGE_VAL (1e300 * 1e300)
#define INFINITY (1e300 * 1e300)
#define NAN (__nan())
double __nan(void);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double pow(double x, double y);
double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double rint(double x);
double nearbyint(double x);
long lrint(double x);
double trunc(double x);
double fmod(double x, double y);
double modf(double x, double *ip);
double frexp(double x, int *e);
double ldexp(double x, int e);
double hypot(double x, double y);
double cbrt(double x);
double fmin(double a, double b);
double fmax(double a, double b);
long lround(double x);
int isnan(double x);
int isinf(double x);
int isfinite(double x);
#define sqrtf sqrt
#define fabsf fabs
#define sinf sin
#define cosf cos
#define tanf tan
#define atan2f atan2
#define floorf floor
#define ceilf ceil
#define powf pow
#define expf exp
#define logf log
#define fmodf fmod
#define roundf round
#endif
