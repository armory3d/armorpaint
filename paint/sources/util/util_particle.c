
#include "../global.h"

void util_particle_init_mesh() {
	if (g_context->paint_body != NULL) {
		return;
	}
	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}

	g_context->paint_body = physics_body_create(g_context->merged_object->base, PHYSICS_SHAPE_MESH, 0.0);
}

void util_particle_init_physics() {
	sim_init();
	util_particle_init_mesh();
}

void util_particle_update() {
	if (g_context->tool != TOOL_TYPE_PARTICLE) {
		return;
	}

	util_particle_init_physics();
	make_particle_get_bullet_material();
	physics_world_update();

	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].timer != NULL && g_context->particles[i].timer->delay <= 0) {
			physics_body_remove(g_context->particles[i].body);
			mesh_object_remove((mesh_object_t *)g_context->particles[i].bullet->ext);
			memset(&g_context->particles[i], 0, sizeof(g_context->particles[i]));
		}
	}

	bool any_active = false;
	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].timer != NULL) {
			any_active = true;
			break;
		}
	}
	if (any_active) {
		g_context->ddirty = 2;
		g_context->rdirty = 2;
		iron_delay_idle_sleep();
	}

	static f64 particle_last_spawn_time = 0.0;
	bool       particle_just_fired      = false;
	if (mouse_down("left") && context_in_paint_area() && !g_context->paint2d && (mouse_started("left") || sys_time() - particle_last_spawn_time >= 0.2)) {
		particle_last_spawn_time = sys_time();

		i32 slot = -1;
		for (i32 i = 0; i < 32; ++i) {
			if (g_context->particles[i].timer == NULL) {
				slot = i;
				break;
			}
		}
		if (slot >= 0) {
			if (mouse_started("left")) {
				history_push_undo            = true;
				g_context->brush_blend_dirty = true;
			}

			object_t      *o  = scene_spawn_object(".Sphere", NULL, true);
			mesh_object_t *mo = o->ext;
			mo->base->name    = ".Bullet";
			mo->base->visible = true;
			mo->material      = make_particle_get_bullet_material();

			util_render_pick_pos_nor_tex();
			f32 nx            = g_context->norx_picked;
			f32 ny            = g_context->nory_picked;
			f32 nz            = g_context->norz_picked;
			f32 sphere_radius = g_context->brush_radius * 0.1f;
			f32 spawn_h       = -(sphere_radius + g_context->particle_spawn_distance) + 0.01;
			mo->base->transform->loc =
			    (vec4_t){g_context->posx_picked + nx * spawn_h, g_context->posy_picked + ny * spawn_h, g_context->posz_picked + nz * spawn_h, 1.0};
			mo->base->transform->scale = (vec4_t){g_context->brush_radius * 0.2, g_context->brush_radius * 0.2, g_context->brush_radius * 0.2, 1.0};
			transform_build_matrix(mo->base->transform);

			physics_body_t *body = physics_body_create(mo->base, PHYSICS_SHAPE_SPHERE, g_context->particle_mass);

			// Random direction
			f32 rx   = math_random() * 2.0f - 1.0f;
			f32 ry   = math_random() * 2.0f - 1.0f;
			f32 rz   = math_random() * 2.0f - 1.0f;
			f32 rlen = sqrtf(rx * rx + ry * ry + rz * rz);
			if (rlen < 0.0001f)
				rlen = 0.0001f;
			rx /= rlen;
			ry /= rlen;
			rz /= rlen;
			if (rx * nx + ry * ny + rz * nz < 0.0f) {
				rx = -rx;
				ry = -ry;
				rz = -rz;
			}
			f32 r       = g_context->particle_random;
			f32 dx      = nx + (rx - nx) * r;
			f32 dy      = ny + (ry - ny) * r;
			f32 dz      = nz + (rz - nz) * r;
			f32 impulse = g_context->particle_spawn_distance * 30.0f;
			physics_body_apply_impulse(body->_body, (vec4_t){dx * impulse, dy * impulse, dz * impulse, 0.0});

			g_context->particles[slot].body   = body;
			g_context->particles[slot].bullet = mo->base;
			g_context->particles[slot].timer  = tween_timer(g_context->particle_lifetime, NULL, NULL);
			particle_just_fired               = true;
		}
	}

	if (mouse_released("left")) {
		g_context->layer_preview_dirty = true;
	}

#ifdef WITH_PHYSICS
	if (!particle_just_fired) {
		for (i32 i = 0; i < 32; ++i) {
			if (g_context->particles[i].timer == NULL) {
				continue;
			}
			physics_pair_t_array_t *pairs = physics_get_contact_pairs(g_context->particles[i].body);
			if (pairs != NULL && pairs->length > 0) {
				physics_pair_t *p                     = pairs->buffer[0];
				g_context->particles[i].hit_last_x = g_context->particles[i].hit_x != 0 ? g_context->particles[i].hit_x : p->pos_a_x;
				g_context->particles[i].hit_last_y = g_context->particles[i].hit_y != 0 ? g_context->particles[i].hit_y : p->pos_a_y;
				g_context->particles[i].hit_last_z = g_context->particles[i].hit_z != 0 ? g_context->particles[i].hit_z : p->pos_a_z;
				g_context->particles[i].hit_x      = p->pos_a_x;
				g_context->particles[i].hit_y      = p->pos_a_y;
				g_context->particles[i].hit_z      = p->pos_a_z;
				g_context->particles[i].hit_nor_x  = p->nor_x;
				g_context->particles[i].hit_nor_y  = p->nor_y;
				g_context->particles[i].hit_nor_z  = p->nor_z;
				g_context->particles[i].contact_time += sys_delta();
				g_context->pdirty = 1;
			}
			else {
				g_context->particles[i].hit_x = 0.0;
			}
		}
	}
#endif
}
