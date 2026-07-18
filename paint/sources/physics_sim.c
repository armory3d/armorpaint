
#include "global.h"

any_array_t *sim_transforms;
bool         sim_initialized = false;

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
		// if (render_path_raytrace_frame != 1) {
		// return;
		// }

		physics_world_t *world = physics_world_active;
		physics_world_update(world);

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
		physics_body_t *pb = any_imap_get(physics_body_object_map, pos->buffer[i]->base->uid);
		if (pb != NULL) {
			physics_body_sync_transform(pb);
		}
	}
}

void sim_add_body(object_t *o, physics_shape_t shape, f32 mass) {
	sim_init();
	physics_body_t *body = physics_body_create();
	body->shape          = shape;
	body->mass           = mass;
	physics_body_init(body, o);
}

void sim_remove_body(physics_body_t *pb) {
	physics_body_remove(pb);
}

mesh_data_t *mesh_data_duplicate(mesh_data_t *source) {
	mesh_data_t *raw = gc_alloc(sizeof(mesh_data_t));
	raw->name        = string_copy(source->name);
	raw->scale_pos   = source->scale_pos;
	raw->scale_tex   = source->scale_tex;

	raw->vertex_arrays = (vertex_array_t_array_t *)any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < source->vertex_arrays->length; ++i) {
		vertex_array_t *src = source->vertex_arrays->buffer[i];
		vertex_array_t *va =
		    GC_ALLOC_INIT(vertex_array_t,
		                  {.attrib = string_copy(src->attrib), .data = string_copy(src->data), .values = i16_array_create_from_array(src->values)});
		any_array_push(raw->vertex_arrays, va);
	}

	raw->index_array = u32_array_create_from_array(source->index_array);

	return mesh_data_create(raw);
}

void sim_duplicate() {
	// Mesh
	mesh_object_t *so = g_context->paint_object;
	if (so == NULL) {
		return;
	}

	mesh_data_t   *data = mesh_data_duplicate(so->data);
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

	// Physics
	physics_body_t *pb = any_imap_get(physics_body_object_map, so->base->uid);
	if (pb != NULL) {
		physics_body_t *pbdup = physics_body_create();
		pbdup->shape          = pb->shape;
		pbdup->mass           = pb->mass;
		physics_body_init(pbdup, dup->base);
	}
}

void sim_delete() {
	mesh_object_t *so = g_context->paint_object;
	array_remove(g_project->_->paint_objects, so);
	mesh_object_remove(so);
	physics_body_t *pb = any_imap_get(physics_body_object_map, so->base->uid);
	sim_remove_body(pb);
}
