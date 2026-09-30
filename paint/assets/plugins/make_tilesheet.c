#include "global.h"

void *plugin;
bool  expanded        = false;
float tile_size_value = 256;
float columns_value   = 8;
float frames_value    = 64;

void *tilesheet;
int   baking;
int   tile_size;
int   columns;
int   frames;

int frame;
int col;
int row;
int wait;
int reskinned;

void on_update() {
	if (!baking) {
		return;
	}

	iron_delay_idle_sleep();

	if (frame < frames) {
		if (!reskinned) {
			reskinned = 1;
			// Pose the mesh at this frame, only meshes imported with skinning applied
			if (project_reskin_mesh(frame)) {
				wait = 2; // Wait for re-render
				return;
			}
		}

		if (wait > 0) {
			wait = wait - 1;
			return;
		}

		float x = col * tile_size;
		float y = row * tile_size;
		viewport_capture_screenshot_to(tilesheet, x, y, tile_size, tile_size);

		frame = frame + 1;
		col   = col + 1;
		if (col >= columns) {
			col = 0;
			row = row + 1;
		}
		reskinned = 0;
	}
	else {
		viewport_save_texture(tilesheet);
		context_t *c            = script_get_context();
		c->capturing_screenshot = false;
		c->ddirty               = 2;
		tilesheet               = NULL;
		baking                  = 0;
	}
}

void on_ui() {
	if (ui_panel(&expanded, "Make Tilesheet", false, false, false)) {

		ui_slider(&tile_size_value, "Tile Size", 0, 512, true, 1, true, UI_ALIGN_LEFT, true);
		ui_slider(&columns_value, "Columns", 0, 64, true, 1, true, UI_ALIGN_LEFT, true);
		ui_slider(&frames_value, "Frames", 0, 1024, true, 1, true, UI_ALIGN_LEFT, true);

		if (ui_button("Bake", UI_ALIGN_CENTER, "") && !baking) {
			tile_size = tile_size_value;
			columns   = columns_value;
			frames    = frames_value;

			// Square atlas
			int size  = tile_size * columns;
			tilesheet = gpu_create_render_target(size, size, GPU_TEXTURE_FORMAT_RGBA32);

			frame                   = 0;
			col                     = 0;
			row                     = 0;
			wait                    = 2;
			reskinned               = 0;
			baking                  = 1;
			context_t *c            = script_get_context();
			c->capturing_screenshot = true;
			c->ddirty               = 2;
		}
	}
}

void main() {
	plugin    = plugin_create();
	tilesheet = NULL;
	baking    = 0;
	plugin_notify_on_ui(plugin, on_ui);
	plugin_notify_on_update(plugin, on_update);
}
