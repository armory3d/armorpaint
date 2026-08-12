
#include "../global.h"

#define PAC_SPEED     1.0
#define PAC_ARRIVE    0.1
#define PAC_CLEARANCE 0.15
#define PAC_TERRAIN   "Terrain"
#define PAC_MAX_RECTS 32
#define PAC_MAX_NODES (PAC_MAX_RECTS * 4 + 2)
#define PAC_EPS       0.001
#define PAC_FAR       1.0e30

static char  *trait_point_and_click_controller_object = NULL;
static quat_t trait_point_and_click_controller_rot    = {0.0, 0.0, 0.0, 1.0};
static bool   trait_point_and_click_controller_moving = false;

typedef struct {
	f32 min_x;
	f32 min_y;
	f32 max_x;
	f32 max_y;
} pac_rect_t;

static pac_rect_t pac_rects[PAC_MAX_RECTS];
static i32        pac_rect_count = 0;
static vec4_t     pac_nodes[PAC_MAX_NODES];
static i32        pac_node_count = 0;
static bool       pac_visible[PAC_MAX_NODES][PAC_MAX_NODES];
static vec4_t     pac_path[PAC_MAX_NODES];
static i32        pac_path_count = 0;
static i32        pac_path_index = 0;

void trait_point_and_click_controller_init(char *object) {
	gc_unroot(trait_point_and_click_controller_object);
	trait_point_and_click_controller_object = string_copy(object);
	gc_root(trait_point_and_click_controller_object);
	trait_point_and_click_controller_moving = false;
	trait_point_and_click_controller_rot    = (quat_t){0.0, 0.0, 0.0, 1.0};
	pac_path_count                          = 0;
	pac_path_index                          = 0;
}

static f32 pac_dist(vec4_t a, vec4_t b) {
	f32 dx = a.x - b.x;
	f32 dy = a.y - b.y;
	return math_sqrt(dx * dx + dy * dy);
}

static bool pac_inside(pac_rect_t *r, vec4_t p) {
	return p.x > r->min_x && p.x < r->max_x && p.y > r->min_y && p.y < r->max_y;
}

static void pac_gather_rects(asim_body_t *self) {
	pac_rect_count = 0;
	f32 grow       = math_sqrt(self->dimx * self->dimx + self->dimy * self->dimy) / 2.0 + PAC_CLEARANCE;

	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	for (i32 i = 0; i < objects->length && pac_rect_count < PAC_MAX_RECTS; ++i) {
		object_t *o = objects->buffer[i]->base;
		if (o->_ == NULL || string_equals(o->name, PAC_TERRAIN)) {
			continue;
		}
		asim_body_t *body = o->_->body;
		if (body == NULL || body == self || body->shape != ASIM_SHAPE_BOX || body->mass > 0.0) {
			continue;
		}

		quat_t rot    = o->transform->rot;
		vec4_t center = vec4_add(o->transform->loc, vec4_apply_quat(body->offset, rot));
		vec4_t half   = (vec4_t){body->dimx / 2.0, body->dimy / 2.0, body->dimz / 2.0, 0.0};

		pac_rect_t r = {PAC_FAR, PAC_FAR, -PAC_FAR, -PAC_FAR};
		for (i32 c = 0; c < 8; ++c) {
			vec4_t corner = (vec4_t){(c & 1) ? half.x : -half.x, (c & 2) ? half.y : -half.y, (c & 4) ? half.z : -half.z, 0.0};
			corner        = vec4_apply_quat(corner, rot);
			r.min_x       = math_min(r.min_x, center.x + corner.x);
			r.min_y       = math_min(r.min_y, center.y + corner.y);
			r.max_x       = math_max(r.max_x, center.x + corner.x);
			r.max_y       = math_max(r.max_y, center.y + corner.y);
		}

		r.min_x -= grow;
		r.min_y -= grow;
		r.max_x += grow;
		r.max_y += grow;
		pac_rects[pac_rect_count++] = r;
	}
}

static vec4_t pac_push_out(vec4_t p) {
	for (i32 pass = 0; pass < 4; ++pass) {
		bool moved = false;
		for (i32 i = 0; i < pac_rect_count; ++i) {
			pac_rect_t *r = &pac_rects[i];
			if (!pac_inside(r, p)) {
				continue;
			}

			f32 left  = p.x - r->min_x;
			f32 right = r->max_x - p.x;
			f32 down  = p.y - r->min_y;
			f32 up    = r->max_y - p.y;
			f32 best  = math_min(math_min(left, right), math_min(down, up));
			if (best == left) {
				p.x = r->min_x - PAC_EPS;
			}
			else if (best == right) {
				p.x = r->max_x + PAC_EPS;
			}
			else if (best == down) {
				p.y = r->min_y - PAC_EPS;
			}
			else {
				p.y = r->max_y + PAC_EPS;
			}
			moved = true;
		}
		if (!moved) {
			break;
		}
	}
	return p;
}

static bool pac_clip_slab(f32 origin, f32 dir, f32 lo, f32 hi, f32 *t0, f32 *t1) {
	if (math_abs(dir) < 1e-6) {
		return origin >= lo && origin <= hi;
	}

	f32 ta = (lo - origin) / dir;
	f32 tb = (hi - origin) / dir;
	if (ta > tb) {
		f32 tmp = ta;
		ta      = tb;
		tb      = tmp;
	}
	if (ta > *t0) {
		*t0 = ta;
	}
	if (tb < *t1) {
		*t1 = tb;
	}
	return *t0 <= *t1;
}

static bool pac_crosses(vec4_t a, vec4_t b, pac_rect_t *r) {
	f32 min_x = r->min_x + PAC_EPS;
	f32 max_x = r->max_x - PAC_EPS;
	f32 min_y = r->min_y + PAC_EPS;
	f32 max_y = r->max_y - PAC_EPS;
	if (min_x >= max_x || min_y >= max_y) {
		return false;
	}

	f32 t0 = 0.0;
	f32 t1 = 1.0;
	return pac_clip_slab(a.x, b.x - a.x, min_x, max_x, &t0, &t1) && pac_clip_slab(a.y, b.y - a.y, min_y, max_y, &t0, &t1);
}

static bool pac_clear(vec4_t a, vec4_t b) {
	for (i32 i = 0; i < pac_rect_count; ++i) {
		if (pac_crosses(a, b, &pac_rects[i])) {
			return false;
		}
	}
	return true;
}

static void pac_build_nodes(vec4_t start, vec4_t goal) {
	pac_nodes[0]   = start;
	pac_nodes[1]   = goal;
	pac_node_count = 2;

	for (i32 i = 0; i < pac_rect_count; ++i) {
		pac_rect_t *r     = &pac_rects[i];
		vec4_t corners[4] = {{r->min_x, r->min_y, 0.0, 1.0}, {r->max_x, r->min_y, 0.0, 1.0}, {r->max_x, r->max_y, 0.0, 1.0}, {r->min_x, r->max_y, 0.0, 1.0}};

		for (i32 c = 0; c < 4; ++c) {
			bool standable = true;
			for (i32 j = 0; j < pac_rect_count && standable; ++j) {
				standable = !pac_inside(&pac_rects[j], corners[c]);
			}
			if (standable) {
				pac_nodes[pac_node_count++] = corners[c];
			}
		}
	}
}

static void pac_build_visibility() {
	for (i32 i = 0; i < pac_node_count; ++i) {
		pac_visible[i][i] = false;
		for (i32 j = i + 1; j < pac_node_count; ++j) {
			bool clear        = pac_clear(pac_nodes[i], pac_nodes[j]);
			pac_visible[i][j] = clear;
			pac_visible[j][i] = clear;
		}
	}
}

static bool pac_search() {
	f32  dist[PAC_MAX_NODES];
	i32  prev[PAC_MAX_NODES];
	bool done[PAC_MAX_NODES];

	for (i32 i = 0; i < pac_node_count; ++i) {
		dist[i] = PAC_FAR;
		prev[i] = -1;
		done[i] = false;
	}
	dist[0] = 0.0;

	for (;;) {
		i32 u = -1;
		for (i32 i = 0; i < pac_node_count; ++i) {
			if (!done[i] && dist[i] < PAC_FAR && (u == -1 || dist[i] < dist[u])) {
				u = i;
			}
		}
		if (u == -1 || u == 1) {
			break;
		}

		done[u] = true;
		for (i32 v = 0; v < pac_node_count; ++v) {
			if (done[v] || !pac_visible[u][v]) {
				continue;
			}
			f32 d = dist[u] + pac_dist(pac_nodes[u], pac_nodes[v]);
			if (d < dist[v]) {
				dist[v] = d;
				prev[v] = u;
			}
		}
	}

	if (dist[1] >= PAC_FAR) {
		return false;
	}

	i32 chain[PAC_MAX_NODES];
	i32 count = 0;
	for (i32 n = 1; n != -1; n = prev[n]) {
		chain[count++] = n;
	}

	pac_path_count = 0;
	for (i32 i = count - 2; i >= 0; --i) {
		pac_path[pac_path_count++] = pac_nodes[chain[i]];
	}
	pac_path_index = 0;
	return true;
}

static bool pac_plan(asim_body_t *body, vec4_t goal) {
	vec4_t pos;
	asim_body_get_pos(body->_body, &pos);

	pac_gather_rects(body);
	vec4_t start = pac_push_out((vec4_t){pos.x, pos.y, 0.0, 1.0});
	goal         = pac_push_out((vec4_t){goal.x, goal.y, 0.0, 1.0});

	if (pac_clear(start, goal)) {
		pac_path[0]    = goal;
		pac_path_count = 1;
		pac_path_index = 0;
		return true;
	}

	pac_build_nodes(start, goal);
	pac_build_visibility();
	return pac_search();
}

static quat_t trait_point_and_click_controller_facing(vec4_t dir) {
	return quat_from_axis_angle(vec4_z_axis(), math_atan2(-dir.x, dir.y) + math_pi());
}

static object_t *trait_point_and_click_controller_run_object(object_t *o) {
	return script_get_object(string("%s_run", o->name));
}

static void trait_point_and_click_controller_set_moving(object_t *o, bool moving) {
	if (moving == trait_point_and_click_controller_moving) {
		return;
	}
	object_t *run = trait_point_and_click_controller_run_object(o);
	if (run == NULL) {
		return;
	}
	trait_point_and_click_controller_moving = moving;
	o->visible                              = !moving;
	run->visible                            = moving;
}

static void trait_point_and_click_controller_place(asim_body_t *body) {
	if (!trait_point_and_click_controller_moving) {
		return;
	}
	object_t *run = trait_point_and_click_controller_run_object(body->obj);
	if (run == NULL) {
		return;
	}

	vec4_t pos;
	asim_body_get_pos(body->_body, &pos);
	quat_t rot = trait_point_and_click_controller_rot;
	vec4_t off = vec4_apply_quat(body->offset, rot);

	transform_t *t = run->transform;
	t->loc         = (vec4_t){pos.x - off.x, pos.y - off.y, pos.z - off.z, 1.0};
	t->rot         = rot;
	transform_build_matrix(t);
}

static void trait_point_and_click_controller_terrain() {
	object_t *o = script_get_object(PAC_TERRAIN);
	if (o == NULL || o->_ == NULL) {
		return;
	}
	asim_body_t *body = o->_->body;
	if (body == NULL || body->shape != ASIM_SHAPE_TERRAIN) {
		script_physics_set_shape(o, ASIM_SHAPE_TERRAIN);
	}
}

static bool trait_point_and_click_controller_pick(vec4_t *target) {
	f32 mx = mouse_view_x();
	f32 my = mouse_view_y();
	if (mx < 0.0 || my < 0.0 || mx > sys_w() || my > sys_h()) {
		return false;
	}

	trait_point_and_click_controller_terrain();
	ray_t *ray = raycast_get_ray(mx, my, scene_camera);
	return asim_terrain_raycast(ray->origin, ray->dir, target);
}

static bool trait_point_and_click_controller_move(asim_body_t *body) {
	vec4_t vel;
	asim_body_get_velocity(body->_body, &vel);
	vec4_t pos;
	asim_body_get_pos(body->_body, &pos);

	while (pac_path_index < pac_path_count && pac_dist(pac_path[pac_path_index], pos) <= PAC_ARRIVE) {
		pac_path_index++;
	}
	if (pac_path_index >= pac_path_count) {
		asim_body_set_velocity(body->_body, 0.0, 0.0, vel.z);
		return false;
	}

	vec4_t target = pac_path[pac_path_index];
	f32    dist   = pac_dist(target, pos);
	vec4_t dir    = (vec4_t){(target.x - pos.x) / dist, (target.y - pos.y) / dist, 0.0, 0.0};

	f32 speed = PAC_SPEED;
	f32 delta = sys_delta();
	if (pac_path_index == pac_path_count - 1 && delta > 0.0 && dist / delta < speed) {
		speed = dist / delta;
	}
	asim_body_set_velocity(body->_body, dir.x * speed, dir.y * speed, vel.z);

	trait_point_and_click_controller_rot = trait_point_and_click_controller_facing(dir);
	asim_body_set_rotation(body, trait_point_and_click_controller_rot);
	return true;
}

static asim_body_t *trait_point_and_click_controller_body(object_t *o) {
	asim_body_t *body = o->_->body;
	if (body != NULL && body->shape == ASIM_SHAPE_BOX && body->mass > 0.0) {
		return body;
	}
	script_physics_set_shape(o, ASIM_SHAPE_BOX);
	return o->_->body;
}

void trait_point_and_click_controller_run() {
	object_t *o = script_get_object(trait_point_and_click_controller_object);
	if (o == NULL) {
		return;
	}

	asim_body_t *body = trait_point_and_click_controller_body(o);
	if (body == NULL) {
		return;
	}

	vec4_t target;
	if (mouse_started("left") && trait_point_and_click_controller_pick(&target) && !pac_plan(body, target)) {
		pac_path_count = 0;
		pac_path_index = 0;
	}

	bool moving = trait_point_and_click_controller_move(body);
	trait_point_and_click_controller_set_moving(o, moving);
	trait_point_and_click_controller_place(body);
	if (moving) {
		g_context->ddirty = 2;
	}
}

void trait_point_and_click_controller_stop() {
	pac_path_count = 0;
	pac_path_index = 0;
	object_t *o    = script_get_object(trait_point_and_click_controller_object);
	if (o != NULL) {
		trait_point_and_click_controller_set_moving(o, false);
	}
}
