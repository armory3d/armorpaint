
#include "../global.h"

static bool tab_debug_render_targets = false;
static bool tab_debug_performance    = false;

void tab_debug_draw(i32 *htab) {
	if (ui_tab(htab, tr("Debug"), false, -1, false)) {

		if (ui_panel(&tab_debug_render_targets, "Render Targets", false, false, false)) {
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

		if (ui_panel(&tab_debug_performance, "Performance", false, false, false)) {
			ui_text(string_tmp("%.2f ms", sys_real_delta() * 1000.0f), UI_ALIGN_LEFT, 0x00000000);
		}
	}
}
