
#include "global.h"

slot_sound_t *slot_sound_create(char *name, sound_t *sound, char *file) {
	slot_sound_t *raw = ALLOC_INIT(slot_sound_t, {0});
	raw->id           = 0;

	for (i32 i = 0; i < g_project->_->sounds->length; ++i) {
		slot_sound_t *slot = g_project->_->sounds->buffer[i];
		if (slot->id >= raw->id) {
			raw->id = slot->id + 1;
		}
	}

	raw->name  = string_copy(name);
	raw->sound = sound;
	raw->file  = string_copy(file);

	return raw;
}
