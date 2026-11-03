/* math.h for Moscow's C libraries (lib/gui/libc.c) */
#ifndef MOSCOW_MATH_H
#define MOSCOW_MATH_H

#define M_PI 3.14159265358979323846
#define HUGE_VAL __builtin_huge_val()
#define isfinite(x) __builtin_isfinite(x)
#define isnan(x) __builtin_isnan(x)

double floor(double x);
double ceil(double x);
double fabs(double x);
double sqrt(double x);
double sin(double x);
double cos(double x);
double fmod(double x, double y);
float floorf(float x);
float ceilf(float x);
float fabsf(float x);
float sqrtf(float x);

#endif
