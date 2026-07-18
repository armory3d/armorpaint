
#ifdef WITH_PHYSICS

#include "../libs/asim.h"
#include "global.h"

physics_body_t *physics_body_create() {
	physics_body_t *body = GC_ALLOC_INIT(physics_body_t, {0});
	return body;
}

void physics_body_init(physics_body_t *body, object_t *obj) {
	any_imap_set(physics_body_object_map, obj->uid, body);

	body->obj = obj;
	transform_compute_dim(obj->transform);
	body->dimx = obj->transform->dim.x;
	body->dimy = obj->transform->dim.y;
	body->dimz = obj->transform->dim.z;

	f32          scale_pos = 1.0;
	i16_array_t *posa      = NULL;
	u32_array_t *inda      = NULL;

	if (body->shape == PHYSICS_SHAPE_MESH || body->shape == PHYSICS_SHAPE_HULL || body->shape == PHYSICS_SHAPE_TERRAIN) {
		mesh_object_t *mo    = obj->ext;
		mesh_data_t   *data  = mo->data;
		vec4_t         scale = obj->transform->scale;

		if (obj->parent != NULL) {
			scale.x *= obj->parent->transform->scale.x;
			scale.y *= obj->parent->transform->scale.y;
			scale.z *= obj->parent->transform->scale.z;
		}

		i16_array_t *positions = mesh_data_get_vertex_array(data, "pos")->values;
		u32_array_t *indices0  = data->index_array;

		scale_pos = scale.x * data->scale_pos;
		posa      = positions;
		inda      = indices0;
	}

	vec4_t loc  = obj->transform->loc;
	body->_body = asim_body_create(body->shape, body->mass, body->dimx, body->dimy, body->dimz, loc.x, loc.y, loc.z, posa, inda, scale_pos);
}

void physics_body_remove(physics_body_t *body) {
	if (body == NULL) {
		return;
	}
	imap_delete(physics_body_object_map, body->obj->uid);
	asim_body_remove(body->_body);
}

void physics_body_set_mass(physics_body_t *body, f32 mass) {
	body->mass = mass;
	if (body->shape == PHYSICS_SHAPE_SPHERE) {
		asim_body_set_mass(body->_body, mass);
	}
}

void physics_body_apply_impulse(physics_body_t *body, vec4_t dir) {
	asim_body_apply_impulse(body->_body, dir.x, dir.y, dir.z);
}

void physics_body_sync_transform(physics_body_t *body) {
	transform_t *transform = body->obj->transform;
	asim_body_sync_transform(body->_body, transform->loc, transform->rot);
}

void physics_body_update(physics_body_t *body) {
	if (body->shape == PHYSICS_SHAPE_MESH) {
		return; ////
	}

	transform_t *transform = body->obj->transform;
	asim_body_get_pos(body->_body, &transform->loc);
	asim_body_get_rot(body->_body, &transform->rot);
	transform_build_matrix(transform);
}

#endif
