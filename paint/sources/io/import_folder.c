
#include "../global.h"

void import_folder_place_image_node(ui_nodes_t *nodes, ui_node_canvas_t *canvas, char *asset, i32 ny, i32 to_id, i32 to_socket) {
	ui_node_t *n                         = nodes_material_create_node("TEX_IMAGE", NULL);
	n->buttons->buffer[0]->default_value = f32_array_create_x(base_get_asset_index(asset));
	n->x                                 = 72;
	n->y                                 = ny;
	ui_node_link_t *l =
	    ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(canvas->links), .from_id = n->id, .from_socket = 0, .to_id = to_id, .to_socket = to_socket});
	any_array_push(canvas->links, l);
}

void import_folder_run(char *path) {
	string_array_t *files     = file_read_directory(path);
	char           *mapbase   = "";
	char           *mapopac   = "";
	char           *mapnor    = "";
	char           *mapocc    = "";
	char           *maprough  = "";
	char           *mapmet    = "";
	char           *mapheight = "";

	bool found_texture = false;
	// Import maps
	for (i32 i = 0; i < files->length; ++i) {
		char *f = files->buffer[i];
		if (!path_is_texture(f)) {
			continue;
		}

		char *base  = to_lower_case(substring(f, 0, string_last_index_of(f, ".")));
		bool  valid = false;
		if (string_equals(mapbase, "") && path_is_base_color_tex(base)) {
			mapbase = string_copy(f);
			valid   = true;
		}
		if (string_equals(mapopac, "") && path_is_opacity_tex(base)) {
			mapopac = string_copy(f);
			valid   = true;
		}
		if (string_equals(mapnor, "") && path_is_normal_map_tex(base)) {
			mapnor = string_copy(f);
			valid  = true;
		}
		if (string_equals(mapocc, "") && path_is_occlusion_tex(base)) {
			mapocc = string_copy(f);
			valid  = true;
		}
		if (string_equals(maprough, "") && path_is_roughness_tex(base)) {
			maprough = string_copy(f);
			valid    = true;
		}
		if (string_equals(mapmet, "") && path_is_metallic_tex(base)) {
			mapmet = string_copy(f);
			valid  = true;
		}
		if (string_equals(mapheight, "") && path_is_displacement_tex(base)) {
			mapheight = string_copy(f);
			valid     = true;
		}

		if (valid) {
			import_texture_run(string("%s%s%s", path, PATH_SEP, f), false);
			found_texture = true;
		}
	}

	if (!found_texture) {
		console_info(tr("Folder does not contain textures"));
		return;
	}

	// Create material
	g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	any_array_push(g_project->_->materials, g_context->material);
	ui_nodes_t       *nodes  = g_context->material->nodes;
	ui_node_canvas_t *canvas = g_context->material->canvas;
	string_array_t   *dirs   = string_split(path, PATH_SEP);
	canvas->name             = string_copy(dirs->buffer[dirs->length - 1]);
	ui_node_t *nout          = NULL;
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			nout = n;
			break;
		}
	}
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->name, "Color")) {
			ui_remove_node(n, canvas);
			break;
		}
	}

	// Place nodes
	i32 pos     = 0;
	i32 start_y = 100;
	i32 node_h  = 180;
	if (!string_equals(mapbase, "")) {
		import_folder_place_image_node(nodes, canvas, mapbase, start_y + node_h * pos, nout->id, 0);
		pos++;
	}
	if (!string_equals(mapopac, "")) {
		import_folder_place_image_node(nodes, canvas, mapopac, start_y + node_h * pos, nout->id, 1);
		pos++;
	}
	if (!string_equals(mapocc, "")) {
		import_folder_place_image_node(nodes, canvas, mapocc, start_y + node_h * pos, nout->id, 2);
		pos++;
	}
	if (!string_equals(maprough, "")) {
		import_folder_place_image_node(nodes, canvas, maprough, start_y + node_h * pos, nout->id, 3);
		pos++;
	}
	if (!string_equals(mapmet, "")) {
		import_folder_place_image_node(nodes, canvas, mapmet, start_y + node_h * pos, nout->id, 4);
		pos++;
	}
	if (!string_equals(mapnor, "")) {
		import_folder_place_image_node(nodes, canvas, mapnor, start_y + node_h * pos, nout->id, 5);
		pos++;
	}
	if (!string_equals(mapheight, "")) {
		import_folder_place_image_node(nodes, canvas, mapheight, start_y + node_h * pos, nout->id, 7);
		pos++;
	}

	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	ui_base_hwnds->buffer[1]->redraws = 2;
	history_new_material();
}
