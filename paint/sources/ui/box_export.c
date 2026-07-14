
#include "../global.h"

bool                     _box_export_bake_material;
export_preset_texture_t *_box_export_t;
bool                     _box_export_merge_vertices;

void box_export_tab_export_textures_run(void *_) {
	if (g_context->export_padding) {
		i32 _dilate_radius      = g_config->dilate_radius;
		g_config->dilate_radius = 256;
		slot_layer_t *_layer    = g_context->layer;
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			g_context->layer = g_project->_->layers->buffer[i];
			render_path_paint_dilate(true, true);
		}
		g_context->layer        = _layer;
		g_config->dilate_radius = _dilate_radius;
	}

	export_texture_run(g_context->texture_export_path, _box_export_bake_material);
}

void box_export_tab_export_textures_path_picked(char *path) {
	g_context->texture_export_path = string_copy(path);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	console_toast(tr("Exporting textures"));
#endif
	sys_notify_on_next_frame(&box_export_tab_export_textures_run, NULL);
}

void box_export_tab_export_textures_on_next_frame(void *_) {
	layers_set_bits();
}

void box_export_tab_export_textures(char *title, bool bake_material) {
	bool tab_vertical = g_config->touch_ui;
	if (ui_tab(box_export_htab, title, tab_vertical, -1, false)) {

		ui_row2();

		string_array_t *base_res_combo = any_array_create_from_raw(
		    (void *[]){
		        "2048",
		        "4096",
		        "8192",
		        "16384",
		        tr("Custom"),
		    },
		    5);

		ui_combo(base_res_handle, base_res_combo, tr("Resolution"), true, UI_ALIGN_LEFT, true);
		if (base_res_handle->changed) {
			config_set_texture_res(base_res_handle->i);
			layers_on_resized();
		}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
		string_array_t *base_bits_combo = any_array_create_from_raw(
		    (void *[]){
		        "8bit",
		    },
		    1);
#else
		string_array_t *base_bits_combo = any_array_create_from_raw(
		    (void *[]){
		        "8bit",
		        "16bit",
		        "32bit",
		    },
		    3);
#endif

		ui_combo(base_bits_handle, base_bits_combo, tr("Color"), true, UI_ALIGN_LEFT, true);
		if (base_bits_handle->changed) {
			sys_notify_on_next_frame(&box_export_tab_export_textures_on_next_frame, NULL);
		}

		if (base_res_handle->i == TEXTURE_RES_CUSTOM) {
			static bool res_was_changed = false;
			ui_row2();
			ui_slider(base_res_x_handle, tr("Width"), 1, 16384, false, 1, true, UI_ALIGN_RIGHT, true);
			ui_slider(base_res_y_handle, tr("Height"), 1, 16384, false, 1, true, UI_ALIGN_RIGHT, true);
			if (res_was_changed && !g_ui->input_down) {
				res_was_changed = false;
				layers_on_resized();
			}
			if (base_res_x_handle->changed || base_res_y_handle->changed) {
				res_was_changed = true;
			}
		}

		ui_row2();
		if (base_bits_handle->i == TEXTURE_BITS_BITS8) {
			ui_handle_t *h               = ui_handle(__ID__);
			h->i                         = g_context->format_type;
			string_array_t *format_combo = any_array_create_from_raw(
			    (void *[]){
			        "png",
			        "jpg",
			    },
			    2);
			g_context->format_type = ui_combo(h, format_combo, tr("Format"), true, UI_ALIGN_LEFT, true);
		}
		else {
			ui_handle_t *h               = ui_handle(__ID__);
			h->i                         = g_context->format_type;
			string_array_t *format_combo = any_array_create_from_raw(
			    (void *[]){
			        "exr",
			    },
			    1);
			g_context->format_type = ui_combo(h, format_combo, tr("Format"), true, UI_ALIGN_LEFT, true);
		}

		g_ui->enabled = g_context->format_type == TEXTURE_LDR_FORMAT_JPG && base_bits_handle->i == TEXTURE_BITS_BITS8;

		ui_handle_t *h_quality    = ui_handle(__ID__);
		h_quality->f              = g_context->format_quality;
		g_context->format_quality = ui_slider(h_quality, tr("Quality"), 0.0, 100.0, true, 1, true, UI_ALIGN_RIGHT, true);

		g_ui->enabled = true;

		ui_row2();
		g_ui->enabled                       = !bake_material;
		ui_handle_t *layers_export_handle   = ui_handle(__ID__);
		layers_export_handle->i             = g_context->layers_export;
		string_array_t *layers_export_combo = any_array_create_from_raw(
		    (void *[]){
		        tr("Visible"),
		        tr("Selected"),
		        tr("Per Object"),
		        tr("Per Udim Tile"),
		    },
		    4);
		g_context->layers_export = ui_combo(layers_export_handle, layers_export_combo, tr("Layers"), true, UI_ALIGN_LEFT, true);
		g_ui->enabled            = true;

		ui_combo(box_export_hpreset, box_export_files, tr("Preset"), true, UI_ALIGN_LEFT, true);
		if (box_export_hpreset->changed) {
			gc_unroot(box_export_preset);
			box_export_preset = NULL;
		}

		ui_row2();

		ui_handle_t *layers_destination_handle = ui_handle(__ID__);
		layers_destination_handle->i           = g_context->layers_destination;

		string_array_t *layers_destination_combo = any_array_create_from_raw(
		    (void *[]){
		        tr("Disk"),
		        tr("Pack into Project"),
		    },
		    2);
		g_context->layers_destination = ui_combo(layers_destination_handle, layers_destination_combo, tr("Destination"), true, UI_ALIGN_LEFT, true);

		ui_handle_t *h_padding    = ui_handle(__ID__);
		h_padding->b              = g_context->export_padding;
		g_context->export_padding = ui_check(h_padding, tr("Padding"), "");

		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_box_hide();
		}
		if (ui_icon_button(tr("Export"), ICON_CHECK, UI_ALIGN_CENTER)) {
			ui_box_hide();
			if (g_context->layers_destination == EXPORT_DESTINATION_PACK_INTO_PROJECT) {
				_box_export_bake_material      = bake_material;
				g_context->texture_export_path = "/";
				sys_notify_on_next_frame(&box_export_tab_export_textures_run, NULL);
			}
			else {
				char *filters = base_bits_handle->i != TEXTURE_BITS_BITS8 ? "exr" : g_context->format_type == TEXTURE_LDR_FORMAT_PNG ? "png" : "jpg";
				_box_export_bake_material = bake_material;
				ui_files_show(filters, true, false, &box_export_tab_export_textures_path_picked);
			}
		}
		if (g_ui->is_hovered) {
			char *key = any_map_get(g_keymap, "file_export_textures");
			char *tip = string("%s (%s)", tr("Export texture files"), key);
			ui_tooltip(tip);
		}
	}
}

char *box_export_preset_to_json(export_preset_t *p) {
	json_encode_begin();
	json_encode_begin_array("textures");
	for (i32 i = 0; i < p->textures->length; ++i) {
		json_encode_begin_object();
		json_encode_string("name", p->textures->buffer[i]->name);
		json_encode_string_array("channels", p->textures->buffer[i]->channels);
		json_encode_string("color_space", p->textures->buffer[i]->color_space);
		json_encode_end_object();
	}
	json_encode_end_array();
	return json_encode_end();
}

void box_export_save_preset() {
	char *name = box_export_files->buffer[box_export_hpreset->i];
	if (string_equals(name, "generic")) {
		return; // generic is const
	}
	char *path = string("%s%sexport_presets%s%s.json", path_data(), PATH_SEP, PATH_SEP, name);
	iron_file_save_bytes(path, sys_string_to_buffer(box_export_preset_to_json(box_export_preset)), 0);
}

void box_export_tab_presets_menu_draw() {
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		array_remove(box_export_preset->textures, _box_export_t);
		box_export_save_preset();
	}
}

void box_export_fetch_presets() {
	gc_unroot(box_export_files);
	box_export_files = file_read_directory(string("%s%sexport_presets", path_data(), PATH_SEP));
	gc_root(box_export_files);
	for (i32 i = 0; i < box_export_files->length; ++i) {
		char *s                     = box_export_files->buffer[i];
		box_export_files->buffer[i] = substring(s, 0, string_length(s) - 5); // Strip .json
	}
}

void box_export_tab_presets_import(char *path) {
	path = string_copy(to_lower_case(path));
	if (ends_with(path, ".json")) {
		char *filename = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));
		char *dst_path = string("%s%sexport_presets%s%s", path_data(), PATH_SEP, PATH_SEP, filename);
		file_copy(path, dst_path); // Copy to presets folder
		box_export_fetch_presets();
		gc_unroot(box_export_preset);
		box_export_preset     = NULL;
		box_export_hpreset->i = string_array_index_of(box_export_files, substring(filename, 0, string_length(filename) - 5)); // Strip .json
		console_info(string("%s %s", tr("Preset imported:"), filename));
	}
	else {
		console_error(strings_unknown_asset_format());
	}
}

void box_export_new_preset(char *name) {
	char *template = "{\
\"textures\": [\
	{ \"name\": \"base\", \"channels\": [\"base_r\", \"base_g\", \"base_b\", \"1.0\"], \"color_space\": \"linear\" }\
]\
}\
";
	if (!ends_with(name, ".json")) {
		name = string("%s.json", name);
	}
	char *path = string("%s%sexport_presets%s%s", path_data(), PATH_SEP, PATH_SEP, name);
	iron_file_save_bytes(path, sys_string_to_buffer(template), 0);
}

void box_export_tab_presets_new_box() {
	bool tab_vertical = g_config->touch_ui;
	if (ui_tab(ui_handle(__ID__), tr("New Preset"), tab_vertical, -1, false)) {
		ui_row2();
		ui_handle_t *h_preset = ui_handle(__ID__);
		if (h_preset->init) {
			h_preset->text = "new_preset";
		}
		char *preset_name = ui_text_input(h_preset, tr("Name"), UI_ALIGN_LEFT, true, false);
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
			box_export_new_preset(preset_name);
			box_export_fetch_presets();
			gc_unroot(box_export_preset);
			box_export_preset     = NULL;
			box_export_hpreset->i = string_array_index_of(box_export_files, preset_name);
			ui_box_hide();
			box_export_htab->i = 1; // Presets
			box_export_show_textures();
		}
	}
}

void box_export_parse_preset() {
	char     *file = string("export_presets/%s.json", box_export_files->buffer[box_export_hpreset->i]);
	buffer_t *blob = data_get_blob(file);
	gc_unroot(box_export_preset);
	box_export_preset = json_parse(sys_buffer_to_string(blob));
	gc_root(box_export_preset);
	data_delete_blob(file);
}

void box_export_tab_presets() {
	bool tab_vertical = g_config->touch_ui;
	if (ui_tab(box_export_htab, tr("Presets"), tab_vertical, -1, false)) {

		f32_array_t *row = f32_array_create_from_raw(
		    (f32[]){
		        3 / 5.0,
		        1 / 5.0,
		        1 / 5.0,
		    },
		    3);
		ui_row(row);

		ui_combo(box_export_hpreset, box_export_files, tr("Preset"), false, UI_ALIGN_LEFT, true);
		if (box_export_hpreset->changed) {
			gc_unroot(box_export_preset);
			box_export_preset = NULL;
		}

		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			ui_box_show_custom(&box_export_tab_presets_new_box, 400, 200, NULL, true, "");
		}

		if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
			ui_files_show("json", false, false, &box_export_tab_presets_import);
		}

		if (box_export_preset == NULL) {
			box_export_parse_preset();
			box_export_hpreset->children = NULL;
		}

		// Texture list
		ui_separator(10, false);
		ui_row6();
		ui_text(tr("Texture"), UI_ALIGN_LEFT, 0x00000000);
		ui_text(tr("R"), UI_ALIGN_LEFT, 0x00000000);
		ui_text(tr("G"), UI_ALIGN_LEFT, 0x00000000);
		ui_text(tr("B"), UI_ALIGN_LEFT, 0x00000000);
		ui_text(tr("A"), UI_ALIGN_LEFT, 0x00000000);
		ui_text(tr("Color Space"), UI_ALIGN_LEFT, 0x00000000);
		g_ui->changed = false;
		for (i32 i = 0; i < box_export_preset->textures->length; ++i) {
			export_preset_texture_t *t = box_export_preset->textures->buffer[i];
			ui_row6();
			ui_handle_t *htex = ui_nest(box_export_hpreset, i);
			htex->text        = string_copy(t->name);
			t->name           = string_copy(ui_text_input(htex, "", UI_ALIGN_LEFT, true, false));

			if (g_ui->is_hovered && g_ui->input_released_r) {
				gc_unroot(_box_export_t);
				_box_export_t = t;
				gc_root(_box_export_t);
				ui_menu_draw(&box_export_tab_presets_menu_draw, -1, -1);
			}

			ui_handle_t *hr = ui_nest(htex, 0);
			hr->i           = string_array_index_of(box_export_channels, t->channels->buffer[0]);
			ui_handle_t *hg = ui_nest(htex, 1);
			hg->i           = string_array_index_of(box_export_channels, t->channels->buffer[1]);
			ui_handle_t *hb = ui_nest(htex, 2);
			hb->i           = string_array_index_of(box_export_channels, t->channels->buffer[2]);
			ui_handle_t *ha = ui_nest(htex, 3);
			ha->i           = string_array_index_of(box_export_channels, t->channels->buffer[3]);

			ui_combo(hr, box_export_channels, tr("R"), false, UI_ALIGN_LEFT, true);
			if (hr->changed) {
				t->channels->buffer[0] = box_export_channels->buffer[hr->i];
			}
			ui_combo(hg, box_export_channels, tr("G"), false, UI_ALIGN_LEFT, true);
			if (hg->changed) {
				t->channels->buffer[1] = box_export_channels->buffer[hg->i];
			}
			ui_combo(hb, box_export_channels, tr("B"), false, UI_ALIGN_LEFT, true);
			if (hb->changed) {
				t->channels->buffer[2] = box_export_channels->buffer[hb->i];
			}
			ui_combo(ha, box_export_channels, tr("A"), false, UI_ALIGN_LEFT, true);
			if (ha->changed) {
				t->channels->buffer[3] = box_export_channels->buffer[ha->i];
			}

			ui_handle_t *hspace = ui_nest(htex, 4);
			hspace->i           = string_array_index_of(box_export_color_spaces, t->color_space);
			ui_combo(hspace, box_export_color_spaces, tr("Color Space"), false, UI_ALIGN_LEFT, true);
			if (hspace->changed) {
				t->color_space = string_copy(box_export_color_spaces->buffer[hspace->i]);
			}
		}

		if (g_ui->changed) {
			box_export_save_preset();
		}

		row = f32_array_create_from_raw(
		    (f32[]){
		        1 / 6.0,
		    },
		    1);
		ui_row(row);
		if (ui_icon_button(tr("Add"), ICON_PLUS, UI_ALIGN_CENTER)) {
			export_preset_texture_t *tex = GC_ALLOC_INIT(export_preset_texture_t, {.name     = "base",
			                                                                       .channels = any_array_create_from_raw(
			                                                                           (void *[]){
			                                                                               "base_r",
			                                                                               "base_g",
			                                                                               "base_b",
			                                                                               "1.0",
			                                                                           },
			                                                                           4),
			                                                                       .color_space = "linear"});
			any_array_push(box_export_preset->textures, tex);
			box_export_hpreset->children = NULL;
			box_export_save_preset();
		}
	}
}

void box_export_tab_atlases() {
	bool tab_vertical = g_config->touch_ui;
	if (ui_tab(box_export_htab, tr("Atlases"), tab_vertical, -1, false)) {
		if (g_project->atlas_objects == NULL || g_project->atlas_objects->length != g_project->_->paint_objects->length) {
			g_project->atlas_objects = i32_array_create_from_raw((i32[]){}, 0);
			g_project->atlas_names   = any_array_create_from_raw((void *[]){}, 0);
			for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
				i32_array_push(g_project->atlas_objects, 0);
				i32 i1 = i + 1;
				any_array_push(g_project->atlas_names, string("%s %s", tr("Atlas"), i32_to_string(i1)));
			}
		}
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			ui_row2();
			ui_text(g_project->_->paint_objects->buffer[i]->base->name, UI_ALIGN_LEFT, 0x00000000);
			ui_handle_t *hatlas                 = ui_nest(ui_handle(__ID__), i);
			hatlas->i                           = g_project->atlas_objects->buffer[i];
			g_project->atlas_objects->buffer[i] = ui_combo(hatlas, g_project->atlas_names, tr("Atlas"), false, UI_ALIGN_LEFT, true);
		}
	}
}

void box_export_tab_export_mesh_path_picked(char *path) {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	char *f = sys_title();
#else
	char *f = ui_files_filename;
#endif
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	console_toast(tr("Exporting mesh"));
#endif

	mesh_object_t_array_t *paint_objects;
	if (box_export_mesh_handle->i == 0) {
		paint_objects = NULL;
	}
	else {
		mesh_object_t *po = g_project->_->paint_objects->buffer[box_export_mesh_handle->i - 1];
		paint_objects     = any_array_create_from_raw(
            (void *[]){
                po,
            },
            1);
	}
	export_mesh_run(string("%s%s%s", path, PATH_SEP, f), paint_objects, _box_export_merge_vertices);
}

void box_export_tab_export_mesh(ui_handle_t *htab) {
	bool tab_vertical = g_config->touch_ui;
	if (ui_tab(htab, tr("Export Mesh"), tab_vertical, -1, false)) {

		ui_row2();

		ui_handle_t *h_export_mesh_format        = ui_handle(__ID__);
		h_export_mesh_format->i                  = g_context->export_mesh_format;
		string_array_t *export_mesh_format_combo = any_array_create_from_raw(
		    (void *[]){
		        "obj",
		        "arm",
		    },
		    2);

#ifdef WITH_PLUGINS
		string_array_push(export_mesh_format_combo, "glb");
#endif

		g_context->export_mesh_format = ui_combo(h_export_mesh_format, export_mesh_format_combo, tr("Format"), true, UI_ALIGN_LEFT, true);

		string_array_t *ar = any_array_create_from_raw(
		    (void *[]){
		        tr("All"),
		    },
		    1);
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			any_array_push(ar, p->base->name);
		}
		ui_combo(box_export_mesh_handle, ar, tr("Meshes"), true, UI_ALIGN_LEFT, true);

		ui_handle_t *hmerge = ui_handle(__ID__);
		if (hmerge->init) {
			hmerge->b = true;
		}
		bool merge_vertices = ui_check(hmerge, tr("Merge Shared Vertices"), "");

		i32                    tris = 0;
		i32                    pos  = box_export_mesh_handle->i;
		mesh_object_t_array_t *paint_objects;
		if (pos == 0) {
			paint_objects = g_project->_->paint_objects;
		}
		else {
			mesh_object_t *po = g_project->_->paint_objects->buffer[pos - 1];
			paint_objects     = any_array_create_from_raw(
                (void *[]){
                    po,
                },
                1);
		}
		for (i32 i = 0; i < paint_objects->length; ++i) {
			mesh_object_t *po = paint_objects->buffer[i];
			tris += math_floor(po->data->index_array->length / 3.0);
		}
		ui_text(string("%s %s", i32_to_string(tris), tr("triangles")), UI_ALIGN_LEFT, 0x00000000);

		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_box_hide();
		}
		if (ui_icon_button(tr("Export"), ICON_CHECK, UI_ALIGN_CENTER)) {
			ui_box_hide();
			_box_export_merge_vertices = merge_vertices;
			ui_files_show(export_mesh_format_combo->buffer[g_context->export_mesh_format], true, false, &box_export_tab_export_mesh_path_picked);
		}
	}
}

void box_export_show_textures_box() {
	if (box_export_files == NULL) {
		box_export_fetch_presets();
		box_export_hpreset->i = string_array_index_of(box_export_files, "generic");
	}
	if (box_export_preset == NULL) {
		box_export_parse_preset();
		box_export_hpreset->children = NULL;
	}

	box_export_tab_export_textures(tr("Export Textures"), false);
	box_export_tab_presets();

	box_export_tab_atlases();
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	box_export_tab_export_mesh(box_export_htab);
#endif
}

void box_export_show_textures() {
	ui_box_show_custom(&box_export_show_textures_box, 600, 400, NULL, true, tr("Export"));
}

void box_export_show_bake_material_box() {
	if (box_export_files == NULL) {
		box_export_fetch_presets();
		box_export_hpreset->i = string_array_index_of(box_export_files, "generic");
	}
	if (box_export_preset == NULL) {
		box_export_parse_preset();
		box_export_hpreset->children = NULL;
	}

	box_export_tab_export_textures(tr("Bake to Textures"), true);
	box_export_tab_presets();
}

void box_export_show_bake_material() {
	ui_box_show_custom(&box_export_show_bake_material_box, 600, 400, NULL, true, tr("Export"));
}

void box_export_show_mesh_box() {
	ui_handle_t *htab = ui_handle(__ID__);
	box_export_tab_export_mesh(htab);
}

void box_export_show_mesh() {
	box_export_mesh_handle->i = g_context->export_mesh_index;
	ui_box_show_custom(&box_export_show_mesh_box, 420, 260, NULL, true, tr("Export"));
}

void box_export_show_material_export_on_next_frame(char *path) {
	export_arm_run_material(path);
}

void box_export_show_material_export(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	sys_notify_on_next_frame(&box_export_show_material_export_on_next_frame, string("%s%s%s", path, PATH_SEP, f));
}

void box_export_show_material_box() {
	ui_handle_t *htab         = ui_handle(__ID__);
	bool         tab_vertical = g_config->touch_ui;
	if (ui_tab(htab, tr("Export Material"), tab_vertical, -1, false)) {
		ui_handle_t *h1                  = ui_handle(__ID__);
		ui_handle_t *h2                  = ui_handle(__ID__);
		h1->b                            = g_context->pack_assets_on_export;
		h2->b                            = g_context->write_icon_on_export;
		g_context->pack_assets_on_export = ui_check(h1, tr("Pack Assets"), "");
		g_context->write_icon_on_export  = ui_check(h2, tr("Export Icon"), "");
		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_box_hide();
		}
		if (ui_icon_button(tr("Export"), ICON_CHECK, UI_ALIGN_CENTER)) {
			ui_box_hide();
			ui_files_show("arm", true, false, &box_export_show_material_export);
		}
	}
}

void box_export_show_material() {
	ui_box_show_custom(&box_export_show_material_box, 400, 200, NULL, true, "");
}

void box_export_show_brush_export_on_next_frame(char *path) {
	export_arm_run_brush(path);
}

void box_export_show_brush_export(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, ""))
		f = string_copy(tr("untitled"));
	sys_notify_on_next_frame(&box_export_show_brush_export_on_next_frame, string("%s%s%s", path, PATH_SEP, f));
}

void box_export_show_brush_box() {
	ui_handle_t *htab         = ui_handle(__ID__);
	bool         tab_vertical = g_config->touch_ui;
	if (ui_tab(htab, tr("Export Brush"), tab_vertical, -1, false)) {
		ui_handle_t *h1                  = ui_handle(__ID__);
		ui_handle_t *h2                  = ui_handle(__ID__);
		h1->b                            = g_context->pack_assets_on_export;
		h2->b                            = g_context->write_icon_on_export;
		g_context->pack_assets_on_export = ui_check(h1, tr("Pack Assets"), "");
		g_context->write_icon_on_export  = ui_check(h2, tr("Export Icon"), "");
		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_box_hide();
		}
		if (ui_icon_button(tr("Export"), ICON_CHECK, UI_ALIGN_CENTER)) {
			ui_box_hide();
			ui_files_show("arm", true, false, &box_export_show_brush_export);
		}
	}
}

void box_export_show_brush() {
	ui_box_show_custom(&box_export_show_brush_box, 400, 200, NULL, true, "");
}

void box_export_show_player_box_path_picked(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	sys_notify_on_next_frame(&export_player_run, string("%s%s%s", path, PATH_SEP, f));
}

void box_export_show_player_box() {
	ui_handle_t *htab         = ui_handle(__ID__);
	bool         tab_vertical = g_config->touch_ui;
	if (ui_tab(htab, tr("Export Player"), tab_vertical, -1, false)) {

		string_array_t *export_player_target_combo = any_array_create_from_raw((void *[]){"Web", "Windows", "Linux", "MacOS"}, 4);
		ui_combo(box_export_h_export_player_target, export_player_target_combo, tr("Target"), true, UI_ALIGN_LEFT, true);

		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_box_hide();
		}
		if (ui_icon_button(tr("Export"), ICON_CHECK, UI_ALIGN_CENTER)) {
			ui_box_hide();
			ui_files_show("", true, false, &box_export_show_player_box_path_picked);
		}
	}
}

void box_export_show_player() {
	ui_box_show_custom(&box_export_show_player_box, 420, 260, NULL, true, tr("Export"));
}
