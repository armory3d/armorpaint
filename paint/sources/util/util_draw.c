#include "../global.h"

void util_draw_dashed_rect(f32 x, f32 y, f32 w, f32 h) {
	f32 dash = 4.0 * UI_SCALE();
	for (f32 t = 0.0; t < w; t += dash * 2.0) {
		f32 l = math_min(dash, w - t);
		draw_filled_rect(x + t, y, l, 1.0);
		draw_filled_rect(x + t, y + h, l, 1.0);
	}
	for (f32 t = 0.0; t < h; t += dash * 2.0) {
		f32 l = math_min(dash, h - t);
		draw_filled_rect(x, y + t, 1.0, l);
		draw_filled_rect(x + w, y + t, 1.0, l);
	}
}

void util_draw_dashed_lines(f32_array_t *pts, bool closed) {
	f32 dash = 4.0 * UI_SCALE();
	f32 t    = 0.0;
	i32 n    = pts->length / 2;
	i32 segs = closed ? n : n - 1;
	for (i32 i = 0; i < segs; ++i) {
		i32 j   = (i + 1) % n;
		f32 x0  = pts->buffer[i * 2];
		f32 y0  = pts->buffer[i * 2 + 1];
		f32 dx  = pts->buffer[j * 2] - x0;
		f32 dy  = pts->buffer[j * 2 + 1] - y0;
		f32 len = math_sqrt(dx * dx + dy * dy);
		f32 d   = 0.0;
		while (d < len) {
			f32  phase = math_fmod(t, dash * 2.0);
			bool on    = phase < dash;
			f32  step  = math_min(on ? dash - phase : dash * 2.0 - phase, len - d);
			if (on) {
				draw_line(x0 + dx * d / len, y0 + dy * d / len, x0 + dx * (d + step) / len, y0 + dy * (d + step) / len, 1.0);
			}
			d += step;
			t += step;
		}
	}
}

void util_draw_select_shape(f32 x, f32 y, f32 w, f32 h) {
	if (g_context->select_mode == SELECT_MODE_RECTANGLE) {
		util_draw_dashed_rect(x, y, w, h);
		return;
	}
	f32_array_t *pts = f32_array_create(0);
	if (g_context->select_mode == SELECT_MODE_ELLIPSE) {
		i32 segs = 64;
		for (i32 i = 0; i < segs; ++i) {
			f32 a = i / (float)segs * math_pi() * 2.0;
			f32_array_push(pts, x + w * 0.5 + math_cos(a) * w * 0.5);
			f32_array_push(pts, y + h * 0.5 + math_sin(a) * h * 0.5);
		}
		util_draw_dashed_lines(pts, true);
	}
	else if (g_context->select_points != NULL) {
		f32_array_t *src = g_context->select_points;
		for (i32 i = 0; i < src->length; i += 2) {
			f32_array_push(pts, x + src->buffer[i] * w);
			f32_array_push(pts, y + src->buffer[i + 1] * h);
		}
		if (g_context->select_dragging) {
			f32_array_push(pts, x + g_context->select_cursor_x * w);
			f32_array_push(pts, y + g_context->select_cursor_y * h);
		}
		if (pts->length >= 4) {
			util_draw_dashed_lines(pts, !g_context->select_dragging);
		}
	}
	array_free(pts);
	free(pts);
}
