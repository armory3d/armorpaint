
#include "global.h"

buffer_t *slot_material_default_canvas = NULL;

slot_material_t *slot_material_create(material_data_t *m, ui_node_canvas_t *c) {
	slot_material_t *raw = ALLOC_INIT(slot_material_t, {0});
	raw->nodes           = ui_nodes_create();
	raw->preview_ready   = false;
	raw->id              = 0;
	raw->paint_base      = true;
	raw->paint_opac      = true;
	raw->paint_opac_mode = OPACITY_MODE_ALPHA;
	raw->paint_occ       = true;
	raw->paint_rough     = true;
	raw->paint_met       = true;
	raw->paint_nor       = true;
	raw->paint_height    = true;
	raw->paint_emis      = true;
	raw->paint_subs      = true;

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *mat = g_project->_->materials->buffer[i];
		if (mat->id >= raw->id) {
			raw->id = mat->id + 1;
		}
	}
	raw->data = m;

	i32 w           = util_render_material_preview_size;
	i32 w_icon      = 50;
	raw->image      = gpu_create_render_target(w, w, GPU_TEXTURE_FORMAT_RGBA64);
	raw->image_icon = gpu_create_render_target(w_icon, w_icon, GPU_TEXTURE_FORMAT_RGBA64);

	if (c == NULL) {
		if (slot_material_default_canvas == NULL) { // Synchronous
			buffer_t *b                  = data_get_blob("default_material.arm");
			slot_material_default_canvas = b;
		}
		ui_node_canvas_t *decoded = armpack_decode(slot_material_default_canvas);
		raw->canvas               = util_clone_canvas(decoded);
		free(decoded);
		i32 id            = (raw->id + 1);
		raw->canvas->name = string("Material %d", id);
	}
	else {
		raw->canvas = util_clone_canvas(c);
	}

	if (g_config->node_previews) {
		for (i32 i = 0; i < raw->canvas->nodes->length; ++i) {
			ui_node_t *n = raw->canvas->nodes->buffer[i];
			n->flags |= UI_NODE_FLAG_PREVIEW;
		}
	}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	raw->nodes->pan_x -= 50; // Center initial position
#else
	raw->nodes->pan_x += 110;
#endif
	raw->nodes->pan_y += 120;

	return raw;
}

void slot_material_unload(slot_material_t *raw) {
	gpu_delete_texture(raw->image);
	gpu_delete_texture(raw->image_icon);
}

void slot_material_delete(slot_material_t *raw) {
	slot_material_unload(raw);
	i32 mpos = array_index_of(g_project->_->materials, raw);
	array_remove(g_project->_->materials, raw);
	if (g_project->_->materials->length > 0) {
		context_set_material(g_project->_->materials->buffer[mpos > 0 ? mpos - 1 : 0]);
	}
}
