
#include "../global.h"

static char *util_touch_action_paint_remap = "";

void util_touch_update() {
	// Same mapping for paint and rotate (predefined in touch keymap)
	if (context_in_3d_view()) {
		char *paint_key  = any_map_get(g_keymap, "action_paint");
		char *rotate_key = any_map_get(g_keymap, "action_rotate");
		if (mouse_started("left") && string_equals(paint_key, rotate_key)) {
			util_touch_action_paint_remap = string_copy(paint_key);
			util_render_pick_pos_nor_tex();
			bool is_mesh = math_abs(g_context->posx_picked) < 50 && math_abs(g_context->posy_picked) < 50 && math_abs(g_context->posz_picked) < 50;
#ifdef IRON_ANDROID
			// Allow rotating with both pen and touch, because hovering a pen prevents touch input on android
			bool pen_only = false;
#else
			bool pen_only = g_context->pen_painting_only;
#endif
			bool is_pen = pen_only && pen_down("tip");
			// Mesh picked - disable rotate
			// Pen painting only - rotate with touch, paint with pen
			if ((is_mesh && !pen_only) || is_pen) {
				any_map_set(g_keymap, "action_rotate", "");
				any_map_set(g_keymap, "action_paint", util_touch_action_paint_remap);
			}
			// World sphere picked - disable paint
			else {
				any_map_set(g_keymap, "action_paint", "");
				any_map_set(g_keymap, "action_rotate", util_touch_action_paint_remap);
			}
		}
		else if (!mouse_down("left") && !string_equals(util_touch_action_paint_remap, "")) {
			any_map_set(g_keymap, "action_rotate", util_touch_action_paint_remap);
			any_map_set(g_keymap, "action_paint", util_touch_action_paint_remap);
			util_touch_action_paint_remap = "";
		}
	}
}
