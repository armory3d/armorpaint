
#include "../global.h"

typedef enum {
	SHAPE_TOOL_STATE_IDLE   = 0,
	SHAPE_TOOL_STATE_BASE   = 1,
	SHAPE_TOOL_STATE_HEIGHT = 2,
} shape_tool_state_t;

#define SHAPE_TOOL_MIN_SIZE     0.0001
#define SHAPE_TOOL_MAX_SEGMENTS 1024
#define TOOL_GROUND_MIN_LOOK_Z  0.2 // Below this the ground is too close to edge-on to pick points on

static shape_tool_state_t _shape_tool_state = SHAPE_TOOL_STATE_IDLE;
static vec4_t             _shape_tool_p0; // Base corners on the ground plane
static vec4_t             _shape_tool_p1;
static f32                _shape_tool_height;
static f32                _shape_tool_height_my; // Mouse position the height drag started at
static i32                _shape_tool_type = -1; // Tool the placement was started with
// From TOOL_TYPE_EMPTY to TOOL_TYPE_TORUS, see util_shape_make_data
static char *_shape_tool_names[] = {"empty", "plane", "cube", "sphere", "cone", "cylinder", "capsule", "torus"};
// Meshes - New and the project templates do not follow the header, same as the initial util_shape_tool_segments
static i32 _shape_tool_default_segments[] = {0, 1, 0, 128, 64, 64, 32, 64};

bool util_tool_is_object_tool(i32 tool) {
	return tool >= TOOL_TYPE_EMPTY && tool <= TOOL_TYPE_CURVE;
}

bool util_shape_tool_has_segments(i32 tool) {
	return tool >= TOOL_TYPE_PLANE && tool <= TOOL_TYPE_TORUS && tool != TOOL_TYPE_CUBE;
}

// Resolution set in the header
static i32 _shape_tool_segments(i32 tool) {
	i32 segments = math_floor(util_shape_tool_segments[tool - TOOL_TYPE_EMPTY]);
	i32 min      = tool == TOOL_TYPE_PLANE ? 1 : 3;
	return segments < min ? min : segments > SHAPE_TOOL_MAX_SEGMENTS ? SHAPE_TOOL_MAX_SEGMENTS : segments;
}

static bool _shape_tool_active() {
	return g_context->tool >= TOOL_TYPE_EMPTY && g_context->tool <= TOOL_TYPE_TORUS;
}

static bool _shape_tool_mouse_ray(vec4_t *origin, vec4_t *dir) {
	ray_t *ray = raycast_get_ray(mouse_view_x(), mouse_view_y(), scene_camera);
	if (ray == NULL) {
		return false;
	}
	*origin = ray->origin;
	*dir    = vec4_norm(ray->dir);
	free(ray);
	return true;
}

// Mouse position on the ground plane
bool util_tool_ground_point(vec4_t *p) {
	vec4_t origin;
	vec4_t dir;
	if (!_shape_tool_mouse_ray(&origin, &dir) || math_abs(dir.z) < 0.000001) {
		return false;
	}
	f32 t = -origin.z / dir.z;
	if (t <= 0.0 || t > scene_camera->data->far_plane) {
		return false;
	}
	*p = (vec4_t){origin.x + dir.x * t, origin.y + dir.y * t, 0.0, 1.0};
	return true;
}

// Mouse position on the ground plane, or on the upright plane through the camera pivot when the ground is seen from the side
bool util_tool_plane_point(vec4_t *p) {
	vec4_t look = camera_object_look_world(scene_camera);
	if (math_abs(look.z) >= TOOL_GROUND_MIN_LOOK_Z) {
		return util_tool_ground_point(p);
	}
	vec4_t origin;
	vec4_t dir;
	if (!_shape_tool_mouse_ray(&origin, &dir)) {
		return false;
	}
	vec4_t n     = vec4_norm((vec4_t){look.x, look.y, 0.0, 1.0});
	f32    denom = vec4_dot(dir, n);
	if (denom < 0.000001) {
		return false;
	}
	f32 t = vec4_dot(vec4_sub(camera_origins[camera_index()], origin), n) / denom;
	if (t <= 0.0 || t > scene_camera->data->far_plane) {
		return false;
	}
	*p = (vec4_t){origin.x + dir.x * t, origin.y + dir.y * t, origin.z + dir.z * t, 1.0};
	return true;
}

// Mouse position on the level or upright plane going through a point, for dragging it
bool util_tool_plane_point_at(vec4_t through, vec4_t *p) {
	vec4_t origin;
	vec4_t dir;
	if (!_shape_tool_mouse_ray(&origin, &dir)) {
		return false;
	}
	vec4_t look  = camera_object_look_world(scene_camera);
	vec4_t n     = math_abs(look.z) >= TOOL_GROUND_MIN_LOOK_Z ? (vec4_t){0.0, 0.0, 1.0, 1.0} : vec4_norm((vec4_t){look.x, look.y, 0.0, 1.0});
	f32    denom = vec4_dot(dir, n);
	if (math_abs(denom) < 0.000001) {
		return false;
	}
	f32 t = vec4_dot(vec4_sub(through, origin), n) / denom;
	if (t <= 0.0 || t > scene_camera->data->far_plane) {
		return false;
	}
	*p = (vec4_t){origin.x + dir.x * t, origin.y + dir.y * t, origin.z + dir.z * t, 1.0};
	return true;
}

// Mouse position along the vertical line going through the confirmed base corner
static f32 _shape_tool_height_at_mouse() {
	vec4_t origin;
	vec4_t dir;
	if (!_shape_tool_mouse_ray(&origin, &dir)) {
		return _shape_tool_height;
	}
	f32 b     = dir.z;
	f32 denom = 1.0 - b * b;
	if (denom < 0.001) {
		// Looking straight down, use the mouse travel instead
		camera_object_t *cam   = scene_camera;
		bool             ortho = cam->p.m[15] != 0.0;
		f32              depth = ortho ? 1.0 : vec4_dist(mat4_get_loc(cam->base->transform->world), _shape_tool_p1);
		return (_shape_tool_height_my - mouse_view_y()) * 2.0 * depth / (cam->p.m[5] * sys_h());
	}
	vec4_t w0 = vec4_sub(_shape_tool_p1, origin);
	return (b * vec4_dot(dir, w0) - w0.z) / denom;
}

bool util_tool_input_enabled() {
	if (!base_ui_enabled || base_player || g_context->paint2d || !context_in_3d_view()) {
		return false;
	}
	if (g_ui->is_hovered || g_ui->is_scrolling || g_ui->combo_selected_id != 0 || ui_menu_show || ui_box_show) {
		return false;
	}
	if (base_is_dragging || base_is_resizing || ui_dock_is_dragging()) {
		return false;
	}
	if (context_is_floating_toolbar() && mouse_view_x() < ui_toolbar_x() + ui_toolbar_w(false)) {
		return false;
	}
	// Camera controls
	if (keyboard_down("alt")) {
		return false;
	}
	return true;
}

static void _shape_tool_capsule_vertex(mesh_geo_t *g, i32 i, f32 x, f32 y, f32 z, vec4_t nor, f32 u, f32 v) {
	g->pos[i * 3]     = x;
	g->pos[i * 3 + 1] = y;
	g->pos[i * 3 + 2] = z;
	g->nor[i * 3]     = nor.x;
	g->nor[i * 3 + 1] = nor.y;
	g->nor[i * 3 + 2] = nor.z;
	g->tex[i * 2]     = u;
	g->tex[i * 2 + 1] = v;
}

// Upright capsule centered at the origin, the caps stay round where the box allows it
static mesh_geo_t *_shape_tool_make_capsule(f32 size_x, f32 size_y, f32 size_z, i32 sides) {
	i32         rings = math_max(sides / 4, 1); // Per cap
	i32         rows  = (rings + 1) * 2;
	f32         rx    = size_x / 2.0;
	f32         ry    = size_y / 2.0;
	f32         cap   = math_min(math_min(rx, ry), size_z / 2.0);
	f32         mid   = size_z / 2.0 - cap; // Half height of the straight part
	f32         arc   = cap * math_pi() / 2.0;
	mesh_geo_t *g     = util_geo_create(rows * (sides + 1), (rows - 1) * sides * 6);
	i32         vi    = 0;
	i32         ii    = 0;
	for (i32 r = 0; r < rows; ++r) {
		bool top = r > rings;
		f32  phi = ((top ? r - 1 : r) / (f32)rings - 1.0) * math_pi() / 2.0;
		f32  cr  = math_cos(phi);
		f32  sr  = math_sin(phi);
		f32  z   = (top ? mid : -mid) + sr * cap;
		f32  v   = ((phi + math_pi() / 2.0) / (math_pi() / 2.0) * arc + (top ? mid * 2.0 : 0.0)) / (arc * 2.0 + mid * 2.0);
		for (i32 k = 0; k <= sides; ++k) {
			f32    a   = k / (f32)sides * math_pi() * 2.0;
			vec4_t nor = vec4_norm((vec4_t){math_cos(a) * cr / rx, math_sin(a) * cr / ry, sr / cap, 1.0});
			_shape_tool_capsule_vertex(g, vi++, math_cos(a) * cr * rx, math_sin(a) * cr * ry, z, nor, k / (f32)sides, v);
		}
	}
	for (i32 r = 0; r < rows - 1; ++r) {
		if (r == rings && mid < SHAPE_TOOL_MIN_SIZE) {
			continue; // No straight part
		}
		for (i32 k = 0; k < sides; ++k) {
			i32 v00      = r * (sides + 1) + k;
			i32 v01      = v00 + sides + 1;
			g->ind[ii++] = v00;
			g->ind[ii++] = v00 + 1;
			g->ind[ii++] = v01 + 1;
			g->ind[ii++] = v00;
			g->ind[ii++] = v01 + 1;
			g->ind[ii++] = v01;
		}
	}
	g->vertex_count = vi;
	g->index_count  = ii;
	return g;
}

static mesh_data_t *_shape_tool_geo_data(mesh_geo_t *g, char *name) {
	mesh_data_t *md = nodes_mesh_geo_to_data(g, name);
	util_geo_free(g);
	return md;
}

// Shape of an object tool
static mesh_data_t *_shape_tool_make_data(i32 tool, i32 segments) {
	if (tool == TOOL_TYPE_PLANE) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_plane(1, 1, segments + 1, segments + 1, 1.0)));
	}
	if (tool == TOOL_TYPE_CUBE) {
		return _shape_tool_geo_data(nodes_mesh_shape(0, (f32[]){2.0, 2.0, 2.0}, 0), "Cube");
	}
	if (tool == TOOL_TYPE_SPHERE) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_uv_sphere(1, segments, math_max(segments / 2, 2), true, 1.0)));
	}
	if (tool == TOOL_TYPE_CONE) {
		return _shape_tool_geo_data(nodes_mesh_shape(3, (f32[]){1.0, 1.0, 1.0}, segments), "Cone");
	}
	if (tool == TOOL_TYPE_CYLINDER) {
		return _shape_tool_geo_data(nodes_mesh_shape(2, (f32[]){1.0, 1.0, 1.0}, segments), "Cylinder");
	}
	if (tool == TOOL_TYPE_CAPSULE) {
		return _shape_tool_geo_data(_shape_tool_make_capsule(1.0, 1.0, 2.0, segments), "Capsule");
	}
	if (tool == TOOL_TYPE_TORUS) {
		return _shape_tool_geo_data(nodes_mesh_shape(4, (f32[]){2.5, 2.5, 2.0}, segments), "Torus");
	}
	return NULL;
}

// Procedural shapes of the object tools, Meshes - New and the project templates, NULL when the name is not one of them
mesh_data_t *util_shape_make_data(char *name) {
	if (string_equals(name, "empty")) {
		// No geometry, the object only carries a transform
		raw_mesh_t  *mesh  = ALLOC_INIT(raw_mesh_t, {.name      = "Empty",
		                                             .posa      = i16_array_create(0),
		                                             .nora      = i16_array_create(0),
		                                             .texa      = i16_array_create(0),
		                                             .inda      = u32_array_create(0),
		                                             .scale_pos = 1.0,
		                                             .scale_tex = 1.0});
		mesh_data_t *md    = mesh_data_create(import_mesh_raw_mesh(mesh));
		md->_->owns_arrays = true;
		return md;
	}
	if (string_equals(name, "plane_2048")) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_plane(1, 1, 2048, 2048, 1.0)));
	}
	if (string_equals(name, "sphere_2048")) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_uv_sphere(1, 4096, 2048, true, 1.0)));
	}
	for (i32 tool = TOOL_TYPE_PLANE; tool <= TOOL_TYPE_TORUS; ++tool) {
		if (string_equals(name, _shape_tool_names[tool - TOOL_TYPE_EMPTY])) {
			return _shape_tool_make_data(tool, _shape_tool_default_segments[tool - TOOL_TYPE_EMPTY]);
		}
	}
	return NULL;
}

static void _shape_tool_commit() {
	i32 tool   = g_context->tool;
	f32 size_x = math_abs(_shape_tool_p1.x - _shape_tool_p0.x);
	f32 size_y = math_abs(_shape_tool_p1.y - _shape_tool_p0.y);
	f32 size_z = math_abs(_shape_tool_height);

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	mesh_object_t *mo;
	if (tool == TOOL_TYPE_CAPSULE) {
		// Built for the box, scaling would squash the caps
		mo = tab_meshes_append_data(_shape_tool_geo_data(_shape_tool_make_capsule(size_x, size_y, size_z, _shape_tool_segments(tool)), "Capsule"));
	}
	else if (tool == TOOL_TYPE_EMPTY) {
		mo = tab_meshes_append_shape("empty");
	}
	else {
		mo = tab_meshes_append_data(_shape_tool_make_data(tool, _shape_tool_segments(tool)));
	}
	if (in_use) {
		draw_begin(current, false, 0);
	}

	transform_t *t = mo->base->transform;
	if (tool == TOOL_TYPE_EMPTY) {
		t->loc = _shape_tool_p0;
	}
	else {
		// Fit the shape into the dragged box, flat shapes keep the scale along their normal
		vec4_t aabb_min;
		vec4_t aabb_max;
		mesh_data_calculate_aabb_min_max(mo->data, &aabb_min, &aabb_max);
		vec4_t dim = vec4_sub(aabb_max, aabb_min);
		t->scale   = (vec4_t){dim.x > SHAPE_TOOL_MIN_SIZE ? size_x / dim.x : 1.0, dim.y > SHAPE_TOOL_MIN_SIZE ? size_y / dim.y : 1.0,
                            dim.z > SHAPE_TOOL_MIN_SIZE ? size_z / dim.z : 1.0, 1.0};
		t->loc = (vec4_t){(_shape_tool_p0.x + _shape_tool_p1.x) / 2.0 - (aabb_min.x + aabb_max.x) / 2.0 * t->scale.x,
		                  (_shape_tool_p0.y + _shape_tool_p1.y) / 2.0 - (aabb_min.y + aabb_max.y) / 2.0 * t->scale.y,
		                  _shape_tool_height / 2.0 - (aabb_min.z + aabb_max.z) / 2.0 * t->scale.z, 1.0};
	}
	transform_build_matrix(t);
	util_mesh_transform_changed();
	tab_meshes_reset_preview_map();

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	ui_header_handle->redraws = 2;
	g_context->ddirty         = 2;
}

void util_shape_tool_update() {
	if (!_shape_tool_active() || g_context->tool != _shape_tool_type || keyboard_started("escape")) {
		_shape_tool_state = SHAPE_TOOL_STATE_IDLE;
		_shape_tool_type  = g_context->tool;
		if (!_shape_tool_active()) {
			return;
		}
	}

	bool enabled = util_tool_input_enabled();
	bool clicked = enabled && mouse_started("left");

	if (_shape_tool_state == SHAPE_TOOL_STATE_IDLE) {
		if (clicked && util_tool_ground_point(&_shape_tool_p0)) {
			_shape_tool_p1     = _shape_tool_p0;
			_shape_tool_height = 0.0;
			if (g_context->tool == TOOL_TYPE_EMPTY) {
				_shape_tool_commit();
			}
			else {
				_shape_tool_state = SHAPE_TOOL_STATE_BASE;
			}
		}
	}
	else if (_shape_tool_state == SHAPE_TOOL_STATE_BASE) {
		if (enabled) {
			util_tool_ground_point(&_shape_tool_p1);
		}
		if (clicked && math_abs(_shape_tool_p1.x - _shape_tool_p0.x) > SHAPE_TOOL_MIN_SIZE &&
		    math_abs(_shape_tool_p1.y - _shape_tool_p0.y) > SHAPE_TOOL_MIN_SIZE) {
			if (g_context->tool == TOOL_TYPE_PLANE) {
				_shape_tool_state = SHAPE_TOOL_STATE_IDLE;
				_shape_tool_commit();
				return;
			}
			_shape_tool_height_my = mouse_view_y();
			_shape_tool_state     = SHAPE_TOOL_STATE_HEIGHT;
		}
	}
	else {
		if (enabled) {
			_shape_tool_height = _shape_tool_height_at_mouse();
		}
		if (clicked && math_abs(_shape_tool_height) > SHAPE_TOOL_MIN_SIZE) {
			_shape_tool_state = SHAPE_TOOL_STATE_IDLE;
			_shape_tool_commit();
		}
	}
}

// Line over the viewport, a and b are in world space
void util_tool_draw_line(vec4_t a, vec4_t b) {
	camera_object_t *cam = scene_camera;
	if (cam->p.m[15] == 0.0) {
		// Keep both ends in front of the perspective camera
		vec4_t eye  = mat4_get_loc(cam->base->transform->world);
		vec4_t look = camera_object_look_world(cam);
		f32    zn   = cam->data->near_plane;
		f32    za   = vec4_dot(vec4_sub(a, eye), look);
		f32    zb   = vec4_dot(vec4_sub(b, eye), look);
		if (za < zn && zb < zn) {
			return;
		}
		vec4_t dir = vec4_sub(b, a);
		if (za < zn) {
			a = vec4_add(a, vec4_mult(dir, (zn - za) / (zb - za)));
		}
		else if (zb < zn) {
			b = vec4_add(a, vec4_mult(dir, (zn - za) / (zb - za)));
		}
	}
	a.w = 1.0;
	b.w = 1.0;
	// Not jittered, the lines are not part of the antialiased frame
	mat4_t vp = mat4_mult_mat(cam->v, cam->no_jitter_p);
	a         = vec4_apply_proj(a, vp);
	b         = vec4_apply_proj(b, vp);
	draw_line_aa(base_x() + (a.x * 0.5 + 0.5) * base_w(), base_y() + (0.5 - a.y * 0.5) * base_h(), base_x() + (b.x * 0.5 + 0.5) * base_w(),
	             base_y() + (0.5 - b.y * 0.5) * base_h(), 1.5 * UI_SCALE());
}

// Dot of a constant size over the viewport, p is in world space
void util_tool_draw_point(vec4_t p) {
	camera_object_t *cam = scene_camera;
	if (cam->p.m[15] == 0.0) {
		vec4_t eye = mat4_get_loc(cam->base->transform->world);
		if (vec4_dot(vec4_sub(p, eye), camera_object_look_world(cam)) < cam->data->near_plane) {
			return;
		}
	}
	p.w = 1.0;
	p   = vec4_apply_proj(p, mat4_mult_mat(cam->v, cam->no_jitter_p));
	draw_filled_circle(base_x() + (p.x * 0.5 + 0.5) * base_w(), base_y() + (0.5 - p.y * 0.5) * base_h(), 3.5 * UI_SCALE(), 12);
}

// Wireframe of the box the shape is placed into, called while drawing the cursor
void util_shape_tool_render() {
	if (g_context->tool != _shape_tool_type || _shape_tool_state == SHAPE_TOOL_STATE_IDLE) {
		return;
	}

	f32    x0 = _shape_tool_p0.x;
	f32    y0 = _shape_tool_p0.y;
	f32    x1 = _shape_tool_p1.x;
	f32    y1 = _shape_tool_p1.y;
	f32    h  = _shape_tool_state == SHAPE_TOOL_STATE_HEIGHT ? _shape_tool_height : 0.0;
	vec4_t c[8];
	for (i32 i = 0; i < 2; ++i) {
		f32 z        = i == 0 ? 0.0 : h;
		c[i * 4]     = (vec4_t){x0, y0, z, 1.0};
		c[i * 4 + 1] = (vec4_t){x1, y0, z, 1.0};
		c[i * 4 + 2] = (vec4_t){x1, y1, z, 1.0};
		c[i * 4 + 3] = (vec4_t){x0, y1, z, 1.0};
	}

	draw_scissor(base_x(), base_y(), base_w(), base_h());
	draw_set_color(0xffffffff);
	for (i32 i = 0; i < 4; ++i) {
		util_tool_draw_line(c[i], c[(i + 1) % 4]);
		if (_shape_tool_state == SHAPE_TOOL_STATE_HEIGHT) {
			util_tool_draw_line(c[4 + i], c[4 + (i + 1) % 4]);
			util_tool_draw_line(c[i], c[4 + i]);
		}
	}
	draw_flush();
	gpu_disable_scissor();
}
