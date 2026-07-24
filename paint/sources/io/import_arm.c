
#include "../global.h"

scene_t *scene_raw_gc;

void import_arm_run_project_on_next_frame(void *_) {
	// Once envmap is imported
	scene_world->strength       = g_project->envmap_strength;
	g_context->envmap_angle     = g_project->envmap_angle;
	g_context->show_envmap_blur = g_project->envmap_blur;
	if (g_context->show_envmap_blur) {
		scene_world->_->envmap = scene_world->_->radiance_mipmaps->buffer[0];
	}
}

void import_arm_run_mesh_on_next_frame(void *_) {
	layers_init();
}

void import_arm_run_mesh(project_t *raw) {
	g_project->_->paint_objects = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < raw->mesh_datas->length; ++i) {
		mesh_data_t   *md     = mesh_data_create(raw->mesh_datas->buffer[i]);
		mesh_object_t *object = NULL;
		if (i == 0) {
			mesh_object_set_data(g_context->paint_object, md);
			object = g_context->paint_object;
		}
		else {
			object               = scene_add_mesh_object(md, g_context->paint_object->material, g_context->paint_object->base);
			object->base->name   = md->name;
			object->skip_context = "paint";
			md->_->handle        = md->name;
			any_map_set(data_cached_meshes, md->_->handle, md);
		}
		object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
		transform_build_matrix(object->base->transform);
		object->base->name = md->name;
		any_array_push(g_project->_->paint_objects, object);
		util_mesh_merge(NULL);
		viewport_scale_to_bounds(2.0);
	}
	sys_notify_on_next_frame(&import_arm_run_mesh_on_next_frame, NULL);
	history_reset();
}

void import_arm_run_material_from_project_on_next_frame(slot_material_t_array_t *imported) {
	for (i32 i = 0; i < imported->length; ++i) {
		slot_material_t *m = imported->buffer[i];
		context_set_material(m);
		make_material_parse_paint_material(true);
		util_render_make_material_preview();
	}
}

bool import_arm_group_exists(ui_node_canvas_t *c) {
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g     = g_project->_->material_groups->buffer[i];
		char         *cname = g->canvas->name;
		if (string_equals(cname, c->name)) {
			return true;
		}
	}
	return false;
}

void import_arm_rename_group(char *name, slot_material_t_array_t *materials, ui_node_canvas_t_array_t *groups) {
	for (i32 i = 0; i < materials->length; ++i) {
		slot_material_t *m = materials->buffer[i];
		for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
			ui_node_t *n = m->canvas->nodes->buffer[i];
			if (string_equals(n->type, "GROUP") && string_equals(n->name, name)) {
				n->name = string("%s.1", n->name);
			}
		}
	}
	for (i32 i = 0; i < groups->length; ++i) {
		ui_node_canvas_t *c = groups->buffer[i];
		if (string_equals(c->name, name)) {
			c->name = string("%s.1", c->name);
		}
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			if (string_equals(n->type, "GROUP") && string_equals(n->name, name)) {
				n->name = string("%s.1", n->name);
			}
		}
	}
}

void import_arm_make_pink(char *abs) {
	console_error(string("%s %s", strings_could_not_locate_texture(), abs));
	u8_array_t *b       = u8_array_create(4);
	b->buffer[0]        = 255;
	b->buffer[1]        = 0;
	b->buffer[2]        = 255;
	b->buffer[3]        = 255;
	gpu_texture_t *pink = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
	any_map_set(data_cached_textures, abs, pink);
}

void import_arm_init_nodes(ui_node_t_array_t *nodes) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *node = nodes->buffer[i];
		if (string_equals(node->type, "TEX_IMAGE")) {
			node->buttons->buffer[0]->default_value = f32_array_create_x(base_get_asset_index(u8_array_to_string(node->buttons->buffer[0]->data)));
			node->buttons->buffer[0]->data          = u8_array_create_from_string("");
		}
	}
}

void import_arm_unpack_asset(project_t *project, char *abs, char *file, bool gc_copy) {
	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	for (i32 i = 0; i < project->packed_assets->length; ++i) {
		packed_asset_t *pa = project->packed_assets->buffer[i];
#ifdef IRON_WINDOWS
		pa->name = string_copy(string_replace_all(pa->name, "/", "\\"));
#else
		pa->name = string_copy(string_replace_all(pa->name, "\\", "/"));
#endif
		pa->name     = string_copy(path_normalize(pa->name));
		bool matches = string_equals(pa->name, file) || string_equals(pa->name, abs);
		if (!matches) {
			char *pa_name   = substring(pa->name, string_last_index_of(pa->name, PATH_SEP) + 1, string_length(pa->name));
			char *file_name = substring(file, string_last_index_of(file, PATH_SEP) + 1, string_length(file));
			matches         = string_equals(pa_name, file_name);
		}
		if (matches) {
			pa->name = string_copy(abs);
			if (!project_packed_asset_exists(g_project->packed_assets, pa->name)) {

				if (gc_copy) {
					packed_asset_t *pa_gc =
					    GC_ALLOC_INIT(packed_asset_t, {.name = string_copy(pa->name), .bytes = u8_array_create_from_array(pa->bytes)}); // project will get GCed
					pa = pa_gc;
				}

				any_array_push(g_project->packed_assets, pa);
			}
			gpu_texture_t *image = gpu_create_texture_from_encoded_bytes(pa->bytes, ends_with(pa->name, ".jpg") ? ".jpg" : ".png");
			any_map_set(data_cached_textures, abs, image);
			break;
		}
	}
}

void import_arm_run_material_from_project(project_t *project, char *path) {
	char *base = path_base_dir(path);
	for (i32 i = 0; i < project->assets->length; ++i) {
		char *file = project->assets->buffer[i];
#ifdef IRON_WINDOWS
		file = string_copy(string_replace_all(file, "/", "\\"));
#else
		file = string_copy(string_replace_all(file, "\\", "/"));
#endif
		// Convert image path from relative to absolute
		char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
		if (project->packed_assets != NULL) {
			abs = string_copy(path_normalize(abs));
			import_arm_unpack_asset(project, abs, file, true);
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		import_texture_run(abs, true);
	}

	material_data_t *m0 = data_get_material("Scene", "Material");

	slot_material_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);

	for (i32 i = 0; i < project->material_nodes->length; ++i) {
		ui_node_canvas_t *c = util_clone_canvas(project->material_nodes->buffer[i]); // project will get GCed
		import_arm_init_nodes(c->nodes);
		g_context->material = slot_material_create(m0, c);
		any_array_push(g_project->_->materials, g_context->material);
		any_array_push(imported, g_context->material);
		history_new_material();
	}

	if (project->material_groups != NULL) {
		for (i32 i = 0; i < project->material_groups->length; ++i) {
			project->material_groups->buffer[i] = util_clone_canvas(project->material_groups->buffer[i]);
		}
		for (i32 i = 0; i < project->material_groups->length; ++i) {
			ui_node_canvas_t *c = project->material_groups->buffer[i];
			while (import_arm_group_exists(c)) {
				import_arm_rename_group(c->name, imported, project->material_groups); // Ensure unique group name
			}
			import_arm_init_nodes(c->nodes);
			node_group_t *ng = GC_ALLOC_INIT(node_group_t, {.canvas = c, .nodes = ui_nodes_create()});
			any_array_push(g_project->_->material_groups, ng);
		}
	}

	sys_notify_on_next_frame(&import_arm_run_material_from_project_on_next_frame, imported);
	gc_unroot(ui_nodes_group_stack);
	ui_nodes_group_stack = any_array_create_from_raw((void *[]){}, 0);
	gc_root(ui_nodes_group_stack);
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	data_delete_blob(path);
}

void import_arm_run_swatches_from_project(project_t *project, char *path, bool replace_existing) {
	if (replace_existing) {
		g_project->swatches = any_array_create_from_raw((void *[]){}, 0);

		if (project->swatches == NULL) { // No swatches contained
			any_array_push(g_project->swatches, project_make_swatch(0xffffffff));
		}
	}

	if (project->swatches != NULL) {
		for (i32 i = 0; i < project->swatches->length; ++i) {
			swatch_color_t *s = util_clone_swatch_color(project->swatches->buffer[i]);
			any_array_push(g_project->swatches, s);
		}
	}
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	data_delete_blob(path);
}

void import_arm_run_project(char *path) {
	buffer_t  *b = data_get_blob(path);
	project_t *project;
	bool       import_as_mesh = false;
#ifdef IRON_WINDOWS
	bool is_cloud = string_index_of(path, "\\cloud\\") >= 0;
#else
	bool is_cloud = string_index_of(path, "/cloud/") >= 0;
#endif
	if (import_arm_is_old(b) && !is_cloud) {
		project = import_arm_from_old(b);
	}
	else if (!import_arm_has_version(b)) {
		import_as_mesh = true;
		gc_unroot(scene_raw_gc);
		scene_raw_gc = armpack_decode(b);
		gc_root(scene_raw_gc);
		project = GC_ALLOC_INIT(project_t, {.mesh_datas = scene_raw_gc->mesh_datas});
	}
	else {
		project = armpack_decode(b);
	}

	if (project->version != NULL && project->layer_datas == NULL) {
		// Import as material
		if (project->material_nodes != NULL) {
			import_arm_run_material_from_project(project, path);
		}
		// Import as brush
		else if (project->brush_nodes != NULL) {
			import_arm_run_brush_from_project(project, path);
		}
		// Import as swatches
		else if (project->swatches != NULL) {
			import_arm_run_swatches_from_project(project, path, false);
		}
		return;
	}

	g_context->layers_preview_dirty = true;
	g_context->layer_filter         = 0;

	project_new(import_as_mesh);
	g_project->_->filepath = string_copy(path);
	gc_unroot(ui_files_filename);
	ui_files_filename = string_copy(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, ".")));
	gc_root(ui_files_filename);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	sys_title_set(ui_files_filename);
#else
	sys_title_set(string("%s - %s", ui_files_filename, manifest_title));
#endif

	// Import as mesh instead
	if (import_as_mesh) {
		import_arm_run_mesh(project);
		return;
	}

// Save to recent
#ifdef IRON_IOS
	char *recent_path = substring(path, string_last_index_of(path, "/") + 1, string_length(path));
#else
	char *recent_path = path;
#endif
#ifdef IRON_WINDOWS
	recent_path = string_copy(string_replace_all(recent_path, "\\", "/"));
#endif
	string_array_t *recent = g_config->recent_projects;
	string_array_remove(recent, recent_path);
	array_insert(recent, 0, recent_path);
	config_save();

	project->_ = g_project->_; // Carry over runtime arrays set up by project_new
	gc_unroot(g_project);
	g_project = project;
	gc_root(g_project);
	layer_data_t *l0                     = g_project->layer_datas->buffer[0];
	base_res_handle->i                   = config_get_texture_res_pos(l0->res);
	base_res_x_handle->f                 = (f32)l0->res;
	base_res_y_handle->f                 = (f32)l0->res;
	texture_bits_t bits_pos              = l0->bpp == 8 ? TEXTURE_BITS_BITS8 : l0->bpp == 16 ? TEXTURE_BITS_BITS16 : TEXTURE_BITS_BITS32;
	base_bits_handle->i                  = bits_pos;
	i32                  bytes_per_pixel = math_floor(l0->bpp / 8.0);
	gpu_texture_format_t format          = l0->bpp == 8 ? GPU_TEXTURE_FORMAT_RGBA32 : l0->bpp == 16 ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA128;

	char *base = path_base_dir(path);
	if (g_project->envmap != NULL) {
		g_project->envmap = data_is_abs(g_project->envmap) ? g_project->envmap : string("%s%s", base, g_project->envmap);
#ifdef IRON_WINDOWS
		g_project->envmap = string_copy(string_replace_all(g_project->envmap, "/", "\\"));
#else
		g_project->envmap = string_copy(string_replace_all(g_project->envmap, "\\", "/"));
#endif
	}

	if (g_project->camera_world != NULL) {
		scene_camera->base->transform->local = mat4_from_f32_array(g_project->camera_world, 0);
		transform_decompose(scene_camera->base->transform);
		scene_camera->data->fov = g_project->camera_fov;
		camera_object_build_proj(scene_camera, -1.0);
		f32_array_t *origin = g_project->camera_origin;
		camera_origins[0]   = (vec4_t){origin->buffer[0], origin->buffer[1], origin->buffer[2], 1.0};
	}

	for (i32 i = 0; i < g_project->assets->length; ++i) {
		char *file = g_project->assets->buffer[i];
#ifdef IRON_WINDOWS
		file = string_copy(string_replace_all(file, "/", "\\"));
#else
		file = string_copy(string_replace_all(file, "\\", "/"));
#endif
		// Convert image path from relative to absolute
		char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
		if (g_project->packed_assets != NULL) {
			abs = string_copy(path_normalize(abs));
			import_arm_unpack_asset(g_project, abs, file, false);
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		bool hdr_as_envmap = ends_with(abs, ".hdr") && string_equals(g_project->envmap, abs);
		import_texture_run(abs, hdr_as_envmap);
	}

	if (g_project->font_assets != NULL) {
		for (i32 i = 0; i < g_project->font_assets->length; ++i) {
			char *file = g_project->font_assets->buffer[i];
#ifdef IRON_WINDOWS
			file = string_copy(string_replace_all(file, "/", "\\"));
#else
			file = string_copy(string_replace_all(file, "\\", "/"));
#endif
			// Convert font path from relative to absolute
			char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
			if (iron_file_exists(abs)) {
				import_font_run(abs);
			}
		}
	}

	if (g_project->sound_assets != NULL) {
		for (i32 i = 0; i < g_project->sound_assets->length; ++i) {
			char *file = g_project->sound_assets->buffer[i];
#ifdef IRON_WINDOWS
			file = string_copy(string_replace_all(file, "/", "\\"));
#else
			file = string_copy(string_replace_all(file, "\\", "/"));
#endif
			// Convert sound path from relative to absolute
			char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
			if (iron_file_exists(abs)) {
				import_sound_run(abs);
			}
		}
	}

	mesh_data_t *md = mesh_data_create(g_project->mesh_datas->buffer[0]);

	mesh_object_set_data(g_context->paint_object, md);
	g_context->paint_object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
	transform_build_matrix(g_context->paint_object->base->transform);
	g_context->paint_object->base->name = md->name;
	g_project->_->paint_objects         = any_array_create_from_raw(
        (void *[]){
            g_context->paint_object,
        },
        1);

	for (i32 i = 1; i < g_project->mesh_datas->length; ++i) {
		mesh_data_t   *raw    = g_project->mesh_datas->buffer[i];
		mesh_data_t   *md     = mesh_data_create(raw);
		mesh_object_t *object = scene_add_mesh_object(md, g_context->paint_object->material, g_context->paint_object->base);
		object->base->name    = md->name;
		object->skip_context  = "paint";
		any_array_push(g_project->_->paint_objects, object);
	}

	transform_set_matrix(g_context->paint_object->base->transform, mat4_from_f32_array(g_project->mesh_transforms->buffer[0], 0));
	for (i32 i = 1; i < g_project->mesh_datas->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		transform_set_matrix(o->base->transform, mat4_from_f32_array(g_project->mesh_transforms->buffer[i], 0));
	}

	if (g_project->mesh_assets != NULL && g_project->mesh_assets->length > 0) {
		char *file             = g_project->mesh_assets->buffer[0];
		char *abs              = data_is_abs(file) ? file : string("%s%s", base, file);
		g_project->mesh_assets = any_array_create_from_raw(
		    (void *[]){
		        abs,
		    },
		    1);
	}

	// No mask by default
	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}

	context_select_paint_object(context_main_object());
	g_context->paint_object->skip_context   = "paint";
	g_context->merged_object->base->visible = true;

	gpu_texture_t *tex = g_project->_->layers->buffer[0]->texpaint;
	if (tex->width != config_get_texture_res_x() || tex->height != config_get_texture_res_y()) {
		if (history_undo_layers != NULL) {
			for (i32 i = 0; i < history_undo_layers->length; ++i) {
				slot_layer_t *l = history_undo_layers->buffer[i];
				slot_layer_resize_and_set_bits(l);
			}
		}
		any_map_t       *rts              = render_path_render_targets;
		render_target_t *blend0           = any_map_get(rts, "texpaint_blend0");
		gpu_texture_t   *_texpaint_blend0 = blend0->_image;
		gpu_delete_texture(_texpaint_blend0);
		blend0->width  = config_get_texture_res_x();
		blend0->height = config_get_texture_res_y();
		blend0->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);

		render_target_t *blend1           = any_map_get(rts, "texpaint_blend1");
		gpu_texture_t   *_texpaint_blend1 = blend1->_image;
		gpu_delete_texture(_texpaint_blend1);
		blend1->width  = config_get_texture_res_x();
		blend1->height = config_get_texture_res_y();
		blend1->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);

		g_context->brush_blend_dirty = true;
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_unload(l);
	}
	g_project->_->layers = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld       = g_project->layer_datas->buffer[i];
		bool          is_group = ld->texpaint == NULL;
		bool          is_mask  = ld->texpaint != NULL && ld->texpaint_nor == NULL;
		slot_layer_t *l        = slot_layer_create("", is_group ? LAYER_SLOT_TYPE_GROUP : is_mask ? LAYER_SLOT_TYPE_MASK : LAYER_SLOT_TYPE_LAYER, NULL);
		if (ld->name != NULL) {
			l->name = string_copy(ld->name);
		}
		l->visible = ld->visible;
		any_array_push(g_project->_->layers, l);
		if (!is_group) {
			gpu_texture_t *_texpaint      = NULL;
			gpu_texture_t *_texpaint_nor  = NULL;
			gpu_texture_t *_texpaint_pack = NULL;

			if (is_mask) {
				_texpaint = gpu_create_texture_from_bytes(lz4_decode(ld->texpaint, ld->res * ld->res * 4), ld->res, ld->res, GPU_TEXTURE_FORMAT_RGBA32);
				draw_begin(l->texpaint, false, 0);
				// draw_set_pipeline(pipes_copy8);
				draw_set_pipeline(g_project->is_bgra ? pipes_copy_bgra : pipes_copy); // Full bits for undo support, R8 is used
				draw_image(_texpaint, 0, 0);
				draw_set_pipeline(NULL);
				draw_end();
			}
			else { // Layer
				// TODO: create render target from bytes
				_texpaint = gpu_create_texture_from_bytes(lz4_decode(ld->texpaint, ld->res * ld->res * 4 * bytes_per_pixel), ld->res, ld->res, format);
				draw_begin(l->texpaint, false, 0);
				draw_set_pipeline(g_project->is_bgra ? pipes_copy_bgra : pipes_copy);
				draw_image(_texpaint, 0, 0);
				draw_set_pipeline(NULL);
				draw_end();

				_texpaint_nor = gpu_create_texture_from_bytes(lz4_decode(ld->texpaint_nor, ld->res * ld->res * 4 * bytes_per_pixel), ld->res, ld->res, format);
				draw_begin(l->texpaint_nor, false, 0);
				draw_set_pipeline(g_project->is_bgra ? pipes_copy_bgra : pipes_copy);
				draw_image(_texpaint_nor, 0, 0);
				draw_set_pipeline(NULL);
				draw_end();

				_texpaint_pack =
				    gpu_create_texture_from_bytes(lz4_decode(ld->texpaint_pack, ld->res * ld->res * 4 * bytes_per_pixel), ld->res, ld->res, format);
				draw_begin(l->texpaint_pack, false, 0);
				draw_set_pipeline(g_project->is_bgra ? pipes_copy_bgra : pipes_copy);
				draw_image(_texpaint_pack, 0, 0);
				draw_set_pipeline(NULL);
				draw_end();

				if (ld->texpaint_sculpt != NULL) {
					render_target_t *t = render_target_create();
					t->name            = string("texpaint_sculpt%s", l->ext);
					t->width           = ld->res;
					t->height          = ld->res;
					t->format          = "RGBA128";
					l->texpaint_sculpt = render_path_create_render_target(t)->_image;

					gpu_texture_t *_texpaint_sculpt =
					    gpu_create_texture_from_bytes(lz4_decode(ld->texpaint_sculpt, ld->res * ld->res * 4 * 4), ld->res, ld->res, GPU_TEXTURE_FORMAT_RGBA128);
					draw_begin(l->texpaint_sculpt, false, 0);
					draw_set_pipeline(pipes_copy128);
					draw_image(_texpaint_sculpt, 0, 0);
					draw_set_pipeline(NULL);
					draw_end();
					gpu_delete_texture(_texpaint_sculpt);
				}
			}

			l->scale   = ld->uv_scale;
			l->angle   = ld->uv_rot;
			l->uv_type = ld->uv_type;
			l->uv_map  = ld->uv_map;
			if (ld->decal_mat != NULL) {
				l->decal_mat = mat4_from_f32_array(ld->decal_mat, 0);
			}
			l->mask_opacity = ld->opacity_mask;
			l->object_mask  = ld->object_mask;
			l->blending     = ld->blending;

			l->paint_base         = ld->paint_base;
			l->paint_opac         = ld->paint_opac;
			l->paint_occ          = ld->paint_occ;
			l->paint_rough        = ld->paint_rough;
			l->paint_met          = ld->paint_met;
			l->paint_nor          = ld->paint_nor;
			l->paint_nor_blend    = ld->paint_nor_blend;
			l->paint_height       = ld->paint_height;
			l->paint_height_blend = ld->paint_height_blend;
			l->paint_emis         = ld->paint_emis;
			l->paint_subs         = ld->paint_subs;
			l->path_points        = ld->path_points;
			l->path_points_world  = ld->path_points_world;
			l->path_points_camera = ld->path_points_camera;
			l->path_points_parent = ld->path_points_parent;
			l->path_tool          = ld->path_tool;
			l->path_curved        = ld->path_curved;

			gpu_delete_texture(_texpaint);
			if (_texpaint_nor != NULL) {
				gpu_delete_texture(_texpaint_nor);
			}
			if (_texpaint_pack != NULL) {
				gpu_delete_texture(_texpaint_pack);
			}
			gc_run();
		}
	}

	// Assign parents to groups and masks
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld = g_project->layer_datas->buffer[i];
		if (ld->parent >= 0) {
			g_project->_->layers->buffer[i]->parent = g_project->_->layers->buffer[ld->parent];
		}
	}

	context_set_layer(g_project->_->layers->buffer[0]);

	// Materials
	material_data_t *m0     = data_get_material("Scene", "Material");
	g_project->_->materials = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->material_nodes->length; ++i) {
		ui_node_canvas_t *n = g_project->material_nodes->buffer[i];
		import_arm_init_nodes(n->nodes);
		g_context->material = slot_material_create(m0, n);

		material_data2_t *md                 = g_project->material_datas->buffer[i];
		g_context->material->paint_base      = md->paint_base;
		g_context->material->paint_opac      = md->paint_opac;
		g_context->material->paint_occ       = md->paint_occ;
		g_context->material->paint_rough     = md->paint_rough;
		g_context->material->paint_met       = md->paint_met;
		g_context->material->paint_nor       = md->paint_nor;
		g_context->material->paint_height    = md->paint_height;
		g_context->material->paint_emis      = md->paint_emis;
		g_context->material->paint_subs      = md->paint_subs;
		g_context->material->paint_opac_mode = md->opac_mode;

		any_array_push(g_project->_->materials, g_context->material);
	}

	ui_nodes_hwnd->redraws = 2;
	gc_unroot(ui_nodes_group_stack);
	ui_nodes_group_stack = any_array_create_from_raw((void *[]){}, 0);
	gc_root(ui_nodes_group_stack);
	g_project->_->material_groups = any_array_create_from_raw((void *[]){}, 0);
	if (g_project->material_groups != NULL) {
		for (i32 i = 0; i < g_project->material_groups->length; ++i) {
			ui_node_canvas_t *g  = g_project->material_groups->buffer[i];
			node_group_t     *ng = GC_ALLOC_INIT(node_group_t, {.canvas = g, .nodes = ui_nodes_create()});
			any_array_push(g_project->_->material_groups, ng);
		}
	}

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m  = g_project->_->materials->buffer[i];
		g_context->material = m;
		make_material_parse_paint_material(true);
		util_render_make_material_preview();
	}

	g_project->_->brushes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->brush_nodes->length; ++i) {
		ui_node_canvas_t *n = g_project->brush_nodes->buffer[i];
		import_arm_init_nodes(n->nodes);
		g_context->brush = slot_brush_create(n);
		any_array_push(g_project->_->brushes, g_context->brush);
		make_material_parse_brush();
		brush_output_node_parse_inputs();
		util_render_make_brush_preview();
	}

	// Fill layers and path layers materials
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld       = g_project->layer_datas->buffer[i];
		slot_layer_t *l        = g_project->_->layers->buffer[i];
		bool          is_group = ld->texpaint == NULL;
		if (!is_group) {
			l->fill_material = ld->fill_material > -1 ? g_project->_->materials->buffer[ld->fill_material] : NULL;
			l->path_material = ld->path_material > -1 ? g_project->_->materials->buffer[ld->path_material] : NULL;
		}
	}

	if (g_project->mesh_materials != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->mesh_materials->length; ++i) {
			i32 mat_index = g_project->mesh_materials->buffer[i];
			if (mat_index >= 0) {
				tab_meshes_set_override(g_project->_->paint_objects->buffer[i], mat_index);
			}
		}
	}

	if (g_project->mesh_parents != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->mesh_parents->length; ++i) {
			i32       parent_index = g_project->mesh_parents->buffer[i];
			object_t *parent       = parent_index >= 0 ? g_project->_->paint_objects->buffer[parent_index]->base : NULL;
			object_set_parent(g_project->_->paint_objects->buffer[i]->base, parent);
		}
	}

	tab_timeline_import(g_project);

	// Select the first stage
	if (g_project->stages != NULL && g_project->stages->length > 0) {
		tab_stages_selected = 0;
		tab_stages_apply(g_project->stages->buffer[0]);
	}

	sys_notify_on_next_frame(&import_arm_run_project_on_next_frame, NULL);

	base_update_workflow();
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	g_context->ddirty                                 = 4;
	data_delete_blob(path);
}

void import_arm_run_material(char *path) {
	buffer_t  *b = data_get_blob(path);
	project_t *project;
	if (import_arm_is_old(b)) {
		project = import_arm_from_old(b);
	}
	else {
		project = armpack_decode(b);
	}

	if (project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_material_from_project(project, path);
}

void import_arm_run_brush(char *path) {
	buffer_t  *b       = data_get_blob(path);
	project_t *project = armpack_decode(b);
	if (project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_brush_from_project(project, path);
}

void import_arm_run_brush_from_project(project_t *project, char *path) {
	char *base = path_base_dir(path);
	for (i32 i = 0; i < project->assets->length; ++i) {
		char *file = project->assets->buffer[i];
#ifdef IRON_WINDOWS
		file = string_copy(string_replace_all(file, "/", "\\"));
#else
		file = string_copy(string_replace_all(file, "\\", "/"));
#endif
		// Convert image path from relative to absolute
		char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
		if (project->packed_assets != NULL) {
			abs = string_copy(path_normalize(abs));
			import_arm_unpack_asset(project, abs, file, true);
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		import_texture_run(abs, true);
	}

	slot_brush_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);

	for (i32 i = 0; i < project->brush_nodes->length; ++i) {
		ui_node_canvas_t *c = util_clone_canvas(project->brush_nodes->buffer[i]);
		import_arm_init_nodes(c->nodes);
		g_context->brush = slot_brush_create(c);
		any_array_push(g_project->_->brushes, g_context->brush);
		any_array_push(imported, g_context->brush);
		make_material_parse_brush();
		brush_output_node_parse_inputs();
		util_render_make_brush_preview();
	}

	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	data_delete_blob(path);
}

void import_arm_run_swatches(char *path, bool replace_existing) {
	buffer_t  *b       = data_get_blob(path);
	project_t *project = armpack_decode(b);
	if (project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_swatches_from_project(project, path, replace_existing);
}

bool import_arm_has_version(buffer_t *b) {
	bool has_version = b->buffer[10] == 118; // 'v';
	return has_version;
}
