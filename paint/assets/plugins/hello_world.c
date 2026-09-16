#include "global.h"

void *plugin;
bool  expanded     = false;
char *text         = "";
int   selection    = 0;
float first_value  = 0;
float second_value = 0;
bool  checked      = false;
int   radio        = 0;

void on_ui() {
	if (ui_panel(&expanded, "My Plugin", false, false, false)) {
		ui_text("Label", 0, 0);

		ui_text_input(&text, "Text Input", UI_ALIGN_LEFT, true, false);
		if (ui_button("Button", UI_ALIGN_CENTER, "")) {
			console_info("Hello");
		}

		f32_array_t *row = f32_array_create(2);
		row->buffer[0]   = 1.0 / 2.0;
		row->buffer[1]   = 1.0 / 2.0;
		ui_row(row);
		ui_button("Button A", UI_ALIGN_CENTER, "");
		ui_button("Button B", UI_ALIGN_CENTER, "");

		string_array_t *items = string_array_create(2);
		items->buffer[0]      = "Item 1";
		items->buffer[1]      = "Item 2";
		ui_combo(&selection, items, "Combo", true, UI_ALIGN_LEFT, true);

		ui_row2();
		ui_slider(&first_value, "Slider", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		ui_slider(&second_value, "Slider", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);

		ui_check(&checked, "Check", "");
		ui_radio(&radio, 0, "Radio 1", "");
		ui_radio(&radio, 1, "Radio 2", "");
		ui_radio(&radio, 2, "Radio 3", "");
	}
}

void main() {
	plugin = plugin_create();
	plugin_notify_on_ui(plugin, on_ui);
}
