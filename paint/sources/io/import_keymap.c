
#include "../global.h"

void import_keymap_run(char *path) {
	if (!path_is_json(path)) {
		console_error(strings_unknown_asset_format());
		return;
	}

	char *filename = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));
	char *dst_path = string("%s%skeymap_presets%s%s", path_data(), PATH_SEP, PATH_SEP, filename);
	file_copy(path, dst_path);       // Copy to preset folder
	box_preferences_fetch_keymaps(); // Refresh file list
	console_info(string("%s %s", tr("Keymap imported:"), filename));
}
