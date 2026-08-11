
#include "../global.h"

void export_arm_run_mesh(char *path, mesh_object_t_array_t *paint_objects) {
	mesh_data_t_array_t *mesh_datas = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *p = paint_objects->buffer[i];
		any_array_push(mesh_datas, p->data);
	}

	scene_t  *raw = GC_ALLOC_INIT(scene_t, {.mesh_datas = mesh_datas});
	buffer_t *b   = util_encode_scene(raw);

	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	iron_file_save_bytes(path, b, b->length + 1);
}

void export_arm_export_node(ui_node_t *n, asset_t_array_t *assets) {
	if (string_equals(n->type, "TEX_IMAGE")) {
		i32 index = n->buttons->buffer[0]->default_value->buffer[0];
		if (index > 9000) { // 9999 - Texture deleted
			n->buttons->buffer[0]->data = u8_array_create_from_string("");
		}
		else {
			n->buttons->buffer[0]->data = u8_array_create_from_string(base_combo_enum_texts(n->type)->buffer[index]);
		}
		if (assets != NULL) {
			asset_t *asset = g_project->_->assets->buffer[index];
			if (array_index_of(assets, asset) == -1) {
				any_array_push(assets, asset);
			}
		}
	}
}

string_array_t *export_arm_assets_to_files(char *project_path, asset_t_array_t *assets) {
	string_array_t *texture_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < assets->length; ++i) {
		asset_t *a = assets->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(a->file, 0)[0];
#endif
		// Convert image path from absolute to relative
		if (same_drive) {
			any_array_push(texture_files, path_to_relative(project_path, a->file));
		}
		else {
			any_array_push(texture_files, a->file);
		}
	}
	return texture_files;
}

string_array_t *export_arm_meshes_to_files(char *project_path) {
	string_array_t *mesh_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->mesh_assets->length; ++i) {
		char *file = g_project->mesh_assets->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(file, 0)[0];
#endif
		// Convert mesh path from absolute to relative
		if (same_drive) {
			any_array_push(mesh_files, path_to_relative(project_path, file));
		}
		else {
			any_array_push(mesh_files, file);
		}
	}
	return mesh_files;
}

string_array_t *export_arm_fonts_to_files(char *project_path, slot_font_t_array_t *fonts) {
	string_array_t *font_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 1; i < fonts->length; ++i) {
		slot_font_t *f = fonts->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(f->file, 0)[0];
#endif
		// Convert font path from absolute to relative
		if (same_drive) {
			any_array_push(font_files, path_to_relative(project_path, f->file));
		}
		else {
			any_array_push(font_files, f->file);
		}
	}
	return font_files;
}

string_array_t *export_arm_sounds_to_files(char *project_path, slot_sound_t_array_t *sounds) {
	string_array_t *sound_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < sounds->length; ++i) {
		slot_sound_t *f = sounds->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(f->file, 0)[0];
#endif
		// Convert sound path from absolute to relative
		if (same_drive) {
			any_array_push(sound_files, path_to_relative(project_path, f->file));
		}
		else {
			any_array_push(sound_files, f->file);
		}
	}
	return sound_files;
}

void export_arm_run_project() {

	tab_timeline_prepare_save();
	tab_scripts_strip_trailing_whitespace();

	workflow_t _workflow = g_config->workflow;
	g_config->workflow   = WORKFLOW_PBR;
	base_update_workflow();

	ui_node_canvas_t_array_t *mnodes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t  *m = g_project->_->materials->buffer[i];
		ui_node_canvas_t *c = util_clone_canvas(m->canvas);
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			export_arm_export_node(n, NULL);
		}
		any_array_push(mnodes, c);
	}

	g_config->workflow = _workflow;
	base_update_workflow();

	ui_node_canvas_t_array_t *bnodes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		slot_brush_t     *b = g_project->_->brushes->buffer[i];
		ui_node_canvas_t *c = util_clone_canvas(b->canvas);
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			export_arm_export_node(n, NULL);
		}
		any_array_push(bnodes, c);
	}

	ui_node_canvas_t_array_t *mgroups = NULL;
	if (g_project->_->material_groups->length > 0) {
		mgroups = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
			node_group_t     *g = g_project->_->material_groups->buffer[i];
			ui_node_canvas_t *c = util_clone_canvas(g->canvas);
			for (i32 i = 0; i < c->nodes->length; ++i) {
				ui_node_t *n = c->nodes->buffer[i];
				export_arm_export_node(n, NULL);
			}
			any_array_push(mgroups, c);
		}
	}

	material_data2_t_array_t *mdata2 = NULL;
	if (g_project->_->materials->length > 0) {
		mdata2 = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t  *m = g_project->_->materials->buffer[i];
			material_data2_t *d = GC_ALLOC_INIT(material_data2_t, {
			                                                          .paint_base   = m->paint_base,
			                                                          .paint_opac   = m->paint_opac,
			                                                          .paint_occ    = m->paint_occ,
			                                                          .paint_rough  = m->paint_rough,
			                                                          .paint_met    = m->paint_met,
			                                                          .paint_nor    = m->paint_nor,
			                                                          .paint_height = m->paint_height,
			                                                          .paint_emis   = m->paint_emis,
			                                                          .paint_subs   = m->paint_subs,
			                                                          .opac_mode    = m->paint_opac_mode,
			                                                      });
			any_array_push(mdata2, d);
		}
	}

	mesh_data_t_array_t *md = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		any_array_push(md, p->data);
	}

	string_array_t *texture_files = export_arm_assets_to_files(g_project->_->filepath, g_project->_->assets);

	string_array_t *font_files  = export_arm_fonts_to_files(g_project->_->filepath, g_project->_->fonts);
	string_array_t *sound_files = export_arm_sounds_to_files(g_project->_->filepath, g_project->_->sounds);
	string_array_t *mesh_files  = export_arm_meshes_to_files(g_project->_->filepath);

	i32 bits_pos = base_bits_handle->i;
	i32 bpp      = bits_pos == TEXTURE_BITS_BITS8 ? 8 : bits_pos == TEXTURE_BITS_BITS16 ? 16 : 32;

	layer_data_t_array_t *ld = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		layer_data_t *d =
		    GC_ALLOC_INIT(layer_data_t, {.name               = l->name,
		                                 .res                = l->texpaint != NULL ? l->texpaint->width : g_project->_->layers->buffer[0]->texpaint->width,
		                                 .bpp                = bpp,
		                                 .texpaint           = l->texpaint != NULL ? lz4_encode(gpu_get_texture_pixels(l->texpaint)) : NULL,
		                                 .uv_scale           = l->scale,
		                                 .uv_rot             = l->angle,
		                                 .uv_type            = l->uv_type,
		                                 .uv_map             = l->uv_map,
		                                 .decal_mat          = l->uv_type == UV_TYPE_PROJECT ? mat4_to_f32_array(l->decal_mat) : NULL,
		                                 .opacity_mask       = l->mask_opacity,
		                                 .fill_material      = l->fill_material != NULL ? array_index_of(g_project->_->materials, l->fill_material) : -1,
		                                 .object_mask        = l->object_mask,
		                                 .blending           = l->blending,
		                                 .parent             = l->parent != NULL ? array_index_of(g_project->_->layers, l->parent) : -1,
		                                 .visible            = l->visible,
		                                 .texpaint_nor       = l->texpaint_nor != NULL ? lz4_encode(gpu_get_texture_pixels(l->texpaint_nor)) : NULL,
		                                 .texpaint_pack      = l->texpaint_pack != NULL ? lz4_encode(gpu_get_texture_pixels(l->texpaint_pack)) : NULL,
		                                 .texpaint_sculpt    = l->texpaint_sculpt != NULL ? lz4_encode(gpu_get_texture_pixels(l->texpaint_sculpt)) : NULL,
		                                 .paint_base         = l->paint_base,
		                                 .paint_opac         = l->paint_opac,
		                                 .paint_occ          = l->paint_occ,
		                                 .paint_rough        = l->paint_rough,
		                                 .paint_met          = l->paint_met,
		                                 .paint_nor          = l->paint_nor,
		                                 .paint_nor_blend    = l->paint_nor_blend,
		                                 .paint_height       = l->paint_height,
		                                 .paint_height_blend = l->paint_height_blend,
		                                 .paint_emis         = l->paint_emis,
		                                 .paint_subs         = l->paint_subs,
		                                 .path_points        = l->path_points,
		                                 .path_points_world  = l->path_points_world,
		                                 .path_points_camera = l->path_points_camera,
		                                 .path_points_parent = l->path_points_parent,
		                                 .path_tool          = l->path_tool,
		                                 .path_curved        = l->path_curved,
		                                 .path_material      = l->path_material != NULL ? array_index_of(g_project->_->materials, l->path_material) : -1});
		any_array_push(ld, d);
	}

	packed_asset_t_array_t *packed_assets = (g_project->packed_assets == NULL || g_project->packed_assets->length == 0) ? NULL : g_project->packed_assets;
#ifdef IRON_IOS
	bool same_drive = false;
#else
	bool same_drive = g_project->envmap != NULL ? char_at(g_project->_->filepath, 0)[0] == char_at(g_project->envmap, 0)[0] : true;
#endif

	g_project->version         = string_copy(manifest_version_project);
	g_project->material_groups = mgroups;
	g_project->material_datas  = mdata2;
	g_project->assets          = texture_files;
	g_project->packed_assets   = packed_assets;
	g_project->swatches        = g_project->swatches;
	g_project->envmap = g_project->envmap != NULL ? (same_drive ? path_to_relative(g_project->_->filepath, g_project->envmap) : g_project->envmap) : NULL;
	g_project->envmap_strength = scene_world->strength;
	g_project->envmap_angle    = g_context->envmap_angle;
	g_project->envmap_blur     = g_context->show_envmap_blur;
	g_project->camera_world    = mat4_to_f32_array(scene_camera->base->transform->local);
	g_project->camera_origin   = vec3_to_f32_array(camera_origins[0]);
	g_project->camera_fov      = scene_camera->data->fov;

	// g_project.mesh_datas = md; // TODO: fix GC ref
	if (g_project->mesh_datas == NULL) {
		g_project->mesh_datas = md;
	}
	else {
		g_project->mesh_datas->length = 0;
		for (i32 i = 0; i < md->length; ++i) {
			any_array_push(g_project->mesh_datas, md->buffer[i]);
		}
	}

	f32_array_t_array_t *mesh_transforms = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p  = g_project->_->paint_objects->buffer[i];
		f32_array_t   *ar = f32_array_create_from_raw(p->base->transform->local.m, 16);
		any_array_push(mesh_transforms, ar);
	}
	g_project->mesh_transforms = mesh_transforms;

	// Source mesh bytes
	// Meshes with no skin data store an empty buffer
	buffer_t_array_t *mesh_skins = any_array_create_from_raw((void *[]){}, 0);
	bool              has_skins  = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		buffer_t *blob = g_project->_->paint_objects->buffer[i]->data->_->skin_blob;
		any_array_push(mesh_skins, blob != NULL ? blob : buffer_create(0));
		has_skins = has_skins || blob != NULL;
	}
	g_project->mesh_skins = has_skins ? mesh_skins : NULL;

	if (g_project->mesh_materials != NULL) {
		g_project->mesh_materials = i32_array_create(g_project->_->paint_objects->length);
		for (i32 i = 0; i < g_project->mesh_materials->length; ++i) {
			g_project->mesh_materials->buffer[i] = tab_meshes_get_override(g_project->_->paint_objects->buffer[i]);
		}
	}

	if (g_project->mesh_parents != NULL) {
		g_project->mesh_parents = i32_array_create(g_project->_->paint_objects->length);
		for (i32 i = 0; i < g_project->mesh_parents->length; ++i) {
			object_t *parent                   = g_project->_->paint_objects->buffer[i]->base->parent;
			g_project->mesh_parents->buffer[i] = -1; // No parent
			if (parent != NULL && parent != _scene_scene_parent) {
				for (i32 j = 0; j < g_project->mesh_parents->length; ++j) {
					if (g_project->_->paint_objects->buffer[j]->base == parent) {
						g_project->mesh_parents->buffer[i] = j;
						break;
					}
				}
			}
		}
	}

	if (g_project->mesh_physics_shapes != NULL) {
		g_project->mesh_physics_shapes = i32_array_create(g_project->_->paint_objects->length);
		g_project->mesh_physics_masses = f32_array_create(g_project->_->paint_objects->length);
		for (i32 i = 0; i < g_project->mesh_physics_shapes->length; ++i) {
			asim_body_t *pb                           = g_project->_->paint_objects->buffer[i]->base->_->body;
			g_project->mesh_physics_shapes->buffer[i] = pb != NULL ? pb->shape : -1; // No physics
			g_project->mesh_physics_masses->buffer[i] = pb != NULL ? pb->mass : 0.0;
		}
	}

	g_project->material_nodes = mnodes;
	g_project->brush_nodes    = bnodes;
	g_project->layer_datas    = ld;
	g_project->font_assets    = font_files;
	g_project->sound_assets   = sound_files;
	g_project->mesh_assets    = mesh_files;

	tab_timeline_export(g_project);

#ifdef IRON_BGRA
	g_project->is_bgra = true;
#else
	g_project->is_bgra = false;
#endif

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	render_target_t *rt        = any_map_get(render_path_render_targets, "buf");
	gpu_texture_t   *tex       = rt->_image;
	gpu_texture_t   *mesh_icon = gpu_create_render_target(256, 256, GPU_TEXTURE_FORMAT_RGBA32);
	f32              r         = sys_w() / (float)sys_h();
	draw_begin(mesh_icon, false, 0);
	draw_scaled_image(tex, -(256 * r - 256) / 2.0, 0, 256 * r, 256);
	draw_end();

	buffer_t   *mesh_icon_pixels = gpu_get_texture_pixels(mesh_icon);
	u8_array_t *u8a              = mesh_icon_pixels;
	for (i32 i = 0; i < 256 * 256 * 4; ++i) {
		u8a->buffer[i] = math_floor(math_pow(u8a->buffer[i] / 255.0, 1.0 / 2.2) * 255);
	}
	iron_write_png(string("%s_icon.png", substring(g_project->_->filepath, 0, string_length(g_project->_->filepath) - 4)), mesh_icon_pixels, 256, 256, 0);
	gpu_delete_texture(mesh_icon);
#endif

	if (g_context->pack_assets_on_save) { // Pack textures
		export_arm_pack_assets(g_project, g_project->_->assets);
	}

	buffer_t *buffer = util_encode_project(g_project);
	iron_file_save_bytes(g_project->_->filepath, buffer, buffer->length + 1);

	// Save to recent
#ifdef IRON_IOS
	char *recent_path = substring(g_project->_->filepath, string_last_index_of(g_project->_->filepath, "/") + 1, string_length(g_project->_->filepath));
#else
	char *recent_path = g_project->_->filepath;
#endif

#ifdef IRON_WINDOWS
	recent_path = string_copy(string_replace_all(recent_path, "\\", "/"));
#endif
	string_array_t *recent = g_config->recent_projects;
	string_array_remove(recent, recent_path);
	array_insert(recent, 0, recent_path);
	config_save();

	tab_timeline_finish_save();

	console_info(tr("Project saved"));
}

packed_asset_t_array_t *export_arm_get_packed_assets(char *project_path, string_array_t *texture_files) {
	packed_asset_t_array_t *packed_assets = NULL;
	if (g_project->packed_assets != NULL) {
		for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
#ifdef IRON_IOS
			bool same_drive = false;
#else
			bool same_drive = char_at(project_path, 0)[0] == char_at(pa->name, 0)[0];
#endif
			// Convert path from absolute to relative
			pa->name = same_drive ? path_to_relative(project_path, pa->name) : pa->name;
			for (i32 i = 0; i < texture_files->length; ++i) {
				char *tf = texture_files->buffer[i];
				if (string_equals(pa->name, tf)) {
					if (packed_assets == NULL) {
						packed_assets = any_array_create_from_raw((void *[]){}, 0);
					}
					any_array_push(packed_assets, pa);
					break;
				}
			}
		}
	}
	return packed_assets;
}

void export_arm_run_material(char *path) {
	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	ui_node_canvas_t_array_t *mnodes  = any_array_create_from_raw((void *[]){}, 0);
	ui_node_canvas_t_array_t *mgroups = NULL;
	slot_material_t          *m       = g_context->material;
	ui_node_canvas_t         *c       = util_clone_canvas(m->canvas);
	asset_t_array_t          *assets  = any_array_create_from_raw((void *[]){}, 0);
	if (ui_nodes_has_group(c)) {
		mgroups = any_array_create_from_raw((void *[]){}, 0);
		ui_nodes_traverse_group(mgroups, c);
		for (i32 i = 0; i < mgroups->length; ++i) {
			ui_node_canvas_t *gc = mgroups->buffer[i];
			for (i32 i = 0; i < gc->nodes->length; ++i) {
				ui_node_t *n = gc->nodes->buffer[i];
				export_arm_export_node(n, assets);
			}
		}
	}
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		export_arm_export_node(n, assets);
	}
	any_array_push(mnodes, c);

	string_array_t         *texture_files = export_arm_assets_to_files(path, assets);
	packed_asset_t_array_t *packed_assets = NULL;
	if (!g_context->pack_assets_on_export) {
		packed_assets = export_arm_get_packed_assets(path, texture_files);
	}

#ifdef IRON_BGRA
	buffer_t *buf = lz4_encode(buffer_bgra64_swap(gpu_get_texture_pixels(m->image)));
#else
	buffer_t *buf = lz4_encode(gpu_get_texture_pixels(m->image));
#endif
	buffer_t_array_t *micons = any_array_create_from_raw(
	    (void *[]){
	        buf,
	    },
	    1);

	material_data2_t_array_t *mdata2 = any_array_create_from_raw((void *[]){}, 0);
	material_data2_t         *md2    = GC_ALLOC_INIT(material_data2_t, {
	                                                                       .paint_base   = m->paint_base,
	                                                                       .paint_opac   = m->paint_opac,
	                                                                       .paint_occ    = m->paint_occ,
	                                                                       .paint_rough  = m->paint_rough,
	                                                                       .paint_met    = m->paint_met,
	                                                                       .paint_nor    = m->paint_nor,
	                                                                       .paint_height = m->paint_height,
	                                                                       .paint_emis   = m->paint_emis,
	                                                                       .paint_subs   = m->paint_subs,
	                                                                       .opac_mode    = m->paint_opac_mode,
                                                            });
	any_array_push(mdata2, md2);

	project_t *raw = GC_ALLOC_INIT(project_t, {.version         = manifest_version_project,
	                                           .material_nodes  = mnodes,
	                                           .material_groups = mgroups,
	                                           .material_icons  = micons,
	                                           .material_datas  = mdata2,
	                                           .assets          = texture_files,
	                                           .packed_assets   = packed_assets});

	if (g_context->write_icon_on_export) { // Separate icon files
		buffer_t *buf = buffer_half_to_u8(gpu_get_texture_pixels(m->image));
#ifdef IRON_BGRA
		buf = buffer_bgra_swap(buf);
#endif
		iron_write_png(string("%s_icon.png", substring(path, 0, string_length(path) - 4)), buf, m->image->width, m->image->height, 0);
		iron_write_jpg(string("%s_icon.jpg", substring(path, 0, string_length(path) - 4)), buf, m->image->width, m->image->height, 0, 50);
	}

	if (g_context->pack_assets_on_export) { // Pack textures
		export_arm_pack_assets(raw, assets);
	}

	buffer_t *buffer = util_encode_project(raw);
	iron_file_save_bytes(path, buffer, buffer->length + 1);
}

void export_arm_run_brush(char *path) {
	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	ui_node_canvas_t_array_t *bnodes = any_array_create_from_raw((void *[]){}, 0);
	slot_brush_t             *b      = g_context->brush;
	ui_node_canvas_t         *c      = util_clone_canvas(b->canvas);
	asset_t_array_t          *assets = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		export_arm_export_node(n, assets);
	}
	any_array_push(bnodes, c);

	string_array_t         *texture_files = export_arm_assets_to_files(path, assets);
	packed_asset_t_array_t *packed_assets = NULL;
	if (!g_context->pack_assets_on_export) {
		packed_assets = export_arm_get_packed_assets(path, texture_files);
	}

#ifdef IRON_BGRA
	buffer_t *buf = lz4_encode(buffer_bgra_swap(gpu_get_texture_pixels(b->image)));
#else
	buffer_t *buf = lz4_encode(gpu_get_texture_pixels(b->image));
#endif
	buffer_t_array_t *bicons = any_array_create_from_raw(
	    (void *[]){
	        buf,
	    },
	    1);

	project_t *raw = GC_ALLOC_INIT(
	    project_t,
	    {.version = manifest_version_project, .brush_nodes = bnodes, .brush_icons = bicons, .assets = texture_files, .packed_assets = packed_assets});

	if (g_context->write_icon_on_export) { // Separate icon files
		buffer_t *buf = gpu_get_texture_pixels(b->image);
#ifdef IRON_BGRA
		buf = buffer_bgra_swap(buf);
#endif
		iron_write_png(string("%s_icon.png", substring(path, 0, string_length(path) - 4)), buf, b->image->width, b->image->height, 0);
		iron_write_jpg(string("%s_icon.jpg", substring(path, 0, string_length(path) - 4)), buf, b->image->width, b->image->height, 0, 50);
	}

	if (g_context->pack_assets_on_export) { // Pack textures
		export_arm_pack_assets(raw, assets);
	}

	buffer_t *buffer = util_encode_project(raw);
	iron_file_save_bytes(path, buffer, buffer->length + 1);
}

void export_arm_pack_assets(project_t *raw, asset_t_array_t *assets) {
	if (raw->packed_assets == NULL) {
		raw->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	gpu_texture_t_array_t *temp_images = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < assets->length; ++i) {
		if (!project_packed_asset_exists(raw->packed_assets, assets->buffer[i]->file)) {
			gpu_texture_t *image = project_get_image(assets->buffer[i]);
			gpu_texture_t *temp  = gpu_create_render_target(image->width, image->height, GPU_TEXTURE_FORMAT_RGBA32);
			draw_begin(temp, false, 0);
			draw_image(image, 0, 0);
			draw_end();
			any_array_push(temp_images, temp);
			packed_asset_t *pa = GC_ALLOC_INIT(packed_asset_t, {.name  = assets->buffer[i]->file,
			                                                    .bytes = ends_with(assets->buffer[i]->file, ".jpg")
			                                                                 ? iron_encode_jpg(gpu_get_texture_pixels(temp), temp->width, temp->height, 0, 80)
			                                                                 : iron_encode_png(gpu_get_texture_pixels(temp), temp->width, temp->height, 0)});
			any_array_push(raw->packed_assets, pa);
		}
	}

	for (i32 i = 0; i < temp_images->length; ++i) {
		gpu_texture_t *image = temp_images->buffer[i];
		gpu_delete_texture(image);
	}
}

void export_arm_run_swatches(char *path) {
	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	project_t *raw    = GC_ALLOC_INIT(project_t, {.version = manifest_version_project, .swatches = g_project->swatches});
	buffer_t  *buffer = util_encode_project(raw);
	iron_file_save_bytes(path, buffer, buffer->length + 1);
}
