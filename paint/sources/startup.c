#include "engine.h"
#include "iron_alloc.h"
#include "iron_array.h"
#include <stdbool.h>
#include <string.h>

// Compass and gizmo meshes are generated: x / y / z colored arrows, gizmo adds scale cubes and rotate rings.
// The overlay shader reads only pos and col, so faces share vertices.

#define OVERLAY_ARROW_SEGS 32
#define OVERLAY_RING_SEGS  8

static i16_array_t *_overlay_pos;
static i16_array_t *_overlay_col;
static u32_array_t *_overlay_ind;
static uint32_t     _overlay_v;
static uint32_t     _overlay_i;
static float        _overlay_inv;

// sin(k * pi / 32), every generated mesh steps by a multiple of it
static float startup_sin(int k) {
	static const float q[17] = {0.00000000f, 0.09801714f, 0.19509032f, 0.29028468f, 0.38268343f, 0.47139674f, 0.55557023f, 0.63439328f, 0.70710678f,
	                            0.77301045f, 0.83146961f, 0.88192126f, 0.92387953f, 0.95694034f, 0.98078528f, 0.99518473f, 1.00000000f};
	k &= 63;
	return k <= 16 ? q[k] : k <= 32 ? q[32 - k] : k <= 48 ? -q[k - 32] : -q[64 - k];
}

static float startup_cos(int k) {
	return startup_sin(k + 16);
}

static int16_t startup_quantize(float f) {
	f *= 32767.0f;
	return (int16_t)(f + (f < 0.0f ? -0.5f : 0.5f));
}

// Buffers come zeroed from calloc, only xyz and the color channel are written
static uint32_t overlay_vert(float x, float y, float z) {
	int16_t *pos = _overlay_pos->buffer + _overlay_v * 4;
	pos[0]       = startup_quantize(x * _overlay_inv);
	pos[1]       = startup_quantize(y * _overlay_inv);
	pos[2]       = startup_quantize(z * _overlay_inv);
	return _overlay_v++;
}

static void overlay_color(uint32_t from, int color) {
	int16_t *col = _overlay_col->buffer + color;
	for (uint32_t v = from; v < _overlay_v; ++v) {
		col[v * 4] = 32767;
	}
}

static void overlay_tri(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t *p = _overlay_ind->buffer + _overlay_i;
	p[0]        = a;
	p[1]        = b;
	p[2]        = c;
	_overlay_i += 3;
}

// Counter-clockwise when viewed from the front
static void overlay_quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	overlay_tri(a, b, c);
	overlay_tri(a, c, d);
}

// Builds the x arrow, y and z arrows are rotated copies of it
static void overlay_arrows(float shaft_r, float shaft_len, float cone_r, float cone_start, float cone_end) {
	uint32_t from  = _overlay_v;
	uint32_t ifrom = _overlay_i;
	int      n     = OVERLAY_ARROW_SEGS;

	// Rings go from +y towards -z
	uint32_t s0 = _overlay_v;
	for (int k = 0; k < n; ++k) {
		overlay_vert(0, shaft_r * startup_cos(k * 64 / n), -shaft_r * startup_sin(k * 64 / n));
	}
	uint32_t s1 = _overlay_v;
	for (int k = 0; k < n; ++k) {
		overlay_vert(shaft_len, shaft_r * startup_cos(k * 64 / n), -shaft_r * startup_sin(k * 64 / n));
	}
	uint32_t c0 = _overlay_v;
	for (int k = 0; k < n; ++k) {
		overlay_vert(cone_start, cone_r * startup_cos(k * 64 / n), -cone_r * startup_sin(k * 64 / n));
	}
	uint32_t tip = overlay_vert(cone_end, 0, 0);

	for (int k = 0; k < n; ++k) {
		int k1 = (k + 1) % n;
		overlay_quad(s0 + k, s1 + k, s1 + k1, s0 + k1);
	}
	// Shaft end faces +x, shaft start and cone base face -x
	for (int k = 1; k < n - 1; ++k) {
		overlay_tri(s1, s1 + k + 1, s1 + k);
	}
	for (int k = 1; k < n - 1; ++k) {
		overlay_tri(s0, s0 + k, s0 + k + 1);
	}
	for (int k = 0; k < n; ++k) {
		overlay_tri(c0 + k, tip, c0 + (k + 1) % n);
	}
	for (int k = 1; k < n - 1; ++k) {
		overlay_tri(c0, c0 + k, c0 + k + 1);
	}
	overlay_color(from, 0);

	uint32_t verts = _overlay_v - from;
	uint32_t inds  = _overlay_i - ifrom;
	int16_t *src   = _overlay_pos->buffer + from * 4;
	int16_t *dst_y = src + verts * 4;
	int16_t *dst_z = dst_y + verts * 4;
	for (uint32_t v = 0; v < verts * 4; v += 4) {
		// (x, y, z) -> (-y, x, z)
		dst_y[v]     = -src[v + 1];
		dst_y[v + 1] = src[v];
		dst_y[v + 2] = src[v + 2];
	}
	for (uint32_t v = 0; v < verts * 4; v += 4) {
		// (x, y, z) -> (-z, y, x)
		dst_z[v]     = -src[v + 2];
		dst_z[v + 1] = src[v + 1];
		dst_z[v + 2] = src[v];
	}
	for (int axis = 1; axis < 3; ++axis) {
		uint32_t offset = _overlay_v - from;
		for (uint32_t i = 0; i < inds; ++i) {
			_overlay_ind->buffer[_overlay_i + i] = _overlay_ind->buffer[ifrom + i] + offset;
		}
		_overlay_v += verts;
		_overlay_i += inds;
		overlay_color(_overlay_v - verts, axis);
	}
}

static void overlay_cube(int axis, float dist, float half) {
	uint32_t from = _overlay_v;
	float    c[3] = {0, 0, 0};
	c[axis]       = dist;
	// Corner i has +half on axis a when bit a is set
	for (int i = 0; i < 8; ++i) {
		overlay_vert(c[0] + (i & 1 ? half : -half), c[1] + (i & 2 ? half : -half), c[2] + (i & 4 ? half : -half));
	}
	for (int a = 0; a < 3; ++a) {
		int u = (a + 1) % 3;
		int v = (a + 2) % 3;
		for (int side = 0; side < 2; ++side) {
			// Corners counter-clockwise around +a, reversed on the -a side
			uint32_t q[4];
			for (int j = 0; j < 4; ++j) {
				int bu = (j == 1 || j == 2) != (side == 0);
				int bv = j >= 2;
				q[j]   = from + (side << a) + (bu << u) + (bv << v);
			}
			overlay_quad(q[0], q[1], q[2], q[3]);
		}
	}
	overlay_color(from, axis);
}

// Quarter ring sweeping from axis s to axis t, with a diamond cross-section; s, t, n form a right-handed basis
static void overlay_ring(int color, float *s, float *t, float *n, float radius, float thickness) {
	uint32_t from = _overlay_v;
	for (int k = 0; k <= OVERLAY_RING_SEGS; ++k) {
		float cs = startup_cos(k * 16 / OVERLAY_RING_SEGS);
		float sn = startup_sin(k * 16 / OVERLAY_RING_SEGS);
		// Outer, +n, inner, -n
		for (int j = 0; j < 4; ++j) {
			float d_r = j == 0 ? thickness : j == 2 ? -thickness : 0;
			float d_n = j == 1 ? thickness : j == 3 ? -thickness : 0;
			float p[3];
			for (int a = 0; a < 3; ++a) {
				p[a] = (radius + d_r) * (cs * s[a] + sn * t[a]) + d_n * n[a];
			}
			overlay_vert(p[0], p[1], p[2]);
		}
	}
	for (int k = 0; k < OVERLAY_RING_SEGS; ++k) {
		uint32_t v = from + k * 4;
		for (int j = 0; j < 4; ++j) {
			int j1 = (j + 1) % 4;
			overlay_quad(v + j, v + 4 + j, v + 4 + j1, v + j1);
		}
	}
	overlay_color(from, color);
}

static void overlay_begin(mesh_data_t *md, uint32_t verts, uint32_t inds, float scale_pos) {
	md->scale_pos     = scale_pos;
	md->scale_tex     = 1.0f;
	md->vertex_arrays = any_array_create(2);
	_overlay_pos      = i16_array_create(verts * 4);
	_overlay_col      = i16_array_create(verts * 4);
	_overlay_ind      = u32_array_create(inds);
	_overlay_v        = 0;
	_overlay_i        = 0;
	_overlay_inv      = 1.0f / scale_pos;

	vertex_array_t *pos          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
	pos->attrib                  = "pos";
	pos->data                    = "short4norm";
	pos->values                  = _overlay_pos;
	md->vertex_arrays->buffer[0] = pos;

	vertex_array_t *col          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
	col->attrib                  = "col";
	col->data                    = "short4norm";
	col->values                  = _overlay_col;
	md->vertex_arrays->buffer[1] = col;
	md->index_array              = _overlay_ind;
}

// Per arrow: 3 rings + tip, 32 side quads, 3 fans and the cone
#define OVERLAY_ARROW_VERTS (OVERLAY_ARROW_SEGS * 3 + 1)
#define OVERLAY_ARROW_INDS  ((OVERLAY_ARROW_SEGS * 3 + (OVERLAY_ARROW_SEGS - 2) * 3) * 3)
#define OVERLAY_RING_VERTS  ((OVERLAY_RING_SEGS + 1) * 4)
#define OVERLAY_RING_INDS   (OVERLAY_RING_SEGS * 4 * 6)

static mesh_data_t *startup_make_compass(void) {
	mesh_data_t *md = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name        = ".Compass";
	overlay_begin(md, 3 * OVERLAY_ARROW_VERTS, 3 * OVERLAY_ARROW_INDS, 1.855f);
	overlay_arrows(0.035f, 1.25f, 0.145f, 1.2435f, 1.855f);
	return md;
}

static mesh_data_t *startup_make_gizmo(void) {
	mesh_data_t *md = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name        = ".Gizmo";
	overlay_begin(md, 3 * (OVERLAY_ARROW_VERTS + 8 + OVERLAY_RING_VERTS), 3 * (OVERLAY_ARROW_INDS + 36 + OVERLAY_RING_INDS), 2.005f);
	overlay_arrows(0.0175f, 0.95f, 0.145f, 1.3935f, 2.005f);
	for (int axis = 0; axis < 3; ++axis) {
		overlay_cube(axis, 1.0f, 0.1f);
	}
	float x[3]  = {1, 0, 0};
	float y[3]  = {0, 1, 0};
	float z[3]  = {0, 0, 1};
	float nx[3] = {-1, 0, 0};
	overlay_ring(2, x, y, z, 1.24f, 0.02f);
	overlay_ring(1, z, x, y, 1.24f, 0.02f);
	overlay_ring(0, z, y, nx, 1.24f, 0.02f);
	return md;
}

// 64 x 32 uv sphere of radius 0.5, same layout as geom_make_uv_sphere: the seam column and pole rows are duplicated for uvs
#define SPHERE_SEGS_U 64
#define SPHERE_SEGS_V 32

static mesh_data_t *startup_make_sphere(void) {
	mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name          = ".Sphere";
	md->scale_pos     = 1.0f;
	md->scale_tex     = 2.0f; // u spans 2 around the equator, v spans 1 between poles, texels stay square
	md->vertex_arrays = any_array_create(3);

	int          w   = SPHERE_SEGS_U + 1;
	int          h   = SPHERE_SEGS_V + 1;
	i16_array_t *pos = i16_array_create(w * h * 4);
	i16_array_t *nor = i16_array_create(w * h * 2);
	i16_array_t *tex = i16_array_create(w * h * 2);
	u32_array_t *ind = u32_array_create(SPHERE_SEGS_U * (SPHERE_SEGS_V - 1) * 6);

	// Steps of pi / 32 in both directions
	float   cu[SPHERE_SEGS_U + 1];
	float   su[SPHERE_SEGS_U + 1];
	int16_t tu[SPHERE_SEGS_U + 1];
	for (int x = 0; x < w; ++x) {
		cu[x] = -startup_cos(x * 64 / SPHERE_SEGS_U);
		su[x] = startup_sin(x * 64 / SPHERE_SEGS_U);
		tu[x] = startup_quantize(x / (float)SPHERE_SEGS_U);
	}

	for (int y = 0; y < h; ++y) {
		int16_t *po = pos->buffer + y * w * 4;
		int16_t *no = nor->buffer + y * w * 2;
		int16_t *te = tex->buffer + y * w * 2;
		int16_t  tv = startup_quantize((1.0f - y / (float)SPHERE_SEGS_V) / 2.0f);
		if (y > SPHERE_SEGS_V / 2) {
			// Mirror of the lower hemisphere
			int16_t *mpo = pos->buffer + (SPHERE_SEGS_V - y) * w * 4;
			int16_t *mno = nor->buffer + (SPHERE_SEGS_V - y) * w * 2;
			for (int x = 0; x < w; ++x) {
				po[x * 4]     = mpo[x * 4];
				po[x * 4 + 1] = mpo[x * 4 + 1];
				po[x * 4 + 2] = -mpo[x * 4 + 2];
				po[x * 4 + 3] = -mpo[x * 4 + 3];
				no[x * 2]     = mno[x * 2];
				no[x * 2 + 1] = mno[x * 2 + 1];
				te[x * 2]     = tu[x];
				te[x * 2 + 1] = tv;
			}
			continue;
		}
		float   sv = startup_sin(y * 32 / SPHERE_SEGS_V);
		float   nz = -startup_cos(y * 32 / SPHERE_SEGS_V);
		int16_t pz = startup_quantize(nz * 0.5f);
		int16_t pw = startup_quantize(nz);
		for (int x = 0; x < w; ++x) {
			float nx      = cu[x] * sv;
			float ny      = su[x] * sv;
			po[x * 4]     = startup_quantize(nx * 0.5f);
			po[x * 4 + 1] = startup_quantize(ny * 0.5f);
			po[x * 4 + 2] = pz;
			po[x * 4 + 3] = pw;
			no[x * 2]     = startup_quantize(nx);
			no[x * 2 + 1] = startup_quantize(ny);
			te[x * 2]     = tu[x];
			te[x * 2 + 1] = tv;
		}
	}
	// Pole vertices sit in the middle of their triangle's uv span
	for (int x = 0; x < SPHERE_SEGS_U; ++x) {
		tex->buffer[x * 2]                           = startup_quantize((x + 0.5f) / SPHERE_SEGS_U);
		tex->buffer[(SPHERE_SEGS_V * w + x + 1) * 2] = startup_quantize((x + 0.5f) / SPHERE_SEGS_U);
	}

	// Pole rows drop the degenerate triangle of each quad
	uint32_t *p = ind->buffer;
	for (int x = 0; x < SPHERE_SEGS_U; ++x) {
		p[0] = x;
		p[1] = w + x;
		p[2] = w + x + 1;
		p += 3;
	}
	for (int y = 1; y < SPHERE_SEGS_V - 1; ++y) {
		for (int x = 0; x < SPHERE_SEGS_U; ++x) {
			uint32_t a = y * w + x + 1;
			uint32_t b = y * w + x;
			uint32_t c = (y + 1) * w + x;
			uint32_t d = (y + 1) * w + x + 1;
			p[0]       = a;
			p[1]       = b;
			p[2]       = d;
			p[3]       = b;
			p[4]       = c;
			p[5]       = d;
			p += 6;
		}
	}
	for (int x = 0; x < SPHERE_SEGS_U; ++x) {
		uint32_t y = SPHERE_SEGS_V - 1;
		p[0]       = y * w + x + 1;
		p[1]       = y * w + x;
		p[2]       = (y + 1) * w + x + 1;
		p += 3;
	}

	char        *attribs[] = {"pos", "nor", "tex"};
	char        *datas[]   = {"short4norm", "short2norm", "short2norm"};
	i16_array_t *values[]  = {pos, nor, tex};
	for (int i = 0; i < 3; ++i) {
		vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
		vat->attrib                  = attribs[i];
		vat->data                    = datas[i];
		vat->values                  = values[i];
		md->vertex_arrays->buffer[i] = vat;
	}
	md->index_array = ind;
	return md;
}

static int16_t  mesh__RotateX_pos[]     = {3276,   -16383, -16383, 0,      3276,   -16383, 16383,  0,      -3276,  -16383, 16383,  0,      -3276, -16383,
                                           -16383, 0,      -3276,  -16383, -16383, 0,      -3276,  -16383, 16383,  0,      -3276,  16383,  16383, 0,
                                           -3276,  16383,  -16383, 0,      -3276,  16383,  -16383, 0,      -3276,  16383,  16383,  0,      3276,  16383,
                                           16383,  0,      3276,   16383,  -16383, 0,      3276,   16383,  -16383, 0,      3276,   16383,  16383, 0,
                                           3276,   -16383, 16383,  0,      3276,   -16383, -16383, 0,      -3276,  -16383, -16383, -32767, -3276, 16383,
                                           -16383, -32767, 3276,   16383,  -16383, -32767, 3276,   -16383, -16383, -32767, -3276,  16383,  16383, 32767,
                                           -3276,  -16383, 16383,  32767,  3276,   -16383, 16383,  32767,  3276,   16383,  16383,  32767};
static int16_t  mesh__RotateX_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                           0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                           0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__RotateX_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                           12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__RotateY_pos[]     = {16383,  -3276,  -16383, 0,     16383,  -3276,  16383,  0,     -16383, -3276,  16383,  0,      -16383, -3276,
                                           -16383, 0,      -16383, -3276, -16383, 0,      -16383, -3276, 16383,  0,      -16383, 3276,   16383,  0,
                                           -16383, 3276,   -16383, 0,     -16383, 3276,   -16383, 0,     -16383, 3276,   16383,  0,      16383,  3276,
                                           16383,  0,      16383,  3276,  -16383, 0,      16383,  3276,  -16383, 0,      16383,  3276,   16383,  0,
                                           16383,  -3276,  16383,  0,     16383,  -3276,  -16383, 0,     -16383, -3276,  -16383, -32767, -16383, 3276,
                                           -16383, -32767, 16383,  3276,  -16383, -32767, 16383,  -3276, -16383, -32767, -16383, 3276,   16383,  32767,
                                           -16383, -3276,  16383,  32767, 16383,  -3276,  16383,  32767, 16383,  3276,   16383,  32767};
static int16_t  mesh__RotateY_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                           0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                           0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__RotateY_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                           12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__RotateZ_pos[]     = {16383,  -16383, -3276,  0,      16383,  -16383, 3276,   0,      -16383, -16383, 3276,   0,      -16383, -16383,
                                           -3276,  0,      -16383, -16383, -3276,  0,      -16383, -16383, 3276,   0,      -16383, 16383,  3276,   0,
                                           -16383, 16383,  -3276,  0,      -16383, 16383,  -3276,  0,      -16383, 16383,  3276,   0,      16383,  16383,
                                           3276,   0,      16383,  16383,  -3276,  0,      16383,  16383,  -3276,  0,      16383,  16383,  3276,   0,
                                           16383,  -16383, 3276,   0,      16383,  -16383, -3276,  0,      -16383, -16383, -3276,  -32767, -16383, 16383,
                                           -3276,  -32767, 16383,  16383,  -3276,  -32767, 16383,  -16383, -3276,  -32767, -16383, 16383,  3276,   32767,
                                           -16383, -16383, 3276,   32767,  16383,  -16383, 3276,   32767,  16383,  16383,  3276,   32767};
static int16_t  mesh__RotateZ_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                           0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                           0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__RotateZ_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                           12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__ScaleX_pos[]     = {-5898, -3276, -3276, 0,      -5898, -3276, 3276,  0,      -5898, 3276,  3276,  0,      -5898, 3276,  -3276, 0,
                                          -5898, 3276,  -3276, 0,      -5898, 3276,  3276,  0,      5898,  3276,  3276,  0,      5898,  3276,  -3276, 0,
                                          5898,  3276,  -3276, 0,      5898,  3276,  3276,  0,      5898,  -3276, 3276,  0,      5898,  -3276, -3276, 0,
                                          5898,  -3276, -3276, 0,      5898,  -3276, 3276,  0,      -5898, -3276, 3276,  0,      -5898, -3276, -3276, 0,
                                          -5898, 3276,  -3276, -32767, 5898,  3276,  -3276, -32767, 5898,  -3276, -3276, -32767, -5898, -3276, -3276, -32767,
                                          5898,  3276,  3276,  32767,  -5898, 3276,  3276,  32767,  -5898, -3276, 3276,  32767,  5898,  -3276, 3276,  32767};
static int16_t  mesh__ScaleX_nor[]     = {-32767, 0, -32767, 0, -32767, 0, -32767, 0, 0, 32767,  0, 32767,  0, 32767,  0, 32767,
                                          32767,  0, 32767,  0, 32767,  0, 32767,  0, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
                                          0,      0, 0,      0, 0,      0, 0,      0, 0, 0,      0, 0,      0, 0,      0, 0};
static uint32_t mesh__ScaleX_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                          12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__ScaleY_pos[]     = {3276,  -5898, -3276, 0,      3276,  -5898, 3276,  0,      -3276, -5898, 3276,  0,      -3276, -5898, -3276, 0,
                                          -3276, -5898, -3276, 0,      -3276, -5898, 3276,  0,      -3276, 5898,  3276,  0,      -3276, 5898,  -3276, 0,
                                          -3276, 5898,  -3276, 0,      -3276, 5898,  3276,  0,      3276,  5898,  3276,  0,      3276,  5898,  -3276, 0,
                                          3276,  5898,  -3276, 0,      3276,  5898,  3276,  0,      3276,  -5898, 3276,  0,      3276,  -5898, -3276, 0,
                                          -3276, -5898, -3276, -32767, -3276, 5898,  -3276, -32767, 3276,  5898,  -3276, -32767, 3276,  -5898, -3276, -32767,
                                          -3276, 5898,  3276,  32767,  -3276, -5898, 3276,  32767,  3276,  -5898, 3276,  32767,  3276,  5898,  3276,  32767};
static int16_t  mesh__ScaleY_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                          0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                          0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__ScaleY_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                          12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__ScaleZ_pos[]     = {-3276, -3276, 5898,  32767,  3276,  -3276, 5898,  32767,  3276,  3276,  5898,  32767,  -3276, 3276,  5898,  32767,
                                          -3276, 3276,  5898,  0,      3276,  3276,  5898,  0,      3276,  3276,  -5898, 0,      -3276, 3276,  -5898, 0,
                                          -3276, 3276,  -5898, -32767, 3276,  3276,  -5898, -32767, 3276,  -3276, -5898, -32767, -3276, -3276, -5898, -32767,
                                          -3276, -3276, -5898, 0,      3276,  -3276, -5898, 0,      3276,  -3276, 5898,  0,      -3276, -3276, 5898,  0,
                                          -3276, 3276,  5898,  0,      -3276, 3276,  -5898, 0,      -3276, -3276, -5898, 0,      -3276, -3276, 5898,  0,
                                          3276,  3276,  -5898, 0,      3276,  3276,  5898,  0,      3276,  -3276, 5898,  0,      3276,  -3276, -5898, 0};
static int16_t  mesh__ScaleZ_nor[]     = {0,      0, 0,      0, 0,      0, 0,      0, 0,     32767,  0,     32767,  0,     32767,  0,     32767,
                                          0,      0, 0,      0, 0,      0, 0,      0, 0,     -32767, 0,     -32767, 0,     -32767, 0,     -32767,
                                          -32767, 0, -32767, 0, -32767, 0, -32767, 0, 32767, 0,      32767, 0,      32767, 0,      32767, 0};
static uint32_t mesh__ScaleZ_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                          12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__TranslateX_pos[]     = {-11796, -3276, -3276, 0,      -11796, -3276, 3276,  0,      -11796, 3276,  3276,  0,      -11796, 3276,  -3276, 0,
                                              -11796, 3276,  -3276, 0,      -11796, 3276,  3276,  0,      11796,  3276,  3276,  0,      11796,  3276,  -3276, 0,
                                              11796,  3276,  -3276, 0,      11796,  3276,  3276,  0,      11796,  -3276, 3276,  0,      11796,  -3276, -3276, 0,
                                              11796,  -3276, -3276, 0,      11796,  -3276, 3276,  0,      -11796, -3276, 3276,  0,      -11796, -3276, -3276, 0,
                                              -11796, 3276,  -3276, -32767, 11796,  3276,  -3276, -32767, 11796,  -3276, -3276, -32767, -11796, -3276, -3276, -32767,
                                              11796,  3276,  3276,  32767,  -11796, 3276,  3276,  32767,  -11796, -3276, 3276,  32767,  11796,  -3276, 3276,  32767};
static int16_t  mesh__TranslateX_nor[]     = {-32767, 0, -32767, 0, -32767, 0, -32767, 0, 0, 32767,  0, 32767,  0, 32767,  0, 32767,
                                              32767,  0, 32767,  0, 32767,  0, 32767,  0, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
                                              0,      0, 0,      0, 0,      0, 0,      0, 0, 0,      0, 0,      0, 0,      0, 0};
static uint32_t mesh__TranslateX_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__TranslateY_pos[]     = {3276,  -11796, -3276, 0,      3276,  -11796, 3276,  0,      -3276, -11796, 3276,  0,      -3276, -11796, -3276, 0,
                                              -3276, -11796, -3276, 0,      -3276, -11796, 3276,  0,      -3276, 11796,  3276,  0,      -3276, 11796,  -3276, 0,
                                              -3276, 11796,  -3276, 0,      -3276, 11796,  3276,  0,      3276,  11796,  3276,  0,      3276,  11796,  -3276, 0,
                                              3276,  11796,  -3276, 0,      3276,  11796,  3276,  0,      3276,  -11796, 3276,  0,      3276,  -11796, -3276, 0,
                                              -3276, -11796, -3276, -32767, -3276, 11796,  -3276, -32767, 3276,  11796,  -3276, -32767, 3276,  -11796, -3276, -32767,
                                              -3276, 11796,  3276,  32767,  -3276, -11796, 3276,  32767,  3276,  -11796, 3276,  32767,  3276,  11796,  3276,  32767};
static int16_t  mesh__TranslateY_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                              0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                              0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__TranslateY_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__TranslateZ_pos[] = {-3276, -3276, 11796,  32767,  3276,  -3276, 11796,  32767,  3276,  3276,  11796,  32767,  -3276, 3276,  11796,  32767,
                                          -3276, 3276,  11796,  0,      3276,  3276,  11796,  0,      3276,  3276,  -11796, 0,      -3276, 3276,  -11796, 0,
                                          -3276, 3276,  -11796, -32767, 3276,  3276,  -11796, -32767, 3276,  -3276, -11796, -32767, -3276, -3276, -11796, -32767,
                                          -3276, -3276, -11796, 0,      3276,  -3276, -11796, 0,      3276,  -3276, 11796,  0,      -3276, -3276, 11796,  0,
                                          -3276, 3276,  11796,  0,      -3276, 3276,  -11796, 0,      -3276, -3276, -11796, 0,      -3276, -3276, 11796,  0,
                                          3276,  3276,  -11796, 0,      3276,  3276,  11796,  0,      3276,  -3276, 11796,  0,      3276,  -3276, -11796, 0};
static int16_t  mesh__TranslateZ_nor[] = {0,      0, 0,      0, 0,      0, 0,      0, 0,     32767,  0,     32767,  0,     32767,  0,     32767,
                                          0,      0, 0,      0, 0,      0, 0,      0, 0,     -32767, 0,     -32767, 0,     -32767, 0,     -32767,
                                          -32767, 0, -32767, 0, -32767, 0, -32767, 0, 32767, 0,      32767, 0,      32767, 0,      32767, 0};
static uint32_t mesh__TranslateZ_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__Plane_pos[]     = {-16383, 0, -16383, 0, 16383, 0, -16383, 0, 16383, 0, 16383, 0, -16383, 0, 16383, 0};
static int16_t  mesh__Plane_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767};
static int16_t  mesh__Plane_tex[]     = {3, 32763, 32763, 32763, 32763, 3, 3, 3};
static uint32_t mesh__Plane_indices[] = {3, 0, 1, 3, 1, 2};

scene_t *startup_get_scene(void) {
	scene_t *scene    = (scene_t *)calloc(1, sizeof(scene_t));
	scene->name       = "Scene";
	scene->camera_ref = "Camera";
	scene->world_ref  = "World";

	// mesh_datas
	scene->mesh_datas            = any_array_create(13);
	scene->mesh_datas->buffer[0] = startup_make_compass();
	scene->mesh_datas->buffer[1] = startup_make_gizmo();
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".RotateX";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__RotateX_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__RotateX_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__RotateX_indices, 36);
		scene->mesh_datas->buffer[2] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".RotateY";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__RotateY_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__RotateY_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__RotateY_indices, 36);
		scene->mesh_datas->buffer[3] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".RotateZ";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__RotateZ_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__RotateZ_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__RotateZ_indices, 36);
		scene->mesh_datas->buffer[4] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".ScaleX";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__ScaleX_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__ScaleX_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__ScaleX_indices, 36);
		scene->mesh_datas->buffer[5] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".ScaleY";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__ScaleY_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__ScaleY_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__ScaleY_indices, 36);
		scene->mesh_datas->buffer[6] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".ScaleZ";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__ScaleZ_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__ScaleZ_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__ScaleZ_indices, 36);
		scene->mesh_datas->buffer[7] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".TranslateX";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__TranslateX_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__TranslateX_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__TranslateX_indices, 36);
		scene->mesh_datas->buffer[8] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".TranslateY";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__TranslateY_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__TranslateY_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array              = u32_array_create_from_raw(mesh__TranslateY_indices, 36);
		scene->mesh_datas->buffer[9] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".TranslateZ";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(2);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__TranslateZ_pos, 96);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__TranslateZ_nor, 48);
			md->vertex_arrays->buffer[1] = vat;
		}
		md->index_array               = u32_array_create_from_raw(mesh__TranslateZ_indices, 36);
		scene->mesh_datas->buffer[10] = md;
	}
	{
		mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
		md->name          = ".Plane";
		md->scale_pos     = 1.0f;
		md->scale_tex     = 1.0f;
		md->vertex_arrays = any_array_create(3);
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "pos";
			vat->data                    = "short4norm";
			vat->values                  = i16_array_create_from_raw(mesh__Plane_pos, 16);
			md->vertex_arrays->buffer[0] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "nor";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__Plane_nor, 8);
			md->vertex_arrays->buffer[1] = vat;
		}
		{
			vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
			vat->attrib                  = "tex";
			vat->data                    = "short2norm";
			vat->values                  = i16_array_create_from_raw(mesh__Plane_tex, 8);
			md->vertex_arrays->buffer[2] = vat;
		}
		md->index_array               = u32_array_create_from_raw(mesh__Plane_indices, 6);
		scene->mesh_datas->buffer[11] = md;
	}
	scene->mesh_datas->buffer[12] = startup_make_sphere();

	// camera_datas
	scene->camera_datas = any_array_create(1);
	{
		camera_data_t *cd              = (camera_data_t *)calloc(1, sizeof(camera_data_t));
		cd->name                       = "Camera";
		cd->near_plane                 = 0.009999999776482582f;
		cd->far_plane                  = 100.00006103515625f;
		cd->fov                        = 0.6909999847412109f;
		cd->aspect                     = 0.0f;
		cd->frustum_culling            = false;
		scene->camera_datas->buffer[0] = cd;
	}

	// world_datas
	scene->world_datas = any_array_create(1);
	{
		world_data_t *wd              = (world_data_t *)calloc(1, sizeof(world_data_t));
		wd->name                      = "World";
		wd->color                     = -15987700;
		wd->strength                  = 1.0f;
		wd->irradiance                = NULL; // Generated in util_render_make_default_world()
		wd->radiance                  = NULL;
		wd->radiance_mipmaps          = 0;
		wd->envmap                    = NULL;
		scene->world_datas->buffer[0] = wd;
	}

	// shader_datas
	scene->shader_datas = any_array_create(17);
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "armdefault";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "mesh";
			sc->depth_write                  = true;
			sc->compare_mode                 = "less";
			sc->cull_mode                    = "clockwise";
			sc->vertex_shader                = "mesh_posnor.vert";
			sc->fragment_shader              = "mesh_posnor.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(3);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->color_attachments->buffer[1] = "RGBA64";
			sc->color_attachments->buffer[2] = "RGBA64";
			sc->depth_attachment             = "D32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(2);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "short4norm";
				sc->vertex_elements->buffer[0] = ve;
			}
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "nor";
				ve->data                       = "short2norm";
				sc->vertex_elements->buffer[1] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(2);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "WVP";
				c->type                  = "float4x4";
				c->link                  = "_world_view_proj_matrix";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "N";
				c->type                  = "float3x3";
				c->link                  = "_normal_matrix";
				sc->constants->buffer[1] = c;
			}
			sc->texture_units       = (tex_unit_t_array_t *)any_array_create(0);
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[0] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = ".Gizmo";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "overlay";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "clockwise";
			sc->vertex_shader                = "mesh_poscol.vert";
			sc->fragment_shader              = "mesh_poscol.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(2);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "short4norm";
				sc->vertex_elements->buffer[0] = ve;
			}
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "col";
				ve->data                       = "short4norm";
				sc->vertex_elements->buffer[1] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "WVP";
				c->type                  = "float4x4";
				c->link                  = "_world_view_proj_matrix";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units       = (tex_unit_t_array_t *)any_array_create(0);
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[1] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "Material";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "mesh";
			sc->depth_write                  = true;
			sc->compare_mode                 = "less";
			sc->cull_mode                    = "clockwise";
			sc->vertex_shader                = "mesh_posnortex.vert";
			sc->fragment_shader              = "mesh_posnortex.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(3);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->color_attachments->buffer[1] = "RGBA64";
			sc->color_attachments->buffer[2] = "RGBA64";
			sc->depth_attachment             = "D32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(3);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "short4norm";
				sc->vertex_elements->buffer[0] = ve;
			}
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "nor";
				ve->data                       = "short2norm";
				sc->vertex_elements->buffer[1] = ve;
			}
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "tex";
				ve->data                       = "short2norm";
				sc->vertex_elements->buffer[2] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(3);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "WVP";
				c->type                  = "float4x4";
				c->link                  = "_world_view_proj_matrix";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "N";
				c->type                  = "float3x3";
				c->link                  = "_normal_matrix";
				sc->constants->buffer[1] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "tex_unpack";
				c->type                  = "float";
				c->link                  = "_tex_unpack";
				sc->constants->buffer[2] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "ImageTexture";
				sc->texture_units->buffer[0] = tu;
			}
			sc->bind_textures = (bind_tex_t_array_t *)any_array_create(1);
			{
				bind_tex_t *bt               = (bind_tex_t *)calloc(1, sizeof(bind_tex_t));
				bt->name                     = string_copy("ImageTexture");
				bt->file                     = string_copy("");
				sc->bind_textures->buffer[0] = bt;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[2] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "compositor_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "compositor_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "compositor_pass.vert";
			sc->fragment_shader              = "compositor_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(6);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "vignette_strength";
				c->type                  = "float";
				c->link                  = "_vignette_strength";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "grain_strength";
				c->type                  = "float";
				c->link                  = "_grain_strength";
				sc->constants->buffer[1] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "tonemap_strength";
				c->type                  = "float";
				c->link                  = "_tonemap_strength";
				sc->constants->buffer[2] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "lut_size";
				c->type                  = "float";
				c->link                  = "_lut_size";
				sc->constants->buffer[3] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "contrast_strength";
				c->type                  = "float";
				c->link                  = "_contrast_strength";
				sc->constants->buffer[4] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "gamma_strength";
				c->type                  = "float";
				c->link                  = "_gamma_strength";
				sc->constants->buffer[5] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "lut_tex";
				tu->link                     = "_lut_tex";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[3] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "taa_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "taa_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "taa_pass.vert";
			sc->fragment_shader              = "taa_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "taa_blend";
				c->type                  = "float";
				c->link                  = "_taa_blend";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex2";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[4] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "copy_pass";
		sd->contexts      = any_array_create(5);
		{
			shader_context_t *sc   = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name               = "copy_pass";
			sc->depth_write        = false;
			sc->compare_mode       = "always";
			sc->cull_mode          = "none";
			sc->vertex_shader      = "copy_pass.vert";
			sc->fragment_shader    = "copy_pass.frag";
			sc->shader_from_source = false;
			sc->vertex_elements    = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copyR8_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_pass.vert";
			sc->fragment_shader              = "copy_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "R8";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[1] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copyR32_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_pass.vert";
			sc->fragment_shader              = "copy_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "R32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[2] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copyRGBA64_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_pass.vert";
			sc->fragment_shader              = "copy_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[3] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copyRGBA128_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_pass.vert";
			sc->fragment_shader              = "copy_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA128";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[4] = sc;
		}
		scene->shader_datas->buffer[5] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "supersample_resolve";
		sd->contexts      = any_array_create(2);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "supersample_resolve";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "supersample_resolve.vert";
			sc->fragment_shader              = "supersample_resolve.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "screen_size_inv";
				c->type                  = "float2";
				c->link                  = "_screen_size_inv";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "supersample_resolveRGBA64";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "supersample_resolve.vert";
			sc->fragment_shader              = "supersample_resolve.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "screen_size_inv";
				c->type                  = "float2";
				c->link                  = "_screen_size_inv";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[1] = sc;
		}
		scene->shader_datas->buffer[6] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "depth_to_ao_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "depth_to_ao_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "depth_to_ao_pass.vert";
			sc->fragment_shader              = "depth_to_ao_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "height_map";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "normal_map";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[7] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "ssao_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "ssao_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "ssao_pass.vert";
			sc->fragment_shader              = "ssao_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "R8";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(6);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "invP";
				c->type                  = "float4x4";
				c->link                  = "_inv_proj_matrix";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "V3";
				c->type                  = "float3x3";
				c->link                  = "_view_matrix3";
				sc->constants->buffer[1] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "camera_proj";
				c->type                  = "float2";
				c->link                  = "_camera_plane_proj";
				sc->constants->buffer[2] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "screen_size_inv";
				c->type                  = "float2";
				c->link                  = "_screen_size_inv";
				sc->constants->buffer[3] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "frame_offset";
				c->type                  = "float";
				c->link                  = "_ssao_frame";
				sc->constants->buffer[4] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "ssao_strength";
				c->type                  = "float";
				c->link                  = "_ssao_strength";
				sc->constants->buffer[5] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbufferD";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbuffer0";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[8] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "ssao_blur_pass";
		sd->contexts      = any_array_create(2);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "ssao_blur_pass_x";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "ssao_blur_pass.vert";
			sc->fragment_shader              = "ssao_blur_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "R8";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "dir_inv";
				c->type                  = "float2";
				c->link                  = "_vec2x_inv";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbuffer0";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "ssao_blur_pass_y";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "ssao_blur_pass.vert";
			sc->fragment_shader              = "ssao_blur_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "R8";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "dir_inv";
				c->type                  = "float2";
				c->link                  = "_vec2y_inv";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbuffer0";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[1] = sc;
		}
		scene->shader_datas->buffer[9] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "bloom_pass";
		sd->contexts      = any_array_create(2);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "bloom_downsample_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "bloom_downsample_pass.vert";
			sc->fragment_shader              = "bloom_downsample_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(2);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "screen_size_inv";
				c->type                  = "float2";
				c->link                  = "_screen_size_inv";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "current_mip_level";
				c->type                  = "int";
				c->link                  = "_bloom_current_mip";
				sc->constants->buffer[1] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "bloom_upsample_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "bloom_upsample_pass.vert";
			sc->fragment_shader              = "bloom_upsample_pass.frag";
			sc->shader_from_source           = false;
			sc->blend_source                 = "blend_one";
			sc->blend_destination            = "blend_one";
			sc->alpha_blend_source           = "blend_zero";
			sc->alpha_blend_destination      = "blend_one";
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(4);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "screen_size_inv";
				c->type                  = "float2";
				c->link                  = "_screen_size_inv";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "current_mip_level";
				c->type                  = "int";
				c->link                  = "_bloom_current_mip";
				sc->constants->buffer[1] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "sample_scale";
				c->type                  = "float";
				c->link                  = "_bloom_sample_scale";
				sc->constants->buffer[2] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "bloom_strength";
				c->type                  = "float";
				c->link                  = "_bloom_strength";
				sc->constants->buffer[3] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[1] = sc;
		}
		scene->shader_datas->buffer[10] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "copy_mrt3_pass";
		sd->contexts      = any_array_create(3);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copy_mrt3_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_mrt3_pass.vert";
			sc->fragment_shader              = "copy_mrt3_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(3);
			sc->color_attachments->buffer[0] = "RGBA32";
			sc->color_attachments->buffer[1] = "RGBA32";
			sc->color_attachments->buffer[2] = "RGBA32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(3);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex0";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex1";
				sc->texture_units->buffer[1] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex2";
				sc->texture_units->buffer[2] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copy_mrt3RGBA64_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_mrt3_pass.vert";
			sc->fragment_shader              = "copy_mrt3_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(3);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->color_attachments->buffer[1] = "RGBA64";
			sc->color_attachments->buffer[2] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(3);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex0";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex1";
				sc->texture_units->buffer[1] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex2";
				sc->texture_units->buffer[2] = tu;
			}
			sd->contexts->buffer[1] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "copy_mrt3RGBA128_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "copy_mrt3_pass.vert";
			sc->fragment_shader              = "copy_mrt3_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(3);
			sc->color_attachments->buffer[0] = "RGBA128";
			sc->color_attachments->buffer[1] = "RGBA128";
			sc->color_attachments->buffer[2] = "RGBA128";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(3);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex0";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex1";
				sc->texture_units->buffer[1] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex2";
				sc->texture_units->buffer[2] = tu;
			}
			sd->contexts->buffer[2] = sc;
		}
		scene->shader_datas->buffer[11] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "dilate_pass";
		sd->contexts      = any_array_create(3);
		{
			shader_context_t *sc   = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name               = "dilate_pass";
			sc->depth_write        = false;
			sc->compare_mode       = "always";
			sc->cull_mode          = "none";
			sc->vertex_shader      = "dilate_pass.vert";
			sc->fragment_shader    = "dilate_pass.frag";
			sc->shader_from_source = false;
			sc->vertex_elements    = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(2);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "dilate_radius";
				c->type                  = "float";
				c->link                  = "_dilate_radius";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "tex_size";
				c->type                  = "float2";
				c->link                  = "_size(tex)";
				sc->constants->buffer[1] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "texdilate";
				tu->link                     = "_texdilatemap";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "dilateRGBA64_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "dilate_pass.vert";
			sc->fragment_shader              = "dilate_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(2);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "dilate_radius";
				c->type                  = "float";
				c->link                  = "_dilate_radius";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "tex_size";
				c->type                  = "float2";
				c->link                  = "_size(tex)";
				sc->constants->buffer[1] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "texdilate";
				tu->link                     = "_texdilatemap";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[1] = sc;
		}
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "dilateRGBA128_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "dilate_pass.vert";
			sc->fragment_shader              = "dilate_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA128";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(2);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "dilate_radius";
				c->type                  = "float";
				c->link                  = "_dilate_radius";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "tex_size";
				c->type                  = "float2";
				c->link                  = "_size(tex)";
				sc->constants->buffer[1] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(2);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "texdilate";
				tu->link                     = "_texdilatemap";
				sc->texture_units->buffer[1] = tu;
			}
			sd->contexts->buffer[2] = sc;
		}
		scene->shader_datas->buffer[12] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "histogram_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "histogram_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "histogram_pass.vert";
			sc->fragment_shader              = "histogram_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants     = (shader_const_t_array_t *)any_array_create(0);
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "tex";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[13] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "deferred_light";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "deferred_light";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "deferred_light.vert";
			sc->fragment_shader              = "deferred_light.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(13);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "invVP";
				c->type                  = "float4x4";
				c->link                  = "_inv_view_proj_matrix";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "eye";
				c->type                  = "float3";
				c->link                  = "_camera_pos";
				sc->constants->buffer[1] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "envmap_data";
				c->type                  = "float4";
				c->link                  = "_envmap_data";
				sc->constants->buffer[2] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "envmap_num_mipmaps";
				c->type                  = "int";
				c->link                  = "_envmap_num_mipmaps";
				sc->constants->buffer[3] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "camera_proj";
				c->type                  = "float2";
				c->link                  = "_camera_plane_proj";
				sc->constants->buffer[4] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "eye_look";
				c->type                  = "float3";
				c->link                  = "_camera_look";
				sc->constants->buffer[5] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "shirr0";
				c->type                  = "float4";
				c->link                  = "_envmap_irradiance0";
				sc->constants->buffer[6] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "shirr1";
				c->type                  = "float4";
				c->link                  = "_envmap_irradiance1";
				sc->constants->buffer[7] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "shirr2";
				c->type                  = "float4";
				c->link                  = "_envmap_irradiance2";
				sc->constants->buffer[8] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "shirr3";
				c->type                  = "float4";
				c->link                  = "_envmap_irradiance3";
				sc->constants->buffer[9] = c;
			}
			{
				shader_const_t *c         = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                   = "shirr4";
				c->type                   = "float4";
				c->link                   = "_envmap_irradiance4";
				sc->constants->buffer[10] = c;
			}
			{
				shader_const_t *c         = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                   = "shirr5";
				c->type                   = "float4";
				c->link                   = "_envmap_irradiance5";
				sc->constants->buffer[11] = c;
			}
			{
				shader_const_t *c         = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                   = "shirr6";
				c->type                   = "float4";
				c->link                   = "_envmap_irradiance6";
				sc->constants->buffer[12] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(10);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbufferD";
				sc->texture_units->buffer[0] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbuffer0";
				sc->texture_units->buffer[1] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "gbuffer1";
				sc->texture_units->buffer[2] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "senvmap_radiance";
				tu->link                     = "_envmap_radiance";
				sc->texture_units->buffer[3] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "senvmap_radiance0";
				tu->link                     = "_envmap_radiance0";
				sc->texture_units->buffer[4] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "senvmap_radiance1";
				tu->link                     = "_envmap_radiance1";
				sc->texture_units->buffer[5] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "senvmap_radiance2";
				tu->link                     = "_envmap_radiance2";
				sc->texture_units->buffer[6] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "senvmap_radiance3";
				tu->link                     = "_envmap_radiance3";
				sc->texture_units->buffer[7] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "senvmap_radiance4";
				tu->link                     = "_envmap_radiance4";
				sc->texture_units->buffer[8] = tu;
			}
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "ssaotex";
				sc->texture_units->buffer[9] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[14] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "world_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "world_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "less";
			sc->cull_mode                    = "clockwise";
			sc->vertex_shader                = "world_pass.vert";
			sc->fragment_shader              = "world_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA64";
			sc->depth_attachment             = "D32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(2);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float3";
				sc->vertex_elements->buffer[0] = ve;
			}
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "nor";
				ve->data                       = "float3";
				sc->vertex_elements->buffer[1] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(2);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "SMVP";
				c->type                  = "float4x4";
				c->link                  = "_skydome_matrix";
				sc->constants->buffer[0] = c;
			}
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "envmap_data_world";
				c->type                  = "float4";
				c->link                  = "_envmap_data_world";
				sc->constants->buffer[1] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "envmap";
				tu->link                     = "_envmap";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[15] = sd;
	}
	{
		shader_data_t *sd = (shader_data_t *)calloc(1, sizeof(shader_data_t));
		sd->name          = "depth_to_normal_pass";
		sd->contexts      = any_array_create(1);
		{
			shader_context_t *sc             = (shader_context_t *)calloc(1, sizeof(shader_context_t));
			sc->name                         = "depth_to_normal_pass";
			sc->depth_write                  = false;
			sc->compare_mode                 = "always";
			sc->cull_mode                    = "none";
			sc->vertex_shader                = "depth_to_normal_pass.vert";
			sc->fragment_shader              = "depth_to_normal_pass.frag";
			sc->shader_from_source           = false;
			sc->color_attachments            = string_array_create(1);
			sc->color_attachments->buffer[0] = "RGBA32";
			sc->vertex_elements              = (vertex_element_t_array_t *)any_array_create(1);
			{
				vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
				ve->name                       = "pos";
				ve->data                       = "float2";
				sc->vertex_elements->buffer[0] = ve;
			}
			sc->constants = (shader_const_t_array_t *)any_array_create(1);
			{
				shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
				c->name                  = "texel_size";
				c->type                  = "float2";
				c->link                  = "_screen_size_inv";
				sc->constants->buffer[0] = c;
			}
			sc->texture_units = (tex_unit_t_array_t *)any_array_create(1);
			{
				tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
				tu->name                     = "height_map";
				sc->texture_units->buffer[0] = tu;
			}
			sd->contexts->buffer[0] = sc;
		}
		scene->shader_datas->buffer[16] = sd;
	}

	// objects
	scene->objects = any_array_create(5);
	{
		object_desc_t *obj0 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
		obj0->name          = ".Compass";
		obj0->type          = "mesh_object";
		obj0->data_ref      = ".Compass";
		obj0->transform =
		    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 16);
		obj0->dimensions          = f32_array_create_from_raw((float[]){2.0f, 2.0f, 2.0f}, 3);
		obj0->visible             = false;
		obj0->spawn               = true;
		obj0->material_ref        = ".Gizmo";
		scene->objects->buffer[0] = obj0;
	}
	{
		object_desc_t *obj1 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
		obj1->name          = ".Gizmo";
		obj1->type          = "mesh_object";
		obj1->data_ref      = ".Gizmo";
		obj1->transform =
		    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 16);
		obj1->dimensions              = f32_array_create_from_raw((float[]){2.1500000953674316f, 2.1500000953674316f, 2.1500000953674316f}, 3);
		obj1->visible                 = false;
		obj1->spawn                   = true;
		obj1->material_ref            = ".Gizmo";
		object_desc_t_array_t *obj1ch = (object_desc_t_array_t *)any_array_create(9);
		obj1->children                = obj1ch;
		{
			object_desc_t *obj2 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj2->name          = ".RotateX";
			obj2->type          = "mesh_object";
			obj2->data_ref      = ".RotateX";
			obj2->transform =
			    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.75f, 0.75f, 1.0f}, 16);
			obj2->dimensions   = f32_array_create_from_raw((float[]){0.20000000298023224f, 1.0f, 1.0f}, 3);
			obj2->visible      = false;
			obj2->spawn        = true;
			obj2->material_ref = "armdefault";
			obj1ch->buffer[0]  = obj2;
		}
		{
			object_desc_t *obj3 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj3->name          = ".RotateY";
			obj3->type          = "mesh_object";
			obj3->data_ref      = ".RotateY";
			obj3->transform =
			    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.75f, 0.0f, 0.75f, 1.0f}, 16);
			obj3->dimensions   = f32_array_create_from_raw((float[]){1.0f, 0.20000000298023224f, 1.0f}, 3);
			obj3->visible      = false;
			obj3->spawn        = true;
			obj3->material_ref = "armdefault";
			obj1ch->buffer[1]  = obj3;
		}
		{
			object_desc_t *obj4 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj4->name          = ".RotateZ";
			obj4->type          = "mesh_object";
			obj4->data_ref      = ".RotateZ";
			obj4->transform =
			    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.75f, 0.75f, 0.0f, 1.0f}, 16);
			obj4->dimensions   = f32_array_create_from_raw((float[]){1.0f, 1.0f, 0.20000000298023224f}, 3);
			obj4->visible      = false;
			obj4->spawn        = true;
			obj4->material_ref = "armdefault";
			obj1ch->buffer[2]  = obj4;
		}
		{
			object_desc_t *obj5 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj5->name          = ".ScaleX";
			obj5->type          = "mesh_object";
			obj5->data_ref      = ".ScaleX";
			obj5->transform    = f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.949999988079071f,
			                                                         0.0f, 0.0f, 1.0f},
			                                               16);
			obj5->dimensions   = f32_array_create_from_raw((float[]){0.36000001430511475f, 0.20000000298023224f, 0.20000000298023224f}, 3);
			obj5->visible      = false;
			obj5->spawn        = true;
			obj5->material_ref = "armdefault";
			obj1ch->buffer[3]  = obj5;
		}
		{
			object_desc_t *obj6 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj6->name          = ".ScaleY";
			obj6->type          = "mesh_object";
			obj6->data_ref      = ".ScaleY";
			obj6->transform     = f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
			                                                          0.949999988079071f, 0.0f, 1.0f},
			                                                16);
			obj6->dimensions    = f32_array_create_from_raw((float[]){0.20000000298023224f, 0.36000001430511475f, 0.20000000298023224f}, 3);
			obj6->visible       = false;
			obj6->spawn         = true;
			obj6->material_ref  = "armdefault";
			obj1ch->buffer[4]   = obj6;
		}
		{
			object_desc_t *obj7 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj7->name          = ".ScaleZ";
			obj7->type          = "mesh_object";
			obj7->data_ref      = ".ScaleZ";
			obj7->transform     = f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
			                                                          0.949999988079071f, 1.0f},
			                                                16);
			obj7->dimensions    = f32_array_create_from_raw((float[]){0.20000000298023224f, 0.20000000298023224f, 0.36000001430511475f}, 3);
			obj7->visible       = false;
			obj7->spawn         = true;
			obj7->material_ref  = "armdefault";
			obj1ch->buffer[5]   = obj7;
		}
		{
			object_desc_t *obj8 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj8->name          = ".TranslateX";
			obj8->type          = "mesh_object";
			obj8->data_ref      = ".TranslateX";
			obj8->transform  = f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.7000000476837158f,
			                                                       0.0f, 0.0f, 1.0f},
			                                             16);
			obj8->dimensions = f32_array_create_from_raw((float[]){0.7200000286102295f, 0.20000000298023224f, 0.20000000298023224f}, 3);
			obj8->visible    = false;
			obj8->spawn      = true;
			obj8->material_ref = "armdefault";
			obj1ch->buffer[6]  = obj8;
		}
		{
			object_desc_t *obj9 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj9->name          = ".TranslateY";
			obj9->type          = "mesh_object";
			obj9->data_ref      = ".TranslateY";
			obj9->transform     = f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
			                                                          1.7000000476837158f, 0.0f, 1.0f},
			                                                16);
			obj9->dimensions    = f32_array_create_from_raw((float[]){0.20000000298023224f, 0.7200000286102295f, 0.20000000298023224f}, 3);
			obj9->visible       = false;
			obj9->spawn         = true;
			obj9->material_ref  = "armdefault";
			obj1ch->buffer[7]   = obj9;
		}
		{
			object_desc_t *obj10 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
			obj10->name          = ".TranslateZ";
			obj10->type          = "mesh_object";
			obj10->data_ref      = ".TranslateZ";
			obj10->transform     = f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
			                                                           1.7000000476837158f, 1.0f},
			                                                 16);
			obj10->dimensions    = f32_array_create_from_raw((float[]){0.20000000298023224f, 0.20000000298023224f, 0.7200000286102295f}, 3);
			obj10->visible       = false;
			obj10->spawn         = true;
			obj10->material_ref  = "armdefault";
			obj1ch->buffer[8]    = obj10;
		}
		scene->objects->buffer[1] = obj1;
	}
	{
		object_desc_t *obj11 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
		obj11->name          = "Camera";
		obj11->type          = "camera_object";
		obj11->data_ref      = "Camera";
		obj11->transform     = f32_array_create_from_raw((float[]){0.8660249710083008f, 0.5f, -0.0f, 0.0f, 0.0f, -0.0f, 1.0f, 0.0f, 0.5f, -0.8660249710083008f,
		                                                           -0.0f, 0.0f, 1.649999976158142f, -2.8628199100494385f, -0.0f, 1.0f},
		                                                 16);
		obj11->visible       = true;
		obj11->spawn         = true;
		scene->objects->buffer[2] = obj11;
	}
	{
		object_desc_t *obj12 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
		obj12->name          = ".Plane";
		obj12->type          = "mesh_object";
		obj12->data_ref      = ".Plane";
		obj12->transform =
		    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 16);
		obj12->dimensions         = f32_array_create_from_raw((float[]){1.0f, 0.0f, 1.0f}, 3);
		obj12->visible            = false;
		obj12->spawn              = true;
		obj12->material_ref       = "Material";
		scene->objects->buffer[3] = obj12;
	}
	{
		object_desc_t *obj13 = (object_desc_t *)calloc(1, sizeof(object_desc_t));
		obj13->name          = ".Sphere";
		obj13->type          = "mesh_object";
		obj13->data_ref      = ".Sphere";
		obj13->transform =
		    f32_array_create_from_raw((float[]){1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 16);
		obj13->dimensions         = f32_array_create_from_raw((float[]){1.0f, 1.0f, 1.0f}, 3);
		obj13->visible            = false;
		obj13->spawn              = true;
		obj13->material_ref       = "Material";
		scene->objects->buffer[4] = obj13;
	}
	return scene;
}
