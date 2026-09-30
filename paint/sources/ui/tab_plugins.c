
#include "../global.h"

void tab_plugins_draw(i32 *htab) {
	if (ui_tab(htab, tr("Plugins"), false, -1, false)) {

		ui_begin_sticky();

		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -100,
		    },
		    1);
		ui_row(row);

		if (ui_icon_button(tr("Preferences"), ICON_COG, UI_ALIGN_CENTER)) {
			box_preferences_tab = PREFERENCES_TAB_PLUGINS;
			box_preferences_show();
		}
		ui_end_sticky();

		// Draw plugins
		string_array_t *keys = map_keys(g_plugins);
		for (i32 i = 0; i < keys->length; ++i) {
			plugin_t *p = any_map_get(g_plugins, keys->buffer[i]);
			if (p->on_ui != NULL) {
				minic_ctx_call_fn(p->ctx, p->on_ui, NULL, 0);
			}
		}
		array_free(keys);
		free(keys);
	}
}
