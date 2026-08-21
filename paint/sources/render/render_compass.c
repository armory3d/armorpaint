
#include "../global.h"

static object_t *_compass_hitbox_x;
static object_t *_compass_hitbox_y;
static object_t *_compass_hitbox_z;
static object_t *_compass_hovered      = NULL;
static object_t *_compass_hovered_last = NULL;

static void _compass_init_hitbox() {
	if (_compass_hitbox_x != NULL) {
		return;
	}

	_compass_hitbox_x = object_create(true);
	_compass_hitbox_y = object_create(true);
	_compass_hitbox_z = object_create(true);

	_compass_hitbox_x->transform->scale = (vec4_t){0.15, 0.15, 0.15, 1.0};
	_compass_hitbox_y->transform->scale = (vec4_t){0.15, 0.15, 0.15, 1.0};
	_compass_hitbox_z->transform->scale = (vec4_t){0.15, 0.15, 0.15, 1.0};

	_compass_hitbox_x->transform->loc = (vec4_t){1.5, 0, 0, 1.0};
	_compass_hitbox_y->transform->loc = (vec4_t){0, 1.5, 0, 1.0};
	_compass_hitbox_z->transform->loc = (vec4_t){0, 0, 1.5, 1.0};

	object_t *compass = scene_get_child(".Compass");
	object_set_parent(_compass_hitbox_x, compass);
	object_set_parent(_compass_hitbox_y, compass);
	object_set_parent(_compass_hitbox_z, compass);
}

static bool _compass_compare_quat(quat_t a, quat_t b) {
	return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

void render_compass() {
	if (!g_context->show_compass || g_config->workspace == WORKSPACE_PLAYER || g_context->capturing_screenshot) {
		return;
	}

	camera_object_t *cam     = scene_camera;
	mesh_object_t   *compass = scene_get_child(".Compass")->ext;

	bool      _visible     = compass->base->visible;
	object_t *_parent      = compass->base->parent;
	quat_t    crot         = cam->base->transform->rot;
	f32       ratio        = sys_w() / (float)sys_h();
	mat4_t    _P           = cam->p;
	cam->p                 = mat4_ortho(-8 * ratio, 8 * ratio, -8, 8, -2, 2);
	compass->base->visible = true;
	compass->base->parent  = cam->base;

	f32  compass_x    = 7.4;
	f32  compass_y    = 7.0;
	bool compass_down = false;

#ifdef IRON_IOS
	if (config_is_iphone()) {
		compass_down = true;
	}
#endif

	if (compass_down) {
		compass_x = 6.0;
		compass_y = -compass_y;
	}

	compass->base->transform->loc   = (vec4_t){compass_x * ratio, compass_y, -1, 1.0};
	compass->base->transform->rot   = (quat_t){-crot.x, -crot.y, -crot.z, crot.w};
	compass->base->transform->scale = (vec4_t){0.4, 0.4, 0.4, 1.0};
	transform_build_matrix(compass->base->transform);
	compass->frustum_culling = false;
	mesh_object_render(compass, "overlay", NULL);

	if (_compass_hovered != NULL) {
		line_draw_color    = _compass_hovered == _compass_hitbox_x ? 0xffff0000 : _compass_hovered == _compass_hitbox_y ? 0xff00ff00 : 0xff0000ff;
		line_draw_strength = 0.1;
		shape_draw_sphere(_compass_hovered->transform->world);
	}

	cam->p                 = _P;
	compass->base->visible = _visible;
	compass->base->parent  = _parent;
}

void render_compass_update() {
	if (!g_context->show_compass) {
		return;
	}

	if (_compass_hovered_last != _compass_hovered) {
		_compass_hovered_last = _compass_hovered;
		g_context->ddirty = 2;
	}
	_compass_hovered = NULL;

	f32  x     = mouse_view_x() / (float)sys_w();
	f32  y     = mouse_view_y() / (float)sys_h();
	bool hover = x > 0.9 && x < 1.0 && y < 0.14 && y > 0.0;
	if (hover) {

		_compass_init_hitbox();

		f32    ratio    = sys_w() / (float)sys_h();
		mat4_t _P       = scene_camera->p;
		scene_camera->p = mat4_ortho(-8 * ratio, 8 * ratio, -8, 8, -2, 2);

		transform_build_matrix(_compass_hitbox_x->transform);
		transform_build_matrix(_compass_hitbox_y->transform);
		transform_build_matrix(_compass_hitbox_z->transform);

		transform_t_array_t *ts = any_array_create_from_raw(
		    (void *[]){
		        _compass_hitbox_x->transform,
		        _compass_hitbox_y->transform,
		        _compass_hitbox_z->transform,
		    },
		    3);

		transform_t *t = raycast_closest_box_intersect(ts, mouse_view_x(), mouse_view_y(), scene_camera);
		if (t != NULL) {
			quat_t cq = scene_camera->base->transform->rot;

			if (t == _compass_hitbox_x->transform) {
				if (mouse_started("left")) {
					// Flip between left / right
					_compass_compare_quat(quat_from_euler(math_pi() / 2.0, 0, math_pi() / 2.0), cq)
					    ? viewport_set_view(-1, 0, 0, math_pi() / 2.0, 0, -math_pi() / 2.0) // Left
					    : viewport_set_view(1, 0, 0, math_pi() / 2.0, 0, math_pi() / 2.0);  // Right
				}
				_compass_hovered = _compass_hitbox_x;
			}

			else if (t == _compass_hitbox_y->transform) {
				if (mouse_started("left")) {
					_compass_compare_quat(quat_from_euler(math_pi() / 2.0, 0, math_pi()), cq)
					    ? viewport_set_view(0, -1, 0, math_pi() / 2.0, 0, 0)         // Front
					    : viewport_set_view(0, 1, 0, math_pi() / 2.0, 0, math_pi()); // Back
				}
				_compass_hovered = _compass_hitbox_y;
			}

			else {
				if (mouse_started("left")) {
					_compass_compare_quat(quat_from_euler(0, 0, 0), cq) ? viewport_set_view(0, 0, -1, math_pi(), 0, math_pi()) // Bottom
					                                                    : viewport_set_view(0, 0, 1, 0, 0, 0);                 // Top
				}
				_compass_hovered = _compass_hitbox_z;
			}
		}
		scene_camera->p = _P;
	}
}
