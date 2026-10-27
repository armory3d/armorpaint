
#include "../global.h"

static vec4_t gizmo_undo_loc;
static quat_t gizmo_undo_rot;
static vec4_t gizmo_undo_scale;
static bool   gizmo_undo_stored = false;

static bool gizmo_quat_equals(quat_t a, quat_t b) {
	return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

static vec4_t gizmo_v        = (vec4_t){0.0, 0.0, 0.0, 1.0};
static vec4_t gizmo_v0       = (vec4_t){0.0, 0.0, 0.0, 1.0};
static quat_t gizmo_q        = (quat_t){0.0, 0.0, 0.0, 1.0};
static quat_t gizmo_q0       = (quat_t){0.0, 0.0, 0.0, 1.0};
static f32    gizmo_drag_raw = 0.0;

static vec4_t gizmo_world_axis_to_parent(object_t *o, vec4_t axis) {
	if (o->parent == NULL) {
		return axis;
	}
	axis.w = 0.0;
	axis   = vec4_apply_mat4(axis, mat4_inv(o->parent->transform->world));
	return vec4_norm(axis);
}

static void gizmo_rotate_world(object_t *o, vec4_t axis, f32 angle) {
	quat_t q          = quat_from_axis_angle(gizmo_world_axis_to_parent(o, axis), angle);
	o->transform->rot = quat_norm(quat_mult(q, o->transform->rot));
}

static void gizmo_scale_world(object_t *o, vec4_t axis, f32 delta) {
	vec4_t a = gizmo_world_axis_to_parent(o, axis);
	a        = vec4_apply_quat(a, quat_inv(o->transform->rot));
	o->transform->scale.x += delta * a.x * a.x;
	o->transform->scale.y += delta * a.y * a.y;
	o->transform->scale.z += delta * a.z * a.z;
}

// Scale the decal along a world axis, mapped onto its rotated local axes
static void gizmo_scale_decal(vec4_t axis, f32 delta) {
	mat4_decomposed_t *dec = mat4_decompose(g_context->layer->decal_mat);
	vec4_t             a   = vec4_apply_quat(axis, quat_inv(dec->rot));
	vec4_t             scl = dec->scl;
	scl.x += delta * a.x * a.x;
	scl.y += delta * a.y * a.y;
	scl.z += delta * a.z * a.z;
	g_context->layer->decal_mat = mat4_compose(dec->loc, dec->rot, scl);
}

// Other selected meshes follow the gizmo of the paint object
static mesh_object_t_array_t *gizmo_others       = NULL;
static vec4_t                *gizmo_others_loc   = NULL;
static quat_t                *gizmo_others_rot   = NULL;
static vec4_t                *gizmo_others_scale = NULL;

static vec4_t gizmo_world_loc(object_t *o) {
	return (vec4_t){transform_world_x(o->transform), transform_world_y(o->transform), transform_world_z(o->transform), 1.0};
}

static void gizmo_set_world_loc(object_t *o, vec4_t v) {
	v.w = 1.0;
	if (o->parent != NULL) {
		v = vec4_apply_mat4(v, mat4_inv(o->parent->transform->world));
	}
	o->transform->loc = (vec4_t){v.x, v.y, v.z, 1.0};
}

static void gizmo_others_begin() {
	mesh_object_t *paint_object = g_context->paint_object;
	if (gizmo_others == NULL) {
		gizmo_others = any_array_create(0);
	}
	gizmo_others->length = 0;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o == paint_object || !tab_meshes_is_selected(o)) {
			continue;
		}
		// Children follow their selected parent, parents of the paint object stay
		bool skip = false;
		for (object_t *p = o->base->parent; p != NULL && !skip; p = p->parent) {
			mesh_object_t *po = p->ext_type != NULL && string_equals(p->ext_type, "mesh_object_t") ? p->ext : NULL;
			skip              = po != NULL && tab_meshes_is_selected(po);
		}
		for (object_t *p = paint_object->base->parent; p != NULL && !skip; p = p->parent) {
			skip = p == o->base;
		}
		if (!skip) {
			any_array_push(gizmo_others, o);
		}
	}
	gizmo_others_loc   = realloc(gizmo_others_loc, sizeof(vec4_t) * (gizmo_others->length + 1));
	gizmo_others_rot   = realloc(gizmo_others_rot, sizeof(quat_t) * (gizmo_others->length + 1));
	gizmo_others_scale = realloc(gizmo_others_scale, sizeof(vec4_t) * (gizmo_others->length + 1));
	for (i32 i = 0; i < gizmo_others->length; ++i) {
		transform_t *t        = gizmo_others->buffer[i]->base->transform;
		gizmo_others_loc[i]   = t->loc;
		gizmo_others_rot[i]   = t->rot;
		gizmo_others_scale[i] = t->scale;
	}
}

// Moved by the same offset, rotated around the gizmo and scaled in place
static void gizmo_others_transform(vec4_t pivot, vec4_t offset, f32 delta) {
	for (i32 i = 0; gizmo_others != NULL && i < gizmo_others->length; ++i) {
		mesh_object_t *mo = gizmo_others->buffer[i];
		if (array_index_of(g_project->_->paint_objects, mo) < 0) {
			continue;
		}
		object_t *o = mo->base;
		if (g_context->translate_x || g_context->translate_y || g_context->translate_z) {
			gizmo_set_world_loc(o, vec4_add(gizmo_world_loc(o), offset));
		}
		else if (g_context->scale_x || g_context->scale_y || g_context->scale_z) {
			gizmo_scale_world(o, g_context->scale_x ? vec4_x_axis() : g_context->scale_y ? vec4_y_axis() : vec4_z_axis(), delta);
		}
		else {
			vec4_t axis  = g_context->rotate_x ? vec4_x_axis() : g_context->rotate_y ? vec4_y_axis() : vec4_z_axis();
			f32    angle = g_context->rotate_z ? delta : -delta;
			vec4_t v     = vec4_apply_quat(vec4_sub(gizmo_world_loc(o), pivot), quat_from_axis_angle(axis, angle));
			gizmo_rotate_world(o, axis, angle);
			gizmo_set_world_loc(o, vec4_add(pivot, v));
		}
		transform_build_matrix(o->transform);
		if (o->_->body != NULL) {
			physics_body_sync_transform(o->_->body);
		}
	}
}

static bool gizmo_others_end() {
	bool changed = false;
	for (i32 i = 0; gizmo_others != NULL && i < gizmo_others->length; ++i) {
		mesh_object_t *o = gizmo_others->buffer[i];
		if (array_index_of(g_project->_->paint_objects, o) < 0) {
			continue;
		}
		transform_t *t = o->base->transform;
		if (!vec4_equals(t->loc, gizmo_others_loc[i]) || !gizmo_quat_equals(t->rot, gizmo_others_rot[i]) || !vec4_equals(t->scale, gizmo_others_scale[i])) {
			history_object_transform(o, gizmo_others_loc[i], gizmo_others_rot[i], gizmo_others_scale[i]);
			changed = true;
		}
	}
	if (gizmo_others != NULL) {
		gizmo_others->length = 0;
	}
	return changed;
}

// Selection rect of the cursor tool, dragged from a click which did not hit the gizmo
static bool gizmo_rect_pressed = false;
static bool gizmo_rect_active  = false;
static f32  gizmo_rect_x       = 0.0;
static f32  gizmo_rect_y       = 0.0;

static void gizmo_rect_start() {
	if (util_tool_input_enabled()) {
		gizmo_rect_pressed = true;
		gizmo_rect_active  = false;
		gizmo_rect_x       = mouse_x;
		gizmo_rect_y       = mouse_y;
	}
}

static bool gizmo_rect_contains(mesh_object_t *o, f32 x0, f32 y0, f32 x1, f32 y1) {
	// Any vertex in the rect, occluded ones too
	mat4_t       m  = mat4_mult_mat(o->base->transform->world_unpack, scene_camera->vp);
	i16_array_t *va = o->data->vertex_arrays->buffer[0]->values;
	for (i32 i = 0; i + 3 < va->length; i += 4) {
		vec4_t p = {va->buffer[i] / 32767.0, va->buffer[i + 1] / 32767.0, va->buffer[i + 2] / 32767.0, 1.0};
		if (p.x * m.m03 + p.y * m.m13 + p.z * m.m23 + m.m33 <= 0.0) {
			continue; // Behind the camera
		}
		p     = vec4_apply_proj(p, m);
		f32 x = base_x() + (p.x * 0.5 + 0.5) * base_w();
		f32 y = base_y() + (0.5 - p.y * 0.5) * base_h();
		if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
			return true;
		}
	}
	return false;
}

static void gizmo_rect_update() {
	if (!gizmo_rect_pressed) {
		return;
	}
	if (g_context->tool != TOOL_TYPE_CURSOR || keyboard_started("escape")) {
		gizmo_rect_pressed = false;
		gizmo_rect_active  = false;
		return;
	}
	if (mouse_down("left")) {
		f32 threshold = 4 * UI_SCALE();
		if (math_abs(mouse_x - gizmo_rect_x) > threshold || math_abs(mouse_y - gizmo_rect_y) > threshold) {
			gizmo_rect_active = true;
		}
		return;
	}

	gizmo_rect_pressed = false;
	if (!gizmo_rect_active) {
		return; // Click, the mesh under the cursor was picked
	}
	gizmo_rect_active              = false;
	f32                    x0      = math_min(gizmo_rect_x, mouse_x);
	f32                    y0      = math_min(gizmo_rect_y, mouse_y);
	f32                    x1      = math_max(gizmo_rect_x, mouse_x);
	f32                    y1      = math_max(gizmo_rect_y, mouse_y);
	mesh_object_t_array_t *objects = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->base->visible && gizmo_rect_contains(o, x0, y0, x1, y1)) {
			any_array_push(objects, o);
		}
	}
	keyboard_down("shift") ? tab_meshes_add_objects(objects) : tab_meshes_select_objects(objects);
	g_context->ddirty = 2;
}

void render_gizmo_render_rect() {
	if (!gizmo_rect_active) {
		return;
	}
	f32 x = math_min(gizmo_rect_x, mouse_x);
	f32 y = math_min(gizmo_rect_y, mouse_y);
	f32 w = math_abs(mouse_x - gizmo_rect_x);
	f32 h = math_abs(mouse_y - gizmo_rect_y);
	draw_scissor(base_x(), base_y(), base_w(), base_h());
	draw_set_color((g_theme->HIGHLIGHT_COL & 0x00ffffff) | 0x33000000);
	draw_filled_rect(x, y, w, h);
	draw_set_color(g_theme->HIGHLIGHT_COL);
	draw_rect(x, y, w, h, 1.0);
	draw_set_color(0xffffffff);
	draw_flush();
	gpu_disable_scissor();
}

void render_gizmo_render_bones() {
	if (g_context->tool != TOOL_TYPE_CURSOR || base_player || g_context->paint2d || context_object_deselected() ||
	    keymap_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN)) {
		return;
	}
	draw_scissor(base_x(), base_y(), base_w(), base_h());
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->data->_->skin_blob == NULL || !o->base->visible || (o != g_context->paint_object && !tab_meshes_is_selected(o))) {
			continue;
		}
		i32    count = 0;
		f32   *bones = util_skin_bones(o->data->_->skin_blob, math_max(o->data->_->skin_frame, 0), &count);
		mat4_t m     = mat4_mult_mat(o->base->transform->world_unpack, mat4_mult_mat(scene_camera->v, scene_camera->no_jitter_p));
		for (i32 j = 0; j < count; ++j) {
			f32 *bone = &bones[j * 7];
			f32  s[4];
			bool behind = false;
			for (i32 k = 0; k < 2; ++k) {
				vec4_t p = {bone[k * 3], bone[k * 3 + 1], bone[k * 3 + 2], 1.0};
				if (p.x * m.m03 + p.y * m.m13 + p.z * m.m23 + m.m33 <= 0.0) {
					behind = true;
					break;
				}
				p            = vec4_apply_proj(p, m);
				s[k * 2]     = base_x() + (p.x * 0.5 + 0.5) * base_w();
				s[k * 2 + 1] = base_y() + (0.5 - p.y * 0.5) * base_h();
			}
			if (behind) {
				continue;
			}
			f32 dx  = s[2] - s[0];
			f32 dy  = s[3] - s[1];
			f32 len = math_sqrt(dx * dx + dy * dy);
			f32 r   = 3.0 * UI_SCALE();
			if (bone[6] != 0.0) {
				// Dashed relation line to a child not starting at the tail
				f32 dash = 4.0 * UI_SCALE();
				draw_set_color(0xaa1a1a1a);
				for (f32 t = 0.0; t < len; t += dash * 2.0) {
					f32 t1 = math_min(t + dash, len);
					draw_line_aa(s[0] + dx * t / len, s[1] + dy * t / len, s[0] + dx * t1 / len, s[1] + dy * t1 / len, 1.0);
				}
				continue;
			}
			// Octahedral bone seen from the camera: a kite widest near the head
			if (len > r) {
				f32 w  = math_min(len * 0.1, 12.0 * UI_SCALE());
				f32 nx = -dy / len * w;
				f32 ny = dx / len * w;
				f32 mx = s[0] + dx * 0.2;
				f32 my = s[1] + dy * 0.2;
				draw_set_color(0x40ffffff);
				draw_filled_triangle(s[0], s[1], mx + nx, my + ny, s[2], s[3]);
				draw_filled_triangle(s[0], s[1], s[2], s[3], mx - nx, my - ny);
				draw_set_color(0xff1a1a1a);
				draw_line_aa(s[0], s[1], mx + nx, my + ny, 1.0);
				draw_line_aa(mx + nx, my + ny, s[2], s[3], 1.0);
				draw_line_aa(s[2], s[3], mx - nx, my - ny, 1.0);
				draw_line_aa(mx - nx, my - ny, s[0], s[1], 1.0);
			}
			draw_set_color(g_theme->HIGHLIGHT_COL);
			draw_filled_circle(s[0], s[1], r, 0);
		}
		free(bones);
	}
	draw_set_color(0xffffffff);
	draw_flush();
	gpu_disable_scissor();
}

// Drawn over the scene, the gizmo is not part of scene_meshes
void render_gizmo_draw() {
	if (g_context->gizmo->visible) {
		mesh_object_t *gizmo       = g_context->gizmo->ext;
		_mesh_object_last_pipeline = NULL;
		mesh_object_render(gizmo, render_path_base_own_scon(gizmo), _render_path_bind_params);
	}
	render_path_end();
}

void render_gizmo_update() {
	bool is_object = g_context->tool == TOOL_TYPE_CURSOR;
	bool is_decal  = base_is_decal_layer();

	object_t *gizmo = g_context->gizmo;
	bool      hide  = keymap_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN);
	gizmo->visible  = (is_object || is_decal) && !hide && !base_player;
	if (!gizmo->visible) {
		return;
	}

	if (is_object) {
		gizmo_rect_update();
	}

	if (is_object && context_object_deselected()) {
		// No gizmo, clicking a mesh selects it
		gizmo->visible = false;
		if (mouse_started("left") && mouse_view_x() < base_w()) {
			g_context->pick_object_id = true;
			gizmo_rect_start();
		}
		else if (mouse_released("left")) {
			g_context->pick_object_id = false;
		}
		return;
	}

	object_t *paint_object = g_context->paint_object->base;

	if (is_object) {
		gizmo->transform->loc =
		    (vec4_t){transform_world_x(paint_object->transform), transform_world_y(paint_object->transform), transform_world_z(paint_object->transform), 1.0};
	}
	else if (is_decal) {
		gizmo->transform->loc = (vec4_t){g_context->layer->decal_mat.m30, g_context->layer->decal_mat.m31, g_context->layer->decal_mat.m32, 1.0};
	}

	camera_object_t *cam                           = scene_camera;
	f32              fov                           = cam->data->fov;
	f32              dist                          = vec4_dist(cam->base->transform->loc, gizmo->transform->loc) / 12.0 * fov;
	gizmo->transform->scale                        = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_translate_x->transform->scale = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_translate_y->transform->scale = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_translate_z->transform->scale = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_scale_x->transform->scale     = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_scale_y->transform->scale     = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_scale_z->transform->scale     = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_rotate_x->transform->scale    = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_rotate_y->transform->scale    = (vec4_t){dist, dist, dist, 1.0};
	g_context->gizmo_rotate_z->transform->scale    = (vec4_t){dist, dist, dist, 1.0};
	transform_build_matrix(gizmo->transform);

	// Scene control
	if (is_object) {
		if (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y || g_context->scale_z ||
		    g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) {
			vec4_t pivot = gizmo_world_loc(paint_object);
			f32    delta = g_context->gizmo_drag - g_context->gizmo_drag_last;
			if (g_context->translate_x) {
				mat4_t p                     = paint_object->parent != NULL ? mat4_inv(paint_object->parent->transform->world) : mat4_identity();
				vec4_t v                     = {g_context->gizmo_drag, gizmo_v.y, gizmo_v.z, 1.0};
				v                            = vec4_apply_mat4(v, p);
				paint_object->transform->loc = (vec4_t){v.x, v.y, v.z, 1.0};
			}
			else if (g_context->translate_y) {
				mat4_t p                     = paint_object->parent != NULL ? mat4_inv(paint_object->parent->transform->world) : mat4_identity();
				vec4_t v                     = {gizmo_v.x, g_context->gizmo_drag, gizmo_v.z, 1.0};
				v                            = vec4_apply_mat4(v, p);
				paint_object->transform->loc = (vec4_t){v.x, v.y, v.z, 1.0};
			}
			else if (g_context->translate_z) {
				mat4_t p                     = paint_object->parent != NULL ? mat4_inv(paint_object->parent->transform->world) : mat4_identity();
				vec4_t v                     = {gizmo_v.x, gizmo_v.y, g_context->gizmo_drag, 1.0};
				v                            = vec4_apply_mat4(v, p);
				paint_object->transform->loc = (vec4_t){v.x, v.y, v.z, 1.0};
			}
			else if (g_context->scale_x) {
				gizmo_scale_world(paint_object, vec4_x_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			else if (g_context->scale_y) {
				gizmo_scale_world(paint_object, vec4_y_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			else if (g_context->scale_z) {
				gizmo_scale_world(paint_object, vec4_z_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			else if (g_context->rotate_x) {
				gizmo_rotate_world(paint_object, vec4_x_axis(), -(g_context->gizmo_drag - g_context->gizmo_drag_last));
			}
			else if (g_context->rotate_y) {
				gizmo_rotate_world(paint_object, vec4_y_axis(), -(g_context->gizmo_drag - g_context->gizmo_drag_last));
			}
			else if (g_context->rotate_z) {
				gizmo_rotate_world(paint_object, vec4_z_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			g_context->gizmo_drag_last = g_context->gizmo_drag;

			transform_build_matrix(paint_object->transform);
			gizmo_others_transform(pivot, vec4_sub(gizmo_world_loc(paint_object), pivot), delta);
			ui_header_handle->redraws = 2;
			g_context->ddirty         = 2;

			render_path_raytrace_ready  = false;
			render_path_raytrace_moving = true;

			physics_body_t *pb = paint_object->_->body;
			if (pb != NULL) {
				physics_body_sync_transform(pb);
			}
		}
	}
	// Decal layer control
	else if (is_decal) {
		if (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y || g_context->scale_z ||
		    g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) {
			if (g_context->translate_x) {
				g_context->layer->decal_mat.m30 = g_context->gizmo_drag;
			}
			else if (g_context->translate_y) {
				g_context->layer->decal_mat.m31 = g_context->gizmo_drag;
			}
			else if (g_context->translate_z) {
				g_context->layer->decal_mat.m32 = g_context->gizmo_drag;
			}
			else if (g_context->scale_x) {
				gizmo_scale_decal(vec4_x_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			else if (g_context->scale_y) {
				gizmo_scale_decal(vec4_y_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			else if (g_context->scale_z) {
				gizmo_scale_decal(vec4_z_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
			}
			else if (g_context->rotate_x) {
				mat4_decomposed_t *dec      = mat4_decompose(g_context->layer->decal_mat);
				gizmo_v                     = dec->loc;
				gizmo_q                     = dec->rot;
				gizmo_v0                    = dec->scl;
				gizmo_q0                    = quat_from_axis_angle(vec4_x_axis(), -g_context->gizmo_drag + g_context->gizmo_drag_last);
				gizmo_q                     = quat_mult(gizmo_q0, gizmo_q);
				g_context->layer->decal_mat = mat4_compose(gizmo_v, gizmo_q, gizmo_v0);
			}
			else if (g_context->rotate_y) {
				mat4_decomposed_t *dec      = mat4_decompose(g_context->layer->decal_mat);
				gizmo_v                     = dec->loc;
				gizmo_q                     = dec->rot;
				gizmo_v0                    = dec->scl;
				gizmo_q0                    = quat_from_axis_angle(vec4_y_axis(), -g_context->gizmo_drag + g_context->gizmo_drag_last);
				gizmo_q                     = quat_mult(gizmo_q0, gizmo_q);
				g_context->layer->decal_mat = mat4_compose(gizmo_v, gizmo_q, gizmo_v0);
			}
			else if (g_context->rotate_z) {
				mat4_decomposed_t *dec      = mat4_decompose(g_context->layer->decal_mat);
				gizmo_v                     = dec->loc;
				gizmo_q                     = dec->rot;
				gizmo_v0                    = dec->scl;
				gizmo_q0                    = quat_from_axis_angle(vec4_z_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
				gizmo_q                     = quat_mult(gizmo_q0, gizmo_q);
				g_context->layer->decal_mat = mat4_compose(gizmo_v, gizmo_q, gizmo_v0);
			}
			g_context->gizmo_drag_last = g_context->gizmo_drag;

			if (g_context->material != g_context->layer->fill_material) {
				context_set_material(g_context->layer->fill_material);
			}
			layers_update_fill_layer(g_context->gizmo_started);
		}
	}

	g_context->gizmo_started = false;
	if (mouse_started("left") && !string_equals(paint_object->name, "Scene")) {
		// Translate, scale
		transform_t_array_t *trs = any_array_create_from_raw(
		    (void *[]){
		        g_context->gizmo_translate_x->transform,
		        g_context->gizmo_translate_y->transform,
		        g_context->gizmo_translate_z->transform,
		        g_context->gizmo_scale_x->transform,
		        g_context->gizmo_scale_y->transform,
		        g_context->gizmo_scale_z->transform,
		    },
		    6);
		transform_t *hit = raycast_closest_box_intersect(trs, mouse_view_x(), mouse_view_y(), scene_camera);
		if (hit != NULL) {
			if (hit->object == g_context->gizmo_translate_x) {
				g_context->translate_x = true;
			}
			else if (hit->object == g_context->gizmo_translate_y) {
				g_context->translate_y = true;
			}
			else if (hit->object == g_context->gizmo_translate_z) {
				g_context->translate_z = true;
			}
			else if (hit->object == g_context->gizmo_scale_x) {
				g_context->scale_x = true;
			}
			else if (hit->object == g_context->gizmo_scale_y) {
				g_context->scale_y = true;
			}
			else if (hit->object == g_context->gizmo_scale_z) {
				g_context->scale_z = true;
			}
			if (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y || g_context->scale_z) {
				g_context->gizmo_offset  = 0.0;
				g_context->gizmo_started = true;
			}
		}
		else {
			// Rotate
			transform_t_array_t *trs = any_array_create_from_raw(
			    (void *[]){
			        g_context->gizmo_rotate_x->transform,
			        g_context->gizmo_rotate_y->transform,
			        g_context->gizmo_rotate_z->transform,
			    },
			    3);
			transform_t *hit = raycast_closest_box_intersect(trs, mouse_view_x(), mouse_view_y(), scene_camera);
			if (hit != NULL) {
				if (hit->object == g_context->gizmo_rotate_x) {
					g_context->rotate_x = true;
				}
				else if (hit->object == g_context->gizmo_rotate_y) {
					g_context->rotate_y = true;
				}
				else if (hit->object == g_context->gizmo_rotate_z) {
					g_context->rotate_z = true;
				}
				if (g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) {
					g_context->gizmo_offset  = 0.0;
					g_context->gizmo_started = true;
				}
			}
			else if (mouse_view_x() < base_w()) {
				g_context->pick_object_id = true;
				if (is_object) {
					gizmo_rect_start();
				}
			}
		}
	}
	else if (mouse_released("left")) {
		g_context->pick_object_id = false;
		g_context->translate_x = g_context->translate_y = g_context->translate_z = false;
		g_context->scale_x = g_context->scale_y = g_context->scale_z = false;
		g_context->rotate_x = g_context->rotate_y = g_context->rotate_z = false;

		if (gizmo_undo_stored) {
			gizmo_undo_stored    = false;
			transform_t *t       = paint_object->transform;
			bool         changed = gizmo_others_end();
			if (!vec4_equals(t->loc, gizmo_undo_loc) || !gizmo_quat_equals(t->rot, gizmo_undo_rot) || !vec4_equals(t->scale, gizmo_undo_scale)) {
				history_object_transform(g_context->paint_object, gizmo_undo_loc, gizmo_undo_rot, gizmo_undo_scale);
				changed = true;
			}
			if (changed) {
				util_mesh_transform_changed();
			}
		}
		if (render_path_raytrace_moving) {
			render_path_raytrace_moving = false;
			render_path_raytrace_ready  = false;
		}
	}

	if (g_context->gizmo_started) {
		g_context->pick_object_id = false;
	}

	if (is_object && g_context->gizmo_started) {
		transform_t *t    = paint_object->transform;
		gizmo_undo_loc    = t->loc;
		gizmo_undo_rot    = t->rot;
		gizmo_undo_scale  = t->scale;
		gizmo_undo_stored = true;
		gizmo_others_begin();
	}

	if (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y || g_context->scale_z ||
	    g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) {
		if (is_object) {
			transform_t *t = paint_object->transform;
			gizmo_v        = (vec4_t){transform_world_x(t), transform_world_y(t), transform_world_z(t), 1.0};
		}
		else if (is_decal) {
			gizmo_v = (vec4_t){g_context->layer->decal_mat.m30, g_context->layer->decal_mat.m31, g_context->layer->decal_mat.m32, 1.0};
		}

		if (!g_context->gizmo_started) {
			g_context->gizmo_drag = gizmo_drag_raw;
		}

		// Project the world axis into screen space and map per-frame mouse delta onto it
		if (g_context->translate_x || g_context->scale_x) {
			if (g_context->gizmo_started) {
				g_context->gizmo_drag = gizmo_v.x;
			}
			vec4_t s0   = vec4_apply_proj(gizmo_v, scene_camera->vp);
			vec4_t s1   = vec4_apply_proj((vec4_t){gizmo_v.x + 1.0, gizmo_v.y, gizmo_v.z, 1.0}, scene_camera->vp);
			f32    sax  = (s1.x - s0.x) * (f32)base_w() * 0.5;
			f32    say  = -(s1.y - s0.y) * (f32)base_h() * 0.5;
			f32    len2 = sax * sax + say * say;
			if (len2 > 0.000001) {
				g_context->gizmo_drag += (mouse_movement_x * sax + mouse_movement_y * say) / len2;
			}
		}
		else if (g_context->translate_y || g_context->scale_y) {
			if (g_context->gizmo_started) {
				g_context->gizmo_drag = gizmo_v.y;
			}
			vec4_t s0   = vec4_apply_proj(gizmo_v, scene_camera->vp);
			vec4_t s1   = vec4_apply_proj((vec4_t){gizmo_v.x, gizmo_v.y + 1.0, gizmo_v.z, 1.0}, scene_camera->vp);
			f32    sax  = (s1.x - s0.x) * (f32)base_w() * 0.5;
			f32    say  = -(s1.y - s0.y) * (f32)base_h() * 0.5;
			f32    len2 = sax * sax + say * say;
			if (len2 > 0.000001) {
				g_context->gizmo_drag += (mouse_movement_x * sax + mouse_movement_y * say) / len2;
			}
		}
		else if (g_context->translate_z || g_context->scale_z) {
			if (g_context->gizmo_started) {
				g_context->gizmo_drag = gizmo_v.z;
			}
			vec4_t s0   = vec4_apply_proj(gizmo_v, scene_camera->vp);
			vec4_t s1   = vec4_apply_proj((vec4_t){gizmo_v.x, gizmo_v.y, gizmo_v.z + 1.0, 1.0}, scene_camera->vp);
			f32    sax  = (s1.x - s0.x) * (f32)base_w() * 0.5;
			f32    say  = -(s1.y - s0.y) * (f32)base_h() * 0.5;
			f32    len2 = sax * sax + say * say;
			if (len2 > 0.000001) {
				g_context->gizmo_drag += (mouse_movement_x * sax + mouse_movement_y * say) / len2;
			}
		}
		else if (g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) {
			// Project the two ring basis axes into screen pixels
			vec4_t s0 = vec4_apply_proj(gizmo_v, scene_camera->vp);
			f32    cx = (s0.x * 0.5 + 0.5) * (f32)base_w();
			f32    cy = (-s0.y * 0.5 + 0.5) * (f32)base_h();
			vec4_t e1p, e2p;
			if (g_context->rotate_x) {
				// Ring in YZ plane
				e1p = vec4_apply_proj((vec4_t){gizmo_v.x, gizmo_v.y, gizmo_v.z + 1.0, 1.0}, scene_camera->vp);
				e2p = vec4_apply_proj((vec4_t){gizmo_v.x, gizmo_v.y + 1.0, gizmo_v.z, 1.0}, scene_camera->vp);
			}
			else if (g_context->rotate_y) {
				// Ring in XZ plane
				e1p = vec4_apply_proj((vec4_t){gizmo_v.x + 1.0, gizmo_v.y, gizmo_v.z, 1.0}, scene_camera->vp);
				e2p = vec4_apply_proj((vec4_t){gizmo_v.x, gizmo_v.y, gizmo_v.z + 1.0, 1.0}, scene_camera->vp);
			}
			else {
				// Ring in XY plane
				e1p = vec4_apply_proj((vec4_t){gizmo_v.x + 1.0, gizmo_v.y, gizmo_v.z, 1.0}, scene_camera->vp);
				e2p = vec4_apply_proj((vec4_t){gizmo_v.x, gizmo_v.y + 1.0, gizmo_v.z, 1.0}, scene_camera->vp);
			}
			f32 e1x = (e1p.x * 0.5 + 0.5) * (f32)base_w() - cx;
			f32 e1y = (-e1p.y * 0.5 + 0.5) * (f32)base_h() - cy;
			f32 e2x = (e2p.x * 0.5 + 0.5) * (f32)base_w() - cx;
			f32 e2y = (-e2p.y * 0.5 + 0.5) * (f32)base_h() - cy;
			f32 det = e1x * e2y - e1y * e2x;
			if (math_abs(det) > 0.000001) {
				f32 mx = mouse_view_x() - cx;
				f32 my = mouse_view_y() - cy;
				f32 u  = (mx * e2y - my * e2x) / det;
				f32 v  = (e1x * my - e1y * mx) / det;
				if (g_context->gizmo_started) {
					mat4_decomposed_t *dec  = mat4_decompose(g_context->layer->decal_mat);
					gizmo_v                 = dec->loc;
					gizmo_q                 = dec->rot;
					gizmo_v0                = dec->scl;
					g_context->gizmo_offset = math_atan2(v, u);
				}
				g_context->gizmo_drag = math_atan2(v, u) - g_context->gizmo_offset;
			}
		}

		gizmo_drag_raw = g_context->gizmo_drag;
		if (keymap_shortcut(any_map_get(g_keymap, "grid_snap"), SHORTCUT_TYPE_DOWN) || g_config->grid_snap) {
			bool is_rotate             = g_context->rotate_x || g_context->rotate_y || g_context->rotate_z;
			f32  step                  = is_rotate ? IRON_PI / 4.0 : 0.5; // 45 degrees / 0.5 units
			g_context->gizmo_drag      = math_round(g_context->gizmo_drag / step) * step;
			g_context->gizmo_drag_last = math_round(g_context->gizmo_drag_last / step) * step;
		}

		if (g_context->gizmo_started) {
			g_context->gizmo_drag_last = g_context->gizmo_drag;
		}
	}

	_input_occupied = (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y ||
	                   g_context->scale_z || g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) &&
	                  mouse_view_x() < base_w();
}
