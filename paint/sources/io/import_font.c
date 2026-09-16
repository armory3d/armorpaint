
#include "../global.h"

void import_font_run_on_next_frame(slot_font_t_array_t *font_slots) {
	for (i32 i = 0; i < font_slots->length; ++i) {
		slot_font_t *f  = font_slots->buffer[i];
		g_context->font = f;
		any_array_push(g_project->_->fonts, f);
		util_render_make_font_preview();
	}
}

void import_font_run(char *path) {
	for (i32 i = 0; i < g_project->_->fonts->length; ++i) {
		slot_font_t *f = g_project->_->fonts->buffer[i];
		if (string_equals(f->file, path)) {
			console_info(strings_asset_already_imported());
			return;
		}
	}

	draw_font_t *font = data_get_font(path);
	draw_font_init(font); // Make sure font_ is ready
	i32                  count      = draw_font_count(font);
	slot_font_t_array_t *font_slots = any_array_create_from_raw((void *[]){}, 0);

	for (i32 i = 0; i < count; ++i) {
		string_array_t *ar   = string_split(path, PATH_SEP);
		char           *name = ar->buffer[ar->length - 1];
		draw_font_t    *f    = ALLOC_INIT(draw_font_t, {.buf = font->buf, .index = font->index});
		draw_font_init(f);
		if (!draw_set_font(f, util_render_font_preview_size)) {
			console_error(tr("Error: Failed to read font data"));
			continue;
		}

		slot_font_t *font_slot = slot_font_create(name, f, path);
		any_array_push(font_slots, font_slot);
	}

	sys_notify_on_next_frame(&import_font_run_on_next_frame, font_slots);

	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
}
