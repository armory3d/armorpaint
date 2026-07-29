
#include "../global.h"

static vec4_t gizmo_v  = (vec4_t){0.0, 0.0, 0.0, 1.0};
static vec4_t gizmo_v0 = (vec4_t){0.0, 0.0, 0.0, 1.0};
static quat_t gizmo_q  = (quat_t){0.0, 0.0, 0.0, 1.0};
static quat_t gizmo_q0 = (quat_t){0.0, 0.0, 0.0, 1.0};

void render_gizmo_update() {
	bool is_object = g_context->tool == TOOL_TYPE_CURSOR;
	bool is_decal  = base_is_decal_layer();

	object_t *gizmo = g_context->gizmo;
	bool      hide  = operator_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN);
	gizmo->visible  = (is_object || is_decal) && !hide && g_config->workspace != WORKSPACE_PLAYER;
	if (!gizmo->visible) {
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
				paint_object->transform->scale.x += g_context->gizmo_drag - g_context->gizmo_drag_last;
			}
			else if (g_context->scale_y) {
				paint_object->transform->scale.y += g_context->gizmo_drag - g_context->gizmo_drag_last;
			}
			else if (g_context->scale_z) {
				paint_object->transform->scale.z += g_context->gizmo_drag - g_context->gizmo_drag_last;
			}
			else if (g_context->rotate_x) {
				gizmo_q0                     = quat_from_axis_angle(vec4_x_axis(), -(g_context->gizmo_drag - g_context->gizmo_drag_last));
				paint_object->transform->rot = quat_mult(paint_object->transform->rot, gizmo_q0);
			}
			else if (g_context->rotate_y) {
				gizmo_q0                     = quat_from_axis_angle(vec4_y_axis(), -(g_context->gizmo_drag - g_context->gizmo_drag_last));
				paint_object->transform->rot = quat_mult(paint_object->transform->rot, gizmo_q0);
			}
			else if (g_context->rotate_z) {
				gizmo_q0                     = quat_from_axis_angle(vec4_z_axis(), g_context->gizmo_drag - g_context->gizmo_drag_last);
				paint_object->transform->rot = quat_mult(paint_object->transform->rot, gizmo_q0);
			}
			g_context->gizmo_drag_last = g_context->gizmo_drag;

			transform_build_matrix(paint_object->transform);

			asim_body_t *pb = paint_object->_->body;
			if (pb != NULL) {
				asim_body_sync_transform(pb);
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
				mat4_decomposed_t *dec = mat4_decompose(g_context->layer->decal_mat);
				gizmo_v                = dec->loc;
				gizmo_q                = dec->rot;
				gizmo_v0               = dec->scl;
				gizmo_v0.x += g_context->gizmo_drag - g_context->gizmo_drag_last;
				g_context->layer->decal_mat = mat4_compose(gizmo_v, gizmo_q, gizmo_v0);
			}
			else if (g_context->scale_y) {
				mat4_decomposed_t *dec = mat4_decompose(g_context->layer->decal_mat);
				gizmo_v                = dec->loc;
				gizmo_q                = dec->rot;
				gizmo_v0               = dec->scl;
				gizmo_v0.y += g_context->gizmo_drag - g_context->gizmo_drag_last;
				g_context->layer->decal_mat = mat4_compose(gizmo_v, gizmo_q, gizmo_v0);
			}
			else if (g_context->scale_z) {
				mat4_decomposed_t *dec = mat4_decompose(g_context->layer->decal_mat);
				gizmo_v                = dec->loc;
				gizmo_q                = dec->rot;
				gizmo_v0               = dec->scl;
				gizmo_v0.z += g_context->gizmo_drag - g_context->gizmo_drag_last;
				g_context->layer->decal_mat = mat4_compose(gizmo_v, gizmo_q, gizmo_v0);
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
			}
		}
	}
	else if (mouse_released("left")) {
		g_context->translate_x = g_context->translate_y = g_context->translate_z = false;
		g_context->scale_x = g_context->scale_y = g_context->scale_z = false;
		g_context->rotate_x = g_context->rotate_y = g_context->rotate_z = false;
	}

	if (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y || g_context->scale_z ||
	    g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) {
		g_context->rdirty = 2;
		if (is_object) {
			transform_t *t = paint_object->transform;
			gizmo_v        = (vec4_t){transform_world_x(t), transform_world_y(t), transform_world_z(t), 1.0};
		}
		else if (is_decal) {
			gizmo_v = (vec4_t){g_context->layer->decal_mat.m30, g_context->layer->decal_mat.m31, g_context->layer->decal_mat.m32, 1.0};
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

		if (g_context->gizmo_started) {
			g_context->gizmo_drag_last = g_context->gizmo_drag;
		}
	}

	_input_occupied = (g_context->translate_x || g_context->translate_y || g_context->translate_z || g_context->scale_x || g_context->scale_y ||
	                   g_context->scale_z || g_context->rotate_x || g_context->rotate_y || g_context->rotate_z) &&
	                  mouse_view_x() < base_w();
}
