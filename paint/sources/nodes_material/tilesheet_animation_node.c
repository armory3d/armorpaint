
#include "../global.h"

static i32  _ts_anim_node_id    = -1;
static i32  _ts_anim_modify_idx = -1; // -1: add a new animation, >= 0: modify existing at this index
static bool _ts_anim_prefill    = false;

static void tilesheet_animation_node_edit_box() {
	bool  modify = _ts_anim_modify_idx >= 0;
	char *title  = modify ? tr("Modify Animation") : tr("Add Animation");
	if (ui_tab(ui_handle(__ID__), title, g_config->touch_ui, -1, false)) {
		ui_handle_t *hname  = ui_handle(__ID__);
		ui_handle_t *hstart = ui_handle(__ID__);
		ui_handle_t *hend   = ui_handle(__ID__);

		if (_ts_anim_prefill) {
			ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, _ts_anim_node_id);
			if (modify && node != NULL) {
				ui_node_button_t *data_but = node->buttons->buffer[3];
				ui_node_button_t *enum_but = node->buttons->buffer[4];
				string_array_t   *parts    = string_split(u8_array_to_string(enum_but->data), "\n");
				hname->text                = _ts_anim_modify_idx < (i32)parts->length ? parts->buffer[_ts_anim_modify_idx] : tr("Animation");
				hstart->f                  = data_but->default_value->buffer[_ts_anim_modify_idx * 2];
				hend->f                    = data_but->default_value->buffer[_ts_anim_modify_idx * 2 + 1];
			}
			else {
				hname->text = tr("Animation");
				hstart->f   = 0.0;
				hend->f     = 1.0;
			}
			_ts_anim_prefill = false;
		}

		char *name        = ui_text_input(hname, tr("Name"), UI_ALIGN_LEFT, true, false);
		i32   start_frame = (i32)ui_slider(hstart, tr("Start Frame"), 0.0, 4095.0, true, 1.0, true, UI_ALIGN_LEFT, true);
		i32   end_frame   = (i32)ui_slider(hend, tr("End Frame"), 0.0, 4095.0, true, 1.0, true, UI_ALIGN_LEFT, true);

		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_box_hide();
		}
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
			ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, _ts_anim_node_id);
			if (node != NULL) {
				ui_node_button_t *data_but = node->buttons->buffer[3];
				ui_node_button_t *enum_but = node->buttons->buffer[4];
				i32               count    = data_but->default_value->length / 2;

				if (modify && _ts_anim_modify_idx < count) {
					data_but->default_value->buffer[_ts_anim_modify_idx * 2]     = (f32)start_frame;
					data_but->default_value->buffer[_ts_anim_modify_idx * 2 + 1] = (f32)end_frame;

					string_array_t *parts     = string_split(u8_array_to_string(enum_but->data), "\n");
					char           *new_names = "";
					for (i32 i = 0; i < (i32)parts->length; i++) {
						char *part = i == _ts_anim_modify_idx ? name : parts->buffer[i];
						new_names  = i == 0 ? part : string_tmp("%s\n%s", new_names, part);
					}
					enum_but->data = u8_array_create_from_string(new_names);

					make_material_parse_paint_material(true);
				}
				else if (!modify) {
					f32_array_push(data_but->default_value, (f32)start_frame);
					f32_array_push(data_but->default_value, (f32)end_frame);

					char *new_names;
					if (count > 0) {
						char *prev = u8_array_to_string(enum_but->data);
						new_names  = string_tmp("%s\n%s", prev, name);
					}
					else {
						new_names = name;
					}
					enum_but->data = u8_array_create_from_string(new_names);

					enum_but->default_value->buffer[0] = (f32)count;

					make_material_parse_paint_material(true);
				}
			}
			ui_box_hide();
		}
	}
}

static void tilesheet_animation_node_show_box(i32 node_id, i32 modify_idx) {
	_ts_anim_node_id    = node_id;
	_ts_anim_modify_idx = modify_idx;
	_ts_anim_prefill    = true;
	char *title         = modify_idx >= 0 ? tr("Modify Animation") : tr("Add Animation");
	ui_box_show_custom(&tilesheet_animation_node_edit_box, 400, 240, NULL, true, title);
}

static void tilesheet_animation_node_remove(i32 node_id) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	if (node == NULL) {
		return;
	}
	ui_node_button_t *data_but = node->buttons->buffer[3];
	ui_node_button_t *enum_but = node->buttons->buffer[4];
	i32               count    = data_but->default_value->length / 2;
	i32               idx      = (i32)enum_but->default_value->buffer[0];
	if (idx >= count) {
		return;
	}

	f32_array_t *a   = data_but->default_value;
	i32          pos = idx * 2;
	for (i32 i = pos; i < (i32)a->length - 2; i++) {
		a->buffer[i] = a->buffer[i + 2];
	}
	a->length -= 2;

	char           *old_names = u8_array_to_string(enum_but->data);
	string_array_t *parts     = string_split(old_names, "\n");
	char           *new_names = "";
	for (i32 i = 0; i < (i32)parts->length; i++) {
		if (i == idx)
			continue;
		if (string_length(new_names) > 0) {
			new_names = string_tmp("%s\n%s", new_names, parts->buffer[i]);
		}
		else {
			new_names = parts->buffer[i];
		}
	}
	if (string_length(new_names) == 0) {
		enum_but->data = u8_array_create_from_string("none");
	}
	else {
		enum_but->data = u8_array_create_from_string(new_names);
	}

	i32 new_count = (i32)(a->length / 2);
	if (enum_but->default_value->buffer[0] >= new_count) {
		enum_but->default_value->buffer[0] = new_count > 0 ? (f32)(new_count - 1) : 0.0;
	}

	make_material_parse_paint_material(true);
}

static void tilesheet_animation_node_edit_menu() {
	ui_node_t *node  = ui_get_node(ui_nodes_get_canvas(true)->nodes, _ts_anim_node_id);
	i32        count = node != NULL ? (i32)(((ui_node_button_t *)node->buttons->buffer[3])->default_value->length / 2) : 0;

	if (ui_menu_button(tr("Add"), "", ICON_PLUS)) {
		tilesheet_animation_node_show_box(_ts_anim_node_id, -1);
	}
	g_ui->enabled = count > 0;
	if (ui_menu_button(tr("Modify"), "", ICON_EDIT)) {
		i32 idx = (i32)((ui_node_button_t *)node->buttons->buffer[4])->default_value->buffer[0];
		tilesheet_animation_node_show_box(_ts_anim_node_id, idx);
	}
	if (ui_menu_button(tr("Remove"), "", ICON_DELETE)) {
		tilesheet_animation_node_remove(_ts_anim_node_id);
	}
	g_ui->enabled = true;
}

static void tilesheet_animation_node_button(i32 node_id) {
	if (ui_button(tr("Edit"), UI_ALIGN_CENTER, "")) {
		_ts_anim_node_id = node_id;
		ui_menu_draw(&tilesheet_animation_node_edit_menu, -1, -1);
	}
}

char *tilesheet_animation_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");

	i32 tiles_x   = (i32)node->buttons->buffer[0]->default_value->buffer[0];
	i32 tiles_y   = (i32)node->buttons->buffer[1]->default_value->buffer[0];
	i32 framerate = (i32)node->buttons->buffer[2]->default_value->buffer[0];

	if (tiles_x < 1)
		tiles_x = 1;
	if (tiles_y < 1)
		tiles_y = 1;
	if (framerate < 1)
		framerate = 1;

	ui_node_button_t *data_but    = node->buttons->buffer[3];
	ui_node_button_t *enum_but    = node->buttons->buffer[4];
	i32               anim_count  = data_but->default_value->length / 2;
	i32               anim_idx    = (i32)enum_but->default_value->buffer[0];
	i32               start_frame = 0;
	i32               end_frame   = tiles_x * tiles_y - 1;

	if (anim_count > 0 && anim_idx < anim_count) {
		start_frame = (i32)data_but->default_value->buffer[anim_idx * 2];
		end_frame   = (i32)data_but->default_value->buffer[anim_idx * 2 + 1];
	}

	i32 anim_len = end_frame - start_frame + 1;
	if (anim_len < 1)
		anim_len = 1;

	node_shader_add_constant(parser_material_kong, "tilesheet_anim_time: float", "_time");

	char *base = parser_material_store_var_name(node);
	parser_material_write(parser_material_kong, string_tmp("var %s_frame: int = int(%d.0 + (float(int(constants.tilesheet_anim_time * %d.0)) %% %d.0));", base,
	                                                   start_frame, framerate, anim_len));
	parser_material_write(parser_material_kong, string_tmp("var %s_tx: float = float(int(float(%s_frame) %% %d.0));", base, base, tiles_x));
	parser_material_write(parser_material_kong, string_tmp("var %s_ty: float = float(int(float(%s_frame) / %d.0));", base, base, tiles_x));

	return string_tmp("float3((%s_tx + tex_coord.x) / %d.0, (%s_ty + tex_coord.y) / %d.0, 0.0)", base, tiles_x, base, tiles_y);
}

void tilesheet_animation_node_init() {
	ui_node_t *node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                .name    = _tr("Tilesheet Animation"),
	                                                .type    = "TILESHEET_ANIM",
	                                                .x       = 0,
	                                                .y       = 0,
	                                                .color   = 0xffb34f5a,
	                                                .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                .outputs = any_array_create_from_raw(
	                                                    (void *[]){
	                                                        ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                         .node_id       = 0,
	                                                                                         .name          = _tr("UV"),
	                                                                                         .type          = "VECTOR",
	                                                                                         .color         = 0xff6363c7,
	                                                                                         .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                         .min           = 0.0,
	                                                                                         .max           = 1.0,
	                                                                                         .precision     = 100,
	                                                                                         .display       = 0}),
	                                                    },
	                                                    1),
	                                                .buttons = any_array_create_from_raw(
	                                                    (void *[]){
	                                                        ALLOC_INIT(ui_node_button_t, {.name          = _tr("Tiles X"),
	                                                                                         .type          = "VALUE",
	                                                                                         .output        = -1,
	                                                                                         .default_value = f32_array_create_x(4),
	                                                                                         .data          = NULL,
	                                                                                         .min           = 1.0,
	                                                                                         .max           = 64.0,
	                                                                                         .precision     = 1,
	                                                                                         .height        = 0}),
	                                                        ALLOC_INIT(ui_node_button_t, {.name          = _tr("Tiles Y"),
	                                                                                         .type          = "VALUE",
	                                                                                         .output        = -1,
	                                                                                         .default_value = f32_array_create_x(4),
	                                                                                         .data          = NULL,
	                                                                                         .min           = 1.0,
	                                                                                         .max           = 64.0,
	                                                                                         .precision     = 1,
	                                                                                         .height        = 0}),
	                                                        ALLOC_INIT(ui_node_button_t, {.name          = _tr("Framerate"),
	                                                                                         .type          = "VALUE",
	                                                                                         .output        = -1,
	                                                                                         .default_value = f32_array_create_x(12),
	                                                                                         .data          = NULL,
	                                                                                         .min           = 1.0,
	                                                                                         .max           = 30.0,
	                                                                                         .precision     = 1,
	                                                                                         .height        = 0}),
	                                                        ALLOC_INIT(ui_node_button_t, {.name          = "tilesheet_animation_node_button",
	                                                                                         .type          = "CUSTOM",
	                                                                                         .output        = -1,
	                                                                                         .default_value = f32_array_create(0),
	                                                                                         .data          = NULL,
	                                                                                         .min           = 0.0,
	                                                                                         .max           = 0.0,
	                                                                                         .precision     = 1,
	                                                                                         .height        = 1}),
	                                                        ALLOC_INIT(ui_node_button_t, {.name          = _tr("Animation"),
	                                                                                         .type          = "ENUM",
	                                                                                         .output        = -1,
	                                                                                         .default_value = f32_array_create_x(0),
	                                                                                         .data          = u8_array_create_from_string("none"),
	                                                                                         .min           = 0.0,
	                                                                                         .max           = 1.0,
	                                                                                         .precision     = 100,
	                                                                                         .height        = 0}),
	                                                    },
	                                                    5),
	                                                .width = 0,
	                                                .flags = 0});

	any_array_push(nodes_material_input, node_def);
	any_map_set(parser_material_node_vectors, "TILESHEET_ANIM", tilesheet_animation_node_vector);
	any_map_set(ui_nodes_custom_buttons, "tilesheet_animation_node_button", tilesheet_animation_node_button);
}
