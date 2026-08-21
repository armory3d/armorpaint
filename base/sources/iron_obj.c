#include "iron_obj.h"

#include "iron_alloc.h"
#include "iron_array.h"
#include "iron_math.h"
#include "iron_string.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static raw_mesh_t *part = NULL;
static f32_array_t pos_temp;
static f32_array_t uv_temp;
static f32_array_t nor_temp;
static uint32_t    va[512];
static uint32_t    ua[512];
static uint32_t    na[512];
static int         vi = 0;
static int         ui = 0;
static int         ni = 0;
static uint8_t     buf[128];
static char        str[256];

static int          vind_off     = 0;
static int          tind_off     = 0;
static int          nind_off     = 0;
static uint8_t     *bytes        = NULL;
static size_t       bytes_length = 0;
static f32_array_t *pos_first;
static f32_array_t *uv_first;
static f32_array_t *nor_first;
void                console_info(char *s);
static bool         check_uvmap = true;

static int read_int() {
	int bi = 0;
	while (part->pos < bytes_length) { // Read into buffer
		char c = bytes[part->pos];
		if (c == '/' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
			break;
		}
		buf[bi++] = c;
		part->pos++;
	}
	int res = 0; // Parse buffer into int
	int dec = 1;
	int off = buf[0] == '-' ? 1 : 0;
	int len = bi - 1;
	for (int i = 0; i < bi - off; ++i) {
		res += (buf[len - i] - 48) * dec;
		dec *= 10;
	}
	if (off > 0) {
		res *= -1;
	}
	return res;
}

static inline void skip_whitespace() {
	while (bytes[part->pos] == ' ' || bytes[part->pos] == '\t') {
		part->pos++;
	}
}

static void read_face_fast() {
	while (true) {
		va[vi++] = read_int() - 1;
		part->pos++; // '/'
		ua[ui++] = read_int() - 1;
		part->pos++; // '/'
		na[ni++] = read_int() - 1;
		skip_whitespace();
		if (bytes[part->pos] == '\n' || bytes[part->pos] == '\r' || part->pos >= bytes_length) {
			break;
		}
	}
}

static void read_face() {
	while (true) {
		va[vi++] = read_int() - 1;
		if (uv_temp.length > 0 || nor_temp.length > 0) {
			part->pos++; // "/"

			if (uv_temp.length > 0) {
				ua[ui++] = read_int() - 1;
			}

			if (nor_temp.length > 0) {
				// Some exporters put fake uv index even when uv data is not present... (f 1/1/1 instead of f 1//1)
				bool has_bogus_uv = uv_temp.length == 0 && bytes[part->pos] != '/';
				if (has_bogus_uv) {
					read_int();
				}

				part->pos++; // "/"
				na[ni++] = read_int() - 1;
			}
		}
		// Some exporters put "//" even when normal and uv data are not present (f 1//)
		else if (uv_temp.length == 0 && nor_temp.length == 0 && bytes[part->pos] == '/') {
			part->pos += 2;
		}

		skip_whitespace();
		if (bytes[part->pos] == '\n' || bytes[part->pos] == '\r' || part->pos >= bytes_length) {
			break;
		}
	}
}

static float read_float() {
	int bi = 0;
	while (true) { // Read into buffer
		char c = bytes[part->pos];
		if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
			break;
		}
		if (c == 'E' || c == 'e') {
			part->pos++;
			int first = buf[0] == '-' ? -(buf[1] - 48) : buf[0] - 48;
			int exp   = read_int();
			int dec   = 1;
			int loop  = exp > 0 ? exp : -exp;
			for (int i = 0; i < loop; ++i) {
				dec *= 10;
			}
			return exp > 0 ? (float)first * dec : (float)first / dec;
		}
		if (bi < 9) {
			buf[bi++] = c;
		}
		part->pos++;
	}
	float   res = 0.0; // Parse buffer into float
	int64_t dot = 1;
	int64_t dec = 1;
	int     off = buf[0] == '-' ? 1 : 0;
	int     len = bi - 1;
	for (int i = 0; i < bi - off; ++i) {
		char c = buf[len - i];
		if (c == '.') {
			dot = dec;
			continue;
		}
		res += (c - 48) * dec;
		dec *= 10;
	}
	if (off > 0) {
		res /= -dot;
	}
	else {
		res /= dot;
	}
	return res;
}

static char *read_string() {
	size_t begin = part->pos;
	while (true) {
		char c = bytes[part->pos];
		if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
			break;
		}
		part->pos++;
	}
	for (int i = 0; i < part->pos - begin; ++i) {
		str[i] = bytes[begin + i];
	}
	str[part->pos - begin] = '\0';
	return str;
}

static void next_line() {
	while (part->pos < bytes_length) {
		char c = bytes[part->pos++];
		if (c == '\n') {
			break; // \n, \r\n
		}
	}
}

static int get_tile(int i1, int i2, int i3, i32_array_t *uv_indices, int tiles_u) {
	float u1     = uv_temp.buffer[uv_indices->buffer[i1] * 2];
	float v1     = uv_temp.buffer[uv_indices->buffer[i1] * 2 + 1];
	float u2     = uv_temp.buffer[uv_indices->buffer[i2] * 2];
	float v2     = uv_temp.buffer[uv_indices->buffer[i2] * 2 + 1];
	float u3     = uv_temp.buffer[uv_indices->buffer[i3] * 2];
	float v3     = uv_temp.buffer[uv_indices->buffer[i3] * 2 + 1];
	int   tile_u = (int)((u1 + u2 + u3) / 3);
	int   tile_v = (int)((v1 + v2 + v3) / 3);
	return tile_u + tile_v * tiles_u;
}

static bool pnpoly(float v0x, float v0y, float v1x, float v1y, float v2x, float v2y, float px, float py) {
	// https://wrf.ecse.rpi.edu//Research/Short_Notes/pnpoly.html
	bool c = false;
	if (((v0y > py) != (v2y > py)) && (px < (v2x - v0x) * (py - v0y) / (v2y - v0y) + v0x)) {
		c = !c;
	}
	if (((v1y > py) != (v0y > py)) && (px < (v0x - v1x) * (py - v1y) / (v0y - v1y) + v1x)) {
		c = !c;
	}
	if (((v2y > py) != (v1y > py)) && (px < (v1x - v2x) * (py - v2y) / (v1y - v2y) + v2x)) {
		c = !c;
	}
	return c;
}

static vec4_t calc_normal(vec4_t a, vec4_t b, vec4_t c) {
	vec4_t cb = vec4_sub(c, b);
	vec4_t ab = vec4_sub(a, b);
	cb        = vec4_cross(cb, ab);
	cb        = vec4_norm(cb);
	return cb;
}

// 'o' for object split, 'g' for groups, 'u'semtl for materials
raw_mesh_t *obj_parse(buffer_t *file_bytes, char split_code, uint64_t start_pos, bool udim) {
	bytes        = file_bytes->buffer;
	bytes_length = file_bytes->length;

	part            = calloc(1, sizeof(raw_mesh_t));
	part->scale_pos = 1.0;
	part->scale_tex = 1.0;
	part->pos       = start_pos;
	part->udims_u   = 1;
	part->udims_v   = 1;
	part->name      = string_copy(str);

	i32_array_t pos_indices = {0};
	i32_array_t uv_indices  = {0};
	i32_array_t nor_indices = {0};

	bool reading_faces  = false;
	bool reading_object = false;
	bool full_attrib    = false;
	check_uvmap         = true;

	if (start_pos == 0) {
		vind_off = tind_off = nind_off = 0;
	}

	if (split_code == 'u' && start_pos > 0) {
		pos_temp = *pos_first;
		nor_temp = *nor_first;
		uv_temp  = *uv_first;
	}
	else {
		array_free(&pos_temp);
		array_free(&uv_temp);
		array_free(&nor_temp);
	}

	while (part->pos < bytes_length) {
		char c0 = bytes[part->pos++];
		if (reading_object && reading_faces && (c0 == 'v' || c0 == split_code)) {
			part->pos--;
			part->has_next = true;
			break;
		}

		if (c0 == 'v') {
			char c1 = bytes[part->pos++];
			if (c1 == ' ') {
				skip_whitespace(); // Some exporters put additional space directly after "v"
				f32_array_push(&pos_temp, read_float());
				skip_whitespace();
				f32_array_push(&pos_temp, read_float());
				skip_whitespace();
				f32_array_push(&pos_temp, read_float());
			}
			else if (c1 == 't') {
				skip_whitespace();
				f32_array_push(&uv_temp, read_float());
				skip_whitespace();
				f32_array_push(&uv_temp, read_float());
				if (nor_temp.length > 0) {
					full_attrib = true;
				}
			}
			else if (c1 == 'n') {
				skip_whitespace();
				f32_array_push(&nor_temp, read_float());
				skip_whitespace();
				f32_array_push(&nor_temp, read_float());
				skip_whitespace();
				f32_array_push(&nor_temp, read_float());
				if (uv_temp.length > 0) {
					full_attrib = true;
				}
			}
		}
		else if (c0 == 'f') {
			skip_whitespace();
			reading_faces = true;
			vi = ui = ni = 0;
			full_attrib ? read_face_fast() : read_face();

			if (vi <= 4) { // Convex, fan triangulation
				i32_array_push(&pos_indices, va[0]);
				i32_array_push(&pos_indices, va[1]);
				i32_array_push(&pos_indices, va[2]);
				for (int i = 3; i < vi; ++i) {
					i32_array_push(&pos_indices, va[0]);
					i32_array_push(&pos_indices, va[i - 1]);
					i32_array_push(&pos_indices, va[i]);
				}
				if (uv_temp.length > 0) {
					i32_array_push(&uv_indices, ua[0]);
					i32_array_push(&uv_indices, ua[1]);
					i32_array_push(&uv_indices, ua[2]);
					for (int i = 3; i < ui; ++i) {
						i32_array_push(&uv_indices, ua[0]);
						i32_array_push(&uv_indices, ua[i - 1]);
						i32_array_push(&uv_indices, ua[i]);
					}
					if (check_uvmap && ua[0] == 0 && ua[1] == 0 && ua[2] == 0) { // Blender points faces with no UV to 0.0 (vt index 0)
						check_uvmap = false;
						console_info("Warning: Mesh is not fully UV unwrapped");
					}
				}
				if (nor_temp.length > 0) {
					i32_array_push(&nor_indices, na[0]);
					i32_array_push(&nor_indices, na[1]);
					i32_array_push(&nor_indices, na[2]);
					for (int i = 3; i < ni; ++i) {
						i32_array_push(&nor_indices, na[0]);
						i32_array_push(&nor_indices, na[i - 1]);
						i32_array_push(&nor_indices, na[i]);
					}
				}
			}
			else { // Convex or concave, ear clipping
				int   _vind_off = split_code == 'u' ? 0 : vind_off;
				int   _nind_off = split_code == 'u' ? 0 : nind_off;
				float nx        = 0.0;
				float ny        = 0.0;
				float nz        = 0.0;
				if (nor_temp.length > 0) {
					nx = nor_temp.buffer[(na[0] - _nind_off) * 3];
					ny = nor_temp.buffer[(na[0] - _nind_off) * 3 + 1];
					nz = nor_temp.buffer[(na[0] - _nind_off) * 3 + 2];
				}
				else {
					vec4_t n = calc_normal((vec4_t){pos_temp.buffer[(va[0] - _vind_off) * 3], pos_temp.buffer[(va[0] - _vind_off) * 3 + 1],
					                                pos_temp.buffer[(va[0] - _vind_off) * 3 + 2], 1.0f},
					                       (vec4_t){pos_temp.buffer[(va[1] - _vind_off) * 3], pos_temp.buffer[(va[1] - _vind_off) * 3 + 1],
					                                pos_temp.buffer[(va[1] - _vind_off) * 3 + 2], 1.0f},
					                       (vec4_t){pos_temp.buffer[(va[2] - _vind_off) * 3], pos_temp.buffer[(va[2] - _vind_off) * 3 + 1],
					                                pos_temp.buffer[(va[2] - _vind_off) * 3 + 2], 1.0f});
					nx       = n.x;
					ny       = n.y;
					nz       = n.z;
				}
				float nxabs = (float)fabs(nx);
				float nyabs = (float)fabs(ny);
				float nzabs = (float)fabs(nz);
				bool  flip  = nx + ny + nz > 0;
				int   axis  = nxabs > nyabs && nxabs > nzabs ? 0 : nyabs > nxabs && nyabs > nzabs ? 1 : 2;
				int   axis0 = axis == 0 ? (flip ? 2 : 1) : axis == 1 ? (flip ? 0 : 2) : (flip ? 1 : 0);
				int   axis1 = axis == 0 ? (flip ? 1 : 2) : axis == 1 ? (flip ? 2 : 0) : (flip ? 0 : 1);

				int loops = 0;
				int i     = -1;
				while (vi > 3 && loops++ < vi) {
					i         = (i + 1) % vi;
					int   i1  = (i + 1) % vi;
					int   i2  = (i + 2) % vi;
					int   vi0 = (va[i] - _vind_off) * 3;
					int   vi1 = (va[i1] - _vind_off) * 3;
					int   vi2 = (va[i2] - _vind_off) * 3;
					float v0x = pos_temp.buffer[vi0 + axis0];
					float v0y = pos_temp.buffer[vi0 + axis1];
					float v1x = pos_temp.buffer[vi1 + axis0];
					float v1y = pos_temp.buffer[vi1 + axis1];
					float v2x = pos_temp.buffer[vi2 + axis0];
					float v2y = pos_temp.buffer[vi2 + axis1];

					float e0x   = v0x - v1x; // Not an interior vertex
					float e0y   = v0y - v1y;
					float e1x   = v2x - v1x;
					float e1y   = v2y - v1y;
					float cross = e0x * e1y - e0y * e1x;
					if (cross <= 0) {
						continue;
					}

					bool overlap = false; // Other vertex found inside this triangle
					for (int j = 0; j < vi - 3; ++j) {
						int   j0 = (va[(i + 3 + j) % vi] - _vind_off) * 3;
						float px = pos_temp.buffer[j0 + axis0];
						float py = pos_temp.buffer[j0 + axis1];
						if (pnpoly(v0x, v0y, v1x, v1y, v2x, v2y, px, py)) {
							overlap = true;
							break;
						}
					}
					if (overlap) {
						continue;
					}

					i32_array_push(&pos_indices, va[i]); // Found ear
					i32_array_push(&pos_indices, va[i1]);
					i32_array_push(&pos_indices, va[i2]);
					if (uv_temp.length > 0) {
						i32_array_push(&uv_indices, ua[i]);
						i32_array_push(&uv_indices, ua[i1]);
						i32_array_push(&uv_indices, ua[i2]);
					}
					if (nor_temp.length > 0) {
						i32_array_push(&nor_indices, na[i]);
						i32_array_push(&nor_indices, na[i1]);
						i32_array_push(&nor_indices, na[i2]);
					}

					for (int j = ((i + 1) % vi); j < vi - 1; ++j) { // Consume vertex
						va[j] = va[j + 1];
						ua[j] = ua[j + 1];
						na[j] = na[j + 1];
					}
					vi--;
					i--;
					loops = 0;
				}
				i32_array_push(&pos_indices, va[0]); // Last one
				i32_array_push(&pos_indices, va[1]);
				i32_array_push(&pos_indices, va[2]);
				if (uv_temp.length > 0) {
					i32_array_push(&uv_indices, ua[0]);
					i32_array_push(&uv_indices, ua[1]);
					i32_array_push(&uv_indices, ua[2]);
				}
				if (nor_temp.length > 0) {
					i32_array_push(&nor_indices, na[0]);
					i32_array_push(&nor_indices, na[1]);
					i32_array_push(&nor_indices, na[2]);
				}
			}
		}
		else if (c0 == split_code) {
			if (split_code == 'u') {
				part->pos += 5; // "u"semtl
			}
			skip_whitespace();
			if (!udim) {
				reading_object = true;
			}
			free(part->name);
			part->name = string_copy(read_string());
		}
		else if (c0 == '\n') { // Empty line
			continue;
		}
		next_line();
	}

	if (start_pos > 0) {
		if (split_code != 'u') {
			for (int i = 0; i < pos_indices.length; ++i) {
				pos_indices.buffer[i] -= vind_off;
			}
			for (int i = 0; i < uv_indices.length; ++i) {
				uv_indices.buffer[i] -= tind_off;
			}
			for (int i = 0; i < nor_indices.length; ++i) {
				nor_indices.buffer[i] -= nind_off;
			}
		}
	}
	else {
		if (split_code == 'u') {
			pos_first = &pos_temp;
			nor_first = &nor_temp;
			uv_first  = &uv_temp;
		}
	}
	vind_off += (int)(pos_temp.length / 3); // Assumes separate vertex data per object
	tind_off += (int)(uv_temp.length / 2);
	nind_off += (int)(nor_temp.length / 3);

	// Pack positions to (-1, 1) range
	part->scale_pos = 0.0;
	for (int i = 0; i < pos_temp.length; ++i) {
		float f = (float)fabs(pos_temp.buffer[i]);
		if (part->scale_pos < f) {
			part->scale_pos = f;
		}
	}
	float inv = 32767 * (1 / part->scale_pos);

	part->posa         = calloc(sizeof(i16_array_t), 1);
	part->posa->length = part->posa->capacity = pos_indices.length * 4;
	part->posa->buffer                        = malloc(part->posa->capacity * sizeof(int16_t));

	part->inda         = calloc(sizeof(u32_array_t), 1);
	part->inda->length = part->inda->capacity = pos_indices.length;
	part->inda->buffer                        = malloc(part->inda->capacity * sizeof(uint32_t));

	part->vertex_count = pos_indices.length;
	part->index_count  = pos_indices.length;
	int inda_length    = pos_indices.length;
	for (int i = 0; i < pos_indices.length; ++i) {
		part->posa->buffer[i * 4]     = (int)(pos_temp.buffer[pos_indices.buffer[i] * 3] * inv);
		part->posa->buffer[i * 4 + 1] = (int)(-pos_temp.buffer[pos_indices.buffer[i] * 3 + 2] * inv);
		part->posa->buffer[i * 4 + 2] = (int)(pos_temp.buffer[pos_indices.buffer[i] * 3 + 1] * inv);
		part->inda->buffer[i]         = i;
	}

	if (nor_indices.length > 0) {
		part->nora         = calloc(sizeof(i16_array_t), 1);
		part->nora->length = part->nora->capacity = nor_indices.length * 2;
		part->nora->buffer                        = malloc(part->nora->capacity * sizeof(int16_t));

		for (int i = 0; i < pos_indices.length; ++i) {
			part->nora->buffer[i * 2]     = (int)(nor_temp.buffer[nor_indices.buffer[i] * 3] * 32767);
			part->nora->buffer[i * 2 + 1] = (int)(-nor_temp.buffer[nor_indices.buffer[i] * 3 + 2] * 32767);
			part->posa->buffer[i * 4 + 3] = (int)(nor_temp.buffer[nor_indices.buffer[i] * 3 + 1] * 32767);
		}
	}
	else {
		// Calc normals
		part->nora         = calloc(sizeof(i16_array_t), 1);
		part->nora->length = part->nora->capacity = inda_length * 2;
		part->nora->buffer                        = malloc(part->nora->capacity * sizeof(int16_t));

		for (int i = 0; i < (int)(inda_length / 3); ++i) {
			int    i1                  = part->inda->buffer[i * 3];
			int    i2                  = part->inda->buffer[i * 3 + 1];
			int    i3                  = part->inda->buffer[i * 3 + 2];
			vec4_t n                   = calc_normal((vec4_t){part->posa->buffer[i1 * 4], part->posa->buffer[i1 * 4 + 1], part->posa->buffer[i1 * 4 + 2], 1.0},
			                                         (vec4_t){part->posa->buffer[i2 * 4], part->posa->buffer[i2 * 4 + 1], part->posa->buffer[i2 * 4 + 2], 1.0},
			                                         (vec4_t){part->posa->buffer[i3 * 4], part->posa->buffer[i3 * 4 + 1], part->posa->buffer[i3 * 4 + 2], 1.0});
			part->nora->buffer[i1 * 2] = (int)(n.x * 32767);
			part->nora->buffer[i1 * 2 + 1] = (int)(n.y * 32767);
			part->posa->buffer[i1 * 4 + 3] = (int)(n.z * 32767);
			part->nora->buffer[i2 * 2]     = (int)(n.x * 32767);
			part->nora->buffer[i2 * 2 + 1] = (int)(n.y * 32767);
			part->posa->buffer[i2 * 4 + 3] = (int)(n.z * 32767);
			part->nora->buffer[i3 * 2]     = (int)(n.x * 32767);
			part->nora->buffer[i3 * 2 + 1] = (int)(n.y * 32767);
			part->posa->buffer[i3 * 4 + 3] = (int)(n.z * 32767);
		}
	}

	if (uv_indices.length > 0) {
		if (udim) {
			// Find number of tiles
			int tiles_u = 1;
			int tiles_v = 1;
			for (int i = 0; i < (int)(uv_temp.length / 2); ++i) {
				while (uv_temp.buffer[i * 2] > tiles_u)
					tiles_u++;
				while (uv_temp.buffer[i * 2 + 1] > tiles_v)
					tiles_v++;
			}

			// Amount of indices pre tile
			uint32_t *num = (uint32_t *)malloc(tiles_u * tiles_v * sizeof(uint32_t));
			memset(num, 0, tiles_u * tiles_v * sizeof(uint32_t));
			for (int i = 0; i < (int)(inda_length / 3); ++i) {
				int tile = get_tile(part->inda->buffer[i * 3], part->inda->buffer[i * 3 + 1], part->inda->buffer[i * 3 + 2], &uv_indices, tiles_u);
				num[tile] += 3;
			}

			// Split indices per tile
			part->udims   = any_array_create(tiles_u * tiles_v);
			part->udims_u = tiles_u;
			part->udims_v = tiles_v;
			for (int i = 0; i < tiles_u * tiles_v; ++i) {
				part->udims->buffer[i] = u32_array_create(num[i]);
				num[i]                 = 0;
			}

			for (int i = 0; i < (int)(inda_length / 3); ++i) {
				int          i1        = part->inda->buffer[i * 3];
				int          i2        = part->inda->buffer[i * 3 + 1];
				int          i3        = part->inda->buffer[i * 3 + 2];
				int          tile      = get_tile(i1, i2, i3, &uv_indices, tiles_u);
				u32_array_t *a         = part->udims->buffer[tile];
				a->buffer[num[tile]++] = i1;
				a->buffer[num[tile]++] = i2;
				a->buffer[num[tile]++] = i3;
			}

			// Normalize uvs to 0-1 range
			int16_t *uvtiles = (int16_t *)malloc(uv_temp.length * sizeof(int16_t));
			for (int i = 0; i < (int)(inda_length / 3); ++i) { // TODO: merge loops
				int i1                                 = part->inda->buffer[i * 3];
				int i2                                 = part->inda->buffer[i * 3 + 1];
				int i3                                 = part->inda->buffer[i * 3 + 2];
				int tile                               = get_tile(i1, i2, i3, &uv_indices, tiles_u);
				int tile_u                             = tile % tiles_u;
				int tile_v                             = (int)(tile / tiles_u);
				uvtiles[uv_indices.buffer[i1] * 2]     = tile_u;
				uvtiles[uv_indices.buffer[i1] * 2 + 1] = tile_v;
				uvtiles[uv_indices.buffer[i2] * 2]     = tile_u;
				uvtiles[uv_indices.buffer[i2] * 2 + 1] = tile_v;
				uvtiles[uv_indices.buffer[i3] * 2]     = tile_u;
				uvtiles[uv_indices.buffer[i3] * 2 + 1] = tile_v;
			}
			for (int i = 0; i < uv_temp.length; ++i) {
				uv_temp.buffer[i] -= uvtiles[i];
			}
			free(uvtiles);
			free(num);
		}

		part->texa         = calloc(sizeof(i16_array_t), 1);
		part->texa->length = part->texa->capacity = uv_indices.length * 2;
		part->texa->buffer                        = malloc(part->texa->capacity * sizeof(int16_t));

		for (int i = 0; i < uv_indices.length; ++i) {
			float uvx = uv_temp.buffer[uv_indices.buffer[i] * 2];
			if (uvx > 1.0) {
				uvx = uvx - (int)(uvx);
			}
			float uvy = uv_temp.buffer[uv_indices.buffer[i] * 2 + 1];
			if (uvy > 1.0) {
				uvy = uvy - (int)(uvy);
			}
			part->texa->buffer[i * 2]     = (int)(uvx * 32767);
			part->texa->buffer[i * 2 + 1] = (int)((1.0 - uvy) * 32767);
		}
	}

	bytes = NULL;
	if (!part->has_next) {
		pos_first = nor_first = uv_first = NULL;
		array_free(&pos_temp);
		array_free(&uv_temp);
		array_free(&nor_temp);
	}

	array_free(&pos_indices);
	array_free(&uv_indices);
	array_free(&nor_indices);

	return part;
}

void obj_destroy(raw_mesh_t *part) {
	// if (part->udims != NULL) {
	// 	for (int i = 0; i < part->udims_u * part->udims_v; ++i) {
	// 		free(part->udims[i]);
	// 	}
	// 	free(part->udims);
	// }

	free(part->posa);
	free(part->nora);
	free(part->texa);
	free(part->inda);
	free(part);
}
