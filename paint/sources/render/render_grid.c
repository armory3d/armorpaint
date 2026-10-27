
#include "../global.h"

// Lines are drawn in tiers, each tier has 10x the spacing of the previous one
#define GRID_TIERS      3
#define GRID_HALF_LINES 60 // Lines drawn to each side of the camera
#define GRID_MAX_LINES  (GRID_TIERS * 2 * (GRID_HALF_LINES * 2 + 1))
#define GRID_VERTEX_LEN 8 // pos.xyz, fade distance, color

static gpu_pipeline_t *_grid_pipeline = NULL;
static gpu_buffer_t   *_grid_vb       = NULL;
static gpu_buffer_t   *_grid_ib       = NULL;
static i32             _grid_vp_loc;
static i32             _grid_center_loc;
static f32            *_grid_vb_data;
static i32             _grid_lines;
static vec4_t          _grid_eye;
static vec4_t          _grid_look;
static bool            _grid_ortho;
static f32             _grid_near;
static f32             _grid_width; // Half of the line width in world units, at depth 1 for the perspective camera

static void _grid_init() {
	if (_grid_pipeline != NULL) {
		return;
	}
	gpu_vertex_structure_t *vs = ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_4X);
	gpu_vertex_structure_add(vs, "col", GPU_VERTEX_DATA_F32_4X);
	_grid_pipeline                          = gpu_create_pipeline();
	_grid_pipeline->input_layout            = vs;
	_grid_pipeline->vertex_shader           = sys_get_shader("grid.vert");
	_grid_pipeline->fragment_shader         = sys_get_shader("grid.frag");
	_grid_pipeline->depth_write             = false;
	_grid_pipeline->depth_mode              = GPU_COMPARE_MODE_LESS;
	_grid_pipeline->cull_mode               = GPU_CULL_MODE_NONE;
	_grid_pipeline->blend_source            = GPU_BLEND_SOURCE_ALPHA;
	_grid_pipeline->blend_destination       = GPU_BLEND_INV_SOURCE_ALPHA;
	_grid_pipeline->alpha_blend_source      = GPU_BLEND_ZERO;
	_grid_pipeline->alpha_blend_destination = GPU_BLEND_ONE;
	_grid_pipeline->color_attachment_count  = 1;
	_grid_pipeline->color_attachment[0]     = GPU_TEXTURE_FORMAT_RGBA64;
	_grid_pipeline->depth_attachment_bits   = 32;
	gpu_pipeline_compile(_grid_pipeline);
	pipes_offset     = 0;
	_grid_vp_loc     = pipes_get_constant_location("float4x4");
	_grid_center_loc = pipes_get_constant_location("float2");
	_grid_vb         = gpu_create_vertex_buffer(GRID_MAX_LINES * 4, vs);
	_grid_ib         = gpu_create_index_buffer(GRID_MAX_LINES * 6);
}

static void _grid_vertex(vec4_t p, f32 fade, u32 color, f32 alpha) {
	*_grid_vb_data++ = p.x;
	*_grid_vb_data++ = p.y;
	*_grid_vb_data++ = p.z;
	*_grid_vb_data++ = fade;
	*_grid_vb_data++ = color_get_rb(color) / 255.0;
	*_grid_vb_data++ = color_get_gb(color) / 255.0;
	*_grid_vb_data++ = color_get_bb(color) / 255.0;
	*_grid_vb_data++ = alpha;
}

// Camera facing quad of a constant width on screen
static void _grid_line(vec4_t a, vec4_t b, f32 fade, u32 color, f32 alpha) {
	vec4_t dir = vec4_sub(b, a);
	vec4_t n;
	f32    wa = _grid_width;
	f32    wb = _grid_width;
	if (_grid_ortho) {
		n = vec4_cross(_grid_look, dir);
	}
	else {
		f32 za = vec4_dot(vec4_sub(a, _grid_eye), _grid_look);
		f32 zb = vec4_dot(vec4_sub(b, _grid_eye), _grid_look);
		if (za < _grid_near && zb < _grid_near) {
			return;
		}
		// Width grows with the depth, keep both ends in front of the camera
		if (za < _grid_near) {
			a  = vec4_add(a, vec4_mult(dir, (_grid_near - za) / (zb - za)));
			za = _grid_near;
		}
		else if (zb < _grid_near) {
			b  = vec4_add(a, vec4_mult(dir, (_grid_near - za) / (zb - za)));
			zb = _grid_near;
		}
		wa *= za;
		wb *= zb;
		n = vec4_cross(vec4_sub(a, _grid_eye), dir);
	}
	if (vec4_len(n) == 0.0) {
		return;
	}
	n = vec4_norm(n);

	_grid_vertex(vec4_add(a, vec4_mult(n, wa)), fade, color, alpha);
	_grid_vertex(vec4_sub(a, vec4_mult(n, wa)), fade, color, alpha);
	_grid_vertex(vec4_sub(b, vec4_mult(n, wb)), fade, color, alpha);
	_grid_vertex(vec4_add(b, vec4_mult(n, wb)), fade, color, alpha);
	_grid_lines++;
}

void render_grid() {
	if (!g_context->show_grid || base_player || g_context->capturing_screenshot) {
		return;
	}

	_grid_init();

	camera_object_t *cam = scene_camera;
	_grid_eye            = mat4_get_loc(cam->base->transform->world);
	_grid_look           = camera_object_look_world(cam);
	_grid_ortho          = cam->p.m[15] != 0.0;
	_grid_near           = cam->data->near_plane;
	_grid_width          = 0.75 * UI_SCALE() * 2.0 / (cam->p.m[5] * sys_h());

	// Spacing follows the camera distance, the finest tier fades out while zooming out
	f32 dist    = math_max(math_max(camera_distance(), math_abs(_grid_eye.z)), 0.0001);
	f32 level   = math_log(dist) / math_log(10.0) - 0.5;
	f32 frac    = level - math_floor(level);
	f32 spacing = math_pow(10.0, math_floor(level));
	// Fade out in front of the far plane
	f32 max_fade = cam->data->far_plane * 0.95;

	_grid_lines   = 0;
	_grid_vb_data = gpu_vertex_buffer_lock(_grid_vb);

	for (i32 tier = 0; tier < GRID_TIERS; ++tier) {
		f32 fade = math_min(spacing * GRID_HALF_LINES, max_fade);
		// A tier takes over the look of the finer one when the spacing changes
		f32 u     = tier + 1.0 - frac;
		f32 alpha = math_min(u, 1.0) * (0.2 + 0.2 * math_min(math_max(u - 1.0, 0.0), 1.0));

		// Lines along x, then along y
		for (i32 axis = 0; axis < 2; ++axis) {
			f32 eye_along  = axis == 0 ? _grid_eye.x : _grid_eye.y;
			f32 eye_across = axis == 0 ? _grid_eye.y : _grid_eye.x;
			i32 center     = math_round(eye_across / spacing);
			for (i32 i = -GRID_HALF_LINES; i <= GRID_HALF_LINES; ++i) {
				i32 index = center + i;
				if (tier < GRID_TIERS - 1 && index % 10 == 0) {
					continue; // Drawn by the next tier
				}
				f32 across = index * spacing;
				f32 offset = across - eye_across;
				if (math_abs(offset) >= fade) {
					continue;
				}
				f32    len = math_sqrt(fade * fade - offset * offset);
				vec4_t a   = axis == 0 ? (vec4_t){eye_along - len, across, 0.0, 1.0} : (vec4_t){across, eye_along - len, 0.0, 1.0};
				vec4_t b   = axis == 0 ? (vec4_t){eye_along + len, across, 0.0, 1.0} : (vec4_t){across, eye_along + len, 0.0, 1.0};
				if (index == 0) {
					_grid_line(a, b, fade, axis == 0 ? 0xffe0404a : 0xff60b030, 0.8); // World axis
				}
				else {
					_grid_line(a, b, fade, 0xff999999, alpha);
				}
			}
		}
		spacing *= 10.0;
	}

	gpu_vertex_buffer_unlock(_grid_vb);

	u32 *ib_data = gpu_index_buffer_lock(_grid_ib);
	for (i32 i = 0; i < GRID_MAX_LINES; ++i) {
		// Unused lines are degenerate
		i32 v              = i < _grid_lines ? i * 4 : 0;
		i32 o              = i < _grid_lines ? 1 : 0;
		ib_data[i * 6]     = v;
		ib_data[i * 6 + 1] = v + o;
		ib_data[i * 6 + 2] = v + o * 2;
		ib_data[i * 6 + 3] = v + o * 2;
		ib_data[i * 6 + 4] = v + o * 3;
		ib_data[i * 6 + 5] = v;
	}
	gpu_index_buffer_unlock(_grid_ib);

	gpu_set_pipeline(_grid_pipeline);
	gpu_set_mat4(_grid_vp_loc, cam->vp);
	gpu_set_float2(_grid_center_loc, _grid_eye.x, _grid_eye.y);
	gpu_set_vertex_buffer(_grid_vb);
	gpu_set_index_buffer(_grid_ib);
	gpu_draw();
}
