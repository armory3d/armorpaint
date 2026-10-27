
#include "../global.h"
#include <stb_truetype.h>

#define TEXT_TOOL_CURVE_STEPS 6
#define TEXT_TOOL_SMOOTH_DOT  0.8 // Outline corners bending less than this are shaded smooth
#define TEXT_TOOL_EPS         0.0000001
#define TEXT_TOOL_MIN_DEPTH   0.001

typedef struct text_pt {
	f32 x;
	f32 y;
} text_pt_t;

typedef struct text_contour {
	i32 start;
	i32 count;
	i32 depth;  // Number of contours around this one, odd for holes
	i32 parent; // Outer contour of a hole
	f32 area;   // Positive when counter-clockwise
} text_contour_t;

// Contours of the glyph being built
static text_pt_t      *_text_pts            = NULL;
static i32             _text_pts_count      = 0;
static i32             _text_pts_cap        = 0;
static text_contour_t *_text_contours       = NULL;
static i32             _text_contours_count = 0;
static i32             _text_contours_cap   = 0;
// Mesh being built, 8 floats per vertex - pos, nor, tex
static f32 *_text_verts       = NULL;
static i32  _text_verts_count = 0;
static i32  _text_verts_cap   = 0;
static u32 *_text_inds        = NULL;
static i32  _text_inds_count  = 0;
static i32  _text_inds_cap    = 0;
static f32  _text_half_depth;
static f32  _text_uv_w;
static f32  _text_uv_h;
static f32  _text_uv_x;

static f32 _text_cross(text_pt_t a, text_pt_t b, text_pt_t c) {
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool _text_same(text_pt_t a, text_pt_t b) {
	return math_abs(a.x - b.x) < TEXT_TOOL_EPS && math_abs(a.y - b.y) < TEXT_TOOL_EPS;
}

static void _text_contour_begin() {
	if (_text_contours_count == _text_contours_cap) {
		_text_contours_cap = _text_contours_cap == 0 ? 16 : _text_contours_cap * 2;
		_text_contours     = realloc(_text_contours, _text_contours_cap * sizeof(text_contour_t));
	}
	_text_contours[_text_contours_count++] = (text_contour_t){.start = _text_pts_count, .count = 0, .depth = 0, .parent = -1, .area = 0.0};
}

static void _text_contour_point(f32 x, f32 y) {
	text_contour_t *c = &_text_contours[_text_contours_count - 1];
	text_pt_t       p = {x, y};
	if (c->count > 0 && _text_same(_text_pts[_text_pts_count - 1], p)) {
		return;
	}
	if (_text_pts_count == _text_pts_cap) {
		_text_pts_cap = _text_pts_cap == 0 ? 256 : _text_pts_cap * 2;
		_text_pts     = realloc(_text_pts, _text_pts_cap * sizeof(text_pt_t));
	}
	_text_pts[_text_pts_count++] = p;
	c->count++;
}

// Drops the closing point and contours with no area
static void _text_contour_end() {
	if (_text_contours_count == 0) {
		return;
	}
	text_contour_t *c = &_text_contours[_text_contours_count - 1];
	if (c->count > 1 && _text_same(_text_pts[c->start], _text_pts[c->start + c->count - 1])) {
		c->count--;
	}
	text_pt_t *p = &_text_pts[c->start];
	for (i32 i = 0; i < c->count; ++i) {
		text_pt_t a = p[i];
		text_pt_t b = p[(i + 1) % c->count];
		c->area += (a.x * b.y - b.x * a.y) * 0.5;
	}
	if (c->count < 3 || math_abs(c->area) < TEXT_TOOL_EPS) {
		_text_contours_count--;
		_text_pts_count = c->start;
	}
	else {
		_text_pts_count = c->start + c->count;
	}
}

static bool _text_contour_contains(text_contour_t *c, text_pt_t p) {
	text_pt_t *pts    = &_text_pts[c->start];
	bool       inside = false;
	for (i32 i = 0, j = c->count - 1; i < c->count; j = i++) {
		if ((pts[i].y > p.y) != (pts[j].y > p.y) && p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x) {
			inside = !inside;
		}
	}
	return inside;
}

// Fills the contours of a glyph, x and y are in font units scaled to the text size
static void _text_glyph_contours(stbtt_fontinfo *info, i32 glyph, f32 scale, f32 pen_x) {
	_text_pts_count      = 0;
	_text_contours_count = 0;
	stbtt_vertex *verts;
	i32           num_verts = stbtt_GetGlyphShape(info, glyph, &verts);
	f32           px        = 0.0;
	f32           py        = 0.0;
	for (i32 i = 0; i < num_verts; ++i) {
		stbtt_vertex *v = &verts[i];
		f32           x = pen_x + v->x * scale;
		f32           y = v->y * scale;
		if (v->type == STBTT_vmove) {
			_text_contour_end();
			_text_contour_begin();
			_text_contour_point(x, y);
		}
		else if (v->type == STBTT_vline) {
			_text_contour_point(x, y);
		}
		else {
			f32 cx  = pen_x + v->cx * scale;
			f32 cy  = v->cy * scale;
			f32 cx1 = pen_x + v->cx1 * scale;
			f32 cy1 = v->cy1 * scale;
			for (i32 k = 1; k <= TEXT_TOOL_CURVE_STEPS; ++k) {
				f32 t = k / (f32)TEXT_TOOL_CURVE_STEPS;
				f32 u = 1.0 - t;
				if (v->type == STBTT_vcurve) {
					_text_contour_point(u * u * px + 2.0 * u * t * cx + t * t * x, u * u * py + 2.0 * u * t * cy + t * t * y);
				}
				else {
					_text_contour_point(u * u * u * px + 3.0 * u * u * t * cx + 3.0 * u * t * t * cx1 + t * t * t * x,
					                    u * u * u * py + 3.0 * u * u * t * cy + 3.0 * u * t * t * cy1 + t * t * t * y);
				}
			}
		}
		px = x;
		py = y;
	}
	_text_contour_end();
	stbtt_FreeShape(info, verts);

	// Nesting decides what is a hole, the winding differs between font formats
	for (i32 i = 0; i < _text_contours_count; ++i) {
		for (i32 j = 0; j < _text_contours_count; ++j) {
			if (i != j && _text_contour_contains(&_text_contours[j], _text_pts[_text_contours[i].start])) {
				_text_contours[i].depth++;
			}
		}
	}
	for (i32 i = 0; i < _text_contours_count; ++i) {
		text_contour_t *c = &_text_contours[i];
		if (c->depth % 2 == 0) {
			continue;
		}
		for (i32 j = 0; j < _text_contours_count; ++j) {
			text_contour_t *o = &_text_contours[j];
			if (i != j && o->depth == c->depth - 1 && _text_contour_contains(o, _text_pts[c->start])) {
				c->parent = j;
				break;
			}
		}
	}
}

static i32 _text_vertex(f32 x, f32 y, f32 z, f32 nx, f32 ny, f32 nz, text_pt_t p) {
	if (_text_verts_count == _text_verts_cap) {
		_text_verts_cap = _text_verts_cap == 0 ? 1024 : _text_verts_cap * 2;
		_text_verts     = realloc(_text_verts, _text_verts_cap * 8 * sizeof(f32));
	}
	f32 *v = &_text_verts[_text_verts_count * 8];
	v[0]   = x;
	v[1]   = y;
	v[2]   = z;
	v[3]   = nx;
	v[4]   = ny;
	v[5]   = nz;
	v[6]   = (p.x - _text_uv_x) / _text_uv_w;
	v[7]   = 1.0 - p.y / _text_uv_h;
	return _text_verts_count++;
}

static void _text_triangle(i32 a, i32 b, i32 c) {
	if (_text_inds_count + 3 > _text_inds_cap) {
		_text_inds_cap = _text_inds_cap == 0 ? 4096 : _text_inds_cap * 2;
		_text_inds     = realloc(_text_inds, _text_inds_cap * sizeof(u32));
	}
	_text_inds[_text_inds_count++] = a;
	_text_inds[_text_inds_count++] = b;
	_text_inds[_text_inds_count++] = c;
}

// Copies a contour, counter-clockwise for outer contours and clockwise for holes
static void _text_contour_copy(text_contour_t *c, text_pt_t *dst) {
	bool reverse = (c->area < 0.0) == (c->depth % 2 == 0);
	for (i32 i = 0; i < c->count; ++i) {
		dst[i] = _text_pts[c->start + (reverse ? c->count - 1 - i : i)];
	}
}

static bool _text_segments_cross(text_pt_t a, text_pt_t b, text_pt_t c, text_pt_t d) {
	if (_text_same(a, c) || _text_same(a, d) || _text_same(b, c) || _text_same(b, d)) {
		return false;
	}
	f32 d0 = _text_cross(a, b, c);
	f32 d1 = _text_cross(a, b, d);
	f32 d2 = _text_cross(c, d, a);
	f32 d3 = _text_cross(c, d, b);
	return ((d0 > 0.0) != (d1 > 0.0)) && ((d2 > 0.0) != (d3 > 0.0));
}

// Whether the line from the polygon vertex i to m stays clear of the polygon and of the holes of the outer contour
static bool _text_bridge_valid(text_pt_t *poly, i32 count, i32 i, text_pt_t m, i32 outer) {
	text_pt_t p    = poly[i];
	text_pt_t prev = poly[(i + count - 1) % count];
	text_pt_t next = poly[(i + 1) % count];
	// m has to be on the inner side of the corner
	bool left0 = _text_cross(prev, p, m) > 0.0;
	bool left1 = _text_cross(p, next, m) > 0.0;
	if (_text_cross(prev, p, next) > 0.0 ? !(left0 && left1) : !(left0 || left1)) {
		return false;
	}
	for (i32 j = 0; j < count; ++j) {
		if (_text_segments_cross(p, m, poly[j], poly[(j + 1) % count])) {
			return false;
		}
	}
	for (i32 h = 0; h < _text_contours_count; ++h) {
		text_contour_t *c = &_text_contours[h];
		if (c->parent != outer) {
			continue;
		}
		for (i32 j = 0; j < c->count; ++j) {
			if (_text_segments_cross(p, m, _text_pts[c->start + j], _text_pts[c->start + (j + 1) % c->count])) {
				return false;
			}
		}
	}
	return true;
}

// Front and back faces of an outer contour with its holes
static void _text_fill(i32 outer) {
	text_contour_t *oc    = &_text_contours[outer];
	i32             total = oc->count;
	i32             holes = 0;
	for (i32 h = 0; h < _text_contours_count; ++h) {
		if (_text_contours[h].parent == outer) {
			total += _text_contours[h].count + 2;
			holes++;
		}
	}
	text_pt_t *poly  = malloc(total * sizeof(text_pt_t));
	text_pt_t *hole  = malloc(total * sizeof(text_pt_t));
	i32        count = oc->count;
	_text_contour_copy(oc, poly);

	// Bridge the holes into the polygon, the rightmost one first
	for (i32 n = 0; n < holes; ++n) {
		i32 best   = -1;
		f32 best_x = 0.0;
		for (i32 h = 0; h < _text_contours_count; ++h) {
			text_contour_t *c = &_text_contours[h];
			if (c->parent != outer || c->depth < 0) {
				continue;
			}
			for (i32 j = 0; j < c->count; ++j) {
				if (best == -1 || _text_pts[c->start + j].x > best_x) {
					best   = h;
					best_x = _text_pts[c->start + j].x;
				}
			}
		}
		text_contour_t *c = &_text_contours[best];
		_text_contour_copy(c, hole);
		c->depth = -1; // Merged
		i32 m    = 0;
		for (i32 j = 1; j < c->count; ++j) {
			if (hole[j].x > hole[m].x) {
				m = j;
			}
		}

		i32 bridge       = -1;
		i32 nearest      = 0;
		f32 bridge_dist  = 0.0;
		f32 nearest_dist = -1.0;
		for (i32 i = 0; i < count; ++i) {
			f32 dx   = poly[i].x - hole[m].x;
			f32 dy   = poly[i].y - hole[m].y;
			f32 dist = dx * dx + dy * dy;
			if (nearest_dist < 0.0 || dist < nearest_dist) {
				nearest      = i;
				nearest_dist = dist;
			}
			if ((bridge == -1 || dist < bridge_dist) && _text_bridge_valid(poly, count, i, hole[m], outer)) {
				bridge      = i;
				bridge_dist = dist;
			}
		}
		if (bridge == -1) {
			bridge = nearest;
		}

		// poly[..bridge], hole[m..], hole[m], poly[bridge..]
		i32 add = c->count + 2;
		for (i32 i = count - 1; i >= bridge; --i) {
			poly[i + add] = poly[i];
		}
		for (i32 j = 0; j <= c->count; ++j) {
			poly[bridge + 1 + j] = hole[(m + j) % c->count];
		}
		count += add;
	}
	free(hole);

	i32 front = _text_verts_count;
	for (i32 i = 0; i < count; ++i) {
		_text_vertex(poly[i].x, -_text_half_depth, poly[i].y, 0.0, -1.0, 0.0, poly[i]);
	}
	i32 back = _text_verts_count;
	for (i32 i = 0; i < count; ++i) {
		_text_vertex(poly[i].x, _text_half_depth, poly[i].y, 0.0, 1.0, 0.0, poly[i]);
	}

	// Ear clipping
	i32 *prev = malloc(count * sizeof(i32));
	i32 *next = malloc(count * sizeof(i32));
	for (i32 i = 0; i < count; ++i) {
		prev[i] = (i + count - 1) % count;
		next[i] = (i + 1) % count;
	}
	i32 remaining = count;
	i32 fails     = 0;
	i32 b         = 0;
	while (remaining > 3) {
		i32  a      = prev[b];
		i32  c      = next[b];
		bool convex = _text_cross(poly[a], poly[b], poly[c]) > TEXT_TOOL_EPS * TEXT_TOOL_EPS;
		bool ear    = convex;
		for (i32 v = next[c]; ear && v != a; v = next[v]) {
			text_pt_t p = poly[v];
			if (_text_same(p, poly[a]) || _text_same(p, poly[b]) || _text_same(p, poly[c])) {
				continue;
			}
			if (_text_cross(poly[a], poly[b], p) >= 0.0 && _text_cross(poly[b], poly[c], p) >= 0.0 && _text_cross(poly[c], poly[a], p) >= 0.0) {
				ear = false;
			}
		}
		// Nothing left to clip cleanly, drop the corner to keep going
		if (ear || fails >= remaining) {
			if (convex) {
				_text_triangle(front + a, front + b, front + c);
				_text_triangle(back + a, back + c, back + b);
			}
			next[a] = c;
			prev[c] = a;
			remaining--;
			fails = 0;
			b     = a;
		}
		else {
			fails++;
			b = c;
		}
	}
	if (_text_cross(poly[prev[b]], poly[b], poly[next[b]]) > 0.0) {
		_text_triangle(front + prev[b], front + b, front + next[b]);
		_text_triangle(back + prev[b], back + next[b], back + b);
	}
	free(prev);
	free(next);
	free(poly);
}

static text_pt_t _text_edge_normal(text_pt_t a, text_pt_t b) {
	f32 dx  = b.x - a.x;
	f32 dy  = b.y - a.y;
	f32 len = math_sqrt(dx * dx + dy * dy);
	return (text_pt_t){dy / len, -dx / len};
}

static text_pt_t _text_corner_normal(text_pt_t n, text_pt_t neighbor) {
	if (n.x * neighbor.x + n.y * neighbor.y < TEXT_TOOL_SMOOTH_DOT) {
		return n;
	}
	f32 x   = n.x + neighbor.x;
	f32 y   = n.y + neighbor.y;
	f32 len = math_sqrt(x * x + y * y);
	return (text_pt_t){x / len, y / len};
}

// Walls joining the front and back faces along a contour
static void _text_sides(text_contour_t *c) {
	text_pt_t *p = malloc(c->count * sizeof(text_pt_t));
	_text_contour_copy(c, p);
	i32 n = c->count;
	for (i32 i = 0; i < n; ++i) {
		text_pt_t a   = p[i];
		text_pt_t b   = p[(i + 1) % n];
		text_pt_t nor = _text_edge_normal(a, b);
		text_pt_t na  = _text_corner_normal(nor, _text_edge_normal(p[(i + n - 1) % n], a));
		text_pt_t nb  = _text_corner_normal(nor, _text_edge_normal(b, p[(i + 2) % n]));
		i32       af  = _text_vertex(a.x, -_text_half_depth, a.y, na.x, 0.0, na.y, a);
		i32       ab  = _text_vertex(a.x, _text_half_depth, a.y, na.x, 0.0, na.y, a);
		i32       bf  = _text_vertex(b.x, -_text_half_depth, b.y, nb.x, 0.0, nb.y, b);
		i32       bb  = _text_vertex(b.x, _text_half_depth, b.y, nb.x, 0.0, nb.y, b);
		_text_triangle(af, bb, bf);
		_text_triangle(af, ab, bb);
	}
	free(p);
}

static bool _text_font_info(stbtt_fontinfo *info) {
	draw_font_t *font = g_context->font->font;
	draw_font_init(font);
	return stbtt_InitFont(info, font->blob, font->offset) != 0;
}

// Scale from font units and the width of the text, false when there is nothing to place
static bool _text_measure(stbtt_fontinfo *info, f32 *scale, f32 *width) {
	char *text = g_context->text_tool_text;
	if (text == NULL || text[0] == '\0' || !_text_font_info(info)) {
		return false;
	}
	*scale   = stbtt_ScaleForPixelHeight(info, math_max(util_text_tool_size, TEXT_TOOL_MIN_DEPTH));
	i32 pen  = 0;
	i32 last = 0;
	for (i32 i = 0; text[i] != '\0';) {
		i32 glyph = stbtt_FindGlyphIndex(info, string_utf8_decode(text + i, &i));
		i32 advance;
		i32 lsb;
		stbtt_GetGlyphHMetrics(info, glyph, &advance, &lsb);
		pen += advance + (last != 0 ? stbtt_GetGlyphKernAdvance(info, last, glyph) : 0);
		last = glyph;
	}
	*width = pen * *scale;
	return *width > 0.0;
}

// Origin is at the bottom center
static mesh_geo_t *_text_make_geo() {
	stbtt_fontinfo info;
	f32            scale;
	f32            width;
	if (!_text_measure(&info, &scale, &width)) {
		return NULL;
	}
	i32 ascent;
	i32 descent;
	i32 line_gap;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);

	_text_verts_count = 0;
	_text_inds_count  = 0;
	_text_half_depth  = math_max(util_text_tool_depth, TEXT_TOOL_MIN_DEPTH) / 2.0;
	_text_uv_x        = -width / 2.0;
	_text_uv_w        = width;
	_text_uv_h        = (ascent - descent) * scale;

	char *text = g_context->text_tool_text;
	i32   pen  = 0;
	i32   last = 0;
	for (i32 i = 0; text[i] != '\0';) {
		i32 glyph = stbtt_FindGlyphIndex(&info, string_utf8_decode(text + i, &i));
		i32 advance;
		i32 lsb;
		stbtt_GetGlyphHMetrics(&info, glyph, &advance, &lsb);
		pen += last != 0 ? stbtt_GetGlyphKernAdvance(&info, last, glyph) : 0;
		_text_glyph_contours(&info, glyph, scale, pen * scale - width / 2.0);
		// Lift the descenders above the ground
		for (i32 j = 0; j < _text_pts_count; ++j) {
			_text_pts[j].y -= descent * scale;
		}
		for (i32 j = 0; j < _text_contours_count; ++j) {
			_text_sides(&_text_contours[j]);
		}
		for (i32 j = 0; j < _text_contours_count; ++j) {
			if (_text_contours[j].depth % 2 == 0) {
				_text_fill(j);
			}
		}
		pen += advance;
		last = glyph;
	}
	if (_text_inds_count == 0) {
		return NULL;
	}

	mesh_geo_t *g = util_geo_create(_text_verts_count, _text_inds_count);
	for (i32 i = 0; i < _text_verts_count; ++i) {
		f32 *v = &_text_verts[i * 8];
		memcpy(&g->pos[i * 3], v, 3 * sizeof(f32));
		memcpy(&g->nor[i * 3], v + 3, 3 * sizeof(f32));
		memcpy(&g->tex[i * 2], v + 6, 2 * sizeof(f32));
	}
	memcpy(g->ind, _text_inds, _text_inds_count * sizeof(u32));
	g->vertex_count = _text_verts_count;
	g->index_count  = _text_inds_count;
	return g;
}

static void _text_commit(vec4_t p) {
	mesh_geo_t *g = _text_make_geo();
	if (g == NULL) {
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	mesh_object_t *mo = tab_meshes_append_data(nodes_mesh_geo_to_data(g, "Text"));
	util_geo_free(g);
	if (in_use) {
		draw_begin(current, false, 0);
	}

	mo->base->transform->loc = p;
	transform_build_matrix(mo->base->transform);
	util_mesh_transform_changed();
	tab_meshes_reset_preview_map();

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	ui_header_handle->redraws = 2;
	g_context->ddirty         = 2;
}

void util_text_tool_update() {
	vec4_t p;
	if (g_context->tool != TOOL_TYPE_TEXT_MESH || !util_tool_input_enabled() || !mouse_started("left") || !util_tool_ground_point(&p)) {
		return;
	}
	_text_commit(p);
}

// Baseline of the text under the mouse, called while drawing the cursor
void util_text_tool_render() {
	vec4_t         p;
	stbtt_fontinfo info;
	f32            scale;
	f32            width;
	if (g_context->tool != TOOL_TYPE_TEXT_MESH || !util_tool_input_enabled() || !util_tool_ground_point(&p) || !_text_measure(&info, &scale, &width)) {
		return;
	}

	f32    h = math_max(util_text_tool_size, TEXT_TOOL_MIN_DEPTH);
	vec4_t c[4];
	c[0] = (vec4_t){p.x - width / 2.0, p.y, 0.0, 1.0};
	c[1] = (vec4_t){p.x + width / 2.0, p.y, 0.0, 1.0};
	c[2] = (vec4_t){p.x + width / 2.0, p.y, h, 1.0};
	c[3] = (vec4_t){p.x - width / 2.0, p.y, h, 1.0};
	draw_scissor(base_x(), base_y(), base_w(), base_h());
	draw_set_color(0xffffffff);
	for (i32 i = 0; i < 4; ++i) {
		util_tool_draw_line(c[i], c[(i + 1) % 4]);
	}
	draw_flush();
	gpu_disable_scissor();
}
