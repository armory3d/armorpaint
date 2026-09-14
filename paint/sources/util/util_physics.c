
#include "../global.h"

typedef struct {
	i32 shape; // -1 = no physics
	f32 mass;
} util_physics_t;

static any_imap_t *util_physics_map = NULL; // object uid -> util_physics_t

static util_physics_t *util_physics_find(object_t *o) {
	if (o == NULL || util_physics_map == NULL) {
		return NULL;
	}
	return any_imap_get(util_physics_map, o->uid);
}

static bool util_physics_in_stage_of(stage_t *stage, object_t *o) {
	return stage == NULL || string_array_index_of(stage->objects, o->name) >= 0;
}

static bool util_physics_in_stage(object_t *o) {
	return util_physics_in_stage_of(tab_stages_get_stage(), o);
}

void util_physics_clear() {
	util_physics_map = NULL;
}

void util_physics_store(object_t *o, i32 shape, f32 mass) {
	if (o == NULL) {
		return;
	}
	if (util_physics_map == NULL) {
		util_physics_map = any_imap_create();
	}
	util_physics_t *p = any_imap_get(util_physics_map, o->uid);
	if (p == NULL) {
		p = ALLOC_INIT(util_physics_t, {0});
		any_imap_set(util_physics_map, o->uid, p);
	}
	p->shape = shape;
	p->mass  = mass;
}

void util_physics_set(object_t *o, i32 shape, f32 mass) {
	if (o == NULL) {
		return;
	}
	util_physics_store(o, shape, mass);
	if (o->_->body != NULL) {
		physics_body_remove(o->_->body);
	}
	if (shape >= 0 && util_physics_in_stage(o)) {
		physics_body_create(o, (physics_shape_t)shape, mass);
	}
}

void util_physics_set_mass(object_t *o, f32 mass) {
	if (o == NULL) {
		return;
	}
	util_physics_store(o, util_physics_get_shape(o), mass);
	if (o->_->body != NULL) {
		physics_body_set_mass(o->_->body, mass); // Zero mass = static
	}
}

i32 util_physics_get_shape(object_t *o) {
	if (o != NULL && o->_->body != NULL) {
		return o->_->body->shape;
	}
	util_physics_t *p = util_physics_find(o);
	return p != NULL ? p->shape : -1;
}

f32 util_physics_get_mass(object_t *o) {
	if (o != NULL && o->_->body != NULL) {
		return o->_->body->mass;
	}
	util_physics_t *p = util_physics_find(o);
	return p != NULL ? p->mass : 0.0;
}

static bool util_physics_is_shared(i32 shape) {
	return shape == PHYSICS_SHAPE_MESH || shape == PHYSICS_SHAPE_TERRAIN;
}

void util_physics_apply_stage(stage_t *stage) {
	bool shared_cleared = false;

	// Drop the bodies which are no longer in the stage
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		object_t       *o     = g_project->_->paint_objects->buffer[i]->base;
		util_physics_t *p     = util_physics_find(o);
		i32             shape = p != NULL ? p->shape : -1;
		if (o->_->body == NULL || (shape >= 0 && util_physics_in_stage_of(stage, o))) {
			continue;
		}
		shared_cleared = shared_cleared || util_physics_is_shared(o->_->body->shape);
		physics_body_remove(o->_->body);
	}

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		object_t       *o     = g_project->_->paint_objects->buffer[i]->base;
		util_physics_t *p     = util_physics_find(o);
		i32             shape = p != NULL ? p->shape : -1;
		if (shape < 0 || !util_physics_in_stage_of(stage, o)) {
			continue;
		}
		if (o->_->body != NULL) {
			if (!shared_cleared || !util_physics_is_shared(shape)) {
				continue;
			}
			physics_body_remove(o->_->body);
		}
		physics_body_create(o, (physics_shape_t)shape, p->mass);
	}
}
