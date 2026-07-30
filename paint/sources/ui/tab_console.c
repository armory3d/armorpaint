
#include "../global.h"

void tab_console_draw_export_on_file_picked(char *path) {
	char *str = string_array_join(console_last_traces, "\n");
	char *f   = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	path = string("%s%s%s", path, PATH_SEP, f);
	if (!ends_with(path, ".txt")) {
		path = string("%s.txt", path);
	}
	iron_file_save_bytes(path, sys_string_to_buffer(str), 0);
}

void tab_console_run_done(char *s) {
	console_log(tr("Script updated."));

	if (starts_with(s, "```c")) {
		s = substring(s, 5, string_length(s) - 4);
	}

	tab_scripts_get();
	g_project->script_datas->buffer[0] = string_copy(s);
	tab_scripts_minimap_dirty          = true;

	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
}

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)

void tab_console_run_button_on_next_frame(void *_) {
	box_preferences_htab->i = PREFERENCES_TAB_NEURAL;
	box_preferences_show();
}

bool tab_console_run_button(ui_handle_t *h_input, bool press_run) {
	bool use_cli = g_config->console_model != CONSOLE_MODEL_QWEN;
	bool found   = true;
	if (!use_cli) {
		char *url       = box_preferneces_model_url_from_name("Qwen");
		char *file_name = box_preferences_file_name_from_url(url);
		found           = box_preferences_model_exists(file_name);
	}

	if (iron_exec_async_done == 0) {
		ui_icon_button(tr("Processing..."), ICON_STOP, UI_ALIGN_CENTER);
	}
	else if (!found && ui_icon_button(tr("Setup"), ICON_COG, UI_ALIGN_CENTER)) {
		sys_notify_on_next_frame(&tab_console_run_button_on_next_frame, NULL);
	}
	else if (found && (ui_icon_button(tr("Run"), ICON_PLAY, UI_ALIGN_CENTER) || press_run)) {

		console_log(string(">%s", h_input->text));
		text_to_text_node_run(h_input->text, tab_console_run_done);
		h_input->text = "";

		return true;
	}
	return false;
}

#endif

void tab_console_draw(ui_handle_t *htab) {
	char *title = console_message_timer > 0 ? string("%s        ", console_message) : tr("Console");
	i32   color = console_message_timer > 0 ? console_message_color : -1;

	if (ui_tab(htab, title, false, color, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		ui_begin_sticky();
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS) // Copy
		f32_array_t *row = f32_array_create_from_raw(
		    (f32[]){
		        -100,
		        -100,
		        -100,
		    },
		    3);
#else
		f32_array_t *row = f32_array_create_from_raw(
		    (f32[]){
		        -100,
		        -100,
		    },
		    2);
#endif
		ui_row(row);

		ui_handle_t *h_input = ui_handle(__ID__);

		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			gc_unroot(console_last_traces);
			console_last_traces = any_array_create_from_raw((void *[]){}, 0);
			gc_root(console_last_traces);
			h_input->text = "";
			text_to_text_node_clear();
		}
		if (ui_icon_button(tr("Export"), ICON_EXPORT, UI_ALIGN_CENTER)) {
			ui_files_show("txt", true, false, &tab_console_draw_export_on_file_picked);
		}
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
		if (ui_icon_button(tr("Copy"), ICON_COPY, UI_ALIGN_CENTER)) {
			char *str = string_array_join(console_last_traces, "\n");
			iron_copy_to_clipboard(str);
		}
#endif

		ui_end_sticky();

		draw_font_t *_font           = g_font;
		i32          _font_size      = g_ui->font_size;
		i32          _element_offset = g_theme->ELEMENT_OFFSET;
		g_theme->ELEMENT_OFFSET      = 0;
		draw_font_t *f               = data_get_font("font_mono.ttf");
		ui_set_font(g_ui, f);
		g_ui->font_size = math_floor(15 * UI_SCALE());
		for (i32 i = 0; i < console_last_traces->length; ++i) {
			char    *t     = console_last_traces->buffer[i];
			i32      len   = string_length(t);
			float    max_w = g_ui->_w;
			uint32_t bg    = starts_with(t, ">") ? 0x22000000 : 0x00000000;
			if (draw_string_width(f, g_ui->font_size, t) <= max_w) {
				ui_text(t, UI_ALIGN_LEFT, bg);
				continue;
			}

			// Word wrap
			i32 start = 0;
			while (start < len) {
				i32 end        = start;
				i32 last_space = -1;
				while (end < len && draw_sub_string_width(f, g_ui->font_size, t, start, end + 1) <= max_w) {
					if (t[end] == ' ') {
						last_space = end;
					}
					end++;
				}
				if (end == len) {
					ui_text(substring(t, start, end), UI_ALIGN_LEFT, bg);
					break;
				}
				if (last_space > start) {
					ui_text(substring(t, start, last_space), UI_ALIGN_LEFT, bg);
					start = last_space + 1;
				}
				else {
					ui_text(substring(t, start, end > start ? end : start + 1), UI_ALIGN_LEFT, bg);
					start = end > start ? end : start + 1;
				}
			}
		}

		g_theme->ELEMENT_OFFSET = _element_offset;

		row = f32_array_create_from_raw(
		    (f32[]){
		        0.9,
		        0.1,
		    },
		    2);
		ui_row(row);

		ui_text_input(h_input, "", UI_ALIGN_LEFT, true, false);
		bool press_run = h_input->changed && g_ui->is_return_down;

		ui_set_font(g_ui, _font);
		g_ui->font_size = _font_size;

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
		tab_console_run_button(h_input, press_run);
#else
		if (ui_icon_button(tr("Run"), ICON_PLAY, UI_ALIGN_CENTER) || press_run) {
			console_log(string(">%s", h_input->text));
			minic_ctx_free(minic_eval(string("float main() { %s }", h_input->text)));
			h_input->text = "";
		}
#endif
	}
}
