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
} asim_pair_t;

typedef struct asim_pair_t_array {
	asim_pair_t **buffer;
	int           length;
	int           capacity;
} asim_pair_t_array_t;

typedef enum {
	ASIM_SHAPE_BOX     = 0,
	ASIM_SHAPE_SPHERE  = 1,
	ASIM_SHAPE_TERRAIN = 2,
	ASIM_SHAPE_MESH    = 3,
} asim_shape_t;

typedef struct {
	float *heights;
	int    res_x;
	int    res_y;
	float  min_x;
	float  min_y;
	float  size_x;
	float  size_y;
} asim_heightfield_t;

struct object;

typedef struct asim_body {
	void          *_body;
	asim_shape_t   shape;
	float          mass;
	float          dimx;
	float          dimy;
	float          dimz;
	vec4_t         offset;
	struct object *obj;
} asim_body_t;

void                 asim_world_create();
void                 asim_world_destroy();
void                 asim_world_update();
asim_pair_t         *asim_world_get_contact(void *body);
asim_pair_t_array_t *asim_get_contact_pairs(asim_body_t *body);

asim_body_t *asim_body_create(struct object *obj, asim_shape_t shape, float mass);
void         asim_body_set_mass(asim_body_t *body, float mass);
void         asim_body_apply_impulse(void *body, vec4_t impulse);
void         asim_body_get_pos(void *body, vec4_t *pos);
void         asim_body_get_rot(void *body, quat_t *rot);
void         asim_body_get_velocity(void *body, vec4_t *vel);
void         asim_body_set_velocity(void *body, float x, float y, float z);
void         asim_body_sync_transform(asim_body_t *body);
void         asim_body_set_rotation(asim_body_t *body, quat_t rot);
void         asim_body_update(asim_body_t *body);
void         asim_body_remove(asim_body_t *body);
float        asim_body_get_speed(asim_body_t *body);
int          asim_terrain_raycast(vec4_t origin, vec4_t dir, vec4_t *hit);
void         asim_set_friction(float v);
void         asim_set_bounciness(float v);
void         asim_set_gravity(float x, float y, float z);
