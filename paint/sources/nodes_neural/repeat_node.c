
#include "../global.h"

static i32 repeat_node_count = 0;

static void repeat_node_on_done(ui_node_t *node) {
	neural_node_current = node;

	iron_delay_idle_sleep();

	if (iron_exec_async_done == 1) {
		neural_node_load_result(node);
		sys_remove_update(repeat_node_on_done);

		if (repeat_node_count > 0) {
			repeat_node_count--;
			text_to_image_node_run(node, repeat_node_on_done);
		}
	}
}

void repeat_node_button(i32 node_id) {
	ui_node_t   *node      = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	char        *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t *h         = ui_handle(node_name);
	i32          count     = (i32)ui_slider(ui_nest(h, 0), tr("Count"), 1.0, 100.0, true, 1.0, true, UI_ALIGN_LEFT, true);

	if (iron_exec_async_done == 0) {
		g_ui->enabled = false;
		ui_button(tr("Running..."), UI_ALIGN_CENTER, "");
		g_ui->enabled = true;
	}
	else if (ui_button(tr("Run"), UI_ALIGN_CENTER, "")) {
		ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
		for (i32 i = 0; i < canvas->links->length; ++i) {
			ui_node_link_t *l = canvas->links->buffer[i];
			if (l->from_id == node->id && l->from_socket == 0) {
				ui_node_t *target = ui_get_node(canvas->nodes, l->to_id);
				if (target != NULL && string_equals(target->type, "NEURAL_TEXT_TO_IMAGE")) {
					repeat_node_count = count - 1;
					text_to_image_node_run(target, repeat_node_on_done);
					break;
				}
			}
		}
	}
}

void repeat_node_init() {

	ui_node_t *repeat_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                       .name    = _tr("Repeat"),
	                                                       .type    = "NEURAL_REPEAT",
	                                                       .x       = 0,
	                                                       .y       = 0,
	                                                       .color   = 0xff4982a0,
	                                                       .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                       .outputs = any_array_create_from_raw(
	                                                           (void *[]){
	                                                               ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                .node_id       = 0,
	                                                                                                .name          = _tr("Out"),
	                                                                                                .type          = "BOOL",
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
	                                                               ALLOC_INIT(ui_node_button_t, {.name          = "repeat_node_button",
	                                                                                                .type          = "CUSTOM",
	                                                                                                .output        = -1,
	                                                                                                .default_value = f32_array_create_x(0),
	                                                                                                .data          = NULL,
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .height        = 2}),
	                                                           },
	                                                           1),
	                                                       .width = 0,
	                                                       .flags = 0});

	any_array_push(nodes_material_neural, repeat_node_def);
	any_map_set(ui_nodes_custom_buttons, "repeat_node_button", repeat_node_button);
}
