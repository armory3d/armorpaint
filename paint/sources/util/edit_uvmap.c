
#include "../global.h"

static i32_array_t *_selected_verts = NULL;
static bool         _is_dragging    = false;
static bool         _is_box_sel     = false;
static f32          _box_x0;
static f32          _box_y0;
static f32          _box_x1;
static f32          _box_y1;

static bool _is_selected(i32 idx) {
	for (i32 i = 0; i < _selected_verts->length; i++) {
		if (_selected_verts->buffer[i] == idx) {
			return true;
		}
	}
	return false;
}

static void _get_tex_transform(f32 *out_tx, f32 *out_ty, f32 *out_tw, f32 *out_th) {
	i32 headerh = g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1 ? ui_header_h * 2 : ui_header_h;
	i32 apph    = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] + headerh;
	if (!base_view3d_show) {
		apph = base_h();
	}
	f32 wm = fmin(ui_view2d_ww, ui_view2d_wh);
	f32 tw = wm * 0.9f * ui_view2d_pan_scale;
	f32 th = tw;
	if (util_uv_uvmap != NULL) {
		th = tw * (util_uv_uvmap->height / (f32)util_uv_uvmap->width);
	}
	*out_tw = tw;
	*out_th = th;
	*out_tx = ui_view2d_ww / 2.0f - tw / 2.0f + ui_view2d_pan_x;
	*out_ty = apph / 2.0f - th / 2.0f + ui_view2d_pan_y;
}

void edit_uvmap_update() {
	if (g_context->paint_object == NULL) {
		return;
	}

	if (_selected_verts == NULL) {
		_selected_verts = i32_array_create_from_raw((i32[]){0}, 1);
		gc_root(_selected_verts);
		_selected_verts->length = 0;
	}

	mesh_data_t *mesh      = g_context->paint_object->data;
	i16_array_t *texa      = mesh->vertex_arrays->buffer[2]->values;
	i32          num_verts = texa->length / 2;

	f32 tx, ty, tw, th;
	_get_tex_transform(&tx, &ty, &tw, &th);

	f32  mx      = mouse_x - ui_view2d_wx;
	f32  my      = mouse_y - ui_view2d_wy;
	bool in_view = mx >= 0 && mx < ui_view2d_ww && my >= 0 && my < ui_view2d_wh;

	if (in_view && mouse_started("left")) {
		f32 min_dist_sq = 20.0f * 20.0f;
		i32 closest     = -1;
		for (i32 i = 0; i < num_verts; i++) {
			f32 vx      = tx + (f32)texa->buffer[i * 2] / 32767.0f * tw;
			f32 vy      = ty + (f32)texa->buffer[i * 2 + 1] / 32767.0f * th;
			f32 dx      = mx - vx;
			f32 dy      = my - vy;
			f32 dist_sq = dx * dx + dy * dy;
			if (dist_sq < min_dist_sq) {
				min_dist_sq = dist_sq;
				closest     = i;
			}
		}
		if (closest >= 0) {
			if (!_is_selected(closest)) {
				_selected_verts->length = 0;
				i32_array_push(_selected_verts, closest);
			}
			_is_dragging = true;
		}
		else {
			_selected_verts->length = 0;
			_is_box_sel             = true;
			_box_x0 = _box_x1 = mx;
			_box_y0 = _box_y1 = my;
		}
	}

	if (mouse_started("right")) {
		_selected_verts->length = 0;
		_is_dragging            = false;
		_is_box_sel             = false;
		ui_view2d_hwnd->redraws = 2;
	}

	if (_is_dragging && mouse_down("left")) {
		f32 duv_x = g_ui->input_dx / tw * 32767.0f;
		f32 duv_y = g_ui->input_dy / th * 32767.0f;
		for (i32 i = 0; i < _selected_verts->length; i++) {
			i32 vi                   = _selected_verts->buffer[i];
			i32 new_x                = (i32)texa->buffer[vi * 2] + (i32)duv_x;
			i32 new_y                = (i32)texa->buffer[vi * 2 + 1] + (i32)duv_y;
			texa->buffer[vi * 2]     = (i16)math_min(math_max(new_x, -32767), 32767);
			texa->buffer[vi * 2 + 1] = (i16)math_min(math_max(new_y, -32767), 32767);
		}
		ui_view2d_hwnd->redraws = 2;
	}

	if (_is_box_sel && mouse_down("left")) {
		_box_x1                 = mx;
		_box_y1                 = my;
		ui_view2d_hwnd->redraws = 2;
	}

	if (mouse_released("left")) {
		if (_is_dragging) {
			_is_dragging     = false;
			mesh_object_t *o = g_context->paint_object;
			mesh_data_build_vertices(o->data->_->vertex_buffer, o->data->vertex_arrays);

			if (g_context->merged_object != NULL) {
				util_mesh_merge(util_mesh_get_visible());
			}
			util_uv_uvmap_cached       = false;
			util_uv_trianglemap_cached = false;
			util_uv_dilatemap_cached   = false;
			ui_view2d_hwnd->redraws    = 2;
		}
		if (_is_box_sel) {
			_is_box_sel             = false;
			f32 bx0                 = math_min(_box_x0, _box_x1);
			f32 bx1                 = math_max(_box_x0, _box_x1);
			f32 by0                 = math_min(_box_y0, _box_y1);
			f32 by1                 = math_max(_box_y0, _box_y1);
			_selected_verts->length = 0;
			for (i32 i = 0; i < num_verts; i++) {
				f32 vx = tx + (f32)texa->buffer[i * 2] / 32767.0f * tw;
				f32 vy = ty + (f32)texa->buffer[i * 2 + 1] / 32767.0f * th;
				if (vx >= bx0 && vx <= bx1 && vy >= by0 && vy <= by1) {
					i32_array_push(_selected_verts, i);
				}
			}
			ui_view2d_hwnd->redraws = 2;
		}
	}

	// All vertices - black squares
	f32 vs = 3.0f * UI_SCALE();
	draw_set_color(0xff000000);
	for (i32 i = 0; i < num_verts; i++) {
		f32 vx = tx + (f32)texa->buffer[i * 2] / 32767.0f * tw;
		f32 vy = ty + (f32)texa->buffer[i * 2 + 1] / 32767.0f * th;
		draw_filled_rect(vx - vs, vy - vs, vs * 2.0f, vs * 2.0f);
	}

	// Selected vertices - white squares
	f32 s = 5.0f * UI_SCALE();
	draw_set_color(0xffffffff);
	for (i32 i = 0; i < _selected_verts->length; i++) {
		i32 vi = _selected_verts->buffer[i];
		f32 vx = tx + (f32)texa->buffer[vi * 2] / 32767.0f * tw;
		f32 vy = ty + (f32)texa->buffer[vi * 2 + 1] / 32767.0f * th;
		draw_filled_rect(vx - s, vy - s, s * 2.0f, s * 2.0f);
	}

	// Box select rectangle
	if (_is_box_sel) {
		f32 bx = math_min(_box_x0, _box_x1);
		f32 by = math_min(_box_y0, _box_y1);
		f32 bw = math_abs(_box_x1 - _box_x0);
		f32 bh = math_abs(_box_y1 - _box_y0);
		draw_set_color(0xffffffff);
		draw_line_aa(bx, by, bx + bw, by, 1.0f);
		draw_line_aa(bx + bw, by, bx + bw, by + bh, 1.0f);
		draw_line_aa(bx + bw, by + bh, bx, by + bh, 1.0f);
		draw_line_aa(bx, by + bh, bx, by, 1.0f);
	}

	draw_set_color(0xffffffff);
}
