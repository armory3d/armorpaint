
#include "../global.h"

void import_text_default_importer(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		console_error(strings_unknown_asset_format());
		return;
	}

	string_array_t *ar   = string_split(path, PATH_SEP);
	char           *name = ar->buffer[ar->length - 1];

	ui_box_show_message(name, sys_buffer_to_string(b), true);
	data_delete_blob(path);
}

void import_text_run(char *path) {
	char *ext                    = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void (*importer)(char *path) = any_map_get(import_text_importers, ext);

	if (importer == NULL) {
		import_text_default_importer(path);
		return;
	}

	importer(path);
}
