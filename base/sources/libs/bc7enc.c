// Mode 6 only bc7enc.c
// Based on bc7enc.c by Richard Geldreich, Jr. 3/31/2020
// https://github.com/richgel999/bc7enc
//
// Public Domain(www.unlicense.org)
// This is free and unencumbered software released into the public domain.
// Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
// software, either in source code form or as a compiled binary, for any purpose,
// commercial or non - commercial, and by any means.
// In jurisdictions that recognize copyright laws, the author or authors of this
// software dedicate any and all copyright interest in the software to the public
// domain.We make this dedication for the benefit of the public at large and to
// the detriment of our heirs and successors.We intend this dedication to be an
// overt act of relinquishment in perpetuity of all present and future rights to
// this software under copyright law.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "bc7enc.h"
#include "../iron_simd.h"
#include <limits.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>

#ifndef IRON_NOSIMD
static inline float iron_float32x4_hsum(iron_float32x4_t v) {
#if defined(IRON_SSE)
	iron_float32x4_t t = iron_float32x4_shuffle_custom(v, v, 1, 0, 3, 2);
	t                  = iron_float32x4_add(v, t);
	iron_float32x4_t s = iron_float32x4_shuffle_custom(t, t, 2, 3, 0, 1);
	return iron_float32x4_get(iron_float32x4_add(t, s), 0);
#elif defined(IRON_NEON)
	return vaddvq_f32(v);
#endif
}
#endif

void bc7enc_compress_block_params_init(bc7enc_compress_block_params *p) {
	p->m_max_partitions_mode                  = BC7ENC_MAX_PARTITIONS1;
	p->m_try_least_squares                    = BC7ENC_TRUE;
	p->m_mode_partition_estimation_filterbank = BC7ENC_TRUE;
	p->m_uber_level                           = 0;
	p->m_use_mode5_for_alpha                  = BC7ENC_TRUE;
	p->m_use_mode7_for_alpha                  = BC7ENC_TRUE;
	p->m_perceptual                           = BC7ENC_TRUE;
	p->m_weights[0]                           = 128;
	p->m_weights[1]                           = 64;
	p->m_weights[2]                           = 16;
	p->m_weights[3]                           = 32;
}

// Helpers
static inline int32_t clampi(int32_t value, int32_t low, int32_t high) {
	if (value < low)
		value = low;
	else if (value > high)
		value = high;
	return value;
}
static inline float clampf(float value, float low, float high) {
	if (value < low)
		value = low;
	else if (value > high)
		value = high;
	return value;
}
static inline float saturate(float value) {
	return clampf(value, 0, 1.0f);
}
static inline uint32_t minimumu(uint32_t a, uint32_t b) {
	return (a < b) ? a : b;
}
static inline float minimumf(float a, float b) {
	return (a < b) ? a : b;
}
static inline float maximumf(float a, float b) {
	return (a > b) ? a : b;
}
static inline int squarei(int i) {
	return i * i;
}
static inline float squaref(float i) {
	return i * i;
}

typedef struct {
	uint8_t m_c[4];
} color_quad_u8;
typedef struct {
	float m_c[4];
} vec4F;

static inline color_quad_u8 *color_quad_u8_set_clamped(color_quad_u8 *pRes, int32_t r, int32_t g, int32_t b, int32_t a) {
	pRes->m_c[0] = (uint8_t)clampi(r, 0, 255);
	pRes->m_c[1] = (uint8_t)clampi(g, 0, 255);
	pRes->m_c[2] = (uint8_t)clampi(b, 0, 255);
	pRes->m_c[3] = (uint8_t)clampi(a, 0, 255);
	return pRes;
}
static inline color_quad_u8 *color_quad_u8_set(color_quad_u8 *pRes, int32_t r, int32_t g, int32_t b, int32_t a) {
	pRes->m_c[0] = (uint8_t)r;
	pRes->m_c[1] = (uint8_t)g;
	pRes->m_c[2] = (uint8_t)b;
	pRes->m_c[3] = (uint8_t)a;
	return pRes;
}
static inline bc7enc_bool color_quad_u8_notequals(const color_quad_u8 *pLHS, const color_quad_u8 *pRHS) {
	return (pLHS->m_c[0] != pRHS->m_c[0]) || (pLHS->m_c[1] != pRHS->m_c[1]) || (pLHS->m_c[2] != pRHS->m_c[2]) || (pLHS->m_c[3] != pRHS->m_c[3]);
}
static inline vec4F *vec4F_set_scalar(vec4F *pV, float x) {
	pV->m_c[0] = x;
	pV->m_c[1] = x;
	pV->m_c[2] = x;
	pV->m_c[3] = x;
	return pV;
}
static inline vec4F *vec4F_set(vec4F *pV, float x, float y, float z, float w) {
	pV->m_c[0] = x;
	pV->m_c[1] = y;
	pV->m_c[2] = z;
	pV->m_c[3] = w;
	return pV;
}
static inline vec4F vec4F_from_color(const color_quad_u8 *pC) {
	vec4F res;
	vec4F_set(&res, pC->m_c[0], pC->m_c[1], pC->m_c[2], pC->m_c[3]);
	return res;
}

static inline vec4F *vec4F_saturate_in_place(vec4F *pV) {
	pV->m_c[0] = saturate(pV->m_c[0]);
	pV->m_c[1] = saturate(pV->m_c[1]);
	pV->m_c[2] = saturate(pV->m_c[2]);
	pV->m_c[3] = saturate(pV->m_c[3]);
	return pV;
}
static inline vec4F vec4F_saturate(const vec4F *pV) {
	vec4F res;
	res.m_c[0] = saturate(pV->m_c[0]);
	res.m_c[1] = saturate(pV->m_c[1]);
	res.m_c[2] = saturate(pV->m_c[2]);
	res.m_c[3] = saturate(pV->m_c[3]);
	return res;
}
static inline vec4F vec4F_add(const vec4F *pLHS, const vec4F *pRHS) {
	vec4F res;
	vec4F_set(&res, pLHS->m_c[0] + pRHS->m_c[0], pLHS->m_c[1] + pRHS->m_c[1], pLHS->m_c[2] + pRHS->m_c[2], pLHS->m_c[3] + pRHS->m_c[3]);
	return res;
}
static inline vec4F vec4F_sub(const vec4F *pLHS, const vec4F *pRHS) {
	vec4F res;
	vec4F_set(&res, pLHS->m_c[0] - pRHS->m_c[0], pLHS->m_c[1] - pRHS->m_c[1], pLHS->m_c[2] - pRHS->m_c[2], pLHS->m_c[3] - pRHS->m_c[3]);
	return res;
}
static inline float vec4F_dot(const vec4F *pLHS, const vec4F *pRHS) {
	return pLHS->m_c[0] * pRHS->m_c[0] + pLHS->m_c[1] * pRHS->m_c[1] + pLHS->m_c[2] * pRHS->m_c[2] + pLHS->m_c[3] * pRHS->m_c[3];
}
static inline vec4F vec4F_mul(const vec4F *pLHS, float s) {
	vec4F res;
	vec4F_set(&res, pLHS->m_c[0] * s, pLHS->m_c[1] * s, pLHS->m_c[2] * s, pLHS->m_c[3] * s);
	return res;
}
static inline vec4F *vec4F_normalize_in_place(vec4F *pV) {
	float s = pV->m_c[0] * pV->m_c[0] + pV->m_c[1] * pV->m_c[1] + pV->m_c[2] * pV->m_c[2] + pV->m_c[3] * pV->m_c[3];
	if (s != 0.0f) {
		s = 1.0f / sqrtf(s);
		pV->m_c[0] *= s;
		pV->m_c[1] *= s;
		pV->m_c[2] *= s;
		pV->m_c[3] *= s;
	}
	return pV;
}

// Various BC7 tables
static const uint32_t g_bc7_weights2[4]  = {0, 21, 43, 64};
static const uint32_t g_bc7_weights3[8]  = {0, 9, 18, 27, 37, 46, 55, 64};
static const uint32_t g_bc7_weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
// Precomputed weight constants used during least fit determination. For each entry in g_bc7_weights[]: w * w, (1.0f - w) * w, (1.0f - w) * (1.0f - w), w
static const float g_bc7_weights4x[16 * 4] = {0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.003906f, 0.058594f, 0.878906f, 0.062500f, 0.019775f, 0.120850f,
                                              0.738525f, 0.140625f, 0.041260f, 0.161865f, 0.635010f, 0.203125f, 0.070557f, 0.195068f, 0.539307f, 0.265625f,
                                              0.107666f, 0.220459f, 0.451416f, 0.328125f, 0.165039f, 0.241211f, 0.352539f, 0.406250f, 0.219727f, 0.249023f,
                                              0.282227f, 0.468750f, 0.282227f, 0.249023f, 0.219727f, 0.531250f, 0.352539f, 0.241211f, 0.165039f, 0.593750f,
                                              0.451416f, 0.220459f, 0.107666f, 0.671875f, 0.539307f, 0.195068f, 0.070557f, 0.734375f, 0.635010f, 0.161865f,
                                              0.041260f, 0.796875f, 0.738525f, 0.120850f, 0.019775f, 0.859375f, 0.878906f, 0.058594f, 0.003906f, 0.937500f,
                                              1.000000f, 0.000000f, 0.000000f, 1.000000f};

static const uint8_t g_bc7_partition1[16]      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t g_bc7_partition2[64 * 16] = {
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1,
    0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0,
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1,
    0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0,
    1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0,
    0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1,
    1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0,
    1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1};

static const uint8_t g_bc7_partition3[64 * 16] = {
    0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2, 0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1, 0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1, 0, 2, 2, 2,
    0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2,
    0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0, 0, 0, 0, 1,
    0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2,
    0, 0, 2, 2, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2, 0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2,
    0, 1, 2, 2, 0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2,
    0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2, 0, 1, 1, 0,
    0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 2,
    1, 1, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0,
    0, 1, 2, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0,
    0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1, 0, 2, 2, 0, 1, 2, 2, 1,
    0, 2, 2, 0, 1, 2, 2, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    2, 2, 2, 2, 0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1, 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2,
    0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2,
    1, 1, 2, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2,
    1, 2, 2, 2, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0,
};

static const uint8_t g_bc7_table_anchor_index_third_subset_1[64] = {3,  3,  15, 15, 8, 3,  15, 15, 8,  8,  6, 6,  6, 5,  3, 3,  3,  3,  8,  15, 3,  3,
                                                                    6,  10, 5,  8,  8, 6,  8,  5,  15, 15, 8, 15, 3, 5,  6, 10, 8,  15, 15, 3,  15, 5,
                                                                    15, 15, 15, 15, 3, 15, 5,  5,  5,  8,  5, 10, 5, 10, 8, 13, 15, 12, 3,  3};

static const uint8_t g_bc7_table_anchor_index_third_subset_2[64] = {15, 8, 8, 3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,  15, 8,  15, 3,  15, 8,
                                                                    15, 8, 3, 15, 6,  10, 15, 15, 10, 8,  15, 3,  15, 10, 10, 8,  9,  10, 6,  15, 8,  15,
                                                                    3,  6, 6, 8,  15, 3,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8};

static const uint8_t g_bc7_table_anchor_index_second_subset[64] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,  8, 2, 2, 8,
                                                                   8,  15, 2,  8,  2,  2,  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15, 15, 2, 8, 2, 2,
                                                                   2,  15, 15, 6,  6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2, 15};
static const uint8_t g_bc7_num_subsets[8]                       = {3, 2, 3, 2, 1, 1, 1, 2};
static const uint8_t g_bc7_partition_bits[8]                    = {4, 6, 6, 6, 0, 0, 0, 6};
static const uint8_t g_bc7_color_index_bitcount[8]              = {3, 3, 2, 2, 2, 2, 4, 2};
static int           get_bc7_color_index_size(int mode, int index_selection_bit) {
    return g_bc7_color_index_bitcount[mode] + index_selection_bit;
}
static uint8_t g_bc7_alpha_index_bitcount[8] = {0, 0, 0, 0, 3, 2, 4, 2};
static int     get_bc7_alpha_index_size(int mode, int index_selection_bit) {
    return g_bc7_alpha_index_bitcount[mode] - index_selection_bit;
}
static const uint8_t g_bc7_mode_has_p_bits[8]        = {1, 1, 0, 1, 0, 0, 1, 1};
static const uint8_t g_bc7_mode_has_shared_p_bits[8] = {0, 1, 0, 0, 0, 0, 0, 0};
static const uint8_t g_bc7_color_precision_table[8]  = {4, 6, 5, 7, 5, 7, 7, 5};
static const int8_t  g_bc7_alpha_precision_table[8]  = {0, 0, 0, 0, 6, 8, 7, 5};
static bc7enc_bool   get_bc7_mode_has_seperate_alpha_selectors(int mode) {
    return (mode == 4) || (mode == 5);
}

typedef struct {
	uint16_t m_error;
	uint8_t  m_lo;
	uint8_t  m_hi;
} endpoint_err;

// Initialize the lookup table used for optimal single color compression in mode 1. Must be called before encoding.
void bc7enc_compress_block_init() {}

typedef struct {
	uint32_t             m_num_pixels;
	const color_quad_u8 *m_pPixels;
	uint32_t             m_num_selector_weights;
	const uint32_t      *m_pSelector_weights;
	const vec4F         *m_pSelector_weightsx;
	uint32_t             m_comp_bits;
	uint32_t             m_weights[4];
	bc7enc_bool          m_has_alpha;
	bc7enc_bool          m_has_pbits;
	bc7enc_bool          m_endpoints_share_pbit;
	bc7enc_bool          m_perceptual;
} color_cell_compressor_params;

typedef struct {
	uint64_t      m_best_overall_err;
	color_quad_u8 m_low_endpoint;
	color_quad_u8 m_high_endpoint;
	uint32_t      m_pbits[2];
	uint8_t      *m_pSelectors;
	uint8_t      *m_pSelectors_temp;
} color_cell_compressor_results;

static inline color_quad_u8 scale_color(const color_quad_u8 *pC, const color_cell_compressor_params *pParams) {
	color_quad_u8 results;

	const uint32_t n = pParams->m_comp_bits + (pParams->m_has_pbits ? 1 : 0);

	for (uint32_t i = 0; i < 4; i++) {
		uint32_t v = pC->m_c[i] << (8 - n);
		v |= (v >> n);
		results.m_c[i] = (uint8_t)(v);
	}

	return results;
}

static inline uint64_t compute_color_distance_rgb(const color_quad_u8 *pE1, const color_quad_u8 *pE2, bc7enc_bool perceptual, const uint32_t weights[4]) {
	int dr, dg, db;

	if (perceptual) {
		const int l1  = pE1->m_c[0] * 109 + pE1->m_c[1] * 366 + pE1->m_c[2] * 37;
		const int cr1 = ((int)pE1->m_c[0] << 9) - l1;
		const int cb1 = ((int)pE1->m_c[2] << 9) - l1;
		const int l2  = pE2->m_c[0] * 109 + pE2->m_c[1] * 366 + pE2->m_c[2] * 37;
		const int cr2 = ((int)pE2->m_c[0] << 9) - l2;
		const int cb2 = ((int)pE2->m_c[2] << 9) - l2;
		dr            = (l1 - l2) >> 8;
		dg            = (cr1 - cr2) >> 8;
		db            = (cb1 - cb2) >> 8;
	}
	else {
		dr = (int)pE1->m_c[0] - (int)pE2->m_c[0];
		dg = (int)pE1->m_c[1] - (int)pE2->m_c[1];
		db = (int)pE1->m_c[2] - (int)pE2->m_c[2];
	}

	return weights[0] * (uint32_t)(dr * dr) + weights[1] * (uint32_t)(dg * dg) + weights[2] * (uint32_t)(db * db);
}

static inline uint64_t compute_color_distance_rgba(const color_quad_u8 *pE1, const color_quad_u8 *pE2, bc7enc_bool perceptual, const uint32_t weights[4]) {
	int da = (int)pE1->m_c[3] - (int)pE2->m_c[3];
	return compute_color_distance_rgb(pE1, pE2, perceptual, weights) + (weights[3] * (uint32_t)(da * da));
}

static uint64_t evaluate_solution(const color_quad_u8 *pLow, const color_quad_u8 *pHigh, const uint32_t pbits[2], const color_cell_compressor_params *pParams,
                                  color_cell_compressor_results *pResults) {
	color_quad_u8 quantMinColor = *pLow;
	color_quad_u8 quantMaxColor = *pHigh;

	if (pParams->m_has_pbits) {
		uint32_t minPBit, maxPBit;

		if (pParams->m_endpoints_share_pbit)
			maxPBit = minPBit = pbits[0];
		else {
			minPBit = pbits[0];
			maxPBit = pbits[1];
		}

		quantMinColor.m_c[0] = (uint8_t)((pLow->m_c[0] << 1) | minPBit);
		quantMinColor.m_c[1] = (uint8_t)((pLow->m_c[1] << 1) | minPBit);
		quantMinColor.m_c[2] = (uint8_t)((pLow->m_c[2] << 1) | minPBit);
		quantMinColor.m_c[3] = (uint8_t)((pLow->m_c[3] << 1) | minPBit);

		quantMaxColor.m_c[0] = (uint8_t)((pHigh->m_c[0] << 1) | maxPBit);
		quantMaxColor.m_c[1] = (uint8_t)((pHigh->m_c[1] << 1) | maxPBit);
		quantMaxColor.m_c[2] = (uint8_t)((pHigh->m_c[2] << 1) | maxPBit);
		quantMaxColor.m_c[3] = (uint8_t)((pHigh->m_c[3] << 1) | maxPBit);
	}

	color_quad_u8 actualMinColor = scale_color(&quantMinColor, pParams);
	color_quad_u8 actualMaxColor = scale_color(&quantMaxColor, pParams);

	const uint32_t N = pParams->m_num_selector_weights;

	color_quad_u8 weightedColors[16];
	weightedColors[0]     = actualMinColor;
	weightedColors[N - 1] = actualMaxColor;

	const uint32_t nc = pParams->m_has_alpha ? 4 : 3;
	for (uint32_t i = 1; i < (N - 1); i++)
		for (uint32_t j = 0; j < nc; j++)
			weightedColors[i].m_c[j] =
			    (uint8_t)((actualMinColor.m_c[j] * (64 - pParams->m_pSelector_weights[i]) + actualMaxColor.m_c[j] * pParams->m_pSelector_weights[i] + 32) >> 6);

	const int lr = actualMinColor.m_c[0];
	const int lg = actualMinColor.m_c[1];
	const int lb = actualMinColor.m_c[2];
	const int dr = actualMaxColor.m_c[0] - lr;
	const int dg = actualMaxColor.m_c[1] - lg;
	const int db = actualMaxColor.m_c[2] - lb;

	uint64_t total_err = 0;

	// Precompute perceptual luma/chroma for all N candidates once.
	// Values fit exactly in float32 (max luma = 255*512 = 130560 < 2^24).
	float cand_l_f[16], cand_cr_f[16], cand_cb_f[16], cand_a_f[16];
	for (uint32_t j = 0; j < N; j++) {
		const int l  = weightedColors[j].m_c[0] * 109 + weightedColors[j].m_c[1] * 366 + weightedColors[j].m_c[2] * 37;
		cand_l_f[j]  = (float)l;
		cand_cr_f[j] = (float)(((int)weightedColors[j].m_c[0] << 9) - l);
		cand_cb_f[j] = (float)(((int)weightedColors[j].m_c[2] << 9) - l);
		cand_a_f[j]  = (float)weightedColors[j].m_c[3];
	}

#ifndef IRON_NOSIMD
	// Precompute pixel luma/chroma so the inner loop is pure SIMD with no branching or extraction.
	float pix_l_f[16], pix_cr_f[16], pix_cb_f[16];
	for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
		const color_quad_u8 *pPix = &pParams->m_pPixels[i];
		const int            l    = pPix->m_c[0] * 109 + pPix->m_c[1] * 366 + pPix->m_c[2] * 37;
		pix_l_f[i]                = (float)l;
		pix_cr_f[i]               = (float)(((int)pPix->m_c[0] << 9) - l);
		pix_cb_f[i]               = (float)(((int)pPix->m_c[2] << 9) - l);
	}

	// Per-pixel best error and selector stored as floats.
	// Outer=candidates, inner=pixels: replaces 256 scalar extractions with 16 at the end.
	float best_err_f[16], best_sel_f[16];
	{
		const iron_float32x4_t inf4 = iron_float32x4_load_all(1e30f);
		const iron_float32x4_t zer4 = iron_float32x4_load_all(0.0f);
		for (uint32_t i = 0; i < 16; i += 4) {
			iron_float32x4_store_unaligned(&best_err_f[i], inf4);
			iron_float32x4_store_unaligned(&best_sel_f[i], zer4);
		}
	}

	const iron_float32x4_t inv256 = iron_float32x4_load_all(1.0f / 256.0f);
	const iron_float32x4_t w0v    = iron_float32x4_load_all((float)pParams->m_weights[0]);
	const iron_float32x4_t w1v    = iron_float32x4_load_all((float)pParams->m_weights[1]);
	const iron_float32x4_t w2v    = iron_float32x4_load_all((float)pParams->m_weights[2]);

	if (pParams->m_has_alpha) {
		float pix_a_f[16];
		for (uint32_t i = 0; i < pParams->m_num_pixels; i++)
			pix_a_f[i] = (float)pParams->m_pPixels[i].m_c[3];
		const iron_float32x4_t w3v = iron_float32x4_load_all((float)pParams->m_weights[3]);

		for (uint32_t j = 0; j < N; j++) {
			const iron_float32x4_t l1v  = iron_float32x4_load_all(cand_l_f[j]);
			const iron_float32x4_t cr1v = iron_float32x4_load_all(cand_cr_f[j]);
			const iron_float32x4_t cb1v = iron_float32x4_load_all(cand_cb_f[j]);
			const iron_float32x4_t a1v  = iron_float32x4_load_all(cand_a_f[j]);
			const iron_float32x4_t jv   = iron_float32x4_load_all((float)j);
			for (uint32_t i = 0; i < 16; i += 4) {
				const iron_float32x4_t drv          = iron_float32x4_mul(iron_float32x4_sub(l1v, iron_float32x4_intrin_load_unaligned(&pix_l_f[i])), inv256);
				const iron_float32x4_t dgv          = iron_float32x4_mul(iron_float32x4_sub(cr1v, iron_float32x4_intrin_load_unaligned(&pix_cr_f[i])), inv256);
				const iron_float32x4_t dbv          = iron_float32x4_mul(iron_float32x4_sub(cb1v, iron_float32x4_intrin_load_unaligned(&pix_cb_f[i])), inv256);
				const iron_float32x4_t dav          = iron_float32x4_sub(a1v, iron_float32x4_intrin_load_unaligned(&pix_a_f[i]));
				iron_float32x4_t       errv         = iron_float32x4_mul(w0v, iron_float32x4_mul(drv, drv));
				errv                                = iron_float32x4_add(errv, iron_float32x4_mul(w1v, iron_float32x4_mul(dgv, dgv)));
				errv                                = iron_float32x4_add(errv, iron_float32x4_mul(w2v, iron_float32x4_mul(dbv, dbv)));
				errv                                = iron_float32x4_add(errv, iron_float32x4_mul(w3v, iron_float32x4_mul(dav, dav)));
				iron_float32x4_t            best_v  = iron_float32x4_intrin_load_unaligned(&best_err_f[i]);
				iron_float32x4_t            best_sv = iron_float32x4_intrin_load_unaligned(&best_sel_f[i]);
				const iron_float32x4_mask_t better  = iron_float32x4_cmplt(errv, best_v);
				iron_float32x4_store_unaligned(&best_err_f[i], iron_float32x4_min(errv, best_v));
				iron_float32x4_store_unaligned(&best_sel_f[i], iron_float32x4_sel(jv, best_sv, better));
			}
		}
	}
	else {
		for (uint32_t j = 0; j < N; j++) {
			const iron_float32x4_t l1v  = iron_float32x4_load_all(cand_l_f[j]);
			const iron_float32x4_t cr1v = iron_float32x4_load_all(cand_cr_f[j]);
			const iron_float32x4_t cb1v = iron_float32x4_load_all(cand_cb_f[j]);
			const iron_float32x4_t jv   = iron_float32x4_load_all((float)j);
			for (uint32_t i = 0; i < 16; i += 4) {
				const iron_float32x4_t drv          = iron_float32x4_mul(iron_float32x4_sub(l1v, iron_float32x4_intrin_load_unaligned(&pix_l_f[i])), inv256);
				const iron_float32x4_t dgv          = iron_float32x4_mul(iron_float32x4_sub(cr1v, iron_float32x4_intrin_load_unaligned(&pix_cr_f[i])), inv256);
				const iron_float32x4_t dbv          = iron_float32x4_mul(iron_float32x4_sub(cb1v, iron_float32x4_intrin_load_unaligned(&pix_cb_f[i])), inv256);
				iron_float32x4_t       errv         = iron_float32x4_mul(w0v, iron_float32x4_mul(drv, drv));
				errv                                = iron_float32x4_add(errv, iron_float32x4_mul(w1v, iron_float32x4_mul(dgv, dgv)));
				errv                                = iron_float32x4_add(errv, iron_float32x4_mul(w2v, iron_float32x4_mul(dbv, dbv)));
				iron_float32x4_t            best_v  = iron_float32x4_intrin_load_unaligned(&best_err_f[i]);
				iron_float32x4_t            best_sv = iron_float32x4_intrin_load_unaligned(&best_sel_f[i]);
				const iron_float32x4_mask_t better  = iron_float32x4_cmplt(errv, best_v);
				iron_float32x4_store_unaligned(&best_err_f[i], iron_float32x4_min(errv, best_v));
				iron_float32x4_store_unaligned(&best_sel_f[i], iron_float32x4_sel(jv, best_sv, better));
			}
		}
	}

	// 16 scalar extractions
	for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
		total_err += (uint64_t)best_err_f[i];
		pResults->m_pSelectors_temp[i] = (uint8_t)(int)best_sel_f[i];
	}

#else // IRON_NOSIMD

	for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
		uint64_t             best_err = UINT64_MAX;
		uint32_t             best_sel = 0;
		const color_quad_u8 *pPix     = &pParams->m_pPixels[i];
		const int            l2       = pPix->m_c[0] * 109 + pPix->m_c[1] * 366 + pPix->m_c[2] * 37;
		const int            cr2      = ((int)pPix->m_c[0] << 9) - l2;
		const int            cb2      = ((int)pPix->m_c[2] << 9) - l2;
		if (pParams->m_has_alpha) {
			const int a2 = (int)pPix->m_c[3];
			for (uint32_t j = 0; j < N; j++) {
				const int      dr  = ((int)cand_l_f[j] - l2) >> 8;
				const int      dg  = ((int)cand_cr_f[j] - cr2) >> 8;
				const int      db  = ((int)cand_cb_f[j] - cb2) >> 8;
				const int      da  = (int)cand_a_f[j] - a2;
				const uint64_t err = pParams->m_weights[0] * (uint32_t)(dr * dr) + pParams->m_weights[1] * (uint32_t)(dg * dg) +
				                     pParams->m_weights[2] * (uint32_t)(db * db) + pParams->m_weights[3] * (uint32_t)(da * da);
				if (err < best_err) {
					best_err = err;
					best_sel = j;
				}
			}
		}
		else {
			for (uint32_t j = 0; j < N; j++) {
				const int      dr = ((int)cand_l_f[j] - l2) >> 8;
				const int      dg = ((int)cand_cr_f[j] - cr2) >> 8;
				const int      db = ((int)cand_cb_f[j] - cb2) >> 8;
				const uint64_t err =
				    pParams->m_weights[0] * (uint32_t)(dr * dr) + pParams->m_weights[1] * (uint32_t)(dg * dg) + pParams->m_weights[2] * (uint32_t)(db * db);
				if (err < best_err) {
					best_err = err;
					best_sel = j;
				}
			}
		}
		total_err += best_err;
		pResults->m_pSelectors_temp[i] = (uint8_t)best_sel;
	}
#endif

	if (total_err < pResults->m_best_overall_err) {
		pResults->m_best_overall_err = total_err;

		pResults->m_low_endpoint  = *pLow;
		pResults->m_high_endpoint = *pHigh;

		pResults->m_pbits[0] = pbits[0];
		pResults->m_pbits[1] = pbits[1];

		memcpy(pResults->m_pSelectors, pResults->m_pSelectors_temp, sizeof(pResults->m_pSelectors[0]) * pParams->m_num_pixels);
	}

	return total_err;
}

static uint64_t find_optimal_solution(uint32_t mode, vec4F xl, vec4F xh, const color_cell_compressor_params *pParams, color_cell_compressor_results *pResults) {
	vec4F_saturate_in_place(&xl);
	vec4F_saturate_in_place(&xh);

	if (pParams->m_has_pbits) {
		const int   iscalep = (1 << (pParams->m_comp_bits + 1)) - 1;
		const float scalep  = (float)iscalep;

		const int32_t totalComps = pParams->m_has_alpha ? 4 : 3;

		uint32_t      best_pbits[2];
		color_quad_u8 bestMinColor, bestMaxColor;

		if (!pParams->m_endpoints_share_pbit) {
			float best_err0 = 1e+9;
			float best_err1 = 1e+9;

			for (int p = 0; p < 2; p++) {
				color_quad_u8 xMinColor, xMaxColor;

				// Notes: The pbit controls which quantization intervals are selected.
				// total_levels=2^(comp_bits+1), where comp_bits=4 for mode 0, etc.
				// pbit 0: v=(b*2)/(total_levels-1), pbit 1: v=(b*2+1)/(total_levels-1) where b is the component bin from [0,total_levels/2-1] and v is the
				// [0,1] component value rearranging you get for pbit 0: b=floor(v*(total_levels-1)/2+.5) rearranging you get for pbit 1:
				// b=floor((v*(total_levels-1)-1)/2+.5)
				for (uint32_t c = 0; c < 4; c++) {
					xMinColor.m_c[c] = (uint8_t)(clampi(((int)((xl.m_c[c] * scalep - p) / 2.0f + .5f)) * 2 + p, p, iscalep - 1 + p));
					xMaxColor.m_c[c] = (uint8_t)(clampi(((int)((xh.m_c[c] * scalep - p) / 2.0f + .5f)) * 2 + p, p, iscalep - 1 + p));
				}

				color_quad_u8 scaledLow  = scale_color(&xMinColor, pParams);
				color_quad_u8 scaledHigh = scale_color(&xMaxColor, pParams);

				float err0 = 0, err1 = 0;
				for (int i = 0; i < totalComps; i++) {
					err0 += squaref(scaledLow.m_c[i] - xl.m_c[i] * 255.0f);
					err1 += squaref(scaledHigh.m_c[i] - xh.m_c[i] * 255.0f);
				}

				if (err0 < best_err0) {
					best_err0     = err0;
					best_pbits[0] = p;

					bestMinColor.m_c[0] = xMinColor.m_c[0] >> 1;
					bestMinColor.m_c[1] = xMinColor.m_c[1] >> 1;
					bestMinColor.m_c[2] = xMinColor.m_c[2] >> 1;
					bestMinColor.m_c[3] = xMinColor.m_c[3] >> 1;
				}

				if (err1 < best_err1) {
					best_err1     = err1;
					best_pbits[1] = p;

					bestMaxColor.m_c[0] = xMaxColor.m_c[0] >> 1;
					bestMaxColor.m_c[1] = xMaxColor.m_c[1] >> 1;
					bestMaxColor.m_c[2] = xMaxColor.m_c[2] >> 1;
					bestMaxColor.m_c[3] = xMaxColor.m_c[3] >> 1;
				}
			}
		}
		else {
			// Endpoints share pbits
			float best_err = 1e+9;

			for (int p = 0; p < 2; p++) {
				color_quad_u8 xMinColor, xMaxColor;
				for (uint32_t c = 0; c < 4; c++) {
					xMinColor.m_c[c] = (uint8_t)(clampi(((int)((xl.m_c[c] * scalep - p) / 2.0f + .5f)) * 2 + p, p, iscalep - 1 + p));
					xMaxColor.m_c[c] = (uint8_t)(clampi(((int)((xh.m_c[c] * scalep - p) / 2.0f + .5f)) * 2 + p, p, iscalep - 1 + p));
				}

				color_quad_u8 scaledLow  = scale_color(&xMinColor, pParams);
				color_quad_u8 scaledHigh = scale_color(&xMaxColor, pParams);

				float err = 0;
				for (int i = 0; i < totalComps; i++)
					err += squaref((scaledLow.m_c[i] / 255.0f) - xl.m_c[i]) + squaref((scaledHigh.m_c[i] / 255.0f) - xh.m_c[i]);

				if (err < best_err) {
					best_err      = err;
					best_pbits[0] = p;
					best_pbits[1] = p;
					for (uint32_t j = 0; j < 4; j++) {
						bestMinColor.m_c[j] = xMinColor.m_c[j] >> 1;
						bestMaxColor.m_c[j] = xMaxColor.m_c[j] >> 1;
					}
				}
			}
		}

		if ((pResults->m_best_overall_err == UINT64_MAX) || color_quad_u8_notequals(&bestMinColor, &pResults->m_low_endpoint) ||
		    color_quad_u8_notequals(&bestMaxColor, &pResults->m_high_endpoint) || (best_pbits[0] != pResults->m_pbits[0]) ||
		    (best_pbits[1] != pResults->m_pbits[1]))
			evaluate_solution(&bestMinColor, &bestMaxColor, best_pbits, pParams, pResults);
	}
	else {
		const int   iscale = (1 << pParams->m_comp_bits) - 1;
		const float scale  = (float)iscale;

		color_quad_u8 trialMinColor, trialMaxColor;
		color_quad_u8_set_clamped(&trialMinColor, (int)(xl.m_c[0] * scale + .5f), (int)(xl.m_c[1] * scale + .5f), (int)(xl.m_c[2] * scale + .5f),
		                          (int)(xl.m_c[3] * scale + .5f));
		color_quad_u8_set_clamped(&trialMaxColor, (int)(xh.m_c[0] * scale + .5f), (int)(xh.m_c[1] * scale + .5f), (int)(xh.m_c[2] * scale + .5f),
		                          (int)(xh.m_c[3] * scale + .5f));

		if ((pResults->m_best_overall_err == UINT64_MAX) || color_quad_u8_notequals(&trialMinColor, &pResults->m_low_endpoint) ||
		    color_quad_u8_notequals(&trialMaxColor, &pResults->m_high_endpoint))
			evaluate_solution(&trialMinColor, &trialMaxColor, pResults->m_pbits, pParams, pResults);
	}

	return pResults->m_best_overall_err;
}

static uint64_t color_cell_compression(uint32_t mode, const color_cell_compressor_params *pParams, color_cell_compressor_results *pResults,
                                       const bc7enc_compress_block_params *pComp_params) {
	pResults->m_best_overall_err = UINT64_MAX;

	// Compute partition's mean color and principle axis.
	vec4F meanColor, axis;
	vec4F_set_scalar(&meanColor, 0.0f);

	for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
		vec4F color = vec4F_from_color(&pParams->m_pPixels[i]);
		meanColor   = vec4F_add(&meanColor, &color);
	}

	vec4F meanColorScaled = vec4F_mul(&meanColor, 1.0f / (float)(pParams->m_num_pixels));

	meanColor = vec4F_mul(&meanColor, 1.0f / (float)(pParams->m_num_pixels * 255.0f));
	vec4F_saturate_in_place(&meanColor);

#ifndef IRON_NOSIMD
	// Precompute per-channel float arrays for SIMD covariance and projection loops below.
	float pix_r_f[16], pix_g_f[16], pix_b_f[16];
	for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
		pix_r_f[i] = (float)pParams->m_pPixels[i].m_c[0];
		pix_g_f[i] = (float)pParams->m_pPixels[i].m_c[1];
		pix_b_f[i] = (float)pParams->m_pPixels[i].m_c[2];
	}
#endif

	if (pParams->m_has_alpha) {
		// Use incremental PCA for RGBA PCA, because it's simple.
		vec4F_set_scalar(&axis, 0.0f);
		for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
			vec4F color = vec4F_from_color(&pParams->m_pPixels[i]);
			color       = vec4F_sub(&color, &meanColorScaled);
			vec4F a     = vec4F_mul(&color, color.m_c[0]);
			vec4F b     = vec4F_mul(&color, color.m_c[1]);
			vec4F c     = vec4F_mul(&color, color.m_c[2]);
			vec4F d     = vec4F_mul(&color, color.m_c[3]);
			vec4F n     = i ? axis : color;
			vec4F_normalize_in_place(&n);
			axis.m_c[0] += vec4F_dot(&a, &n);
			axis.m_c[1] += vec4F_dot(&b, &n);
			axis.m_c[2] += vec4F_dot(&c, &n);
			axis.m_c[3] += vec4F_dot(&d, &n);
		}
		vec4F_normalize_in_place(&axis);
	}
	else {
		// Use covar technique for RGB PCA, because it doesn't require per-pixel normalization.
		float cov[6] = {0, 0, 0, 0, 0, 0};

#ifndef IRON_NOSIMD
		{
			const iron_float32x4_t mr4 = iron_float32x4_load_all(meanColorScaled.m_c[0]);
			const iron_float32x4_t mg4 = iron_float32x4_load_all(meanColorScaled.m_c[1]);
			const iron_float32x4_t mb4 = iron_float32x4_load_all(meanColorScaled.m_c[2]);
			iron_float32x4_t       c0v = iron_float32x4_load_all(0.0f), c1v = iron_float32x4_load_all(0.0f);
			iron_float32x4_t       c2v = iron_float32x4_load_all(0.0f), c3v = iron_float32x4_load_all(0.0f);
			iron_float32x4_t       c4v = iron_float32x4_load_all(0.0f), c5v = iron_float32x4_load_all(0.0f);
			for (uint32_t i = 0; i < 16; i += 4) {
				const iron_float32x4_t rv = iron_float32x4_sub(iron_float32x4_intrin_load_unaligned(&pix_r_f[i]), mr4);
				const iron_float32x4_t gv = iron_float32x4_sub(iron_float32x4_intrin_load_unaligned(&pix_g_f[i]), mg4);
				const iron_float32x4_t bv = iron_float32x4_sub(iron_float32x4_intrin_load_unaligned(&pix_b_f[i]), mb4);
				c0v                       = iron_float32x4_add(c0v, iron_float32x4_mul(rv, rv));
				c1v                       = iron_float32x4_add(c1v, iron_float32x4_mul(rv, gv));
				c2v                       = iron_float32x4_add(c2v, iron_float32x4_mul(rv, bv));
				c3v                       = iron_float32x4_add(c3v, iron_float32x4_mul(gv, gv));
				c4v                       = iron_float32x4_add(c4v, iron_float32x4_mul(gv, bv));
				c5v                       = iron_float32x4_add(c5v, iron_float32x4_mul(bv, bv));
			}
			cov[0] = iron_float32x4_hsum(c0v);
			cov[1] = iron_float32x4_hsum(c1v);
			cov[2] = iron_float32x4_hsum(c2v);
			cov[3] = iron_float32x4_hsum(c3v);
			cov[4] = iron_float32x4_hsum(c4v);
			cov[5] = iron_float32x4_hsum(c5v);
		}
#else
		for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
			const color_quad_u8 *pV = &pParams->m_pPixels[i];
			float                r  = pV->m_c[0] - meanColorScaled.m_c[0];
			float                g  = pV->m_c[1] - meanColorScaled.m_c[1];
			float                b  = pV->m_c[2] - meanColorScaled.m_c[2];
			cov[0] += r * r;
			cov[1] += r * g;
			cov[2] += r * b;
			cov[3] += g * g;
			cov[4] += g * b;
			cov[5] += b * b;
		}
#endif

		float vfr = .9f, vfg = 1.0f, vfb = .7f;
		for (uint32_t iter = 0; iter < 3; iter++) {
			float r = vfr * cov[0] + vfg * cov[1] + vfb * cov[2];
			float g = vfr * cov[1] + vfg * cov[3] + vfb * cov[4];
			float b = vfr * cov[2] + vfg * cov[4] + vfb * cov[5];

			float m = maximumf(maximumf(fabsf(r), fabsf(g)), fabsf(b));
			if (m > 1e-10f) {
				m = 1.0f / m;
				r *= m;
				g *= m;
				b *= m;
			}

			vfr = r;
			vfg = g;
			vfb = b;
		}

		float len = vfr * vfr + vfg * vfg + vfb * vfb;
		if (len < 1e-10f)
			vec4F_set_scalar(&axis, 0.0f);
		else {
			len = 1.0f / sqrtf(len);
			vfr *= len;
			vfg *= len;
			vfb *= len;
			vec4F_set(&axis, vfr, vfg, vfb, 0);
		}
	}

	// TODO: Try picking the 2 colors with the largest projection onto the axis, instead of computing new colors along the axis.

	if (vec4F_dot(&axis, &axis) < .5f) {
		if (pParams->m_perceptual)
			vec4F_set(&axis, .213f, .715f, .072f, pParams->m_has_alpha ? .715f : 0);
		else
			vec4F_set(&axis, 1.0f, 1.0f, 1.0f, pParams->m_has_alpha ? 1.0f : 0);
		vec4F_normalize_in_place(&axis);
	}

	float l = 1e+9f, h = -1e+9f;

#ifndef IRON_NOSIMD
	if (!pParams->m_has_alpha) {
		// RGB path: axis.m_c[3] == 0 so only R,G,B contribute to the dot product.
		const iron_float32x4_t mr4 = iron_float32x4_load_all(meanColorScaled.m_c[0]);
		const iron_float32x4_t mg4 = iron_float32x4_load_all(meanColorScaled.m_c[1]);
		const iron_float32x4_t mb4 = iron_float32x4_load_all(meanColorScaled.m_c[2]);
		const iron_float32x4_t axv = iron_float32x4_load_all(axis.m_c[0]);
		const iron_float32x4_t ayv = iron_float32x4_load_all(axis.m_c[1]);
		const iron_float32x4_t azv = iron_float32x4_load_all(axis.m_c[2]);
		iron_float32x4_t       lv  = iron_float32x4_load_all(1e+9f);
		iron_float32x4_t       hv  = iron_float32x4_load_all(-1e+9f);
		for (uint32_t i = 0; i < 16; i += 4) {
			const iron_float32x4_t rv = iron_float32x4_sub(iron_float32x4_intrin_load_unaligned(&pix_r_f[i]), mr4);
			const iron_float32x4_t gv = iron_float32x4_sub(iron_float32x4_intrin_load_unaligned(&pix_g_f[i]), mg4);
			const iron_float32x4_t bv = iron_float32x4_sub(iron_float32x4_intrin_load_unaligned(&pix_b_f[i]), mb4);
			iron_float32x4_t       dv = iron_float32x4_mul(rv, axv);
			dv                        = iron_float32x4_add(dv, iron_float32x4_mul(gv, ayv));
			dv                        = iron_float32x4_add(dv, iron_float32x4_mul(bv, azv));
			lv                        = iron_float32x4_min(lv, dv);
			hv                        = iron_float32x4_max(hv, dv);
		}
		l = minimumf(minimumf(iron_float32x4_get(lv, 0), iron_float32x4_get(lv, 1)), minimumf(iron_float32x4_get(lv, 2), iron_float32x4_get(lv, 3)));
		h = maximumf(maximumf(iron_float32x4_get(hv, 0), iron_float32x4_get(hv, 1)), maximumf(iron_float32x4_get(hv, 2), iron_float32x4_get(hv, 3)));
	}
	else
#endif
		for (uint32_t i = 0; i < pParams->m_num_pixels; i++) {
			vec4F color = vec4F_from_color(&pParams->m_pPixels[i]);
			vec4F q     = vec4F_sub(&color, &meanColorScaled);
			float d     = vec4F_dot(&q, &axis);
			l           = minimumf(l, d);
			h           = maximumf(h, d);
		}

	l *= (1.0f / 255.0f);
	h *= (1.0f / 255.0f);

	vec4F b0       = vec4F_mul(&axis, l);
	vec4F b1       = vec4F_mul(&axis, h);
	vec4F c0       = vec4F_add(&meanColor, &b0);
	vec4F c1       = vec4F_add(&meanColor, &b1);
	vec4F minColor = vec4F_saturate(&c0);
	vec4F maxColor = vec4F_saturate(&c1);

	vec4F whiteVec;
	vec4F_set_scalar(&whiteVec, 1.0f);

	if (vec4F_dot(&minColor, &whiteVec) > vec4F_dot(&maxColor, &whiteVec)) {
		float a = minColor.m_c[0], b = minColor.m_c[1], c = minColor.m_c[2], d = minColor.m_c[3];
		minColor.m_c[0] = maxColor.m_c[0];
		minColor.m_c[1] = maxColor.m_c[1];
		minColor.m_c[2] = maxColor.m_c[2];
		minColor.m_c[3] = maxColor.m_c[3];
		maxColor.m_c[0] = a;
		maxColor.m_c[1] = b;
		maxColor.m_c[2] = c;
		maxColor.m_c[3] = d;
	}

	// First find a solution using the block's PCA.
	if (!find_optimal_solution(mode, minColor, maxColor, pParams, pResults))
		return 0;

	return pResults->m_best_overall_err;
}

static void set_block_bits(uint8_t *pBytes, uint32_t val, uint32_t num_bits, uint32_t *pCur_ofs) {
	while (num_bits) {
		const uint32_t n = minimumu(8 - (*pCur_ofs & 7), num_bits);
		pBytes[*pCur_ofs >> 3] |= (uint8_t)(val << (*pCur_ofs & 7));
		val >>= n;
		num_bits -= n;
		*pCur_ofs += n;
	}
}

typedef struct {
	uint32_t      m_mode;
	uint32_t      m_partition;
	uint8_t       m_selectors[16];
	uint8_t       m_alpha_selectors[16];
	color_quad_u8 m_low[3];
	color_quad_u8 m_high[3];
	uint32_t      m_pbits[3][2];
	uint32_t      m_rotation;
	uint32_t      m_index_selector;
} bc7_optimization_results;

static void encode_bc7_block(void *pBlock, const bc7_optimization_results *pResults) {
	const uint32_t best_mode        = pResults->m_mode;
	const uint32_t total_subsets    = g_bc7_num_subsets[best_mode];
	const uint32_t total_partitions = 1 << g_bc7_partition_bits[best_mode];

	const uint8_t *pPartition;
	if (total_subsets == 1)
		pPartition = &g_bc7_partition1[0];
	else if (total_subsets == 2)
		pPartition = &g_bc7_partition2[pResults->m_partition * 16];
	else
		pPartition = &g_bc7_partition3[pResults->m_partition * 16];

	uint8_t color_selectors[16];
	memcpy(color_selectors, pResults->m_selectors, 16);

	uint8_t alpha_selectors[16];
	memcpy(alpha_selectors, pResults->m_alpha_selectors, 16);

	color_quad_u8 low[3], high[3];
	memcpy(low, pResults->m_low, sizeof(low));
	memcpy(high, pResults->m_high, sizeof(high));

	uint32_t pbits[3][2];
	memcpy(pbits, pResults->m_pbits, sizeof(pbits));

	int anchor[3] = {-1, -1, -1};

	for (uint32_t k = 0; k < total_subsets; k++) {
		uint32_t anchor_index = 0;
		if (k) {
			if ((total_subsets == 3) && (k == 1))
				anchor_index = g_bc7_table_anchor_index_third_subset_1[pResults->m_partition];
			else if ((total_subsets == 3) && (k == 2))
				anchor_index = g_bc7_table_anchor_index_third_subset_2[pResults->m_partition];
			else
				anchor_index = g_bc7_table_anchor_index_second_subset[pResults->m_partition];
		}

		anchor[k] = anchor_index;

		const uint32_t color_index_bits  = get_bc7_color_index_size(best_mode, pResults->m_index_selector);
		const uint32_t num_color_indices = 1 << color_index_bits;

		if (color_selectors[anchor_index] & (num_color_indices >> 1)) {
			for (uint32_t i = 0; i < 16; i++)
				if (pPartition[i] == k)
					color_selectors[i] = (uint8_t)((num_color_indices - 1) - color_selectors[i]);

			if (get_bc7_mode_has_seperate_alpha_selectors(best_mode)) {
				for (uint32_t q = 0; q < 3; q++) {
					uint8_t t      = low[k].m_c[q];
					low[k].m_c[q]  = high[k].m_c[q];
					high[k].m_c[q] = t;
				}
			}
			else {
				color_quad_u8 tmp = low[k];
				low[k]            = high[k];
				high[k]           = tmp;
			}

			if (!g_bc7_mode_has_shared_p_bits[best_mode]) {
				uint32_t t  = pbits[k][0];
				pbits[k][0] = pbits[k][1];
				pbits[k][1] = t;
			}
		}

		if (get_bc7_mode_has_seperate_alpha_selectors(best_mode)) {
			const uint32_t alpha_index_bits  = get_bc7_alpha_index_size(best_mode, pResults->m_index_selector);
			const uint32_t num_alpha_indices = 1 << alpha_index_bits;

			if (alpha_selectors[anchor_index] & (num_alpha_indices >> 1)) {
				for (uint32_t i = 0; i < 16; i++)
					if (pPartition[i] == k)
						alpha_selectors[i] = (uint8_t)((num_alpha_indices - 1) - alpha_selectors[i]);

				uint8_t t      = low[k].m_c[3];
				low[k].m_c[3]  = high[k].m_c[3];
				high[k].m_c[3] = t;
			}
		}
	}

	uint8_t *pBlock_bytes = (uint8_t *)(pBlock);
	memset(pBlock_bytes, 0, BC7ENC_BLOCK_SIZE);

	uint32_t cur_bit_ofs = 0;
	set_block_bits(pBlock_bytes, 1 << best_mode, best_mode + 1, &cur_bit_ofs);

	if (total_partitions > 1)
		set_block_bits(pBlock_bytes, pResults->m_partition, (total_partitions == 64) ? 6 : 4, &cur_bit_ofs);

	const uint32_t total_comps = (best_mode >= 4) ? 4 : 3;
	for (uint32_t comp = 0; comp < total_comps; comp++) {
		for (uint32_t subset = 0; subset < total_subsets; subset++) {
			set_block_bits(pBlock_bytes, low[subset].m_c[comp], (comp == 3) ? g_bc7_alpha_precision_table[best_mode] : g_bc7_color_precision_table[best_mode],
			               &cur_bit_ofs);
			set_block_bits(pBlock_bytes, high[subset].m_c[comp], (comp == 3) ? g_bc7_alpha_precision_table[best_mode] : g_bc7_color_precision_table[best_mode],
			               &cur_bit_ofs);
		}
	}

	if (g_bc7_mode_has_p_bits[best_mode]) {
		for (uint32_t subset = 0; subset < total_subsets; subset++) {
			set_block_bits(pBlock_bytes, pbits[subset][0], 1, &cur_bit_ofs);
			if (!g_bc7_mode_has_shared_p_bits[best_mode])
				set_block_bits(pBlock_bytes, pbits[subset][1], 1, &cur_bit_ofs);
		}
	}

	for (uint32_t y = 0; y < 4; y++) {
		for (uint32_t x = 0; x < 4; x++) {
			int idx = x + y * 4;

			uint32_t n = pResults->m_index_selector ? get_bc7_alpha_index_size(best_mode, pResults->m_index_selector)
			                                        : get_bc7_color_index_size(best_mode, pResults->m_index_selector);

			if ((idx == anchor[0]) || (idx == anchor[1]) || (idx == anchor[2]))
				n--;

			set_block_bits(pBlock_bytes, pResults->m_index_selector ? alpha_selectors[idx] : color_selectors[idx], n, &cur_bit_ofs);
		}
	}

	if (get_bc7_mode_has_seperate_alpha_selectors(best_mode)) {
		for (uint32_t y = 0; y < 4; y++) {
			for (uint32_t x = 0; x < 4; x++) {
				int idx = x + y * 4;

				uint32_t n = pResults->m_index_selector ? get_bc7_color_index_size(best_mode, pResults->m_index_selector)
				                                        : get_bc7_alpha_index_size(best_mode, pResults->m_index_selector);

				if ((idx == anchor[0]) || (idx == anchor[1]) || (idx == anchor[2]))
					n--;

				set_block_bits(pBlock_bytes, pResults->m_index_selector ? color_selectors[idx] : alpha_selectors[idx], n, &cur_bit_ofs);
			}
		}
	}
}

static void handle_alpha_block(void *pBlock, const color_quad_u8 *pPixels, const bc7enc_compress_block_params *pComp_params,
                               color_cell_compressor_params *pParams) {
	pParams->m_pSelector_weights    = g_bc7_weights4;
	pParams->m_pSelector_weightsx   = (const vec4F *)g_bc7_weights4x;
	pParams->m_num_selector_weights = 16;
	pParams->m_comp_bits            = 7;
	pParams->m_has_pbits            = BC7ENC_TRUE;
	pParams->m_endpoints_share_pbit = BC7ENC_FALSE;
	pParams->m_has_alpha            = BC7ENC_TRUE;
	pParams->m_perceptual           = pComp_params->m_perceptual;
	pParams->m_num_pixels           = 16;
	pParams->m_pPixels              = pPixels;

	bc7_optimization_results opt_results6, opt_results5, opt_results7;

	color_cell_compressor_results results6;
	results6.m_pSelectors = opt_results6.m_selectors;
	uint8_t selectors_temp[16];
	results6.m_pSelectors_temp = selectors_temp;

	uint64_t best_err  = color_cell_compression(6, pParams, &results6, pComp_params);
	uint32_t best_mode = 6;

	if (best_mode == 6) {
		opt_results6.m_mode           = 6;
		opt_results6.m_partition      = 0;
		opt_results6.m_low[0]         = results6.m_low_endpoint;
		opt_results6.m_high[0]        = results6.m_high_endpoint;
		opt_results6.m_pbits[0][0]    = results6.m_pbits[0];
		opt_results6.m_pbits[0][1]    = results6.m_pbits[1];
		opt_results6.m_rotation       = 0;
		opt_results6.m_index_selector = 0;

		encode_bc7_block(pBlock, &opt_results6);
	}
}

static void handle_opaque_block(void *pBlock, const color_quad_u8 *pPixels, const bc7enc_compress_block_params *pComp_params,
                                color_cell_compressor_params *pParams) {
	uint8_t selectors_temp[16];

	// Mode 6
	bc7_optimization_results opt_results;

	pParams->m_pSelector_weights    = g_bc7_weights4;
	pParams->m_pSelector_weightsx   = (const vec4F *)g_bc7_weights4x;
	pParams->m_num_selector_weights = 16;
	pParams->m_comp_bits            = 7;
	pParams->m_has_pbits            = BC7ENC_TRUE;
	pParams->m_endpoints_share_pbit = BC7ENC_FALSE;
	pParams->m_perceptual           = pComp_params->m_perceptual;
	pParams->m_num_pixels           = 16;
	pParams->m_pPixels              = pPixels;
	pParams->m_has_alpha            = BC7ENC_FALSE;

	color_cell_compressor_results results6;
	results6.m_pSelectors      = opt_results.m_selectors;
	results6.m_pSelectors_temp = selectors_temp;

	uint64_t best_err = color_cell_compression(6, pParams, &results6, pComp_params);

	opt_results.m_mode           = 6;
	opt_results.m_partition      = 0;
	opt_results.m_low[0]         = results6.m_low_endpoint;
	opt_results.m_high[0]        = results6.m_high_endpoint;
	opt_results.m_pbits[0][0]    = results6.m_pbits[0];
	opt_results.m_pbits[0][1]    = results6.m_pbits[1];
	opt_results.m_index_selector = 0;
	opt_results.m_rotation       = 0;

	encode_bc7_block(pBlock, &opt_results);
}

bc7enc_bool bc7enc_compress_block(void *pBlock, const void *pPixelsRGBA, const bc7enc_compress_block_params *pComp_params) {
	const color_quad_u8 *pPixels = (const color_quad_u8 *)(pPixelsRGBA);

	color_cell_compressor_params params;
	if (pComp_params->m_perceptual) {
		// https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.709_conversion
		const float pr_weight = (.5f / (1.0f - .2126f)) * (.5f / (1.0f - .2126f));
		const float pb_weight = (.5f / (1.0f - .0722f)) * (.5f / (1.0f - .0722f));
		params.m_weights[0]   = (int)(pComp_params->m_weights[0] * 4.0f);
		params.m_weights[1]   = (int)(pComp_params->m_weights[1] * 4.0f * pr_weight);
		params.m_weights[2]   = (int)(pComp_params->m_weights[2] * 4.0f * pb_weight);
		params.m_weights[3]   = pComp_params->m_weights[3] * 4;
	}
	else
		memcpy(params.m_weights, pComp_params->m_weights, sizeof(params.m_weights));

	for (uint32_t i = 0; i < 16; i++) {
		if (pPixels[i].m_c[3] < 255) {
			handle_alpha_block(pBlock, pPixels, pComp_params, &params);
			return BC7ENC_TRUE;
		}
	}
	handle_opaque_block(pBlock, pPixels, pComp_params, &params);
	return BC7ENC_FALSE;
}

static uint32_t get_block_bits(const uint8_t *pBytes, uint32_t num_bits, uint32_t *pCur_ofs) {
	uint32_t val       = 0;
	uint32_t bits_read = 0;
	while (num_bits) {
		const uint32_t n     = minimumu(8 - (*pCur_ofs & 7), num_bits);
		const uint32_t chunk = (pBytes[*pCur_ofs >> 3] >> (*pCur_ofs & 7)) & ((1u << n) - 1);
		val |= chunk << bits_read;
		bits_read += n;
		num_bits -= n;
		*pCur_ofs += n;
	}
	return val;
}

void bc7enc_decompress_block(void *pPixelsRGBA, const void *pBlock) {
	const uint8_t *pBytes      = (const uint8_t *)pBlock;
	color_quad_u8 *pPixels     = (color_quad_u8 *)pPixelsRGBA;
	uint32_t       cur_bit_ofs = 0;

	get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t r0 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t r1 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t g0 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t g1 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t b0 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t b1 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t a0 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t a1 = get_block_bits(pBytes, 7, &cur_bit_ofs);
	const uint32_t p0 = get_block_bits(pBytes, 1, &cur_bit_ofs);
	const uint32_t p1 = get_block_bits(pBytes, 1, &cur_bit_ofs);

	color_quad_u8 low, high;
	low.m_c[0]  = (uint8_t)((r0 << 1) | p0);
	low.m_c[1]  = (uint8_t)((g0 << 1) | p0);
	low.m_c[2]  = (uint8_t)((b0 << 1) | p0);
	low.m_c[3]  = (uint8_t)((a0 << 1) | p0);
	high.m_c[0] = (uint8_t)((r1 << 1) | p1);
	high.m_c[1] = (uint8_t)((g1 << 1) | p1);
	high.m_c[2] = (uint8_t)((b1 << 1) | p1);
	high.m_c[3] = (uint8_t)((a1 << 1) | p1);

	for (uint32_t i = 0; i < 16; i++) {
		const uint32_t index = get_block_bits(pBytes, (i == 0) ? 3 : 4, &cur_bit_ofs);
		const uint32_t w     = g_bc7_weights4[index];
		pPixels[i].m_c[0]    = (uint8_t)((low.m_c[0] * (64 - w) + high.m_c[0] * w + 32) >> 6);
		pPixels[i].m_c[1]    = (uint8_t)((low.m_c[1] * (64 - w) + high.m_c[1] * w + 32) >> 6);
		pPixels[i].m_c[2]    = (uint8_t)((low.m_c[2] * (64 - w) + high.m_c[2] * w + 32) >> 6);
		pPixels[i].m_c[3]    = (uint8_t)((low.m_c[3] * (64 - w) + high.m_c[3] * w + 32) >> 6);
	}
}

void bc7enc_decompress(void *pPixelsRGBA, const void *pBC7Blocks, int width, int height) {
	const uint8_t *pBlocks  = (const uint8_t *)pBC7Blocks;
	uint8_t       *dst      = (uint8_t *)pPixelsRGBA;
	const int      blocks_x = (width + 3) / 4;
	const int      blocks_y = (height + 3) / 4;

	for (int by = 0; by < blocks_y; by++) {
		for (int bx = 0; bx < blocks_x; bx++) {
			color_quad_u8 pixels[16];
			bc7enc_decompress_block(pixels, pBlocks + (size_t)(by * blocks_x + bx) * BC7ENC_BLOCK_SIZE);
			for (int py = 0; py < 4; py++) {
				int y = by * 4 + py;
				if (y >= height)
					break;
				for (int px = 0; px < 4; px++) {
					int x = bx * 4 + px;
					if (x >= width)
						continue;
					memcpy(dst + ((size_t)y * width + x) * 4, &pixels[py * 4 + px], 4);
				}
			}
		}
	}
}
