#include "../global.h"

typedef struct {
	f32 cx;
	f32 cy;
	f32 w;
	f32 h;
	f32 cosa;
	f32 sina;
} rotated_rect_t;

static rotated_rect_t util_stencil_rotated_rect(f32 cx, f32 cy, f32 w, f32 h, f32 angle) {
	rotated_rect_t r;
	r.cx   = cx;
	r.cy   = cy;
	r.w    = w;
	r.h    = h;
	r.cosa = math_cos(-angle);
	r.sina = math_sin(-angle);
	return r;
}

static void util_stencil_to_screen(rotated_rect_t *r, f32 lx, f32 ly, f32 *x, f32 *y) {
	*x = r->cx + lx * r->cosa - ly * r->sina;
	*y = r->cy + lx * r->sina + ly * r->cosa;
}

static void util_stencil_to_local(rotated_rect_t *r, f32 x, f32 y, f32 *lx, f32 *ly) {
	f32 dx = x - r->cx;
	f32 dy = y - r->cy;
	*lx    = dx * r->cosa + dy * r->sina;
	*ly    = -dx * r->sina + dy * r->cosa;
}

static bool util_stencil_hit(f32 lx, f32 ly, f32 hx, f32 hy, f32 radius) {
	return math_abs(lx - hx) < radius && math_abs(ly - hy) < radius;
}

static bool util_stencil_hit_corner(rotated_rect_t *r, f32 lx, f32 ly) {
	return util_stencil_hit(lx, ly, -r->w / 2.0, -r->h / 2.0, 8) || util_stencil_hit(lx, ly, r->w / 2.0, -r->h / 2.0, 8) ||
	       util_stencil_hit(lx, ly, -r->w / 2.0, r->h / 2.0, 8) || util_stencil_hit(lx, ly, r->w / 2.0, r->h / 2.0, 8);
}

static void util_stencil_draw_image(gpu_texture_t *img, rotated_rect_t *r) {
	mat3_t m = {0};
	m.m00    = r->cosa;
	m.m10    = -r->sina * r->h / r->w;
	m.m01    = r->sina * r->w / r->h;
	m.m11    = r->cosa;
	m.m20    = 0.5 - 0.5 * (m.m00 + m.m10);
	m.m21    = 0.5 - 0.5 * (m.m01 + m.m11);
	m.m22    = 1.0;
	draw_set_transform(m);
	draw_scaled_image(img, r->cx - r->w / 2.0, r->cy - r->h / 2.0, r->w, r->h);
	draw_set_transform(mat3_nan());
}

static void util_stencil_draw_handles(rotated_rect_t *r) {
	f32 corners[8];
	f32 signs[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
	for (i32 i = 0; i < 4; ++i) {
		util_stencil_to_screen(r, signs[i * 2] * r->w / 2.0, signs[i * 2 + 1] * r->h / 2.0, &corners[i * 2], &corners[i * 2 + 1]);
	}
	// Outline
	for (i32 i = 0; i < 4; ++i) {
		i32 j = (i + 1) % 4;
		draw_line(corners[i * 2], corners[i * 2 + 1], corners[j * 2], corners[j * 2 + 1], 1.0);
	}
	// Scale
	for (i32 i = 0; i < 4; ++i) {
		draw_rect(corners[i * 2] - 8, corners[i * 2 + 1] - 8, 16, 16, 1.0);
	}
	// Rotate
	f32 x;
	f32 y;
	util_stencil_to_screen(r, 0, -r->h / 2.0, &x, &y);
	draw_filled_circle(x, y, 8, 0);
}

static rotated_rect_t util_stencil_brush_rect() {
	rect_t *s = ui_base_get_brush_stencil_rect();
	return util_stencil_rotated_rect(s->x + s->w / 2.0, s->y + s->h / 2.0, s->w, s->h, g_context->brush_stencil_angle);
}

void util_stencil_transform() {
	if (util_stencil_view_image_transform()) {
		return;
	}
	if (g_context->brush_stencil_image != NULL && keymap_shortcut(any_map_get(g_keymap, "stencil_transform"), SHORTCUT_TYPE_DOWN)) {
		rotated_rect_t r = util_stencil_brush_rect();
		if (mouse_started("left")) {
			f32 lx;
			f32 ly;
			util_stencil_to_local(&r, mouse_x, mouse_y, &lx, &ly);
			g_context->brush_stencil_scaling  = util_stencil_hit_corner(&r, lx, ly);
			g_context->brush_stencil_rotating = util_stencil_hit(lx, ly, 0, -r.h / 2.0, 16);
		}
		f32 _scale = g_context->brush_stencil_scale;
		if (mouse_down("left")) {
			if (g_context->brush_stencil_scaling) {
				i32 mult = mouse_x > r.cx ? 1 : -1;
				g_context->brush_stencil_scale += mouse_movement_x / 400.0 * mult;
			}
			else if (g_context->brush_stencil_rotating) {
				g_context->brush_stencil_angle = -math_atan2(mouse_y - r.cy, mouse_x - r.cx) - math_pi() / 2.0;
			}
			else {
				g_context->brush_stencil_x += mouse_movement_x / (float)base_w();
				g_context->brush_stencil_y += mouse_movement_y / (float)base_h();
			}
		}
		else {
			g_context->brush_stencil_scaling = false;
		}
		if (mouse_wheel_delta != 0) {
			g_context->brush_stencil_scale -= mouse_wheel_delta / 10.0;
		}
		// Center after scale
		f32 ratio = base_h() / (float)g_context->brush_stencil_image->height;
		f32 old_w = _scale * g_context->brush_stencil_image->width * ratio;
		f32 new_w = g_context->brush_stencil_scale * g_context->brush_stencil_image->width * ratio;
		f32 old_h = _scale * base_h();
		f32 new_h = g_context->brush_stencil_scale * base_h();
		g_context->brush_stencil_x += (old_w - new_w) / (float)base_w() / 2.0;
		g_context->brush_stencil_y += (old_h - new_h) / (float)base_h() / 2.0;
	}
}

void util_stencil_render() {
	rotated_rect_t r = util_stencil_brush_rect();
	if (!keymap_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN)) {
		draw_set_color(0x88ffffff);
		util_stencil_draw_image(g_context->brush_stencil_image, &r);
		draw_set_color(0xffffffff);
	}
	if (base_ui_enabled && keymap_shortcut(any_map_get(g_keymap, "stencil_transform"), SHORTCUT_TYPE_DOWN)) {
		util_stencil_draw_handles(&r);
	}
}

typedef enum {
	VIEW_IMAGE_ACTION_NONE,
	VIEW_IMAGE_ACTION_MOVE,
	VIEW_IMAGE_ACTION_SCALE,
	VIEW_IMAGE_ACTION_ROTATE,
} view_image_action_t;

static f32 view_image_start_dist;
static f32 view_image_start_scale;

static rotated_rect_t util_stencil_view_image_rect(gpu_texture_t *img) {
	f32 vh = base_h();
	f32 h  = g_context->view_image_scale * vh;
	f32 w  = img->width * (h / (f32)img->height);
	return util_stencil_rotated_rect(base_view_x(0) + g_context->view_image_x * base_view_w(), base_appy + g_context->view_image_y * vh, w, h,
	                                 g_context->view_image_angle);
}

gpu_texture_t *util_stencil_view_image() {
	asset_t *asset = g_context->view_image;
	if (asset == NULL) {
		return NULL;
	}
	if (array_index_of(g_project->_->assets, asset) == -1) {
		g_context->view_image = NULL;
		return NULL;
	}
	return project_get_image(asset);
}

void util_stencil_view_image_open(asset_t *asset, f32 x, f32 y) {
	g_context->view_image         = asset;
	g_context->view_image_x       = x;
	g_context->view_image_y       = y;
	g_context->view_image_scale   = 0.5;
	g_context->view_image_angle   = 0.0;
	g_context->view_image_opacity = 0.5;
	g_context->view_image_action  = VIEW_IMAGE_ACTION_NONE;
}

void util_stencil_view_image_close() {
	g_context->view_image        = NULL;
	g_context->view_image_action = VIEW_IMAGE_ACTION_NONE;
}

bool util_stencil_view_image_transform() {
	gpu_texture_t *img = util_stencil_view_image();
	if (img == NULL || !base_view3d_show) {
		return false;
	}
	if (!keymap_shortcut(any_map_get(g_keymap, "stencil_transform"), SHORTCUT_TYPE_DOWN) || !mouse_down("left")) {
		g_context->view_image_action = VIEW_IMAGE_ACTION_NONE;
	}
	if (!keymap_shortcut(any_map_get(g_keymap, "stencil_transform"), SHORTCUT_TYPE_DOWN)) {
		return false;
	}

	rotated_rect_t r = util_stencil_view_image_rect(img);
	f32            lx;
	f32            ly;
	util_stencil_to_local(&r, mouse_x, mouse_y, &lx, &ly);
	bool over = math_abs(lx) < r.w / 2.0 + 8 && math_abs(ly) < r.h / 2.0 + 8;

	if (mouse_started("left") && context_in_3d_view()) {
		if (util_stencil_hit(lx, ly, 0, r.h / 2.0, 10)) {
			util_stencil_view_image_close();
			return true;
		}
		if (util_stencil_hit(lx, ly, -r.w / 2.0, -r.h / 2.0, 8) || util_stencil_hit(lx, ly, r.w / 2.0, -r.h / 2.0, 8) ||
		    util_stencil_hit(lx, ly, -r.w / 2.0, r.h / 2.0, 8) || util_stencil_hit(lx, ly, r.w / 2.0, r.h / 2.0, 8)) {
			g_context->view_image_action = VIEW_IMAGE_ACTION_SCALE;
			view_image_start_dist        = math_max(math_sqrt(lx * lx + ly * ly), 1.0);
			view_image_start_scale       = g_context->view_image_scale;
		}
		else if (util_stencil_hit(lx, ly, 0, -r.h / 2.0, 16)) {
			g_context->view_image_action = VIEW_IMAGE_ACTION_ROTATE;
		}
		else if (over) {
			g_context->view_image_action = VIEW_IMAGE_ACTION_MOVE;
		}
	}

	if (g_context->view_image_action == VIEW_IMAGE_ACTION_SCALE) {
		f32 dist                    = math_sqrt(lx * lx + ly * ly);
		g_context->view_image_scale = math_max(view_image_start_scale * dist / view_image_start_dist, 0.02);
	}
	else if (g_context->view_image_action == VIEW_IMAGE_ACTION_ROTATE) {
		g_context->view_image_angle = -math_atan2(mouse_y - r.cy, mouse_x - r.cx) - math_pi() / 2.0;
	}
	else if (g_context->view_image_action == VIEW_IMAGE_ACTION_MOVE) {
		g_context->view_image_x += mouse_movement_x / (f32)base_view_w();
		g_context->view_image_y += mouse_movement_y / (f32)base_h();
	}

	if (mouse_wheel_delta != 0 && over && context_in_3d_view()) {
		g_context->view_image_opacity = math_max(math_min(g_context->view_image_opacity - mouse_wheel_delta / 10.0, 1.0), 0.1);
		return true;
	}

	return g_context->view_image_action != VIEW_IMAGE_ACTION_NONE;
}

void util_stencil_view_image_render() {
	gpu_texture_t *img = util_stencil_view_image();
	if (img == NULL || !base_view3d_show) {
		return;
	}
	rotated_rect_t r = util_stencil_view_image_rect(img);
	draw_scissor(base_view_x(0), base_appy, base_view_w(), base_h());
	if (!keymap_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN)) {
		draw_set_color(color_from_floats(1, 1, 1, g_context->view_image_opacity));
		util_stencil_draw_image(img, &r);
		draw_set_color(0xffffffff);
	}
	if (base_ui_enabled && keymap_shortcut(any_map_get(g_keymap, "stencil_transform"), SHORTCUT_TYPE_DOWN)) {
		util_stencil_draw_handles(&r);
		f32 x;
		f32 y;
		util_stencil_to_screen(&r, 0, r.h / 2.0, &x, &y);
		draw_set_color(0xff000000);
		draw_filled_circle(x, y, 10, 0);
		draw_set_color(0xffffffff);
		draw_line(x - 4, y - 4, x + 4, y + 4, 2.0);
		draw_line(x + 4, y - 4, x - 4, y + 4, 2.0);
	}
	draw_flush();
	gpu_disable_scissor();
}
