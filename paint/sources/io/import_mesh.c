
#include "../global.h"

bool import_mesh_clear_layers  = true;
bool import_mesh_needs_unwrap  = false;
bool import_mesh_no_reset      = false;
bool import_mesh_no_scale      = false;
bool import_mesh_keep_timeline = false;

void import_mesh_run(char *path, bool _clear_layers, bool replace_existing, bool keep_camera) {
	if (!path_is_mesh(path)) {
		if (!context_enable_import_plugin(path)) {
			console_error(strings_unknown_asset_format());
			return;
		}
	}

	import_mesh_clear_layers = _clear_layers;
	import_mesh_no_reset     = keep_camera;
	g_context->layer_filter  = 0;

	char *p = to_lower_case(path);
	if (ends_with(p, ".obj")) {
		import_obj_run(path, replace_existing);
	}
	else if (ends_with(p, ".blend")) {
		import_blend_mesh_run(path, replace_existing);
	}
	else {
		char *ext                           = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
		raw_mesh_t *(*importer)(char *path) = any_map_get(import_mesh_importers, ext);

		raw_mesh_t *mesh = importer(path);
		if (string_equals(mesh->name, "")) {
			mesh->name = string_copy(path_base_name(path));
		}

		replace_existing ? import_mesh_make_mesh(mesh) : import_mesh_add_mesh(mesh);

		bool has_next = mesh->has_next;
		while (has_next) {
			raw_mesh_t *mesh = importer(path);
			if (string_equals(mesh->name, "")) {
				mesh->name = string_copy(path_base_name(path));
			}
			has_next = mesh->has_next;
			import_mesh_add_mesh(mesh);
		}
	}

	g_project->mesh_assets = any_array_create_from_raw(
	    (void *[]){
	        path,
	    },
	    1);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	sys_title_set(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, ".")));
#endif
}

i32 import_mesh_finish_import_sort(void **pa, void **pb) {
	mesh_object_t *a = *(pa);
	mesh_object_t *b = *(pb);
	return strcmp(a->base->name, b->base->name);
}

void import_mesh_finish_import(void *_) {
	if (g_context->merged_object != NULL) {
		mesh_data_delete(g_context->merged_object->data);
		mesh_object_remove(g_context->merged_object);
		g_context->merged_object = NULL;
	}

	context_select_paint_object(context_main_object());

	// No mask by default
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		p->base->visible = true;
	}

	if (g_project->_->paint_objects->length > 1) {
		// Sort by name
		array_sort(g_project->_->paint_objects, &import_mesh_finish_import_sort);

		// Reparent
		mesh_object_t *new_parent = g_project->_->paint_objects->buffer[0];
		object_set_parent(new_parent->base, NULL);
		for (i32 i = 1; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			object_set_parent(p->base, new_parent->base);
		}
		context_select_paint_object(context_main_object());

		if (g_context->merged_object == NULL) {
			util_mesh_merge(NULL);
		}
		g_context->paint_object->skip_context   = "paint";
		g_context->merged_object->base->visible = true;
	}

	if (!import_mesh_no_scale) {
		viewport_scale_to_bounds(2.0);
	}
	import_mesh_no_scale = false;

	if (string_equals(g_context->paint_object->base->name, "")) {
		g_context->paint_object->base->name = "Object";
	}

	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();
	ui_view2d_hwnd->redraws    = 2;
	render_path_raytrace_ready = false;
	g_context->paint_body      = NULL;
	tab_meshes_reset_preview_map();
	base_update_workflow();

	if (import_mesh_needs_unwrap) {
		import_mesh_needs_unwrap = false;
		project_unwrap_mesh_box();
	}
}

void _import_mesh_make_mesh_clear_layers(void *_) {
	layers_init();
}

bool _import_mesh_is_unique_name(char *s) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (string_equals(p->base->name, s)) {
			return false;
		}
	}
	return true;
}

char *_import_mesh_number_ext(i32 i) {
	if (i < 10) {
		return string(".00%s", i32_to_string(i));
	}
	if (i < 100) {
		return string(".0%s", i32_to_string(i));
	}
	return string(".%s", i32_to_string(i));
}

void import_mesh_make_mesh(raw_mesh_t *mesh) {
	if (mesh == NULL || mesh->posa == NULL || mesh->nora == NULL || mesh->inda == NULL || mesh->posa->length == 0) {
		console_error(strings_failed_to_read_mesh_data());
		return;
	}

	import_mesh_needs_unwrap = mesh->texa == NULL;
	if (mesh->texa == NULL) {
		i32 verts  = mesh->posa->length / 4;
		mesh->texa = i16_array_create(verts * 2);
	}

	mesh_data_t *raw = import_mesh_raw_mesh(mesh);

	mesh_data_t *md         = mesh_data_create(raw);
	g_context->paint_object = context_main_object();

	context_select_paint_object(context_main_object());
	if (!import_mesh_no_reset) {
		viewport_reset();
	}

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p == g_context->paint_object) {
			continue;
		}
		data_delete_mesh(p->data->_->handle);
		mesh_object_remove(p);
	}

	char *handle = g_context->paint_object->data->_->handle;
	if (!string_equals(handle, "SceneSphere") && !string_equals(handle, "ScenePlane")) {
		sys_notify_on_next_frame(&mesh_data_delete, g_context->paint_object->data);
	}

	mesh_object_set_data(g_context->paint_object, md);
	g_context->paint_object->base->name = mesh->name;
	g_project->_->paint_objects         = any_array_create_from_raw(
        (void *[]){
            g_context->paint_object,
        },
        1);

	md->_->handle = string_copy(raw->name);
	any_map_set(data_cached_meshes, md->_->handle, md);

	if (!import_mesh_keep_timeline) {
		tab_timeline_reset();
	}

	g_context->ddirty = 4;

	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_dilatemap_cached                          = false;

	if (import_mesh_clear_layers) {
		while (g_project->_->layers->length > 0) {
			slot_layer_t *l = array_pop(g_project->_->layers);
			slot_layer_unload(l);
		}
		layers_new_layer(false, -1, NULL);
		sys_notify_on_next_frame(&_import_mesh_make_mesh_clear_layers, NULL);
		history_reset();
	}

	g_project->stages = NULL;

	// Wait for add_mesh calls to finish
	sys_notify_on_next_frame(&import_mesh_finish_import, NULL);
}

void import_mesh_add_mesh(raw_mesh_t *mesh) {
	if (mesh->texa == NULL) {
		i32 verts  = mesh->posa->length / 4;
		mesh->texa = i16_array_create(verts * 2);
	}

	mesh_data_t *raw = import_mesh_raw_mesh(mesh);
	// util_mesh_pack_uvs(mesh->texa);
	mesh_data_t *md = mesh_data_create(raw);

	mesh_object_t *object = scene_add_mesh_object(md, g_context->paint_object->material, g_context->paint_object->base);
	object->base->name    = mesh->name;
	object->skip_context  = "paint";

	// Ensure unique names
	char *oname = object->base->name;
	char *ext   = "";
	i32   i     = 0;
	while (!_import_mesh_is_unique_name(string("%s%s", oname, ext))) {
		ext = string_copy(_import_mesh_number_ext(++i));
	}
	object->base->name = string("%s%s", object->base->name, ext);
	raw->name          = string("%s%s", raw->name, ext);

	any_array_push(g_project->_->paint_objects, object);
	md->_->handle = string_copy(raw->name);
	any_map_set(data_cached_meshes, md->_->handle, md);

	g_context->ddirty = 4;

	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_dilatemap_cached                          = false;
}

mesh_data_t *import_mesh_raw_mesh(raw_mesh_t *mesh) {
	mesh_data_t *raw = GC_ALLOC_INIT(mesh_data_t, {.name          = mesh->name,
	                                               .vertex_arrays = any_array_create_from_raw(
	                                                   (void *[]){
	                                                       GC_ALLOC_INIT(vertex_array_t, {.values = mesh->posa, .attrib = "pos", .data = "short4norm"}),
	                                                       GC_ALLOC_INIT(vertex_array_t, {.values = mesh->nora, .attrib = "nor", .data = "short2norm"}),
	                                                       GC_ALLOC_INIT(vertex_array_t, {.values = mesh->texa, .attrib = "tex", .data = "short2norm"}),
	                                                   },
	                                                   3),
	                                               .index_array = mesh->inda,
	                                               .scale_pos   = mesh->scale_pos,
	                                               .scale_tex   = mesh->scale_tex});
	if (mesh->texa1 != NULL) {
		vertex_array_t *va = GC_ALLOC_INIT(vertex_array_t, {.values = mesh->texa1, .attrib = "tex1", .data = "short2norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	if (mesh->cola != NULL) {
		vertex_array_t *va = GC_ALLOC_INIT(vertex_array_t, {.values = mesh->cola, .attrib = "col", .data = "short4norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	return raw;
}
