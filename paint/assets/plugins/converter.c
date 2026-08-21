#include "global.h"

void *plugin;
ui_handle_t *h1;

void on_arm_to_json(char *path) {
	buffer_t *b = data_get_blob(path);
	char *s = armpack_decode_to_json(b);
	buffer_t *out = sys_string_to_buffer(s);
	char *p = substring(path, 0, string_length(path) - 3);
	p = string("%sjson", p);
	iron_file_save_bytes(p, out, 0);
}

void on_json_to_arm(char *path) {
	buffer_t *b = data_get_blob(path);
	buffer_t *out = json_encode_to_armpack(sys_buffer_to_string(b));
	char *p = substring(path, 0, string_length(path) - 4);
	p = string("%sarm", p);
	iron_file_save_bytes(p, out, 0);
}

void on_ui() {
	if (ui_panel(h1, "Converter", false, false, false)) {
		ui_row2();
		if (ui_button(".arm to .json", UI_ALIGN_CENTER, "")) {
			ui_files_show2("arm", false, true, on_arm_to_json);
		}
		if (ui_button(".json to .arm", UI_ALIGN_CENTER, "")) {
			ui_files_show2("json", false, true, on_json_to_arm);
		}
	}
};

void main() {
	plugin = plugin_create();
	h1 = ui_handle_create();

	plugin_notify_on_ui(plugin, on_ui);
}
