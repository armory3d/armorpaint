
// Exposes the engine and app api to minic scripts

#include "engine.h"
#include "iron_alloc.h"
#include "iron_armpack.h"
#include "iron_array.h"
#include "iron_draw.h"
#include "iron_file.h"
#include "iron_input.h"
#include "iron_json.h"
#include "iron_map.h"
#include "iron_obj.h"
#include "iron_shape.h"
#include "iron_string.h"
#include "iron_system.h"
#include "iron_ui.h"
#include "minic.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "functions.h"

gpu_texture_t *gpu_create_render_target(i32 width, i32 height, i32 format);
void           iron_delay_idle_sleep();

static const char *minic_read_str(minic_val_t v) {
	if (v.type == MINIC_T_PTR && v.p != NULL) {
		return (const char *)v.p;
	}
	return "";
}

static int minic_vformat(const char *fmt, minic_val_t *args, int argc, char *buf, int bufsize) {
	int pos = 0;
	int arg = 0;
	while (*fmt != '\0') {
		if (*fmt != '%') {
			if (buf && pos < bufsize - 1)
				buf[pos] = *fmt;
			pos++;
			fmt++;
			continue;
		}
		fmt++;
		char spec = *fmt++;
		if (spec == '\0') {
			break;
		}
		char tmp[64];
		int  n = 0;
		if (spec == 'd' || spec == 'i') {
			int iv = arg < argc ? (int)minic_val_to_d(args[arg++]) : 0;
			n      = snprintf(tmp, sizeof(tmp), "%d", iv);
		}
		else if (spec == 'u') {
			unsigned uv = arg < argc ? (unsigned)(int)minic_val_to_d(args[arg++]) : 0u;
			n           = snprintf(tmp, sizeof(tmp), "%u", uv);
		}
		else if (spec == 'f' || spec == 'g' || spec == 'e') {
			double     dv       = arg < argc ? minic_val_to_d(args[arg++]) : 0.0;
			const char fspec[3] = {'%', spec, '\0'};
			n                   = snprintf(tmp, sizeof(tmp), fspec, dv);
		}
		else if (spec == 's') {
			const char *sv   = arg < argc ? minic_read_str(args[arg++]) : "";
			int         slen = (int)strlen(sv);
			if (buf) {
				int copy = slen < bufsize - 1 - pos ? slen : bufsize - 1 - pos;
				if (copy > 0)
					memcpy(buf + pos, sv, copy);
			}
			pos += slen;
			continue;
		}
		else if (spec == 'p') {
			void *pv = (arg < argc && args[arg].type == MINIC_T_PTR) ? args[arg++].p : (void *)(uintptr_t)(uint64_t)minic_val_to_d(args[arg++]);
			n        = snprintf(tmp, sizeof(tmp), "%p", pv);
		}
		else if (spec == 'c') {
			if (buf && pos < bufsize - 1)
				buf[pos] = (char)(arg < argc ? (int)minic_val_to_d(args[arg++]) : 0);
			pos++;
			continue;
		}
		else {
			if (buf && pos < bufsize - 1)
				buf[pos] = '%';
			pos++;
			if (spec != '%') {
				if (buf && pos < bufsize - 1)
					buf[pos] = spec;
				pos++;
			}
			continue;
		}
		if (n > 0) {
			if (buf) {
				int copy = n < bufsize - 1 - pos ? n : bufsize - 1 - pos;
				if (copy > 0)
					memcpy(buf + pos, tmp, copy);
			}
			pos += n;
		}
	}
	if (buf && pos < bufsize)
		buf[pos] = '\0';
	return pos;
}

static minic_val_t minic_printf_native(minic_val_t *args, int argc) {
	if (argc < 1 || args[0].type != MINIC_T_PTR)
		return minic_val_int(0);
	const char *fmt = (const char *)args[0].p;
	int         len = minic_vformat(fmt, args + 1, argc - 1, NULL, 0);
	char       *buf = (char *)malloc(len + 1);
	minic_vformat(fmt, args + 1, argc - 1, buf, len + 1);
	console_log(buf);
	free(buf);
	return minic_val_int(len);
}

static minic_val_t minic_string_native(minic_val_t *args, int argc) {
	if (argc < 1 || args[0].type != MINIC_T_PTR)
		return minic_val_ptr(NULL);
	const char *fmt = (const char *)args[0].p;
	int         len = minic_vformat(fmt, args + 1, argc - 1, NULL, 0);
	char       *buf = string_alloc(len + 1);
	minic_vformat(fmt, args + 1, argc - 1, buf, len + 1);
	return minic_val_ptr(buf);
}

// iron_math wrappers: scripts store math types as arrays of boxed minic_val_t floats,
// the C functions take and return them by value
static void minic_box(minic_val_t *dst, const float *src, int n) {
	for (int i = 0; i < n; ++i) {
		dst[i] = minic_val_float(src[i]);
	}
}

static void minic_unbox(float *dst, const minic_val_t *src, int n) {
	for (int i = 0; i < n; ++i) {
		dst[i] = src[i].f;
	}
}

// Normalize math-type representation into raw floats: arena pointers are script values
// stored as boxed minic_val_t, anything else is a native C struct field
static void minic_read_floats(float *dst, void *p, int n) {
	if (p == NULL) {
		for (int i = 0; i < n; ++i) {
			dst[i] = 0.0f;
		}
	}
	else if (minic_in_arena(p)) {
		minic_unbox(dst, (minic_val_t *)p, n);
	}
	else {
		memcpy(dst, p, n * sizeof(float));
	}
}

// clang-format off
static vec2_t minic_get_vec2(void *p) { vec2_t v; minic_read_floats(&v.x, p, 2); return v; }
static vec4_t minic_get_vec4(void *p) { vec4_t v; minic_read_floats(&v.x, p, 4); return v; }
static quat_t minic_get_quat(void *p) { quat_t q; minic_read_floats(&q.x, p, 4); return q; }
static mat3_t minic_get_mat3(void *p) { mat3_t m; minic_read_floats(m.m, p, 9); return m; }
static mat4_t minic_get_mat4(void *p) { mat4_t m; minic_read_floats(m.m, p, 16); return m; }
static void minic_set_vec2(minic_val_t *o, vec2_t v) { minic_box(o, &v.x, 2); }
static void minic_set_vec4(minic_val_t *o, vec4_t v) { minic_box(o, &v.x, 4); }
static void minic_set_quat(minic_val_t *o, quat_t q) { minic_box(o, &q.x, 4); }
static void minic_set_mat3(minic_val_t *o, mat3_t m) { minic_box(o, m.m, 9); }
static void minic_set_mat4(minic_val_t *o, mat4_t m) { minic_box(o, m.m, 16); }
// clang-format on

// A call may pass too few arguments or the wrong kind
static void *minic_arg_ptr(minic_val_t *a, int c, int i) {
	return (i < c && a[i].type == MINIC_T_PTR) ? a[i].p : NULL;
}

static float minic_arg_float(minic_val_t *a, int c, int i) {
	return i < c ? (float)minic_val_to_d(a[i]) : 0.0f;
}

// Argument accessors for the wrapper table
#define V2(i) minic_get_vec2(minic_arg_ptr(_a, _c, i))
#define V4(i) minic_get_vec4(minic_arg_ptr(_a, _c, i))
#define QT(i) minic_get_quat(minic_arg_ptr(_a, _c, i))
#define M3(i) minic_get_mat3(minic_arg_ptr(_a, _c, i))
#define M4(i) minic_get_mat4(minic_arg_ptr(_a, _c, i))
#define AF(i) minic_arg_float(_a, _c, i)
#define AP(i) minic_arg_ptr(_a, _c, i)

// One X(return-kind, name, call) line per math function; expanded twice:
// once to define the mn_* wrappers, once to register them
#define MINIC_MATH_API                                                         \
	X(F, vec2_len, vec2_len(V2(0)))                                            \
	X(V2, vec2_set_len, vec2_set_len(V2(0), AF(1)))                            \
	X(V2, vec2_mult, vec2_mult(V2(0), AF(1)))                                  \
	X(V2, vec2_add, vec2_add(V2(0), V2(1)))                                    \
	X(V2, vec2_sub, vec2_sub(V2(0), V2(1)))                                    \
	X(F, vec2_cross, vec2_cross(V2(0), V2(1)))                                 \
	X(V2, vec2_norm, vec2_norm(V2(0)))                                         \
	X(F, vec2_dot, vec2_dot(V2(0), V2(1)))                                     \
	X(V2, vec2_nan, vec2_nan())                                                \
	X(I, vec2_isnan, vec2_isnan(V2(0)))                                        \
	X(V4, vec4_cross, vec4_cross(V4(0), V4(1)))                                \
	X(V4, vec4_add, vec4_add(V4(0), V4(1)))                                    \
	X(V4, vec4_fadd, vec4_fadd(V4(0), AF(1), AF(2), AF(3), AF(4)))             \
	X(V4, vec4_norm, vec4_norm(V4(0)))                                         \
	X(V4, vec4_mult, vec4_mult(V4(0), AF(1)))                                  \
	X(F, vec4_dot, vec4_dot(V4(0), V4(1)))                                     \
	X(V4, vec4_apply_proj, vec4_apply_proj(V4(0), M4(1)))                      \
	X(V4, vec4_apply_mat4, vec4_apply_mat4(V4(0), M4(1)))                      \
	X(V4, vec4_apply_axis_angle, vec4_apply_axis_angle(V4(0), V4(1), AF(2)))   \
	X(V4, vec4_apply_quat, vec4_apply_quat(V4(0), QT(1)))                      \
	X(I, vec4_equals, vec4_equals(V4(0), V4(1)))                               \
	X(I, vec4_almost_equals, vec4_almost_equals(V4(0), V4(1), AF(2)))          \
	X(F, vec4_len, vec4_len(V4(0)))                                            \
	X(V4, vec4_sub, vec4_sub(V4(0), V4(1)))                                    \
	X(F, vec4_dist, vec4_dist(V4(0), V4(1)))                                   \
	X(V4, vec4_reflect, vec4_reflect(V4(0), V4(1)))                            \
	X(V4, vec4_clamp, vec4_clamp(V4(0), AF(1), AF(2)))                         \
	X(V4, vec4_x_axis, vec4_x_axis())                                          \
	X(V4, vec4_y_axis, vec4_y_axis())                                          \
	X(V4, vec4_z_axis, vec4_z_axis())                                          \
	X(V4, vec4_nan, vec4_nan())                                                \
	X(I, vec4_isnan, vec4_isnan(V4(0)))                                        \
	X(Q, quat_from_axis_angle, quat_from_axis_angle(V4(0), AF(1)))             \
	X(Q, quat_from_mat, quat_from_mat(M4(0)))                                  \
	X(Q, quat_from_rot_mat, quat_from_rot_mat(M4(0)))                          \
	X(Q, quat_mult, quat_mult(QT(0), QT(1)))                                   \
	X(Q, quat_norm, quat_norm(QT(0)))                                          \
	X(V4, quat_get_euler, quat_get_euler(QT(0)))                               \
	X(Q, quat_from_euler, quat_from_euler(AF(0), AF(1), AF(2)))                \
	X(F, quat_dot, quat_dot(QT(0), QT(1)))                                     \
	X(Q, quat_from_to, quat_from_to(V4(0), V4(1)))                             \
	X(Q, quat_inv, quat_inv(QT(0)))                                            \
	X(M3, mat3_identity, mat3_identity())                                      \
	X(M3, mat3_translation, mat3_translation(AF(0), AF(1)))                    \
	X(M3, mat3_rotation, mat3_rotation(AF(0)))                                 \
	X(M3, mat3_scale, mat3_scale(M3(0), V4(1)))                                \
	X(M3, mat3_set_from4, mat3_set_from4(M4(0)))                               \
	X(M3, mat3_multmat, mat3_multmat(M3(0), M3(1)))                            \
	X(M3, mat3_transpose, mat3_transpose(M3(0)))                               \
	X(M3, mat3_nan, mat3_nan())                                                \
	X(I, mat3_isnan, mat3_isnan(M3(0)))                                        \
	X(M4, mat4_identity, mat4_identity())                                      \
	X(M4, mat4_persp, mat4_persp(AF(0), AF(1), AF(2), AF(3)))                  \
	X(M4, mat4_ortho, mat4_ortho(AF(0), AF(1), AF(2), AF(3), AF(4), AF(5)))    \
	X(M4, mat4_rot_z, mat4_rot_z(AF(0)))                                       \
	X(M4, mat4_compose, mat4_compose(V4(0), QT(1), V4(2)))                     \
	X(M4, mat4_set_loc, mat4_set_loc(M4(0), V4(1)))                            \
	X(M4, mat4_from_quat, mat4_from_quat(QT(0)))                               \
	X(M4, mat4_translate, mat4_translate(M4(0), AF(1), AF(2), AF(3)))          \
	X(M4, mat4_scale, mat4_scale(M4(0), V4(1)))                                \
	X(M4, mat4_mult_mat3x4, mat4_mult_mat3x4(M4(0), M4(1)))                    \
	X(M4, mat4_mult_mat, mat4_mult_mat(M4(0), M4(1)))                          \
	X(M4, mat4_inv, mat4_inv(M4(0)))                                           \
	X(M4, mat4_transpose, mat4_transpose(M4(0)))                               \
	X(M4, mat4_transpose3, mat4_transpose3(M4(0)))                             \
	X(V4, mat4_get_loc, mat4_get_loc(M4(0)))                                   \
	X(V4, mat4_get_scale, mat4_get_scale(M4(0)))                               \
	X(M4, mat4_mult, mat4_mult(M4(0), AF(1)))                                  \
	X(M4, mat4_to_rot, mat4_to_rot(M4(0)))                                     \
	X(V4, mat4_right, mat4_right(M4(0)))                                       \
	X(V4, mat4_look, mat4_look(M4(0)))                                         \
	X(V4, mat4_up, mat4_up(M4(0)))                                             \
	X(P, mat4_to_f32_array, mat4_to_f32_array(M4(0)))                          \
	X(F, mat4_determinant, mat4_determinant(M4(0)))                            \
	X(M4, mat4_nan, mat4_nan())                                                \
	X(I, mat4_isnan, mat4_isnan(M4(0)))                                        \
	X(VOID, transform_set_matrix, transform_set_matrix(AP(0), M4(1)))          \
	X(VOID, transform_rotate, transform_rotate(AP(0), V4(1), AF(2)))           \
	X(VOID, transform_move, transform_move(AP(0), V4(1), AF(2)))               \
	X(V4, transform_look, transform_look(AP(0)))                               \
	X(V4, transform_right, transform_right(AP(0)))                             \
	X(V4, transform_up, transform_up(AP(0)))                                   \
	X(V4, raycast_aabb_mouse, raycast_aabb_mouse((object_t *)AP(0)))           \
	X(I, point_in_aabb, point_in_aabb((object_t *)AP(0), V4(1)))               \
	X(VOID, script_tween_to, script_tween_to((object_t *)AP(0), V4(1), AF(2))) \
	X(VOID, line_draw_render, line_draw_render(M4(0)))                         \
	X(VOID, line_draw_bounds, line_draw_bounds(M4(0), V4(1)))                  \
	X(VOID, shape_draw_sphere, shape_draw_sphere(M4(0)))                       \
	X(VOID, draw_set_transform, draw_set_transform(M3(0)))

// Wrapper generators per return kind
#define MN_HEAD(n)                                       \
	static minic_val_t mn_##n(minic_val_t *_a, int _c) { \
		(void)_a;                                        \
		(void)_c;
#define MN_F(n, e)             \
	MN_HEAD(n)                 \
	return minic_val_float(e); \
	}
#define MN_I(n, e)           \
	MN_HEAD(n)               \
	return minic_val_int(e); \
	}
#define MN_P(n, e)           \
	MN_HEAD(n)               \
	return minic_val_ptr(e); \
	}
#define MN_VOID(n, e)        \
	MN_HEAD(n)               \
	e;                       \
	return minic_val_void(); \
	}
#define MN_BOX(n, e, setter, count)                                                 \
	MN_HEAD(n)                                                                      \
	minic_val_t *_o = (minic_val_t *)minic_alloc(count * (int)sizeof(minic_val_t)); \
	setter(_o, e);                                                                  \
	return minic_val_ptr(_o);                                                       \
	}
#define MN_V2(n, e) MN_BOX(n, e, minic_set_vec2, 2)
#define MN_V4(n, e) MN_BOX(n, e, minic_set_vec4, 4)
#define MN_Q(n, e)  MN_BOX(n, e, minic_set_quat, 4)
#define MN_M3(n, e) MN_BOX(n, e, minic_set_mat3, 9)
#define MN_M4(n, e) MN_BOX(n, e, minic_set_mat4, 16)

#define X(kind, n, e) MN_##kind(n, e)
MINIC_MATH_API
#undef X

// All array types share the buffer/length/capacity layout
static void minic_register_array_struct(const char *name, int size, minic_type_t buffer_deref) {
	minic_struct_begin(name, size);
	minic_struct_field("buffer", (int)offsetof(u8_array_t, buffer), MINIC_T_PTR, buffer_deref, NULL);
	minic_struct_field("length", (int)offsetof(u8_array_t, length), MINIC_T_INT, MINIC_T_INT, NULL);
	minic_struct_field("capacity", (int)offsetof(u8_array_t, capacity), MINIC_T_INT, MINIC_T_INT, NULL);
}

#define MINIC_API_MAX_SIGS 1024

static const char *minic_api_sig_names[MINIC_API_MAX_SIGS];
static const char *minic_api_sig_hints[MINIC_API_MAX_SIGS];
static int         minic_api_sig_count = 0;

static void minic_api_register(const char *name, const char *sig, minic_native_fn_t fn) {
	char stripped[MINIC_MAX_SIG];
	int  n    = 0;
	bool skip = false;
	for (const char *p = sig; *p != '\0' && n < MINIC_MAX_SIG - 1; ++p) {
		if (*p == ' ' || *p == ':') {
			skip = true;
		}
		else if (*p == '(' || *p == ',' || *p == ')') {
			skip = false;
		}
		if (!skip) {
			stripped[n++] = *p;
		}
	}
	stripped[n] = '\0';
	minic_register(name, stripped, fn);

	if (minic_api_sig_count < MINIC_API_MAX_SIGS) {
		minic_api_sig_names[minic_api_sig_count] = name;
		minic_api_sig_hints[minic_api_sig_count] = sig;
		minic_api_sig_count++;
	}
}

static const char *minic_api_sig_hint(const char *name) {
	for (int i = 0; i < minic_api_sig_count; ++i) {
		if (strcmp(minic_api_sig_names[i], name) == 0) {
			return minic_api_sig_hints[i];
		}
	}
	return NULL;
}

#define MINIC_ARG_i(k) minic_arg_i(a, n, k)
#define MINIC_ARG_f(k) minic_arg_f(a, n, k)
#define MINIC_ARG_p(k) minic_arg_p(a, n, k)
#define MINIC_ARG_b(k) minic_arg_i(a, n, k)
#define MINIC_ARG_c(k) minic_arg_i(a, n, k)

#define MINIC_RET_i(call) return minic_val_int((int)(call))
#define MINIC_RET_f(call) return minic_val_float(call)
#define MINIC_RET_p(call) return minic_val_ptr(call)
#define MINIC_RET_b(call) return minic_val_int((call) ? 1 : 0)
#define MINIC_RET_c(call) return minic_val_int((int)(call))
#define MINIC_RET_v(call)        \
	do {                         \
		call;                    \
		return minic_val_int(0); \
	} while (0)

// Pass 1: one thunk per exported function
#define X0(name, sig, r)                                  \
	static minic_val_t mw_##name(minic_val_t *a, int n) { \
		(void)a;                                          \
		(void)n;                                          \
		MINIC_RET_##r(name());                            \
	}
#define X1(name, sig, r, t0)                              \
	static minic_val_t mw_##name(minic_val_t *a, int n) { \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0)));           \
	}
#define X2(name, sig, r, t0, t1)                                   \
	static minic_val_t mw_##name(minic_val_t *a, int n) {          \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1))); \
	}
#define X3(name, sig, r, t0, t1, t2)                                                  \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                             \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2))); \
	}
#define X4(name, sig, r, t0, t1, t2, t3)                                                                 \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                                                \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2), MINIC_ARG_##t3(3))); \
	}
#define X5(name, sig, r, t0, t1, t2, t3, t4)                                                                                \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                                                                   \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2), MINIC_ARG_##t3(3), MINIC_ARG_##t4(4))); \
	}
#define X6(name, sig, r, t0, t1, t2, t3, t4, t5)                                                                                               \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                                                                                      \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2), MINIC_ARG_##t3(3), MINIC_ARG_##t4(4), MINIC_ARG_##t5(5))); \
	}
#define X7(name, sig, r, t0, t1, t2, t3, t4, t5, t6)                                                                                                    \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                                                                                               \
		MINIC_RET_##r(                                                                                                                                  \
		    name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2), MINIC_ARG_##t3(3), MINIC_ARG_##t4(4), MINIC_ARG_##t5(5), MINIC_ARG_##t6(6))); \
	}
#define X8(name, sig, r, t0, t1, t2, t3, t4, t5, t6, t7)                                                                                     \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                                                                                    \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2), MINIC_ARG_##t3(3), MINIC_ARG_##t4(4), MINIC_ARG_##t5(5), \
		                   MINIC_ARG_##t6(6), MINIC_ARG_##t7(7)));                                                                           \
	}
#define X9(name, sig, r, t0, t1, t2, t3, t4, t5, t6, t7, t8)                                                                                 \
	static minic_val_t mw_##name(minic_val_t *a, int n) {                                                                                    \
		MINIC_RET_##r(name(MINIC_ARG_##t0(0), MINIC_ARG_##t1(1), MINIC_ARG_##t2(2), MINIC_ARG_##t3(3), MINIC_ARG_##t4(4), MINIC_ARG_##t5(5), \
		                   MINIC_ARG_##t6(6), MINIC_ARG_##t7(7), MINIC_ARG_##t8(8)));                                                        \
	}
#include "minic_api_list.h"
#undef X0
#undef X1
#undef X2
#undef X3
#undef X4
#undef X5
#undef X6
#undef X7
#undef X8
#undef X9

// Pass 2: the registration table, in declaration order
typedef struct {
	const char       *name;
	const char       *sig;
	minic_native_fn_t fn;
} minic_api_entry_t;

#define X0(name, sig, r)                                     {#name, sig, mw_##name},
#define X1(name, sig, r, t0)                                 {#name, sig, mw_##name},
#define X2(name, sig, r, t0, t1)                             {#name, sig, mw_##name},
#define X3(name, sig, r, t0, t1, t2)                         {#name, sig, mw_##name},
#define X4(name, sig, r, t0, t1, t2, t3)                     {#name, sig, mw_##name},
#define X5(name, sig, r, t0, t1, t2, t3, t4)                 {#name, sig, mw_##name},
#define X6(name, sig, r, t0, t1, t2, t3, t4, t5)             {#name, sig, mw_##name},
#define X7(name, sig, r, t0, t1, t2, t3, t4, t5, t6)         {#name, sig, mw_##name},
#define X8(name, sig, r, t0, t1, t2, t3, t4, t5, t6, t7)     {#name, sig, mw_##name},
#define X9(name, sig, r, t0, t1, t2, t3, t4, t5, t6, t7, t8) {#name, sig, mw_##name},
static const minic_api_entry_t minic_api_entries[] = {
#include "minic_api_list.h"
};
#undef X0
#undef X1
#undef X2
#undef X3
#undef X4
#undef X5
#undef X6
#undef X7
#undef X8
#undef X9

void minic_register_builtins() {
	minic_api_sig_count = 0;

	minic_register_native("printf", minic_printf_native);
	minic_register_native("string", minic_string_native);

	// iron_array
	minic_register_array_struct("i8_array_t", (int)sizeof(i8_array_t), MINIC_T_CHAR);
	minic_register_array_struct("u8_array_t", (int)sizeof(u8_array_t), MINIC_T_CHAR);
	minic_register_array_struct("i16_array_t", (int)sizeof(i16_array_t), MINIC_T_INT);
	minic_register_array_struct("u16_array_t", (int)sizeof(u16_array_t), MINIC_T_INT);
	minic_register_array_struct("i32_array_t", (int)sizeof(i32_array_t), MINIC_T_INT);
	minic_register_array_struct("u32_array_t", (int)sizeof(u32_array_t), MINIC_T_INT);
	minic_register_array_struct("f32_array_t", (int)sizeof(f32_array_t), MINIC_T_FLOAT);
	minic_register_array_struct("any_array_t", (int)sizeof(any_array_t), MINIC_T_PTR);
	minic_register_array_struct("string_array_t", (int)sizeof(string_array_t), MINIC_T_PTR);
	minic_register_array_struct("buffer_t", (int)sizeof(buffer_t), MINIC_T_CHAR);

	// iron_math
	MINIC_STRUCT(vec2_t);
	MINIC_F(x);
	MINIC_F(y);
	MINIC_END();

	MINIC_STRUCT(vec3_t);
	MINIC_F(x);
	MINIC_F(y);
	MINIC_F(z);
	MINIC_END();

	MINIC_STRUCT(vec4_t);
	MINIC_F(x);
	MINIC_F(y);
	MINIC_F(z);
	MINIC_F(w);
	MINIC_END();

	MINIC_STRUCT(quat_t);
	MINIC_F(x);
	MINIC_F(y);
	MINIC_F(z);
	MINIC_F(w);
	MINIC_END();

	// Script-layout matrices (boxed fields)
	static const char *mat3_fields[] = {"m00", "m01", "m02", "m10", "m11", "m12", "m20", "m21", "m22"};
	static const char *mat4_fields[] = {"m00", "m01", "m02", "m03", "m10", "m11", "m12", "m13", "m20", "m21", "m22", "m23", "m30", "m31", "m32", "m33"};
	minic_register_struct("mat3_t", mat3_fields, 9);
	minic_register_struct("mat4_t", mat4_fields, 16);

	// iron_ui
	MINIC_ENUM("ui_layout_t", "UI_LAYOUT_VERTICAL", "UI_LAYOUT_HORIZONTAL");
	MINIC_ENUM("ui_align_t", "UI_ALIGN_LEFT", "UI_ALIGN_CENTER", "UI_ALIGN_RIGHT");
	MINIC_ENUM("ui_state_t", "UI_STATE_IDLE", "UI_STATE_STARTED", "UI_STATE_DOWN", "UI_STATE_RELEASED", "UI_STATE_HOVERED");

	MINIC_ENUM("gpu_texture_format_t", "GPU_TEXTURE_FORMAT_RGBA32", "GPU_TEXTURE_FORMAT_RGBA64", "GPU_TEXTURE_FORMAT_RGBA128", "GPU_TEXTURE_FORMAT_R8",
	           "GPU_TEXTURE_FORMAT_R16", "GPU_TEXTURE_FORMAT_R32", "GPU_TEXTURE_FORMAT_D32", "GPU_TEXTURE_FORMAT_RGBA32_BC7");
	MINIC_ENUM("physics_shape_t", "PHYSICS_SHAPE_BOX", "PHYSICS_SHAPE_SPHERE", "PHYSICS_SHAPE_TERRAIN", "PHYSICS_SHAPE_MESH");
	MINIC_ENUM("tool_type_t", "TOOL_TYPE_BRUSH", "TOOL_TYPE_ERASER", "TOOL_TYPE_FILL", "TOOL_TYPE_DECAL", "TOOL_TYPE_TEXT", "TOOL_TYPE_CLONE", "TOOL_TYPE_BLUR",
	           "TOOL_TYPE_PARTICLE", "TOOL_TYPE_COLORID", "TOOL_TYPE_PICKER", "TOOL_TYPE_MATERIAL", "TOOL_TYPE_CURSOR", "TOOL_TYPE_SELECT", "TOOL_TYPE_BAKE");

	MINIC_STRUCT(ui_handle_t);
	MINIC_I(i);
	MINIC_F(f);
	MINIC_I(b);
	MINIC_I(layout);
	MINIC_F(scroll_offset);
	MINIC_I(color);
	MINIC_I(redraws);
	MINIC_S(text);
	MINIC_I(scroll_enabled);
	MINIC_I(drag_enabled);
	MINIC_I(changed);
	MINIC_I(init);
	MINIC_O(children, any_array_t);
	MINIC_END();

	MINIC_STRUCT(ui_node_socket_t);
	MINIC_I(id);
	MINIC_I(node_id);
	MINIC_S(name);
	MINIC_S(type);
	MINIC_I(color);
	MINIC_O(default_value, f32_array_t);
	MINIC_F(min);
	MINIC_F(max);
	MINIC_F(precision);
	MINIC_I(display);
	MINIC_END();

	MINIC_STRUCT(ui_node_button_t);
	MINIC_S(name);
	MINIC_S(type);
	MINIC_I(output);
	MINIC_O(default_value, f32_array_t);
	MINIC_O(data, u8_array_t);
	MINIC_F(min);
	MINIC_F(max);
	MINIC_F(precision);
	MINIC_F(height);
	MINIC_END();

	MINIC_STRUCT(ui_node_link_t);
	MINIC_I(id);
	MINIC_I(from_id);
	MINIC_I(from_socket);
	MINIC_I(to_id);
	MINIC_I(to_socket);
	MINIC_END();

	MINIC_STRUCT(ui_node_t);
	MINIC_I(id);
	MINIC_S(name);
	MINIC_S(type);
	MINIC_F(x);
	MINIC_F(y);
	MINIC_I(color);
	MINIC_O(inputs, any_array_t);
	MINIC_O(outputs, any_array_t);
	MINIC_O(buttons, any_array_t);
	MINIC_F(width);
	MINIC_I(flags);
	MINIC_END();

	MINIC_STRUCT(ui_node_canvas_t);
	MINIC_S(name);
	MINIC_O(nodes, any_array_t);
	MINIC_O(links, any_array_t);
	MINIC_END();

	MINIC_STRUCT(slot_material_t);
	MINIC_O(canvas, ui_node_canvas_t);
	MINIC_I(id);
	MINIC_I(paint_base);
	MINIC_I(paint_opac);
	MINIC_I(paint_occ);
	MINIC_I(paint_rough);
	MINIC_I(paint_met);
	MINIC_I(paint_nor);
	MINIC_I(paint_height);
	MINIC_I(paint_emis);
	MINIC_I(paint_subs);
	MINIC_END();

	// engine.h
	MINIC_STRUCT(obj_t);
	MINIC_S(name);
	MINIC_S(type);
	MINIC_S(data_ref);
	MINIC_O(transform, f32_array_t);
	MINIC_O(dimensions, f32_array_t);
	MINIC_I(visible);
	MINIC_I(spawn);
	MINIC_P(anim);
	MINIC_S(material_ref);
	MINIC_O(children, obj_t_array_t);
	MINIC_P(_);
	MINIC_END();

	MINIC_STRUCT(vertex_array_t);
	MINIC_S(attrib);
	MINIC_S(data);
	MINIC_O(values, i16_array_t);
	MINIC_END();

	MINIC_STRUCT(mesh_data_t);
	MINIC_S(name);
	MINIC_F(scale_pos);
	MINIC_F(scale_tex);
	MINIC_O(vertex_arrays, vertex_array_t_array_t);
	MINIC_O(index_array, u32_array_t);
	MINIC_P(_);
	MINIC_END();

	MINIC_STRUCT(camera_data_t);
	MINIC_S(name);
	MINIC_F(near_plane);
	MINIC_F(far_plane);
	MINIC_F(fov);
	MINIC_F(aspect);
	MINIC_I(frustum_culling);
	MINIC_O(ortho, f32_array_t);
	MINIC_END();

	MINIC_STRUCT(world_data_t);
	MINIC_S(name);
	MINIC_I(color);
	MINIC_F(strength);
	MINIC_S(irradiance);
	MINIC_S(radiance);
	MINIC_I(radiance_mipmaps);
	MINIC_S(envmap);
	MINIC_P(_);
	MINIC_END();

	MINIC_STRUCT(vertex_element_t);
	MINIC_S(name);
	MINIC_S(data);
	MINIC_END();

	MINIC_STRUCT(shader_const_t);
	MINIC_S(name);
	MINIC_S(type);
	MINIC_S(link);
	MINIC_END();

	MINIC_STRUCT(tex_unit_t);
	MINIC_S(name);
	MINIC_S(link);
	MINIC_END();

	MINIC_STRUCT(shader_context_t);
	MINIC_S(name);
	MINIC_I(depth_write);
	MINIC_S(compare_mode);
	MINIC_S(cull_mode);
	MINIC_S(vertex_shader);
	MINIC_S(fragment_shader);
	MINIC_I(shader_from_source);
	MINIC_S(blend_source);
	MINIC_S(blend_destination);
	MINIC_S(alpha_blend_source);
	MINIC_S(alpha_blend_destination);
	MINIC_O(color_attachments, string_array_t);
	MINIC_S(depth_attachment);
	MINIC_O(vertex_elements, vertex_element_t_array_t);
	MINIC_O(constants, shader_const_t_array_t);
	MINIC_O(texture_units, tex_unit_t_array_t);
	MINIC_O(bind_textures, bind_tex_t_array_t);
	MINIC_END();

	MINIC_STRUCT(bind_tex_t);
	MINIC_S(name);
	MINIC_S(file);
	MINIC_END();

	MINIC_STRUCT(shader_data_t);
	MINIC_S(name);
	MINIC_O(contexts, any_array_t);
	MINIC_P(_);
	MINIC_END();

	MINIC_STRUCT(render_target_t);
	MINIC_S(name);
	MINIC_I(width);
	MINIC_I(height);
	MINIC_S(format);
	MINIC_F(scale);
	MINIC_P(_image);
	MINIC_END();

	MINIC_STRUCT(object_t);
	MINIC_I(uid);
	MINIC_F(urandom);
	MINIC_O(raw, obj_t);
	MINIC_S(name);
	MINIC_O(transform, transform_t);
	MINIC_P(parent);
	MINIC_O(children, any_array_t);
	MINIC_B(visible);
	MINIC_B(culled);
	MINIC_B(is_empty);
	MINIC_P(ext);
	MINIC_S(ext_type);
	MINIC_END();

	MINIC_STRUCT(mesh_object_t);
	MINIC_O(base, object_t);
	MINIC_O(data, mesh_data_t);
	MINIC_O(material, shader_data_t);
	MINIC_F(camera_dist);
	MINIC_I(frustum_culling);
	MINIC_S(skip_context);
	MINIC_S(force_context);
	MINIC_END();

	MINIC_STRUCT(transform_t);
	MINIC_E(loc, vec4_t);
	MINIC_E(rot, quat_t);
	MINIC_E(scale, vec4_t);
	MINIC_F(scale_world);
	MINIC_I(dirty);
	MINIC_O(object, object_t);
	MINIC_F(radius);
	MINIC_END();

	MINIC_STRUCT(camera_object_t);
	MINIC_O(base, object_t);
	MINIC_O(data, camera_data_t);
	MINIC_I(frame);
	MINIC_O(frustum_planes, frustum_plane_array_t);
	MINIC_END();

	// types.h
	MINIC_STRUCT(config_t);
	MINIC_I(window_w);
	MINIC_I(window_h);
	MINIC_F(window_scale);
	MINIC_F(rp_supersample);
	MINIC_O(recent_projects, string_array_t);
	MINIC_O(plugins, string_array_t);
	MINIC_S(keymap);
	MINIC_S(theme);
	MINIC_I(undo_steps);
	MINIC_F(camera_fov);
	MINIC_I(layer_res);
	MINIC_I(brush_live);
	MINIC_I(node_previews);
	MINIC_I(material_live);
	MINIC_I(workspace);
	MINIC_I(workflow);
	MINIC_END();

	MINIC_STRUCT(context_t);
	MINIC_O(paint_object, mesh_object_t);
	MINIC_I(ddirty);
	MINIC_I(pdirty);
	MINIC_O(material, slot_material_t);
	MINIC_P(layer);
	MINIC_P(brush);
	MINIC_I(tool);
	MINIC_F(brush_radius);
	MINIC_F(brush_opacity);
	MINIC_F(brush_hardness);
	MINIC_F(brush_scale);
	MINIC_F(brush_angle);
	MINIC_I(brush_blending);
	MINIC_I(viewport_mode);
	MINIC_I(xray);
	MINIC_B(capturing_screenshot);
	MINIC_END();

	MINIC_STRUCT(project_t);
	MINIC_S(version);
	MINIC_O(assets, string_array_t);
	MINIC_I(is_bgra);
	MINIC_S(envmap);
	MINIC_F(envmap_strength);
	MINIC_F(envmap_angle);
	MINIC_F(camera_fov);
	MINIC_O(camera_world, f32_array_t);
	MINIC_O(camera_origin, f32_array_t);
	MINIC_P(swatches);
	MINIC_P(brush_nodes);
	MINIC_P(material_nodes);
	MINIC_O(font_assets, string_array_t);
	MINIC_P(layer_datas);
	MINIC_P(mesh_datas);
	MINIC_O(script_datas, string_array_t);
	MINIC_END();

	// iron_math wrappers
#define X(kind, n, e) minic_register_native(#n, mn_##n);
	MINIC_MATH_API
#undef X

	// iron_input globals
	minic_register_global("mouse_x", &mouse_x, MINIC_T_FLOAT);
	minic_register_global("mouse_y", &mouse_y, MINIC_T_FLOAT);

	// minic_api_list.h
	for (int i = 0; i < (int)(sizeof(minic_api_entries) / sizeof(minic_api_entries[0])); ++i) {
		minic_api_register(minic_api_entries[i].name, minic_api_entries[i].sig, minic_api_entries[i].fn);
	}
}

static const char *minic_api_type_name(minic_type_t t, minic_type_t deref, const char *struct_name) {
	if (t == MINIC_T_INT) {
		return "int";
	}
	if (t == MINIC_T_FLOAT) {
		return "float";
	}
	if (t == MINIC_T_BOOL) {
		return "bool";
	}
	if (t == MINIC_T_CHAR) {
		return "char";
	}
	if (t == MINIC_T_VOID) {
		return "void";
	}
	if (t == MINIC_T_EMBED) {
		return struct_name != NULL && struct_name[0] != '\0' ? struct_name : "void";
	}
	if (t == MINIC_T_PTR) {
		if (deref == MINIC_T_CHAR) {
			return "char *";
		}
		if (struct_name != NULL && struct_name[0] != '\0') {
			return NULL; // caller formats as "struct_name *"
		}
		return "void *";
	}
	return "int";
}

static const char *minic_api_sig_type(char c) {
	switch (c) {
	case 'f':
		return "float";
	case 'p':
		return "void *";
	case 'b':
		return "bool";
	case 'c':
		return "char";
	case 'v':
		return "void";
	default:
		return "int";
	}
}

static const char *minic_api_sig_read_type(const char **p, char *buf, int buf_size) {
	char c = **p;
	(*p)++;
	if (**p != ':') {
		return minic_api_sig_type(c);
	}
	(*p)++;
	const char *type = *p;
	while (**p != '\0' && **p != ' ' && **p != '(' && **p != ',' && **p != ')') {
		(*p)++;
	}
	snprintf(buf, buf_size, "%.*s *", (int)(*p - type), type);
	return buf;
}

static void minic_api_func_write(buffer_t *sb, const char *name, const char *sig) {
	if (sig[0] == '\0') {
		string_buffer_append(sb, string("void %s(...);\n", name));
		return;
	}
	const char *p = sig;
	char        ret_buf[MINIC_MAX_NAME + 4];
	const char *ret     = minic_api_sig_read_type(&p, ret_buf, sizeof(ret_buf));
	const char *ret_sep = ret[strlen(ret) - 1] == '*' ? "" : " ";
	string_buffer_append(sb, string("%s%s%s(", ret, ret_sep, name));
	if (*p == '(') {
		p++;
	}
	int arg = 0;
	while (*p != '\0' && *p != ')') {
		if (*p == ',') {
			p++;
			continue;
		}
		if (arg > 0) {
			string_buffer_append(sb, ", ");
		}
		char        type_buf[MINIC_MAX_NAME + 4];
		const char *type     = minic_api_sig_read_type(&p, type_buf, sizeof(type_buf));
		const char *arg_name = "";
		int         name_len = 0;
		if (*p == ' ') {
			p++;
			arg_name = p;
			while (*p != '\0' && *p != ',' && *p != ')') {
				p++;
			}
			name_len = (int)(p - arg_name);
		}
		// Pointer types already end with "*"
		const char *sep = name_len > 0 && type[strlen(type) - 1] != '*' ? " " : "";
		string_buffer_append(sb, string("%s%s%.*s", type, sep, name_len, arg_name));
		arg++;
	}
	if (arg == 0) {
		string_buffer_append(sb, "void");
	}
	string_buffer_append(sb, ");\n");
}

char *minic_api_header_generate(void) {
	minic_register_builtins();

	buffer_t sb;
	string_buffer_init(&sb);
	string_buffer_append(&sb, "// ArmorPaint script API\n\n");

	// Structs
	for (int i = 0; i < minic_struct_count; ++i) {
		minic_struct_t *s = &minic_structs[i];
		string_buffer_append(&sb, string("typedef struct %s {\n", s->name));
		for (int f = 0; f < s->field_count; ++f) {
			const char *stype = s->field_structs[f];
			const char *tn    = minic_api_type_name(s->types[f], s->deref_types[f], stype);
			if (tn == NULL) {
				string_buffer_append(&sb, string("    %s *%s;\n", stype, s->fields[f]));
			}
			else if (s->types[f] == MINIC_T_EMBED) {
				string_buffer_append(&sb, string("    %s %s;\n", tn, s->fields[f]));
			}
			else {
				string_buffer_append(&sb, string("    %s %s;\n", tn, s->fields[f]));
			}
		}
		string_buffer_append(&sb, string("} %s;\n\n", s->name));
	}

	// Enums
	int enum_count = minic_enum_const_count_get();
	if (enum_count > 0) {
		string_buffer_append(&sb, "// Enums\n");
		for (int i = 0; i < enum_count; ++i) {
			string_buffer_append(&sb, string("#define %s %d\n", minic_enum_const_name_at(i), minic_enum_const_value_at(i)));
		}
		string_buffer_append(&sb, "\n");
	}

	// Globals
	int global_count = minic_global_count_get();
	if (global_count > 0) {
		string_buffer_append(&sb, "// Globals\n");
		for (int i = 0; i < global_count; ++i) {
			minic_type_t t = minic_global_type_at(i);
			string_buffer_append(&sb, string("extern %s %s;\n", minic_api_type_name(t, t, NULL), minic_global_name_at(i)));
		}
		string_buffer_append(&sb, "\n");
	}

	// Functions
	string_buffer_append(&sb, "// Functions\n");
	int func_count = minic_ext_func_count_get();
	for (int i = 0; i < func_count; ++i) {
		const char *name = minic_ext_func_name_at(i);
		const char *sig  = minic_api_sig_hint(name);
		if (sig == NULL) {
			sig = minic_ext_func_sig_at(i);
		}
		minic_api_func_write(&sb, name, sig);
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}
