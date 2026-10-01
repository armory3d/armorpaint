
#include "../global.h"

raw_mesh_t *geom_make_plane(f32 size_x, f32 size_y, i32 verts_x, i32 verts_y, f32 uv_scale) {

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {0});
	mesh->scale_pos  = 1.0;
	mesh->scale_tex  = uv_scale;
	mesh->name       = "Plane";
	mesh->has_next   = false;

	// Pack positions to (-1, 1) range
	f32 half_x      = size_x / 2.0;
	f32 half_y      = size_y / 2.0;
	mesh->scale_pos = math_max(size_x, size_y);
	f32 inv         = (1 / (float)mesh->scale_pos) * 32767;

	mesh->posa = i16_array_create(verts_x * verts_y * 4);
	mesh->nora = i16_array_create(verts_x * verts_y * 2);
	mesh->texa = i16_array_create(verts_x * verts_y * 2);
	mesh->inda = u32_array_create((verts_x - 1) * (verts_y - 1) * 6);
	f32 step_x = size_x / (float)(verts_x - 1);
	f32 step_y = size_y / (float)(verts_y - 1);
	for (i32 i = 0; i < verts_x * verts_y; ++i) {
		f32 x                         = (i % verts_x) * step_x - half_x;
		f32 y                         = math_floor(i / (float)verts_x) * step_y - half_y;
		mesh->posa->buffer[i * 4]     = math_floor(x * inv);
		mesh->posa->buffer[i * 4 + 1] = math_floor(y * inv);
		mesh->posa->buffer[i * 4 + 2] = 0;
		mesh->nora->buffer[i * 2]     = 0;
		mesh->nora->buffer[i * 2 + 1] = 0;
		mesh->posa->buffer[i * 4 + 3] = 32767;
		x                             = (i % verts_x) / (float)(verts_x - 1);
		y                             = 1.0 - math_floor(i / (float)verts_x) / (float)(verts_y - 1);
		mesh->texa->buffer[i * 2]     = math_floor(x * 32767);
		mesh->texa->buffer[i * 2 + 1] = math_floor(y * 32767);
	}
	for (i32 i = 0; i < (verts_x - 1) * (verts_y - 1); ++i) {
		f32 x                         = i % (verts_x - 1);
		f32 y                         = math_floor(i / (float)(verts_y - 1));
		mesh->inda->buffer[i * 6]     = y * verts_x + x;
		mesh->inda->buffer[i * 6 + 1] = y * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 2] = (y + 1) * verts_x + x;
		mesh->inda->buffer[i * 6 + 3] = y * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 4] = (y + 1) * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 5] = (y + 1) * verts_x + x;
	}

	return mesh;
}

raw_mesh_t *geom_make_uv_sphere(f32 radius, i32 width_segments, i32 height_segments, bool stretch_uv, f32 uv_scale) {

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {0});
	mesh->scale_pos  = 1.0;
	mesh->scale_tex  = 1.0;
	mesh->name       = "Sphere";
	mesh->has_next   = false;

	// Pack positions to (-1, 1) range
	mesh->scale_pos = radius;
	mesh->scale_tex = uv_scale;
	f32 inv         = (1 / (float)mesh->scale_pos) * 32767;
	f32 pi2         = math_pi() * 2;

	i32 width_verts  = width_segments + 1;
	i32 height_verts = height_segments + 1;
	mesh->posa       = i16_array_create(width_verts * height_verts * 4);
	mesh->nora       = i16_array_create(width_verts * height_verts * 2);
	mesh->texa       = i16_array_create(width_verts * height_verts * 2);
	mesh->inda       = u32_array_create(width_segments * height_segments * 6 - width_segments * 6);

	vec4_t nor = (vec4_t){0.0, 0.0, 0.0, 1.0};
	i32    pos = 0;
	for (i32 y = 0; y < height_verts; ++y) {
		f32 v      = y / (float)height_segments;
		f32 v_flip = 1.0 - v;
		if (!stretch_uv) {
			v_flip /= 2;
		}
		f32 u_off = y == 0 ? 0.5 / (float)width_segments : y == height_segments ? -0.5 / (float)width_segments : 0.0;
		for (i32 x = 0; x < width_verts; ++x) {
			f32 u                      = x / (float)width_segments;
			f32 u_pi2                  = u * pi2;
			f32 v_pi                   = v * math_pi();
			f32 v_pi_sin               = math_sin(v_pi);
			f32 vx                     = -radius * math_cos(u_pi2) * v_pi_sin;
			f32 vy                     = radius * math_sin(u_pi2) * v_pi_sin;
			f32 vz                     = -radius * math_cos(v_pi);
			i32 i4                     = pos * 4;
			i32 i2                     = pos * 2;
			mesh->posa->buffer[i4]     = math_floor(vx * inv);
			mesh->posa->buffer[i4 + 1] = math_floor(vy * inv);
			mesh->posa->buffer[i4 + 2] = math_floor(vz * inv);
			nor                        = (vec4_t){vx, vy, vz, 1.0};
			nor                        = vec4_norm(nor);
			mesh->posa->buffer[i4 + 3] = math_floor(nor.z * 32767);
			mesh->nora->buffer[i2]     = math_floor(nor.x * 32767);
			mesh->nora->buffer[i2 + 1] = math_floor(nor.y * 32767);
			i32 tx                     = (math_floor((u + u_off) * 32767) - 1);
			i32 ty                     = (math_floor(v_flip * 32767) - 1);
			tx                         = tx < 0 ? 0 : (tx > 32767 ? 32767 : tx);
			ty                         = ty < 0 ? 0 : (ty > 32767 ? 32767 : ty);
			mesh->texa->buffer[i2]     = tx;
			mesh->texa->buffer[i2 + 1] = ty;
			pos++;
		}
	}

	pos                  = 0;
	i32 height_segments1 = height_segments - 1;
	for (i32 y = 0; y < height_segments; ++y) {
		for (i32 x = 0; x < width_segments; ++x) {
			i32 x1 = x + 1;
			i32 y1 = y + 1;
			f32 a  = y * width_verts + x1;
			f32 b  = y * width_verts + x;
			f32 c  = y1 * width_verts + x;
			f32 d  = y1 * width_verts + x1;
			if (y > 0) {
				mesh->inda->buffer[pos++] = a;
				mesh->inda->buffer[pos++] = b;
				mesh->inda->buffer[pos++] = d;
			}
			if (y < height_segments1) {
				mesh->inda->buffer[pos++] = b;
				mesh->inda->buffer[pos++] = c;
				mesh->inda->buffer[pos++] = d;
			}
		}
	}

	return mesh;
}

// Procedural modeling

typedef struct geom {
	f32 *pos; // xyz per vertex
	i32  vcount;
	i32  vcap;
	u32 *ind; // triangles
	i32  icount;
	i32  icap;
	f32 *uv;   // uv per index (face corner), NULL until unwrapped
	f32 *wt;   // 4 bone weights per vertex, NULL until set by geom_weights
	u16 *jt;   // 4 bone indices per vertex
	u8  *part; // color atlas part per triangle, NULL = part 0
} geom_t;

enum {
	GEOM_BOX,
	GEOM_CYLINDER,
	GEOM_ELLIPSOID,
	GEOM_CAPSULE,
	GEOM_TORUS,
	GEOM_ZIGZAG
};
enum {
	GEOM_UNION,
	GEOM_SUBTRACT,
	GEOM_INTERSECT
};

typedef struct geom_prim {
	i32 type;
	i32 op;
	f32 a[9];
	f32 rot[9]; // local to world rotation, row major
	f32 tr[3];  // local to world translation
} geom_prim_t;

static geom_prim_t *geom_prims;
static i32          geom_prim_count;
static f32          geom_voxel;
static f32          geom_rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
static f32          geom_tr[3];
static u8_array_t  *geom_out;
static i32          geom_out_base[3]; // vertices, uvs and normals written so far

static geom_t *geom_create() {
	return calloc(1, sizeof(geom_t));
}

static i32 geom_add_vert(geom_t *g, f32 x, f32 y, f32 z) {
	if (g->vcount == g->vcap) {
		g->vcap = g->vcap == 0 ? 1024 : g->vcap * 2;
		g->pos  = realloc(g->pos, sizeof(f32) * 3 * g->vcap);
	}
	g->pos[g->vcount * 3]     = x;
	g->pos[g->vcount * 3 + 1] = y;
	g->pos[g->vcount * 3 + 2] = z;
	return g->vcount++;
}

static void geom_add_tri(geom_t *g, i32 a, i32 b, i32 c) {
	if (g->icount + 3 > g->icap) {
		g->icap = g->icap == 0 ? 3072 : g->icap * 2;
		g->ind  = realloc(g->ind, sizeof(u32) * g->icap);
	}
	g->ind[g->icount++] = a;
	g->ind[g->icount++] = b;
	g->ind[g->icount++] = c;
}

static void geom_add_quad(geom_t *g, i32 a, i32 b, i32 c, i32 d) {
	geom_add_tri(g, a, b, c);
	geom_add_tri(g, a, c, d);
}

static void geom_rotation(i32 axis, f32 deg, f32 *m) {
	f32 a    = deg * (f32)(3.14159265358979 / 180.0);
	f32 c    = cosf(a);
	f32 s    = sinf(a);
	f32 x[9] = {1, 0, 0, 0, c, -s, 0, s, c};
	f32 y[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
	f32 z[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
	memcpy(m, axis == 0 ? x : axis == 1 ? y : z, sizeof(f32) * 9);
}

// Noise

static u8   geom_perm[512];
static bool geom_perm_ready = false;

static f32 geom_fade(f32 t) {
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static f32 geom_grad(i32 h, f32 x, f32 y, f32 z) {
	h     = h & 15;
	f32 u = h < 8 ? x : y;
	f32 v = h < 4 ? y : (h == 12 || h == 14) ? x : z;
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static f32 geom_lerp(f32 t, f32 a, f32 b) {
	return a + t * (b - a);
}

static f32 geom_noise(f32 x, f32 y, f32 z) {
	if (!geom_perm_ready) {
		u32 s = 1234567;
		for (i32 i = 0; i < 256; ++i) {
			geom_perm[i] = i;
		}
		for (i32 i = 255; i > 0; --i) {
			s            = s * 1664525 + 1013904223;
			i32 j        = (s >> 8) % (i + 1);
			u8  t        = geom_perm[i];
			geom_perm[i] = geom_perm[j];
			geom_perm[j] = t;
		}
		memcpy(geom_perm + 256, geom_perm, 256);
		geom_perm_ready = true;
	}
	f32 fx = floorf(x);
	f32 fy = floorf(y);
	f32 fz = floorf(z);
	i32 X  = (i32)fx & 255;
	i32 Y  = (i32)fy & 255;
	i32 Z  = (i32)fz & 255;
	x -= fx;
	y -= fy;
	z -= fz;
	f32 u  = geom_fade(x);
	f32 v  = geom_fade(y);
	f32 w  = geom_fade(z);
	u8 *p  = geom_perm;
	i32 A  = p[X] + Y;
	i32 AA = p[A] + Z;
	i32 AB = p[A + 1] + Z;
	i32 B  = p[X + 1] + Y;
	i32 BA = p[B] + Z;
	i32 BB = p[B + 1] + Z;
	return geom_lerp(w,
	                 geom_lerp(v, geom_lerp(u, geom_grad(p[AA], x, y, z), geom_grad(p[BA], x - 1, y, z)),
	                           geom_lerp(u, geom_grad(p[AB], x, y - 1, z), geom_grad(p[BB], x - 1, y - 1, z))),
	                 geom_lerp(v, geom_lerp(u, geom_grad(p[AA + 1], x, y, z - 1), geom_grad(p[BA + 1], x - 1, y, z - 1)),
	                           geom_lerp(u, geom_grad(p[AB + 1], x, y - 1, z - 1), geom_grad(p[BB + 1], x - 1, y - 1, z - 1))));
}

// Signed distance field shapes

void geom_sdf_begin(f32 voxel) {
	geom_voxel      = voxel;
	geom_prim_count = 0;
	geom_sdf_transform(0, 0, 0, 0, 0);
}

// Places the following shapes: world = translate(x, y, z) * rotate(axis 0/1/2, deg) * local
void geom_sdf_transform(i32 axis, f32 deg, f32 x, f32 y, f32 z) {
	geom_rotation(axis, deg, geom_rot);
	geom_tr[0] = x;
	geom_tr[1] = y;
	geom_tr[2] = z;
}

static void geom_sdf_add(i32 type, i32 op, f32 *a, i32 n) {
	geom_prims     = realloc(geom_prims, sizeof(geom_prim_t) * (geom_prim_count + 1));
	geom_prim_t *p = &geom_prims[geom_prim_count++];
	p->type        = type;
	p->op          = op;
	memcpy(p->a, a, sizeof(f32) * n);
	memcpy(p->rot, geom_rot, sizeof(geom_rot));
	memcpy(p->tr, geom_tr, sizeof(geom_tr));
}

// Box from min to max corner with edges rounded by radius round
void geom_sdf_box(f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1, f32 round, i32 op) {
	f32 a[7] = {(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2, (x1 - x0) / 2, (y1 - y0) / 2, (z1 - z0) / 2, round};
	geom_sdf_add(GEOM_BOX, op, a, 7);
}

// Cylinder of radius r and length h centered at (x, y, z) along axis 0/1/2, edges rounded by round
void geom_sdf_cylinder(f32 x, f32 y, f32 z, f32 r, f32 h, i32 axis, f32 round, i32 op) {
	f32 a[7] = {x, y, z, r, h / 2, (f32)axis, round};
	geom_sdf_add(GEOM_CYLINDER, op, a, 7);
}

void geom_sdf_ellipsoid(f32 x, f32 y, f32 z, f32 rx, f32 ry, f32 rz, i32 op) {
	f32 a[6] = {x, y, z, rx, ry, rz};
	geom_sdf_add(GEOM_ELLIPSOID, op, a, 6);
}

// Round-ended segment from a to b, radius blending from ra to rb
void geom_sdf_capsule(f32 ax, f32 ay, f32 az, f32 bx, f32 by, f32 bz, f32 ra, f32 rb, i32 op) {
	f32 a[8] = {ax, ay, az, bx, by, bz, ra, rb};
	geom_sdf_add(GEOM_CAPSULE, op, a, 8);
}

// Torus around the z axis, squashed along z by zscale
void geom_sdf_torus(f32 x, f32 y, f32 z, f32 major, f32 minor, f32 zscale, i32 op) {
	f32 a[6] = {x, y, z, major, minor, zscale};
	geom_sdf_add(GEOM_TORUS, op, a, 6);
}

// Half space x * side >= 0, its edge pushed out into count zigzag teeth of height amp around the x axis
// (no tooth where the edge faces -y)
void geom_sdf_zigzag(f32 side, f32 amp, i32 count, i32 op) {
	f32 a[3] = {side, amp, (f32)count};
	geom_sdf_add(GEOM_ZIGZAG, op, a, 3);
}

static f32 geom_len3(f32 x, f32 y, f32 z) {
	return sqrtf(x * x + y * y + z * z);
}

static f32 geom_prim_eval(geom_prim_t *p, f32 wx, f32 wy, f32 wz) {
	// World to local: transpose(rot) * (w - tr)
	f32  dx = wx - p->tr[0];
	f32  dy = wy - p->tr[1];
	f32  dz = wz - p->tr[2];
	f32 *r  = p->rot;
	f32  x  = r[0] * dx + r[3] * dy + r[6] * dz;
	f32  y  = r[1] * dx + r[4] * dy + r[7] * dz;
	f32  z  = r[2] * dx + r[5] * dy + r[8] * dz;
	f32 *a  = p->a;
	if (p->type == GEOM_BOX) {
		f32 qx = fabsf(x - a[0]) - (a[3] - a[6]);
		f32 qy = fabsf(y - a[1]) - (a[4] - a[6]);
		f32 qz = fabsf(z - a[2]) - (a[5] - a[6]);
		f32 in = fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
		return geom_len3(fmaxf(qx, 0), fmaxf(qy, 0), fmaxf(qz, 0)) + in - a[6];
	}
	if (p->type == GEOM_CYLINDER) {
		f32 c[3] = {x - a[0], y - a[1], z - a[2]};
		i32 ax   = (i32)a[5];
		f32 h    = c[ax];
		c[ax]    = 0;
		f32 qr   = geom_len3(c[0], c[1], c[2]) - (a[3] - a[6]);
		f32 qh   = fabsf(h) - (a[4] - a[6]);
		return sqrtf(fmaxf(qr, 0) * fmaxf(qr, 0) + fmaxf(qh, 0) * fmaxf(qh, 0)) + fminf(fmaxf(qr, qh), 0.0f) - a[6];
	}
	if (p->type == GEOM_ELLIPSOID) {
		f32 px = (x - a[0]) / a[3];
		f32 py = (y - a[1]) / a[4];
		f32 pz = (z - a[2]) / a[5];
		f32 k0 = geom_len3(px, py, pz);
		f32 k1 = geom_len3(px / a[3], py / a[4], pz / a[5]);
		return k1 > 0.0f ? k0 * (k0 - 1.0f) / k1 : -fminf(a[3], fminf(a[4], a[5]));
	}
	if (p->type == GEOM_CAPSULE) {
		f32 pax = x - a[0], pay = y - a[1], paz = z - a[2];
		f32 bax = a[3] - a[0], bay = a[4] - a[1], baz = a[5] - a[2];
		f32 h = (pax * bax + pay * bay + paz * baz) / (bax * bax + bay * bay + baz * baz);
		h     = fminf(fmaxf(h, 0.0f), 1.0f);
		return geom_len3(pax - bax * h, pay - bay * h, paz - baz * h) - (a[6] + (a[7] - a[6]) * h);
	}
	if (p->type == GEOM_TORUS) {
		f32 px = x - a[0], py = y - a[1], pz = (z - a[2]) / a[5];
		f32 q = sqrtf(px * px + py * py) - a[3];
		return (sqrtf(q * q + pz * pz) - a[4]) * fminf(a[5], 1.0f);
	}
	// GEOM_ZIGZAG
	f32 deg    = atan2f(z, y) * (f32)(180.0 / 3.14159265358979);
	f32 period = 360.0f / a[2];
	f32 tooth  = 0.0f;
	if (fabsf(deg) < 180.0f - period) {
		f32 t = fmodf((deg + 180.0f - period) / (period / 2.0f), 2.0f);
		tooth = a[1] * (1.0f - fabsf(t - 1.0f));
	}
	return -(x * a[0]) - tooth;
}

static f32 geom_sdf_eval(f32 x, f32 y, f32 z) {
	f32 d = 1e9f;
	for (i32 i = 0; i < geom_prim_count; ++i) {
		f32 p  = geom_prim_eval(&geom_prims[i], x, y, z);
		i32 op = i == 0 ? GEOM_UNION : geom_prims[i].op;
		d      = op == GEOM_UNION ? fminf(d, p) : op == GEOM_SUBTRACT ? fmaxf(d, -p) : fmaxf(d, p);
	}
	return d;
}

static void geom_prim_bounds(geom_prim_t *p, f32 *mn, f32 *mx) {
	f32 *a = p->a;
	f32  lo[3], hi[3];
	if (p->type == GEOM_BOX || p->type == GEOM_ELLIPSOID) {
		for (i32 i = 0; i < 3; ++i) {
			lo[i] = a[i] - a[i + 3];
			hi[i] = a[i] + a[i + 3];
		}
	}
	else if (p->type == GEOM_CYLINDER) {
		for (i32 i = 0; i < 3; ++i) {
			f32 e = i == (i32)a[5] ? a[4] : a[3];
			lo[i] = a[i] - e;
			hi[i] = a[i] + e;
		}
	}
	else if (p->type == GEOM_CAPSULE) {
		f32 r = fmaxf(a[6], a[7]);
		for (i32 i = 0; i < 3; ++i) {
			lo[i] = fminf(a[i], a[i + 3]) - r;
			hi[i] = fmaxf(a[i], a[i + 3]) + r;
		}
	}
	else if (p->type == GEOM_TORUS) {
		f32 e[3] = {a[3] + a[4], a[3] + a[4], a[4] * a[5]};
		for (i32 i = 0; i < 3; ++i) {
			lo[i] = a[i] - e[i];
			hi[i] = a[i] + e[i];
		}
	}
	else {
		return; // Unbounded
	}
	for (i32 c = 0; c < 8; ++c) {
		f32 l[3] = {c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]};
		for (i32 i = 0; i < 3; ++i) {
			f32 w = p->rot[i * 3] * l[0] + p->rot[i * 3 + 1] * l[1] + p->rot[i * 3 + 2] * l[2] + p->tr[i];
			mn[i] = fminf(mn[i], w);
			mx[i] = fmaxf(mx[i], w);
		}
	}
}

static void geom_smooth(geom_t *g, i32 iterations, f32 factor) {
	f32 *sum = malloc(sizeof(f32) * 3 * g->vcount);
	i32 *cnt = malloc(sizeof(i32) * g->vcount);
	for (i32 it = 0; it < iterations; ++it) {
		memset(sum, 0, sizeof(f32) * 3 * g->vcount);
		memset(cnt, 0, sizeof(i32) * g->vcount);
		for (i32 i = 0; i < g->icount; i += 3) {
			for (i32 e = 0; e < 3; ++e) {
				u32 a = g->ind[i + e];
				u32 b = g->ind[i + (e + 1) % 3];
				for (i32 k = 0; k < 3; ++k) {
					sum[a * 3 + k] += g->pos[b * 3 + k];
					sum[b * 3 + k] += g->pos[a * 3 + k];
				}
				cnt[a]++;
				cnt[b]++;
			}
		}
		for (i32 v = 0; v < g->vcount; ++v) {
			if (cnt[v] == 0) {
				continue;
			}
			for (i32 k = 0; k < 3; ++k) {
				f32 avg = sum[v * 3 + k] / cnt[v];
				g->pos[v * 3 + k] += (avg - g->pos[v * 3 + k]) * factor;
			}
		}
	}
	free(sum);
	free(cnt);
}

geom_t *geom_sdf_end(i32 smooth) {
	f32 v     = geom_voxel;
	f32 mn[3] = {1e9f, 1e9f, 1e9f};
	f32 mx[3] = {-1e9f, -1e9f, -1e9f};
	for (i32 i = 0; i < geom_prim_count; ++i) {
		if (i == 0 || geom_prims[i].op == GEOM_UNION) {
			geom_prim_bounds(&geom_prims[i], mn, mx);
		}
	}
	i32 n[3];
	for (i32 i = 0; i < 3; ++i) {
		mn[i] -= v * 2;
		n[i] = (i32)ceilf((mx[i] + v * 2 - mn[i]) / v) + 1;
	}
	i32  nx = n[0], ny = n[1], nz = n[2];
	f32 *f = malloc(sizeof(f32) * nx * ny * nz);
	for (i32 k = 0; k < nz; ++k) {
		for (i32 j = 0; j < ny; ++j) {
			for (i32 i = 0; i < nx; ++i) {
				f[i + nx * (j + ny * k)] = geom_sdf_eval(mn[0] + i * v, mn[1] + j * v, mn[2] + k * v);
			}
		}
	}

	geom_t *g  = geom_create();
	i32     cx = nx - 1, cy = ny - 1, cz = nz - 1;
	i32    *cell = malloc(sizeof(i32) * cx * cy * cz);
	for (i32 k = 0; k < cz; ++k) {
		for (i32 j = 0; j < cy; ++j) {
			for (i32 i = 0; i < cx; ++i) {
				f32 c[8];
				i32 inside = 0;
				for (i32 b = 0; b < 8; ++b) {
					c[b] = f[(i + (b & 1)) + nx * ((j + ((b >> 1) & 1)) + ny * (k + ((b >> 2) & 1)))];
					inside += c[b] < 0.0f;
				}
				cell[i + cx * (j + cy * k)] = -1;
				if (inside == 0 || inside == 8) {
					continue;
				}
				// Average of the edge crossings
				f32 s[3]  = {0, 0, 0};
				i32 count = 0;
				for (i32 b = 0; b < 8; ++b) {
					for (i32 bit = 1; bit < 8; bit <<= 1) {
						if ((b & bit) || (c[b] < 0.0f) == (c[b | bit] < 0.0f)) {
							continue;
						}
						f32 t = c[b] / (c[b] - c[b | bit]);
						i32 e = b | bit;
						s[0] += (b & 1) + ((e & 1) - (b & 1)) * t;
						s[1] += ((b >> 1) & 1) + (((e >> 1) & 1) - ((b >> 1) & 1)) * t;
						s[2] += ((b >> 2) & 1) + (((e >> 2) & 1) - ((b >> 2) & 1)) * t;
						count++;
					}
				}
				cell[i + cx * (j + cy * k)] = geom_add_vert(g, mn[0] + (i + s[0] / count) * v, mn[1] + (j + s[1] / count) * v, mn[2] + (k + s[2] / count) * v);
			}
		}
	}
	// A quad for every grid edge crossing the surface, from the 4 cells around it
	for (i32 k = 0; k < nz; ++k) {
		for (i32 j = 0; j < ny; ++j) {
			for (i32 i = 0; i < nx; ++i) {
				i32 p[3] = {i, j, k};
				for (i32 a = 0; a < 3; ++a) {
					i32 u = (a + 1) % 3;
					i32 w = (a + 2) % 3;
					if (p[a] >= n[a] - 1 || p[u] < 1 || p[u] > n[u] - 2 || p[w] < 1 || p[w] > n[w] - 2) {
						continue;
					}
					i32 q[3] = {i, j, k};
					q[a]++;
					bool in0 = f[i + nx * (j + ny * k)] < 0.0f;
					bool in1 = f[q[0] + nx * (q[1] + ny * q[2])] < 0.0f;
					if (in0 == in1) {
						continue;
					}
					i32 quad[4];
					i32 du[4] = {0, 1, 1, 0};
					i32 dw[4] = {0, 0, 1, 1};
					for (i32 m = 0; m < 4; ++m) {
						i32 c[3] = {i, j, k};
						c[u]     = p[u] - 1 + du[m];
						c[w]     = p[w] - 1 + dw[m];
						quad[m]  = cell[c[0] + cx * (c[1] + cy * c[2])];
					}
					if (!in0) {
						i32 t   = quad[1];
						quad[1] = quad[3];
						quad[3] = t;
					}
					// Split along the shorter diagonal
					f32 *p0 = &g->pos[quad[0] * 3], *p1 = &g->pos[quad[1] * 3], *p2 = &g->pos[quad[2] * 3], *p3 = &g->pos[quad[3] * 3];
					f32  d02 = geom_len3(p0[0] - p2[0], p0[1] - p2[1], p0[2] - p2[2]);
					f32  d13 = geom_len3(p1[0] - p3[0], p1[1] - p3[1], p1[2] - p3[2]);
					if (d02 <= d13) {
						geom_add_quad(g, quad[0], quad[1], quad[2], quad[3]);
					}
					else {
						geom_add_quad(g, quad[1], quad[2], quad[3], quad[0]);
					}
				}
			}
		}
	}
	free(f);
	free(cell);
	geom_smooth(g, smooth, 0.6f);
	geom_prim_count = 0;
	return g;
}

// Direct primitives

// Flat shaded box
geom_t *geom_box(f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1) {
	geom_t *g       = geom_create();
	f32     b[2][3] = {{x0, y0, z0}, {x1, y1, z1}};
	// Per face: axis, side, then 4 corners counterclockwise seen from outside
	for (i32 a = 0; a < 3; ++a) {
		i32 u = (a + 1) % 3;
		i32 w = (a + 2) % 3;
		for (i32 s = 0; s < 2; ++s) {
			i32 du[4] = {0, 1, 1, 0};
			i32 dw[4] = {0, 0, 1, 1};
			i32 first = g->vcount;
			for (i32 m = 0; m < 4; ++m) {
				f32 c[3];
				c[a] = b[s][a];
				c[u] = b[du[m]][u];
				c[w] = b[dw[m]][w];
				geom_add_vert(g, c[0], c[1], c[2]);
			}
			if (s == 1) {
				geom_add_quad(g, first, first + 1, first + 2, first + 3);
			}
			else {
				geom_add_quad(g, first, first + 3, first + 2, first + 1);
			}
		}
	}
	return g;
}

// Plane at z = 0 facing +z, divided into nx * ny quads
geom_t *geom_grid(f32 x0, f32 y0, f32 x1, f32 y1, i32 nx, i32 ny) {
	geom_t *g = geom_create();
	for (i32 j = 0; j <= ny; ++j) {
		for (i32 i = 0; i <= nx; ++i) {
			geom_add_vert(g, x0 + (x1 - x0) * i / nx, y0 + (y1 - y0) * j / ny, 0.0f);
		}
	}
	for (i32 j = 0; j < ny; ++j) {
		for (i32 i = 0; i < nx; ++i) {
			i32 a = i + j * (nx + 1);
			geom_add_quad(g, a, a + 1, a + nx + 2, a + nx + 1);
		}
	}
	return g;
}

// Capped cylinder along z centered at (x, y, z)
geom_t *geom_cylinder(f32 x, f32 y, f32 z, f32 r, f32 h, i32 segs) {
	geom_t *g = geom_create();
	for (i32 s = 0; s < 2; ++s) {
		for (i32 i = 0; i < segs; ++i) {
			f32 a = 2.0f * 3.14159265f * i / segs;
			geom_add_vert(g, x + cosf(a) * r, y + sinf(a) * r, z + (s == 0 ? -h : h) / 2);
		}
	}
	i32 bottom = geom_add_vert(g, x, y, z - h / 2);
	i32 top    = geom_add_vert(g, x, y, z + h / 2);
	for (i32 i = 0; i < segs; ++i) {
		i32 n = (i + 1) % segs;
		geom_add_quad(g, i, n, segs + n, segs + i);
		geom_add_tri(g, bottom, n, i);
		geom_add_tri(g, top, segs + i, segs + n);
	}
	return g;
}

// Torus around the z axis centered at (x, y, z)
geom_t *geom_torus(f32 x, f32 y, f32 z, f32 major, f32 minor, i32 segs, i32 sides) {
	geom_t *g = geom_create();
	for (i32 i = 0; i < segs; ++i) {
		f32 a = 2.0f * 3.14159265f * i / segs;
		for (i32 k = 0; k < sides; ++k) {
			f32 b = 2.0f * 3.14159265f * k / sides;
			f32 r = major + cosf(b) * minor;
			geom_add_vert(g, x + cosf(a) * r, y + sinf(a) * r, z + sinf(b) * minor);
		}
	}
	for (i32 i = 0; i < segs; ++i) {
		i32 n = (i + 1) % segs;
		for (i32 k = 0; k < sides; ++k) {
			i32 m = (k + 1) % sides;
			geom_add_quad(g, i * sides + k, n * sides + k, n * sides + m, i * sides + m);
		}
	}
	return g;
}

// Mesh edits

void geom_rotate(geom_t *g, i32 axis, f32 deg, f32 px, f32 py, f32 pz) {
	f32 m[9];
	geom_rotation(axis, deg, m);
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 *p = &g->pos[v * 3];
		f32  x = p[0] - px, y = p[1] - py, z = p[2] - pz;
		p[0] = m[0] * x + m[1] * y + m[2] * z + px;
		p[1] = m[3] * x + m[4] * y + m[5] * z + py;
		p[2] = m[6] * x + m[7] * y + m[8] * z + pz;
	}
}

void geom_translate(geom_t *g, f32 x, f32 y, f32 z) {
	for (i32 v = 0; v < g->vcount; ++v) {
		g->pos[v * 3] += x;
		g->pos[v * 3 + 1] += y;
		g->pos[v * 3 + 2] += z;
	}
}

void geom_flip(geom_t *g) {
	for (i32 i = 0; i < g->icount; i += 3) {
		u32 t         = g->ind[i + 1];
		g->ind[i + 1] = g->ind[i + 2];
		g->ind[i + 2] = t;
	}
}

geom_t *geom_copy(geom_t *g) {
	geom_t *c = geom_create();
	*c        = *g;
	c->pos    = malloc(sizeof(f32) * 3 * g->vcap);
	c->ind    = malloc(sizeof(u32) * g->icap);
	memcpy(c->pos, g->pos, sizeof(f32) * 3 * g->vcount);
	memcpy(c->ind, g->ind, sizeof(u32) * g->icount);
	c->uv   = NULL;
	c->wt   = NULL;
	c->jt   = NULL;
	c->part = NULL;
	return c;
}

// Keeps the faces whose center lies within lo..hi along axis 0/1/2, drops unused vertices
void geom_keep(geom_t *g, i32 axis, f32 lo, f32 hi) {
	i32 *remap  = malloc(sizeof(i32) * g->vcount);
	i32  icount = 0;
	for (i32 v = 0; v < g->vcount; ++v) {
		remap[v] = -1;
	}
	for (i32 i = 0; i < g->icount; i += 3) {
		f32 c = (g->pos[g->ind[i] * 3 + axis] + g->pos[g->ind[i + 1] * 3 + axis] + g->pos[g->ind[i + 2] * 3 + axis]) / 3.0f;
		if (c < lo || c > hi) {
			continue;
		}
		if (g->part != NULL) {
			g->part[icount / 3] = g->part[i / 3];
		}
		for (i32 k = 0; k < 3; ++k) {
			g->ind[icount++]     = g->ind[i + k];
			remap[g->ind[i + k]] = 0;
		}
	}
	i32 vcount = 0;
	for (i32 v = 0; v < g->vcount; ++v) {
		if (remap[v] == 0) {
			remap[v] = vcount;
			memmove(&g->pos[vcount * 3], &g->pos[v * 3], sizeof(f32) * 3);
			if (g->wt != NULL) {
				memmove(&g->wt[vcount * 4], &g->wt[v * 4], sizeof(f32) * 4);
				memmove(&g->jt[vcount * 4], &g->jt[v * 4], sizeof(u16) * 4);
			}
			vcount++;
		}
	}
	for (i32 i = 0; i < icount; ++i) {
		g->ind[i] = remap[g->ind[i]];
	}
	g->vcount = vcount;
	g->icount = icount;
	free(remap);
}

// Area weighted vertex normals
static f32 *geom_normals(geom_t *g) {
	f32 *n = calloc(g->vcount * 3, sizeof(f32));
	for (i32 i = 0; i < g->icount; i += 3) {
		f32 *a = &g->pos[g->ind[i] * 3], *b = &g->pos[g->ind[i + 1] * 3], *c = &g->pos[g->ind[i + 2] * 3];
		f32  e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		f32  e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
		f32  f[3]  = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		for (i32 k = 0; k < 3; ++k) {
			for (i32 m = 0; m < 3; ++m) {
				n[g->ind[i + k] * 3 + m] += f[m];
			}
		}
	}
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 l = geom_len3(n[v * 3], n[v * 3 + 1], n[v * 3 + 2]);
		if (l > 0.0f) {
			n[v * 3] /= l;
			n[v * 3 + 1] /= l;
			n[v * 3 + 2] /= l;
		}
	}
	return n;
}

// Lumpy hand-pressed surface: displaces along the normals with layered noise
void geom_clay(geom_t *g, f32 amp, f32 freq, f32 seed, f32 fine) {
	f32 *n      = geom_normals(g);
	f32  off[3] = {seed * 13.1f, seed * 7.7f, seed * 3.3f};
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 *c    = &g->pos[v * 3];
		f32  p[3] = {c[0] * freq + off[0], c[1] * freq + off[1], c[2] * freq + off[2]};
		f32  d    = geom_noise(p[0], p[1], p[2]) * amp + geom_noise(p[0] * 3.1f, p[1] * 3.1f, p[2] * 3.1f) * amp * 0.35f;
		if (fine > 0.0f) {
			d += geom_noise(c[0] * 90.0f + off[0], c[1] * 90.0f + off[1], c[2] * 90.0f + off[2]) * fine;
		}
		for (i32 k = 0; k < 3; ++k) {
			c[k] += n[v * 3 + k] * d;
		}
	}
	free(n);
}

void geom_unwrap(geom_t *g, f32 margin) {
	f32 mn[3] = {1e9f, 1e9f, 1e9f};
	f32 mx[3] = {-1e9f, -1e9f, -1e9f};
	for (i32 v = 0; v < g->vcount; ++v) {
		for (i32 k = 0; k < 3; ++k) {
			mn[k] = fminf(mn[k], g->pos[v * 3 + k]);
			mx[k] = fmaxf(mx[k], g->pos[v * 3 + k]);
		}
	}
	f32 scale = 1e-6f;
	for (i32 k = 0; k < 3; ++k) {
		scale = fmaxf(scale, (mx[k] - mn[k]) / 2);
	}
	f32        *n      = geom_normals(g);
	raw_mesh_t *mesh   = calloc(1, sizeof(raw_mesh_t));
	mesh->posa         = calloc(1, sizeof(i16_array_t));
	mesh->nora         = calloc(1, sizeof(i16_array_t));
	mesh->inda         = calloc(1, sizeof(u32_array_t));
	mesh->posa->buffer = malloc(sizeof(i16) * 4 * g->vcount);
	mesh->posa->length = g->vcount * 4;
	mesh->nora->buffer = malloc(sizeof(i16) * 2 * g->vcount);
	mesh->nora->length = g->vcount * 2;
	mesh->inda->buffer = malloc(sizeof(u32) * g->icount);
	mesh->inda->length = g->icount;
	memcpy(mesh->inda->buffer, g->ind, sizeof(u32) * g->icount);
	for (i32 v = 0; v < g->vcount; ++v) {
		for (i32 k = 0; k < 3; ++k) {
			mesh->posa->buffer[v * 4 + k] = (i16)(((g->pos[v * 3 + k] - (mn[k] + mx[k]) / 2) / scale) * 32767.0f);
		}
		mesh->posa->buffer[v * 4 + 3] = (i16)(n[v * 3 + 2] * 32767.0f);
		mesh->nora->buffer[v * 2]     = (i16)(n[v * 3] * 32767.0f);
		mesh->nora->buffer[v * 2 + 1] = (i16)(n[v * 3 + 1] * 32767.0f);
	}
	mesh->vertex_count    = g->vcount;
	mesh->index_count     = g->icount;
	mesh->scale_pos       = scale;
	mesh->scale_tex       = 1.0f;
	f32 last_margin       = util_uv_unwrap_margin;
	util_uv_unwrap_margin = margin;
	util_uv_unwrap_run(mesh);
	util_uv_unwrap_margin = last_margin;
	// Output vertex i is face corner i; texa stores (1 - u, v)
	free(g->uv);
	g->uv = malloc(sizeof(f32) * 2 * g->icount);
	for (i32 i = 0; i < g->icount; ++i) {
		g->uv[i * 2]     = 1.0f - mesh->texa->buffer[i * 2] / 32767.0f;
		g->uv[i * 2 + 1] = mesh->texa->buffer[i * 2 + 1] / 32767.0f;
	}
	free(mesh->posa->buffer);
	free(mesh->nora->buffer);
	free(mesh->inda->buffer);
	free(mesh->texa->buffer);
	free(mesh->posa);
	free(mesh->nora);
	free(mesh->inda);
	free(mesh->texa);
	free(mesh);
	free(n);
}

geom_t *geom_ellipsoid(f32 x, f32 y, f32 z, f32 rx, f32 ry, f32 rz, i32 segs, i32 rings) {
	geom_t *g   = geom_create();
	i32     top = geom_add_vert(g, x, y, z + rz);
	for (i32 k = 1; k < rings; ++k) {
		f32 phi = 3.14159265f * k / rings;
		for (i32 i = 0; i < segs; ++i) {
			f32 a = 2.0f * 3.14159265f * i / segs;
			geom_add_vert(g, x + cosf(a) * sinf(phi) * rx, y + sinf(a) * sinf(phi) * ry, z + cosf(phi) * rz);
		}
	}
	i32 bottom = geom_add_vert(g, x, y, z - rz);
	for (i32 i = 0; i < segs; ++i) {
		i32 n = (i + 1) % segs;
		geom_add_tri(g, top, 1 + i, 1 + n);
		for (i32 k = 0; k < rings - 2; ++k) {
			i32 a = 1 + k * segs;
			i32 b = a + segs;
			geom_add_quad(g, a + i, b + i, b + n, a + n);
		}
		i32 last = 1 + (rings - 2) * segs;
		geom_add_tri(g, bottom, last + n, last + i);
	}
	return g;
}

// Maps local coordinates to p + x * u + y * (w x u) + z * w, u and w orthonormal
void geom_basis(geom_t *g, f32 ux, f32 uy, f32 uz, f32 wx, f32 wy, f32 wz, f32 px, f32 py, f32 pz) {
	f32 n[3] = {wy * uz - wz * uy, wz * ux - wx * uz, wx * uy - wy * ux};
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 *p = &g->pos[v * 3];
		f32  x = p[0], y = p[1], z = p[2];
		p[0] = px + x * ux + y * n[0] + z * wx;
		p[1] = py + x * uy + y * n[1] + z * wy;
		p[2] = pz + x * uz + y * n[2] + z * wz;
	}
}

void geom_scale(geom_t *g, f32 s) {
	for (i32 i = 0; i < g->vcount * 3; ++i) {
		g->pos[i] *= s;
	}
}

static f32 *geom_tube_pts   = NULL; // x, y, z, radius
static i32  geom_tube_count = 0;
static i32  geom_tube_cap   = 0;

void geom_tube_begin() {
	geom_tube_count = 0;
}

void geom_tube_point(f32 x, f32 y, f32 z, f32 r) {
	if (geom_tube_count == geom_tube_cap) {
		geom_tube_cap = geom_tube_cap == 0 ? 64 : geom_tube_cap * 2;
		geom_tube_pts = realloc(geom_tube_pts, sizeof(f32) * 4 * geom_tube_cap);
	}
	f32 *p = &geom_tube_pts[geom_tube_count++ * 4];
	p[0]   = x;
	p[1]   = y;
	p[2]   = z;
	p[3]   = r;
}

static f32 geom_catmull(f32 p0, f32 p1, f32 p2, f32 p3, f32 t) {
	return 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
}

static void geom_normalize(f32 *v) {
	f32 l = geom_len3(v[0], v[1], v[2]);
	if (l > 0.0f) {
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
}

static void geom_cross(f32 *a, f32 *b, f32 *out) {
	f32 c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	memcpy(out, c, sizeof(c));
}

geom_t *geom_tube_end(i32 sides, i32 steps, f32 squash, i32 curve_steps, i32 transport) {
	i32  n   = geom_tube_count;
	f32 *pts = geom_tube_pts;
	f32 *res = NULL;
	if (curve_steps > 0 && n >= 2) {
		// Ends extended by reflection, radii by repetition
		f32 *ext = malloc(sizeof(f32) * 4 * (n + 2));
		memcpy(&ext[4], pts, sizeof(f32) * 4 * n);
		for (i32 k = 0; k < 3; ++k) {
			ext[k]               = pts[k] * 2 - pts[4 + k];
			ext[(n + 1) * 4 + k] = pts[(n - 1) * 4 + k] * 2 - pts[(n - 2) * 4 + k];
		}
		ext[3]               = pts[3];
		ext[(n + 1) * 4 + 3] = pts[(n - 1) * 4 + 3];
		i32 m                = 0;
		res                  = malloc(sizeof(f32) * 4 * ((n - 1) * curve_steps + 1));
		for (i32 i = 1; i < n; ++i) {
			i32 count = i < n - 1 ? curve_steps : curve_steps + 1;
			for (i32 s = 0; s < count; ++s) {
				f32  t = (f32)s / curve_steps;
				f32 *o = &res[m++ * 4];
				for (i32 k = 0; k < 4; ++k) {
					o[k] = geom_catmull(ext[(i - 1) * 4 + k], ext[i * 4 + k], ext[(i + 1) * 4 + k], ext[(i + 2) * 4 + k], t);
				}
				o[3] = fmaxf(0.002f, o[3]);
			}
		}
		free(ext);
		pts = res;
		n   = m;
	}

	geom_t *g = geom_create();
	if (n < 2) {
		free(res);
		return g;
	}
	// Rings: center, radius, direction
	i32  ring_count = (n - 1) * steps + 1;
	f32 *rings      = malloc(sizeof(f32) * 7 * ring_count);
	i32  r          = 0;
	for (i32 i = 0; i < n - 1; ++i) {
		f32 *a     = &pts[i * 4];
		f32 *b     = &pts[(i + 1) * 4];
		f32  d[3]  = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		i32  count = i < n - 2 ? steps : steps + 1;
		geom_normalize(d);
		for (i32 s = 0; s < count; ++s) {
			f32  t = (f32)s / steps;
			f32 *o = &rings[r++ * 7];
			for (i32 k = 0; k < 4; ++k) {
				o[k] = a[k] + (b[k] - a[k]) * t;
			}
			memcpy(&o[4], d, sizeof(d));
		}
	}
	i32 *ring_first = malloc(sizeof(i32) * ring_count);
	f32  side[3]    = {0, 0, 0};
	for (i32 i = 0; i < ring_count; ++i) {
		f32 *c = &rings[i * 7];
		f32 *d = &c[4];
		if (i == 0 || !transport) {
			f32 up[3] = {0, 0, 1};
			if (fabsf(d[2]) >= 0.9f) {
				up[1] = 1;
				up[2] = 0;
			}
			geom_cross(d, up, side);
		}
		else {
			f32 dot = side[0] * d[0] + side[1] * d[1] + side[2] * d[2];
			for (i32 k = 0; k < 3; ++k) {
				side[k] -= d[k] * dot;
			}
		}
		geom_normalize(side);
		f32 up2[3];
		geom_cross(side, d, up2);
		geom_normalize(up2);
		ring_first[i] = g->vcount;
		for (i32 k = 0; k < sides; ++k) {
			f32 a  = 2.0f * 3.14159265f * k / sides;
			f32 ca = cosf(a) * c[3];
			f32 sa = sinf(a) * c[3] * squash;
			geom_add_vert(g, c[0] + side[0] * ca + up2[0] * sa, c[1] + side[1] * ca + up2[1] * sa, c[2] + side[2] * ca + up2[2] * sa);
		}
	}
	// Ring runs clockwise around d, so this order faces outward
	for (i32 i = 0; i < ring_count - 1; ++i) {
		i32 r0 = ring_first[i];
		i32 r1 = ring_first[i + 1];
		for (i32 k = 0; k < sides; ++k) {
			i32 m = (k + 1) % sides;
			geom_add_quad(g, r0 + k, r1 + k, r1 + m, r0 + m);
		}
	}
	// Rounded caps: three quarter-circle rings and a tip
	for (i32 e = 0; e < 2; ++e) {
		i32  end  = e == 0 ? 0 : ring_count - 1;
		f32  sign = e == 0 ? -1.0f : 1.0f;
		f32 *c    = &rings[end * 7];
		f32 *d    = &c[4];
		i32  ring = ring_first[end];
		i32  prev = ring;
		for (i32 j = 1; j < 4; ++j) {
			f32 ang   = j / 4.0f * 3.14159265f / 2;
			i32 first = g->vcount;
			for (i32 k = 0; k < sides; ++k) {
				f32 p[3];
				for (i32 m = 0; m < 3; ++m) {
					p[m] = c[m] + (g->pos[(ring + k) * 3 + m] - c[m]) * cosf(ang) + d[m] * sign * c[3] * sinf(ang);
				}
				geom_add_vert(g, p[0], p[1], p[2]);
			}
			for (i32 k = 0; k < sides; ++k) {
				i32 m = (k + 1) % sides;
				if (sign > 0) {
					geom_add_quad(g, first + k, first + m, prev + m, prev + k);
				}
				else {
					geom_add_quad(g, prev + k, prev + m, first + m, first + k);
				}
			}
			prev = first;
		}
		i32 tip = geom_add_vert(g, c[0] + d[0] * sign * c[3], c[1] + d[1] * sign * c[3], c[2] + d[2] * sign * c[3]);
		for (i32 k = 0; k < sides; ++k) {
			i32 m = (k + 1) % sides;
			if (sign > 0) {
				geom_add_tri(g, tip, prev + m, prev + k);
			}
			else {
				geom_add_tri(g, prev + k, prev + m, tip);
			}
		}
	}
	free(ring_first);
	free(rings);
	free(res);
	return g;
}

i32 geom_vertex_count(geom_t *g) {
	return g->vcount;
}

f32 geom_vertex_get(geom_t *g, i32 v, i32 axis) {
	return g->pos[v * 3 + axis];
}

void geom_vertex_set(geom_t *g, i32 v, f32 x, f32 y, f32 z) {
	g->pos[v * 3]     = x;
	g->pos[v * 3 + 1] = y;
	g->pos[v * 3 + 2] = z;
}

void geom_weights(geom_t *g, i32 v, i32 b0, f32 w0, i32 b1, f32 w1, i32 b2, f32 w2) {
	if (g->wt == NULL) {
		g->wt = calloc(g->vcount * 4, sizeof(f32));
		g->jt = calloc(g->vcount * 4, sizeof(u16));
	}
	i32 bones[3]   = {b0, b1, b2};
	f32 weights[3] = {w0, w1, w2};
	i32 from       = v < 0 ? 0 : v;
	i32 to         = v < 0 ? g->vcount : v + 1;
	for (i32 i = from; i < to; ++i) {
		i32 n = 0;
		memset(&g->wt[i * 4], 0, sizeof(f32) * 4);
		memset(&g->jt[i * 4], 0, sizeof(u16) * 4);
		for (i32 k = 0; k < 3; ++k) {
			if (bones[k] >= 0 && weights[k] > 0.0001f) {
				g->wt[i * 4 + n] = weights[k];
				g->jt[i * 4 + n] = (u16)bones[k];
				n++;
			}
		}
	}
}

void geom_weights_blend(geom_t *g, i32 a, i32 b, f32 ax, f32 ay, f32 az, f32 bx, f32 by, f32 bz) {
	f32 d[3] = {bx - ax, by - ay, bz - az};
	f32 dd   = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 *p = &g->pos[v * 3];
		f32  t = ((p[0] - ax) * d[0] + (p[1] - ay) * d[1] + (p[2] - az) * d[2]) / dd;
		t      = fminf(fmaxf(t, 0.0f), 1.0f);
		t      = t * t * (3 - 2 * t);
		geom_weights(g, v, a, 1.0f - t, b, t, -1, 0.0f);
	}
}

void geom_part(geom_t *g, i32 part) {
	free(g->part);
	g->part = malloc(g->icount / 3);
	memset(g->part, part, g->icount / 3);
}

geom_t *geom_merge(geom_t *dst, geom_t *src) {
	if (dst == NULL) {
		dst = geom_create();
	}
	i32 v0 = dst->vcount;
	i32 t0 = dst->icount / 3;
	if (dst->wt != NULL || src->wt != NULL) {
		f32 *wt = calloc((v0 + src->vcount) * 4, sizeof(f32));
		u16 *jt = calloc((v0 + src->vcount) * 4, sizeof(u16));
		if (dst->wt != NULL) {
			memcpy(wt, dst->wt, sizeof(f32) * 4 * v0);
			memcpy(jt, dst->jt, sizeof(u16) * 4 * v0);
		}
		if (src->wt != NULL) {
			memcpy(&wt[v0 * 4], src->wt, sizeof(f32) * 4 * src->vcount);
			memcpy(&jt[v0 * 4], src->jt, sizeof(u16) * 4 * src->vcount);
		}
		free(dst->wt);
		free(dst->jt);
		dst->wt = wt;
		dst->jt = jt;
	}
	if (dst->part != NULL || src->part != NULL) {
		u8 *part = calloc(t0 + src->icount / 3, 1);
		if (dst->part != NULL) {
			memcpy(part, dst->part, t0);
		}
		if (src->part != NULL) {
			memcpy(part + t0, src->part, src->icount / 3);
		}
		free(dst->part);
		dst->part = part;
	}
	for (i32 v = 0; v < src->vcount; ++v) {
		geom_add_vert(dst, src->pos[v * 3], src->pos[v * 3 + 1], src->pos[v * 3 + 2]);
	}
	for (i32 i = 0; i < src->icount; i += 3) {
		geom_add_tri(dst, src->ind[i] + v0, src->ind[i + 1] + v0, src->ind[i + 2] + v0);
	}
	free(dst->uv);
	dst->uv = NULL;
	free(src->pos);
	free(src->ind);
	free(src->uv);
	free(src->wt);
	free(src->jt);
	free(src->part);
	free(src);
	return dst;
}

void geom_displace(geom_t *g, f32 freq0, f32 amp0, f32 freq1, f32 amp1) {
	f32 *n = geom_normals(g);
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 *p = &g->pos[v * 3];
		f32  d = geom_noise(p[0] * freq0, p[1] * freq0, p[2] * freq0) * amp0 + geom_noise(p[0] * freq1, p[1] * freq1, p[2] * freq1) * amp1;
		for (i32 k = 0; k < 3; ++k) {
			p[k] += n[v * 3 + k] * d;
		}
	}
	free(n);
}

// Color atlas

static u8 geom_palette_rgb[256][3];

void geom_palette(i32 part, i32 r, i32 g, i32 b) {
	geom_palette_rgb[part & 255][0] = (u8)r;
	geom_palette_rgb[part & 255][1] = (u8)g;
	geom_palette_rgb[part & 255][2] = (u8)b;
}

static void geom_tex(geom_t *g, i32 i, f32 *u, f32 *v) {
	*u = 1.0f - g->uv[i * 2];
	*v = g->uv[i * 2 + 1];
}

static u32 geom_rng_state;

static f32 geom_rng_uniform() {
	geom_rng_state = geom_rng_state * 1664525u + 1013904223u;
	return ((geom_rng_state >> 8) + 0.5f) / 16777216.0f;
}

static f32 geom_rng_normal() {
	f32 u = geom_rng_uniform();
	f32 v = geom_rng_uniform();
	return sqrtf(-2.0f * logf(u)) * cosf(2.0f * 3.14159265f * v);
}

int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);

void geom_atlas(geom_t *g, i32 size, i32 seed, char *path) {
	if (g->uv == NULL) {
		return;
	}
	f32 *img = malloc(sizeof(f32) * 3 * size * size);
	for (i32 i = 0; i < size * size; ++i) {
		for (i32 k = 0; k < 3; ++k) {
			img[i * 3 + k] = geom_palette_rgb[0][k];
		}
	}
	for (i32 t = 0; t < g->icount / 3; ++t) {
		f32 px[3], py[3];
		for (i32 k = 0; k < 3; ++k) {
			geom_tex(g, t * 3 + k, &px[k], &py[k]);
			px[k] *= size;
			py[k] *= size;
		}
		i32 x0   = (i32)fmaxf(floorf(fminf(px[0], fminf(px[1], px[2])) - 2), 0);
		i32 y0   = (i32)fmaxf(floorf(fminf(py[0], fminf(py[1], py[2])) - 2), 0);
		i32 x1   = (i32)fminf(ceilf(fmaxf(px[0], fmaxf(px[1], px[2])) + 2), size);
		i32 y1   = (i32)fminf(ceilf(fmaxf(py[0], fmaxf(py[1], py[2])) + 2), size);
		f32 area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
		f32 sgn  = area > 0 ? 1.0f : area < 0 ? -1.0f : 0.0f;
		u8 *col  = geom_palette_rgb[g->part != NULL ? g->part[t] : 0];
		for (i32 y = y0; y < y1; ++y) {
			for (i32 x = x0; x < x1; ++x) {
				bool inside = true;
				for (i32 k = 0; k < 3 && inside; ++k) {
					i32 m  = (k + 1) % 3;
					f32 ex = px[m] - px[k];
					f32 ey = py[m] - py[k];
					f32 l  = sqrtf(ex * ex + ey * ey);
					if (l > 0) {
						inside = (ex * (y + 0.5f - py[k]) - ey * (x + 0.5f - px[k])) * sgn / l >= -1.5f;
					}
				}
				if (inside) {
					for (i32 k = 0; k < 3; ++k) {
						img[(y * size + x) * 3 + k] = col[k];
					}
				}
			}
		}
	}
	i32  cells     = size / 16;
	f32 *noise     = malloc(sizeof(f32) * cells * cells);
	geom_rng_state = (u32)seed * 2654435761u + 1;
	for (i32 i = 0; i < cells * cells; ++i) {
		noise[i] = geom_rng_normal();
	}
	u8 *out = malloc(3 * size * size);
	for (i32 y = 0; y < size; ++y) {
		f32 ty = (y + 0.5f) / 16 - 0.5f;
		i32 j0 = (i32)fminf(fmaxf(floorf(ty), 0), cells - 1);
		i32 j1 = j0 + 1 < cells ? j0 + 1 : cells - 1;
		f32 fy = fminf(fmaxf(ty - j0, 0), 1);
		for (i32 x = 0; x < size; ++x) {
			f32 tx = (x + 0.5f) / 16 - 0.5f;
			i32 i0 = (i32)fminf(fmaxf(floorf(tx), 0), cells - 1);
			i32 i1 = i0 + 1 < cells ? i0 + 1 : cells - 1;
			f32 fx = fminf(fmaxf(tx - i0, 0), 1);
			f32 a  = noise[j0 * cells + i0] * (1 - fx) + noise[j0 * cells + i1] * fx;
			f32 b  = noise[j1 * cells + i0] * (1 - fx) + noise[j1 * cells + i1] * fx;
			f32 n  = a * (1 - fy) + b * fy;
			for (i32 k = 0; k < 3; ++k) {
				f32 c                       = img[(y * size + x) * 3 + k] * (1.0f + 0.035f * n);
				out[(y * size + x) * 3 + k] = (u8)(fminf(fmaxf(c, 0), 255) + 0.5f);
			}
		}
	}
	stbi_write_png(path, size, size, 3, out, size * 3);
	free(out);
	free(noise);
	free(img);
}

// Skeleton

#define SKEL_MAX_BONES 64

typedef struct skel_bone {
	i32 parent;
	f32 rest[12];  // Bone to object space, 3 rows of 4: columns are the x, y (head to tail), z axes and the head
	f32 world[12]; // Posed bone to object space
	f32 rot[3];
	f32 loc[3];
} skel_bone_t;

static skel_bone_t skel_bones[SKEL_MAX_BONES];
static i32         skel_bone_count  = 0;
static f32        *skel_mats        = NULL;
static i32         skel_frame_count = 0;
static i32         skel_frame_cap   = 0;

// out = a * b for 3x4 affine matrices
static void skel_mul(f32 *a, f32 *b, f32 *out) {
	f32 m[12];
	for (i32 r = 0; r < 3; ++r) {
		for (i32 c = 0; c < 4; ++c) {
			m[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + (c == 3 ? a[r * 4 + 3] : 0.0f);
		}
	}
	memcpy(out, m, sizeof(m));
}

static void skel_inverse(f32 *a, f32 *out) {
	f32 m[12];
	for (i32 r = 0; r < 3; ++r) {
		for (i32 c = 0; c < 3; ++c) {
			m[r * 4 + c] = a[c * 4 + r];
		}
		m[r * 4 + 3] = -(a[3] * a[r] + a[7] * a[4 + r] + a[11] * a[8 + r]);
	}
	memcpy(out, m, sizeof(m));
}

void skel_begin() {
	skel_bone_count  = 0;
	skel_frame_count = 0;
}

// Adds a bone from head to tail
i32 skel_bone(i32 parent, f32 hx, f32 hy, f32 hz, f32 tx, f32 ty, f32 tz, f32 zx, f32 zy, f32 zz) {
	if (skel_bone_count == SKEL_MAX_BONES) {
		return -1;
	}
	skel_bone_t *b = &skel_bones[skel_bone_count];
	memset(b, 0, sizeof(skel_bone_t));
	b->parent = parent;
	f32 y[3]  = {tx - hx, ty - hy, tz - hz};
	geom_normalize(y);
	f32 d    = zx * y[0] + zy * y[1] + zz * y[2];
	f32 z[3] = {zx - y[0] * d, zy - y[1] * d, zz - y[2] * d};
	geom_normalize(z);
	f32 x[3];
	geom_cross(y, z, x);
	f32 h[3] = {hx, hy, hz};
	for (i32 r = 0; r < 3; ++r) {
		b->rest[r * 4]     = x[r];
		b->rest[r * 4 + 1] = y[r];
		b->rest[r * 4 + 2] = z[r];
		b->rest[r * 4 + 3] = h[r];
	}
	return skel_bone_count++;
}

void skel_clip_begin() {
	skel_frame_count = 0;
}

void skel_pose(i32 bone, f32 rx, f32 ry, f32 rz, f32 lx, f32 ly, f32 lz) {
	if (bone < 0 || bone >= skel_bone_count) {
		return;
	}
	skel_bone_t *b = &skel_bones[bone];
	b->rot[0]      = rx;
	b->rot[1]      = ry;
	b->rot[2]      = rz;
	b->loc[0]      = lx;
	b->loc[1]      = ly;
	b->loc[2]      = lz;
}

void skel_frame() {
	if (skel_frame_count == skel_frame_cap) {
		skel_frame_cap = skel_frame_cap == 0 ? 64 : skel_frame_cap * 2;
		skel_mats      = realloc(skel_mats, sizeof(f32) * 12 * SKEL_MAX_BONES * skel_frame_cap);
	}
	f32 *out = &skel_mats[skel_frame_count * skel_bone_count * 12];
	for (i32 i = 0; i < skel_bone_count; ++i) {
		skel_bone_t *b = &skel_bones[i];
		// Basis = translation * Rz * Ry * Rx
		f32 rx[9], ry[9], rz[9], m[9], r[9];
		geom_rotation(0, b->rot[0], rx);
		geom_rotation(1, b->rot[1], ry);
		geom_rotation(2, b->rot[2], rz);
		for (i32 k = 0; k < 9; ++k) {
			m[k] = rz[(k / 3) * 3] * ry[k % 3] + rz[(k / 3) * 3 + 1] * ry[3 + k % 3] + rz[(k / 3) * 3 + 2] * ry[6 + k % 3];
		}
		for (i32 k = 0; k < 9; ++k) {
			r[k] = m[(k / 3) * 3] * rx[k % 3] + m[(k / 3) * 3 + 1] * rx[3 + k % 3] + m[(k / 3) * 3 + 2] * rx[6 + k % 3];
		}
		f32 basis[12];
		for (i32 row = 0; row < 3; ++row) {
			basis[row * 4]     = r[row * 3];
			basis[row * 4 + 1] = r[row * 3 + 1];
			basis[row * 4 + 2] = r[row * 3 + 2];
			basis[row * 4 + 3] = b->loc[row];
		}
		// World = parent world * (parent rest^-1 * rest) * basis
		if (b->parent >= 0) {
			skel_bone_t *p = &skel_bones[b->parent];
			f32          local[12];
			skel_inverse(p->rest, local);
			skel_mul(local, b->rest, local);
			skel_mul(p->world, local, b->world);
		}
		else {
			memcpy(b->world, b->rest, sizeof(b->rest));
		}
		skel_mul(b->world, basis, b->world);
		// Skinning matrix = world * rest^-1
		f32 inv[12];
		skel_inverse(b->rest, inv);
		skel_mul(b->world, inv, &out[i * 12]);
	}
	for (i32 i = 0; i < skel_bone_count; ++i) {
		memset(skel_bones[i].rot, 0, sizeof(f32) * 3);
		memset(skel_bones[i].loc, 0, sizeof(f32) * 3);
	}
	skel_frame_count++;
}

// Writes the unwrapped, weighted mesh with the frames of the current clip as a skin file
bool skel_write(geom_t *g, char *name, char *path) {
	if (g->uv == NULL || g->wt == NULL || skel_frame_count == 0) {
		return false;
	}
	i32 *head = malloc(sizeof(i32) * g->vcount);
	i32 *next = malloc(sizeof(i32) * g->icount);
	i32 *src  = malloc(sizeof(i32) * g->icount); // Source vertex of each output vertex
	i32 *corn = malloc(sizeof(i32) * g->icount); // First corner of each output vertex
	u32 *ind  = malloc(sizeof(u32) * g->icount);
	i32  vc   = 0;
	for (i32 v = 0; v < g->vcount; ++v) {
		head[v] = -1;
	}
	for (i32 i = 0; i < g->icount; ++i) {
		i32 v = g->ind[i];
		i32 o = head[v];
		while (o >= 0 && (g->uv[corn[o] * 2] != g->uv[i * 2] || g->uv[corn[o] * 2 + 1] != g->uv[i * 2 + 1])) {
			o = next[o];
		}
		if (o < 0) {
			o       = vc++;
			src[o]  = v;
			corn[o] = i;
			next[o] = head[v];
			head[v] = o;
		}
		ind[i] = o;
	}
	f32 *n       = geom_normals(g);
	f32 *pos     = malloc(sizeof(f32) * 3 * vc);
	f32 *nor     = malloc(sizeof(f32) * 3 * vc);
	f32 *tex     = malloc(sizeof(f32) * 2 * vc);
	u16 *joints  = malloc(sizeof(u16) * 4 * vc);
	f32 *weights = malloc(sizeof(f32) * 4 * vc);
	for (i32 o = 0; o < vc; ++o) {
		i32 v = src[o];
		memcpy(&pos[o * 3], &g->pos[v * 3], sizeof(f32) * 3);
		memcpy(&nor[o * 3], &n[v * 3], sizeof(f32) * 3);
		geom_tex(g, corn[o], &tex[o * 2], &tex[o * 2 + 1]);
		memcpy(&joints[o * 4], &g->jt[v * 4], sizeof(u16) * 4);
		f32 sum = g->wt[v * 4] + g->wt[v * 4 + 1] + g->wt[v * 4 + 2] + g->wt[v * 4 + 3];
		for (i32 k = 0; k < 4; ++k) {
			weights[o * 4 + k] = sum > 0 ? g->wt[v * 4 + k] / sum : 0;
		}
	}
	buffer_t *blob = util_skin_blob_create(name, vc, pos, nor, tex, g->icount, ind, joints, weights, skel_bone_count, skel_frame_count, skel_mats);
	iron_file_save_bytes(path, blob, blob->length);
	free(blob->buffer);
	free(blob);
	free(n);
	free(pos);
	free(nor);
	free(tex);
	free(joints);
	free(weights);
	free(head);
	free(next);
	free(src);
	free(corn);
	free(ind);
	return true;
}

// OBJ export

static void geom_write(char *fmt, f32 a, f32 b, f32 c) {
	char s[128];
	snprintf(s, sizeof(s), fmt, a, b, c);
	export_obj_write_string(geom_out, s);
}

void geom_export_add(geom_t *g, char *name, bool flat) {
	if (geom_out == NULL) {
		geom_out = u8_array_create(0);
		memset(geom_out_base, 0, sizeof(geom_out_base));
	}
	char s[128];
	snprintf(s, sizeof(s), "o %s\n", name);
	export_obj_write_string(geom_out, s);
	for (i32 v = 0; v < g->vcount; ++v) {
		f32 *p = &g->pos[v * 3];
		geom_write("v %.6f %.6f %.6f\n", p[0], p[2], -p[1]);
	}
	if (g->uv != NULL) {
		for (i32 i = 0; i < g->icount; ++i) {
			geom_write("vt %.6f %.6f\n", g->uv[i * 2], g->uv[i * 2 + 1], 0);
		}
	}
	f32 *n = geom_normals(g);
	if (flat) {
		for (i32 i = 0; i < g->icount; i += 3) {
			f32 *a = &g->pos[g->ind[i] * 3], *b = &g->pos[g->ind[i + 1] * 3], *c = &g->pos[g->ind[i + 2] * 3];
			f32  e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
			f32  e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
			f32  f[3]  = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
			f32  l     = fmaxf(geom_len3(f[0], f[1], f[2]), 1e-12f);
			geom_write("vn %.4f %.4f %.4f\n", f[0] / l, f[2] / l, -f[1] / l);
		}
	}
	else {
		for (i32 v = 0; v < g->vcount; ++v) {
			geom_write("vn %.4f %.4f %.4f\n", n[v * 3], n[v * 3 + 2], -n[v * 3 + 1]);
		}
	}
	free(n);
	// Indices are 1-based and global to the file; uvs are per corner, flat normals per face
	i32 *b = geom_out_base;
	for (i32 i = 0; i < g->icount; i += 3) {
		export_obj_write_string(geom_out, "f");
		for (i32 k = 0; k < 3; ++k) {
			i32 vi = g->ind[i + k] + 1 + b[0];
			i32 ni = (flat ? i / 3 : (i32)g->ind[i + k]) + 1 + b[2];
			if (g->uv != NULL) {
				snprintf(s, sizeof(s), " %d/%d/%d", vi, i + k + 1 + b[1], ni);
			}
			else {
				snprintf(s, sizeof(s), " %d//%d", vi, ni);
			}
			export_obj_write_string(geom_out, s);
		}
		export_obj_write_string(geom_out, "\n");
	}
	b[0] += g->vcount;
	b[1] += g->uv != NULL ? g->icount : 0;
	b[2] += flat ? g->icount / 3 : g->vcount;
}

void geom_export_write(char *path) {
	if (geom_out == NULL) {
		return;
	}
	iron_file_save_bytes(path, geom_out, geom_out->length);
	array_delete(geom_out);
	geom_out = NULL;
}
