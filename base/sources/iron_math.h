/*! \file iron_math.h
 *  \brief Vector/matrix/quaternion math. \ingroup math
 */
#pragma once

#include "iron_array.h"
#include "iron_global.h"
#include <stdint.h>

#define IRON_PI  3.14159265358979323846
#define IRON_TAU 6.283185307

typedef struct vec2 {
	float x;
	float y;
} vec2_t;

typedef struct vec3 {
	float x;
	float y;
	float z;
} vec3_t;

typedef struct vec4 {
	float x;
	float y;
	float z;
	float w;
} vec4_t;

typedef struct quat {
	float x;
	float y;
	float z;
	float w;
} quat_t;

typedef union mat3 {
	float m[3 * 3];
	struct {
		float m00;
		float m01;
		float m02;
		float m10;
		float m11;
		float m12;
		float m20;
		float m21;
		float m22;
	};
} mat3_t;

typedef union mat4 {
	float m[4 * 4];
	struct {
		float m00;
		float m01;
		float m02;
		float m03;
		float m10;
		float m11;
		float m12;
		float m13;
		float m20;
		float m21;
		float m22;
		float m23;
		float m30;
		float m31;
		float m32;
		float m33;
	};
} mat4_t;

void    iron_color_components(uint32_t color, float *red, float *green, float *blue, float *alpha);
void    iron_random_init(int64_t seed);
int64_t iron_random_get(void);
int64_t iron_random_get_max(int64_t max);
int64_t iron_random_get_in(int64_t min, int64_t max);

float  vec2_len(vec2_t v);
vec2_t vec2_set_len(vec2_t v, float length);
vec2_t vec2_mult(vec2_t a, float f);
vec2_t vec2_add(vec2_t a, vec2_t b);
vec2_t vec2_sub(vec2_t a, vec2_t b);
float  vec2_cross(vec2_t a, vec2_t b);
vec2_t vec2_norm(vec2_t v);
float  vec2_dot(vec2_t a, vec2_t b);
vec2_t vec2_nan();
bool   vec2_isnan(vec2_t v);

f32_array_t *vec3_to_f32_array(vec4_t v);

vec4_t vec4_cross(vec4_t a, vec4_t b);
vec4_t vec4_add(vec4_t a, vec4_t b);
vec4_t vec4_fadd(vec4_t a, float x, float y, float z, float w);
vec4_t vec4_norm(vec4_t a);
vec4_t vec4_mult(vec4_t v, float f);
float  vec4_dot(vec4_t a, vec4_t b);
vec4_t vec4_apply_proj(vec4_t a, mat4_t m);
vec4_t vec4_apply_mat4(vec4_t a, mat4_t m);
vec4_t vec4_apply_axis_angle(vec4_t a, vec4_t axis, float angle);
vec4_t vec4_apply_quat(vec4_t a, quat_t q);
bool   vec4_equals(vec4_t a, vec4_t b);
bool   vec4_almost_equals(vec4_t a, vec4_t b, float prec);
float  vec4_len(vec4_t a);
vec4_t vec4_sub(vec4_t a, vec4_t b);
float  vec4_dist(vec4_t v1, vec4_t v2);
float  vec4_fdist(float v1x, float v1y, float v1z, float v2x, float v2y, float v2z);
vec4_t vec4_reflect(vec4_t a, vec4_t n);
vec4_t vec4_clamp(vec4_t a, float min, float max);
vec4_t vec4_x_axis();
vec4_t vec4_y_axis();
vec4_t vec4_z_axis();
vec4_t vec4_nan();
bool   vec4_isnan(vec4_t v);

quat_t quat_from_axis_angle(vec4_t axis, float angle);
quat_t quat_from_mat(mat4_t m);
quat_t quat_from_rot_mat(mat4_t m);
quat_t quat_mult(quat_t a, quat_t b);
quat_t quat_norm(quat_t q);
vec4_t quat_get_euler(quat_t q);
quat_t quat_from_euler(float x, float y, float z);
float  quat_dot(quat_t a, quat_t b);
quat_t quat_from_to(vec4_t v0, vec4_t v1);
quat_t quat_inv(quat_t q);
quat_t quat_slerp(quat_t a, quat_t b, float t);

mat3_t mat3_identity();
mat3_t mat3_translation(float x, float y);
mat3_t mat3_rotation(float alpha);
mat3_t mat3_scale(mat3_t m, vec4_t v);
mat3_t mat3_set_from4(mat4_t m4);
mat3_t mat3_multmat(mat3_t a, mat3_t b);
mat3_t mat3_transpose(mat3_t m);
mat3_t mat3_nan();
bool   mat3_isnan(mat3_t m);

typedef struct mat4_decomposed {
	vec4_t loc;
	quat_t rot;
	vec4_t scl;
} mat4_decomposed_t;

mat4_t             mat4_identity();
mat4_t             mat4_from_f32_array(f32_array_t *a, int offset);
mat4_t             mat4_persp(float fov_y, float aspect, float zn, float zf);
mat4_t             mat4_ortho(float left, float right, float bottom, float top, float znear, float zfar);
mat4_t             mat4_rot_z(float alpha);
mat4_t             mat4_compose(vec4_t loc, quat_t rot, vec4_t scl);
mat4_t             mat4_tween(mat4_t a, mat4_t b, float t);
mat4_decomposed_t *mat4_decompose(mat4_t m);
mat4_t             mat4_set_loc(mat4_t m, vec4_t v);
mat4_t             mat4_from_quat(quat_t q);
mat4_t             mat4_translate(mat4_t m, float x, float y, float z);
mat4_t             mat4_scale(mat4_t m, vec4_t v);
mat4_t             mat4_mult_mat3x4(mat4_t a, mat4_t b);
mat4_t             mat4_mult_mat(mat4_t a, mat4_t b);
mat4_t             mat4_inv(mat4_t a);
mat4_t             mat4_transpose(mat4_t m);
mat4_t             mat4_transpose3(mat4_t m);
vec4_t             mat4_get_loc(mat4_t m);
vec4_t             mat4_get_scale(mat4_t m);
mat4_t             mat4_mult(mat4_t m, float s);
mat4_t             mat4_to_rot(mat4_t m);
vec4_t             mat4_right(mat4_t m);
vec4_t             mat4_look(mat4_t m);
vec4_t             mat4_up(mat4_t m);
f32_array_t       *mat4_to_f32_array(mat4_t m);
float              mat4_cofactor(float m0, float m1, float m2, float m3, float m4, float m5, float m6, float m7, float m8);
float              mat4_determinant(mat4_t m);
mat4_t             mat4_nan();
bool               mat4_isnan(mat4_t m);
