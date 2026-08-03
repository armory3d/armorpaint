
#include "iron_math.h"
#include "iron_gc.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void iron_color_components(uint32_t color, float *red, float *green, float *blue, float *alpha) {
	*alpha = ((color & 0xff000000) >> 24) / 255.0f;
	*red   = ((color & 0x00ff0000) >> 16) / 255.0f;
	*green = ((color & 0x0000ff00) >> 8) / 255.0f;
	*blue  = (color & 0x000000ff) / 255.0f;
}

// xoshiro256** 1.0
static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

static uint64_t s[4] = {1, 2, 3, 4};

uint64_t next(void) {
	const uint64_t result = rotl(s[1] * 5, 7) * 9;

	const uint64_t t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 45);

	return result;
}

void iron_random_init(int64_t seed) {
	s[0] = (uint64_t)seed;
	s[1] = 2;
	s[2] = 3;
	s[3] = 4;
	s[1] = next();
	s[2] = next();
	s[3] = next();
}

int64_t iron_random_get(void) {
	return (int64_t)next();
}

int64_t iron_random_get_max(int64_t max) {
	int64_t value = iron_random_get();
	return (value < -LLONG_MAX ? LLONG_MAX : llabs(value)) % (max + 1);
}

int64_t iron_random_get_in(int64_t min, int64_t max) {
	int64_t value = iron_random_get();
	return (value < -LLONG_MAX ? LLONG_MAX : llabs(value)) % (max + 1 - min) + min;
}

// ██╗   ██╗███████╗ ██████╗██████╗
// ██║   ██║██╔════╝██╔════╝╚════██╗
// ██║   ██║█████╗  ██║      █████╔╝
// ╚██╗ ██╔╝██╔══╝  ██║     ██╔═══╝
//  ╚████╔╝ ███████╗╚██████╗███████╗
//   ╚═══╝  ╚══════╝ ╚═════╝╚══════╝

float vec2_len(vec2_t v) {
	return sqrtf(v.x * v.x + v.y * v.y);
}

vec2_t vec2_set_len(vec2_t v, float length) {
	float current_length = vec2_len(v);
	if (current_length == 0) {
		return v;
	}
	float mul = length / current_length;
	v.x *= mul;
	v.y *= mul;
	return v;
}

vec2_t vec2_mult(vec2_t v, float f) {
	v.x *= f;
	v.y *= f;
	return v;
}

vec2_t vec2_add(vec2_t a, vec2_t b) {
	a.x += b.x;
	a.y += b.y;
	return a;
}

vec2_t vec2_sub(vec2_t a, vec2_t b) {
	a.x -= b.x;
	a.y -= b.y;
	return a;
}

float vec2_cross(vec2_t a, vec2_t b) {
	return a.x * b.y - a.y * b.x;
}

vec2_t vec2_norm(vec2_t v) {
	float n = vec2_len(v);
	if (n > 0.0) {
		float inv_n = 1.0f / n;
		v.x *= inv_n;
		v.y *= inv_n;
	}
	return v;
}

float vec2_dot(vec2_t a, vec2_t b) {
	return a.x * b.x + a.y * b.y;
}

vec2_t vec2_nan() {
	vec2_t v;
	v.x = NAN;
	return v;
}

bool vec2_isnan(vec2_t v) {
	return isnan(v.x);
}

// ██╗   ██╗███████╗ ██████╗██╗  ██╗
// ██║   ██║██╔════╝██╔════╝██║  ██║
// ██║   ██║█████╗  ██║     ███████║
// ╚██╗ ██╔╝██╔══╝  ██║     ╚════██║
//  ╚████╔╝ ███████╗╚██████╗     ██║
//   ╚═══╝  ╚══════╝ ╚═════╝     ╚═╝

f32_array_t *vec3_to_f32_array(vec4_t v) {
	f32_array_t *res = f32_array_create(3);
	res->buffer[0]   = v.x;
	res->buffer[1]   = v.y;
	res->buffer[2]   = v.z;
	return res;
}

vec4_t vec4_cross(vec4_t a, vec4_t b) {
	vec4_t v;
	v.x = a.y * b.z - a.z * b.y;
	v.y = a.z * b.x - a.x * b.z;
	v.z = a.x * b.y - a.y * b.x;
	return v;
}

vec4_t vec4_add(vec4_t a, vec4_t b) {
	a.x += b.x;
	a.y += b.y;
	a.z += b.z;
	a.w += b.w;
	return a;
}

vec4_t vec4_fadd(vec4_t a, float x, float y, float z, float w) {
	a.x += x;
	a.y += y;
	a.z += z;
	a.w += w;
	return a;
}

vec4_t vec4_norm(vec4_t a) {
	float n = vec4_len(a);
	if (n > 0.0) {
		float inv_n = 1.0f / n;
		a.x *= inv_n;
		a.y *= inv_n;
		a.z *= inv_n;
	}
	return a;
}

vec4_t vec4_mult(vec4_t a, float f) {
	a.x *= f;
	a.y *= f;
	a.z *= f;
	a.w *= f;
	return a;
}

float vec4_dot(vec4_t a, vec4_t b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec4_t vec4_apply_proj(vec4_t a, mat4_t m) {
	vec4_t v;
	float  d = 1.0 / (m.m[3] * a.x + m.m[7] * a.y + m.m[11] * a.z + m.m[15]); // Perspective divide
	v.x      = (m.m[0] * a.x + m.m[4] * a.y + m.m[8] * a.z + m.m[12]) * d;
	v.y      = (m.m[1] * a.x + m.m[5] * a.y + m.m[9] * a.z + m.m[13]) * d;
	v.z      = (m.m[2] * a.x + m.m[6] * a.y + m.m[10] * a.z + m.m[14]) * d;
	return v;
}

vec4_t vec4_apply_mat4(vec4_t a, mat4_t m) {
	vec4_t v;
	v.x = m.m[0] * a.x + m.m[4] * a.y + m.m[8] * a.z + m.m[12] * a.w;
	v.y = m.m[1] * a.x + m.m[5] * a.y + m.m[9] * a.z + m.m[13] * a.w;
	v.z = m.m[2] * a.x + m.m[6] * a.y + m.m[10] * a.z + m.m[14] * a.w;
	v.w = m.m[3] * a.x + m.m[7] * a.y + m.m[11] * a.z + m.m[15] * a.w;
	return v;
}

vec4_t vec4_apply_axis_angle(vec4_t a, vec4_t axis, float angle) {
	quat_t q = quat_from_axis_angle(axis, angle);
	return vec4_apply_quat(a, q);
}

vec4_t vec4_apply_quat(vec4_t a, quat_t q) {
	vec4_t v;
	float  ix = q.w * a.x + q.y * a.z - q.z * a.y;
	float  iy = q.w * a.y + q.z * a.x - q.x * a.z;
	float  iz = q.w * a.z + q.x * a.y - q.y * a.x;
	float  iw = -q.x * a.x - q.y * a.y - q.z * a.z;
	v.x       = ix * q.w + iw * -q.x + iy * -q.z - iz * -q.y;
	v.y       = iy * q.w + iw * -q.y + iz * -q.x - ix * -q.z;
	v.z       = iz * q.w + iw * -q.z + ix * -q.y - iy * -q.x;
	return v;
}

bool vec4_equals(vec4_t a, vec4_t b) {
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool vec4_almost_equals(vec4_t a, vec4_t b, float prec) {
	return fabs(a.x - b.x) < prec && fabs(a.y - b.y) < prec && fabs(a.z - b.z) < prec;
}

float vec4_len(vec4_t a) {
	return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

vec4_t vec4_sub(vec4_t a, vec4_t b) {
	a.x -= b.x;
	a.y -= b.y;
	a.z -= b.z;
	return a;
}

float vec4_dist(vec4_t v1, vec4_t v2) {
	return vec4_fdist(v1.x, v1.y, v1.z, v2.x, v2.y, v2.z);
}

float vec4_fdist(float v1x, float v1y, float v1z, float v2x, float v2y, float v2z) {
	float vx = v1x - v2x;
	float vy = v1y - v2y;
	float vz = v1z - v2z;
	return sqrtf(vx * vx + vy * vy + vz * vz);
}

vec4_t vec4_reflect(vec4_t a, vec4_t n) {
	float d = 2 * vec4_dot(a, n);
	a.x -= d * n.x;
	a.y -= d * n.y;
	a.z -= d * n.z;
	return a;
}

vec4_t vec4_clamp(vec4_t a, float min, float max) {
	float l = vec4_len(a);
	if (l < min) {
		return vec4_mult(vec4_norm(a), min);
	}
	else if (l > max) {
		return vec4_mult(vec4_norm(a), max);
	}
	return a;
}

vec4_t vec4_x_axis() {
	return (vec4_t){1.0, 0.0, 0.0, 1.0};
}

vec4_t vec4_y_axis() {
	return (vec4_t){0.0, 1.0, 0.0, 1.0};
}

vec4_t vec4_z_axis() {
	return (vec4_t){0.0, 0.0, 1.0, 1.0};
}

vec4_t vec4_nan() {
	vec4_t v;
	v.x = NAN;
	return v;
}

bool vec4_isnan(vec4_t v) {
	return isnan(v.x);
}

//  ██████╗ ██╗   ██╗ █████╗ ████████╗
// ██╔═══██╗██║   ██║██╔══██╗╚══██╔══╝
// ██║   ██║██║   ██║███████║   ██║
// ██║▄▄ ██║██║   ██║██╔══██║   ██║
// ╚██████╔╝╚██████╔╝██║  ██║   ██║
//  ╚══▀▀═╝  ╚═════╝ ╚═╝  ╚═╝   ╚═╝

quat_t quat_from_axis_angle(vec4_t axis, float angle) {
	float  s = sinf(angle * 0.5);
	quat_t q;
	q.x = axis.x * s;
	q.y = axis.y * s;
	q.z = axis.z * s;
	q.w = cosf(angle * 0.5);
	return quat_norm(q);
}

quat_t quat_from_mat(mat4_t m) {
	m = mat4_to_rot(m);
	return quat_from_rot_mat(m);
}

quat_t quat_from_rot_mat(mat4_t m) {
	// Assumes the upper 3x3 is a pure rotation matrix
	float  m11 = m.m[0];
	float  m12 = m.m[4];
	float  m13 = m.m[8];
	float  m21 = m.m[1];
	float  m22 = m.m[5];
	float  m23 = m.m[9];
	float  m31 = m.m[2];
	float  m32 = m.m[6];
	float  m33 = m.m[10];
	float  tr  = m11 + m22 + m33;
	float  s   = 0.0;
	quat_t q;

	if (tr > 0) {
		s   = 0.5 / sqrtf(tr + 1.0);
		q.w = 0.25 / s;
		q.x = (m32 - m23) * s;
		q.y = (m13 - m31) * s;
		q.z = (m21 - m12) * s;
	}
	else if (m11 > m22 && m11 > m33) {
		s   = 2.0 * sqrtf(1.0 + m11 - m22 - m33);
		q.w = (m32 - m23) / s;
		q.x = 0.25 * s;
		q.y = (m12 + m21) / s;
		q.z = (m13 + m31) / s;
	}
	else if (m22 > m33) {
		s   = 2.0 * sqrtf(1.0 + m22 - m11 - m33);
		q.w = (m13 - m31) / s;
		q.x = (m12 + m21) / s;
		q.y = 0.25 * s;
		q.z = (m23 + m32) / s;
	}
	else {
		s   = 2.0 * sqrtf(1.0 + m33 - m11 - m22);
		q.w = (m21 - m12) / s;
		q.x = (m13 + m31) / s;
		q.y = (m23 + m32) / s;
		q.z = 0.25 * s;
	}
	return quat_norm(q);
}

quat_t quat_mult(quat_t a, quat_t b) {
	quat_t q;
	q.x = a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y;
	q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return q;
}

quat_t quat_norm(quat_t q) {
	float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (l == 0.0) {
		q.x = 0;
		q.y = 0;
		q.z = 0;
		q.w = 0;
	}
	else {
		l = 1.0 / l;
		q.x *= l;
		q.y *= l;
		q.z *= l;
		q.w *= l;
	}
	return q;
}

vec4_t quat_get_euler(quat_t q) {
	float a = -2 * (q.x * q.z - q.w * q.y);
	float b = q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z;
	float c = 2 * (q.x * q.y + q.w * q.z);
	float d = -2 * (q.y * q.z - q.w * q.x);
	float e = q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z;
	return (vec4_t){atan2f(d, e), atan2f(a, b), asinf(c), 1.0};
}

quat_t quat_from_euler(float x, float y, float z) {
	float f  = x / 2;
	float c1 = cosf(f);
	float s1 = sinf(f);
	f        = y / 2;
	float c2 = cosf(f);
	float s2 = sinf(f);
	f        = z / 2;
	float c3 = cosf(f);
	float s3 = sinf(f);
	// YZX
	quat_t q;
	q.x = s1 * c2 * c3 + c1 * s2 * s3;
	q.y = c1 * s2 * c3 + s1 * c2 * s3;
	q.z = c1 * c2 * s3 - s1 * s2 * c3;
	q.w = c1 * c2 * c3 - s1 * s2 * s3;
	return q;
}

float quat_dot(quat_t a, quat_t b) {
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
}

quat_t quat_from_to(vec4_t v0, vec4_t v1) {
	// Rotation formed by direction vectors
	// v0 and v1 should be normalized first
	float dot = vec4_dot(v0, v1);
	if (dot < -0.999999) {
		vec4_t a = vec4_cross(vec4_x_axis(), v0);
		if (vec4_len(a) < 0.000001) {
			a = vec4_cross(vec4_y_axis(), v0);
		}
		a = vec4_norm(a);
		return quat_from_axis_angle(a, IRON_PI);
	}
	else if (dot > 0.999999) {
		return (quat_t){0, 0, 0, 1};
	}
	else {
		vec4_t a = vec4_cross(v0, v1);
		quat_t q = (quat_t){a.x, a.y, a.z, 1.0 + dot};
		return quat_norm(q);
	}
}

quat_t quat_inv(quat_t q) {
	float l = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	l       = -1.0 / l;
	q.x     = q.x * l;
	q.y     = q.y * l;
	q.z     = q.z * l;
	q.w     = -q.w * l;
	return q;
}

quat_t quat_slerp(quat_t a, quat_t b, float t) {
	float dot = quat_dot(a, b);
	if (dot < 0.0f) {
		b.x = -b.x;
		b.y = -b.y;
		b.z = -b.z;
		b.w = -b.w;
		dot = -dot;
	}
	if (dot > 0.9995f) {
		quat_t r = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w)};
		return quat_norm(r);
	}
	float theta_0 = acosf(dot);
	float theta   = theta_0 * t;
	float sin_t   = sinf(theta);
	float sin_t0  = sinf(theta_0);
	float s0      = cosf(theta) - dot * sin_t / sin_t0;
	float s1      = sin_t / sin_t0;
	return (quat_t){s0 * a.x + s1 * b.x, s0 * a.y + s1 * b.y, s0 * a.z + s1 * b.z, s0 * a.w + s1 * b.w};
}

// ███╗   ███╗ █████╗ ████████╗██████╗
// ████╗ ████║██╔══██╗╚══██╔══╝╚════██╗
// ██╔████╔██║███████║   ██║    █████╔╝
// ██║╚██╔╝██║██╔══██║   ██║    ╚═══██╗
// ██║ ╚═╝ ██║██║  ██║   ██║   ██████╔╝
// ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═════╝

mat3_t mat3_identity() {
	return (mat3_t){1, 0, 0, 0, 1, 0, 0, 0, 1};
}

mat3_t mat3_translation(float x, float y) {
	return (mat3_t){1, 0, 0, 0, 1, 0, x, y, 1};
}

mat3_t mat3_rotation(float alpha) {
	return (mat3_t){cosf(alpha), sinf(alpha), 0, -sinf(alpha), cosf(alpha), 0, 0, 0, 1};
}

mat3_t mat3_scale(mat3_t m, vec4_t v) {
	m.m[0] *= v.x;
	m.m[1] *= v.x;
	m.m[2] *= v.x;
	m.m[3] *= v.y;
	m.m[4] *= v.y;
	m.m[5] *= v.y;
	m.m[6] *= v.z;
	m.m[7] *= v.z;
	m.m[8] *= v.z;
	return m;
}

mat3_t mat3_set_from4(mat4_t m4) {
	mat3_t m;
	m.m[0] = m4.m[0];
	m.m[1] = m4.m[1];
	m.m[2] = m4.m[2];
	m.m[3] = m4.m[4];
	m.m[4] = m4.m[5];
	m.m[5] = m4.m[6];
	m.m[6] = m4.m[8];
	m.m[7] = m4.m[9];
	m.m[8] = m4.m[10];
	return m;
}

mat3_t mat3_multmat(mat3_t a, mat3_t b) {
	return (mat3_t){a.m[0] * b.m[0] + a.m[3] * b.m[1] + a.m[6] * b.m[2], a.m[1] * b.m[0] + a.m[4] * b.m[1] + a.m[7] * b.m[2],
	                a.m[2] * b.m[0] + a.m[5] * b.m[1] + a.m[8] * b.m[2], a.m[0] * b.m[3] + a.m[3] * b.m[4] + a.m[6] * b.m[5],
	                a.m[1] * b.m[3] + a.m[4] * b.m[4] + a.m[7] * b.m[5], a.m[2] * b.m[3] + a.m[5] * b.m[4] + a.m[8] * b.m[5],
	                a.m[0] * b.m[6] + a.m[3] * b.m[7] + a.m[6] * b.m[8], a.m[1] * b.m[6] + a.m[4] * b.m[7] + a.m[7] * b.m[8],
	                a.m[2] * b.m[6] + a.m[5] * b.m[7] + a.m[8] * b.m[8]};
}

mat3_t mat3_transpose(mat3_t m) {
	float f = m.m[1];
	m.m[1]  = m.m[3];
	m.m[3]  = f;

	f      = m.m[2];
	m.m[2] = m.m[6];
	m.m[6] = f;

	f      = m.m[5];
	m.m[5] = m.m[7];
	m.m[7] = f;

	return m;
}

mat3_t mat3_nan() {
	mat3_t m;
	m.m[0] = NAN;
	return m;
}

bool mat3_isnan(mat3_t m) {
	return isnan(m.m[0]);
}

// ███╗   ███╗ █████╗ ████████╗██╗  ██╗
// ████╗ ████║██╔══██╗╚══██╔══╝██║  ██║
// ██╔████╔██║███████║   ██║   ███████║
// ██║╚██╔╝██║██╔══██║   ██║   ╚════██║
// ██║ ╚═╝ ██║██║  ██║   ██║        ██║
// ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝        ╚═╝

mat4_t mat4_identity() {
	return (mat4_t){1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
}

mat4_t mat4_from_f32_array(f32_array_t *a, int offset) {
	mat4_t m;
	memcpy(m.m, a->buffer + offset, 16 * sizeof(float));
	return m;
}

f32_array_t *mat4_to_f32_array(mat4_t m) {
	f32_array_t *a = f32_array_create(16);
	memcpy(a->buffer, m.m, 16 * sizeof(float));
	return a;
}

mat4_t mat4_persp(float fov_y, float aspect, float zn, float zf) {
	float uh = 1.0 / tanf(fov_y / 2);
	float uw = uh / aspect;
	return (mat4_t){uw, 0, 0, 0, 0, uh, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1, 0, 0, 2 * zf * zn / (zn - zf), 0};
}

mat4_t mat4_ortho(float left, float right, float bottom, float top, float znear, float zfar) {
	float rl = right - left;
	float tb = top - bottom;
	float fn = zfar - znear;
	float tx = -(right + left) / (rl);
	float ty = -(top + bottom) / (tb);
	float tz = -(zfar + znear) / (fn);
	return (mat4_t){2 / rl, 0, 0, 0, 0, 2 / tb, 0, 0, 0, 0, -2 / fn, 0, tx, ty, tz, 1};
}

mat4_t mat4_rot_z(float alpha) {
	float ca = cosf(alpha);
	float sa = sinf(alpha);
	return (mat4_t){ca, sa, 0, 0, -sa, ca, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

mat4_t mat4_compose(vec4_t loc, quat_t rot, vec4_t scl) {
	mat4_t m = mat4_from_quat(rot);
	m        = mat4_scale(m, scl);
	m        = mat4_set_loc(m, loc);
	return m;
}

mat4_t mat4_tween(mat4_t a, mat4_t b, float t) {
	mat4_decomposed_t *da  = mat4_decompose(a);
	mat4_decomposed_t *db  = mat4_decompose(b);
	vec4_t             loc = {da->loc.x + t * (db->loc.x - da->loc.x), da->loc.y + t * (db->loc.y - da->loc.y), da->loc.z + t * (db->loc.z - da->loc.z), 1.0f};
	vec4_t             scl = {da->scl.x + t * (db->scl.x - da->scl.x), da->scl.y + t * (db->scl.y - da->scl.y), da->scl.z + t * (db->scl.z - da->scl.z), 1.0f};
	quat_t             rot = quat_slerp(da->rot, db->rot, t);
	return mat4_compose(loc, rot, scl);
}

mat4_decomposed_t *mat4_decompose(mat4_t m) {
	vec4_t loc;
	quat_t rot;
	vec4_t scl;
	loc.x = m.m[12];
	loc.y = m.m[13];
	loc.z = m.m[14];
	scl.x = vec4_len((vec4_t){m.m[0], m.m[1], m.m[2], 1.0});
	scl.y = vec4_len((vec4_t){m.m[4], m.m[5], m.m[6], 1.0});
	scl.z = vec4_len((vec4_t){m.m[8], m.m[9], m.m[10], 1.0});
	if (mat4_determinant(m) < 0.0) {
		scl.x = -scl.x;
	}
	float invs = 1.0 / scl.x; // Scale the rotation part
	m.m[0] *= invs;
	m.m[1] *= invs;
	m.m[2] *= invs;
	invs = 1.0 / scl.y;
	m.m[4] *= invs;
	m.m[5] *= invs;
	m.m[6] *= invs;
	invs = 1.0 / scl.z;
	m.m[8] *= invs;
	m.m[9] *= invs;
	m.m[10] *= invs;
	rot = quat_from_rot_mat(m);

	mat4_decomposed_t *dec = gc_alloc(sizeof(mat4_decomposed_t));
	dec->loc               = loc;
	dec->rot               = rot;
	dec->scl               = scl;
	return dec;
}

mat4_t mat4_set_loc(mat4_t m, vec4_t v) {
	m.m[12] = v.x;
	m.m[13] = v.y;
	m.m[14] = v.z;
	return m;
}

mat4_t mat4_from_quat(quat_t q) {
	float x2 = q.x + q.x;
	float y2 = q.y + q.y;
	float z2 = q.z + q.z;
	float xx = q.x * x2;
	float xy = q.x * y2;
	float xz = q.x * z2;
	float yy = q.y * y2;
	float yz = q.y * z2;
	float zz = q.z * z2;
	float wx = q.w * x2;
	float wy = q.w * y2;
	float wz = q.w * z2;

	mat4_t m;
	m.m[0] = 1.0 - (yy + zz);
	m.m[4] = xy - wz;
	m.m[8] = xz + wy;

	m.m[1] = xy + wz;
	m.m[5] = 1.0 - (xx + zz);
	m.m[9] = yz - wx;

	m.m[2]  = xz - wy;
	m.m[6]  = yz + wx;
	m.m[10] = 1.0 - (xx + yy);

	m.m[3]  = 0.0;
	m.m[7]  = 0.0;
	m.m[11] = 0.0;
	m.m[12] = 0.0;
	m.m[13] = 0.0;
	m.m[14] = 0.0;
	m.m[15] = 1.0;

	return m;
}

mat4_t mat4_translate(mat4_t m, float x, float y, float z) {
	m.m[0] += x * m.m[3];
	m.m[1] += y * m.m[3];
	m.m[2] += z * m.m[3];
	m.m[4] += x * m.m[7];
	m.m[5] += y * m.m[7];
	m.m[6] += z * m.m[7];
	m.m[8] += x * m.m[11];
	m.m[9] += y * m.m[11];
	m.m[10] += z * m.m[11];
	m.m[12] += x * m.m[15];
	m.m[13] += y * m.m[15];
	m.m[14] += z * m.m[15];
	return m;
}

mat4_t mat4_scale(mat4_t m, vec4_t v) {
	m.m[0] *= v.x;
	m.m[1] *= v.x;
	m.m[2] *= v.x;
	m.m[3] *= v.x;
	m.m[4] *= v.y;
	m.m[5] *= v.y;
	m.m[6] *= v.y;
	m.m[7] *= v.y;
	m.m[8] *= v.z;
	m.m[9] *= v.z;
	m.m[10] *= v.z;
	m.m[11] *= v.z;
	return m;
}

mat4_t mat4_mult_mat3x4(mat4_t a, mat4_t b) {
	float a00 = a.m[0];
	float a01 = a.m[1];
	float a02 = a.m[2];
	float a03 = a.m[3];
	float a10 = a.m[4];
	float a11 = a.m[5];
	float a12 = a.m[6];
	float a13 = a.m[7];
	float a20 = a.m[8];
	float a21 = a.m[9];
	float a22 = a.m[10];
	float a23 = a.m[11];
	float a30 = a.m[12];
	float a31 = a.m[13];
	float a32 = a.m[14];
	float a33 = a.m[15];
	float b0  = b.m[0];
	float b1  = b.m[4];
	float b2  = b.m[8];
	float b3  = b.m[12];

	mat4_t m;
	m.m[0]  = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	m.m[4]  = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	m.m[8]  = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	m.m[12] = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	b0      = b.m[1];
	b1      = b.m[5];
	b2      = b.m[9];
	b3      = b.m[13];
	m.m[1]  = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	m.m[5]  = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	m.m[9]  = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	m.m[13] = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	b0      = b.m[2];
	b1      = b.m[6];
	b2      = b.m[10];
	b3      = b.m[14];
	m.m[2]  = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	m.m[6]  = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	m.m[10] = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	m.m[14] = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	m.m[3]  = 0;
	m.m[7]  = 0;
	m.m[11] = 0;
	m.m[15] = 1;
	return m;
}

mat4_t mat4_mult_mat(mat4_t a, mat4_t b) {
	float a00 = a.m[0];
	float a01 = a.m[1];
	float a02 = a.m[2];
	float a03 = a.m[3];
	float a10 = a.m[4];
	float a11 = a.m[5];
	float a12 = a.m[6];
	float a13 = a.m[7];
	float a20 = a.m[8];
	float a21 = a.m[9];
	float a22 = a.m[10];
	float a23 = a.m[11];
	float a30 = a.m[12];
	float a31 = a.m[13];
	float a32 = a.m[14];
	float a33 = a.m[15];

	float b0 = b.m[0];
	float b1 = b.m[4];
	float b2 = b.m[8];
	float b3 = b.m[12];
	a.m[0]   = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	a.m[4]   = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	a.m[8]   = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	a.m[12]  = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	b0      = b.m[1];
	b1      = b.m[5];
	b2      = b.m[9];
	b3      = b.m[13];
	a.m[1]  = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	a.m[5]  = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	a.m[9]  = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	a.m[13] = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	b0      = b.m[2];
	b1      = b.m[6];
	b2      = b.m[10];
	b3      = b.m[14];
	a.m[2]  = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	a.m[6]  = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	a.m[10] = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	a.m[14] = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	b0      = b.m[3];
	b1      = b.m[7];
	b2      = b.m[11];
	b3      = b.m[15];
	a.m[3]  = a00 * b0 + a01 * b1 + a02 * b2 + a03 * b3;
	a.m[7]  = a10 * b0 + a11 * b1 + a12 * b2 + a13 * b3;
	a.m[11] = a20 * b0 + a21 * b1 + a22 * b2 + a23 * b3;
	a.m[15] = a30 * b0 + a31 * b1 + a32 * b2 + a33 * b3;

	return a;
}

mat4_t mat4_inv(mat4_t a) {
	float a00 = a.m[0];
	float a01 = a.m[1];
	float a02 = a.m[2];
	float a03 = a.m[3];
	float a10 = a.m[4];
	float a11 = a.m[5];
	float a12 = a.m[6];
	float a13 = a.m[7];
	float a20 = a.m[8];
	float a21 = a.m[9];
	float a22 = a.m[10];
	float a23 = a.m[11];
	float a30 = a.m[12];
	float a31 = a.m[13];
	float a32 = a.m[14];
	float a33 = a.m[15];
	float b00 = a00 * a11 - a01 * a10;
	float b01 = a00 * a12 - a02 * a10;
	float b02 = a00 * a13 - a03 * a10;
	float b03 = a01 * a12 - a02 * a11;
	float b04 = a01 * a13 - a03 * a11;
	float b05 = a02 * a13 - a03 * a12;
	float b06 = a20 * a31 - a21 * a30;
	float b07 = a20 * a32 - a22 * a30;
	float b08 = a20 * a33 - a23 * a30;
	float b09 = a21 * a32 - a22 * a31;
	float b10 = a21 * a33 - a23 * a31;
	float b11 = a22 * a33 - a23 * a32;

	float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	if (det == 0.0) {
		return mat4_identity();
	}
	det = 1.0 / det;

	mat4_t m;
	m.m[0]  = (a11 * b11 - a12 * b10 + a13 * b09) * det;
	m.m[1]  = (a02 * b10 - a01 * b11 - a03 * b09) * det;
	m.m[2]  = (a31 * b05 - a32 * b04 + a33 * b03) * det;
	m.m[3]  = (a22 * b04 - a21 * b05 - a23 * b03) * det;
	m.m[4]  = (a12 * b08 - a10 * b11 - a13 * b07) * det;
	m.m[5]  = (a00 * b11 - a02 * b08 + a03 * b07) * det;
	m.m[6]  = (a32 * b02 - a30 * b05 - a33 * b01) * det;
	m.m[7]  = (a20 * b05 - a22 * b02 + a23 * b01) * det;
	m.m[8]  = (a10 * b10 - a11 * b08 + a13 * b06) * det;
	m.m[9]  = (a01 * b08 - a00 * b10 - a03 * b06) * det;
	m.m[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
	m.m[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
	m.m[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
	m.m[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
	m.m[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
	m.m[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;

	return m;
}

mat4_t mat4_transpose(mat4_t m) {
	float f = m.m[1];
	m.m[1]  = m.m[4];
	m.m[4]  = f;

	f      = m.m[2];
	m.m[2] = m.m[8];
	m.m[8] = f;

	f       = m.m[3];
	m.m[3]  = m.m[12];
	m.m[12] = f;

	f      = m.m[6];
	m.m[6] = m.m[9];
	m.m[9] = f;

	f       = m.m[7];
	m.m[7]  = m.m[13];
	m.m[13] = f;

	f       = m.m[11];
	m.m[11] = m.m[14];
	m.m[14] = f;

	return m;
}

mat4_t mat4_transpose3(mat4_t m) {
	float f = m.m[1];
	m.m[1]  = m.m[4];
	m.m[4]  = f;

	f      = m.m[2];
	m.m[2] = m.m[8];
	m.m[8] = f;

	f      = m.m[6];
	m.m[6] = m.m[9];
	m.m[9] = f;

	return m;
}

vec4_t mat4_get_loc(mat4_t m) {
	return (vec4_t){m.m[12], m.m[13], m.m[14], m.m[15]};
}

vec4_t mat4_get_scale(mat4_t m) {
	return (vec4_t){sqrtf(m.m[0] * m.m[0] + m.m[4] * m.m[4] + m.m[8] * m.m[8]), sqrtf(m.m[1] * m.m[1] + m.m[5] * m.m[5] + m.m[9] * m.m[9]),
	                sqrtf(m.m[2] * m.m[2] + m.m[6] * m.m[6] + m.m[10] * m.m[10]), 1.0};
}

mat4_t mat4_mult(mat4_t m, float s) {
	m.m[0] *= s;
	m.m[1] *= s;
	m.m[2] *= s;
	m.m[3] *= s;
	m.m[4] *= s;
	m.m[5] *= s;
	m.m[6] *= s;
	m.m[7] *= s;
	m.m[8] *= s;
	m.m[9] *= s;
	m.m[10] *= s;
	m.m[11] *= s;
	m.m[12] *= s;
	m.m[13] *= s;
	m.m[14] *= s;
	m.m[15] *= s;
	return m;
}

mat4_t mat4_to_rot(mat4_t m) {
	float scale = 1.0 / vec4_len((vec4_t){m.m[0], m.m[1], m.m[2], 1.0});
	m.m[0]      = m.m[0] * scale;
	m.m[1]      = m.m[1] * scale;
	m.m[2]      = m.m[2] * scale;
	scale       = 1.0 / vec4_len((vec4_t){m.m[4], m.m[5], m.m[6], 1.0});
	m.m[4]      = m.m[4] * scale;
	m.m[5]      = m.m[5] * scale;
	m.m[6]      = m.m[6] * scale;
	scale       = 1.0 / vec4_len((vec4_t){m.m[8], m.m[9], m.m[10], 1.0});
	m.m[8]      = m.m[8] * scale;
	m.m[9]      = m.m[9] * scale;
	m.m[10]     = m.m[10] * scale;
	m.m[3]      = 0.0;
	m.m[7]      = 0.0;
	m.m[11]     = 0.0;
	m.m[12]     = 0.0;
	m.m[13]     = 0.0;
	m.m[14]     = 0.0;
	m.m[15]     = 1.0;
	return m;
}

vec4_t mat4_right(mat4_t m) {
	return vec4_norm((vec4_t){m.m[0], m.m[1], m.m[2], 1.0});
}

vec4_t mat4_look(mat4_t m) {
	return vec4_norm((vec4_t){m.m[4], m.m[5], m.m[6], 1.0});
}

vec4_t mat4_up(mat4_t m) {
	return vec4_norm((vec4_t){m.m[8], m.m[9], m.m[10], 1.0});
}

float mat4_cofactor(float m0, float m1, float m2, float m3, float m4, float m5, float m6, float m7, float m8) {
	return m0 * (m4 * m8 - m5 * m7) - m1 * (m3 * m8 - m5 * m6) + m2 * (m3 * m7 - m4 * m6);
}

float mat4_determinant(mat4_t m) {
	float c00 = mat4_cofactor(m.m[5], m.m[9], m.m[13], m.m[6], m.m[10], m.m[14], m.m[7], m.m[11], m.m[15]);
	float c01 = mat4_cofactor(m.m[4], m.m[8], m.m[12], m.m[6], m.m[10], m.m[14], m.m[7], m.m[11], m.m[15]);
	float c02 = mat4_cofactor(m.m[4], m.m[8], m.m[12], m.m[5], m.m[9], m.m[13], m.m[7], m.m[11], m.m[15]);
	float c03 = mat4_cofactor(m.m[4], m.m[8], m.m[12], m.m[5], m.m[9], m.m[13], m.m[6], m.m[10], m.m[14]);
	return m.m[0] * c00 - m.m[1] * c01 + m.m[2] * c02 - m.m[3] * c03;
}

mat4_t mat4_nan() {
	mat4_t m;
	m.m[0] = NAN;
	return m;
}

bool mat4_isnan(mat4_t m) {
	return isnan(m.m[0]);
}
