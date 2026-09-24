#include "global.h"

void *plugin;

// Register custom viewport shader
void viewport_shader(void *shader) {
	node_shader_write_frag(shader, " \
		float3 light_dir = float3(0.5, 0.5, -0.5);\
		float dotnl = max(dot(n, light_dir), 0.0); \
		output_color = basecol * step(0.5, dotnl) + basecol; \
	");
};

void on_delete() {
	context_set_viewport_shader(NULL);
}

void main() {
	plugin = plugin_create();

	context_set_viewport_shader(viewport_shader);
	plugin_notify_on_delete(plugin, on_delete);
}
