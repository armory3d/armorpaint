#pragma once

#include <iron_array.h>
#include <iron_math.h>

typedef struct {
	float pos_a_x;
	float pos_a_y;
	float pos_a_z;
	float nor_x;
	float nor_y;
	float nor_z;
} physics_pair_t;

void            asim_world_create();
void            asim_world_destroy();
void            asim_world_update(float time_step);
physics_pair_t *asim_world_get_contact(void *body);

void *asim_body_create(int shape, float mass, float dimx, float dimy, float dimz, float x, float y, float z, void *posa, void *inda, float scale_pos);
void  asim_body_set_mass(void *body, float mass);
void  asim_body_apply_impulse(void *body, float x, float y, float z);
void  asim_body_get_pos(void *body, vec4_t *pos);
void  asim_body_get_rot(void *body, quat_t *rot);
void  asim_body_sync_transform(void *body, vec4_t pos, quat_t rot);
void  asim_body_remove(void *body);
float asim_body_get_speed(void *body);
void  asim_set_friction(float v);
void  asim_set_bounciness(float v);
void  asim_set_gravity(float x, float y, float z);
