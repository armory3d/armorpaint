
#include "../global.h"

static f32 export_texture_gamma = 1.0 / 2.2;

static void export_texture_write_texture(char *file, buffer_t *pixels, i32 type, i32 off) {
	i32 res_x       = config_get_texture_res_x();
	i32 res_y       = config_get_texture_res_y();
	i32 bits_handle = base_bits_handle->i;
	i32 bits        = bits_handle == TEXTURE_BITS_BITS8 ? 8 : bits_handle == TEXTURE_BITS_BITS16 ? 16 : 32;
	i32 format      = 0; // RGBA
	if (type == 1) {
		format = 2; // RGB1
	}
	if (type == 2 && off == 0) {
		format = 3; // RRR1
	}
	if (type == 2 && off == 1) {
		format = 4; // GGG1
	}
	if (type == 2 && off == 2) {
		format = 5; // BBB1
	}
	if (type == 2 && off == 3) {
		format = 6; // AAA1
	}

	if (g_context->layers_destination == EXPORT_DESTINATION_PACK_INTO_PROJECT) {
#ifdef IRON_BGRA
		if (format == 2) { // RGB1
			buffer_bgra_swap(pixels);
		}
#endif
		gpu_texture_t *image = gpu_create_texture_from_bytes(pixels, res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);
		any_map_set(data_cached_textures, file, image);
		string_array_t *ar   = string_split(file, PATH_SEP);
		char           *name = ar->buffer[ar->length - 1];
		for (i32 i = 0; i < (i32)ar->length - 1; ++i) {
			free(ar->buffer[i]);
		}
		array_free(ar);
		free(ar);
		asset_t *asset = ALLOC_INIT(asset_t, {.name = name, .file = file, .id = g_project->_->next_asset_id++});
		any_array_push(g_project->_->assets, asset);
		if (g_project->assets == NULL) {
			g_project->assets = any_array_create_from_raw((void *[]){}, 0);
		}
		any_array_push(g_project->assets, asset->file);
		asset->image            = image;
		asset_t_array_t *assets = any_array_create_from_raw(
		    (void *[]){
		        asset,
		    },
		    1);
		export_arm_pack_assets(g_project, assets);
		return;
	}

	if (bits == 8 && g_context->format_type == TEXTURE_LDR_FORMAT_PNG) {
		iron_write_png(file, pixels, res_x, res_y, format);
	}
	else if (bits == 8 && g_context->format_type == TEXTURE_LDR_FORMAT_JPG) {
		iron_write_jpg(file, pixels, res_x, res_y, format, math_floor(g_context->format_quality));
	}
	else { // Exr
		buffer_t *b = export_exr_run(res_x, res_y, pixels, bits, type, off);
		iron_file_save_bytes(file, b, b->length);
		array_free(b);
		free(b);
	}
}

static void export_texture_to_srgb(buffer_t *to, i32 to_channel) {
	for (i32 i = 0; i < math_floor((to->length) / 4.0); ++i) {
		buffer_set_u8(to, i * 4 + to_channel, math_floor(math_pow(buffer_get_u8(to, i * 4 + to_channel) / 255.0, export_texture_gamma) * 255));
	}
}

static i32 _export_texture_channel_bgra_swap(i32 c) {
	return c == 0 ? 2 : c == 2 ? 0 : c;
}

static void export_texture_copy_channel(buffer_t *from, i32 from_channel, buffer_t *to, i32 to_channel, bool linear) {
#ifdef IRON_BGRA
	from_channel = _export_texture_channel_bgra_swap(from_channel);
#endif
	for (i32 i = 0; i < math_floor((to->length) / 4.0); ++i) {
		buffer_set_u8(to, i * 4 + to_channel, buffer_get_u8(from, i * 4 + from_channel));
	}
	if (!linear) {
		export_texture_to_srgb(to, to_channel);
	}
}

static void export_texture_copy_channel_inv(buffer_t *from, i32 from_channel, buffer_t *to, i32 to_channel, bool linear) {
#ifdef IRON_BGRA
	from_channel = _export_texture_channel_bgra_swap(from_channel);
#endif
	for (i32 i = 0; i < math_floor((to->length) / 4.0); ++i) {
		buffer_set_u8(to, i * 4 + to_channel, 255 - buffer_get_u8(from, i * 4 + from_channel));
	}
	if (!linear) {
		export_texture_to_srgb(to, to_channel);
	}
}

static void export_texture_extract_channel(buffer_t *from, i32 from_channel, buffer_t *to, i32 to_channel, i32 step, i32 mask, bool linear) {
#ifdef IRON_BGRA
	from_channel = _export_texture_channel_bgra_swap(from_channel);
#endif
	for (i32 i = 0; i < math_floor((to->length) / 4.0); ++i) {
		buffer_set_u8(to, i * 4 + to_channel, buffer_get_u8(from, i * 4 + from_channel) % step == mask ? 255 : 0);
	}
	if (!linear) {
		export_texture_to_srgb(to, to_channel);
	}
}

static void export_texture_compute_diff_spec_channel(buffer_t *from_albedo, buffer_t *from_pack, i32 albedo_channel, buffer_t *to, i32 to_channel, bool linear,
                                                     bool is_specular) {
#ifdef IRON_BGRA
	albedo_channel    = _export_texture_channel_bgra_swap(albedo_channel);
	to_channel        = _export_texture_channel_bgra_swap(to_channel);
	i32 metal_channel = _export_texture_channel_bgra_swap(2);
#else
	i32 metal_channel = 2;
#endif
	for (i32 i = 0; i < math_floor((to->length) / 4.0); ++i) {
		f32 albedo = buffer_get_u8(from_albedo, i * 4 + albedo_channel) / 255.0;
		f32 metal  = buffer_get_u8(from_pack, i * 4 + metal_channel) / 255.0;
		f32 value;
		if (is_specular) {
			value = 0.04 * (1.0 - metal) + albedo * metal;
		}
		else {
			value = albedo * (1.0 - metal);
		}
		buffer_set_u8(to, i * 4 + to_channel, (u8)math_floor(value * 255.0 + 0.5));
	}
	if (!linear) {
		export_texture_to_srgb(to, to_channel);
	}
}

static void export_texture_set_channel(i32 value, buffer_t *to, i32 to_channel, bool linear) {
	for (i32 i = 0; i < math_floor((to->length) / 4.0); ++i) {
		buffer_set_u8(to, i * 4 + to_channel, value);
	}
	if (!linear) {
		export_texture_to_srgb(to, to_channel);
	}
}

static void export_texture_run_layers(char *path, slot_layer_t_array_t *layers, char *object_name, bool bake_material) {
	i32 texture_size_x = config_get_texture_res_x();
	i32 texture_size_y = config_get_texture_res_y();
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	char *f = sys_title();
#else
	char *f = ui_files_filename;
#endif
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	texture_ldr_format_t format_type = g_context->format_type;
	i32                  bits        = base_bits_handle->i == TEXTURE_BITS_BITS8 ? 8 : 16;
	char                *ext         = bits == 16 ? ".exr" : format_type == TEXTURE_LDR_FORMAT_PNG ? ".png" : ".jpg";
	if (ends_with(f, ext)) {
		f = string_copy(substring(f, 0, string_length(f) - 4));
	}

	bool is_udim = g_context->layers_export == EXPORT_MODE_PER_UDIM_TILE;
	if (is_udim) {
		ext = string("%s%s", object_name, ext);
	}

	layers_make_temp_img();
	layers_make_export_img();
	render_target_t *rt    = any_map_get(render_path_render_targets, "empty_white");
	gpu_texture_t   *empty = rt->_image;

	// Append object mask name
	bool export_selected = g_context->layers_export == EXPORT_MODE_SELECTED;
	if (export_selected && slot_layer_get_object_mask(layers->buffer[0]) > 0) {
		f = string("%s_%s", f, g_project->_->paint_objects->buffer[slot_layer_get_object_mask(layers->buffer[0]) - 1]->base->name);
	}
	if (!is_udim && !export_selected && !string_equals(object_name, "")) {
		f = string("%s_%s", f, object_name);
	}

	// Clear export layer
	_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.5, 0.5, 1.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expc, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(1.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();

	// Flatten layers
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *l1 = layers->buffer[i];
		if (!export_selected && !slot_layer_is_visible(l1)) {
			continue;
		}
		if (!slot_layer_is_layer(l1)) {
			continue;
		}
		if (slot_layer_get_filters(l1, false) != NULL) {
			continue;
		}

		if (!string_equals(object_name, "") && slot_layer_get_object_mask(l1) > 0) {
			if (is_udim && !ends_with(g_project->_->paint_objects->buffer[slot_layer_get_object_mask(l1) - 1]->base->name, object_name)) {
				continue;
			}
			bool per_object = g_context->layers_export == EXPORT_MODE_PER_OBJECT;
			if (per_object && !string_equals(g_project->_->paint_objects->buffer[slot_layer_get_object_mask(l1) - 1]->base->name, object_name)) {
				continue;
			}
		}

		gpu_texture_t        *mask    = empty;
		slot_layer_t_array_t *l1masks = slot_layer_get_masks(l1, true);
		if (l1masks != NULL && !bake_material) {
			layers_make_temp_mask_img();
			draw_begin(pipes_temp_mask_image, true, 0xffffffff);
			draw_end();
			slot_layer_t *l1 = ALLOC_INIT(slot_layer_t, {.texpaint = pipes_temp_mask_image});
			for (i32 i = 0; i < l1masks->length; ++i) {
				layers_merge_layer(l1, l1masks->buffer[i], false);
			}
			mask = pipes_temp_mask_image;
		}

		if (l1->paint_base) {
			draw_begin(layers_temp_image, false, 0); // Copy to temp
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expa, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, layers->length > 1 ? slot_layer_get_blending(l1) : 0);
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l1->paint_nor) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expb, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, l1->texpaint_nor);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, l1->texpaint_nor->width);
			gpu_set_int(pipes_blending, l1->paint_nor_blend ? 102 : 101);
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expc, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			if (l1->paint_occ && l1->paint_rough && l1->paint_met && l1->paint_height) {
				layers_commands_merge_pack(pipes_merge, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask,
				                           l1->paint_height_blend ? 103 : 101);
			}
			else {
				if (l1->paint_occ)
					layers_commands_merge_pack(pipes_merge_r, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
				if (l1->paint_rough)
					layers_commands_merge_pack(pipes_merge_g, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
				if (l1->paint_met)
					layers_commands_merge_pack(pipes_merge_b, layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, 101);
			}
		}
	}

	gpu_texture_t *texpaint      = layers_expa;
	gpu_texture_t *texpaint_nor  = layers_expb;
	gpu_texture_t *texpaint_pack = layers_expc;

	buffer_t        *pixpaint      = NULL;
	buffer_t        *pixpaint_nor  = NULL;
	buffer_t        *pixpaint_pack = NULL;
	export_preset_t *preset        = box_export_preset;
	buffer_t        *pix           = NULL;

	for (i32 i = 0; i < preset->textures->length; ++i) {
		export_preset_texture_t *t = preset->textures->buffer[i];
		for (i32 i = 0; i < t->channels->length; ++i) {
			char *c = t->channels->buffer[i];
			if ((string_equals(c, "base_r") || string_equals(c, "base_g") || string_equals(c, "base_b") || string_equals(c, "opac") ||
			     string_equals(c, "diff_r") || string_equals(c, "diff_g") || string_equals(c, "diff_b") || string_equals(c, "spec_r") ||
			     string_equals(c, "spec_g") || string_equals(c, "spec_b")) &&
			    pixpaint == NULL) {
				pixpaint = gpu_get_texture_pixels(texpaint);
			}
			if ((string_equals(c, "nor_r") || string_equals(c, "nor_g") || string_equals(c, "nor_g_directx") || string_equals(c, "nor_b") ||
			     string_equals(c, "emis") || string_equals(c, "subs")) &&
			    pixpaint_nor == NULL) {
				pixpaint_nor = gpu_get_texture_pixels(texpaint_nor);
			}
			if ((string_equals(c, "occ") || string_equals(c, "rough") || string_equals(c, "metal") || string_equals(c, "height") ||
			     string_equals(c, "smooth") || string_equals(c, "diff_r") || string_equals(c, "diff_g") || string_equals(c, "diff_b") ||
			     string_equals(c, "spec_r") || string_equals(c, "spec_g") || string_equals(c, "spec_b")) &&
			    pixpaint_pack == NULL) {
				pixpaint_pack = gpu_get_texture_pixels(texpaint_pack);
			}
		}
	}

	for (i32 i = 0; i < preset->textures->length; ++i) {
		export_preset_texture_t *t              = preset->textures->buffer[i];
		string_array_t          *c              = t->channels;
		char                    *tex_name       = !string_equals(t->name, "") ? string("_%s", t->name) : "";
		bool                     single_channel = c->buffer[0] == c->buffer[1] && c->buffer[1] == c->buffer[2] && string_equals(c->buffer[3], "1.0");
		char                    *out_path       = string("%s%s%s%s%s", path, PATH_SEP, f, tex_name, ext);
		if (string_equals(c->buffer[0], "base_r") && string_equals(c->buffer[1], "base_g") && string_equals(c->buffer[2], "base_b") &&
		    string_equals(c->buffer[3], "1.0") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint, 1, 0);
		}
		else if (string_equals(c->buffer[0], "nor_r") && string_equals(c->buffer[1], "nor_g") && string_equals(c->buffer[2], "nor_b") &&
		         string_equals(c->buffer[3], "1.0") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint_nor, 1, 0);
		}
		else if (string_equals(c->buffer[0], "occ") && string_equals(c->buffer[1], "rough") && string_equals(c->buffer[2], "metal") &&
		         string_equals(c->buffer[3], "1.0") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint_pack, 1, 0);
		}
		else if (single_channel && string_equals(c->buffer[0], "occ") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint_pack, 2, 0);
		}
		else if (single_channel && string_equals(c->buffer[0], "rough") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint_pack, 2, 1);
		}
		else if (single_channel && string_equals(c->buffer[0], "metal") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint_pack, 2, 2);
		}
		else if (single_channel && string_equals(c->buffer[0], "height") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint_pack, 2, 3);
		}
		else if (single_channel && string_equals(c->buffer[0], "opac") && string_equals(t->color_space, "linear")) {
			export_texture_write_texture(out_path, pixpaint, 2, 3);
		}
		else {
			if (pix == NULL) {
				pix = buffer_create(texture_size_x * texture_size_y * 4 * math_floor(bits / 8.0));
			}
			for (i32 i = 0; i < 4; ++i) {
				char *c = t->channels->buffer[i];
				if (string_equals(c, "base_r")) {
					export_texture_copy_channel(pixpaint, 0, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "base_g")) {
					export_texture_copy_channel(pixpaint, 1, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "base_b")) {
					export_texture_copy_channel(pixpaint, 2, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "height")) {
					export_texture_copy_channel(pixpaint_pack, 3, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "metal")) {
					export_texture_copy_channel(pixpaint_pack, 2, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "nor_r")) {
					export_texture_copy_channel(pixpaint_nor, 0, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "nor_g")) {
					export_texture_copy_channel(pixpaint_nor, 1, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "nor_g_directx")) {
					export_texture_copy_channel_inv(pixpaint_nor, 1, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "nor_b")) {
					export_texture_copy_channel(pixpaint_nor, 2, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "occ")) {
					export_texture_copy_channel(pixpaint_pack, 0, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "opac")) {
					export_texture_copy_channel(pixpaint, 3, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "rough")) {
					export_texture_copy_channel(pixpaint_pack, 1, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "smooth")) {
					export_texture_copy_channel_inv(pixpaint_pack, 1, pix, i, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "emis")) {
					export_texture_extract_channel(pixpaint_nor, 3, pix, i, 3, 1, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "subs")) {
					export_texture_extract_channel(pixpaint_nor, 3, pix, i, 3, 2, string_equals(t->color_space, "linear"));
				}
				else if (string_equals(c, "diff_r")) {
					export_texture_compute_diff_spec_channel(pixpaint, pixpaint_pack, 0, pix, i, string_equals(t->color_space, "linear"), false);
				}
				else if (string_equals(c, "diff_g")) {
					export_texture_compute_diff_spec_channel(pixpaint, pixpaint_pack, 1, pix, i, string_equals(t->color_space, "linear"), false);
				}
				else if (string_equals(c, "diff_b")) {
					export_texture_compute_diff_spec_channel(pixpaint, pixpaint_pack, 2, pix, i, string_equals(t->color_space, "linear"), false);
				}
				else if (string_equals(c, "spec_r")) {
					export_texture_compute_diff_spec_channel(pixpaint, pixpaint_pack, 0, pix, i, string_equals(t->color_space, "linear"), true);
				}
				else if (string_equals(c, "spec_g")) {
					export_texture_compute_diff_spec_channel(pixpaint, pixpaint_pack, 1, pix, i, string_equals(t->color_space, "linear"), true);
				}
				else if (string_equals(c, "spec_b")) {
					export_texture_compute_diff_spec_channel(pixpaint, pixpaint_pack, 2, pix, i, string_equals(t->color_space, "linear"), true);
				}
				else if (string_equals(c, "0.0")) {
					export_texture_set_channel(0, pix, i, true);
				}
				else if (string_equals(c, "1.0")) {
					export_texture_set_channel(255, pix, i, true);
				}
			}
			export_texture_write_texture(out_path, pix, 3, 0);
		}
	}

	// Release staging memory allocated in gpu_get_texture_pixels()
	// texpaint->pixels = NULL;
	// texpaint_nor->pixels = NULL;
	// texpaint_pack->pixels = NULL;
}

static void export_texture_run_bake_material(char *path) {
	if (render_path_paint_live_layer == NULL) {
		render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
	}

	tool_type_t _tool = g_context->tool;
	g_context->tool   = TOOL_TYPE_FILL;
	make_material_parse_paint_material(true);
	mesh_object_t *_paint_object = g_context->paint_object;
	mesh_object_t *planeo        = scene_get_child(".Plane")->ext;
	planeo->base->visible        = true;
	g_context->paint_object      = planeo;
	g_context->pdirty            = 1;

	u8_array_t *_visibles = u8_array_create_from_raw((u8[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		u8_array_push(_visibles, p->base->visible);
		p->base->visible = false;
	}

	render_path_paint_use_live_layer(true);
	render_path_paint_commands_paint(false);
	render_path_paint_use_live_layer(false);

	g_context->tool = _tool;
	make_material_parse_paint_material(true);
	g_context->pdirty       = 0;
	planeo->base->visible   = false;
	g_context->paint_object = _paint_object;

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		g_project->_->paint_objects->buffer[i]->base->visible = _visibles->buffer[i];
	}

	slot_layer_t_array_t *layers = any_array_create_from_raw(
	    (void *[]){
	        render_path_paint_live_layer,
	    },
	    1);
	export_texture_run_layers(path, layers, "", true);
}

void export_texture_run(char *path, bool bake_material) {

	// Exporting without opening the export box
	if (box_export_files == NULL) {
		box_export_fetch_presets();
		i32 i                 = string_array_index_of(box_export_files, "generic");
		box_export_hpreset->i = i > 0 ? i : 0;
	}
	if (box_export_preset == NULL) {
		box_export_parse_preset();
	}

	if (bake_material) {
		export_texture_run_bake_material(path);
	}
	else if (g_context->layers_export == EXPORT_MODE_PER_UDIM_TILE) {
		string_array_t *udim_tiles = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (slot_layer_get_object_mask(l) > 0) {
				char *name = g_project->_->paint_objects->buffer[slot_layer_get_object_mask(l) - 1]->base->name;
				if (string_equals(substring(name, string_length(name) - 5, 2), ".1")) { // tile.1001
					any_array_push(udim_tiles, substring(name, string_length(name) - 5, string_length(name)));
				}
			}
		}
		if (udim_tiles->length > 0) {
			for (i32 i = 0; i < udim_tiles->length; ++i) {
				char *udim_tile = udim_tiles->buffer[i];
				export_texture_run_layers(path, g_project->_->layers, udim_tile, false);
			}
		}
		else {
			export_texture_run_layers(path, g_project->_->layers, "", false);
		}
	}
	else if (g_context->layers_export == EXPORT_MODE_PER_OBJECT) {
		string_array_t *object_names = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (slot_layer_get_object_mask(l) > 0) {
				char *name = g_project->_->paint_objects->buffer[slot_layer_get_object_mask(l) - 1]->base->name;
				if (string_array_index_of(object_names, name) == -1) {
					any_array_push(object_names, name);
				}
			}
		}
		if (object_names->length > 0) {
			for (i32 i = 0; i < object_names->length; ++i) {
				char *name = object_names->buffer[i];
				export_texture_run_layers(path, g_project->_->layers, name, false);
			}
		}
		else {
			export_texture_run_layers(path, g_project->_->layers, "", false);
		}
	}
	else { // Visible or selected
		bool atlas_export = false;
		if (g_project->atlas_objects != NULL) {
			for (i32 i = 1; i < g_project->atlas_objects->length; ++i) {
				if (g_project->atlas_objects->buffer[i - 1] != g_project->atlas_objects->buffer[i]) {
					atlas_export = true;
					break;
				}
			}
		}
		if (atlas_export) {
			string_array_t *used_atlases = project_get_used_atlases();
			for (i32 atlas_index = 0; atlas_index < g_project->atlas_objects->length; ++atlas_index) {
				// Skip atlases that no object is assigned to
				bool atlas_used = false;
				for (i32 object_index = 0; object_index < g_project->atlas_objects->length; ++object_index) {
					if (g_project->atlas_objects->buffer[object_index] == atlas_index) {
						atlas_used = true;
						break;
					}
				}
				if (!atlas_used) {
					continue;
				}
				slot_layer_t_array_t *layers = any_array_create_from_raw((void *[]){}, 0);
				for (i32 i = 0; i < g_project->_->layers->length; ++i) {
					slot_layer_t *l    = g_project->_->layers->buffer[i];
					i32           mask = slot_layer_get_object_mask(l);
					bool          add  = false;
					if (mask == 0) { // Shared object
						add = true;
					}
					else if (mask <= g_project->_->paint_objects->length) { // Specific object
						add = g_project->atlas_objects->buffer[mask - 1] == atlas_index;
					}
					else if (used_atlases != NULL) { // Atlas
						i32 used_index = mask - g_project->_->paint_objects->length - 1;
						if (used_index < used_atlases->length) {
							add = string_array_index_of(g_project->atlas_names, used_atlases->buffer[used_index]) == atlas_index;
						}
					}
					if (add) {
						any_array_push(layers, l);
					}
				}
				if (layers->length > 0) {
					export_texture_run_layers(path, layers, g_project->atlas_names->buffer[atlas_index], false);
				}
			}
		}
		else {
			slot_layer_t_array_t *layers;
			if (g_context->layers_export == EXPORT_MODE_SELECTED) {
				if (slot_layer_is_group(g_context->layer)) {
					layers = slot_layer_get_children(g_context->layer);
				}
				else {
					layers = any_array_create_from_raw(
					    (void *[]){
					        g_context->layer,
					    },
					    1);
				}
			}
			else {
				layers = g_project->_->layers;
			}
			export_texture_run_layers(path, layers, "", false);
		}
	}

#ifdef IRON_IOS
	if (config_is_iphone()) {
		console_info(string("%s (\"Files/On My iPhone/%s\")", tr("Textures exported"), manifest_title));
	}
	else {
		console_info(string("%s (\"Files/On My iPad/%s\")", tr("Textures exported"), manifest_title));
	}
#elif defined(IRON_ANDROID)
	console_info(string("%s (\"Files/Internal storage/Pictures/%s\")", tr("Textures exported"), manifest_title));
#else
	console_info(tr("Textures exported"));
#endif
	ui_files_last_path = "";
}
