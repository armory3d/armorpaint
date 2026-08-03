
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
		if (*p == ' ') {
			skip = true;
		}
		else if (*p == ',' || *p == ')') {
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
	R(object_create, "p(i is_empty)");
	R(object_set_parent, "v(p raw,p parent_object)");
	R(object_remove, "v(p raw)");
	R(object_get_child, "p(p raw,p name)");

	// transform
	R(transform_create, "p(p object)");
	R(transform_reset, "v(p raw)");
	R(transform_update, "v(p raw)");
	R(transform_build_matrix, "v(p raw)");
	R(transform_decompose, "v(p raw)");
	R(transform_world_x, "f(p raw)");
	R(transform_world_y, "f(p raw)");
	R(transform_world_z, "f(p raw)");

	// camera_object
	R(camera_object_create, "p(p data)");
	R(camera_object_build_proj, "v(p raw,f screen_aspect)");
	R(camera_object_remove, "v(p raw)");
	R(camera_object_build_mat, "v(p raw)");

	// world_data
	R(world_data_parse, "p(p name,p id)");
	R(world_data_load_envmap, "v(p raw)");

	// material_data
	R(material_data_create, "p(p raw,p file)");
	R(material_data_parse, "p(p file,p name)");
	R(material_data_get_context, "p(p raw,p name)");
	R(material_context_load, "v(p raw)");

	// shader_data
	R(shader_data_create, "p(p raw)");
	R(shader_data_parse, "p(p file,p name)");
	R(shader_data_delete, "v(p raw)");
	R(shader_data_get_context, "p(p raw,p name)");

	// shader_context
	R(shader_context_load, "v(p raw)");
	R(shader_context_compile, "v(p raw)");
	R(shader_context_finish_compile, "v(p raw)");
	R(shader_context_delete, "v(p raw)");
	R(shader_context_add_const, "v(p raw,i offset)");
	R(shader_context_add_tex, "v(p raw,i i)");

	// mesh_data
	R(mesh_data_parse, "p(p name,p id)");
	R(mesh_data_create, "p(p raw)");
	R(mesh_data_get_vertex_size, "i(p vertex_data)");
	R(mesh_data_build_vertices, "v(p vertex_buffer,p vertex_arrays)");
	R(mesh_data_build_indices, "v(p index_buffer,p index_array)");
	R(mesh_data_get_vertex_array, "p(p raw,p name)");
	R(mesh_data_build, "v(p raw)");
	R(mesh_data_delete, "v(p raw)");

	// mesh_object
	R(mesh_object_create, "p(p data,p material)");
	R(mesh_object_set_data, "v(p raw,p data)");
	R(mesh_object_remove, "v(p raw)");
	R(mesh_object_render, "v(p raw,p context,p bind_params)");

	// data
	R(data_get_mesh, "p(p file,p name)");
	R(data_get_camera, "p(p file,p name)");
	R(data_get_material, "p(p file,p name)");
	R(data_get_world, "p(p file,p name)");
	R(data_get_shader, "p(p file,p name)");
	R(data_get_scene_raw, "p(p file)");
	R(data_get_texture, "p(p file)");
	R(data_get_blob, "p(p file)");
	R(data_get_video, "p(p file)");
	R(data_get_font, "p(p file)");
	R(data_get_sound, "p(p file)");
	R(data_delete_mesh, "v(p handle)");
	R(data_delete_blob, "v(p handle)");
	R(data_delete_texture, "v(p handle)");
	R(data_delete_video, "v(p handle)");
	R(data_delete_font, "v(p handle)");
	R(data_is_abs, "b(p file)");
	R(data_path, "p()");

	// scene
	R(scene_create, "p(p format)");
	R(scene_remove, "v()");
	R(scene_set_active, "p(p scene_name)");
	R(scene_add_object, "p(p parent)");
	R(scene_get_child, "p(p name)");
	R(scene_add_mesh_object, "p(p data,p material,p parent)");
	R(scene_add_camera_object, "p(p data,p parent)");
	R(scene_add_scene, "p(p scene_name,p parent)");
	R(scene_spawn_object, "p(p name,p parent,i spawn_children)");
	R(scene_get_raw_object_by_name, "p(p format,p name)");
	R(scene_create_object, "p(p o,p format,p parent)");
	R(scene_create_mesh_object, "p(p o,p format,p parent,p material)");
	R(scene_gen_transform, "v(p object,p transform)");

	// render_path
	R(render_path_set_target, "v(p target,p additional,p depth_buffer,i flags,i color,f depth)");
	R(render_path_end, "v()");
	R(render_path_draw_meshes, "v(p context)");
	R(render_path_draw_skydome, "v(p handle)");
	R(render_path_bind_target, "v(p target,p uniform)");
	R(render_path_draw_shader, "v(p handle)");
	R(render_path_load_shader, "v(p handle)");
	R(render_path_resize, "v()");
	R(render_path_create_render_target, "p(p t)");
	R(render_target_create, "p()");

	// ui
	R(ui_begin, "v(p ui)");
	R(ui_begin_sticky, "v()");
	R(ui_end_sticky, "v()");
	R(ui_begin_region, "v(p ui,i x,i y,i w)");
	R(ui_end_region, "v()");
	R(ui_window, "b(p handle,i x,i y,i w,i h,i drag)");
	R(ui_button, "b(p text,i align,p label)");
	R(ui_text, "i(p text,i align,i bg)");
	R(ui_tab, "b(p handle,p text,i vertical,i color,i align_right)");
	R(ui_panel, "b(p handle,p text,i is_tree,i filled,i align_right)");
	R(ui_sub_image, "i(p image,i tint,i h,i sx,i sy,i sw,i sh)");
	R(ui_image, "i(p image,i tint,i h)");
	R(ui_text_input, "p(p handle,p label,i align,i editable,i live_update)");
	R(ui_check, "b(p handle,p text,p label)");
	R(ui_radio, "b(p handle,i position,p text,p label)");
	R(ui_combo, "i(p handle,p texts,p label,i show_label,i align,i search_bar)");
	R(ui_slider, "f(p handle,p text,f from,f to,i filled,f precision,i display_value,i align,i text_edit)");
	R(ui_row, "v(p ratios)");
	R(ui_row2, "v()");
	R(ui_row3, "v()");
	R(ui_row4, "v()");
	R(ui_row5, "v()");
	R(ui_row6, "v()");
	R(ui_row7, "v()");
	R(ui_separator, "v(i h,i fill)");
	R(ui_tooltip, "v(p text)");
	R(ui_tooltip_image, "v(p image,i max_width)");
	R(ui_end, "v()");
	R(ui_end_window, "v()");
	R(ui_mouse_down, "v(p ui,i button,i x,i y)");
	R(ui_mouse_move, "v(p ui,i x,i y,i movement_x,i movement_y)");
	R(ui_mouse_up, "v(p ui,i button,i x,i y)");
	R(ui_mouse_wheel, "v(p ui,f delta)");
	R(ui_key_down, "v(p ui,i key_code)");
	R(ui_key_up, "v(p ui,i key_code)");
	R(ui_key_press, "v(p ui,i character)");
	R(ui_handle_create, "p()");
	R(ui_nest, "p(p handle,i pos)");
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
	R(ui_draw_string, "v(p text,f x_offset,f y_offset,i align,i truncation)");
	R(ui_draw_shadow, "v(f x,f y,f w,f h)");
	R(ui_draw_rect, "v(i fill,i shadows,f x,f y,f w,f h)");
	R(ui_start_text_edit, "v(p handle,i align)");
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
	R(ui_float_input, "f(p handle,p label,i align,f precision)");
	R(ui_inline_radio, "i(p handle,p texts,i align)");
	R(ui_color_wheel, "i(p handle,i alpha,f w,f h,i color_preview,p picker,p data)");
	R(ui_text_area, "p(p handle,i align,i editable,p label,i word_wrap)");
	R(ui_begin_menu, "v()");
	R(ui_end_menu, "v()");
	R(ui_menubar_button, "b(p text)");
	R(ui_color_r, "i(i color)");
	R(ui_color_g, "i(i color)");
	R(ui_color_b, "i(i color)");
	R(ui_color_a, "i(i color)");
	R(ui_color, "i(i r,i g,i b,i a)");

	// ui_nodes
	R(ui_nodes_init, "v(p nodes)");
	R(ui_node_canvas, "v(p nodes,p canvas)");
	R(ui_nodes_rgba_popup, "v(p nhandle,p val,i x,i y)");
	R(ui_remove_node, "v(p n,p canvas)");
	R(UI_NODES_SCALE, "f()");
	R(UI_NODES_PAN_X, "f()");
	R(UI_NODES_PAN_Y, "f()");
	R(UI_NODE_X, "f(p node)");
	R(UI_NODE_Y, "f(p node)");
	R(UI_NODE_W, "f(p node)");
	R(UI_NODE_H, "f(p canvas,p node)");
	R(UI_OUTPUT_Y, "f(p node,i pos)");
	R(UI_INPUT_Y, "f(p canvas,p node,i pos)");
	R(UI_OUTPUTS_H, "f(p node,i length)");
	R(UI_BUTTONS_H, "f(p node)");
	R(UI_LINE_H, "f()");
	R(ui_get_socket_id, "i(p nodes)");
	R(ui_get_link, "p(p links,i id)");
	R(ui_next_link_id, "i(p links)");
	R(ui_get_node, "p(p nodes,i id)");
	R(ui_next_node_id, "i(p nodes)");

	// sys
	R(sys_time, "f()");
	R(sys_delta, "f()");
	R(sys_real_delta, "f()");
	R(sys_w, "i()");
	R(sys_h, "i()");
	R(sys_x, "i()");
	R(sys_y, "i()");
	R(sys_title, "p()");
	R(sys_title_set, "v(p value)");
	R(sys_get_shader, "p(p name)");
	R(sys_buffer_to_string, "p(p b)");
	R(sys_string_to_buffer, "p(p str)");

	// iron_shape
	R(line_draw_init, "v()");
	R(line_draw_lineb, "v(i a,i b,i c,i d,i e,i f)");
	R(line_draw_line, "v(f x1,f y1,f z1,f x2,f y2,f z2)");
	R(line_draw_begin, "v()");
	R(line_draw_end, "v()");

	// iron_draw
	R(draw_begin, "v(p target,i clear,i color)");
	R(draw_scaled_sub_image, "v(p img,f sx,f sy,f sw,f sh,f dx,f dy,f dw,f dh)");
	R(draw_scaled_image, "v(p tex,f dx,f dy,f dw,f dh)");
	R(draw_sub_image, "v(p tex,f sx,f sy,f sw,f sh,f x,f y)");
	R(draw_image, "v(p tex,f x,f y)");
	R(draw_filled_triangle, "v(f x0,f y0,f x1,f y1,f x2,f y2)");
	R(draw_filled_rect, "v(f x,f y,f width,f height)");
	R(draw_rect, "v(f x,f y,f width,f height,f strength)");
	R(draw_line, "v(f x0,f y0,f x1,f y1,f strength)");
	R(draw_line_aa, "v(f x0,f y0,f x1,f y1,f strength)");
	R(draw_string, "v(p text,f x,f y)");
	R(draw_end, "v()");
	R(draw_flush, "v()");
	R(draw_set_color, "v(i color)");
	R(draw_get_color, "i()");
	R(draw_set_pipeline, "v(p pipeline)");
	R(draw_set_font, "b(p font,i size)");
	R(draw_sub_string_width, "f(p font,i font_size,p text,i start,i end)");
	R(draw_string_width, "i(p font,i font_size,p text)");
	R(draw_filled_circle, "v(f cx,f cy,f radius,i segments)");
	R(draw_circle, "v(f cx,f cy,f radius,i segments,f strength)");
	R(draw_cubic_bezier, "v(p x,p y,i segments,f strength)");

	// iron_audio
#ifdef IRON_AUDIO
	R(audio_play, "v(p sound,i loop)");
#endif

	// iron_string
	R(string_alloc, "p(i size)");
	R(string_copy, "p(p a)");
	R(string_length, "i(p str)");
	R(string_equals, "b(p a,p b)");
	R(i32_to_string, "p(i i)");
	R(i32_to_string_hex, "p(i i)");
	R(i64_to_string, "p(i i)");
	R(u64_to_string, "p(i i)");
	R(f32_to_string, "p(f f)");
	R(f32_to_string_with_zeros, "p(f f)");
	R(string_strip_trailing_zeros, "v(p str)");
	R(string_index_of, "i(p s,p search)");
	R(string_index_of_pos, "i(p s,p search,i pos)");
	R(string_last_index_of, "i(p s,p search)");
	R(string_split, "p(p s,p sep)");
	R(string_array_join, "p(p a,p separator)");
	R(string_replace_all, "p(p s,p search,p replace)");
	R(substring, "p(p s,i start,i end)");
	R(string_from_char_code, "p(i c)");
	R(char_code_at, "i(p s,i i)");
	R(char_at, "p(p s,i i)");
	R(starts_with, "b(p s,p start)");
	R(ends_with, "b(p s,p end)");
	R(to_lower_case, "p(p s)");
	R(to_upper_case, "p(p s)");
	R(trim_end, "p(p str)");
	R(string_utf8_decode, "i(p str,p i)");

	// iron_file
	R(iron_file_reader_open, "b(p reader,p filepath,i type)");
	R(iron_file_reader_close, "b(p reader)");
	R(iron_file_reader_read, "i(p reader,p data,i size)");
	R(iron_file_reader_size, "i(p reader)");
	R(iron_file_reader_pos, "i(p reader)");
	R(iron_file_reader_seek, "b(p reader,i pos)");
	R(iron_file_writer_open, "b(p writer,p filepath)");
	R(iron_file_writer_write, "v(p writer,p data,i size)");
	R(iron_file_writer_close, "v(p writer)");
	R(iron_read_directory, "p(p path)");
	R(iron_create_directory, "v(p path)");
	R(iron_is_directory, "b(p path)");
	R(iron_file_exists, "b(p path)");
	R(iron_delete_file, "v(p path)");
	R(iron_file_save_bytes, "v(p path,p bytes,i length)");
	R(iron_file_download, "v(p url,p callback,i size,p dst_path)");
	R(file_read_directory, "p(p path)");
	R(file_copy, "v(p src_path,p dst_path)");
	R(file_start, "v(p path)");
	R(file_download_to, "v(p url,p dst_path,p done,i size)");

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
	R(i32_map_set, "v(p m,p k,i v)");
	R(f32_map_set, "v(p m,p k,f v)");
	R(any_map_set, "v(p m,p k,p v)");
	R(i32_map_get, "i(p m,p k)");
	R(f32_map_get, "f(p m,p k)");
	R(any_map_get, "p(p m,p k)");
	R(map_delete, "v(p m,p k)");
	R(map_keys, "p(p m)");
	R(i32_map_create, "p()");
	R(any_map_create, "p()");
	R(i32_imap_set, "v(p m,i k,i v)");
	R(any_imap_set, "v(p m,i k,p v)");
	R(i32_imap_get, "i(p m,i k)");
	R(any_imap_get, "p(p m,i k)");
	R(imap_delete, "v(p m,i k)");
	R(imap_keys, "p(p m)");
	R(any_imap_create, "p()");

	// iron_array
	R(array_free, "v(p a)");
	R(i8_array_push, "v(p a,i e)");
	R(u8_array_push, "v(p a,i e)");
	R(i16_array_push, "v(p a,i e)");
	R(u16_array_push, "v(p a,i e)");
	R(i32_array_push, "v(p a,i e)");
	R(u32_array_push, "v(p a,i e)");
	R(f32_array_push, "v(p a,f e)");
	R(any_array_push, "v(p a,p e)");
	R(string_array_push, "v(p a,p e)");
	R(i8_array_resize, "v(p a,i size)");
	R(u8_array_resize, "v(p a,i size)");
	R(i16_array_resize, "v(p a,i size)");
	R(u16_array_resize, "v(p a,i size)");
	R(i32_array_resize, "v(p a,i size)");
	R(u32_array_resize, "v(p a,i size)");
	R(f32_array_resize, "v(p a,i size)");
	R(any_array_resize, "v(p a,i size)");
	R(string_array_resize, "v(p a,i size)");
	R(buffer_resize, "v(p b,i size)");
	R(array_sort, "v(p ar,p compare)");
	R(i32_array_sort, "v(p ar,p compare)");
	R(array_pop, "p(p ar)");
	R(i32_array_pop, "i(p ar)");
	R(array_shift, "p(p ar)");
	R(array_splice, "v(p ar,i start,i delete_count)");
	R(i32_array_splice, "v(p ar,i start,i delete_count)");
	R(array_concat, "p(p a,p b)");
	R(array_slice, "p(p a,i begin,i end)");
	R(array_insert, "v(p a,i at,p e)");
	R(array_remove, "v(p ar,p e)");
	R(string_array_remove, "v(p ar,p e)");
	R(i32_array_remove, "v(p ar,i e)");
	R(array_index_of, "i(p ar,p e)");
	R(string_array_index_of, "i(p ar,p e)");
	R(i32_array_index_of, "i(p ar,i e)");
	R(array_reverse, "v(p ar)");
	R(buffer_slice, "p(p a,i begin,i end)");
	R(buffer_get_u8, "i(p b,i p)");
	R(buffer_get_i8, "i(p b,i p)");
	R(buffer_get_u16, "i(p b,i p)");
	R(buffer_get_i16, "i(p b,i p)");
	R(buffer_get_f16, "f(p b,i p)");
	R(buffer_get_u32, "i(p b,i p)");
	R(buffer_get_i32, "i(p b,i p)");
	R(buffer_get_f32, "f(p b,i p)");
	R(buffer_get_f64, "f(p b,i p)");
	R(buffer_get_i64, "i(p b,i p)");
	R(buffer_set_u8, "v(p b,i p,i n)");
	R(buffer_set_i8, "v(p b,i p,i n)");
	R(buffer_set_u16, "v(p b,i p,i n)");
	R(buffer_set_i16, "v(p b,i p,i n)");
	R(buffer_set_u32, "v(p b,i p,i n)");
	R(buffer_set_i32, "v(p b,i p,i n)");
	R(buffer_set_f32, "v(p b,i p,f n)");
	R(buffer_create, "p(i length)");
	R(buffer_create_from_raw, "p(p raw,i length)");
	R(f32_array_create, "p(i length)");
	R(f32_array_create_from_buffer, "p(p b)");
	R(f32_array_create_from_array, "p(p from)");
	R(f32_array_create_from_raw, "p(p raw,i length)");
	R(f32_array_create_x, "p(f x)");
	R(f32_array_create_xy, "p(f x,f y)");
	R(f32_array_create_xyz, "p(f x,f y,f z)");
	R(f32_array_create_xyzw, "p(f x,f y,f z,f w)");
	R(f32_array_create_xyzwv, "p(f x,f y,f z,f w,f v)");
	R(u32_array_create, "p(i length)");
	R(u32_array_create_from_array, "p(p from)");
	R(u32_array_create_from_raw, "p(p raw,i length)");
	R(i32_array_create, "p(i length)");
	R(i32_array_create_from_array, "p(p from)");
	R(i32_array_create_from_raw, "p(p raw,i length)");
	R(u16_array_create, "p(i length)");
	R(u16_array_create_from_raw, "p(p raw,i length)");
	R(i16_array_create, "p(i length)");
	R(i16_array_create_from_array, "p(p from)");
	R(i16_array_create_from_raw, "p(p raw,i length)");
	R(u8_array_create, "p(i length)");
	R(u8_array_create_from_array, "p(p from)");
	R(u8_array_create_from_raw, "p(p raw,i length)");
	R(u8_array_create_from_string, "p(p s)");
	R(u8_array_to_string, "p(p a)");
	R(i8_array_create, "p(i length)");
	R(i8_array_create_from_raw, "p(p raw,i length)");
	R(any_array_create, "p(i length)");
	R(any_array_create_from_raw, "p(p raw,i length)");
	R(string_array_create, "p(i length)");
	R(float_to_half_fast, "i(f value)");
	R(half_to_u8_fast, "i(i h)");

	// iron_input
	minic_register_global("mouse_x", &mouse_x, MINIC_T_FLOAT);
	minic_register_global("mouse_y", &mouse_y, MINIC_T_FLOAT);
	R(mouse_down, "b(p button)");
	R(mouse_down_any, "b()");
	R(mouse_started, "b(p button)");
	R(mouse_started_any, "b()");
	R(mouse_released, "b(p button)");
	R(mouse_view_x, "f()");
	R(mouse_view_y, "f()");
	R(keyboard_down, "b(p key)");
	R(keyboard_started, "b(p key)");
	R(keyboard_started_any, "b()");
	R(keyboard_released, "b(p key)");
	R(keyboard_repeat, "b(p key)");
	R(keyboard_key_code, "p(i key)");

	// paint
	R(plugin_create, "p()");
	R(plugin_notify_on_ui, "v(p plugin,p f)");
	R(plugin_notify_on_update, "v(p plugin,p f)");
	R(plugin_notify_on_delete, "v(p plugin,p f)");
	R(script_notify_on_update, "v(p fn)");
	R(script_notify_on_next_frame, "v(p fn)");
	R(console_info, "v(p s)");
	R(console_error, "v(p s)");
	R(console_log, "v(p s)");
	R(ui_box_show_message, "v(p title,p text,i copyable)");
	R(ui_files_show2, "v(p filters,i is_save,i open_multiple,p files_done)");
	R(project_save, "v(i save_and_quit)");
	R(project_filepath_get, "p()");
	R(project_basepath_get, "p()");
	R(project_filepath_set, "v(p s)");
	R(script_get_context, "p()");
	R(script_get_config, "p()");
	R(script_get_project, "p()");
	R(script_get_object, "p(p s)");
	R(script_shape_add, "p(p name)");
	R(script_set_stage, "v(p name)");
	R(script_show_envmap, "v(i b)");
	R(script_fade_to_stage, "v(p stage)");
	R(script_timer, "v(f delay,p fn)");
	R(script_get_stage, "p()");
	R(script_set_tilesheet_anim, "v(p o,p anim)");
	R(script_draw_particles, "v(p texture,f x,f y,f w,f h,i atlas_x,i atlas_frames)");
	R(script_paint, "v(f x,f y)");
	R(script_paint_world, "v(f x,f y,f z)");
	R(script_paint_end, "v()");
	R(script_fill_layer, "v()");
	R(script_material_create, "p(p name)");
	R(script_material_set, "v(p m)");
	R(script_object_set_material, "v(p object,p material)");
	R(script_material_delete, "v(p m)");
	R(script_material_create_node, "p(p type)");
	R(script_material_create_node_at, "p(p type,f x,f y)");
	R(script_material_get_node, "p(p type)");
	R(script_material_get_node_id, "p(i id)");
	R(script_material_connect, "v(p from,i from_socket,p to,i to_socket)");
	R(script_material_disconnect, "v(p to,i to_socket)");
	R(script_material_remove_node, "v(p node)");
	R(script_material_set_float, "v(p node,i is_input,i socket,f value)");
	R(script_material_set_color, "v(p node,i is_input,i socket,f r,f g,f b,f a)");
	R(script_material_set_vector, "v(p node,i is_input,i socket,f x,f y,f z)");
	R(script_material_update, "v()");
	R(context_set_viewport_shader, "v(p viewport_shader)");
	R(context_set_viewport_mode, "v(i mode)");
	R(context_set_camera_controls, "v(i i)");
	R(node_shader_write_frag, "v(p raw,p s)");
	R(plugin_register_texture, "v(p format,p fn)");
	R(plugin_unregister_texture, "v(p format)");
	R(plugin_register_mesh, "v(p format,p fn)");
	R(plugin_unregister_mesh, "v(p format)");
	R(plugin_register_text, "v(p format,p fn)");
	R(plugin_unregister_text, "v(p format)");
	R(plugin_make_raw_mesh, "p(p name,p posa,p nora,p inda,f scale_pos)");
	R(plugin_material_category_add, "v(p category_name,p node_list)");
	R(plugin_brush_category_add, "v(p category_name,p node_list)");
	R(plugin_material_category_remove, "v(p category_name)");
	R(plugin_brush_category_remove, "v(p category_name)");
	R(plugin_material_custom_nodes_set, "v(p node_type,p fn)");
	R(plugin_brush_custom_nodes_set, "v(p node_type,p fn)");
	R(plugin_material_custom_nodes_remove, "v(p node_type)");
	R(plugin_brush_custom_nodes_remove, "v(p node_type)");
	R(plugin_material_kong_get, "p()");
	R(parser_material_parse_value_input, "p(p inp,i vector_as_grayscale)");
	R(context_main_object, "p()");
	R(export_texture_run, "v(p path,i bake_material)");
	R(context_select_tool, "v(i i)");
	R(gpu_create_render_target, "p(i width,i height,i format)");
	R(viewport_capture_screenshot_to, "v(p target,f x,f y,f w,f h)");
	R(viewport_save_texture, "v(p screenshot)");
	R(project_reskin_mesh, "b(i frame)");
	R(iron_delay_idle_sleep, "v()");

	// json
	R(json_parse, "p(p s)");
	R(json_parse_to_map, "p(p s)");
	R(json_encode_begin, "v()");
	R(json_encode_end, "p()");
	R(json_encode_string, "v(p k,p v)");
	R(json_encode_string_array, "v(p k,p a)");
	R(json_encode_f32, "v(p k,f f)");
	R(json_encode_i32, "v(p k,i i)");
	R(json_encode_null, "v(p k)");
	R(json_encode_f32_array, "v(p k,p a)");
	R(json_encode_i32_array, "v(p k,p a)");
	R(json_encode_bool, "v(p k,i b)");
	R(json_encode_begin_array, "v(p k)");
	R(json_encode_end_array, "v()");
	R(json_encode_begin_object, "v()");
	R(json_encode_end_object, "v()");
	R(json_encode_map, "v(p m)");
	R(json_encode_to_armpack, "p(p json)");

	// armpack
	R(armpack_decode, "p(p b)");
	R(armpack_decode_to_map, "p(p b)");
	R(armpack_decode_to_json, "p(p b)");
	R(armpack_encode_start, "v(p encoded)");
	R(armpack_encode_end, "i()");
	R(armpack_encode_map, "v(i count)");
	R(armpack_encode_array, "v(i count)");
	R(armpack_encode_array_f32, "v(p f32a)");
	R(armpack_encode_array_i32, "v(p i32a)");
	R(armpack_encode_array_i16, "v(p i16a)");
	R(armpack_encode_array_u8, "v(p u8a)");
	R(armpack_encode_array_string, "v(p strings)");
	R(armpack_encode_string, "v(p str)");
	R(armpack_encode_i32, "v(i i)");
	R(armpack_encode_f32, "v(f f)");
	R(armpack_encode_bool, "v(i b)");
	R(armpack_encode_null, "v()");
	R(armpack_size_map, "i()");
	R(armpack_size_array, "i()");
	R(armpack_size_array_f32, "i(p f32a)");
	R(armpack_size_array_u8, "i(p u8a)");
	R(armpack_size_string, "i(p str)");
	R(armpack_size_i32, "i()");
	R(armpack_size_f32, "i()");
	R(armpack_size_bool, "i()");
	R(armpack_map_get_f32, "f(p map,p key)");
	R(armpack_map_get_i32, "i(p map,p key)");
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
		if (sig[0] == '\0') {
			string_buffer_append(&sb, string("void %s(...);\n", name));
			continue;
		}
		const char *ret = minic_api_sig_type(sig[0]);
		string_buffer_append(&sb, string("%s %s(", ret, name));
		const char *p = sig;
		if (*p) {
			p++; // Skip ret
		}
		if (*p == '(') {
			p++;
		}
		int arg = 0;
		while (*p && *p != ')') {
			if (*p == ',') {
				p++;
				continue;
			}
			if (arg > 0) {
				string_buffer_append(&sb, ", ");
			}
			const char *type = minic_api_sig_type(*p);
			p++;
			const char *arg_name = "";
			int         name_len = 0;
			if (*p == ' ') {
				p++;
				arg_name = p;
				while (*p && *p != ',' && *p != ')') {
					p++;
				}
				name_len = (int)(p - arg_name);
			}
			// Pointer types already end with "*"
			const char *sep = name_len > 0 && type[strlen(type) - 1] != '*' ? " " : "";
			string_buffer_append(&sb, string("%s%s%.*s", type, sep, name_len, arg_name));
			arg++;
		}
		if (arg == 0) {
			string_buffer_append(&sb, "void");
		}
		string_buffer_append(&sb, ");\n");
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}
