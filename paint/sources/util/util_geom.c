
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
	f32 *uv; // uv per index (face corner), NULL until unwrapped
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
	c->uv = NULL;
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
