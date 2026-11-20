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
	int              length;
	int              capacity;
} physics_pair_t_array_t;

typedef enum {
	PHYSICS_SHAPE_BOX       = 0,
	PHYSICS_SHAPE_SPHERE    = 1,
	PHYSICS_SHAPE_TERRAIN   = 2,
	PHYSICS_SHAPE_MESH      = 3,
	PHYSICS_SHAPE_WIND      = 4,
	PHYSICS_SHAPE_ATTRACTOR = 5,
	PHYSICS_SHAPE_DOMAIN    = 6,
} physics_shape_t;

typedef struct {
	vec4_t vel;
	void  *body;
} physics_hit_t;

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
	void           *_body;
	physics_shape_t shape;
	float           mass;
	float           dimx;
	float           dimy;
	float           dimz;
	vec4_t          offset;
	struct object  *obj;
} physics_body_t;

void                    physics_world_create();
void                    physics_world_destroy();
void                    physics_world_update();
void                    physics_world_step(float dt);
physics_pair_t         *physics_world_get_contact(void *body);
physics_pair_t_array_t *physics_get_contact_pairs(physics_body_t *body);

physics_body_t *physics_body_create(struct object *obj, physics_shape_t shape, float mass);
void            physics_body_set_mass(physics_body_t *body, float mass);
void            physics_body_set_dim(physics_body_t *body, float dimx, float dimy, float dimz);
void            physics_body_apply_impulse(void *body, vec4_t impulse);
void            physics_body_get_pos(void *body, vec4_t *pos);
void            physics_body_get_rot(void *body, quat_t *rot);
void            physics_body_get_velocity(void *body, vec4_t *vel);
void            physics_body_set_velocity(void *body, float x, float y, float z);
void            physics_body_sync_transform(physics_body_t *body);
void            physics_body_move(physics_body_t *body, float dt);
void            physics_body_set_rotation(physics_body_t *body, quat_t rot);
void            physics_body_update(physics_body_t *body);
void            physics_body_remove(physics_body_t *body);
float           physics_body_get_speed(physics_body_t *body);
int             physics_terrain_raycast(vec4_t origin, vec4_t dir, vec4_t *hit);
int             physics_box_raycast(vec4_t origin, vec4_t dir, float max_dist, vec4_t *hit);
void            physics_set_friction(float v);
void            physics_set_bounciness(float v);
void            physics_set_gravity(float x, float y, float z);
vec4_t          physics_get_gravity();
int             physics_collide_point(vec4_t *pos, float radius, physics_hit_t *hit);
void            physics_body_apply_impulse_at(void *body, vec4_t impulse, vec4_t point);
vec4_t          physics_body_velocity_at(void *body, vec4_t point);
float           physics_body_give(void *body, vec4_t point, vec4_t n); // Inverse mass along n at point
int             physics_collider_version();
vec4_t          physics_field_force(vec4_t p);
vec4_t          physics_field_wind(vec4_t p);
int             physics_field_any(int wind);

// iron_physics_sim.c
typedef enum {
	PHYSICS_MATERIAL_LIQUID = 0,
	PHYSICS_MATERIAL_JELLY  = 1,
	PHYSICS_MATERIAL_SAND   = 2,
} physics_material_t;

typedef struct physics_sim {
	int   nx;
	int   ny;
	int   nz;
	float cell;
	float min[3]; // Corner of the domain, cell centers are at min + (index + 0.5) * cell

	float         *vel[3];
	float         *mass[3]; // Liquid mass on the faces, a full cell is 1
	float        **density;
	int            gas_count;
	float         *temperature;
	unsigned char *emitter;
	unsigned char *solid;
	float         *solid_vel;
	void         **solid_body;
	unsigned char *solid_base;
	float         *solid_base_vel;
	void         **solid_base_body;
	unsigned char *closed[3];
	int           *closed_list;
	int            closed_count;
	int           *body_cells;
	int            body_cell_count;
	float         *obstacles;
	int            obstacle_count;
	int            collider_version;
	float         *pressure;
	int            mass_box[6];
	int            pressure_box[6];

	// Particles
	float         *pos;    // xyz per particle
	float         *pvel;   // xyz per particle
	float         *affine; // 3x3 per particle, velocity gradient
	float         *deform; // 3x3 per particle, deformation gradient of solid materials
	float         *stress; // 3x3 per particle, stress over density of solid materials
	unsigned char *material;
	unsigned char *owner;
	int            count;
	int            cap;

	float    buoyancy;       // Upward acceleration of hot gas
	float    dissipation;    // Gas density fade per second
	float    cooling;        // Temperature fade per second
	float    vorticity;      // Strength of the small swirls in the air
	float    viscosity;      // Liquid, 0 - 1
	float    stiffness[256]; // Solids
	unsigned seed;

	float *tmp[3];
	float *cell_tmp[6];
} physics_sim_t;

physics_sim_t *physics_sim_create(int nx, int ny, int nz, float cell, float *min);
void           physics_sim_destroy(physics_sim_t *s);
void           physics_sim_add_particle(physics_sim_t *s, float x, float y, float z, int material, int owner);
int            physics_sim_add_gas(physics_sim_t *s);
void           physics_sim_step(physics_sim_t *s, float dt, unsigned emit);
int            physics_sim_sample(physics_sim_t *s, vec4_t p, vec4_t *vel, float *liquid);
float *physics_sim_particle_field(float *pos, int count, float spacing, float voxel, int max_res, int *nx, int *ny, int *nz, float *min, float *out_voxel);

// iron_physics_cloth.c
typedef struct physics_cloth {
	float *pos;    // xyz per particle
	float *origin; // xyz per particle, the input shape
	float *prev;
	float *vel;      // xyz per particle
	float *inv_mass; // 1 per particle, 0 pins it in place
	int    count;
	int   *pairs;
	float *rest; // Length of each constraint
	float *lambda;
	int    constraint_count;
	int    stretch_count;
	int   *tris; // Three particles per triangle, for the wind
	int    tri_count;
	float  stretch_compliance; // Softness of the edges, 0 is rigid
	float  bend_compliance;    // Softness of the folds
	float  damping;            // Velocity lost per second, 0 - 1
	float  thickness;          // Distance kept from colliders
	float  mass;
	int (*collide)(vec4_t *pos, float radius, physics_hit_t *hit);
} physics_cloth_t;

physics_cloth_t *physics_cloth_create(float *pos, int count, unsigned int *ind, int index_count);
void             physics_cloth_destroy(physics_cloth_t *c);
void             physics_cloth_step(physics_cloth_t *c, float dt, struct physics_sim *grid);
