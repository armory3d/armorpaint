
#include "../global.h"

#define TPC_SENSITIVITY  0.0025
#define TPC_PITCH_MIN    (-1.3)
#define TPC_PITCH_MAX    0.6
#define TPC_SPEED        2.0
#define TPC_CAMERA_DIST  3.5
#define TPC_CAMERA_FOCUS 0.45

static char *trait_third_person_controller_object = NULL;
static f32   trait_third_person_controller_yaw    = 0.0;
static f32   trait_third_person_controller_pitch  = 0.0;
static bool  trait_third_person_controller_moving = false;

void trait_third_person_controller_init(char *object) {
	gc_unroot(trait_third_person_controller_object);
	trait_third_person_controller_object = string_copy(object);
	gc_root(trait_third_person_controller_object);
	trait_third_person_controller_moving = false;
}

static quat_t trait_third_person_controller_facing() {
	return quat_from_axis_angle(vec4_z_axis(), trait_third_person_controller_yaw + math_pi());
}

static vec4_t trait_third_person_controller_look() {
	f32 cp = math_cos(trait_third_person_controller_pitch);
	return (vec4_t){-math_sin(trait_third_person_controller_yaw) * cp, math_cos(trait_third_person_controller_yaw) * cp,
	                math_sin(trait_third_person_controller_pitch), 0.0};
}

static void trait_third_person_controller_grab() {
	vec4_t look                         = camera_object_look_world(scene_camera);
	trait_third_person_controller_yaw   = math_atan2(-look.x, look.y);
	trait_third_person_controller_pitch = math_asin(math_max(math_min(look.z, 1.0), -1.0));
	trait_third_person_controller_pitch = math_max(math_min(trait_third_person_controller_pitch, TPC_PITCH_MAX), TPC_PITCH_MIN);
	iron_mouse_lock();
}

static void trait_third_person_controller_rotate() {
	if (mouse_movement_x == 0 && mouse_movement_y == 0) {
		return;
	}
	trait_third_person_controller_yaw -= mouse_movement_x * TPC_SENSITIVITY;
	trait_third_person_controller_pitch -= mouse_movement_y * TPC_SENSITIVITY;
	trait_third_person_controller_pitch = math_max(math_min(trait_third_person_controller_pitch, TPC_PITCH_MAX), TPC_PITCH_MIN);
}

static object_t *trait_third_person_controller_run_object(object_t *o) {
	return script_get_object(string("%s_run", o->name));
}

static void trait_third_person_controller_set_moving(object_t *o, bool moving) {
	if (moving == trait_third_person_controller_moving) {
		return;
	}
	object_t *run = trait_third_person_controller_run_object(o);
	if (run == NULL) {
		return;
	}
	trait_third_person_controller_moving = moving;
	o->visible                           = !moving;
	run->visible                         = moving;
}

static void trait_third_person_controller_place(physics_body_t *body) {
	if (!trait_third_person_controller_moving) {
		return;
	}
	object_t *run = trait_third_person_controller_run_object(body->obj);
	if (run == NULL) {
		return;
	}

	vec4_t pos;
	physics_body_get_pos(body->_body, &pos);
	quat_t rot = trait_third_person_controller_facing();
	vec4_t off = vec4_apply_quat(body->offset, rot);

	transform_t *t = run->transform;
	t->loc         = (vec4_t){pos.x - off.x, pos.y - off.y, pos.z - off.z, 1.0};
	t->rot         = rot;
	transform_build_matrix(t);
}

static bool trait_third_person_controller_move(physics_body_t *body) {
	f32    s       = math_sin(trait_third_person_controller_yaw);
	f32    c       = math_cos(trait_third_person_controller_yaw);
	vec4_t forward = (vec4_t){-s, c, 0.0, 0.0};
	vec4_t right   = (vec4_t){c, s, 0.0, 0.0};
	vec4_t dir     = (vec4_t){0.0, 0.0, 0.0, 0.0};

	if (keyboard_down("w")) {
		dir = vec4_add(dir, forward);
	}
	if (keyboard_down("s")) {
		dir = vec4_sub(dir, forward);
	}
	if (keyboard_down("d")) {
		dir = vec4_add(dir, right);
	}
	if (keyboard_down("a")) {
		dir = vec4_sub(dir, right);
	}

	vec4_t vel;
	physics_body_get_velocity(body->_body, &vel);
	bool moving = dir.x != 0.0 || dir.y != 0.0;
	if (moving) {
		dir = vec4_norm(dir);
		physics_body_set_velocity(body->_body, dir.x * TPC_SPEED, dir.y * TPC_SPEED, vel.z);
	}
	else {
		physics_body_set_velocity(body->_body, 0.0, 0.0, vel.z);
	}

	physics_body_set_rotation(body, trait_third_person_controller_facing());
	return moving;
}

static void trait_third_person_controller_follow(physics_body_t *body) {
	vec4_t pos;
	physics_body_get_pos(body->_body, &pos);

	f32    size  = math_sqrt(body->dimx * body->dimx + body->dimy * body->dimy + body->dimz * body->dimz);
	f32    dist  = (size > 0.0 ? size : 1.0) * TPC_CAMERA_DIST;
	vec4_t look  = trait_third_person_controller_look();
	vec4_t focus = (vec4_t){pos.x, pos.y, pos.z + body->dimz * TPC_CAMERA_FOCUS, 1.0};

	transform_t *ct = scene_camera->base->transform;
	ct->loc         = (vec4_t){focus.x - look.x * dist, focus.y - look.y * dist, focus.z - look.z * dist, 1.0};
	ct->rot         = quat_mult(quat_from_axis_angle(vec4_z_axis(), trait_third_person_controller_yaw),
	                            quat_from_axis_angle(vec4_x_axis(), math_pi() / 2.0 + trait_third_person_controller_pitch));
	camera_object_build_mat(scene_camera);
	g_context->ddirty = 2;
}

static physics_body_t *trait_third_person_controller_body(object_t *o) {
	physics_body_t *body = o->_->body;
	if (body != NULL && body->shape == PHYSICS_SHAPE_BOX && body->mass > 0.0) {
		return body;
	}
	script_physics_set_shape(o, PHYSICS_SHAPE_BOX);
	return o->_->body;
}

void trait_third_person_controller_run() {
	object_t *o = script_get_object(trait_third_person_controller_object);
	if (o == NULL) {
		return;
	}

	physics_body_t *body = trait_third_person_controller_body(o);
	if (body == NULL) {
		return;
	}

	bool control = iron_mouse_is_locked();
	if (control && keyboard_started("escape")) {
		iron_mouse_unlock();
		control = false;
	}
	else if (!control && mouse_started("left")) {
		trait_third_person_controller_grab();
		control = iron_mouse_is_locked();
	}

	if (!control) {
		trait_third_person_controller_set_moving(o, false);
		return;
	}

	trait_third_person_controller_rotate();
	trait_third_person_controller_set_moving(o, trait_third_person_controller_move(body));
	trait_third_person_controller_place(body);
	trait_third_person_controller_follow(body);
}

void trait_third_person_controller_stop() {
	object_t *o = script_get_object(trait_third_person_controller_object);
	if (o != NULL) {
		trait_third_person_controller_set_moving(o, false);
	}
}
