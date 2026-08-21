#include "global.h"

void *plugin;
ui_handle_t *h0;
ui_handle_t *h1;
ui_handle_t *h2;
ui_handle_t *h3;
ui_handle_t *h4;
ui_handle_t *h5;
ui_handle_t *h6;

void on_ui() {
	if (ui_panel(h0, "My Plugin", false, false, false)) {
		ui_text("Label", 0, 0);

		ui_text_input(h1, "Text Input", UI_ALIGN_LEFT, true, false);
		if (ui_button("Button", UI_ALIGN_CENTER, "")) {
			console_info("Hello");
		}

		f32_array_t *row = f32_array_create(2);
		row->buffer[0] = 1.0 / 2.0;
		row->buffer[1] = 1.0 / 2.0;
		ui_row(row);
		ui_button("Button A", UI_ALIGN_CENTER, "");
		ui_button("Button B", UI_ALIGN_CENTER, "");

		string_array_t *items = string_array_create(2);
		items->buffer[0] = "Item 1";
		items->buffer[1] = "Item 2";
		ui_combo(h2, items, "Combo", true, UI_ALIGN_LEFT, true);

		ui_row2();
		ui_slider(h3, "Slider", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		ui_slider(h4, "Slider", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);

		ui_check(h5, "Check", "");
		ui_radio(h6, 0, "Radio 1", "");
		ui_radio(h6, 1, "Radio 2", "");
		ui_radio(h6, 2, "Radio 3", "");
	}
}

void main() {
	plugin = plugin_create();
	h0 = ui_handle_create();
	h1 = ui_handle_create();
	h2 = ui_handle_create();
	h3 = ui_handle_create();
	h4 = ui_handle_create();
	h5 = ui_handle_create();
	h6 = ui_handle_create();
	plugin_notify_on_ui(plugin, on_ui);
}
