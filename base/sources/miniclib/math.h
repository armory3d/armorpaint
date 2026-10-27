#pragma once

double fabs(double n);
float  fabsf(float n);
double fmax(double x, double y);
float  fmaxf(float x, float y);
double fmin(double x, double y);
float  fminf(float x, float y);
double ldexp(double x, int exp);
double pow(double base, double exponent);
float  powf(float base, float exponent);
double floor(double x);
float  floorf(float x);
double ceil(double x);
float  ceilf(float x);
double round(double x);
float  roundf(float x);
double sin(double x);
float  sinf(float x);
double asin(double x);
float  asinf(float x);
double cos(double x);
float  cosf(float x);
double acos(double x);
float  acosf(float x);
double tan(double x);
float  tanf(float x);
double atan(double x);
float  atanf(float x);
double atan2(double x, double y);
float  atan2f(float x, float y);
double log(double x);
float  logf(float x);
double log2(double x);
float  log2f(float x);
double exp(double x);
float  expf(float x);
double exp2(double x);
float  exp2f(float x);
double frexp(double x, int *exp);
float  frexpf(float x, int *exp);
double sqrt(double x);
float  sqrtf(float x);
double fmod(double x, double y);
float  fmodf(float x, float y);

int isnan(float x);
int isfinite(float x);
#define NAN (0.0F / 0.0F)
