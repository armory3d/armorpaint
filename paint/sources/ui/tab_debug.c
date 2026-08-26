
#include "../global.h"

void tab_debug_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Debug"), false, -1, false)) {

		ui_handle_t *h0 = ui_handle(__ID__);
		if (ui_panel(h0, "Render Targets", false, false, false)) {
			string_array_t *rt_keys = map_keys(render_path_render_targets);
			array_sort(rt_keys, NULL);
			for (i32 i = 0; i < rt_keys->length; ++i) {
				render_target_t *rt = any_map_get(render_path_render_targets, rt_keys->buffer[i]);
				ui_text(rt_keys->buffer[i], UI_ALIGN_LEFT, 0x00000000);
				ui_image(rt->_image, 0xffffffff, -1.0);
			}
			array_free(rt_keys);
			free(rt_keys);
		}

		ui_handle_t *h1 = ui_handle(__ID__);
		if (ui_panel(h1, "Performance", false, false, false)) {
			ui_text(string_tmp("%.2f ms", sys_real_delta() * 1000.0f), UI_ALIGN_LEFT, 0x00000000);
		}
	}
}
