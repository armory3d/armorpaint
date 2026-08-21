#include "global.h"

void *plugin;
ui_handle_t *h1;
ui_handle_t *h2;
float timer = 0.0;

void on_ui() {
	if (ui_panel(h1, "Auto Save")) {
		ui_slider(h2, "min", 1, 15, false, 1, true, UI_ALIGN_LEFT, true);
	}
}

void on_update() {
	if (string_equals(project_filepath_get(), "")) {
		return;
	}
	timer += sys_real_delta();
	if (timer >= h2->f * 60.0) {
		timer = 0.0;
		project_save(false);
	}
}

void main() {
	plugin = plugin_create();
	h1 = ui_handle_create();
	h2 = ui_handle_create();
	h2->f = 5.0;

	plugin_notify_on_ui(plugin, on_ui);
	plugin_notify_on_update(plugin, on_update);
}
