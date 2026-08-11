
#include "global.h"

any_array_t *sim_transforms;
bool         sim_initialized = false;

void sim_init() {
	if (sim_initialized) {
		return;
	}
	asim_world_create();
	sim_initialized = true;
}

void sim_update() {
	render_path_raytrace_ready = false;

	if (sim_running) {
		asim_world_update();
		iron_delay_idle_sleep();
		if (sim_record) {
			render_target_t *rt     = any_map_get(render_path_render_targets, "last");
			buffer_t        *pixels = gpu_get_texture_pixels(rt->_image);
#ifdef IRON_BGRA
			buffer_bgra_swap(pixels);
#endif
			// iron_mp4_encode(pixels);
		}
	}
}

void sim_play() {
	sim_running = true;

	if (sim_record) {
		if (string_equals(g_project->_->filepath, "")) {
			console_error(tr("Save project first"));
			sim_record = false;
			return;
		}
		char            *path = string("%s/output.mp4", path_base_dir(g_project->_->filepath));
		render_target_t *rt   = any_map_get(render_path_render_targets, "last");
		// iron_mp4_begin(path, rt._image.width, rt._image.height);
	}

	// Save transforms
	gc_unroot(sim_transforms);
	sim_transforms = any_array_create_from_raw((void *[]){}, 0);
	gc_root(sim_transforms);
	mesh_object_t_array_t *pos = g_project->_->paint_objects;
	for (i32 i = 0; i < pos->length; ++i) {
		mat4_t *m = gc_alloc(sizeof(mat4_t));
		memcpy(m->m, pos->buffer[i]->base->transform->local.m, sizeof(m->m));
		any_array_push(sim_transforms, m);
	}
}

void sim_stop() {
	sim_running = false;

	if (sim_record) {
		// iron_mp4_end();
	}

	// Restore transforms
	mesh_object_t_array_t *pos = g_project->_->paint_objects;
	for (i32 i = 0; i < pos->length; ++i) {
		transform_set_matrix(pos->buffer[i]->base->transform, *(mat4_t *)sim_transforms->buffer[i]);
		asim_body_t *pb = pos->buffer[i]->base->_->body;
		if (pb != NULL) {
			asim_body_sync_transform(pb);
		}
	}
}

void sim_add_body(object_t *o, asim_shape_t shape, f32 mass) {
	sim_init();
	asim_body_create(o, shape, mass);
}

mesh_object_t *sim_duplicate_object(mesh_object_t *so) {
	// Mesh
	if (so == NULL) {
		return NULL;
	}

	mesh_data_t   *data = util_mesh_data_duplicate(so->data);
	mesh_object_t *dup  = scene_add_mesh_object(data, so->material, so->base->parent);
	transform_set_matrix(dup->base->transform, so->base->transform->local);
	any_array_push(g_project->_->paint_objects, dup);

	// Ensure unique name
	char *oname = so->base->name;
	char *ext   = "";
	i32   i     = 0;
	while (!_import_mesh_is_unique_name(string("%s%s", oname, ext))) {
		ext = string_copy(_import_mesh_number_ext(++i));
	}
	dup->base->name = string("%s%s", oname, ext);
	dup->data->name = dup->base->name;
	tab_stages_add_object(dup->base->name);

	// Material override
	i32 mat_index = tab_meshes_get_override(so);
	if (mat_index >= 0) {
		tab_meshes_set_override_data(dup, mat_index, so->material);
		g_project->mesh_materials = i32_array_create(0);
	}

	// Physics
	asim_body_t *pb = so->base->_->body;
	if (pb != NULL) {
		asim_body_create(dup->base, pb->shape, pb->mass);
	}

	return dup;
}

void sim_duplicate() {
	sim_duplicate_object(g_context->paint_object);
}

void sim_delete() {
	mesh_object_t *so = g_context->paint_object;
	array_remove(g_project->_->paint_objects, so);
	mesh_object_remove(so);
}
