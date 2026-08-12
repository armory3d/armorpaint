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

typedef struct physics_pair_t_array {
	physics_pair_t **buffer;
	int           length;
	int           capacity;
} physics_pair_t_array_t;

typedef enum {
	PHYSICS_SHAPE_BOX     = 0,
	PHYSICS_SHAPE_SPHERE  = 1,
	PHYSICS_SHAPE_TERRAIN = 2,
	PHYSICS_SHAPE_MESH    = 3,
} physics_shape_t;

typedef struct {
	float *heights;
	int    res_x;
	int    res_y;
	float  min_x;
	float  min_y;
	float  size_x;
	float  size_y;
} physics_heightfield_t;

struct object;

typedef struct physics_body {
	void          *_body;
	physics_shape_t   shape;
	float          mass;
	float          dimx;
	float          dimy;
	float          dimz;
	vec4_t         offset;
	struct object *obj;
} physics_body_t;

void                 physics_world_create();
void                 physics_world_destroy();
void                 physics_world_update();
physics_pair_t         *physics_world_get_contact(void *body);
physics_pair_t_array_t *physics_get_contact_pairs(physics_body_t *body);

physics_body_t *physics_body_create(struct object *obj, physics_shape_t shape, float mass);
void         physics_body_set_mass(physics_body_t *body, float mass);
void         physics_body_apply_impulse(void *body, vec4_t impulse);
void         physics_body_get_pos(void *body, vec4_t *pos);
void         physics_body_get_rot(void *body, quat_t *rot);
void         physics_body_get_velocity(void *body, vec4_t *vel);
void         physics_body_set_velocity(void *body, float x, float y, float z);
void         physics_body_sync_transform(physics_body_t *body);
void         physics_body_set_rotation(physics_body_t *body, quat_t rot);
void         physics_body_update(physics_body_t *body);
void         physics_body_remove(physics_body_t *body);
float        physics_body_get_speed(physics_body_t *body);
int          physics_terrain_raycast(vec4_t origin, vec4_t dir, vec4_t *hit);
void         physics_set_friction(float v);
void         physics_set_bounciness(float v);
void         physics_set_gravity(float x, float y, float z);
