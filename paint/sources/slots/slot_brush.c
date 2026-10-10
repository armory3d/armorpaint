
#include "../global.h"

buffer_t *slot_brush_default_canvas = NULL;

static bool slot_brush_is_unique_name(ui_node_canvas_t *self, char *name) {
	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		ui_node_canvas_t *c = g_project->_->brushes->buffer[i]->canvas;
		if (c != self && string_equals(c->name, name)) {
			return false;
		}
	}
	return true;
}

char *slot_brush_unique_name(ui_node_canvas_t *self, char *name) {
	if (slot_brush_is_unique_name(self, name)) {
		return name;
	}
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!slot_brush_is_unique_name(self, res)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

slot_brush_t *slot_brush_create(ui_node_canvas_t *c) {
	slot_brush_t *raw  = ALLOC_INIT(slot_brush_t, {0});
	raw->nodes         = ui_nodes_create();
	raw->preview_ready = false;
	raw->id            = 0;

	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		slot_brush_t *brush = g_project->_->brushes->buffer[i];
		if (brush->id >= raw->id) {
			raw->id = brush->id + 1;
		}
	}

	if (c == NULL) {
		if (slot_brush_default_canvas == NULL) { // Synchronous
			buffer_t *b               = data_get_blob("default_brush.arm");
			slot_brush_default_canvas = b;
		}
		ui_node_canvas_t *decoded = armpack_decode(slot_brush_default_canvas);
		raw->canvas               = util_clone_canvas(decoded);
		free(decoded);

		i32 id            = (raw->id + 1);
		raw->canvas->name = string("Brush %d", id);
	}
	else {
		raw->canvas = util_clone_canvas(c);
	}
	raw->canvas->name = string_copy(slot_brush_unique_name(raw->canvas, raw->canvas->name));

	return raw;
}
