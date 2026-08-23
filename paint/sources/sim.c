
#include "global.h"

bool sim_initialized = false;

void sim_init() {
	if (sim_initialized) {
		return;
	}
	physics_world_create();
	sim_initialized = true;
}

void sim_update() {
	render_path_raytrace_ready = false;

	if (sim_running) {
		trait_update();
		physics_world_update();
		iron_delay_idle_sleep();
	}
}

void sim_play() {
	sim_running = true;
}

void sim_stop() {
	sim_running = false;
	trait_stop();
}

void sim_add_body(object_t *o, physics_shape_t shape, f32 mass) {
	sim_init();
	physics_body_create(o, shape, mass);
}

static void sim_shift_object_masks(i32 from) {
	if (g_project->_->layers != NULL) {
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->object_mask >= from) {
				++l->object_mask;
			}
		}
	}
	if (g_context->layer_filter >= from) {
		++g_context->layer_filter;
	}
}

mesh_object_t *sim_duplicate_object(mesh_object_t *so) {
	// Mesh
	if (so == NULL) {
		return NULL;
	}

	mesh_data_t   *data = so->data;
	mesh_object_t *dup  = scene_add_mesh_object(data, so->material, so->base->parent);
	transform_set_matrix(dup->base->transform, so->base->transform->local);

	// Insert below the original
	i32 index = array_index_of(g_project->_->paint_objects, so);
	i32 at    = index < 0 ? g_project->_->paint_objects->length : index + 1;
	array_insert((any_array_t *)g_project->_->paint_objects, at, dup);
	sim_shift_object_masks(at + 1);

	// Ensure unique name
	char *oname = so->base->name;
	char *ext   = "";
	i32   i     = 0;
	while (!_import_mesh_is_unique_name(string("%s%s", oname, ext))) {
		ext = string_copy(_import_mesh_number_ext(++i));
	}
	dup->base->name = string("%s%s", oname, ext);
	tab_stages_add_object(dup->base->name);

	// Material override
	i32 mat_index = tab_meshes_get_override(so);
	if (mat_index >= 0) {
		tab_meshes_set_override_data(dup, mat_index, so->material);
		g_project->mesh_materials = i32_array_create(0);
	}

	// Physics
	physics_body_t *pb = so->base->_->body;
	if (pb != NULL) {
		physics_body_create(dup->base, pb->shape, pb->mass);
	}

	tab_meshes_sort_hierarchy();
	tab_timeline_sync();

	return dup;
}

void sim_duplicate() {
	sim_duplicate_object(g_context->paint_object);
	util_mesh_merge(NULL);
	g_context->ddirty = 2;
}

void sim_delete() {
	if (g_project->_->paint_objects->length < 2) {
		return;
	}
	tab_meshes_draw_context_menu_delete(g_context->paint_object);
}
