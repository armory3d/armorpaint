#include "global.h"

void *plugin;
ui_handle_t *h0;

void on_ui() {
	if (ui_panel(h0, "PCA", false, false, false)) {

		if (ui_button("Button", UI_ALIGN_CENTER, "")) {
			console_info("Hello");
		}
	}
}

void main() {
	plugin = plugin_create();
	h0 = ui_handle_create();
	gc_root(plugin);
	gc_root(h0);
	plugin_notify_on_ui(plugin, on_ui);
}
