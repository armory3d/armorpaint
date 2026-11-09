
#include "../global.h"

gpu_texture_t *_tab_swatches_empty;
i32            tab_swatches_drag_pos = -1;
i32            _tab_swatches_draw_i;
swatch_color_t tab_swatches_edit_prev;

// White rounded rect, tinted with the swatch color
gpu_texture_t *tab_swatches_empty_get() {
	if (_tab_swatches_empty == NULL) {
		i32         size   = 64;
		f32         radius = 10.0;
		u8_array_t *b      = u8_array_create(size * size * 4);
		for (i32 y = 0; y < size; ++y) {
			for (i32 x = 0; x < size; ++x) {
				// Distance into the corner circle, anti-aliased edge
				f32 cx           = math_max(math_max(radius - (x + 0.5), (x + 0.5) - (size - radius)), 0.0);
				f32 cy           = math_max(math_max(radius - (y + 0.5), (y + 0.5) - (size - radius)), 0.0);
				f32 d            = math_sqrt(cx * cx + cy * cy);
				f32 alpha        = math_min(math_max(radius - d + 0.5, 0.0), 1.0);
				i32 i            = (y * size + x) * 4;
				b->buffer[i + 0] = 255;
				b->buffer[i + 1] = 255;
				b->buffer[i + 2] = 255;
				b->buffer[i + 3] = alpha * 255;
			}
		}
		_tab_swatches_empty = gpu_create_texture_from_bytes(b, size, size, GPU_TEXTURE_FORMAT_RGBA32);
		array_free(b);
		free(b);
	}
	return _tab_swatches_empty;
}

typedef struct tab_swatches_sphere {
	u32            base;
	f32            roughness;
	f32            metallic;
	gpu_texture_t *image;
} tab_swatches_sphere_t;

static swatch_display_t       _tab_swatches_display      = SWATCH_DISPLAY_2D;
static tab_swatches_sphere_t *_tab_swatches_spheres      = NULL;
static i32                    _tab_swatches_sphere_count = 0;

static u8 tab_swatches_tonemap(f32 c) {
	f32 x = math_max(0.0, c - 0.004);
	f32 v = (x * (x * 6.2 + 0.5)) / (x * (x * 6.2 + 1.7) + 0.06);
	return math_floor(math_min(math_max(v, 0.0), 1.0) * 255.0 + 0.5);
}

static void tab_swatches_env_brdf(f32 roughness, f32 dotnv, f32 *a, f32 *b) {
	f32 rx   = -1.0 * roughness + 1.0;
	f32 ry   = -0.0275 * roughness + 0.0425;
	f32 rz   = -0.572 * roughness + 1.04;
	f32 rw   = 0.022 * roughness - 0.04;
	f32 a004 = math_min(rx * rx, math_pow(2.0, -9.28 * dotnv)) * rx + ry;
	*a       = -1.04 * a004 + rz;
	*b       = 1.04 * a004 + rw;
}

static f32 tab_swatches_env(f32 rx, f32 ry, f32 rz, f32 dx, f32 dy, f32 dz, f32 roughness) {
	static const f32 horizon[]   = {0.5, 0.55, 0.6, 0.7, 0.8, 1.0};
	static const f32 lobe_exp[]  = {600.0, 150.0, 40.0, 12.0, 5.0, 2.0};
	static const f32 lobe_peak[] = {3.0, 2.0, 1.2, 0.8, 0.5, 0.35};
	f32              level       = roughness * 5.0;
	i32              l0          = math_floor(level);
	i32              l1          = l0 < 5 ? l0 + 1 : 5;
	f32              t           = level - l0;
	f32              v[2];
	for (i32 k = 0; k < 2; ++k) {
		i32 l   = k == 0 ? l0 : l1;
		f32 h   = horizon[l];
		f32 s   = math_min(math_max((ry + h) / (2.0 * h), 0.0), 1.0);
		f32 dot = rx * dx + ry * dy + rz * dz;
		v[k]    = 0.12 + (0.47 - 0.12) * s * s * (3.0 - 2.0 * s) + (dot > 0.0 ? lobe_peak[l] * math_pow(dot, lobe_exp[l]) : 0.0);
	}
	return v[0] + (v[1] - v[0]) * t;
}

static gpu_texture_t *tab_swatches_make_sphere(swatch_color_t *s) {
	i32         size = 96;
	u8_array_t *b    = u8_array_create(size * size * 4);
	f32         base[3];
	base[0] = math_pow(color_get_rb(s->base) / 255.0, 2.2);
	base[1] = math_pow(color_get_gb(s->base) / 255.0, 2.2);
	base[2] = math_pow(color_get_bb(s->base) / 255.0, 2.2);

	f32 dx    = -0.416;
	f32 dy    = 0.78;
	f32 dz    = 0.468;
	f32 r     = size / 2.0 - 1.0;
	f32 rough = math_min(math_max(s->roughness, 0.0), 1.0);
	f32 metal = math_min(math_max(s->metallic, 0.0), 1.0);
	for (i32 y = 0; y < size; ++y) {
		for (i32 x = 0; x < size; ++x) {
			f32 px    = (x + 0.5 - size / 2.0) / r;
			f32 py    = (size / 2.0 - (y + 0.5)) / r;
			f32 d2    = px * px + py * py;
			f32 alpha = math_min(math_max((1.0 - math_sqrt(d2)) * r + 0.5, 0.0), 1.0);
			i32 i     = (y * size + x) * 4;
			if (alpha <= 0.0) {
				b->buffer[i + 3] = 0;
				continue;
			}
			f32 nz    = math_sqrt(math_max(1.0 - d2, 0.0));
			f32 dotnv = math_max(nz, 0.05);

			f32 rx  = 2.0 * nz * px;
			f32 ry  = 2.0 * nz * py;
			f32 rz  = 2.0 * nz * nz - 1.0;
			f32 irr = (0.886227 * 0.5 + 2.0 * 0.511664 * 0.65 * (px * dx + py * dy + nz * dz)) / math_pi();
			f32 env = tab_swatches_env(rx, ry, rz, dx, dy, dz, rough);
			f32 ba;
			f32 bb;
			tab_swatches_env_brdf(rough, dotnv, &ba, &bb);
			for (i32 c = 0; c < 3; ++c) {
				f32 albedo       = base[c] * (1.0 - metal);
				f32 f0           = 0.04 + (base[c] - 0.04) * metal;
				f32 col          = (irr * albedo + env * (f0 * ba + bb)) * 4.0;
				b->buffer[i + c] = tab_swatches_tonemap(math_max(col, 0.0));
			}
			b->buffer[i + 3] = alpha * 255;
		}
	}
	gpu_texture_t *image = gpu_create_texture_from_bytes(b, size, size, GPU_TEXTURE_FORMAT_RGBA32);
	array_free(b);
	free(b);
	return image;
}

static gpu_texture_t *tab_swatches_sphere_get(i32 i, swatch_color_t *s) {
	if (i >= _tab_swatches_sphere_count) {
		i32 count             = math_max(i + 1, _tab_swatches_sphere_count * 2);
		_tab_swatches_spheres = realloc(_tab_swatches_spheres, sizeof(tab_swatches_sphere_t) * count);
		memset(_tab_swatches_spheres + _tab_swatches_sphere_count, 0, sizeof(tab_swatches_sphere_t) * (count - _tab_swatches_sphere_count));
		_tab_swatches_sphere_count = count;
	}
	tab_swatches_sphere_t *e = &_tab_swatches_spheres[i];
	if (e->image == NULL || e->base != s->base || e->roughness != s->roughness || e->metallic != s->metallic) {
		if (e->image != NULL) {
			gpu_delete_texture(e->image);
		}
		e->base      = s->base;
		e->roughness = s->roughness;
		e->metallic  = s->metallic;
		e->image     = tab_swatches_make_sphere(s);
	}
	return e->image;
}

void tab_swatches_delete_swatch(swatch_color_t *swatch) {
	i32 i = array_index_of(g_project->swatches, swatch);
	history_delete_swatch(swatch);
	g_context->swatch = g_project->swatches->buffer[i == g_project->swatches->length - 1 ? i - 1 : i + 1];
	array_splice(g_project->swatches, i, 1);
	ui_dock_redraw(TAB_AREA_STATUS);
}

static void tab_swatches_create_material_on_next_frame(void *swatch) {
	tab_materials_accept_swatch_drop(swatch);
}

static void tab_swatches_apply_picked_color_on_next_frame(void *_) {
	tab_materials_apply_picked_color(g_context->picked_color);
}

void tab_swatches_draw_menu() {
	i32 i = _tab_swatches_draw_i;

	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		g_context->swatch = project_clone_swatch(g_context->swatch);
		any_array_push(g_project->swatches, g_context->swatch);
		history_duplicate_swatch();
	}
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	else if (ui_menu_button(tr("Copy Hex Code"), "", ICON_HASH)) {
		i32 color = g_context->swatch->base;
		color     = color_set_ab(color, g_context->swatch->opacity * 255);
		u32 val   = color;
		iron_copy_to_clipboard(i32_to_string(val));
	}
#endif
	else if (g_project->swatches->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_swatches_delete_swatch(g_project->swatches->buffer[i]);
	}
	else if (ui_menu_button(tr("Create Material"), "", ICON_SPHERE)) {
		sys_notify_on_next_frame(&tab_swatches_create_material_on_next_frame, g_project->swatches->buffer[i]);
	}
	else if (ui_menu_button(tr("Create Color Layer"), "", ICON_LAYERS)) {
		i32 color = g_project->swatches->buffer[i]->base;
		color     = color_set_ab(color, g_project->swatches->buffer[i]->opacity * 255);
		layers_create_color_layer(color, g_project->swatches->buffer[i]->occlusion, g_project->swatches->buffer[i]->roughness,
		                          g_project->swatches->buffer[i]->metallic, -1);
	}
}

void tab_swatches_draw_color_picker_callback(swatch_color_t *color) {
	i32 i = _tab_swatches_draw_i;
	if (i >= g_project->swatches->length) {
		return;
	}
	history_edit_swatch(i, g_project->swatches->buffer[i]);
	g_project->swatches->buffer[i] = project_clone_swatch(color);
	g_context->swatch              = g_project->swatches->buffer[i];
}

void tab_swatches_draw_color_picker() {
	g_context->color_picker_previous_tool = g_context->tool;
	context_select_tool(TOOL_TYPE_PICKER);
	g_context->color_picker_callback = &tab_swatches_draw_color_picker_callback;
}

static bool tab_swatches_color_equals(swatch_color_t *a, swatch_color_t *b) {
	return a->base == b->base && a->opacity == b->opacity && a->occlusion == b->occlusion && a->roughness == b->roughness && a->metallic == b->metallic &&
	       a->normal == b->normal && a->emission == b->emission && a->height == b->height && a->subsurface == b->subsurface;
}

static ui_color_state_t tab_swatches_color_state;

void tab_swatches_draw_edit_menu() {
	g_ui->changed = false;
	ui_color_wheel((u32 *)&g_context->swatch->base, &tab_swatches_color_state, false, -1, 11 * g_theme->ELEMENT_H * UI_SCALE(), true,
	               &tab_swatches_draw_color_picker, NULL);

	ui_slider(&g_context->swatch->opacity, "Opacity", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
	g_context->swatch->opacity = math_max(0.0, math_min(1.0, g_context->swatch->opacity));

	if (g_config->tools != TOOLS_SCULPT) {
		ui_slider(&g_context->swatch->occlusion, "Occlusion", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&g_context->swatch->roughness, "Roughness", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&g_context->swatch->metallic, "Metallic", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&g_context->swatch->height, "Height", 0, 1, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}

	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
	if (g_ui->input_released) {
		g_context->swatch         = g_context->swatch; // Trigger material preview update
		g_context->picked_color   = util_clone_swatch_color(g_context->swatch);
		ui_header_handle->redraws = 2;
		sys_notify_on_next_frame(&tab_swatches_apply_picked_color_on_next_frame, NULL);
	}

	i32 index = array_index_of(g_project->swatches, g_context->swatch);
	if (index != -1 && !g_ui->input_down && !g_ui->is_typing && !tab_swatches_color_equals(&tab_swatches_edit_prev, g_context->swatch)) {
		history_edit_swatch(index, &tab_swatches_edit_prev);
		tab_swatches_edit_prev = *g_context->swatch;
	}
}

void tab_swatches_draw_import() {
	if (ui_menu_button(tr("Replace Existing"), "", ICON_NONE)) {
		project_import_swatches(true);
		g_context->swatch = g_project->swatches->buffer[0];
	}
	if (ui_menu_button(tr("Append"), "", ICON_NONE)) {
		project_import_swatches(false);
	}
}

void tab_swatches_draw_edit() {
	if (ui_menu_button(tr("Restore"), "", ICON_REPLAY)) {
		history_replace_swatches(tr("Restore Swatches"));
		project_set_default_swatches();
		g_context->swatch = g_project->swatches->buffer[0];
	}
	if (ui_menu_button(tr("Clear"), "", ICON_ERASE)) {
		history_replace_swatches(tr("Clear Swatches"));
		g_context->swatch   = project_make_swatch(0xffffffff);
		g_project->swatches = any_array_create_from_raw(
		    (void *[]){
		        g_context->swatch,
		    },
		    1);
	}

	ui_menu_separator();
	ui_menu_align();
	ui_menu_label(tr("Display"), NULL);
	ui_menu_align();
	string_array_t *display_items = any_array_create_from_raw(
	    (void *[]){
	        tr("2D"),
	        tr("3D"),
	    },
	    2);
	ui_inline_radio((int *)&_tab_swatches_display, display_items, UI_ALIGN_LEFT);
	if (g_ui->changed) {
		ui_menu_keep_open = true;
	}
}

void tab_swatches_draw(i32 *htab) {
	if (ui_tab(htab, tr("Swatches"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -100,
		        -100,
		        -100,
		        -100,
		    },
		    4);
		ui_row(row);

		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			g_context->swatch = project_make_swatch(0xffffffff);
			any_array_push(g_project->swatches, g_context->swatch);
			history_new_swatch();
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Add new swatch"));
		}

		if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_swatches_draw_import, -1, -1);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Import swatches"));
		}

		if (ui_icon_button(tr("Export"), ICON_EXPORT, UI_ALIGN_CENTER)) {
			project_export_swatches();
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Export swatches"));
		}

		if (ui_icon_button(tr("Edit"), ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_swatches_draw_edit, -1, -1);
		}

		ui_end_sticky();
		ui_separator(3, false);

		i32 size  = 26 * ui_base_asset_scale();
		i32 slotw = math_floor(size * UI_SCALE());
		i32 num   = math_floor(g_ui->_w / (float)(slotw + 3));
		if (num == 0) {
			return;
		}
		bool drag_pos_set = false;

		f32 uix = 0.0;
		f32 uiy = 0.0;
		for (i32 row = 0; row < math_floor(math_ceil(g_project->swatches->length / (float)num)); ++row) {
			f32_array_t *ar = f32_array_create_from_raw((f32[]){}, 0);
			for (i32 i = 0; i < num; ++i) {
				f32_array_push(ar, 1 / (float)num);
			}
			ui_row(ar);

			g_ui->_x += 2;
			if (row > 0) {
				g_ui->_y += 6;
			}

			for (i32 j = 0; j < num; ++j) {
				i32 i = j + row * num;
				if (i >= g_project->swatches->length) {
					ui_end_element_of_size(slotw);
					continue;
				}

				if (g_context->swatch == g_project->swatches->buffer[i]) {
					i32 w = size + 6;
					draw_set_color(g_theme->HIGHLIGHT_COL);
					ui_draw_rect(true, false, g_ui->_x - 2 * UI_SCALE(), g_ui->_y - 2 * UI_SCALE() - 1, w * UI_SCALE(), w * UI_SCALE());
					draw_set_color(0xffffffff);
				}

				uix = g_ui->_x;
				uiy = g_ui->_y;

				// Draw the drag position indicator
				if (base_drag_swatch != NULL && tab_swatches_drag_pos == i) {
					ui_fill(-1, -2, 2, size + 6, g_theme->HIGHLIGHT_COL);
				}

				swatch_color_t *swatch = g_project->swatches->buffer[i];
				ui_state_t      state  = _tab_swatches_display == SWATCH_DISPLAY_3D ? ui_image(tab_swatches_sphere_get(i, swatch), 0xffffffff, slotw)
				                                                                    : ui_image(tab_swatches_empty_get(), swatch->base, slotw);

				if (state == UI_STATE_STARTED) {
					g_context->swatch         = g_project->swatches->buffer[i];
					base_drag_off_x           = -(mouse_x - uix - g_ui->_window_x);
					base_drag_off_y           = -(mouse_y - uiy - g_ui->_window_y + 1);
					base_drag_swatch          = g_context->swatch;
					g_context->picked_color   = util_clone_swatch_color(g_context->swatch);
					ui_header_handle->redraws = 2;
					sys_notify_on_next_frame(&tab_swatches_apply_picked_color_on_next_frame, NULL);
				}
				else if (state == UI_STATE_HOVERED) {
					tab_swatches_drag_pos = (mouse_x > uix + g_ui->_window_x + slotw / 2.0)
					                            ? i + 1
					                            : i; // Switch to the next position if the mouse crosses the swatch rectangle center
					drag_pos_set          = true;
				}
				else if (state == UI_STATE_RELEASED) {
					if (sys_time() - g_context->select_time < 0.2) {
						_tab_swatches_draw_i   = i;
						tab_swatches_edit_prev = *g_project->swatches->buffer[i];
						ui_menu_draw(&tab_swatches_draw_edit_menu, -1, -1);
					}

					g_context->select_time = sys_time();
				}
				if (g_ui->is_hovered && g_ui->input_released_r) {
					g_context->swatch = g_project->swatches->buffer[i];

					_tab_swatches_draw_i = i;

					ui_menu_draw(&tab_swatches_draw_menu, -1, -1);
				}
				if (g_ui->is_hovered) {
					i32 color = g_project->swatches->buffer[i]->base;
					color     = color_set_ab(color, g_project->swatches->buffer[i]->opacity * 255);
					u32 val   = color;
					ui_tooltip(string_tmp("#%s", i32_to_string_hex(val)));
				}
			}
		}

		// Draw the rightmost line next to the last swatch
		if (base_drag_swatch != NULL && tab_swatches_drag_pos == g_project->swatches->length) {
			g_ui->_x = uix; // Reset the position because otherwise it would start in the row below
			g_ui->_y = uiy;
			ui_fill(size + 2, -2, 2, size + 6, g_theme->HIGHLIGHT_COL);
		}

		// Currently there is no valid drag_position so reset it
		if (!drag_pos_set) {
			tab_swatches_drag_pos = -1;
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && g_ui->is_delete_down && g_project->swatches->length > 1) {
			g_ui->is_delete_down = false;
			tab_swatches_delete_swatch(g_context->swatch);
		}
		if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
			g_context->swatch = project_clone_swatch(g_context->swatch);
			any_array_push(g_project->swatches, g_context->swatch);
		}
		if (in_focus) {
			i32 i = array_index_of(g_project->swatches, g_context->swatch);
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
				if (i > 0) {
					g_context->swatch = g_project->swatches->buffer[i - 1];
				}
			}
			if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
				if (i < g_project->swatches->length - 1) {
					g_context->swatch = g_project->swatches->buffer[i + 1];
				}
			}
		}
	}
}

void tab_swatches_accept_swatch_drop(swatch_color_t *swatch) {
	// No valid position available
	if (tab_swatches_drag_pos == -1) {
		return;
	}

	i32 swatch_pos = array_index_of(g_project->swatches, swatch);
	// A new swatch from color picker
	if (swatch_pos == -1) {
		array_insert(g_project->swatches, tab_swatches_drag_pos, swatch);
	}
	else if (math_abs(swatch_pos - tab_swatches_drag_pos) > 0) { // Existing swatch is reordered
		array_remove(g_project->swatches, swatch);
		// If the new position is after the old one, decrease by one because the swatch has been deleted
		i32 new_pos = tab_swatches_drag_pos - swatch_pos > 0 ? tab_swatches_drag_pos - 1 : tab_swatches_drag_pos;
		array_insert(g_project->swatches, new_pos, swatch);
	}
}
