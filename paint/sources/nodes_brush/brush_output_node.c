
#include "../global.h"

typedef struct brush_output_node {
	struct logic_node *base;
	struct ui_node    *raw;
} brush_output_node_t;

static brush_output_node_t *brush_output_node_inst;

void brush_output_node_parse_inputs() {
	brush_output_node_t *self         = brush_output_node_inst;
	gpu_texture_t       *last_mask    = g_context->brush_mask_image;
	gpu_texture_t       *last_stencil = g_context->brush_stencil_image;

	logic_node_value_t *input0 = logic_node_input_get(self->base->inputs->buffer[0]);
	logic_node_value_t *input1 = logic_node_input_get(self->base->inputs->buffer[1]);
	logic_node_value_t *input2 = logic_node_input_get(self->base->inputs->buffer[2]);
	logic_node_value_t *input3 = logic_node_input_get(self->base->inputs->buffer[3]);
	logic_node_value_t *input4 = logic_node_input_get(self->base->inputs->buffer[4]);
	logic_node_value_t *input5 = logic_node_input_get(self->base->inputs->buffer[5]);
	logic_node_value_t *input6 = logic_node_input_get(self->base->inputs->buffer[6]);

	g_context->paint_vec          = input0->_vec4;
	g_context->brush_nodes_radius = input1->_f32;
	g_context->brush_nodes_scale  = input2->_f32;
	g_context->brush_nodes_angle  = input3->_f32;

	g_context->brush_nodes_uses_random = false;
	for (i32 i = 0; i < g_context->brush->canvas->nodes->length; ++i) {
		ui_node_t *n = g_context->brush->canvas->nodes->buffer[i];
		if (string_equals(n->type, "random_node")) {
			g_context->brush_nodes_uses_random = true;
			break;
		}
	}

	logic_node_value_t *opac = input4; // Float or texture name
	if (opac == NULL) {
		opac = GC_ALLOC_INIT(logic_node_value_t, {._f32 = 1.0});
	}
	if (opac->_str != NULL) { // string
		g_context->brush_mask_image_is_alpha = ends_with(opac->_str, ".a");
		opac->_str                           = string_copy(substring(opac->_str, 0, string_last_index_of(opac->_str, ".")));
		g_context->brush_nodes_opacity       = 1.0;
		i32 index                            = -1;
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			if (string_equals(g_project->_->assets->buffer[i]->name, opac->_str)) {
				index = i;
				break;
			}
		}
		if (index != -1) {
			asset_t *asset              = g_project->_->assets->buffer[index];
			g_context->brush_mask_image = project_get_image(asset);
		}
	}
	else {
		g_context->brush_nodes_opacity = opac->_f32;
		g_context->brush_mask_image    = NULL;
	}

	g_context->brush_nodes_hardness = input5->_f32;

	logic_node_value_t *stencil = input6; // Float or texture name
	if (stencil == NULL) {
		stencil = GC_ALLOC_INIT(logic_node_value_t, {._f32 = 1.0});
	}
	if (stencil->_str != NULL) { // string
		g_context->brush_stencil_image_is_alpha = ends_with(stencil->_str, ".a");
		stencil->_str                           = string_copy(substring(stencil->_str, 0, string_last_index_of(stencil->_str, ".")));
		i32 index                               = -1;
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			if (string_equals(g_project->_->assets->buffer[i]->name, stencil->_str)) {
				index = i;
				break;
			}
		}
		if (index != -1) {
			asset_t *asset                 = g_project->_->assets->buffer[index];
			g_context->brush_stencil_image = project_get_image(asset);
		}
	}
	else {
		g_context->brush_stencil_image = NULL;
	}

	if (last_mask != g_context->brush_mask_image || last_stencil != g_context->brush_stencil_image) {
		make_material_parse_paint_material(true);
	}

	g_context->brush_directional = self->raw->buttons->buffer[0]->default_value->buffer[0] > 0.0;
}

void brush_output_node_run() {
	f32 left   = 0.0;
	f32 right  = 1.0;
	f32 top    = 0.0;
	f32 bottom = 1.0;

	if (g_context->paint2d) {
		left  = 1.0;
		right = (g_context->split_view ? 2.0 : 1.0) + ui_view2d_ww / (float)base_w();
	}

	// Do not paint over floating toolbar
	if (context_is_floating_toolbar()) {
		i32 w = ui_toolbar_x() + ui_toolbar_w(false);
		left += w / (float)sys_w();
		top += w / (float)sys_h();

#ifdef IRON_IOS
		if (config_is_iphone()) {
			top += w / (float)sys_h();
		}
#endif
	}

	// First time init
	if (g_context->last_paint_x < 0 || g_context->last_paint_y < 0) {
		g_context->last_paint_vec_x = g_context->paint_vec.x;
		g_context->last_paint_vec_y = g_context->paint_vec.y;
	}

	// Paint bounds
	if (g_context->paint_vec.x < left || g_context->paint_vec.x > right || g_context->paint_vec.y < top || g_context->paint_vec.y > bottom) {
		return;
	}

	// Do not paint over fill layer
	if (g_context->layer->fill_material != NULL && g_context->tool != TOOL_TYPE_PICKER && g_context->tool != TOOL_TYPE_MATERIAL &&
	    g_context->tool != TOOL_TYPE_COLORID) {
		return;
	}

	// Do not paint over groups
	if (slot_layer_is_group(g_context->layer)) {
		return;
	}

	if (g_context->brush_locked) {
		return;
	}

	if (!slot_layer_is_visible(g_context->layer) && !g_context->paint2d) {
		return;
	}

	if (g_ui->is_hovered || base_is_dragging || base_is_resizing || g_ui->is_scrolling || g_ui->combo_selected_handle != NULL) {
		return;
	}

	bool down    = mouse_down("left") || pen_down("tip");
	bool started = mouse_started("left") || pen_started("tip");

	// Set color pick
	if (down && g_context->tool == TOOL_TYPE_COLORID && g_project->_->assets->length > 0) {
		g_context->colorid_picked  = true;
		ui_toolbar_handle->redraws = 1;
	}

	// Path layer - add path points only
	if (slot_layer_is_path(g_context->layer) && !started) {
		return;
	}

	// Path layer - dragging existing path point
	if (slot_layer_is_path(g_context->layer) && util_layer_is_path_point_dragging()) {
		return;
	}

	// Prevent painting the same spot
	bool same_spot = g_context->paint_vec.x == g_context->last_paint_x && g_context->paint_vec.y == g_context->last_paint_y;
	bool lazy      = g_context->tool == TOOL_TYPE_BRUSH && g_context->brush_lazy_radius > 0;
	if (down && (same_spot || lazy)) {
		g_context->painted++;
	}
	else {
		g_context->painted = 0;
	}
	g_context->last_paint_x = g_context->paint_vec.x;
	g_context->last_paint_y = g_context->paint_vec.y;

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		g_context->painted = 0; // Always paint particles
	}

	if (g_context->painted == 0) {
		brush_output_node_parse_inputs();
	}

	// Path layer - add point and repaint
	if (slot_layer_is_path(g_context->layer)) {
		util_layer_add_path_point(g_context->layer, g_context->paint_vec.x, g_context->paint_vec.y);
		return;
	}

	if (g_context->painted <= 1) {
		g_context->pdirty = 1;
		g_context->rdirty = 2;
		sculpt_push_undo  = true;
	}
}

void *brush_output_node_create(ui_node_t *raw, f32_array_t *args) {
	brush_output_node_t *n = GC_ALLOC_INIT(brush_output_node_t, {0});
	n->base                = logic_node_create(n);
	n->raw                 = raw;
	brush_output_node_inst = n;
	return n;
}

// let brush_output_node_def: node_t = {
// 	id: 0,
// 	name: _tr("Brush Output"),
// 	type: "brush_output_node",
// 	x: 0,
// 	y: 0,
// 	color: 0xff4982a0,
// 	inputs: [
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Position"),
// 			type: "VECTOR",
// 			color: 0xff63c763,
// 			default_value: f32_array_create_xyz(0.0, 0.0, 0.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		},
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Radius"),
// 			type: "VALUE",
// 			color: 0xffa1a1a1,
// 			default_value: f32_array_create_x(1.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		},
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Scale"),
// 			type: "VALUE",
// 			color: 0xffa1a1a1,
// 			default_value: f32_array_create_x(1.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		},
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Angle"),
// 			type: "VALUE",
// 			color: 0xffa1a1a1,
// 			default_value: f32_array_create_x(0.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		},
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Opacity"),
// 			type: "VALUE",
// 			color: 0xffa1a1a1,
// 			default_value: f32_array_create_x(1.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		},
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Hardness"),
// 			type: "VALUE",
// 			color: 0xffa1a1a1,
// 			default_value: f32_array_create_x(1.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		},
// 		{
// 			id: 0,
// 			node_id: 0,
// 			name: _tr("Stencil"),
// 			type: "VALUE",
// 			color: 0xffa1a1a1,
// 			default_value: f32_array_create_x(1.0),
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			display: 0
// 		}
// 	],
// 	outputs: [],
// 	buttons: [
// 		{
// 			name: _tr("Directional"),
// 			type: "BOOL",
// 			output: 0,
// 			default_value: f32_array_create_x(0),
//			data: NULL,
//			min: 0.0,
//			max: 1.0,
//			precision: 100,
//			height: 0
// 		}
// 	],
//	width: 0,
//	flags: 0
// };
