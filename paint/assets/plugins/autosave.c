#include "global.h"

void *plugin;
bool  expanded = false;
float interval = 5.0;
float timer    = 0.0;

void on_ui() {
	if (ui_panel(&expanded, "Auto Save", false, false, false)) {
		ui_slider(&interval, "min", 1, 15, false, 1, true, UI_ALIGN_LEFT, true);
	}
}

void on_update() {
	if (string_equals(project_filepath_get(), "")) {
		return;
	}
	timer += sys_real_delta();
	if (timer >= interval * 60.0) {
		timer = 0.0;
		project_save(false);
	}
}

void main() {
	plugin = plugin_create();

	plugin_notify_on_ui(plugin, on_ui);
	plugin_notify_on_update(plugin, on_update);
}
