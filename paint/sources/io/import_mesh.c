
#include "../global.h"

bool import_mesh_clear_layers  = true;
bool import_mesh_needs_unwrap  = false;
bool import_mesh_no_reset      = false;
bool import_mesh_no_scale      = false;
bool import_mesh_keep_timeline = false;
bool import_mesh_append        = false;

static mesh_object_t *import_mesh_appended = NULL;

void import_mesh_run(char *path, bool _clear_layers, bool replace_existing, bool keep_camera) {
	if (!path_is_mesh(path)) {
		if (!context_enable_import_plugin(path)) {
			console_error(strings_unknown_asset_format());
			return;
		}
	}

	import_mesh_clear_layers = _clear_layers;
	import_mesh_no_reset     = keep_camera;
	import_mesh_append       = !replace_existing;
	import_mesh_appended     = NULL;
	g_context->layer_filter  = 0;

	if (path_has_ext(path, "obj")) {
		import_obj_run(path, replace_existing);
	}
	else if (path_has_ext(path, "blend")) {
		import_blend_mesh_run(path, replace_existing);
	}
	else {
		char *ext                           = path_ext(path);
		raw_mesh_t *(*importer)(char *path) = any_map_get(import_mesh_importers, ext);
		free(ext);
		if (importer == NULL) {
			console_error(strings_unknown_asset_format());
			return;
		}

		raw_mesh_t *mesh = importer(path);
		if (mesh == NULL) {
			console_error(string("Failed to import '%s'", path));
			return;
		}
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

static mesh_object_t *import_mesh_make_root() {
	char          *name = string_copy(_import_mesh_unique_name("Root"));
	mesh_data_t   *md   = util_shape_make_data("empty");
	md->name            = name;
	mesh_object_t *mo   = scene_add_mesh_object(md, g_context->paint_object->material, NULL);
	mo->base->name      = name;
	tab_stages_add_object(name);
	return mo;
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

	if (!import_mesh_append) {
		// No mask by default
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			p->base->visible = true;
		}
	}

	// Keep appended objects at scene root
	if (import_mesh_append) {
		g_project->mesh_parents = i32_array_create(0);
	}

	if (g_project->_->paint_objects->length > 1) {
		if (!import_mesh_append) {
			// Sort by name
			array_sort(g_project->_->paint_objects, &import_mesh_finish_import_sort);

			// Parent all meshes to an empty root
			mesh_object_t *root = import_mesh_make_root();
			for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
				mesh_object_t *p = g_project->_->paint_objects->buffer[i];
				object_set_parent(p->base, root->base);
				transform_reset(p->base->transform);
				transform_build_matrix(p->base->transform);
			}
			array_insert(g_project->_->paint_objects, 0, root);
			g_project->mesh_parents = i32_array_create(0);
		}
		context_select_paint_object(import_mesh_append ? context_main_object() : g_project->_->paint_objects->buffer[1]);

		if (g_context->merged_object == NULL) {
			util_mesh_merge(NULL);
		}
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}

	if (import_mesh_append && import_mesh_appended != NULL && array_index_of(g_project->_->paint_objects, import_mesh_appended) >= 0) {
		context_select_paint_object(import_mesh_appended);
	}
	import_mesh_appended = NULL;

	if (!import_mesh_no_scale && !import_mesh_append) {
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
	base_update_tools();

	if (import_mesh_needs_unwrap) {
		import_mesh_needs_unwrap = false;
		project_unwrap_mesh_box();
	}

	import_mesh_append = false;
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

char *_import_mesh_unique_name(char *name) {
	// Returns the name or the next free .00X variant
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = i == 0 ? base : string_tmp("%s%s", base, strings_number_ext(i));
	if (_import_mesh_is_unique_name(res)) {
		return res;
	}

	// Mark the numbers after i taken by base.00X names in one pass, instead of a pass per number tried,
	// one of the next length + 1 numbers is free
	mesh_object_t_array_t *objects  = g_project->_->paint_objects;
	i32                    count    = objects->length + 1;
	bool                  *taken    = calloc(count, sizeof(bool));
	i32                    base_len = string_length(base);
	for (i32 j = 0; j < objects->length; ++j) {
		char *s = objects->buffer[j]->base->name;
		if (strncmp(s, base, base_len) != 0 || s[base_len] != '.') {
			continue;
		}
		char *digits = s + base_len + 1;
		i32   len    = 0;
		while (digits[len] >= '0' && digits[len] <= '9') {
			++len;
		}
		if (len < 3 || len > 9 || digits[len] != '\0') {
			continue;
		}
		i32 k = parse_int(digits) - i - 1;
		if (k >= 0 && k < count) {
			taken[k] = true;
		}
	}
	i32 k = 0;
	while (taken[k]) {
		++k;
	}
	free(taken);
	return string_tmp("%s%s", base, strings_number_ext(i + 1 + k));
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

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->skin_blob   = mesh->blob;
	md->_->skin_frame  = -1;
	md->_->owns_arrays = true;

	g_context->paint_object = context_main_object();

	context_select_paint_object(context_main_object());
	if (!import_mesh_no_reset) {
		viewport_reset();
	}

	nodes_mesh_reset();
	util_mesh_remove_objects(g_project->_->paint_objects, g_context->paint_object);

	char *handle = g_context->paint_object->data->_->handle;
	if (!string_equals(handle, "SceneSphere") && !string_equals(handle, "ScenePlane")) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, g_context->paint_object->data);
	}

	mesh_object_set_data(g_context->paint_object, md);
	g_context->paint_object->base->name = mesh->name;

	mesh_object_t_array_t *old_paint_objects = g_project->_->paint_objects;
	g_project->_->paint_objects              = any_array_create_from_raw(
        (void *[]){
            g_context->paint_object,
        },
        1);
	array_delete(old_paint_objects);

	if (!import_mesh_keep_timeline) {
		tab_timeline_reset();
	}

	g_context->ddirty = 4;

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_seammap_cached                            = false;
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

	g_project->stages                      = NULL;
	g_context->paint_object->base->visible = true;
	tab_stages_init();

	// Wait for add_mesh calls to finish
	sys_notify_on_next_frame(&import_mesh_finish_import, NULL);
}

void import_mesh_add_mesh(raw_mesh_t *mesh) {
	if (mesh->texa == NULL) {
		i32 verts  = mesh->posa->length / 4;
		mesh->texa = i16_array_create(verts * 2);
	}

	mesh_data_t *raw = import_mesh_raw_mesh(mesh);

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->skin_blob   = mesh->blob;
	md->_->skin_frame  = -1;
	md->_->owns_arrays = true;

	object_t      *parent = import_mesh_append ? NULL : g_context->paint_object->base;
	mesh_object_t *object = scene_add_mesh_object(md, g_context->paint_object->material, parent);
	object->base->name    = mesh->name;

	// Ensure unique names
	char *uname = _import_mesh_unique_name(object->base->name);
	if (!string_equals(uname, object->base->name)) {
		object->base->name = string_copy(uname);
		raw->name          = string_copy(uname);
	}

	any_array_push(g_project->_->paint_objects, object);
	tab_stages_add_object(object->base->name);
	if (import_mesh_append && import_mesh_appended == NULL) {
		import_mesh_appended = object;
	}

	g_context->ddirty = 4;

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	util_uv_uvmap_cached                              = false;
	util_uv_trianglemap_cached                        = false;
	util_uv_seammap_cached                            = false;
	util_uv_dilatemap_cached                          = false;
}

mesh_data_t *import_mesh_raw_mesh(raw_mesh_t *mesh) {
	mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = mesh->name,
	                                            .vertex_arrays = any_array_create_from_raw(
	                                                (void *[]){
	                                                    ALLOC_INIT(vertex_array_t, {.values = mesh->posa, .attrib = "pos", .data = "short4norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = mesh->nora, .attrib = "nor", .data = "short2norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = mesh->texa, .attrib = "tex", .data = "short2norm"}),
	                                                },
	                                                3),
	                                            .index_array = mesh->inda,
	                                            .scale_pos   = mesh->scale_pos,
	                                            .scale_tex   = mesh->scale_tex});
	if (mesh->texa1 != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = mesh->texa1, .attrib = "tex1", .data = "short2norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	if (mesh->cola != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = mesh->cola, .attrib = "col", .data = "short4norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	return raw;
}
