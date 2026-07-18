
#include "../global.h"

static slot_layer_t *path_layer_current      = NULL;
static object_t    **path_point_spheres      = NULL;
static i32           path_point_sphere_count = 0;
i32                  path_point_dragging     = -1;
i32                  path_layer_last_active  = -1;

bool util_layer_is_path_point_dragging() {
	return path_point_dragging >= 0;
}

static void path_destroy_spheres() {
	for (i32 i = 0; i < path_point_sphere_count; i++) {
		mesh_object_remove((mesh_object_t *)path_point_spheres[i]->ext);
	}
	gc_free(path_point_spheres);
	path_point_spheres      = NULL;
	path_point_sphere_count = 0;
}

static bool project_to_screen(vec4_t wpos, f32 *sx, f32 *sy) {
	vec4_t clip = vec4_apply_mat4(wpos, scene_camera->vp);
	if (clip.w > 0.0f) {
		*sx = (clip.x / clip.w + 1.0f) * 0.5f;
		*sy = (-clip.y / clip.w + 1.0f) * 0.5f;
		return true;
	}
	return false;
}

static f32 bezier_eval(f32 p0, f32 pc, f32 p1, f32 t) {
	f32 t1 = 1.0f - t;
	return t1 * t1 * t1 * p0 + 3.0f * t * t1 * pc + t * t * t * p1;
}

f32 util_layer_brush_screen_radius() {
	// Prevent cursor jumping constantly if randomness is used in brush nodes
	f32 brush_nodes_radius = g_context->brush_nodes_uses_random ? 1.0f : g_context->brush_nodes_radius;

	f32  r_world = g_context->brush_radius * brush_nodes_radius / 15.0f * 2.0f;
	bool on_mesh = math_abs(g_context->posx_picked) < 50.0f && math_abs(g_context->posy_picked) < 50.0f && math_abs(g_context->posz_picked) < 50.0f;
	if (!on_mesh) {
		return r_world;
	}
	vec4_t c_w   = (vec4_t){g_context->posx_picked, g_context->posy_picked, g_context->posz_picked, 1.0f};
	vec4_t right = camera_object_right_world(scene_camera);
	vec4_t e_w   = vec4_add(c_w, vec4_mult(right, r_world));
	f32    cx, cy, ex, ey;
	if (!project_to_screen(c_w, &cx, &cy) || !project_to_screen(e_w, &ex, &ey)) {
		return r_world;
	}
	f32 aspect = sys_w() / (f32)sys_h();
	f32 dx     = (ex - cx) * aspect;
	f32 dy     = ey - cy;
	return sqrtf(dx * dx + dy * dy);
}

static void path_push_camera(f32_array_t *ar) {
	vec4_t loc = scene_camera->base->transform->loc;
	quat_t rot = scene_camera->base->transform->rot;
	f32_array_push(ar, loc.x);
	f32_array_push(ar, loc.y);
	f32_array_push(ar, loc.z);
	f32_array_push(ar, loc.w);
	f32_array_push(ar, rot.x);
	f32_array_push(ar, rot.y);
	f32_array_push(ar, rot.z);
	f32_array_push(ar, rot.w);
	f32_array_push(ar, sys_w() / (f32)sys_h());
}

static void path_set_camera(f32_array_t *points_camera, i32 num_camera, i32 ci) {
	if (ci < num_camera) {
		scene_camera->base->transform->loc = (vec4_t){points_camera->buffer[ci * 9 + 0], points_camera->buffer[ci * 9 + 1], points_camera->buffer[ci * 9 + 2],
		                                              points_camera->buffer[ci * 9 + 3]};
		scene_camera->base->transform->rot = (quat_t){points_camera->buffer[ci * 9 + 4], points_camera->buffer[ci * 9 + 5], points_camera->buffer[ci * 9 + 6],
		                                              points_camera->buffer[ci * 9 + 7]};
		camera_object_build_proj(scene_camera, points_camera->buffer[ci * 9 + 8]);
		camera_object_build_mat(scene_camera);
		render_path_base_draw_gbuffer();
		if (g_context->layer->texpaint_sculpt != NULL) {
			render_path_sculpt_snapshot_gbuffer();
		}
	}
}

static void path_paint(f32 px, f32 py, f32 *prev_px, f32 *prev_py, bool sphere_mode) {
	g_context->decal_x          = px;
	g_context->decal_y          = py;
	g_context->paint_vec.x      = px;
	g_context->paint_vec.y      = py;
	g_context->last_paint_vec_x = sphere_mode ? px : *prev_px;
	g_context->last_paint_vec_y = sphere_mode ? py : *prev_py;
	g_context->pdirty           = 1;
	render_path_paint_commands_paint(false);
	*prev_px = px;
	*prev_py = py;
}

static void path_paint_curved(f32_array_t *points, f32_array_t *points_world, f32_array_t *points_camera, i32_array_t *points_parent, i32 num_world,
                              i32 num_camera, i32 num_parent, bool sphere_mode, f32 dot_spacing) {
	f32 prev_px = 0.5f;
	f32 prev_py = 0.5f;

	// Paint the first anchor
	f32 pt0x = points->length >= 2 ? points->buffer[0] : 0.5f;
	f32 pt0y = points->length >= 2 ? points->buffer[1] : 0.5f;
	path_set_camera(points_camera, num_camera, 0);
	prev_px                     = pt0x;
	prev_py                     = pt0y;
	g_context->prev_paint_vec_x = pt0x;
	g_context->prev_paint_vec_y = pt0y;
	path_paint(pt0x, pt0y, &prev_px, &prev_py, sphere_mode);

	// Paint curve - anchor[p], control[j-1], anchor[j]
	for (i32 j = 2; j < num_world; j += 2) {
		i32 p   = (j < num_parent) ? points_parent->buffer[j] : j - 2;
		f32 ax1 = j * 2 + 1 < (i32)points->length ? points->buffer[j * 2] : 0.5f;
		f32 ay1 = j * 2 + 1 < (i32)points->length ? points->buffer[j * 2 + 1] : 0.5f;

		f32 wx0 = points_world->buffer[p * 3];
		f32 wy0 = points_world->buffer[p * 3 + 1];
		f32 wz0 = points_world->buffer[p * 3 + 2];
		f32 wcx = points_world->buffer[(j - 1) * 3];
		f32 wcy = points_world->buffer[(j - 1) * 3 + 1];
		f32 wcz = points_world->buffer[(j - 1) * 3 + 2];
		f32 wx1 = points_world->buffer[j * 3];
		f32 wy1 = points_world->buffer[j * 3 + 1];
		f32 wz1 = points_world->buffer[j * 3 + 2];

		// Restore the start anchor camera and re-project its position as prev
		if (p < num_camera) {
			path_set_camera(points_camera, num_camera, p);
			project_to_screen((vec4_t){wx0, wy0, wz0, 1.0f}, &prev_px, &prev_py);
		}

		bool have_j                 = j < num_camera;
		f32  prev_bwx               = wx0;
		f32  prev_bwy               = wy0;
		f32  prev_bwz               = wz0;
		g_context->prev_paint_vec_x = prev_px;
		g_context->prev_paint_vec_y = prev_py;
		if (sphere_mode) {
			// Pre-sample bezier to build arc-length table, then paint at evenly-spaced positions
			f32 sbx[33];
			f32 sby[33];
			f32 sbz[33];
			f32 slen[33];
			sbx[0]  = wx0;
			sby[0]  = wy0;
			sbz[0]  = wz0;
			slen[0] = 0.0f;
			for (i32 k = 1; k <= 32; k++) {
				f32 kt = k / 32.0f;
				sbx[k] = bezier_eval(wx0, wcx, wx1, kt);
				sby[k] = bezier_eval(wy0, wcy, wy1, kt);
				sbz[k] = bezier_eval(wz0, wcz, wz1, kt);
				f32 dx = sbx[k] - sbx[k - 1], dy = sby[k] - sby[k - 1], dz = sbz[k] - sbz[k - 1];
				slen[k] = slen[k - 1] + sqrtf(dx * dx + dy * dy + dz * dz);
			}
			i32 n = dot_spacing > 0.0f ? (i32)ceilf(slen[32] / dot_spacing) : 17;
			n     = n < 2 ? 2 : n > 64 ? 64 : n;
			for (i32 s = 1; s <= n; s++) {
				f32 tgt = s / (f32)n * slen[32];
				i32 k   = 1;
				while (k < 32 && slen[k] < tgt)
					k++;
				f32 d   = slen[k] - slen[k - 1];
				f32 a   = d > 0.0f ? (tgt - slen[k - 1]) / d : 1.0f;
				f32 bwx = sbx[k - 1] + a * (sbx[k] - sbx[k - 1]);
				f32 bwy = sby[k - 1] + a * (sby[k] - sby[k - 1]);
				f32 bwz = sbz[k - 1] + a * (sbz[k] - sbz[k - 1]);
				if (s == n && have_j) {
					path_set_camera(points_camera, num_camera, j);
					project_to_screen((vec4_t){prev_bwx, prev_bwy, prev_bwz, 1.0f}, &prev_px, &prev_py);
					g_context->prev_paint_vec_x = prev_px;
					g_context->prev_paint_vec_y = prev_py;
				}
				f32 bx = ax1, by = ay1;
				project_to_screen((vec4_t){bwx, bwy, bwz, 1.0f}, &bx, &by);
				path_paint(bx, by, &prev_px, &prev_py, sphere_mode);
				prev_bwx = bwx;
				prev_bwy = bwy;
				prev_bwz = bwz;
			}
		}
		else { // Capsule
			for (i32 s = 1; s <= 17; s++) {
				f32 t   = s / 17.0f;
				f32 bwx = bezier_eval(wx0, wcx, wx1, t);
				f32 bwy = bezier_eval(wy0, wcy, wy1, t);
				f32 bwz = bezier_eval(wz0, wcz, wz1, t);
				if (s == 17 && have_j) {
					path_set_camera(points_camera, num_camera, j);
					// Re-project the previous world pos from the new camera so that
					// last_paint_vec and paint_vec are in the same screen space
					project_to_screen((vec4_t){prev_bwx, prev_bwy, prev_bwz, 1.0f}, &prev_px, &prev_py);
					g_context->prev_paint_vec_x = prev_px;
					g_context->prev_paint_vec_y = prev_py;
				}
				f32 bx = ax1, by = ay1;
				project_to_screen((vec4_t){bwx, bwy, bwz, 1.0f}, &bx, &by);
				path_paint(bx, by, &prev_px, &prev_py, sphere_mode);
				prev_bwx = bwx;
				prev_bwy = bwy;
				prev_bwz = bwz;
			}
		}
	}

	render_path_paint_dilate(true, true);
}

static void path_paint_straight(f32_array_t *points, f32_array_t *points_world, f32_array_t *points_camera, i32_array_t *points_parent, i32 num_world,
                                i32 num_camera, bool sphere_mode, f32 dot_spacing) {
	if (num_world > 0 && points->length >= 2) {
		g_context->prev_paint_vec_x = points->buffer[0];
		g_context->prev_paint_vec_y = points->buffer[1];
	}
	for (i32 i = 0; i < num_world; i++) {
		path_set_camera(points_camera, num_camera, i);

		f32 cur_px  = points->buffer[i * 2];
		f32 cur_py  = points->buffer[i * 2 + 1];
		f32 prev_px = cur_px;
		f32 prev_py = cur_py;
		i32 parent  = points_parent->buffer[i];

		if (sphere_mode && parent >= 0) {
			f32 pwx = points_world->buffer[parent * 3];
			f32 pwy = points_world->buffer[parent * 3 + 1];
			f32 pwz = points_world->buffer[parent * 3 + 2];
			f32 cwx = points_world->buffer[i * 3];
			f32 cwy = points_world->buffer[i * 3 + 1];
			f32 cwz = points_world->buffer[i * 3 + 2];
			f32 ppx = cur_px, ppy = cur_py;
			project_to_screen((vec4_t){pwx, pwy, pwz, 1.0f}, &ppx, &ppy);
			prev_px                     = ppx;
			prev_py                     = ppy;
			f32 dx                      = cwx - pwx;
			f32 dy                      = cwy - pwy;
			f32 dz                      = cwz - pwz;
			f32 len                     = sqrtf(dx * dx + dy * dy + dz * dz);
			i32 n                       = dot_spacing > 0.0f ? (i32)ceilf(len / dot_spacing) : 17;
			n                           = n < 1 ? 1 : n > 64 ? 64 : n;
			g_context->prev_paint_vec_x = ppx;
			g_context->prev_paint_vec_y = ppy;
			for (i32 s = 1; s <= n; s++) {
				f32 t   = s / (f32)n;
				f32 iwx = pwx + t * (cwx - pwx);
				f32 iwy = pwy + t * (cwy - pwy);
				f32 iwz = pwz + t * (cwz - pwz);
				f32 ix = 0.0f, iy = 0.0f;
				if (project_to_screen((vec4_t){iwx, iwy, iwz, 1.0f}, &ix, &iy)) {
					path_paint(ix, iy, &prev_px, &prev_py, sphere_mode);
				}
			}
		}
		else { // Capsule
			if (parent >= 0) {
				f32 pwx = points_world->buffer[parent * 3];
				f32 pwy = points_world->buffer[parent * 3 + 1];
				f32 pwz = points_world->buffer[parent * 3 + 2];
				project_to_screen((vec4_t){pwx, pwy, pwz, 1.0f}, &prev_px, &prev_py);
			}
			path_paint(cur_px, cur_py, &prev_px, &prev_py, sphere_mode);
		}
	}

	render_path_paint_dilate(true, true);
}

static void path_repaint(slot_layer_t *l) {
	if (l->path_material == NULL) {
		return;
	}
	f32_array_t *points_world = l->path_points_world;
	if (points_world == NULL || points_world->length == 0) {
		return;
	}

	slot_layer_t    *_layer     = g_context->layer;
	slot_material_t *_material  = g_context->material;
	tool_type_t      _tool      = g_context->tool;
	f32              _last_x    = g_context->last_paint_vec_x;
	f32              _last_y    = g_context->last_paint_vec_y;
	vec4_t           _paint_vec = g_context->paint_vec;
	g_context->layer            = l;
	g_context->material         = l->path_material;
	g_context->tool             = l->path_tool;

	make_material_save_paint_material();
	make_material_parse_paint_material(false);
	layers_set_object_mask();

	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);

	if (l->texpaint_sculpt != NULL) {
		// Restore the undeformed base mesh
		sculpt_import_mesh_pack_to_texture(l->texpaint_sculpt);
	}

	vec4_t _camera_loc = scene_camera->base->transform->loc;
	quat_t _camera_rot = scene_camera->base->transform->rot;

	f32_array_t *points        = l->path_points;
	f32_array_t *points_camera = l->path_points_camera;
	i32_array_t *points_parent = l->path_points_parent;
	i32          num_world     = points_world->length / 3;
	i32          num_camera    = points_camera->length / 9;
	i32          num_parent    = points_parent->length;
	bool         sphere_mode   = g_context->brush_lazy_radius > 0 && g_context->brush_lazy_step > 0;
	// Spacing in world units
	f32 r_world     = g_context->brush_radius * g_context->brush_nodes_radius / 15.0f * 2.0f;
	f32 dot_spacing = sphere_mode ? g_context->brush_lazy_radius * g_context->brush_lazy_step * r_world * 3.0 : 0.0f;

	if (l->path_curved && num_world >= 1) {
		path_paint_curved(points, points_world, points_camera, points_parent, num_world, num_camera, num_parent, sphere_mode, dot_spacing);
	}
	else {
		path_paint_straight(points, points_world, points_camera, points_parent, num_world, num_camera, sphere_mode, dot_spacing);
	}

	scene_camera->base->transform->loc = _camera_loc;
	scene_camera->base->transform->rot = _camera_rot;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);
	render_path_base_draw_gbuffer();

	g_context->pdirty           = 0;
	g_context->rtdirty          = 1;
	g_context->rdirty           = 2;
	g_context->tool             = _tool;
	g_context->layer            = _layer;
	g_context->material         = _material;
	g_context->last_paint_vec_x = _last_x;
	g_context->last_paint_vec_y = _last_y;
	g_context->paint_vec        = _paint_vec;
	make_material_restore_paint_material();
}

void util_layer_clear_path_points(slot_layer_t *l) {
	if (l->path_points == NULL) {
		return;
	}
	l->path_points->length        = 0;
	l->path_points_world->length  = 0;
	l->path_points_camera->length = 0;
	l->path_points_parent->length = 0;
	l->path_tool                  = -1;
	path_layer_last_active        = -1;
}

void util_layer_add_path_point(slot_layer_t *l, f32 screen_x, f32 screen_y) {
	if (l->path_tool == -1) {
		l->path_tool = g_context->tool;
	}
	if ((i32)g_context->tool != l->path_tool) {
		return;
	}

	bool _paint2d = g_context->paint2d;
	if (_paint2d) {
		// Convert 2D view x-coordinate [1, 1+ww/base_w] to 3D range [0, 1]
		screen_x = (screen_x * base_w() - base_w()) / (float)ui_view2d_ww;
		render_path_paint_set_plane_mesh();
		g_context->paint2d = false;
	}

	f32_array_t *points        = l->path_points;
	f32_array_t *points_world  = l->path_points_world;
	f32_array_t *points_camera = l->path_points_camera;
	i32_array_t *points_parent = l->path_points_parent;

	for (i32 j = 0; j < path_point_sphere_count; j++) {
		path_point_spheres[j]->visible = false;
	}

	// Re-render gbuffer without spheres so gbufferD is clean
	render_path_base_draw_gbuffer();

	f32_array_push(points, screen_x);
	f32_array_push(points, screen_y);
	f32 saved_pvx          = g_context->paint_vec.x;
	f32 saved_pvy          = g_context->paint_vec.y;
	g_context->paint_vec.x = screen_x;
	g_context->paint_vec.y = screen_y;
	util_render_pick_pos_nor_tex();
	g_context->paint_vec.x = saved_pvx;
	g_context->paint_vec.y = saved_pvy;
	f32_array_push(points_world, g_context->posx_picked);
	f32_array_push(points_world, g_context->posy_picked);
	f32_array_push(points_world, g_context->posz_picked);
	path_push_camera(points_camera);

	// For curved paths the parent must be an anchor
	i32 parent = path_layer_last_active;
	if (l->path_curved && parent % 2 == 1) {
		if (parent < (i32)points_parent->length) {
			parent = points_parent->buffer[parent];
		}
		else {
			parent -= 1;
		}
	}
	i32_array_push(points_parent, parent);

	// New point becomes the parent for the next stroke
	i32 new_index          = points->length / 2 - 1;
	path_layer_last_active = new_index;

	path_repaint(l);

	for (i32 j = 0; j < path_point_sphere_count; j++) {
		path_point_spheres[j]->visible = true;
	}

	if (_paint2d) {
		g_context->paint2d = true;
		render_path_paint_restore_plane_mesh();
	}
}

void util_layer_repaint_path(slot_layer_t *l) {
	for (i32 j = 0; j < path_point_sphere_count; j++) {
		path_point_spheres[j]->visible = false;
	}
	path_repaint(l);
	for (i32 j = 0; j < path_point_sphere_count; j++) {
		path_point_spheres[j]->visible = true;
	}
}

static void path_point_move(slot_layer_t *l, i32 idx, f32 new_x, f32 new_y) {
	f32_array_t *points        = l->path_points;
	f32_array_t *points_world  = l->path_points_world;
	f32_array_t *points_camera = l->path_points_camera;

	points->buffer[idx * 2]     = new_x;
	points->buffer[idx * 2 + 1] = new_y;

	// Pick world position at new screen location
	f32 saved_pvx          = g_context->paint_vec.x;
	f32 saved_pvy          = g_context->paint_vec.y;
	g_context->paint_vec.x = new_x;
	g_context->paint_vec.y = new_y;
	util_render_pick_pos_nor_tex();
	g_context->paint_vec.x = saved_pvx;
	g_context->paint_vec.y = saved_pvy;

	if (math_abs(g_context->posx_picked) < 50.0f) {
		points_world->buffer[idx * 3]     = g_context->posx_picked;
		points_world->buffer[idx * 3 + 1] = g_context->posy_picked;
		points_world->buffer[idx * 3 + 2] = g_context->posz_picked;
		if (points_camera->length >= (idx + 1) * 9) {
			vec4_t loc                         = scene_camera->base->transform->loc;
			quat_t rot                         = scene_camera->base->transform->rot;
			points_camera->buffer[idx * 9 + 0] = loc.x;
			points_camera->buffer[idx * 9 + 1] = loc.y;
			points_camera->buffer[idx * 9 + 2] = loc.z;
			points_camera->buffer[idx * 9 + 3] = loc.w;
			points_camera->buffer[idx * 9 + 4] = rot.x;
			points_camera->buffer[idx * 9 + 5] = rot.y;
			points_camera->buffer[idx * 9 + 6] = rot.z;
			points_camera->buffer[idx * 9 + 7] = rot.w;
			points_camera->buffer[idx * 9 + 8] = sys_w() / (f32)sys_h();
		}
	}
}

void util_layer_check_path_grab() {
	// Check if mouse ray hits any point sphere
	if (g_config->workspace == WORKSPACE_PLAYER || g_context->paint2d) {
		return;
	}

	slot_layer_t *l = g_context->layer;
	if (!slot_layer_is_path(l) || !slot_layer_is_visible(l)) {
		return;
	}

	i32 vis_points = l->path_points->length / 2;
	if (!mouse_started("left") || g_ui->is_hovered || base_is_dragging || vis_points == 0) {
		return;
	}

	f32_array_t *points_world = l->path_points_world;
	f32          min_dist     = 1e10f;
	i32          closest      = -1;

	for (i32 i = 0; i < vis_points; i++) {
		f32    wx         = points_world->buffer[i * 3];
		f32    wy         = points_world->buffer[i * 3 + 1];
		f32    wz         = points_world->buffer[i * 3 + 2];
		f32    dist       = vec4_dist(scene_camera->base->transform->loc, (vec4_t){wx, wy, wz, 1.0});
		f32    fov        = scene_camera->data->fov;
		f32    hit_radius = dist / 8.0f * fov * 0.25f;
		ray_t *ray        = raycast_get_ray(mouse_view_x(), mouse_view_y(), scene_camera);
		if (ray_intersects_sphere(ray, (vec4_t){wx, wy, wz, 1.0}, hit_radius) && dist < min_dist) {
			min_dist = dist;
			closest  = i;
		}
	}

	if (closest >= 0) {
		path_point_dragging = closest;
		for (i32 j = 0; j < path_point_sphere_count; j++) {
			if (path_point_spheres[j] != NULL) {
				path_point_spheres[j]->visible = false;
			}
		}
	}
}

void util_layer_update_path() {
	if (g_config->workspace == WORKSPACE_PLAYER || g_context->paint2d) {
		return;
	}

	slot_layer_t *l       = g_context->layer;
	bool          is_path = slot_layer_is_path(l) && slot_layer_is_visible(l);

	// Clear spheres when switching away from path layer
	if (path_layer_current != l || !is_path) {
		path_destroy_spheres();
		path_point_dragging    = -1;
		path_layer_current     = is_path ? l : NULL;
		path_layer_last_active = -1;
		if (is_path) {
			// Resume from the last point when switching to a path layer
			i32 n                  = l->path_points->length / 2;
			path_layer_last_active = n > 0 ? n - 1 : -1;
		}
	}

	if (!is_path) {
		return;
	}

	f32_array_t *points       = l->path_points;
	f32_array_t *points_world = l->path_points_world;
	i32          num_points   = points->length / 2;
	i32          vis_points   = num_points;

	// Sync sphere count with number of visible path points
	if (vis_points < path_point_sphere_count) {
		while (path_point_sphere_count > vis_points) {
			path_point_sphere_count--;
			mesh_object_remove((mesh_object_t *)path_point_spheres[path_point_sphere_count]->ext);
			path_point_spheres[path_point_sphere_count] = NULL;
		}
	}
	else if (vis_points > path_point_sphere_count) {
		path_point_spheres = (object_t **)gc_realloc(path_point_spheres, vis_points * sizeof(object_t *));
		gc_root(path_point_spheres);
		for (i32 i = path_point_sphere_count; i < vis_points; i++) {
			object_t      *o      = scene_spawn_object(".Sphere", NULL, true);
			mesh_object_t *mo     = o->ext;
			mo->material          = make_particle_get_bullet_material();
			o->visible            = true;
			path_point_spheres[i] = o;
		}
		path_point_sphere_count = vis_points;
	}

	// Update sphere transforms
	for (i32 i = 0; i < vis_points; i++) {
		if (path_point_spheres[i] == NULL) {
			continue;
		}
		f32 wx    = points_world->buffer[i * 3];
		f32 wy    = points_world->buffer[i * 3 + 1];
		f32 wz    = points_world->buffer[i * 3 + 2];
		f32 dist  = vec4_dist(scene_camera->base->transform->loc, (vec4_t){wx, wy, wz, 1.0});
		f32 fov   = scene_camera->data->fov;
		f32 scale = dist / 8.0f * fov * 0.1f * (i == path_layer_last_active ? 1.5f : 1.0f);

		path_point_spheres[i]->transform->loc   = (vec4_t){wx, wy, wz, 1.0};
		path_point_spheres[i]->transform->scale = (vec4_t){scale, scale, scale, 1.0};
		transform_build_matrix(path_point_spheres[i]->transform);
	}

	// Release drag - the dragged point becomes the parent for the next new point
	if (mouse_released("left") && path_point_dragging >= 0) {
		for (i32 j = 0; j < path_point_sphere_count; j++) {
			path_point_spheres[j]->visible = true;
		}
		path_layer_last_active          = path_point_dragging;
		path_point_dragging             = -1;
		g_context->layer_preview_dirty  = true;
		g_context->layers_preview_dirty = true;
	}

	// Drag point
	if (mouse_down("left") && !mouse_started("left") && path_point_dragging >= 0) {
		f32 new_x = g_context->paint_vec.x;
		f32 new_y = g_context->paint_vec.y;
		f32 old_x = points->buffer[path_point_dragging * 2];
		f32 old_y = points->buffer[path_point_dragging * 2 + 1];

		if (math_abs(new_x - old_x) > 0.0005f || math_abs(new_y - old_y) > 0.0005f) {
			if (keyboard_down("control")) {
				// Move all points
				f32 dx = new_x - old_x;
				f32 dy = new_y - old_y;
				for (i32 i = 0; i < num_points; i++) {
					path_point_move(l, i, points->buffer[i * 2] + dx, points->buffer[i * 2 + 1] + dy);
				}
			}
			else {
				path_point_move(l, path_point_dragging, new_x, new_y);
			}

			util_layer_repaint_path(l);
			for (i32 j = 0; j < path_point_sphere_count; j++) {
				path_point_spheres[j]->visible = false;
			}
		}
		return;
	}
}
