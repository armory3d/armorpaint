
#include "global.h"

char *_plugin_name = NULL;

// Tools added by plugins, tool ids start at TOOL_TYPE_PLUGIN
static any_array_t *plugin_tools = NULL;

static void plugin_remove_tools(plugin_t *p) {
	if (plugin_tools == NULL) {
		return;
	}
	plugin_tool_t *active = plugin_tool_get(g_context->tool);
	if (active != NULL && active->plugin == p) {
		context_select_tool(TOOL_TYPE_BRUSH);
		active = NULL;
	}
	for (i32 i = plugin_tools->length - 1; i >= 0; --i) {
		plugin_tool_t *t = plugin_tools->buffer[i];
		if (t->plugin == p) {
			array_splice(plugin_tools, i, 1);
		}
	}
	// Ids of the following tools shift down, rebuild the toolbar entries
	ui_toolbar_tool_names->length     = TOOL_TYPE_PLUGIN;
	ui_toolbar_tooltip_extras->length = TOOL_TYPE_PLUGIN;
	for (i32 i = 0; i < plugin_tools->length; ++i) {
		plugin_tool_t *t = plugin_tools->buffer[i];
		any_array_push(ui_toolbar_tool_names, t->name);
		any_array_push(ui_toolbar_tooltip_extras, t->tooltip);
		if (t == active) {
			g_context->tool = TOOL_TYPE_PLUGIN + i;
		}
	}
	ui_toolbar_handle->redraws = 2;
}

plugin_t *plugin_create() {
	plugin_t *p = ALLOC_INIT(plugin_t, {0});
	p->name     = string_copy(_plugin_name);
	if (p->name == NULL) {
		return p;
	}
	any_map_set(g_plugins, p->name, p);
	return p;
}

void plugin_start(char *plugin) {
	char     *file = string("plugins/%s", plugin);
	buffer_t *blob = data_get_blob(file);
	if (blob == NULL) {
		return;
	}
	_plugin_name     = string_copy(plugin);
	minic_ctx_t *ctx = minic_eval_named(sys_buffer_to_string(blob), plugin);
	data_delete_blob(file);
	// Store context on the plugin so callbacks can use it and it can be freed on stop
	plugin_t *p = any_map_get(g_plugins, plugin);
	if (p == NULL) { // Script did not call plugin_create()
		p = plugin_create();
	}
	p->ctx       = ctx;
	_plugin_name = NULL;
}

void plugin_stop(char *plugin) {
	plugin_t *p = any_map_get(g_plugins, plugin);
	if (p == NULL) {
		return;
	}
	if (p->on_delete != NULL) {
		minic_ctx_call_fn(p->ctx, p->on_delete, NULL, 0);
	}
	plugin_remove_tools(p);
	minic_ctx_free(p->ctx);
	p->ctx = NULL;
	map_delete(g_plugins, plugin);
}

void plugin_notify_on_ui(plugin_t *plugin, void *f) {
	plugin->on_ui = f;
}

void plugin_notify_on_update(plugin_t *plugin, void *f) {
	plugin->on_update = f;
}

void plugin_notify_on_delete(plugin_t *plugin, void *f) {
	plugin->on_delete = f;
}

// Adds a tool to the toolbar, the paint pipeline is skipped while it is active
// on_header draws the tool properties, on_stroke runs every frame of a stroke in the viewport, on_stroke_end on release
i32 plugin_register_tool(plugin_t *plugin, char *name, char *tooltip, i32 icon, void *on_header, void *on_stroke, void *on_stroke_end) {
	if (plugin_tools == NULL) {
		plugin_tools = any_array_create(0);
	}
	plugin_tool_t *t = ALLOC_INIT(plugin_tool_t, {0});
	t->plugin        = plugin;
	t->name          = string_copy(name);
	t->tooltip       = string_copy(tooltip != NULL ? tooltip : "");
	t->icon          = icon;
	t->on_header     = on_header;
	t->on_stroke     = on_stroke;
	t->on_stroke_end = on_stroke_end;
	any_array_push(plugin_tools, t);
	any_array_push(ui_toolbar_tool_names, t->name);
	any_array_push(ui_toolbar_tooltip_extras, t->tooltip);
	ui_toolbar_handle->redraws = 2;
	return TOOL_TYPE_PLUGIN + plugin_tools->length - 1;
}

// NULL for the built-in tools
plugin_tool_t *plugin_tool_get(i32 tool) {
	if (plugin_tools == NULL || tool < TOOL_TYPE_PLUGIN || tool - TOOL_TYPE_PLUGIN >= plugin_tools->length) {
		return NULL;
	}
	return plugin_tools->buffer[tool - TOOL_TYPE_PLUGIN];
}

void plugin_tool_call(plugin_tool_t *t, void *f) {
	if (t != NULL && f != NULL && t->plugin->ctx != NULL) {
		minic_ctx_call_fn(t->plugin->ctx, f, NULL, 0);
	}
}

// image is sampled by the uv of the mesh, its red channel blends the mesh color towards color, NULL removes the overlay
void plugin_tool_set_overlay(i32 tool, gpu_texture_t *image, i32 color) {
	plugin_tool_t *t = plugin_tool_get(tool);
	if (t == NULL) {
		return;
	}
	bool reparse     = (t->overlay == NULL) != (image == NULL) || t->overlay_color != color;
	t->overlay       = image;
	t->overlay_color = color;
	if (reparse && g_context->tool == tool) {
		make_material_parse_mesh_material();
	}
	g_context->ddirty = 2;
}

// Active plugin tool when it has an overlay
plugin_tool_t *plugin_tool_overlay() {
	plugin_tool_t *t = plugin_tool_get(g_context->tool);
	return t != NULL && t->overlay != NULL ? t : NULL;
}
