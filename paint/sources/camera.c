
#include "global.h"

i32    camera_redraws       = 0;
f32    camera_ease          = 1.0;
bool   camera_controls_down = false;
vec4_t camera_dir           = (vec4_t){0.0, 0.0, 0.0, 1.0};

static bool camera_wrap_mouse() {
	if (mouse_view_x() < 0 || mouse_view_x() > sys_w() || mouse_view_y() < 0 || mouse_view_y() > sys_h()) {
		if (!g_config->wrap_mouse || !camera_controls_down) {
			return false;
		}
		if (mouse_view_x() < 0) {
			mouse_x = mouse_last_x = sys_x() + sys_w();
			iron_mouse_set_position(math_floor(mouse_x), math_floor(mouse_y));
		}
		else if (mouse_view_x() > sys_w()) {
			mouse_x = mouse_last_x = sys_x();
			iron_mouse_set_position(math_floor(mouse_x), math_floor(mouse_y));
		}
		else if (mouse_view_y() < 0) {
			mouse_y = mouse_last_y = sys_y() + sys_h();
			iron_mouse_set_position(math_floor(mouse_x), math_floor(mouse_y));
		}
		else if (mouse_view_y() > sys_h()) {
			mouse_y = mouse_last_y = sys_y();
			iron_mouse_set_position(math_floor(mouse_x), math_floor(mouse_y));
		}
	}
	return true;
}

static void camera_orbit_action(bool modif, bool default_keymap, bool ruler) {
	camera_object_t *camera = scene_camera;
	camera_redraws          = 2;
	f32              move_y = ruler ? 0.0 : mouse_movement_y;

	if (g_context->camera_pivot == CAMERA_PIVOT_CURSOR) {
		static f32  pivot_x     = 0;
		static f32  pivot_y     = 0;
		static f32  pivot_z     = 0;
		static bool pivot_valid = false;

		bool rotate_started =
		    operator_shortcut(any_map_get(g_keymap, "action_rotate"), SHORTCUT_TYPE_STARTED) || (mouse_started("right") && !modif && default_keymap);
		if (rotate_started) {
			util_render_pick_pos_nor_tex();
			pivot_valid = math_abs(g_context->posx_picked) < 50 && math_abs(g_context->posy_picked) < 50 && math_abs(g_context->posz_picked) < 50;
			if (pivot_valid) {
				pivot_x = g_context->posx_picked;
				pivot_y = g_context->posy_picked;
				pivot_z = g_context->posz_picked;
			}
		}

		if (pivot_valid) {
			f32 angle_x = -mouse_movement_x / 100.0 * g_config->camera_rotation_speed;
			f32 angle_y = -move_y / 100.0 * g_config->camera_rotation_speed;

			transform_rotate(camera->base->transform, vec4_z_axis(), angle_x);
			vec4_t right = camera_object_right_world(camera);
			transform_rotate(camera->base->transform, right, angle_y);
			vec4_t up_world = camera_object_up_world(camera);
			if (up_world.z < 0 && !g_config->camera_upside_down) {
				transform_rotate(camera->base->transform, right, -angle_y);
				angle_y = 0.0;
			}

			vec4_t offset =
			    (vec4_t){camera->base->transform->loc.x - pivot_x, camera->base->transform->loc.y - pivot_y, camera->base->transform->loc.z - pivot_z, 0.0};
			quat_t q_x                     = quat_from_axis_angle(vec4_z_axis(), angle_x);
			quat_t q_y                     = quat_from_axis_angle(right, angle_y);
			offset                         = vec4_apply_quat(offset, q_x);
			offset                         = vec4_apply_quat(offset, q_y);
			camera->base->transform->loc.x = pivot_x + offset.x;
			camera->base->transform->loc.y = pivot_y + offset.y;
			camera->base->transform->loc.z = pivot_z + offset.z;
			camera_object_build_mat(camera);
		}
		return;
	}

	f32 dist = camera_distance();
	transform_move(camera->base->transform, camera_object_look_world(camera), dist);
	transform_rotate(camera->base->transform, vec4_z_axis(), -mouse_movement_x / 100.0 * g_config->camera_rotation_speed);
	transform_rotate(camera->base->transform, camera_object_right_world(camera), -move_y / 100.0 * g_config->camera_rotation_speed);
	vec4_t up_world = camera_object_up_world(camera);
	if (up_world.z < 0 && !g_config->camera_upside_down) {
		transform_rotate(camera->base->transform, camera_object_right_world(camera), move_y / 100.0 * g_config->camera_rotation_speed);
	}
	transform_move(camera->base->transform, camera_object_look_world(camera), -dist);
}

static void camera_rotate_action(bool modif, bool default_keymap, bool ruler) {
	camera_object_t *camera = scene_camera;
	camera_redraws          = 2;
	f32              move_y = ruler ? 0.0 : mouse_movement_y;
	transform_t     *t      = context_main_object()->base->transform;
	vec4_t           up     = transform_up(t);

	if (g_context->camera_pivot == CAMERA_PIVOT_CURSOR) {
		static f32  pivot_x     = 0;
		static f32  pivot_y     = 0;
		static f32  pivot_z     = 0;
		static bool pivot_valid = false;

		bool rotate_started =
		    operator_shortcut(any_map_get(g_keymap, "action_rotate"), SHORTCUT_TYPE_STARTED) || (mouse_started("right") && !modif && default_keymap);
		if (rotate_started) {
			util_render_pick_pos_nor_tex();
			pivot_valid = math_abs(g_context->posx_picked) < 50 && math_abs(g_context->posy_picked) < 50 && math_abs(g_context->posz_picked) < 50;
			if (pivot_valid) {
				pivot_x = g_context->posx_picked;
				pivot_y = g_context->posy_picked;
				pivot_z = g_context->posz_picked;
			}
		}

		if (pivot_valid) {
			f32    angle_x = (mouse_movement_x / 120.0) * g_config->camera_rotation_speed;
			f32    angle_y = (move_y / 120.0) * g_config->camera_rotation_speed;
			vec4_t right   = camera_object_right_world(camera);

			transform_rotate(t, up, angle_x);
			transform_rotate(t, right, angle_y);
			transform_build_matrix(t);
			vec4_t tup = transform_up(t);
			if (tup.z < 0 && !g_config->camera_upside_down) {
				transform_rotate(t, right, -angle_y);
				angle_y = 0.0;
			}

			vec4_t offset = (vec4_t){t->loc.x - pivot_x, t->loc.y - pivot_y, t->loc.z - pivot_z, 0.0};
			quat_t q_x    = quat_from_axis_angle(up, angle_x);
			quat_t q_y    = quat_from_axis_angle(right, angle_y);
			offset        = vec4_apply_quat(offset, q_x);
			offset        = vec4_apply_quat(offset, q_y);
			t->loc.x      = pivot_x + offset.x;
			t->loc.y      = pivot_y + offset.y;
			t->loc.z      = pivot_z + offset.z;
			transform_build_matrix(t);
		}
		return;
	}

	transform_rotate(t, up, (mouse_movement_x / 120.0) * g_config->camera_rotation_speed);
	vec4_t right = camera_object_right_world(camera);
	transform_rotate(t, right, (move_y / 120.0) * g_config->camera_rotation_speed);
	transform_build_matrix(t);
	vec4_t tup = transform_up(t);
	if (tup.z < 0 && !g_config->camera_upside_down) {
		transform_rotate(t, right, -(move_y / 120.0) * g_config->camera_rotation_speed);
	}
}

f32 camera_get_zoom_speed() {
	i32              sign   = g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL_INVERTED || g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL_INVERTED ||
                       g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL_HORIZONTAL_INVERTED
	                              ? -1
	                              : 1;
	camera_object_t *camera = scene_camera;
	f32              fov_adjust = camera->data->fov;
	return (g_config->camera_zoom_speed * sign) / (float)fov_adjust;
}

f32 camera_get_zoom_delta() {
	return g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL              ? -mouse_movement_y
	       : g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL_INVERTED   ? -mouse_movement_y
	       : g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL          ? mouse_movement_x
	       : g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL_INVERTED ? mouse_movement_x
	                                                                        : -(mouse_movement_y - mouse_movement_x);
}

static void camera_zoom_action(bool modif_key) {
	camera_object_t *camera = scene_camera;

	if (operator_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_DOWN)) {
		camera_redraws = 2;
		f32 f          = camera_get_zoom_delta() / (float)(150.0 * (1.0 / (camera_distance() / 2.0)));
		f *= camera_get_zoom_speed();
		transform_move(camera->base->transform, camera_object_look(camera), f);
	}

	if (mouse_wheel_delta != 0 && !modif_key) {
		camera_redraws = 2;
		f32 f          = mouse_wheel_delta * (-0.2) * ((camera_distance() / 4.0));
		f *= camera_get_zoom_speed();
		transform_move(camera->base->transform, camera_object_look(camera), f);
	}
}

static void camera_fly_action(bool modif_key) {
	camera_object_t *camera = scene_camera;

	bool move_forward  = keyboard_down("w") || keyboard_down("up") || mouse_wheel_delta < 0;
	bool move_backward = keyboard_down("s") || keyboard_down("down") || mouse_wheel_delta > 0;
	bool strafe_left   = keyboard_down("a") || keyboard_down("left");
	bool strafe_right  = keyboard_down("d") || keyboard_down("right");
	bool strafe_up     = keyboard_down("e");
	bool strafe_down   = keyboard_down("q");
	f32  fast          = keyboard_down("shift") ? 2.0 : (keyboard_down("alt") ? 0.5 : 1.0);
	if (mouse_wheel_delta != 0) {
		fast *= math_abs(mouse_wheel_delta) * 4.0;
	}

	if (move_forward || move_backward || strafe_right || strafe_left || strafe_up || strafe_down) {
		camera_ease += sys_delta() * 15;
		if (camera_ease > 1.0) {
			camera_ease = 1.0;
		}
		camera_dir   = (vec4_t){0, 0, 0, 1.0};
		vec4_t look  = camera_object_look(camera);
		vec4_t right = camera_object_right(camera);
		if (move_forward) {
			camera_dir = vec4_fadd(camera_dir, look.x, look.y, look.z, 0.0);
		}
		if (move_backward) {
			camera_dir = vec4_fadd(camera_dir, -look.x, -look.y, -look.z, 0.0);
		}
		if (strafe_right) {
			camera_dir = vec4_fadd(camera_dir, right.x, right.y, right.z, 0.0);
		}
		if (strafe_left) {
			camera_dir = vec4_fadd(camera_dir, -right.x, -right.y, -right.z, 0.0);
		}
		if (strafe_up) {
			camera_dir = vec4_fadd(camera_dir, 0, 0, 1, 0.0);
		}
		if (strafe_down) {
			camera_dir = vec4_fadd(camera_dir, 0, 0, -1, 0.0);
		}
	}
	else {
		camera_ease -= sys_delta() * 20.0 * camera_ease;
		if (camera_ease < 0.0) {
			camera_ease = 0.0;
		}
	}

	f32 d = sys_delta() * fast * camera_ease * 2.0 * ((move_forward || move_backward) ? g_config->camera_zoom_speed : g_config->camera_pan_speed);
	if (d > 0.0) {
		transform_move(camera->base->transform, camera_dir, d);
		if (g_context->camera_type == CAMERA_TYPE_ORTHOGRAPHIC) {
			viewport_update_camera_type(g_context->camera_type);
		}
	}

	camera_redraws = 2;
	transform_rotate(camera->base->transform, vec4_z_axis(), -mouse_movement_x / 200.0 * g_config->camera_rotation_speed);
	transform_rotate(camera->base->transform, camera_object_right(camera), -mouse_movement_y / 200.0 * g_config->camera_rotation_speed);
}

void camera_init() {
	camera_reset(-1);
}

i32 camera_index() {
	return g_context->view_index_last > 0 ? 1 : 0;
}

void camera_pan_action(bool modif, bool default_keymap) {
	camera_object_t *camera = scene_camera;
	if (operator_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_DOWN) || (mouse_down("middle") && !modif && default_keymap)) {
		camera_redraws                 = 2;
		f32    f                       = 150 * (1.0 / (float)(camera_distance() / 4.0));
		vec4_t look                    = vec4_mult(transform_look(camera->base->transform), mouse_movement_y / (float)f * g_config->camera_pan_speed);
		vec4_t right                   = vec4_mult(transform_right(camera->base->transform), -mouse_movement_x / (float)f * g_config->camera_pan_speed);
		camera->base->transform->loc   = vec4_add(camera->base->transform->loc, look);
		camera->base->transform->loc   = vec4_add(camera->base->transform->loc, right);
		camera_origins[camera_index()] = vec4_add(camera_origins[camera_index()], look);
		camera_origins[camera_index()] = vec4_add(camera_origins[camera_index()], right);
		camera_object_build_mat(camera);
	}
}

void camera_set_pivot_center_to_mouse() {
	util_render_pick_pos_nor_tex();
	bool is_mesh = math_abs(g_context->posx_picked) < 50 && math_abs(g_context->posy_picked) < 50 && math_abs(g_context->posz_picked) < 50;
	if (!is_mesh) {
		return;
	}

	camera_origins[camera_index()].x = g_context->posx_picked;
	camera_origins[camera_index()].y = g_context->posy_picked;
	camera_origins[camera_index()].z = g_context->posz_picked;

	camera_redraws                 = 2;
	camera_object_t *camera        = scene_camera;
	vec4_t           up            = vec4_mult(transform_up(camera->base->transform), camera_distance());
	camera->base->transform->loc.x = g_context->posx_picked;
	camera->base->transform->loc.y = g_context->posy_picked;
	camera->base->transform->loc.z = g_context->posz_picked;
	camera->base->transform->loc   = vec4_add(camera->base->transform->loc, up);
	camera_object_build_mat(camera);
}

void camera_update(void *_) {
	if (!camera_wrap_mouse() || g_context->camera_controls == CAMERA_CONTROLS_NONE) {
		return;
	}

	bool modif_key      = keyboard_down("alt") || keyboard_down("shift") || keyboard_down("control");
	bool modif          = modif_key || string_equals(any_map_get(g_keymap, "action_rotate"), "middle");
	bool default_keymap = string_equals(g_config->keymap, "default.json");
	// Restrict the rotation to the horizontal axis
	bool ruler = keyboard_down(any_map_get(g_keymap, "brush_ruler"));

	if (operator_shortcut(any_map_get(g_keymap, "action_rotate"), SHORTCUT_TYPE_STARTED) ||
	    operator_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_STARTED) ||
	    operator_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_STARTED) ||
	    operator_shortcut(any_map_get(g_keymap, "rotate_envmap"), SHORTCUT_TYPE_STARTED) || (mouse_started("right") && (!modif || ruler)) ||
	    (mouse_started("middle") && !modif) || (mouse_wheel_delta != 0 && !modif_key)) {
		camera_controls_down = true;
	}
	else if (!operator_shortcut(any_map_get(g_keymap, "action_rotate"), SHORTCUT_TYPE_DOWN) &&
	         !operator_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_DOWN) &&
	         !operator_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_DOWN) &&
	         !operator_shortcut(any_map_get(g_keymap, "rotate_envmap"), SHORTCUT_TYPE_DOWN) && !(mouse_down("right") && !modif) &&
	         !(mouse_down("middle") && !modif) && (mouse_wheel_delta == 0 && !modif_key)) {
		camera_controls_down = false;
	}

	if (_input_occupied || !base_ui_enabled || base_is_dragging || g_ui->is_scrolling || g_ui->combo_selected_handle != NULL || !camera_controls_down) {
		return;
	}

	camera_controls_t controls = g_context->camera_controls;

	bool rotate_down =
	    operator_shortcut(any_map_get(g_keymap, "action_rotate"), SHORTCUT_TYPE_DOWN) || (mouse_down("right") && (!modif || ruler) && default_keymap);

	if (controls == CAMERA_CONTROLS_ORBIT && rotate_down) {
		camera_orbit_action(modif, default_keymap, ruler);
	}
	else if (controls == CAMERA_CONTROLS_ROTATE && rotate_down) {
		camera_rotate_action(modif, default_keymap, ruler);
	}

	if (controls == CAMERA_CONTROLS_ROTATE || controls == CAMERA_CONTROLS_ORBIT) {
		camera_pan_action(modif, default_keymap);
		camera_zoom_action(modif_key);
	}
	else if (controls == CAMERA_CONTROLS_FLY && mouse_down("right")) {
		camera_fly_action(modif_key);
	}

	if (operator_shortcut(any_map_get(g_keymap, "view_pivot_center"), SHORTCUT_TYPE_STARTED)) {
		camera_set_pivot_center_to_mouse();
	}

	if (operator_shortcut(any_map_get(g_keymap, "rotate_envmap"), SHORTCUT_TYPE_DOWN)) {
		camera_redraws = 2;
		g_context->envmap_angle -= mouse_movement_x / 100.0;
	}

	if (camera_redraws > 0) {
		camera_redraws--;

		if (!ui_menu_show) {
			g_context->ddirty = 2;
		}

		if (g_context->camera_type == CAMERA_TYPE_ORTHOGRAPHIC) {
			viewport_update_camera_type(g_context->camera_type);
		}
	}
}

f32 camera_distance() {
	camera_object_t *camera = scene_camera;
	return vec4_dist(camera_origins[camera_index()], camera->base->transform->loc);
}

void camera_reset(i32 view_index) {
	camera_object_t *camera = scene_camera;
	if (view_index == -1) {
		camera_origins[0] = (vec4_t){0, 0, 0, 1};
		camera_origins[1] = (vec4_t){0, 0, 0, 1};

		camera_views[0] = camera->base->transform->local;
		camera_views[1] = camera->base->transform->local;
	}
	else {
		camera_origins[view_index] = (vec4_t){0, 0, 0, 1.0};
		camera_views[view_index]   = camera->base->transform->local;
	}

#ifdef IRON_IOS
	if (config_is_iphone()) {
		viewport_zoom(-2.8);
		vec4_t right                   = vec4_mult(transform_right(camera->base->transform), -0.135);
		camera->base->transform->loc   = vec4_add(camera->base->transform->loc, right);
		camera_origins[camera_index()] = vec4_add(camera_origins[camera_index()], right);
		camera_object_build_mat(camera);
	}
#endif
}
