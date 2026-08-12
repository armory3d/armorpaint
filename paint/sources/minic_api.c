
// Exposes the engine and app api to minic scripts

#include "engine.h"
#include "iron_armpack.h"
#include "iron_array.h"
#include "iron_draw.h"
#include "iron_file.h"
#include "iron_gc.h"
#include "iron_input.h"
#include "iron_json.h"
#include "iron_map.h"
#include "iron_obj.h"
#include "iron_shape.h"
#include "iron_string.h"
#include "iron_sys.h"
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

static void minic_api_register(const char *name, const char *sig, minic_ext_fn_raw_t fn) {
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

#define R(name, sig) minic_api_register(#name, sig, (minic_ext_fn_raw_t)name)

void minic_register_builtins() {
	minic_api_sig_count = 0;

	minic_register_native("printf", minic_printf_native);
	minic_register_native("string", minic_string_native);

	// iron_array
	minic_register_array_struct("i8_array_t", (int)sizeof(i8_array_t), MINIC_T_INT);
	minic_register_array_struct("u8_array_t", (int)sizeof(u8_array_t), MINIC_T_INT);
	minic_register_array_struct("i16_array_t", (int)sizeof(i16_array_t), MINIC_T_INT);
	minic_register_array_struct("u16_array_t", (int)sizeof(u16_array_t), MINIC_T_INT);
	minic_register_array_struct("i32_array_t", (int)sizeof(i32_array_t), MINIC_T_INT);
	minic_register_array_struct("u32_array_t", (int)sizeof(u32_array_t), MINIC_T_INT);
	minic_register_array_struct("f32_array_t", (int)sizeof(f32_array_t), MINIC_T_FLOAT);
	minic_register_array_struct("any_array_t", (int)sizeof(any_array_t), MINIC_T_PTR);
	minic_register_array_struct("string_array_t", (int)sizeof(string_array_t), MINIC_T_PTR);
	minic_register_array_struct("buffer_t", (int)sizeof(buffer_t), MINIC_T_INT);

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
	MINIC_END();

	MINIC_STRUCT(shader_data_t);
	MINIC_S(name);
	MINIC_O(contexts, any_array_t);
	MINIC_END();

	MINIC_STRUCT(bind_const_t);
	MINIC_S(name);
	MINIC_O(vec, f32_array_t);
	MINIC_END();

	MINIC_STRUCT(bind_tex_t);
	MINIC_S(name);
	MINIC_S(file);
	MINIC_END();

	MINIC_STRUCT(material_context_t);
	MINIC_S(name);
	MINIC_O(bind_constants, bind_const_t_array_t);
	MINIC_O(bind_textures, bind_tex_t_array_t);
	MINIC_P(_);
	MINIC_END();

	MINIC_STRUCT(material_data_t);
	MINIC_S(name);
	MINIC_S(shader);
	MINIC_O(contexts, material_context_t_array_t);
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
	MINIC_I(visible);
	MINIC_I(culled);
	MINIC_I(is_empty);
	MINIC_P(ext);
	MINIC_S(ext_type);
	MINIC_END();

	MINIC_STRUCT(mesh_object_t);
	MINIC_O(base, object_t);
	MINIC_O(data, mesh_data_t);
	MINIC_O(material, material_data_t);
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
	MINIC_I(rdirty);
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
	R(iron_random_get, "i()");
	R(iron_random_get_max, "i(i max)");
	R(iron_random_get_in, "i(i min,i max)");
	R(vec4_fdist, "f(f v1x,f v1y,f v1z,f v2x,f v2y,f v2z)");
	R(mat4_cofactor, "f(f m0,f m1,f m2,f m3,f m4,f m5,f m6,f m7,f m8)");
	R(cosf, "f(f x)");
	R(sinf, "f(f x)");

	// object
	R(object_create, "p:object_t(i is_empty)");
	R(object_set_parent, "v(p:object_t raw,p:object_t parent_object)");
	R(object_remove, "v(p:object_t raw)");
	R(object_get_child, "p:object_t(p:object_t raw,p:char name)");

	// transform
	R(transform_create, "p:transform_t(p:object_t object)");
	R(transform_reset, "v(p:transform_t raw)");
	R(transform_update, "v(p:transform_t raw)");
	R(transform_build_matrix, "v(p:transform_t raw)");
	R(transform_decompose, "v(p:transform_t raw)");
	R(transform_world_x, "f(p:transform_t raw)");
	R(transform_world_y, "f(p:transform_t raw)");
	R(transform_world_z, "f(p:transform_t raw)");

	// camera_object
	R(camera_object_create, "p:camera_object_t(p:camera_data_t data)");
	R(camera_object_build_proj, "v(p:camera_object_t raw,f screen_aspect)");
	R(camera_object_remove, "v(p:camera_object_t raw)");
	R(camera_object_build_mat, "v(p:camera_object_t raw)");

	// world_data
	R(world_data_parse, "p:world_data_t(p:char name,p:char id)");
	R(world_data_load_envmap, "v(p:world_data_t raw)");

	// material_data
	R(material_data_create, "p:material_data_t(p:material_data_t raw,p:char file)");
	R(material_data_parse, "p:material_data_t(p:char file,p:char name)");
	R(material_data_get_context, "p:material_context_t(p:material_data_t raw,p:char name)");
	R(material_context_load, "v(p:material_context_t raw)");

	// shader_data
	R(shader_data_create, "p:shader_data_t(p:shader_data_t raw)");
	R(shader_data_parse, "p:shader_data_t(p:char file,p:char name)");
	R(shader_data_delete, "v(p:shader_data_t raw)");
	R(shader_data_get_context, "p:shader_context_t(p:shader_data_t raw,p:char name)");

	// shader_context
	R(shader_context_load, "v(p:shader_context_t raw)");
	R(shader_context_compile, "v(p:shader_context_t raw)");
	R(shader_context_finish_compile, "v(p:shader_context_t raw)");
	R(shader_context_delete, "v(p:shader_context_t raw)");
	R(shader_context_add_const, "v(p:shader_context_t raw,i offset)");
	R(shader_context_add_tex, "v(p:shader_context_t raw,i i)");

	// mesh_data
	R(mesh_data_parse, "p:mesh_data_t(p:char name,p:char id)");
	R(mesh_data_create, "p:mesh_data_t(p:mesh_data_t raw)");
	R(mesh_data_get_vertex_size, "i(p:char vertex_data)");
	R(mesh_data_build_vertices, "v(p:gpu_buffer_t vertex_buffer,p:any_array_t vertex_arrays)");
	R(mesh_data_build_indices, "v(p:gpu_buffer_t index_buffer,p:u32_array_t index_array)");
	R(mesh_data_get_vertex_array, "p:vertex_array_t(p:mesh_data_t raw,p:char name)");
	R(mesh_data_build, "v(p:mesh_data_t raw)");
	R(mesh_data_delete, "v(p:mesh_data_t raw)");

	// mesh_object
	R(mesh_object_create, "p:mesh_object_t(p:mesh_data_t data,p:material_data_t material)");
	R(mesh_object_set_data, "v(p:mesh_object_t raw,p:mesh_data_t data)");
	R(mesh_object_remove, "v(p:mesh_object_t raw)");
	R(mesh_object_render, "v(p:mesh_object_t raw,p:char context,p:string_array_t bind_params)");

	// data
	R(data_get_mesh, "p:mesh_data_t(p:char file,p:char name)");
	R(data_get_camera, "p:camera_data_t(p:char file,p:char name)");
	R(data_get_material, "p:material_data_t(p:char file,p:char name)");
	R(data_get_world, "p:world_data_t(p:char file,p:char name)");
	R(data_get_shader, "p:shader_data_t(p:char file,p:char name)");
	R(data_get_scene_raw, "p:scene_t(p:char file)");
	R(data_get_texture, "p:gpu_texture_t(p:char file)");
	R(data_get_blob, "p:buffer_t(p:char file)");
	R(data_get_video, "p:video_t(p:char file)");
	R(data_get_font, "p:draw_font_t(p:char file)");
	R(data_get_sound, "p:sound_t(p:char file)");
	R(data_delete_mesh, "v(p:char handle)");
	R(data_delete_blob, "v(p:char handle)");
	R(data_delete_texture, "v(p:char handle)");
	R(data_delete_video, "v(p:char handle)");
	R(data_delete_font, "v(p:char handle)");
	R(data_is_abs, "b(p:char file)");
	R(data_path, "p:char()");

	// scene
	R(scene_create, "p:object_t(p:scene_t format)");
	R(scene_remove, "v()");
	R(scene_set_active, "p:object_t(p:char scene_name)");
	R(scene_add_object, "p:object_t(p:object_t parent)");
	R(scene_get_child, "p:object_t(p:char name)");
	R(scene_add_mesh_object, "p:mesh_object_t(p:mesh_data_t data,p:material_data_t material,p:object_t parent)");
	R(scene_add_camera_object, "p:camera_object_t(p:camera_data_t data,p:object_t parent)");
	R(scene_add_scene, "p:object_t(p:char scene_name,p:object_t parent)");
	R(scene_spawn_object, "p:object_t(p:char name,p:object_t parent,i spawn_children)");
	R(scene_get_raw_object_by_name, "p:obj_t(p:scene_t format,p:char name)");
	R(scene_create_object, "p:object_t(p:obj_t o,p:scene_t format,p:object_t parent)");
	R(scene_create_mesh_object, "p:object_t(p:obj_t o,p:scene_t format,p:object_t parent,p:material_data_t material)");
	R(scene_gen_transform, "v(p:obj_t object,p:transform_t transform)");

	// render_path
	R(render_path_set_target, "v(p:char target,p:string_array_t additional,p:char depth_buffer,i flags,i color,f depth)");
	R(render_path_end, "v()");
	R(render_path_draw_meshes, "v(p:char context)");
	R(render_path_draw_skydome, "v(p:char handle)");
	R(render_path_bind_target, "v(p:char target,p:char uniform)");
	R(render_path_draw_shader, "v(p:char handle)");
	R(render_path_load_shader, "v(p:char handle)");
	R(render_path_resize, "v()");
	R(render_path_create_render_target, "p:render_target_t(p:render_target_t t)");
	R(render_target_create, "p:render_target_t()");

	// ui
	R(ui_begin, "v(p:ui_t ui)");
	R(ui_begin_sticky, "v()");
	R(ui_end_sticky, "v()");
	R(ui_begin_region, "v(p:ui_t ui,i x,i y,i w)");
	R(ui_end_region, "v()");
	R(ui_window, "b(p:ui_handle_t handle,i x,i y,i w,i h,i drag)");
	R(ui_button, "b(p:char text,i align,p:char label)");
	R(ui_text, "i(p:char text,i align,i bg)");
	R(ui_tab, "b(p:ui_handle_t handle,p:char text,i vertical,i color,i align_right)");
	R(ui_panel, "b(p:ui_handle_t handle,p:char text,i is_tree,i filled,i align_right)");
	R(ui_sub_image, "i(p:gpu_texture_t image,i tint,i h,i sx,i sy,i sw,i sh)");
	R(ui_image, "i(p:gpu_texture_t image,i tint,i h)");
	R(ui_text_input, "p:char(p:ui_handle_t handle,p:char label,i align,i editable,i live_update)");
	R(ui_check, "b(p:ui_handle_t handle,p:char text,p:char label)");
	R(ui_radio, "b(p:ui_handle_t handle,i position,p:char text,p:char label)");
	R(ui_combo, "i(p:ui_handle_t handle,p:string_array_t texts,p:char label,i show_label,i align,i search_bar)");
	R(ui_slider, "f(p:ui_handle_t handle,p:char text,f from,f to,i filled,f precision,i display_value,i align,i text_edit)");
	R(ui_row, "v(p:f32_array_t ratios)");
	R(ui_row2, "v()");
	R(ui_row3, "v()");
	R(ui_row4, "v()");
	R(ui_row5, "v()");
	R(ui_row6, "v()");
	R(ui_row7, "v()");
	R(ui_separator, "v(i h,i fill)");
	R(ui_tooltip, "v(p:char text)");
	R(ui_tooltip_image, "v(p:gpu_texture_t image,i max_width)");
	R(ui_end, "v()");
	R(ui_end_window, "v()");
	R(ui_mouse_down, "v(p:ui_t ui,i button,i x,i y)");
	R(ui_mouse_move, "v(p:ui_t ui,i x,i y,i movement_x,i movement_y)");
	R(ui_mouse_up, "v(p:ui_t ui,i button,i x,i y)");
	R(ui_mouse_wheel, "v(p:ui_t ui,f delta)");
	R(ui_key_down, "v(p:ui_t ui,i key_code)");
	R(ui_key_up, "v(p:ui_t ui,i key_code)");
	R(ui_key_press, "v(p:ui_t ui,i character)");
	R(ui_handle_create, "p:ui_handle_t()");
	R(ui_nest, "p:ui_handle_t(p:ui_handle_t handle,i pos)");
	R(ui_set_scale, "v(f factor)");
	R(ui_get_hover, "b(f elem_h)");
	R(ui_get_released, "b(f elem_h)");
	R(ui_input_in_rect, "b(f x,f y,f w,f h)");
	R(ui_fill, "v(f x,f y,f w,f h,i color)");
	R(ui_rect, "v(f x,f y,f w,f h,i color,f strength)");
	R(ui_is_visible, "b(f elem_h)");
	R(ui_end_element, "v()");
	R(ui_end_element_of_size, "v(f element_size)");
	R(ui_fade_color, "v(f alpha)");
	R(ui_draw_string, "v(p:char text,f x_offset,f y_offset,i align,i truncation)");
	R(ui_draw_shadow, "v(f x,f y,f w,f h)");
	R(ui_draw_rect, "v(i fill,i shadows,f x,f y,f w,f h)");
	R(ui_start_text_edit, "v(p:ui_handle_t handle,i align)");
	R(UI_SCALE, "f()");
	R(UI_ELEMENT_W, "f()");
	R(UI_ELEMENT_H, "f()");
	R(UI_ELEMENT_OFFSET, "f()");
	R(UI_ARROW_SIZE, "f()");
	R(UI_BUTTON_H, "f()");
	R(UI_CHECK_SIZE, "f()");
	R(UI_CHECK_SELECT_SIZE, "f()");
	R(UI_FONT_SIZE, "f()");
	R(UI_SCROLL_W, "f()");
	R(UI_TEXT_OFFSET, "f()");
	R(UI_TAB_W, "f()");
	R(UI_HEADER_DRAG_H, "f()");
	R(UI_TOOLTIP_DELAY, "f()");
	R(ui_float_input, "f(p:ui_handle_t handle,p:char label,i align,f precision)");
	R(ui_inline_radio, "i(p:ui_handle_t handle,p:string_array_t texts,i align)");
	R(ui_color_wheel, "i(p:ui_handle_t handle,i alpha,f w,f h,i color_preview,p picker,p data)");
	R(ui_text_area, "p:char(p:ui_handle_t handle,i align,i editable,p:char label,i word_wrap)");
	R(ui_begin_menu, "v()");
	R(ui_end_menu, "v()");
	R(ui_menubar_button, "b(p:char text)");
	R(ui_color_r, "i(i color)");
	R(ui_color_g, "i(i color)");
	R(ui_color_b, "i(i color)");
	R(ui_color_a, "i(i color)");
	R(ui_color, "i(i r,i g,i b,i a)");

	// ui_nodes
	R(ui_nodes_init, "v(p:ui_nodes_t nodes)");
	R(ui_node_canvas, "v(p:ui_nodes_t nodes,p:ui_node_canvas_t canvas)");
	R(ui_nodes_rgba_popup, "v(p:ui_handle_t nhandle,p:float val,i x,i y)");
	R(ui_remove_node, "v(p:ui_node_t n,p:ui_node_canvas_t canvas)");
	R(UI_NODES_SCALE, "f()");
	R(UI_NODES_PAN_X, "f()");
	R(UI_NODES_PAN_Y, "f()");
	R(UI_NODE_X, "f(p:ui_node_t node)");
	R(UI_NODE_Y, "f(p:ui_node_t node)");
	R(UI_NODE_W, "f(p:ui_node_t node)");
	R(UI_NODE_H, "f(p:ui_node_canvas_t canvas,p:ui_node_t node)");
	R(UI_OUTPUT_Y, "f(p:ui_node_t node,i pos)");
	R(UI_INPUT_Y, "f(p:ui_node_canvas_t canvas,p:ui_node_t node,i pos)");
	R(UI_OUTPUTS_H, "f(p:ui_node_t node,i length)");
	R(UI_BUTTONS_H, "f(p:ui_node_t node)");
	R(UI_LINE_H, "f()");
	R(ui_get_socket_id, "i(p:ui_node_array_t nodes)");
	R(ui_get_link, "p:ui_node_link_t(p:ui_node_link_array_t links,i id)");
	R(ui_next_link_id, "i(p:ui_node_link_array_t links)");
	R(ui_get_node, "p:ui_node_t(p:ui_node_array_t nodes,i id)");
	R(ui_next_node_id, "i(p:ui_node_array_t nodes)");

	// sys
	R(sys_time, "f()");
	R(sys_delta, "f()");
	R(sys_real_delta, "f()");
	R(sys_w, "i()");
	R(sys_h, "i()");
	R(sys_x, "i()");
	R(sys_y, "i()");
	R(sys_title, "p:char()");
	R(sys_title_set, "v(p:char value)");
	R(sys_get_shader, "p:gpu_shader_t(p:char name)");
	R(sys_buffer_to_string, "p:char(p:buffer_t b)");
	R(sys_string_to_buffer, "p:buffer_t(p:char str)");

	// iron_shape
	R(line_draw_init, "v()");
	R(line_draw_lineb, "v(i a,i b,i c,i d,i e,i f)");
	R(line_draw_line, "v(f x1,f y1,f z1,f x2,f y2,f z2)");
	R(line_draw_begin, "v()");
	R(line_draw_end, "v()");

	// iron_draw
	R(draw_begin, "v(p:gpu_texture_t target,i clear,i color)");
	R(draw_scaled_sub_image, "v(p:gpu_texture_t img,f sx,f sy,f sw,f sh,f dx,f dy,f dw,f dh)");
	R(draw_scaled_image, "v(p:gpu_texture_t tex,f dx,f dy,f dw,f dh)");
	R(draw_sub_image, "v(p:gpu_texture_t tex,f sx,f sy,f sw,f sh,f x,f y)");
	R(draw_image, "v(p:gpu_texture_t tex,f x,f y)");
	R(draw_filled_triangle, "v(f x0,f y0,f x1,f y1,f x2,f y2)");
	R(draw_filled_rect, "v(f x,f y,f width,f height)");
	R(draw_rect, "v(f x,f y,f width,f height,f strength)");
	R(draw_line, "v(f x0,f y0,f x1,f y1,f strength)");
	R(draw_line_aa, "v(f x0,f y0,f x1,f y1,f strength)");
	R(draw_string, "v(p:char text,f x,f y)");
	R(draw_end, "v()");
	R(draw_flush, "v()");
	R(draw_set_color, "v(i color)");
	R(draw_get_color, "i()");
	R(draw_set_pipeline, "v(p:gpu_pipeline_t pipeline)");
	R(draw_set_font, "b(p:draw_font_t font,i size)");
	R(draw_sub_string_width, "f(p:draw_font_t font,i font_size,p:char text,i start,i end)");
	R(draw_string_width, "i(p:draw_font_t font,i font_size,p:char text)");
	R(draw_filled_circle, "v(f cx,f cy,f radius,i segments)");
	R(draw_circle, "v(f cx,f cy,f radius,i segments,f strength)");
	R(draw_cubic_bezier, "v(p:f32_array_t x,p:f32_array_t y,i segments,f strength)");

	// iron_audio
#ifdef IRON_AUDIO
	R(audio_play, "v(p:iron_a1_sound_t sound,i loop)");
#endif

	// iron_string
	R(string_alloc, "p:char(i size)");
	R(string_copy, "p:char(p:char a)");
	R(string_length, "i(p:char str)");
	R(string_equals, "b(p:char a,p:char b)");
	R(i32_to_string, "p:char(i i)");
	R(i32_to_string_hex, "p:char(i i)");
	R(i64_to_string, "p:char(i i)");
	R(u64_to_string, "p:char(i i)");
	R(f32_to_string, "p:char(f f)");
	R(f32_to_string_with_zeros, "p:char(f f)");
	R(string_strip_trailing_zeros, "v(p:char str)");
	R(string_index_of, "i(p:char s,p:char search)");
	R(string_index_of_pos, "i(p:char s,p:char search,i pos)");
	R(string_last_index_of, "i(p:char s,p:char search)");
	R(string_split, "p:any_array_t(p:char s,p:char sep)");
	R(string_array_join, "p:char(p:any_array_t a,p:char separator)");
	R(string_replace_all, "p:char(p:char s,p:char search,p:char replace)");
	R(substring, "p:char(p:char s,i start,i end)");
	R(string_from_char_code, "p:char(i c)");
	R(char_code_at, "i(p:char s,i i)");
	R(char_at, "p:char(p:char s,i i)");
	R(starts_with, "b(p:char s,p:char start)");
	R(ends_with, "b(p:char s,p:char end)");
	R(to_lower_case, "p:char(p:char s)");
	R(to_upper_case, "p:char(p:char s)");
	R(trim_end, "p:char(p:char str)");
	R(string_utf8_decode, "i(p:char str,p:int i)");

	// iron_file
	R(iron_file_reader_open, "b(p:iron_file_reader_t reader,p:char filepath,i type)");
	R(iron_file_reader_close, "b(p:iron_file_reader_t reader)");
	R(iron_file_reader_read, "i(p:iron_file_reader_t reader,p data,i size)");
	R(iron_file_reader_size, "i(p:iron_file_reader_t reader)");
	R(iron_file_reader_pos, "i(p:iron_file_reader_t reader)");
	R(iron_file_reader_seek, "b(p:iron_file_reader_t reader,i pos)");
	R(iron_file_writer_open, "b(p:iron_file_writer_t writer,p:char filepath)");
	R(iron_file_writer_write, "v(p:iron_file_writer_t writer,p data,i size)");
	R(iron_file_writer_close, "v(p:iron_file_writer_t writer)");
	R(iron_read_directory, "p:char(p:char path)");
	R(iron_create_directory, "v(p:char path)");
	R(iron_is_directory, "b(p:char path)");
	R(iron_file_exists, "b(p:char path)");
	R(iron_delete_file, "v(p:char path)");
	R(iron_file_save_bytes, "v(p:char path,p:buffer_t bytes,i length)");
	R(iron_file_download, "v(p:char url,p callback,i size,p:char dst_path)");
	R(file_read_directory, "p:any_array_t(p:char path)");
	R(file_copy, "v(p:char src_path,p:char dst_path)");
	R(file_start, "v(p:char path)");
	R(file_download_to, "v(p:char url,p:char dst_path,p done,i size)");

	// iron_gc
	R(gc_alloc, "p(i size)");
	R(gc_leaf, "v(p ptr)");
	R(gc_root, "v(p ptr)");
	R(gc_unroot, "v(p ptr)");
	R(gc_realloc, "p(p ptr,i size)");
	R(gc_free, "v(p ptr)");
	R(gc_pause, "v()");
	R(gc_resume, "v()");
	R(gc_run, "v()");
	R(gc_start, "v(p bos)");
	R(gc_stop, "v()");

	// iron_map
	R(i32_map_set, "v(p:i32_map_t m,p:char k,i v)");
	R(f32_map_set, "v(p:f32_map_t m,p:char k,f v)");
	R(any_map_set, "v(p:any_map_t m,p:char k,p v)");
	R(i32_map_get, "i(p:i32_map_t m,p:char k)");
	R(f32_map_get, "f(p:f32_map_t m,p:char k)");
	R(any_map_get, "p(p:any_map_t m,p:char k)");
	R(map_delete, "v(p:any_map_t m,p:char k)");
	R(map_keys, "p:any_array_t(p:any_map_t m)");
	R(i32_map_create, "p:i32_map_t()");
	R(any_map_create, "p:any_map_t()");
	R(i32_imap_set, "v(p:i32_imap_t m,i k,i v)");
	R(any_imap_set, "v(p:any_imap_t m,i k,p v)");
	R(i32_imap_get, "i(p:i32_imap_t m,i k)");
	R(any_imap_get, "p(p:any_imap_t m,i k)");
	R(imap_delete, "v(p:any_imap_t m,i k)");
	R(imap_keys, "p:i32_array_t(p:any_imap_t m)");
	R(any_imap_create, "p:any_imap_t()");

	// iron_array
	R(array_free, "v(p a)");
	R(i8_array_push, "v(p:i8_array_t a,i e)");
	R(u8_array_push, "v(p:u8_array_t a,i e)");
	R(i16_array_push, "v(p:i16_array_t a,i e)");
	R(u16_array_push, "v(p:u16_array_t a,i e)");
	R(i32_array_push, "v(p:i32_array_t a,i e)");
	R(u32_array_push, "v(p:u32_array_t a,i e)");
	R(f32_array_push, "v(p:f32_array_t a,f e)");
	R(any_array_push, "v(p:any_array_t a,p e)");
	R(string_array_push, "v(p:string_array_t a,p e)");
	R(i8_array_resize, "v(p:i8_array_t a,i size)");
	R(u8_array_resize, "v(p:u8_array_t a,i size)");
	R(i16_array_resize, "v(p:i16_array_t a,i size)");
	R(u16_array_resize, "v(p:u16_array_t a,i size)");
	R(i32_array_resize, "v(p:i32_array_t a,i size)");
	R(u32_array_resize, "v(p:u32_array_t a,i size)");
	R(f32_array_resize, "v(p:f32_array_t a,i size)");
	R(any_array_resize, "v(p:any_array_t a,i size)");
	R(string_array_resize, "v(p:string_array_t a,i size)");
	R(buffer_resize, "v(p:buffer_t b,i size)");
	R(array_sort, "v(p:any_array_t ar,p compare)");
	R(i32_array_sort, "v(p:i32_array_t ar,p compare)");
	R(array_pop, "p(p:any_array_t ar)");
	R(i32_array_pop, "i(p:i32_array_t ar)");
	R(array_shift, "p(p:any_array_t ar)");
	R(array_splice, "v(p:any_array_t ar,i start,i delete_count)");
	R(i32_array_splice, "v(p:i32_array_t ar,i start,i delete_count)");
	R(array_concat, "p:any_array_t(p:any_array_t a,p:any_array_t b)");
	R(array_slice, "p:any_array_t(p:any_array_t a,i begin,i end)");
	R(array_insert, "v(p:any_array_t a,i at,p e)");
	R(array_remove, "v(p:any_array_t ar,p e)");
	R(string_array_remove, "v(p:string_array_t ar,p:char e)");
	R(i32_array_remove, "v(p:i32_array_t ar,i e)");
	R(array_index_of, "i(p:any_array_t ar,p e)");
	R(string_array_index_of, "i(p:string_array_t ar,p:char e)");
	R(i32_array_index_of, "i(p:i32_array_t ar,i e)");
	R(array_reverse, "v(p:any_array_t ar)");
	R(buffer_slice, "p:buffer_t(p:buffer_t a,i begin,i end)");
	R(buffer_get_u8, "i(p:buffer_t b,i p)");
	R(buffer_get_i8, "i(p:buffer_t b,i p)");
	R(buffer_get_u16, "i(p:buffer_t b,i p)");
	R(buffer_get_i16, "i(p:buffer_t b,i p)");
	R(buffer_get_f16, "f(p:buffer_t b,i p)");
	R(buffer_get_u32, "i(p:buffer_t b,i p)");
	R(buffer_get_i32, "i(p:buffer_t b,i p)");
	R(buffer_get_f32, "f(p:buffer_t b,i p)");
	R(buffer_get_f64, "f(p:buffer_t b,i p)");
	R(buffer_get_i64, "i(p:buffer_t b,i p)");
	R(buffer_set_u8, "v(p:buffer_t b,i p,i n)");
	R(buffer_set_i8, "v(p:buffer_t b,i p,i n)");
	R(buffer_set_u16, "v(p:buffer_t b,i p,i n)");
	R(buffer_set_i16, "v(p:buffer_t b,i p,i n)");
	R(buffer_set_u32, "v(p:buffer_t b,i p,i n)");
	R(buffer_set_i32, "v(p:buffer_t b,i p,i n)");
	R(buffer_set_f32, "v(p:buffer_t b,i p,f n)");
	R(buffer_create, "p:buffer_t(i length)");
	R(buffer_create_from_raw, "p:buffer_t(p:uint8_t raw,i length)");
	R(f32_array_create, "p:f32_array_t(i length)");
	R(f32_array_create_from_buffer, "p:f32_array_t(p:buffer_t b)");
	R(f32_array_create_from_array, "p:f32_array_t(p:f32_array_t from)");
	R(f32_array_create_from_raw, "p:f32_array_t(p:float raw,i length)");
	R(f32_array_create_x, "p:f32_array_t(f x)");
	R(f32_array_create_xy, "p:f32_array_t(f x,f y)");
	R(f32_array_create_xyz, "p:f32_array_t(f x,f y,f z)");
	R(f32_array_create_xyzw, "p:f32_array_t(f x,f y,f z,f w)");
	R(f32_array_create_xyzwv, "p:f32_array_t(f x,f y,f z,f w,f v)");
	R(u32_array_create, "p:u32_array_t(i length)");
	R(u32_array_create_from_array, "p:u32_array_t(p:u32_array_t from)");
	R(u32_array_create_from_raw, "p:u32_array_t(p:uint32_t raw,i length)");
	R(i32_array_create, "p:i32_array_t(i length)");
	R(i32_array_create_from_array, "p:i32_array_t(p:i32_array_t from)");
	R(i32_array_create_from_raw, "p:i32_array_t(p:int32_t raw,i length)");
	R(u16_array_create, "p:u16_array_t(i length)");
	R(u16_array_create_from_raw, "p:u16_array_t(p:uint16_t raw,i length)");
	R(i16_array_create, "p:i16_array_t(i length)");
	R(i16_array_create_from_array, "p:i16_array_t(p:i16_array_t from)");
	R(i16_array_create_from_raw, "p:i16_array_t(p:int16_t raw,i length)");
	R(u8_array_create, "p:u8_array_t(i length)");
	R(u8_array_create_from_array, "p:u8_array_t(p:u8_array_t from)");
	R(u8_array_create_from_raw, "p:u8_array_t(p:uint8_t raw,i length)");
	R(u8_array_create_from_string, "p:u8_array_t(p:char s)");
	R(u8_array_to_string, "p:char(p:u8_array_t a)");
	R(i8_array_create, "p:i8_array_t(i length)");
	R(i8_array_create_from_raw, "p:i8_array_t(p:int8_t raw,i length)");
	R(any_array_create, "p:any_array_t(i length)");
	R(any_array_create_from_raw, "p:any_array_t(p raw,i length)");
	R(string_array_create, "p:string_array_t(i length)");
	R(float_to_half_fast, "i(f value)");
	R(half_to_u8_fast, "i(i h)");

	// iron_input
	minic_register_global("mouse_x", &mouse_x, MINIC_T_FLOAT);
	minic_register_global("mouse_y", &mouse_y, MINIC_T_FLOAT);
	R(mouse_down, "b(p:char button)");
	R(mouse_down_any, "b()");
	R(mouse_started, "b(p:char button)");
	R(mouse_started_any, "b()");
	R(mouse_released, "b(p:char button)");
	R(mouse_view_x, "f()");
	R(mouse_view_y, "f()");
	R(keyboard_down, "b(p:char key)");
	R(keyboard_started, "b(p:char key)");
	R(keyboard_started_any, "b()");
	R(keyboard_released, "b(p:char key)");
	R(keyboard_repeat, "b(p:char key)");
	R(keyboard_key_code, "p:char(i key)");

	// paint
	R(plugin_create, "p:plugin_t()");
	R(plugin_notify_on_ui, "v(p:plugin_t plugin,p f)");
	R(plugin_notify_on_update, "v(p:plugin_t plugin,p f)");
	R(plugin_notify_on_delete, "v(p:plugin_t plugin,p f)");
	R(script_notify_on_update, "v(p fn)");
	R(script_notify_on_next_frame, "v(p fn)");
	R(console_info, "v(p:char s)");
	R(console_error, "v(p:char s)");
	R(console_log, "v(p:char s)");
	R(ui_box_show_message, "v(p:char title,p:char text,i copyable)");
	R(ui_files_show2, "v(p:char filters,i is_save,i open_multiple,p files_done)");
	R(project_save, "v(i save_and_quit)");
	R(project_filepath_get, "p:char()");
	R(project_basepath_get, "p:char()");
	R(project_filepath_set, "v(p:char s)");
	R(script_get_context, "p:context_t()");
	R(script_get_config, "p:config_t()");
	R(script_get_project, "p:project_t()");
	R(script_get_object, "p:object_t(p:char s)");
	R(script_shape_add, "p:object_t(p:char name)");
	R(script_object_duplicate, "p:object_t(p:object_t o)");
	R(script_add_trait, "v(p:char object,p:char trait)");
	R(script_physics_set_shape, "v(p:object_t o,i shape)");
	R(script_physics_set_mass, "v(p:object_t o,f mass)");
	R(script_physics_apply_impulse, "v(p:object_t o,f x,f y,f z)");
	R(script_physics_set_velocity, "v(p:object_t o,f x,f y,f z)");
	R(script_physics_sync_transform, "v(p:object_t o)");
	R(script_set_stage, "v(p:char name)");
	R(script_show_envmap, "v(i b)");
	R(script_fade_to_stage, "v(p:char stage)");
	R(script_timer, "v(f delay,p fn)");
	R(script_get_stage, "p:char()");
	R(script_set_tilesheet_anim, "v(p:object_t o,p:char anim)");
	R(script_draw_particles, "v(p:gpu_texture_t texture,f x,f y,f w,f h,i atlas_x,i atlas_frames)");
	R(script_paint, "v(f x,f y)");
	R(script_paint_world, "v(f x,f y,f z)");
	R(script_paint_end, "v()");
	R(script_fill_layer, "v()");
	R(script_material_create, "p:slot_material_t(p:char name)");
	R(script_material_set, "v(p:slot_material_t m)");
	R(script_object_set_material, "v(p:object_t object,p:slot_material_t material)");
	R(script_material_delete, "v(p:slot_material_t m)");
	R(script_material_create_node, "p:ui_node_t(p:char type)");
	R(script_material_create_node_at, "p:ui_node_t(p:char type,f x,f y)");
	R(script_material_get_node, "p:ui_node_t(p:char type)");
	R(script_material_get_node_id, "p:ui_node_t(i id)");
	R(script_material_connect, "v(p:ui_node_t from,i from_socket,p:ui_node_t to,i to_socket)");
	R(script_material_disconnect, "v(p:ui_node_t to,i to_socket)");
	R(script_material_remove_node, "v(p:ui_node_t node)");
	R(script_material_set_float, "v(p:ui_node_t node,i is_input,i socket,f value)");
	R(script_material_set_color, "v(p:ui_node_t node,i is_input,i socket,f r,f g,f b,f a)");
	R(script_material_set_vector, "v(p:ui_node_t node,i is_input,i socket,f x,f y,f z)");
	R(script_material_update, "v()");
	R(context_set_viewport_shader, "v(p viewport_shader)");
	R(context_set_viewport_mode, "v(i mode)");
	R(context_set_camera_controls, "v(i i)");
	R(node_shader_write_frag, "v(p:node_shader_t raw,p:char s)");
	R(plugin_register_texture, "v(p:char format,p fn)");
	R(plugin_unregister_texture, "v(p:char format)");
	R(plugin_register_mesh, "v(p:char format,p fn)");
	R(plugin_unregister_mesh, "v(p:char format)");
	R(plugin_register_text, "v(p:char format,p fn)");
	R(plugin_unregister_text, "v(p:char format)");
	R(plugin_make_raw_mesh, "p:raw_mesh_t(p:char name,p:i16_array_t posa,p:i16_array_t nora,p:u32_array_t inda,f scale_pos)");
	R(plugin_material_category_add, "v(p:char category_name,p:any_array_t node_list)");
	R(plugin_brush_category_add, "v(p:char category_name,p:any_array_t node_list)");
	R(plugin_material_category_remove, "v(p:char category_name)");
	R(plugin_brush_category_remove, "v(p:char category_name)");
	R(plugin_material_custom_nodes_set, "v(p:char node_type,p fn)");
	R(plugin_brush_custom_nodes_set, "v(p:char node_type,p fn)");
	R(plugin_material_custom_nodes_remove, "v(p:char node_type)");
	R(plugin_brush_custom_nodes_remove, "v(p:char node_type)");
	R(plugin_material_kong_get, "p()");
	R(parser_material_parse_value_input, "p:char(p:ui_node_socket_t inp,i vector_as_grayscale)");
	R(context_main_object, "p:mesh_object_t()");
	R(export_texture_run, "v(p:char path,i bake_material)");
	R(context_select_tool, "v(i i)");
	R(gpu_create_render_target, "p:gpu_texture_t(i width,i height,i format)");
	R(viewport_capture_screenshot_to, "v(p:gpu_texture_t target,f x,f y,f w,f h)");
	R(viewport_save_texture, "v(p:gpu_texture_t screenshot)");
	R(project_reskin_mesh, "b(i frame)");
	R(iron_delay_idle_sleep, "v()");

	// json
	R(json_parse, "p(p:char s)");
	R(json_parse_to_map, "p:any_map_t(p:char s)");
	R(json_encode_begin, "v()");
	R(json_encode_end, "p:char()");
	R(json_encode_string, "v(p:char k,p:char v)");
	R(json_encode_string_array, "v(p:char k,p:string_array_t a)");
	R(json_encode_f32, "v(p:char k,f f)");
	R(json_encode_i32, "v(p:char k,i i)");
	R(json_encode_null, "v(p:char k)");
	R(json_encode_f32_array, "v(p:char k,p:f32_array_t a)");
	R(json_encode_i32_array, "v(p:char k,p:i32_array_t a)");
	R(json_encode_bool, "v(p:char k,i b)");
	R(json_encode_begin_array, "v(p:char k)");
	R(json_encode_end_array, "v()");
	R(json_encode_begin_object, "v()");
	R(json_encode_end_object, "v()");
	R(json_encode_map, "v(p:any_map_t m)");
	R(json_encode_to_armpack, "p:buffer_t(p:char json)");

	// armpack
	R(armpack_decode, "p(p:buffer_t b)");
	R(armpack_decode_to_map, "p:any_map_t(p:buffer_t b)");
	R(armpack_decode_to_json, "p:char(p:buffer_t b)");
	R(armpack_encode_start, "v(p encoded)");
	R(armpack_encode_end, "i()");
	R(armpack_encode_map, "v(i count)");
	R(armpack_encode_array, "v(i count)");
	R(armpack_encode_array_f32, "v(p:f32_array_t f32a)");
	R(armpack_encode_array_i32, "v(p:i32_array_t i32a)");
	R(armpack_encode_array_i16, "v(p:i16_array_t i16a)");
	R(armpack_encode_array_u8, "v(p:u8_array_t u8a)");
	R(armpack_encode_array_string, "v(p:string_array_t strings)");
	R(armpack_encode_string, "v(p:char str)");
	R(armpack_encode_i32, "v(i i)");
	R(armpack_encode_f32, "v(f f)");
	R(armpack_encode_bool, "v(i b)");
	R(armpack_encode_null, "v()");
	R(armpack_size_map, "i()");
	R(armpack_size_array, "i()");
	R(armpack_size_array_f32, "i(p:f32_array_t f32a)");
	R(armpack_size_array_u8, "i(p:u8_array_t u8a)");
	R(armpack_size_string, "i(p:char str)");
	R(armpack_size_i32, "i()");
	R(armpack_size_f32, "i()");
	R(armpack_size_bool, "i()");
	R(armpack_map_get_f32, "f(p:any_map_t map,p:char key)");
	R(armpack_map_get_i32, "i(p:any_map_t map,p:char key)");
}

#undef R

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
