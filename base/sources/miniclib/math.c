#include "math.h"

#ifdef IRON_WASM
__attribute__((import_module("imports"), import_name("js_pow"))) double   js_pow(double base, double exponent);
__attribute__((import_module("imports"), import_name("js_sin"))) double   js_sin(double x);
__attribute__((import_module("imports"), import_name("js_cos"))) double   js_cos(double x);
__attribute__((import_module("imports"), import_name("js_tan"))) double   js_tan(double x);
__attribute__((import_module("imports"), import_name("js_log"))) double   js_log(double x);
__attribute__((import_module("imports"), import_name("js_exp"))) double   js_exp(double x);
__attribute__((import_module("imports"), import_name("js_sqrt"))) double  js_sqrt(double x);
__attribute__((import_module("imports"), import_name("js_acos"))) double  js_acos(double x);
__attribute__((import_module("imports"), import_name("js_asin"))) double  js_asin(double x);
__attribute__((import_module("imports"), import_name("js_atan"))) double  js_atan(double x);
__attribute__((import_module("imports"), import_name("js_atan2"))) double js_atan2(double x, double y);
#endif

double fabs(double n) {
	return n < 0 ? -n : n;
}

float fabsf(float n) {
	return n < 0 ? -n : n;
}

double fmax(double x, double y) {
	return x > y ? x : y;
}

float fmaxf(float x, float y) {
	return x > y ? x : y;
}

double fmin(double x, double y) {
	return x < y ? x : y;
}

float fminf(float x, float y) {
	return x < y ? x : y;
}

static double exp2_int(int exp) {
	union {
		double             d;
		unsigned long long u;
	} v;
	v.u = (unsigned long long)(exp + 1023) << 52;
	return v.d;
}

double ldexp(double x, int exp) {
	while (exp > 1023) {
		x *= exp2_int(1023);
		exp -= 1023;
	}
	while (exp < -1022) {
		x *= exp2_int(-1022);
		exp += 1022;
	}
	return x * exp2_int(exp);
}

double pow(double base, double exponent) {
#ifdef IRON_WASM
	return js_pow(base, exponent);
#endif
	if (exponent == 0.0)
		return 1.0;
	if (exponent < 0.0)
		return 1.0 / pow(base, -exponent);
	double result = 1.0;
	int    exp    = (int)exponent;
	for (int i = 0; i < exp; i++)
		result *= base;
	return result;
}

float powf(float base, float exponent) {
#ifdef IRON_WASM
	return js_pow(base, exponent);
#endif
	return (float)pow(base, exponent);
}

double floor(double x) {
	return (double)(int)x - (x < 0.0 && x != (int)x ? 1 : 0);
}

float floorf(float x) {
	return (float)floor(x);
}

double ceil(double x) {
	return (double)(int)x + (x > 0.0 && x != (int)x ? 1 : 0);
}

float ceilf(float x) {
	return (float)ceil(x);
}

double round(double x) {
	return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5);
}

float roundf(float x) {
	// return (float)round(x); // __builtin_roundf
	return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5);
}

double sin(double x) {
#ifdef IRON_WASM
	return js_sin(x);
#endif
	return 0.0;
}

float sinf(float x) {
#ifdef IRON_WASM
	return js_sin(x);
#endif
	return 0.0f;
}

double asin(double x) {
#ifdef IRON_WASM
	return js_asin(x);
#endif
	return 0.0;
}

float asinf(float x) {
#ifdef IRON_WASM
	return js_asin(x);
#endif
	return 0.0f;
}

double cos(double x) {
#ifdef IRON_WASM
	return js_cos(x);
#endif
	return 0.0;
}

float cosf(float x) {
#ifdef IRON_WASM
	return js_cos(x);
#endif
	return 0.0f;
}

double acos(double x) {
#ifdef IRON_WASM
	return js_acos(x);
#endif
	return 0.0;
}

float acosf(float x) {
#ifdef IRON_WASM
	return js_acos(x);
#endif
	return 0.0f;
}

double tan(double x) {
#ifdef IRON_WASM
	return js_tan(x);
#endif
	return 0.0;
}

float tanf(float x) {
#ifdef IRON_WASM
	return js_tan(x);
#endif
	return 0.0f;
}

double atan(double x) {
#ifdef IRON_WASM
	return js_atan(x);
#endif
	return 0.0;
}

float atanf(float x) {
#ifdef IRON_WASM
	return js_atan(x);
#endif
	return 0.0f;
}

double atan2(double x, double y) {
#ifdef IRON_WASM
	return js_atan2(x, y);
#endif
	return 0.0;
}

float atan2f(float x, float y) {
#ifdef IRON_WASM
	return js_atan2(x, y);
#endif
	return 0.0f;
}

double log(double x) {
#ifdef IRON_WASM
	return js_log(x);
#endif
	return 0.0;
}

float logf(float x) {
#ifdef IRON_WASM
	return js_log(x);
#endif
	return 0.0f;
}

double log2(double x) {
	return log(x) / log(2.0);
}

float log2f(float x) {
	return (float)log2(x);
}

double exp(double x) {
#ifdef IRON_WASM
	return js_exp(x);
#endif
	return 0.0;
}

float expf(float x) {
#ifdef IRON_WASM
	return js_exp(x);
#endif
	return 0.0;
}

double exp2(double x) {
#ifdef IRON_WASM
	return js_pow(2.0, x);
#endif
	double r = 1.0;
	int    n = (int)x;
	for (int i = 0; i < n; i++)
		r *= 2.0;
	return r;
}

float exp2f(float x) {
	return (float)exp2(x);
}

double frexp(double x, int *exp) {
	if (x == 0.0) {
		*exp = 0;
		return 0.0;
	}
	double sign = x < 0.0 ? -1.0 : 1.0;
	x *= sign;
	*exp = 0;
	while (x >= 1.0) {
		x /= 2.0;
		(*exp)++;
	}
	while (x < 0.5) {
		x *= 2.0;
		(*exp)--;
	}
	return x * sign;
}

float frexpf(float x, int *exp) {
	return (float)frexp(x, exp);
}

double sqrt(double x) {
#ifdef IRON_WASM
	return js_sqrt(x);
#endif
	return 0.0;
}

float sqrtf(float x) {
#ifdef IRON_WASM
	return js_sqrt(x);
#endif
	return 0.0f;
}

double fmod(double x, double y) {
	return x - (int)(x / y) * y;
}

float fmodf(float x, float y) {
	return x - (int)(x / y) * y;
}

int isnan(float x) {
	return x != x;
}
