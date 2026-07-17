
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

#include "types.h"

typedef struct raw_mesh raw_mesh_t;
extern context_t       *g_context;
extern config_t        *g_config;
extern project_t       *g_project;

void   console_log(char *s);
void   console_info(char *s);
void   console_error(char *s);
bool   point_in_aabb(object_t *object, vec4_t point);
vec4_t raycast_aabb_mouse(object_t *object);
void   iron_delay_idle_sleep();

void           ui_box_show_message(char *title, char *text, bool copyable);
void           ui_files_show2(char *filters, bool is_save, bool open_multiple, void *files_done);
void           project_save(bool save_and_quit);
char          *project_filepath_get();
char          *project_basepath_get();
void           project_filepath_set(char *s);
void           project_reimport_mesh_skinned(i32 frame);
void           context_set_viewport_shader(void *viewport_shader);
void           context_set_viewport_mode(int mode);
void           context_set_camera_controls(int i);
void           node_shader_write_frag(void *raw, char *s);
mesh_object_t *context_main_object();
void           export_texture_run(char *path, bool bake_material);
void           context_select_tool(i32 i);
gpu_texture_t *gpu_create_render_target(i32 width, i32 height, i32 format);
void           viewport_capture_screenshot_to(gpu_texture_t *target, float x, float y, float w, float h);
void           viewport_save_texture(gpu_texture_t *screenshot);
char          *parser_material_parse_value_input(ui_node_socket_t *inp, bool vector_as_grayscale);

void       *plugin_create();
void        plugin_notify_on_ui(void *plugin, void *f);
void        plugin_notify_on_update(void *plugin, void *f);
void        plugin_notify_on_delete(void *plugin, void *f);
void        plugin_register_texture(char *format, void *fn);
void        plugin_unregister_texture(char *format);
void        plugin_register_mesh(char *format, void *fn);
void        plugin_unregister_mesh(char *format);
void        plugin_register_text(char *format, void *fn);
void        plugin_unregister_text(char *format);
raw_mesh_t *plugin_make_raw_mesh(char *name, i16_array_t *posa, i16_array_t *nora, u32_array_t *inda, float scale_pos);
void        plugin_material_category_add(char *category_name, any_array_t *node_list);
void        plugin_brush_category_add(char *category_name, any_array_t *node_list);
void        plugin_material_category_remove(char *category_name);
void        plugin_brush_category_remove(char *category_name);
void        plugin_material_custom_nodes_set(char *node_type, void *fn);
void        plugin_brush_custom_nodes_set(char *node_type, void *fn);
void        plugin_material_custom_nodes_remove(char *node_type);
void        plugin_brush_custom_nodes_remove(char *node_type);
void       *plugin_material_kong_get();

void       script_tween_to(object_t *o, vec4_t to, f32 speed);
void       script_notify_on_update(void *fn);
void       script_notify_on_next_frame(void *fn);
context_t *script_get_context();
config_t  *script_get_config();
project_t *script_get_project();
void       script_set_stage(char *name);
void       script_fade_to_stage(char *stage);
void       script_timer(f32 delay, void *fn);
char      *script_get_stage();
void       script_set_tilesheet_anim(object_t *o, char *anim);
object_t  *script_get_object(char *s);
void       script_draw_particles(gpu_texture_t *texture, float x, float y, float w, float h, int atlas_x, int atlas_frames);

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

// Argument accessors for the wrapper table
#define V2(i) minic_get_vec2(_a[i].p)
#define V4(i) minic_get_vec4(_a[i].p)
#define QT(i) minic_get_quat(_a[i].p)
#define M3(i) minic_get_mat3(_a[i].p)
#define M4(i) minic_get_mat4(_a[i].p)
#define AF(i) (_a[i].f)
#define AP(i) (_a[i].p)

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

#define R(name, sig) minic_register(#name, sig, (minic_ext_fn_raw_t)name)

void minic_register_builtins() {
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
	MINIC_P(material);
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
	R(iron_random_get_max, "i(i)");
	R(iron_random_get_in, "i(i,i)");
	R(vec4_fdist, "f(f,f,f,f,f,f)");
	R(mat4_cofactor, "f(f,f,f,f,f,f,f,f,f)");
	R(cosf, "f(f)");
	R(sinf, "f(f)");

	// object
	R(object_create, "p(i)");
	R(object_set_parent, "v(p,p)");
	R(object_remove, "v(p)");
	R(object_get_child, "p(p,p)");

	// transform
	R(transform_create, "p(p)");
	R(transform_reset, "v(p)");
	R(transform_update, "v(p)");
	R(transform_build_matrix, "v(p)");
	R(transform_decompose, "v(p)");
	R(transform_world_x, "f(p)");
	R(transform_world_y, "f(p)");
	R(transform_world_z, "f(p)");

	// camera_object
	R(camera_object_create, "p(p)");
	R(camera_object_build_proj, "v(p,f)");
	R(camera_object_remove, "v(p)");
	R(camera_object_build_mat, "v(p)");

	// world_data
	R(world_data_parse, "p(p,p)");
	R(world_data_load_envmap, "v(p)");

	// material_data
	R(material_data_create, "p(p,p)");
	R(material_data_parse, "p(p,p)");
	R(material_data_get_context, "p(p,p)");
	R(material_context_load, "v(p)");

	// shader_data
	R(shader_data_create, "p(p)");
	R(shader_data_parse, "p(p,p)");
	R(shader_data_delete, "v(p)");
	R(shader_data_get_context, "p(p,p)");

	// shader_context
	R(shader_context_load, "v(p)");
	R(shader_context_compile, "v(p)");
	R(shader_context_finish_compile, "v(p)");
	R(shader_context_delete, "v(p)");
	R(shader_context_add_const, "v(p,i)");
	R(shader_context_add_tex, "v(p,i)");

	// mesh_data
	R(mesh_data_parse, "p(p,p)");
	R(mesh_data_create, "p(p)");
	R(mesh_data_get_vertex_size, "i(p)");
	R(mesh_data_build_vertices, "v(p,p)");
	R(mesh_data_build_indices, "v(p,p)");
	R(mesh_data_get_vertex_array, "p(p,p)");
	R(mesh_data_build, "v(p)");
	R(mesh_data_delete, "v(p)");

	// mesh_object
	R(mesh_object_create, "p(p,p)");
	R(mesh_object_set_data, "v(p,p)");
	R(mesh_object_remove, "v(p)");
	R(mesh_object_render, "v(p,p,p)");

	// data
	R(data_get_mesh, "p(p,p)");
	R(data_get_camera, "p(p,p)");
	R(data_get_material, "p(p,p)");
	R(data_get_world, "p(p,p)");
	R(data_get_shader, "p(p,p)");
	R(data_get_scene_raw, "p(p)");
	R(data_get_texture, "p(p)");
	R(data_get_blob, "p(p)");
	R(data_get_video, "p(p)");
	R(data_get_font, "p(p)");
	R(data_get_sound, "p(p)");
	R(data_delete_mesh, "v(p)");
	R(data_delete_blob, "v(p)");
	R(data_delete_texture, "v(p)");
	R(data_delete_video, "v(p)");
	R(data_delete_font, "v(p)");
	R(data_is_abs, "b(p)");
	R(data_path, "p()");

	// scene
	R(scene_create, "p(p)");
	R(scene_remove, "v()");
	R(scene_set_active, "p(p)");
	R(scene_add_object, "p(p)");
	R(scene_get_child, "p(p)");
	R(scene_add_mesh_object, "p(p,p,p)");
	R(scene_add_camera_object, "p(p,p)");
	R(scene_add_scene, "p(p,p)");
	R(scene_spawn_object, "p(p,p,i)");
	R(scene_get_raw_object_by_name, "p(p,p)");
	R(scene_create_object, "p(p,p,p)");
	R(scene_create_mesh_object, "p(p,p,p,p)");
	R(scene_gen_transform, "v(p,p)");

	// render_path
	R(render_path_set_target, "v(p,p,p,i,i,f)");
	R(render_path_end, "v()");
	R(render_path_draw_meshes, "v(p)");
	R(render_path_draw_skydome, "v(p)");
	R(render_path_bind_target, "v(p,p)");
	R(render_path_draw_shader, "v(p)");
	R(render_path_load_shader, "v(p)");
	R(render_path_resize, "v()");
	R(render_path_create_render_target, "p(p)");
	R(render_target_create, "p()");

	// ui
	R(ui_begin, "v(p)");
	R(ui_begin_sticky, "v()");
	R(ui_end_sticky, "v()");
	R(ui_begin_region, "v(p,i,i,i)");
	R(ui_end_region, "v()");
	R(ui_window, "b(p,i,i,i,i,i)");
	R(ui_button, "b(p,i,p)");
	R(ui_text, "i(p,i,i)");
	R(ui_tab, "b(p,p,i,i,i)");
	R(ui_panel, "b(p,p,i,i,i)");
	R(ui_sub_image, "i(p,i,i,i,i,i,i)");
	R(ui_image, "i(p,i,i)");
	R(ui_text_input, "p(p,p,i,i,i)");
	R(ui_check, "b(p,p,p)");
	R(ui_radio, "b(p,i,p,p)");
	R(ui_combo, "i(p,p,p,i,i,i)");
	R(ui_slider, "f(p,p,f,f,i,f,i,i,i)");
	R(ui_row, "v(p)");
	R(ui_row2, "v()");
	R(ui_row3, "v()");
	R(ui_row4, "v()");
	R(ui_row5, "v()");
	R(ui_row6, "v()");
	R(ui_row7, "v()");
	R(ui_separator, "v(i,i)");
	R(ui_tooltip, "v(p)");
	R(ui_tooltip_image, "v(p,i)");
	R(ui_end, "v()");
	R(ui_end_window, "v()");
	R(ui_mouse_down, "v(p,i,i,i)");
	R(ui_mouse_move, "v(p,i,i,i,i)");
	R(ui_mouse_up, "v(p,i,i,i)");
	R(ui_mouse_wheel, "v(p,f)");
	R(ui_key_down, "v(p,i)");
	R(ui_key_up, "v(p,i)");
	R(ui_key_press, "v(p,i)");
	R(ui_handle_create, "p()");
	R(ui_nest, "p(p,i)");
	R(ui_set_scale, "v(f)");
	R(ui_get_hover, "b(f)");
	R(ui_get_released, "b(f)");
	R(ui_input_in_rect, "b(f,f,f,f)");
	R(ui_fill, "v(f,f,f,f,i)");
	R(ui_rect, "v(f,f,f,f,i,f)");
	R(ui_is_visible, "b(f)");
	R(ui_end_element, "v()");
	R(ui_end_element_of_size, "v(f)");
	R(ui_fade_color, "v(f)");
	R(ui_draw_string, "v(p,f,f,i,i)");
	R(ui_draw_shadow, "v(f,f,f,f)");
	R(ui_draw_rect, "v(i,i,f,f,f,f)");
	R(ui_start_text_edit, "v(p,i)");
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
	R(ui_float_input, "f(p,p,i,f)");
	R(ui_inline_radio, "i(p,p,i)");
	R(ui_color_wheel, "i(p,i,f,f,i,p,p)");
	R(ui_text_area, "p(p,i,i,p,i)");
	R(ui_begin_menu, "v()");
	R(ui_end_menu, "v()");
	R(ui_menubar_button, "b(p)");
	R(ui_color_r, "i(i)");
	R(ui_color_g, "i(i)");
	R(ui_color_b, "i(i)");
	R(ui_color_a, "i(i)");
	R(ui_color, "i(i,i,i,i)");

	// ui_nodes
	R(ui_nodes_init, "v(p)");
	R(ui_node_canvas, "v(p,p)");
	R(ui_nodes_rgba_popup, "v(p,p,i,i)");
	R(ui_remove_node, "v(p,p)");
	R(UI_NODES_SCALE, "f()");
	R(UI_NODES_PAN_X, "f()");
	R(UI_NODES_PAN_Y, "f()");
	R(UI_NODE_X, "f(p)");
	R(UI_NODE_Y, "f(p)");
	R(UI_NODE_W, "f(p)");
	R(UI_NODE_H, "f(p,p)");
	R(UI_OUTPUT_Y, "f(p,i)");
	R(UI_INPUT_Y, "f(p,p,i)");
	R(UI_OUTPUTS_H, "f(p,i)");
	R(UI_BUTTONS_H, "f(p)");
	R(UI_LINE_H, "f()");
	R(ui_get_socket_id, "i(p)");
	R(ui_get_link, "p(p,i)");
	R(ui_next_link_id, "i(p)");
	R(ui_get_node, "p(p,i)");
	R(ui_next_node_id, "i(p)");

	// sys
	R(sys_time, "f()");
	R(sys_delta, "f()");
	R(sys_real_delta, "f()");
	R(sys_w, "i()");
	R(sys_h, "i()");
	R(sys_x, "i()");
	R(sys_y, "i()");
	R(sys_title, "p()");
	R(sys_title_set, "v(p)");
	R(sys_get_shader, "p(p)");
	R(sys_buffer_to_string, "p(p)");
	R(sys_string_to_buffer, "p(p)");

	// iron_shape
	R(line_draw_init, "v()");
	R(line_draw_lineb, "v(i,i,i,i,i,i)");
	R(line_draw_line, "v(f,f,f,f,f,f)");
	R(line_draw_begin, "v()");
	R(line_draw_end, "v()");

	// iron_draw
	R(draw_begin, "v(p,i,i)");
	R(draw_scaled_sub_image, "v(p,f,f,f,f,f,f,f,f)");
	R(draw_scaled_image, "v(p,f,f,f,f)");
	R(draw_sub_image, "v(p,f,f,f,f,f,f)");
	R(draw_image, "v(p,f,f)");
	R(draw_filled_triangle, "v(f,f,f,f,f,f)");
	R(draw_filled_rect, "v(f,f,f,f)");
	R(draw_rect, "v(f,f,f,f,f)");
	R(draw_line, "v(f,f,f,f,f)");
	R(draw_line_aa, "v(f,f,f,f,f)");
	R(draw_string, "v(p,f,f)");
	R(draw_end, "v()");
	R(draw_flush, "v()");
	R(draw_set_color, "v(i)");
	R(draw_get_color, "i()");
	R(draw_set_pipeline, "v(p)");
	R(draw_set_font, "b(p,i)");
	R(draw_sub_string_width, "f(p,i,p,i,i)");
	R(draw_string_width, "i(p,i,p)");
	R(draw_filled_circle, "v(f,f,f,i)");
	R(draw_circle, "v(f,f,f,i,f)");
	R(draw_cubic_bezier, "v(p,p,i,f)");

	// iron_audio
#ifdef IRON_AUDIO
	R(audio_play, "v(p,i)");
#endif

	// iron_string
	R(string_alloc, "p(i)");
	R(string_copy, "p(p)");
	R(string_length, "i(p)");
	R(string_equals, "b(p,p)");
	R(i32_to_string, "p(i)");
	R(i32_to_string_hex, "p(i)");
	R(i64_to_string, "p(i)");
	R(u64_to_string, "p(i)");
	R(f32_to_string, "p(f)");
	R(f32_to_string_with_zeros, "p(f)");
	R(string_strip_trailing_zeros, "v(p)");
	R(string_index_of, "i(p,p)");
	R(string_index_of_pos, "i(p,p,i)");
	R(string_last_index_of, "i(p,p)");
	R(string_split, "p(p,p)");
	R(string_array_join, "p(p,p)");
	R(string_replace_all, "p(p,p,p)");
	R(substring, "p(p,i,i)");
	R(string_from_char_code, "p(i)");
	R(char_code_at, "i(p,i)");
	R(char_at, "p(p,i)");
	R(starts_with, "b(p,p)");
	R(ends_with, "b(p,p)");
	R(to_lower_case, "p(p)");
	R(to_upper_case, "p(p)");
	R(trim_end, "p(p)");
	R(string_utf8_decode, "i(p,p)");

	// iron_file
	R(iron_file_reader_open, "b(p,p,i)");
	R(iron_file_reader_close, "b(p)");
	R(iron_file_reader_read, "i(p,p,i)");
	R(iron_file_reader_size, "i(p)");
	R(iron_file_reader_pos, "i(p)");
	R(iron_file_reader_seek, "b(p,i)");
	R(iron_file_writer_open, "b(p,p)");
	R(iron_file_writer_write, "v(p,p,i)");
	R(iron_file_writer_close, "v(p)");
	R(iron_read_directory, "p(p)");
	R(iron_create_directory, "v(p)");
	R(iron_is_directory, "b(p)");
	R(iron_file_exists, "b(p)");
	R(iron_delete_file, "v(p)");
	R(iron_file_save_bytes, "v(p,p,i)");
	R(iron_file_download, "v(p,p,i,p)");
	R(file_read_directory, "p(p)");
	R(file_copy, "v(p,p)");
	R(file_start, "v(p)");
	R(file_download_to, "v(p,p,p,i)");

	// iron_gc
	R(gc_alloc, "p(i)");
	R(gc_leaf, "v(p)");
	R(gc_root, "v(p)");
	R(gc_unroot, "v(p)");
	R(gc_realloc, "p(p,i)");
	R(gc_free, "v(p)");
	R(gc_pause, "v()");
	R(gc_resume, "v()");
	R(gc_run, "v()");
	R(gc_start, "v(p)");
	R(gc_stop, "v()");

	// iron_map
	R(i32_map_set, "v(p,p,i)");
	R(f32_map_set, "v(p,p,f)");
	R(any_map_set, "v(p,p,p)");
	R(i32_map_get, "i(p,p)");
	R(f32_map_get, "f(p,p)");
	R(any_map_get, "p(p,p)");
	R(map_delete, "v(p,p)");
	R(map_keys, "p(p)");
	R(i32_map_create, "p()");
	R(any_map_create, "p()");
	R(i32_imap_set, "v(p,i,i)");
	R(any_imap_set, "v(p,i,p)");
	R(i32_imap_get, "i(p,i)");
	R(any_imap_get, "p(p,i)");
	R(imap_delete, "v(p,i)");
	R(imap_keys, "p(p)");
	R(any_imap_create, "p()");

	// iron_array
	R(array_free, "v(p)");
	R(i8_array_push, "v(p,i)");
	R(u8_array_push, "v(p,i)");
	R(i16_array_push, "v(p,i)");
	R(u16_array_push, "v(p,i)");
	R(i32_array_push, "v(p,i)");
	R(u32_array_push, "v(p,i)");
	R(f32_array_push, "v(p,f)");
	R(any_array_push, "v(p,p)");
	R(string_array_push, "v(p,p)");
	R(i8_array_resize, "v(p,i)");
	R(u8_array_resize, "v(p,i)");
	R(i16_array_resize, "v(p,i)");
	R(u16_array_resize, "v(p,i)");
	R(i32_array_resize, "v(p,i)");
	R(u32_array_resize, "v(p,i)");
	R(f32_array_resize, "v(p,i)");
	R(any_array_resize, "v(p,i)");
	R(string_array_resize, "v(p,i)");
	R(buffer_resize, "v(p,i)");
	R(array_sort, "v(p,p)");
	R(i32_array_sort, "v(p,p)");
	R(array_pop, "p(p)");
	R(i32_array_pop, "i(p)");
	R(array_shift, "p(p)");
	R(array_splice, "v(p,i,i)");
	R(i32_array_splice, "v(p,i,i)");
	R(array_concat, "p(p,p)");
	R(array_slice, "p(p,i,i)");
	R(array_insert, "v(p,i,p)");
	R(array_remove, "v(p,p)");
	R(string_array_remove, "v(p,p)");
	R(i32_array_remove, "v(p,i)");
	R(array_index_of, "i(p,p)");
	R(string_array_index_of, "i(p,p)");
	R(i32_array_index_of, "i(p,i)");
	R(array_reverse, "v(p)");
	R(buffer_slice, "p(p,i,i)");
	R(buffer_get_u8, "i(p,i)");
	R(buffer_get_i8, "i(p,i)");
	R(buffer_get_u16, "i(p,i)");
	R(buffer_get_i16, "i(p,i)");
	R(buffer_get_f16, "f(p,i)");
	R(buffer_get_u32, "i(p,i)");
	R(buffer_get_i32, "i(p,i)");
	R(buffer_get_f32, "f(p,i)");
	R(buffer_get_f64, "f(p,i)");
	R(buffer_get_i64, "i(p,i)");
	R(buffer_set_u8, "v(p,i,i)");
	R(buffer_set_i8, "v(p,i,i)");
	R(buffer_set_u16, "v(p,i,i)");
	R(buffer_set_i16, "v(p,i,i)");
	R(buffer_set_u32, "v(p,i,i)");
	R(buffer_set_i32, "v(p,i,i)");
	R(buffer_set_f32, "v(p,i,f)");
	R(buffer_create, "p(i)");
	R(buffer_create_from_raw, "p(p,i)");
	R(f32_array_create, "p(i)");
	R(f32_array_create_from_buffer, "p(p)");
	R(f32_array_create_from_array, "p(p)");
	R(f32_array_create_from_raw, "p(p,i)");
	R(f32_array_create_x, "p(f)");
	R(f32_array_create_xy, "p(f,f)");
	R(f32_array_create_xyz, "p(f,f,f)");
	R(f32_array_create_xyzw, "p(f,f,f,f)");
	R(f32_array_create_xyzwv, "p(f,f,f,f,f)");
	R(u32_array_create, "p(i)");
	R(u32_array_create_from_array, "p(p)");
	R(u32_array_create_from_raw, "p(p,i)");
	R(i32_array_create, "p(i)");
	R(i32_array_create_from_array, "p(p)");
	R(i32_array_create_from_raw, "p(p,i)");
	R(u16_array_create, "p(i)");
	R(u16_array_create_from_raw, "p(p,i)");
	R(i16_array_create, "p(i)");
	R(i16_array_create_from_array, "p(p)");
	R(i16_array_create_from_raw, "p(p,i)");
	R(u8_array_create, "p(i)");
	R(u8_array_create_from_array, "p(p)");
	R(u8_array_create_from_raw, "p(p,i)");
	R(u8_array_create_from_string, "p(p)");
	R(u8_array_to_string, "p(p)");
	R(i8_array_create, "p(i)");
	R(i8_array_create_from_raw, "p(p,i)");
	R(any_array_create, "p(i)");
	R(any_array_create_from_raw, "p(p,i)");
	R(string_array_create, "p(i)");
	R(float_to_half_fast, "i(f)");
	R(half_to_u8_fast, "i(i)");

	// iron_input
	minic_register_global("mouse_x", &mouse_x, MINIC_T_FLOAT);
	minic_register_global("mouse_y", &mouse_y, MINIC_T_FLOAT);
	R(mouse_down, "b(p)");
	R(mouse_down_any, "b()");
	R(mouse_started, "b(p)");
	R(mouse_started_any, "b()");
	R(mouse_released, "b(p)");
	R(mouse_view_x, "f()");
	R(mouse_view_y, "f()");
	R(keyboard_down, "b(p)");
	R(keyboard_started, "b(p)");
	R(keyboard_started_any, "b()");
	R(keyboard_released, "b(p)");
	R(keyboard_repeat, "b(p)");
	R(keyboard_key_code, "p(i)");

	// paint
	R(plugin_create, "p()");
	R(plugin_notify_on_ui, "v(p,p)");
	R(plugin_notify_on_update, "v(p,p)");
	R(plugin_notify_on_delete, "v(p,p)");
	R(script_notify_on_update, "v(p)");
	R(script_notify_on_next_frame, "v(p)");
	R(console_info, "v(p)");
	R(console_error, "v(p)");
	R(console_log, "v(p)");
	R(ui_box_show_message, "v(p,p,i)");
	R(ui_files_show2, "v(p,i,i,p)");
	R(project_save, "v(i)");
	R(project_filepath_get, "p()");
	R(project_basepath_get, "p()");
	R(project_filepath_set, "v(p)");
	R(script_get_context, "p()");
	R(script_get_config, "p()");
	R(script_get_project, "p()");
	R(script_get_object, "p(p)");
	R(script_set_stage, "v(p)");
	R(script_fade_to_stage, "v(p)");
	R(script_timer, "v(f,p)");
	R(script_get_stage, "p()");
	R(script_set_tilesheet_anim, "v(p,p)");
	R(script_draw_particles, "v(p,f,f,f,f,i,i)");
	R(context_set_viewport_shader, "v(p)");
	R(context_set_viewport_mode, "v(i)");
	R(context_set_camera_controls, "v(i)");
	R(node_shader_write_frag, "v(p,p)");
	R(plugin_register_texture, "v(p,p)");
	R(plugin_unregister_texture, "v(p)");
	R(plugin_register_mesh, "v(p,p)");
	R(plugin_unregister_mesh, "v(p)");
	R(plugin_register_text, "v(p,p)");
	R(plugin_unregister_text, "v(p)");
	R(plugin_make_raw_mesh, "p(p,p,p,p,f)");
	R(plugin_material_category_add, "v(p,p)");
	R(plugin_brush_category_add, "v(p,p)");
	R(plugin_material_category_remove, "v(p)");
	R(plugin_brush_category_remove, "v(p)");
	R(plugin_material_custom_nodes_set, "v(p,p)");
	R(plugin_brush_custom_nodes_set, "v(p,p)");
	R(plugin_material_custom_nodes_remove, "v(p)");
	R(plugin_brush_custom_nodes_remove, "v(p)");
	R(plugin_material_kong_get, "p()");
	R(parser_material_parse_value_input, "p(p,i)");
	R(context_main_object, "p()");
	R(export_texture_run, "v(p,i)");
	R(context_select_tool, "v(i)");
	R(gpu_create_render_target, "p(i,i,i)");
	R(viewport_capture_screenshot_to, "v(p,f,f,f,f)");
	R(viewport_save_texture, "v(p)");
	R(project_reimport_mesh_skinned, "v(i)");
	R(iron_delay_idle_sleep, "v()");

	// json
	R(json_parse, "p(p)");
	R(json_parse_to_map, "p(p)");
	R(json_encode_begin, "v()");
	R(json_encode_end, "p()");
	R(json_encode_string, "v(p,p)");
	R(json_encode_string_array, "v(p,p)");
	R(json_encode_f32, "v(p,f)");
	R(json_encode_i32, "v(p,i)");
	R(json_encode_null, "v(p)");
	R(json_encode_f32_array, "v(p,p)");
	R(json_encode_i32_array, "v(p,p)");
	R(json_encode_bool, "v(p,i)");
	R(json_encode_begin_array, "v(p)");
	R(json_encode_end_array, "v()");
	R(json_encode_begin_object, "v()");
	R(json_encode_end_object, "v()");
	R(json_encode_map, "v(p)");
	R(json_encode_to_armpack, "p(p)");

	// armpack
	R(armpack_decode, "p(p)");
	R(armpack_decode_to_map, "p(p)");
	R(armpack_decode_to_json, "p(p)");
	R(armpack_encode_start, "v(p)");
	R(armpack_encode_end, "i()");
	R(armpack_encode_map, "v(i)");
	R(armpack_encode_array, "v(i)");
	R(armpack_encode_array_f32, "v(p)");
	R(armpack_encode_array_i32, "v(p)");
	R(armpack_encode_array_i16, "v(p)");
	R(armpack_encode_array_u8, "v(p)");
	R(armpack_encode_array_string, "v(p)");
	R(armpack_encode_string, "v(p)");
	R(armpack_encode_i32, "v(i)");
	R(armpack_encode_f32, "v(f)");
	R(armpack_encode_bool, "v(i)");
	R(armpack_encode_null, "v()");
	R(armpack_size_map, "i()");
	R(armpack_size_array, "i()");
	R(armpack_size_array_f32, "i(p)");
	R(armpack_size_array_u8, "i(p)");
	R(armpack_size_string, "i(p)");
	R(armpack_size_i32, "i()");
	R(armpack_size_f32, "i()");
	R(armpack_size_bool, "i()");
	R(armpack_map_get_f32, "f(p,p)");
	R(armpack_map_get_i32, "i(p,p)");
}

#undef R
