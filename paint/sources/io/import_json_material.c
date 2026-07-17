
#include "../global.h"

ui_node_t *import_json_material_get_output_node(ui_node_canvas_t *canvas) {
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			return n;
		}
	}
	return NULL;
}

void import_json_material_add_output_node(ui_node_canvas_t *canvas) {
	buffer_t         *b        = data_get_blob("default_material.arm");
	ui_node_canvas_t *def      = armpack_decode(b);
	ui_node_t        *template = import_json_material_get_output_node(def);
	if (template == NULL) {
		return;
	}
	ui_node_t *n = util_clone_canvas_node(template);

	i32 id = 0;
	f32 x  = 0.0;
	f32 y  = 0.0;
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *other = canvas->nodes->buffer[i];
		if (other->id >= id) {
			id = other->id + 1;
		}
		if (i == 0 || other->x > x) {
			x = other->x;
			y = other->y;
		}
	}
	n->id = id;
	n->x  = x + 250;
	n->y  = y;

	i32 socket_id = ui_get_socket_id(canvas->nodes);
	for (i32 i = 0; i < n->inputs->length; ++i) {
		n->inputs->buffer[i]->id      = socket_id++;
		n->inputs->buffer[i]->node_id = n->id;
	}
	for (i32 i = 0; i < n->outputs->length; ++i) {
		n->outputs->buffer[i]->id      = socket_id++;
		n->outputs->buffer[i]->node_id = n->id;
	}
	any_array_push(canvas->nodes, n);
}

void import_json_material_run(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return;
	}
	ui_node_canvas_t *canvas = json_parse(sys_buffer_to_string(b));
	gc_root(canvas);
	data_delete_blob(path);

	if (canvas == NULL || canvas->nodes == NULL || canvas->links == NULL || canvas->nodes->length == 0) {
		console_error(tr("Failed to import material"));
		gc_unroot(canvas);
		return;
	}

	// Convert button data from string to u8 array
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		for (i32 j = 0; j < n->buttons->length; ++j) {
			ui_node_button_t *but = n->buttons->buffer[j];
			if (but->data != NULL) {
				char *s   = string_replace_all((char *)but->data, "\\n", "\n");
				but->data = u8_array_create_from_raw((uint8_t *)s, strlen(s) + 1);
			}
		}
	}

	if (canvas->name == NULL || string_equals(canvas->name, "")) {
		char *file   = path_base_name(path);
		canvas->name = string_copy(substring(file, 0, string_last_index_of(file, ".")));
	}

	if (import_json_material_get_output_node(canvas) == NULL) {
		import_json_material_add_output_node(canvas);
	}

	material_data_t *m0 = data_get_material("Scene", "Material");
	g_context->material = slot_material_create(m0, canvas);
	any_array_push(g_project->_->materials, g_context->material);
	gc_unroot(canvas);

	base_update_workflow_nodes();
	history_new_material();

	slot_material_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(imported, g_context->material);
	sys_notify_on_next_frame(&import_arm_run_material_from_project_on_next_frame, imported);
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
}
