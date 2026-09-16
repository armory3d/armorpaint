
#include "../global.h"

void import_sound_run(char *path) {
	for (i32 i = 0; i < g_project->_->sounds->length; ++i) {
		slot_sound_t *s = g_project->_->sounds->buffer[i];
		if (string_equals(s->file, path)) {
			console_info(strings_asset_already_imported());
			return;
		}
	}

	string_array_t *ar   = string_split(path, PATH_SEP);
	char           *name = ar->buffer[ar->length - 1];

	sound_t      *sound = data_get_sound(path);
	slot_sound_t *s     = slot_sound_create(name, sound, path);
	g_context->sound    = s;
	any_array_push(g_project->_->sounds, s);

	console_info(string("%s %s", tr("Sound imported:"), name));
}
