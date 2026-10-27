
#include <iron.h>

void camera_update() {
	camera_object_t *camera        = scene_camera;
	bool             move_forward  = keyboard_down("w");
	bool             move_backward = keyboard_down("s");
	bool             strafe_left   = keyboard_down("a");
	bool             strafe_right  = keyboard_down("d");
	bool             strafe_up     = keyboard_down("e");
	bool             strafe_down   = keyboard_down("q");
	f32              fast          = keyboard_down("shift") ? 2.0 : 1.0;
	f32              speed         = 5.0;
	vec4_t           dir           = (vec4_t){0.0, 0.0, 0.0, 1.0};

	if (move_forward || move_backward || strafe_right || strafe_left || strafe_up || strafe_down) {
		vec4_t clook  = camera_object_look(camera);
		vec4_t cright = camera_object_right(camera);
		if (move_forward) {
			dir = vec4_fadd(dir, clook.x, clook.y, clook.z, 0.0);
		}
		if (move_backward) {
			dir = vec4_fadd(dir, -clook.x, -clook.y, -clook.z, 0.0);
		}
		if (strafe_right) {
			dir = vec4_fadd(dir, cright.x, cright.y, cright.z, 0.0);
		}
		if (strafe_left) {
			dir = vec4_fadd(dir, -cright.x, -cright.y, -cright.z, 0.0);
		}
		if (strafe_up) {
			dir = vec4_fadd(dir, 0, 0, 1, 0.0);
		}
		if (strafe_down) {
			dir = vec4_fadd(dir, 0, 0, -1, 0.0);
		}
	}

	f32 d = sys_delta() * speed * fast;
	if (d > 0.0) {
		transform_move(camera->base->transform, dir, d);
	}

	if (mouse_down("left")) {
		transform_rotate(camera->base->transform, vec4_z_axis(), -mouse_movement_x / (float)200);
		transform_rotate(camera->base->transform, camera_object_right(camera), -mouse_movement_y / (float)200);
	}
}
