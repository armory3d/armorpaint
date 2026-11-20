#include "../global.h"

#define FRACTURE_K       12 // Spheres per chunk
#define FRACTURE_SUB     4
#define FRACTURE_ITER    8
#define FRACTURE_BS      16 // pos 3, quat 4, vel 3, angvel 3, released, sleeping, still steps
#define FRACTURE_CS      18 // a, b, n 3, ra 3, rb 3, depth, bounce, jn, jt, collider vel 3
#define FRACTURE_PS      8  // pos 3, quat 4, released
#define FRACTURE_MAXC    40000
#define FRACTURE_DENSITY 500.0f
#define FRACTURE_SOAK    2.0f
#define FRACTURE_WALL    0.45f // Max shell thickness relative to the wall

typedef struct mesh_fracture {
	i32    np;
	i32    nt;
	i32    nv;
	f32    seed;
	f32    blast;
	f32    spin;
	f32    start;
	f32    shrink;
	f32    mcx; // Blast center
	f32    mcy;
	f32   *st;
	u8    *ig;  // Ignored pairs
	i32   *pid; // Triangle chunk
	f32   *t0;  // Release frame
	f32   *pc;  // Centers xyz, area
	f32   *lp;  // Spheres, chunk space
	f32   *lg;  // Ground points, chunk space
	i32   *gs;  // First ground point
	f32   *rp;  // Sphere radius
	f32   *rad; // Bounding radius
	f32   *im;  // Inverse mass
	f32   *ii;  // Inverse inertia
	f32   *ct;  // Contacts
	void **cb;  // Moving body of each contact
	i32    nc;
	f32   *touch;
	f32   *th; // Thickness per vertex
} mesh_fracture_t;

static f32 fracture_rnd(mesh_fracture_t *fr, f32 n) {
	f32 x = sinf(n * 12.9898f + fr->seed * 78.233f) * 43758.5453f;
	return x - floorf(x);
}

// q is x, y, z, w
static void fracture_qrot(f32 *q, f32 vx, f32 vy, f32 vz, f32 *o) {
	f32 tx = 2.0f * (q[1] * vz - q[2] * vy);
	f32 ty = 2.0f * (q[2] * vx - q[0] * vz);
	f32 tz = 2.0f * (q[0] * vy - q[1] * vx);
	o[0]   = vx + q[3] * tx + (q[1] * tz - q[2] * ty);
	o[1]   = vy + q[3] * ty + (q[2] * tx - q[0] * tz);
	o[2]   = vz + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static bool fracture_dynamic(mesh_fracture_t *fr, i32 b) {
	return fr->st[b * FRACTURE_BS + 13] > 0.5f && fr->st[b * FRACTURE_BS + 14] < 0.5f;
}

static f32 fracture_inv_mass(mesh_fracture_t *fr, i32 b) {
	return b < 0 || !fracture_dynamic(fr, b) ? 0.0f : fr->im[b];
}

static f32 fracture_inv_inertia(mesh_fracture_t *fr, i32 b) {
	return b < 0 || !fracture_dynamic(fr, b) ? 0.0f : fr->ii[b];
}

static void fracture_sphere_world(mesh_fracture_t *fr, i32 b, i32 i, f32 *o) {
	f32 *s = fr->st + b * FRACTURE_BS;
	f32 *l = fr->lp + (b * FRACTURE_K + i) * 3;
	fracture_qrot(s + 3, l[0], l[1], l[2], o);
	o[0] += s[0];
	o[1] += s[1];
	o[2] += s[2];
}

static bool fracture_chunks_overlap(mesh_fracture_t *fr, i32 a, i32 b) {
	f32 pa[3], pb[3];
	f32 r = fr->rp[a] + fr->rp[b];
	for (i32 i = 0; i < FRACTURE_K; i++) {
		fracture_sphere_world(fr, a, i, pa);
		for (i32 j = 0; j < FRACTURE_K; j++) {
			fracture_sphere_world(fr, b, j, pb);
			f32 dx = pb[0] - pa[0], dy = pb[1] - pa[1], dz = pb[2] - pa[2];
			if (dx * dx + dy * dy + dz * dz < r * r) {
				return true;
			}
		}
	}
	return false;
}

static void *fracture_contact_body(mesh_fracture_t *fr, f32 *c) {
	return fr->cb[(c - fr->ct) / FRACTURE_CS];
}

static vec4_t fracture_contact_point(mesh_fracture_t *fr, f32 *c) {
	f32 *sb = fr->st + (i32)c[1] * FRACTURE_BS;
	return (vec4_t){sb[0] + c[8], sb[1] + c[9], sb[2] + c[10], 1.0f};
}

// Velocity of b relative to a
static void fracture_contact_velocity(mesh_fracture_t *fr, f32 *c, f32 *o) {
	i32  a  = (i32)c[0];
	i32  b  = (i32)c[1];
	f32 *sb = fr->st + b * FRACTURE_BS;
	o[0]    = sb[7] + sb[11] * c[10] - sb[12] * c[9];
	o[1]    = sb[8] + sb[12] * c[8] - sb[10] * c[10];
	o[2]    = sb[9] + sb[10] * c[9] - sb[11] * c[8];
	if (a >= 0) {
		f32 *sa = fr->st + a * FRACTURE_BS;
		o[0] -= sa[7] + sa[11] * c[7] - sa[12] * c[6];
		o[1] -= sa[8] + sa[12] * c[5] - sa[10] * c[7];
		o[2] -= sa[9] + sa[10] * c[6] - sa[11] * c[5];
		return;
	}
	void *body = fracture_contact_body(fr, c);
	if (body != NULL) {
		vec4_t v = physics_body_velocity_at(body, fracture_contact_point(fr, c));
		o[0] -= v.x;
		o[1] -= v.y;
		o[2] -= v.z;
		return;
	}
	o[0] -= c[15];
	o[1] -= c[16];
	o[2] -= c[17];
}

// a is -1 for the world
static void fracture_add_contact(mesh_fracture_t *fr, i32 a, i32 b, f32 nx, f32 ny, f32 nz, f32 px, f32 py, f32 pz, f32 depth, f32 *cv, void *body) {
	if (fr->nc >= FRACTURE_MAXC) {
		return;
	}
	f32 *st = fr->st;
	f32 *c  = fr->ct + fr->nc * FRACTURE_CS;
	c[0]    = (f32)a;
	c[1]    = (f32)b;
	c[2]    = nx;
	c[3]    = ny;
	c[4]    = nz;
	c[5] = c[6] = c[7] = 0.0f;
	if (a >= 0) {
		c[5] = px - st[a * FRACTURE_BS];
		c[6] = py - st[a * FRACTURE_BS + 1];
		c[7] = pz - st[a * FRACTURE_BS + 2];
	}
	c[8]           = px - st[b * FRACTURE_BS];
	c[9]           = py - st[b * FRACTURE_BS + 1];
	c[10]          = pz - st[b * FRACTURE_BS + 2];
	c[11]          = depth;
	c[13]          = 0.0f;
	c[14]          = 0.0f;
	c[15]          = cv != NULL ? cv[0] : 0.0f;
	c[16]          = cv != NULL ? cv[1] : 0.0f;
	c[17]          = cv != NULL ? cv[2] : 0.0f;
	fr->cb[fr->nc] = body;

	// Bounce
	f32 v[3];
	fracture_contact_velocity(fr, c, v);
	f32 vn       = v[0] * nx + v[1] * ny + v[2] * nz;
	c[12]        = vn < -1.0f ? -0.25f * vn : 0.0f;
	fr->touch[b] = 1.0f;
	if (a >= 0) {
		fr->touch[a] = 1.0f;
	}
	fr->nc++;
}

// Impulse p to b, -p to a or the body
static void fracture_apply_impulse(mesh_fracture_t *fr, f32 *c, f32 px, f32 py, f32 pz) {
	i32  a  = (i32)c[0];
	i32  b  = (i32)c[1];
	f32  mb = fracture_inv_mass(fr, b);
	f32  ib = fracture_inv_inertia(fr, b);
	f32 *sb = fr->st + b * FRACTURE_BS;
	sb[7] += px * mb;
	sb[8] += py * mb;
	sb[9] += pz * mb;
	sb[10] += ib * (c[9] * pz - c[10] * py);
	sb[11] += ib * (c[10] * px - c[8] * pz);
	sb[12] += ib * (c[8] * py - c[9] * px);
	if (a >= 0) {
		f32  ma = fracture_inv_mass(fr, a);
		f32  ia = fracture_inv_inertia(fr, a);
		f32 *sa = fr->st + a * FRACTURE_BS;
		sa[7] -= px * ma;
		sa[8] -= py * ma;
		sa[9] -= pz * ma;
		sa[10] -= ia * (c[6] * pz - c[7] * py);
		sa[11] -= ia * (c[7] * px - c[5] * pz);
		sa[12] -= ia * (c[5] * py - c[6] * px);
		return;
	}
	void *body = fracture_contact_body(fr, c);
	if (body != NULL) {
		physics_body_apply_impulse_at(body, (vec4_t){-px, -py, -pz, 0.0f}, fracture_contact_point(fr, c));
	}
}

// Inverse effective mass along d
static f32 fracture_contact_k(mesh_fracture_t *fr, f32 *c, f32 dx, f32 dy, f32 dz) {
	i32 a  = (i32)c[0];
	i32 b  = (i32)c[1];
	f32 cx = c[9] * dz - c[10] * dy, cy = c[10] * dx - c[8] * dz, cz = c[8] * dy - c[9] * dx;
	f32 k = fracture_inv_mass(fr, b) + fracture_inv_inertia(fr, b) * (cx * cx + cy * cy + cz * cz);
	if (a >= 0) {
		cx = c[6] * dz - c[7] * dy;
		cy = c[7] * dx - c[5] * dz;
		cz = c[5] * dy - c[6] * dx;
		k += fracture_inv_mass(fr, a) + fracture_inv_inertia(fr, a) * (cx * cx + cy * cy + cz * cz);
	}
	else if (fracture_contact_body(fr, c) != NULL) {
		k += physics_body_give(fracture_contact_body(fr, c), fracture_contact_point(fr, c), (vec4_t){dx, dy, dz, 0.0f});
	}
	return k;
}

// Breaks chunk p off
static void fracture_release(mesh_fracture_t *fr, i32 p) {
	i32  np = fr->np;
	f32 *st = fr->st;
	f32 *pc = fr->pc;
	f32 *s  = st + p * FRACTURE_BS;
	s[13]   = 1.0f;
	f32 ox = pc[p * 4] - fr->mcx, oy = pc[p * 4 + 1] - fr->mcy;
	f32 ol = fmaxf(sqrtf(ox * ox + oy * oy), 0.0001f);
	f32 bs = fr->blast * (0.4f + fracture_rnd(fr, p * 7.77f + 3.0f));
	s[7]   = ox / ol * bs + (fracture_rnd(fr, p * 2.13f + 4.0f) - 0.5f) * fr->blast * 0.5f;
	s[8]   = oy / ol * bs + (fracture_rnd(fr, p * 4.71f + 5.0f) - 0.5f) * fr->blast * 0.5f;
	s[9]   = fr->blast * 0.5f * fracture_rnd(fr, p * 9.13f + 6.0f);
	f32 sp = fr->spin * (1.0f + 3.0f * fracture_rnd(fr, p * 3.3f + 10.0f));
	s[10]  = (fracture_rnd(fr, p * 6.1f + 7.0f) - 0.5f) * sp;
	s[11]  = (fracture_rnd(fr, p * 8.3f + 8.0f) - 0.5f) * sp;
	s[12]  = (fracture_rnd(fr, p * 1.9f + 9.0f) - 0.5f) * sp;
	for (i32 q = 0; q < np; q++) {
		if (q == p) {
			continue;
		}
		f32 dx = st[q * FRACTURE_BS] - s[0], dy = st[q * FRACTURE_BS + 1] - s[1], dz = st[q * FRACTURE_BS + 2] - s[2];
		f32 rr = fr->rad[p] + fr->rad[q];
		if (dx * dx + dy * dy + dz * dz > rr * rr) {
			continue;
		}
		if (fracture_chunks_overlap(fr, p, q)) {
			fr->ig[p < q ? p * np + q : q * np + p] = 1;
		}
	}
}

static void fracture_step(mesh_fracture_t *fr, f32 dt, physics_sim_t *grid) {
	i32    np   = fr->np;
	f32   *st   = fr->st;
	vec4_t g    = physics_get_gravity();
	i32    pull = physics_field_any(0);
	f32    pa[3], pb[3];
	for (i32 b = 0; b < np; b++) {
		fr->touch[b] = 0.0f;
		if (!fracture_dynamic(fr, b)) {
			continue;
		}
		// Forces
		f32   *s = st + b * FRACTURE_BS;
		vec4_t p = {s[0], s[1], s[2], 1.0f};
		vec4_t f = pull ? physics_field_force(p) : (vec4_t){0};
		s[7] += (g.x + f.x) * dt;
		s[8] += (g.y + f.y) * dt;
		s[9] += (g.z + f.z) * dt;
		vec4_t u;
		f32    wet;
		if (grid != NULL && physics_sim_sample(grid, p, &u, &wet) && wet > 0.0f) {
			f32 k = fminf(FRACTURE_SOAK * dt, 1.0f) * wet;
			s[7] += (u.x - s[7]) * k;
			s[8] += (u.y - s[8]) * k;
			s[9] += (u.z - s[9]) * k;
		}
	}

	// Contacts with the world, speculative within a substep
	f32    gl = vec4_len(g);
	vec4_t up = gl > 1e-6f ? vec4_mult(g, -1.0f / gl) : (vec4_t){0.0f, 0.0f, 1.0f, 0.0f};
	fr->nc    = 0;
	for (i32 b = 0; b < np; b++) {
		f32          *s      = st + b * FRACTURE_BS;
		vec4_t        center = {s[0], s[1], s[2], 1.0f};
		physics_hit_t hit;
		f32           sv = sqrtf(s[7] * s[7] + s[8] * s[8] + s[9] * s[9]);
		f32           sw = sqrtf(s[10] * s[10] + s[11] * s[11] + s[12] * s[12]);
		f32           h  = (sv + sw * fr->rad[b]) * dt + 0.01f;
		if (!physics_collide_point(&center, fr->rad[b] + h * 3.0f, &hit)) {
			continue;
		}
		// A collider moving into a resting chunk breaks it off
		if (!fracture_dynamic(fr, b)) {
			if (vec4_len(hit.vel) > 0.6f) {
				if (s[13] < 0.5f) {
					fracture_release(fr, b);
				}
				s[14] = 0.0f;
				s[15] = 0.0f;
			}
			else if (hit.body == NULL) {
				continue;
			}
		}
		for (i32 i = fr->gs[b]; i < fr->gs[b + 1]; i++) {
			fracture_qrot(s + 3, fr->lg[i * 3], fr->lg[i * 3 + 1], fr->lg[i * 3 + 2], pa);
			vec4_t p    = {pa[0] + s[0], pa[1] + s[1], pa[2] + s[2], 1.0f};
			vec4_t from = vec4_add(p, vec4_mult(up, h));
			vec4_t q    = from;
			if (!physics_collide_point(&q, h * 2.0f, &hit)) {
				continue;
			}
			vec4_t d = vec4_sub(q, from);
			f32    l = vec4_len(d);
			if (l < 1e-9f) {
				continue;
			}
			vec4_t n     = vec4_mult(d, 1.0f / l);
			f32    depth = l - h * 2.0f + h * vec4_dot(up, n);
			f32    cv[3] = {hit.vel.x, hit.vel.y, hit.vel.z};
			fracture_add_contact(fr, -1, b, n.x, n.y, n.z, p.x, p.y, p.z, depth, cv, hit.body);
		}
	}

	// Chunk contacts
	for (i32 a = 0; a < np; a++) {
		f32 *sa = st + a * FRACTURE_BS;
		for (i32 b = a + 1; b < np; b++) {
			bool da = fracture_dynamic(fr, a);
			bool db = fracture_dynamic(fr, b);
			if (!da && !db) {
				continue;
			}
			f32 *sb = st + b * FRACTURE_BS;
			f32  dx = sb[0] - sa[0], dy = sb[1] - sa[1], dz = sb[2] - sa[2];
			f32  rr = fr->rad[a] + fr->rad[b];
			if (dx * dx + dy * dy + dz * dz > rr * rr) {
				continue;
			}
			i32 ig = a * np + b;
			if (fr->ig[ig]) {
				if (!fracture_chunks_overlap(fr, a, b)) {
					fr->ig[ig] = 0;
				}
				continue;
			}
			// Wake
			if (!da || !db) {
				i32  other = da ? a : b;
				i32  rest  = da ? b : a;
				f32 *so    = st + other * FRACTURE_BS;
				f32  sp    = so[7] * so[7] + so[8] * so[8] + so[9] * so[9];
				if (st[rest * FRACTURE_BS + 13] > 0.5f && sp > 0.36f) {
					st[rest * FRACTURE_BS + 14] = 0.0f;
					st[rest * FRACTURE_BS + 15] = 0.0f;
				}
			}
			f32 r = fr->rp[a] + fr->rp[b];
			for (i32 i = 0; i < FRACTURE_K; i++) {
				fracture_sphere_world(fr, a, i, pa);
				for (i32 j = 0; j < FRACTURE_K; j++) {
					fracture_sphere_world(fr, b, j, pb);
					f32 ex = pb[0] - pa[0], ey = pb[1] - pa[1], ez = pb[2] - pa[2];
					f32 d2 = ex * ex + ey * ey + ez * ez;
					if (d2 >= r * r || d2 < 1e-12f) {
						continue;
					}
					f32 d = sqrtf(d2);
					fracture_add_contact(fr, a, b, ex / d, ey / d, ez / d, (pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f, r - d, NULL,
					                     NULL);
				}
			}
		}
	}

	// Sequential impulses
	f32 v[3];
	for (i32 it = 0; it < FRACTURE_ITER; it++) {
		for (i32 ci = 0; ci < fr->nc; ci++) {
			f32 *c = fr->ct + ci * FRACTURE_CS;
			fracture_contact_velocity(fr, c, v);
			f32 vn     = v[0] * c[2] + v[1] * c[3] + v[2] * c[4];
			f32 target = c[11] < 0.0f ? c[11] / dt : fmaxf(fminf(0.2f / dt * fmaxf(c[11] - 0.003f, 0.0f), 1.0f), c[12]);
			f32 k      = fracture_contact_k(fr, c, c[2], c[3], c[4]);
			if (k < 1e-12f) {
				continue;
			}
			f32 dl  = (target - vn) / k;
			f32 acc = fmaxf(c[13] + dl, 0.0f);
			dl      = acc - c[13];
			c[13]   = acc;
			fracture_apply_impulse(fr, c, c[2] * dl, c[3] * dl, c[4] * dl);

			fracture_contact_velocity(fr, c, v);
			vn     = v[0] * c[2] + v[1] * c[3] + v[2] * c[4];
			f32 tx = v[0] - c[2] * vn, ty = v[1] - c[3] * vn, tz = v[2] - c[4] * vn;
			f32 tl = sqrtf(tx * tx + ty * ty + tz * tz);
			if (tl < 1e-6f) {
				continue;
			}
			tx /= tl;
			ty /= tl;
			tz /= tl;
			f32 kt   = fracture_contact_k(fr, c, tx, ty, tz);
			f32 dt2  = -tl / kt;
			f32 lim  = 0.6f * c[13];
			f32 acct = fmaxf(c[14] + dt2, -lim);
			dt2      = acct - c[14];
			c[14]    = acct;
			fracture_apply_impulse(fr, c, tx * dt2, ty * dt2, tz * dt2);
		}
	}

	// Integrate and sleep
	for (i32 b = 0; b < np; b++) {
		if (!fracture_dynamic(fr, b)) {
			continue;
		}
		f32 *s  = st + b * FRACTURE_BS;
		f32  ld = 1.0f / (1.0f + dt * 0.1f);
		f32  ad = 1.0f / (1.0f + dt * 0.8f);
		s[7] *= ld;
		s[8] *= ld;
		s[9] *= ld;
		s[10] *= ad;
		s[11] *= ad;
		s[12] *= ad;
		s[0] += s[7] * dt;
		s[1] += s[8] * dt;
		s[2] += s[9] * dt;
		f32 wx = s[10], wy = s[11], wz = s[12];
		f32 qx = s[3], qy = s[4], qz = s[5], qw = s[6];
		f32 h = 0.5f * dt;
		s[3] += h * (wx * qw + wy * qz - wz * qy);
		s[4] += h * (wy * qw + wz * qx - wx * qz);
		s[5] += h * (wz * qw + wx * qy - wy * qx);
		s[6] += h * (-wx * qx - wy * qy - wz * qz);
		f32 ql = sqrtf(s[3] * s[3] + s[4] * s[4] + s[5] * s[5] + s[6] * s[6]);
		s[3] /= ql;
		s[4] /= ql;
		s[5] /= ql;
		s[6] /= ql;
		f32 sv = s[7] * s[7] + s[8] * s[8] + s[9] * s[9];
		f32 sw = wx * wx + wy * wy + wz * wz;
		if (fr->touch[b] > 0.5f && sv < 0.08f * 0.08f && sw < 0.5f * 0.5f) {
			s[15] += 1.0f;
			if (s[15] > 12.0f) {
				s[14] = 1.0f;
				s[7] = s[8] = s[9] = s[10] = s[11] = s[12] = 0.0f;
			}
		}
		else {
			s[15] = 0.0f;
		}
	}
}

static vec4_t fracture_vert(f32 *p, i32 i) {
	return (vec4_t){p[i * 3], p[i * 3 + 1], p[i * 3 + 2], 1.0f};
}

static i32 fracture_cell(f32 x, f32 mn, f32 cell, i32 n) {
	i32 i = (i32)floorf((x - mn) / cell);
	return i < 0 ? 0 : i >= n ? n - 1 : i;
}

// Shell thickness per vertex, limited by the distance to the opposite surface so thin parts do not poke through
static f32 *fracture_thickness(mesh_geo_t *in, f32 thick) {
	i32  nv  = in->vertex_count;
	i32  nt  = in->index_count / 3;
	f32 *ip  = in->pos;
	u32 *ii  = in->ind;
	f32 *th  = malloc(sizeof(f32) * nv);
	f32  len = thick / FRACTURE_WALL;

	// Grid with cells at least as long as the rays, a ray touches up to 8 cells
	f32 mn[3] = {1e9f, 1e9f, 1e9f};
	f32 mx[3] = {-1e9f, -1e9f, -1e9f};
	for (i32 i = 0; i < nv * 3; i++) {
		mn[i % 3] = fminf(mn[i % 3], ip[i]);
		mx[i % 3] = fmaxf(mx[i % 3], ip[i]);
	}
	f32 cell = fmaxf(fmaxf(len, fmaxf(mx[0] - mn[0], fmaxf(mx[1] - mn[1], mx[2] - mn[2])) / 64.0f), 1e-6f);
	i32 n[3];
	for (i32 a = 0; a < 3; a++) {
		n[a] = (i32)((mx[a] - mn[a]) / cell) + 1;
	}

	// Triangles per cell
	i32  cells = n[0] * n[1] * n[2];
	i32 *gs    = calloc(cells + 1, sizeof(i32));
	i32 *gl    = NULL;
	i32  lo[3], hi[3];
	for (i32 fill = 0; fill < 2; fill++) {
		for (i32 t = 0; t < nt; t++) {
			for (i32 a = 0; a < 3; a++) {
				f32 x = ip[ii[t * 3] * 3 + a], y = ip[ii[t * 3 + 1] * 3 + a], z = ip[ii[t * 3 + 2] * 3 + a];
				lo[a] = fracture_cell(fminf(x, fminf(y, z)), mn[a], cell, n[a]);
				hi[a] = fracture_cell(fmaxf(x, fmaxf(y, z)), mn[a], cell, n[a]);
			}
			for (i32 z = lo[2]; z <= hi[2]; z++) {
				for (i32 y = lo[1]; y <= hi[1]; y++) {
					for (i32 x = lo[0]; x <= hi[0]; x++) {
						i32 c = (z * n[1] + y) * n[0] + x;
						if (fill) {
							gl[--gs[c]] = t;
						}
						else {
							gs[c]++;
						}
					}
				}
			}
		}
		// Cell ends, filling counts them down to the starts
		if (!fill) {
			for (i32 c = 1; c < cells; c++) {
				gs[c] += gs[c - 1];
			}
			gs[cells] = gs[cells - 1];
			gl        = malloc(sizeof(i32) * math_max(gs[cells], 1));
		}
	}

	// Nearest hit along the inward normal
	i32 *stamp = malloc(sizeof(i32) * nt);
	for (i32 t = 0; t < nt; t++) {
		stamp[t] = -1;
	}
	for (i32 v = 0; v < nv; v++) {
		vec4_t o    = fracture_vert(ip, v);
		ray_t  r    = {o, vec4_norm(vec4_mult(fracture_vert(in->nor, v), -1.0f))};
		f32    best = len;
		for (i32 a = 0; a < 3; a++) {
			f32 x = (&o.x)[a], y = x + (&r.dir.x)[a] * len;
			lo[a] = fracture_cell(fminf(x, y), mn[a], cell, n[a]);
			hi[a] = fracture_cell(fmaxf(x, y), mn[a], cell, n[a]);
		}
		for (i32 z = lo[2]; z <= hi[2]; z++) {
			for (i32 y = lo[1]; y <= hi[1]; y++) {
				for (i32 x = lo[0]; x <= hi[0]; x++) {
					i32 c = (z * n[1] + y) * n[0] + x;
					for (i32 j = gs[c]; j < gs[c + 1]; j++) {
						i32 t = gl[j];
						if (stamp[t] == v) {
							continue;
						}
						stamp[t] = v;
						vec4_t h =
						    ray_intersect_triangle(&r, fracture_vert(ip, ii[t * 3]), fracture_vert(ip, ii[t * 3 + 1]), fracture_vert(ip, ii[t * 3 + 2]), false);
						f32 d = vec4_isnan(h) ? best : vec4_dist(h, o);
						// Triangles sharing the vertex hit at zero
						if (d > len * 0.001f) {
							best = fminf(best, d);
						}
					}
				}
			}
		}
		th[v] = fminf(thick, best * FRACTURE_WALL);
	}
	free(stamp);
	free(gs);
	free(gl);
	return th;
}

// NULL when there are no chunks
mesh_fracture_t *mesh_fracture_create(mesh_geo_t *in, ui_node_t *node) {
	i32 np     = (i32)floorf(nodes_mesh_value(node, 1) + 0.5f);
	f32 start  = nodes_mesh_value(node, 2);
	f32 spread = nodes_mesh_value(node, 3);
	f32 thick  = fmaxf(nodes_mesh_value(node, 6), 0.001f);
	f32 gap    = nodes_mesh_value(node, 7);
	i32 nv     = in->vertex_count;
	i32 nt     = in->index_count / 3;
	if (np < 1 || nt == 0) {
		return NULL;
	}
	mesh_fracture_t *fr = calloc(1, sizeof(mesh_fracture_t));
	fr->np              = np;
	fr->nt              = nt;
	fr->nv              = nv;
	fr->th              = fracture_thickness(in, thick);
	fr->start           = start;
	fr->blast           = nodes_mesh_value(node, 4);
	fr->spin            = nodes_mesh_value(node, 5);
	fr->shrink          = 1.0f - gap;
	fr->seed            = nodes_mesh_value(node, 8);
	fr->pid             = malloc(sizeof(i32) * nt);
	f32  shrink         = fr->shrink;
	f32 *ip             = in->pos;
	f32 *inn            = in->nor;
	u32 *ii             = in->ind;

	f32 bmin[3] = {1e9f, 1e9f, 1e9f};
	f32 bmax[3] = {-1e9f, -1e9f, -1e9f};
	for (i32 i = 0; i < nv; i++) {
		for (i32 k = 0; k < 3; k++) {
			bmin[k] = fminf(bmin[k], ip[i * 3 + k]);
			bmax[k] = fmaxf(bmax[k], ip[i * 3 + k]);
		}
	}
	fr->mcx = (bmin[0] + bmax[0]) * 0.5f;
	fr->mcy = (bmin[1] + bmax[1]) * 0.5f;
	f32 hz  = fmaxf(bmax[2] - bmin[2], 0.0001f);

	// Voronoi seeds by area
	f32 *ta   = malloc(sizeof(f32) * nt);
	f32 *ca   = malloc(sizeof(f32) * nt);
	f32  area = 0.0f;
	for (i32 t = 0; t < nt; t++) {
		f32 *a     = ip + ii[t * 3] * 3;
		f32 *b     = ip + ii[t * 3 + 1] * 3;
		f32 *c     = ip + ii[t * 3 + 2] * 3;
		f32  e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		f32  e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
		f32  cx    = e1[1] * e2[2] - e1[2] * e2[1];
		f32  cy    = e1[2] * e2[0] - e1[0] * e2[2];
		f32  cz    = e1[0] * e2[1] - e1[1] * e2[0];
		ta[t]      = sqrtf(cx * cx + cy * cy + cz * cz) * 0.5f;
		area += ta[t];
		ca[t] = area;
	}
	f32 *sd = malloc(sizeof(f32) * np * 3);
	for (i32 p = 0; p < np; p++) {
		f32 r  = fracture_rnd(fr, p * 3.17f + 1.0f) * area;
		i32 lo = 0, hi = nt - 1;
		while (lo < hi) {
			i32 mid = (lo + hi) / 2;
			if (ca[mid] < r) {
				lo = mid + 1;
			}
			else {
				hi = mid;
			}
		}
		f32 u = fracture_rnd(fr, p * 5.53f + 11.0f);
		f32 w = fracture_rnd(fr, p * 2.47f + 12.0f);
		if (u + w > 1.0f) {
			u = 1.0f - u;
			w = 1.0f - w;
		}
		f32 *a = ip + ii[lo * 3] * 3;
		f32 *b = ip + ii[lo * 3 + 1] * 3;
		f32 *c = ip + ii[lo * 3 + 2] * 3;
		for (i32 k = 0; k < 3; k++) {
			sd[p * 3 + k] = a[k] + (b[k] - a[k]) * u + (c[k] - a[k]) * w;
		}
	}

	// Nearest seed
	i32 *pid = fr->pid;
	f32 *tc  = malloc(sizeof(f32) * nt * 3);
	fr->pc   = calloc(np * 4, sizeof(f32));
	for (i32 t = 0; t < nt; t++) {
		f32 *a        = ip + ii[t * 3] * 3;
		f32 *b        = ip + ii[t * 3 + 1] * 3;
		f32 *c        = ip + ii[t * 3 + 2] * 3;
		f32  x        = (a[0] + b[0] + c[0]) / 3.0f;
		f32  y        = (a[1] + b[1] + c[1]) / 3.0f;
		f32  z        = (a[2] + b[2] + c[2]) / 3.0f;
		tc[t * 3]     = x;
		tc[t * 3 + 1] = y;
		tc[t * 3 + 2] = z;
		i32 best      = 0;
		f32 bd        = 1e9f;
		for (i32 p = 0; p < np; p++) {
			f32 dx = x - sd[p * 3], dy = y - sd[p * 3 + 1], dz = z - sd[p * 3 + 2];
			f32 d = dx * dx + dy * dy + dz * dz;
			if (d < bd) {
				bd   = d;
				best = p;
			}
		}
		pid[t] = best;
		f32 w  = ta[t] + 1e-9f;
		fr->pc[best * 4] += x * w;
		fr->pc[best * 4 + 1] += y * w;
		fr->pc[best * 4 + 2] += z * w;
		fr->pc[best * 4 + 3] += w;
	}
	f32 *pc = fr->pc;
	for (i32 p = 0; p < np; p++) {
		f32 n = fmaxf(pc[p * 4 + 3], 1e-9f);
		pc[p * 4] /= n;
		pc[p * 4 + 1] /= n;
		pc[p * 4 + 2] /= n;
	}

	// Chunk triangles
	i32 *cs = calloc(np + 1, sizeof(i32));
	i32 *cl = malloc(sizeof(i32) * nt);
	for (i32 t = 0; t < nt; t++) {
		cs[pid[t] + 1] += 1;
	}
	for (i32 p = 0; p < np; p++) {
		cs[p + 1] += cs[p];
	}
	i32 *cf = calloc(np, sizeof(i32));
	for (i32 t = 0; t < nt; t++) {
		i32 p             = pid[t];
		cl[cs[p] + cf[p]] = t;
		cf[p] += 1;
	}

	// Ground points
	fr->gs     = calloc(np + 1, sizeof(i32));
	fr->lg     = malloc(sizeof(f32) * nt * 3 * 2 * 3);
	i32 *stamp = malloc(sizeof(i32) * (nv + 1));
	for (i32 i = 0; i < nv; i++) {
		stamp[i] = -1;
	}
	i32 ng = 0;
	for (i32 p = 0; p < np; p++) {
		fr->gs[p] = ng;
		for (i32 j = cs[p]; j < cs[p + 1]; j++) {
			for (i32 k = 0; k < 3; k++) {
				i32 v = ii[cl[j] * 3 + k];
				if (stamp[v] == p) {
					continue;
				}
				stamp[v] = p;
				for (i32 f = 0; f < 2; f++) {
					f32 o = fr->th[v] * f;
					for (i32 a = 0; a < 3; a++) {
						fr->lg[ng * 3 + a] = (ip[v * 3 + a] - inn[v * 3 + a] * o - pc[p * 4 + a]) * shrink;
					}
					ng++;
				}
			}
		}
	}
	fr->gs[np] = ng;

	// Spheres by farthest point sampling
	fr->lp  = malloc(sizeof(f32) * np * FRACTURE_K * 3);
	fr->rp  = malloc(sizeof(f32) * np);
	fr->rad = malloc(sizeof(f32) * np);
	fr->im  = malloc(sizeof(f32) * np);
	fr->ii  = malloc(sizeof(f32) * np);
	f32 *md = malloc(sizeof(f32) * nt);
	for (i32 p = 0; p < np; p++) {
		i32 c0 = cs[p], c1 = cs[p + 1];
		f32 pa     = fmaxf(pc[p * 4 + 3], 1e-6f);
		fr->rp[p]  = fmaxf(0.6f * sqrtf(pa / FRACTURE_K), thick * 0.5f);
		fr->im[p]  = 1.0f / (pa * thick * FRACTURE_DENSITY);
		fr->rad[p] = 0.0f;
		if (c1 == c0) {
			memset(fr->lp + p * FRACTURE_K * 3, 0, sizeof(f32) * FRACTURE_K * 3);
			fr->ii[p]  = fr->im[p];
			fr->rad[p] = fr->rp[p];
			continue;
		}
		for (i32 j = c0; j < c1; j++) {
			md[cl[j]] = 1e9f;
		}
		i32 pick = cl[c0];
		f32 r2   = 0.0f;
		for (i32 i = 0; i < FRACTURE_K; i++) {
			f32  x = tc[pick * 3], y = tc[pick * 3 + 1], z = tc[pick * 3 + 2];
			f32 *na = inn + ii[pick * 3] * 3;
			f32 *nb = inn + ii[pick * 3 + 1] * 3;
			f32 *nc = inn + ii[pick * 3 + 2] * 3;
			f32  nx = na[0] + nb[0] + nc[0];
			f32  ny = na[1] + nb[1] + nc[1];
			f32  nz = na[2] + nb[2] + nc[2];
			f32  nl = fmaxf(sqrtf(nx * nx + ny * ny + nz * nz), 1e-6f);
			f32  ht = (fr->th[ii[pick * 3]] + fr->th[ii[pick * 3 + 1]] + fr->th[ii[pick * 3 + 2]]) / 6.0f;
			nx /= nl;
			ny /= nl;
			nz /= nl;
			f32 *l = fr->lp + (p * FRACTURE_K + i) * 3;
			l[0]   = x - nx * ht - pc[p * 4];
			l[1]   = y - ny * ht - pc[p * 4 + 1];
			l[2]   = z - nz * ht - pc[p * 4 + 2];
			f32 ll = l[0] * l[0] + l[1] * l[1] + l[2] * l[2];
			r2 += ll;
			fr->rad[p] = fmaxf(fr->rad[p], sqrtf(ll) + fmaxf(fr->rp[p], thick * 0.5f));
			f32 far    = -1.0f;
			for (i32 j = c0; j < c1; j++) {
				i32 t  = cl[j];
				f32 dx = tc[t * 3] - x, dy = tc[t * 3 + 1] - y, dz = tc[t * 3 + 2] - z;
				md[t] = fminf(md[t], dx * dx + dy * dy + dz * dz);
				if (md[t] > far) {
					far  = md[t];
					pick = t;
				}
			}
		}
		for (i32 i = fr->gs[p]; i < fr->gs[p + 1]; i++) {
			f32 *g     = fr->lg + i * 3;
			fr->rad[p] = fmaxf(fr->rad[p], sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]));
		}
		// Uniform inertia
		fr->ii[p] = 1.0f / fmaxf((1.0f / fr->im[p]) * (r2 / FRACTURE_K + 0.4f * fr->rp[p] * fr->rp[p]), 1e-9f);
	}

	// Top crumbles first
	fr->st = calloc(np * FRACTURE_BS, sizeof(f32));
	fr->ig = calloc(np * np, 1);
	fr->t0 = malloc(sizeof(f32) * np);
	for (i32 p = 0; p < np; p++) {
		f32 *s    = fr->st + p * FRACTURE_BS;
		s[0]      = pc[p * 4];
		s[1]      = pc[p * 4 + 1];
		s[2]      = pc[p * 4 + 2];
		s[6]      = 1.0f;
		f32 zn    = (pc[p * 4 + 2] - bmin[2]) / hz;
		fr->t0[p] = start + spread * (0.35f * fracture_rnd(fr, p * 5.31f + 2.0f) + 0.65f * (1.0f - zn));
	}
	fr->ct    = malloc(sizeof(f32) * FRACTURE_MAXC * FRACTURE_CS);
	fr->cb    = malloc(sizeof(void *) * FRACTURE_MAXC);
	fr->touch = malloc(sizeof(f32) * np);

	free(ta);
	free(ca);
	free(sd);
	free(tc);
	free(cs);
	free(cl);
	free(cf);
	free(stamp);
	free(md);
	return fr;
}

void mesh_fracture_destroy(mesh_fracture_t *fr) {
	if (fr == NULL) {
		return;
	}
	free(fr->st);
	free(fr->ig);
	free(fr->pid);
	free(fr->t0);
	free(fr->pc);
	free(fr->lp);
	free(fr->lg);
	free(fr->gs);
	free(fr->rp);
	free(fr->rad);
	free(fr->im);
	free(fr->ii);
	free(fr->ct);
	free(fr->cb);
	free(fr->touch);
	free(fr->th);
	free(fr);
}

// Called once per frame after the rigid bodies
void mesh_fracture_frame(mesh_fracture_t *fr, i32 frame, f32 dt, physics_sim_t *grid) {
	for (i32 p = 0; p < fr->np; p++) {
		if (fr->st[p * FRACTURE_BS + 13] < 0.5f && fr->t0[p] <= frame) {
			fracture_release(fr, p);
		}
	}
	for (i32 s = 0; s < FRACTURE_SUB; s++) {
		fracture_step(fr, dt / FRACTURE_SUB, grid);
	}
}

// FRACTURE_PS floats per chunk
void mesh_fracture_poses(mesh_fracture_t *fr, f32 *out) {
	for (i32 p = 0; p < fr->np; p++) {
		memcpy(out + p * FRACTURE_PS, fr->st + p * FRACTURE_BS, sizeof(f32) * 7);
		out[p * FRACTURE_PS + 7] = fr->st[p * FRACTURE_BS + 13];
	}
}

// Position and velocity per ground point
i32 mesh_fracture_obstacles(mesh_fracture_t *fr, f32 *out) {
	f32 a[3];
	for (i32 b = 0; b < fr->np; b++) {
		f32 *s = fr->st + b * FRACTURE_BS;
		for (i32 i = fr->gs[b]; i < fr->gs[b + 1]; i++) {
			fracture_qrot(s + 3, fr->lg[i * 3], fr->lg[i * 3 + 1], fr->lg[i * 3 + 2], a);
			f32 *o = out + i * 6;
			o[0]   = s[0] + a[0];
			o[1]   = s[1] + a[1];
			o[2]   = s[2] + a[2];
			o[3]   = s[7] + s[11] * a[2] - s[12] * a[1];
			o[4]   = s[8] + s[12] * a[0] - s[10] * a[2];
			o[5]   = s[9] + s[10] * a[1] - s[11] * a[0];
		}
	}
	return fr->gs[fr->np];
}

// Pushes a small sphere out of the chunks, no push back
i32 mesh_fracture_collide(mesh_fracture_t *fr, vec4_t *pos, f32 radius, physics_hit_t *hit) {
	i32 any = 0;
	f32 pa[3];
	for (i32 b = 0; b < fr->np; b++) {
		f32 *s  = fr->st + b * FRACTURE_BS;
		f32  dx = pos->x - s[0], dy = pos->y - s[1], dz = pos->z - s[2];
		f32  rr = fr->rad[b] + radius;
		if (dx * dx + dy * dy + dz * dz > rr * rr) {
			continue;
		}
		bool touch = false;
		f32  r     = fr->rp[b] + radius;
		for (i32 i = 0; i < FRACTURE_K; i++) {
			fracture_sphere_world(fr, b, i, pa);
			f32 ex = pos->x - pa[0], ey = pos->y - pa[1], ez = pos->z - pa[2];
			f32 e2 = ex * ex + ey * ey + ez * ez;
			if (e2 >= r * r || e2 < 1e-12f) {
				continue;
			}
			f32 e  = sqrtf(e2);
			pos->x = pa[0] + ex / e * r;
			pos->y = pa[1] + ey / e * r;
			pos->z = pa[2] + ez / e * r;
			touch  = true;
		}
		if (touch) {
			f32 ax = pos->x - s[0], ay = pos->y - s[1], az = pos->z - s[2];
			*hit = (physics_hit_t){{s[7] + s[11] * az - s[12] * ay, s[8] + s[12] * ax - s[10] * az, s[9] + s[10] * ay - s[11] * ax, 0.0f}, NULL};
			any  = 1;
		}
	}
	return any;
}

mesh_geo_t *mesh_fracture_node_eval(ui_node_t *node) {
	mesh_geo_t    *in = nodes_mesh_eval_input(node, 0);
	mesh_object_t *o  = nodes_mesh_eval_object;
	if (in->index_count == 0 || o == NULL) {
		return in;
	}
	nodes_mesh_eval_reads_frame = true;

	f32             *frm;
	mesh_fracture_t *fr    = sim_world_fracture(o->base->uid, node->id, &frm);
	i32              frame = tab_timeline_selected_frame;
	if (fr == NULL || fr->nt != in->index_count / 3 || fr->nv != in->vertex_count) {
		return in; // Not simulated yet
	}
	i32  np       = fr->np;
	bool released = false;
	for (i32 p = 0; p < np && !released; p++) {
		released = frm[p * FRACTURE_PS + 7] > 0.5f;
	}
	if (frame < fr->start && !released) {
		return in;
	}

	// World space
	mat4_t m = o->base->transform->world;
	nodes_mesh_geo_transform(in, m);
	i32  nt     = fr->nt;
	f32 *ip     = in->pos;
	f32 *inn    = in->nor;
	f32 *it     = in->tex;
	u32 *ii     = in->ind;
	f32 *pc     = fr->pc;
	i32 *pid    = fr->pid;
	f32  shrink = fr->shrink;
	f32 *th     = fr->th;

	// Outer face, inner face, side walls
	mesh_geo_t *g  = util_geo_create(nt * 18, nt * 24);
	f32        *op = g->pos;
	f32        *ot = g->tex;
	f32        *on = g->nor;
	u32        *oi = g->ind;
	for (i32 t = 0; t < nt; t++) {
		i32 vb = t * 18;
		for (i32 k = 0; k < 3; k++) {
			i32 s = ii[t * 3 + k];
			f32 x = ip[s * 3], y = ip[s * 3 + 1], z = ip[s * 3 + 2];
			f32 ix = x - inn[s * 3] * th[s], iy = y - inn[s * 3 + 1] * th[s], iz = z - inn[s * 3 + 2] * th[s];
			i32 kn               = k == 2 ? 0 : k + 1;
			i32 j                = k + 3;
			op[(vb + k) * 3]     = x;
			op[(vb + k) * 3 + 1] = y;
			op[(vb + k) * 3 + 2] = z;
			op[(vb + j) * 3]     = ix;
			op[(vb + j) * 3 + 1] = iy;
			op[(vb + j) * 3 + 2] = iz;
			ot[(vb + k) * 2]     = it[s * 2];
			ot[(vb + k) * 2 + 1] = it[s * 2 + 1];
			ot[(vb + j) * 2]     = it[s * 2];
			ot[(vb + j) * 2 + 1] = it[s * 2 + 1];
			// Inner face points inward
			for (i32 a = 0; a < 3; a++) {
				on[(vb + k) * 3 + a] = inn[s * 3 + a];
				on[(vb + j) * 3 + a] = -inn[s * 3 + a];
			}
			// Wall of edge k -> kn
			i32 w               = vb + 6 + k * 4;
			i32 s2              = ii[t * 3 + kn];
			op[w * 3]           = x;
			op[w * 3 + 1]       = y;
			op[w * 3 + 2]       = z;
			op[(w + 1) * 3]     = ip[s2 * 3];
			op[(w + 1) * 3 + 1] = ip[s2 * 3 + 1];
			op[(w + 1) * 3 + 2] = ip[s2 * 3 + 2];
			op[(w + 2) * 3]     = ix;
			op[(w + 2) * 3 + 1] = iy;
			op[(w + 2) * 3 + 2] = iz;
			op[(w + 3) * 3]     = ip[s2 * 3] - inn[s2 * 3] * th[s2];
			op[(w + 3) * 3 + 1] = ip[s2 * 3 + 1] - inn[s2 * 3 + 1] * th[s2];
			op[(w + 3) * 3 + 2] = ip[s2 * 3 + 2] - inn[s2 * 3 + 2] * th[s2];
			// Flat wall normal
			f32 e1x = op[(w + 2) * 3] - x, e1y = op[(w + 2) * 3 + 1] - y, e1z = op[(w + 2) * 3 + 2] - z;
			f32 e2x = op[(w + 1) * 3] - x, e2y = op[(w + 1) * 3 + 1] - y, e2z = op[(w + 1) * 3 + 2] - z;
			f32 nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
			f32 nl = fmaxf(sqrtf(nx * nx + ny * ny + nz * nz), 0.000001f);
			// Wall uvs keep tangents valid
			f32 eu              = it[s2 * 2] - it[s * 2];
			f32 ev              = it[s2 * 2 + 1] - it[s * 2 + 1];
			ot[w * 2]           = it[s * 2];
			ot[w * 2 + 1]       = it[s * 2 + 1];
			ot[(w + 1) * 2]     = it[s2 * 2];
			ot[(w + 1) * 2 + 1] = it[s2 * 2 + 1];
			ot[(w + 2) * 2]     = it[s * 2] - ev * 0.5f;
			ot[(w + 2) * 2 + 1] = it[s * 2 + 1] + eu * 0.5f;
			ot[(w + 3) * 2]     = it[s2 * 2] - ev * 0.5f;
			ot[(w + 3) * 2 + 1] = it[s2 * 2 + 1] + eu * 0.5f;
			for (i32 q = 0; q < 4; q++) {
				on[(w + q) * 3]     = nx / nl;
				on[(w + q) * 3 + 1] = ny / nl;
				on[(w + q) * 3 + 2] = nz / nl;
			}
			i32 ib     = t * 24 + 6 + k * 6;
			oi[ib]     = w;
			oi[ib + 1] = w + 2;
			oi[ib + 2] = w + 1;
			oi[ib + 3] = w + 1;
			oi[ib + 4] = w + 2;
			oi[ib + 5] = w + 3;
		}
		oi[t * 24]     = vb;
		oi[t * 24 + 1] = vb + 1;
		oi[t * 24 + 2] = vb + 2;
		oi[t * 24 + 3] = vb + 3;
		oi[t * 24 + 4] = vb + 5;
		oi[t * 24 + 5] = vb + 4;
	}

	// Place the chunks, those about to break off tremble
	f32 *jit = calloc(np * 3, sizeof(f32));
	for (i32 p = 0; p < np; p++) {
		f32 pre = fr->t0[p] - frame;
		if (frm[p * FRACTURE_PS + 7] < 0.5f && pre > 0.0f && pre < 8.0f) {
			f32 amp        = 0.006f * (1.0f - pre / 8.0f);
			jit[p * 3]     = (fracture_rnd(fr, p * 1.3f + frame * 0.71f) - 0.5f) * amp;
			jit[p * 3 + 1] = (fracture_rnd(fr, p * 2.9f + frame * 0.37f) - 0.5f) * amp;
			jit[p * 3 + 2] = (fracture_rnd(fr, p * 3.7f + frame * 0.53f) - 0.5f) * amp;
		}
	}
	f32 ov[3];
	for (i32 v = 0; v < nt * 18; v++) {
		i32  p = pid[v / 18];
		f32 *r = frm + p * FRACTURE_PS;
		fracture_qrot(r + 3, (op[v * 3] - pc[p * 4]) * shrink, (op[v * 3 + 1] - pc[p * 4 + 1]) * shrink, (op[v * 3 + 2] - pc[p * 4 + 2]) * shrink, ov);
		op[v * 3]     = ov[0] + r[0] + jit[p * 3];
		op[v * 3 + 1] = ov[1] + r[1] + jit[p * 3 + 1];
		op[v * 3 + 2] = ov[2] + r[2] + jit[p * 3 + 2];
		fracture_qrot(r + 3, on[v * 3], on[v * 3 + 1], on[v * 3 + 2], ov);
		on[v * 3]     = ov[0];
		on[v * 3 + 1] = ov[1];
		on[v * 3 + 2] = ov[2];
	}
	free(jit);
	util_geo_free(in);
	nodes_mesh_geo_transform(g, mat4_inv(m));
	return g;
}

void mesh_fracture_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Fracture"), "MESH_FRACTURE", 0xff4982a0, "MESH");
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Pieces"), 100, 1, 500, 1);
	nodes_mesh_def_value(n, _tr("Start Frame"), 20, 0, 250, 1);
	nodes_mesh_def_value(n, _tr("Collapse Frames"), 90, 0, 250, 1);
	nodes_mesh_def_value(n, _tr("Blast"), 0.6, 0.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Spin"), 2.0, 0.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Thickness"), 0.1, 0.001, 1.0, 1000);
	nodes_mesh_def_value(n, _tr("Gap"), 0.02, 0.0, 0.5, 1000);
	nodes_mesh_def_value(n, _tr("Seed"), 1, 0, 100, 1);
	any_map_set(nodes_mesh_node_evals, "MESH_FRACTURE", mesh_fracture_node_eval);
}
