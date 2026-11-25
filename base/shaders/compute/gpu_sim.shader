
cbuffer constant_buffer {
	uint4  op;   // kernel, items, threads per dispatch row, axis
	uint4  dims; // cells xyz, cell count
	uint4  u0;   // per kernel
	uint4  u1;
	uint4  u2;
	uint4  u3;
	float4 f0; // per kernel
	float4 f1; // gravity xyz, dt
	float4 f2; // per kernel
	float4 grid; // corner xyz, cell size
};

uint data[];

// Header slots, SIM_GPU_* in iron_physics_sim.c
const int S_VEL    = 0; // Faces, three per field, one per axis
const int S_MASS   = 3;
const int S_TMP    = 6;
const int S_PM     = 9; // Fixed point momentum
const int S_PMS    = 12; // Fixed point mass
const int S_COEF   = 15;
const int S_CLOSED = 18;
const int S_FORCE  = 21;
const int S_WIND   = 24;
const int S_TEMP   = 27; // Cells
const int S_PRESS  = 28;
const int S_R      = 29;
const int S_D      = 30;
const int S_AD     = 31;
const int S_IDIAG  = 32;
const int S_FIXED  = 33;
const int S_SOLID  = 34; // 1 collider, 2 obstacle point
const int S_SVEL   = 35; // Three fields
const int S_SDF    = 36; // Distance to the colliders, normal xyz: four fields
const int S_BODY   = 37; // Body index + 1, 0 for none
const int S_EMIT   = 38; // Gas index + 1
const int S_CT     = 39; // Eight scratch fields
const int S_DENS   = 40; // One field per gas
const int S_FLAME  = 41;
const int S_PPOS   = 42; // Particles
const int S_PVEL   = 43;
const int S_PAFF   = 44;
const int S_PDEF   = 45;
const int S_PSTR   = 46;
const int S_PINFO  = 47; // Material, owner << 8, dead << 16
const int S_BCELL  = 48; // Cells of the bodies
const int S_BACC   = 49; // 12 fixed point sums per body: liquid impulse, torque, grain impulse, torque
const int S_BPOS   = 50; // Body center, velocity, angular velocity: nine floats per body
const int S_RED    = 51; // Partial sums
const int S_SCAL   = 52;
const int S_STIFF  = 53; // Per owner
const int S_Z      = 54; // Preconditioned residual

const int K_CLEAR       = 0;
const int K_COPY        = 1;
const int K_CLOSED      = 2;
const int K_CLOSE_FACES = 3;
const int K_ADVECT_AIR  = 4;
const int K_ADVECT      = 5;
const int K_BURN        = 6;
const int K_EMIT        = 7;
const int K_BUOYANCY    = 8;
const int K_CURL        = 9;
const int K_VORT_FORCE  = 10;
const int K_VORT_APPLY  = 11;
const int K_FIELDS      = 12;
const int K_P2G         = 13;
const int K_BLEND       = 14;
const int K_VISCOSITY   = 15;
const int K_G2P         = 16;
const int K_MOVE        = 17;
const int K_FIXED       = 18;
const int K_FIXED_LIQ   = 19;
const int K_COEF        = 20;
const int K_PCG_INIT    = 21;
const int K_REDUCE      = 22;
const int K_SCALARS     = 23;
const int K_PCG_AD      = 24;
const int K_PCG_UPDATE  = 25;
const int K_PCG_DIR     = 26;
const int K_GRADIENT    = 27;
const int K_STATS       = 28;
const int K_PUSH_BODIES = 29;
const int K_PCG_RESET   = 30;
const int K_PCG_ZERO    = 31;
const int K_MG_FIXED    = 32;
const int K_MG_FACES    = 33;
const int K_MG_DIAG     = 34;
const int K_MG_SMOOTH   = 35;
const int K_MG_RESIDUAL = 36;
const int K_MG_RESTRICT = 37;
const int K_MG_PROLONG  = 38;
const int K_PCG_DOT     = 39;
const int K_SUBSTEP     = 40;

// Scalars
const int C_RZ    = 0;
const int C_ALPHA = 1;
const int C_BETA  = 2;
const int C_ERR   = 3; // Bits, atomic max
const int C_TOL   = 4;
const int C_DONE  = 5; // 1 converged, 2 nothing to solve
const int C_ITERS = 6;
const int C_SPEED = 7; // Bits, atomic max
const int C_GAS   = 8;
const int C_BMAX  = 9; // Bits, atomic max
const int C_RED2  = 16; // 64 partial sums
const int C_T     = 96; // Frame time stepped so far
const int C_SUB   = 97; // Size of the substep
const int C_IDLE  = 98; // 1 past the end of the frame, the kernels of the substeps queued after it skip
const int C_ITMAX = 99; // Most pcg iterations of a solve this frame
const int C_STEPS = 100; // Substeps run this frame

const float AIR            = 0.01;
const float PRESSURE_EPS   = 0.0001;
const float MASS_FIXED     = 1048576.0; // 2^20
const float BODY_FIXED     = 1024.0;
const float PARTICLE_MASS  = 0.125;
const float POISSON        = 0.3;
const float JELLY_MIN      = 0.3;
const float JELLY_MAX      = 3.0;
const float BODY_DRAG      = 4.0;
const float WIND_RATE      = 2.0;

uint slot(int s) {
	return data[uint(s)];
}

float ld(uint off, int i) {
	return asfloat(data[off + uint(i)]);
}

void st(uint off, int i, float v) {
	data[off + uint(i)] = asuint(v);
}

int ldi(uint off, int i) {
	return int(data[off + uint(i)]);
}

int nx() {
	return int(constant_buffer.dims.x);
}

int ny() {
	return int(constant_buffer.dims.y);
}

int nz() {
	return int(constant_buffer.dims.z);
}

int cells() {
	return int(constant_buffer.dims.w);
}

float cell_size() {
	return constant_buffer.grid.w;
}

float comp(float3 v, int i) {
	if (i == 0) {
		return v.x;
	}
	if (i == 1) {
		return v.y;
	}
	return v.z;
}

int icomp(int3 v, int i) {
	if (i == 0) {
		return v.x;
	}
	if (i == 1) {
		return v.y;
	}
	return v.z;
}

float sgn(float x) {
	if (x >= 0.0) {
		return 1.0;
	}
	return 0.0 - 1.0;
}

// Face grid of an axis
int3 face_dims(int a) {
	int3 n = int3(nx(), ny(), nz());
	if (a == 0) {
		n.x = n.x + 1;
	}
	if (a == 1) {
		n.y = n.y + 1;
	}
	if (a == 2) {
		n.z = n.z + 1;
	}
	return n;
}

float3 face_offset(int a) {
	float3 o = float3(0.5, 0.5, 0.5);
	if (a == 0) {
		o.x = 0.0;
	}
	if (a == 1) {
		o.y = 0.0;
	}
	if (a == 2) {
		o.z = 0.0;
	}
	return o;
}

int3 unflatten(int t, int3 n) {
	return int3(t % n.x, (t / n.x) % n.y, t / (n.x * n.y));
}

int flatten(int3 c, int3 n) {
	return c.x + n.x * (c.y + n.y * c.z);
}

int cell_index(int i, int j, int k) {
	return i + nx() * (j + ny() * k);
}

// Trilinear, in cell units
float trilinear(uint off, int3 n, float3 o, float3 p) {
	float gx = clamp(p.x - o.x, 0.0, float(n.x) - 1.001);
	float gy = clamp(p.y - o.y, 0.0, float(n.y) - 1.001);
	float gz = clamp(p.z - o.z, 0.0, float(n.z) - 1.001);
	int   cx = int(gx);
	int   cy = int(gy);
	int   cz = int(gz);
	float tx = gx - float(cx);
	float ty = gy - float(cy);
	float tz = gz - float(cz);
	int   dj = n.x;
	int   dk = n.x * n.y;
	int   b  = cx + n.x * (cy + n.y * cz);
	float c00 = lerp(ld(off, b), ld(off, b + 1), tx);
	float c10 = lerp(ld(off, b + dj), ld(off, b + dj + 1), tx);
	float c01 = lerp(ld(off, b + dk), ld(off, b + dk + 1), tx);
	float c11 = lerp(ld(off, b + dk + dj), ld(off, b + dk + dj + 1), tx);
	return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

float trilinear_face(int field, int a, float3 p) {
	return trilinear(slot(field + a), face_dims(a), face_offset(a), p);
}

float3 velocity_at(int field, float3 p) {
	return float3(trilinear_face(field, 0, p), trilinear_face(field, 1, p), trilinear_face(field, 2, p));
}

// Exact at a cell center: the faces on both sides
float3 center_velocity(int i, int j, int k) {
	uint  u   = slot(S_VEL);
	uint  v   = slot(S_VEL + 1);
	uint  w   = slot(S_VEL + 2);
	int   fu  = i + (nx() + 1) * (j + ny() * k);
	int   fv  = i + nx() * (j + (ny() + 1) * k);
	int   fw  = cell_index(i, j, k);
	float vx  = ld(u, fu) + (ld(u, fu + 1) - ld(u, fu)) * 0.5;
	float vy  = ld(v, fv) + (ld(v, fv + nx()) - ld(v, fv)) * 0.5;
	float vz  = ld(w, fw) + (ld(w, fw + nx() * ny()) - ld(w, fw)) * 0.5;
	return float3(vx, vy, vz);
}

float3 cell_center(int c) {
	int3 p = unflatten(c, int3(nx(), ny(), nz()));
	return constant_buffer.grid.xyz + (float3(float(p.x), float(p.y), float(p.z)) + 0.5) * cell_size();
}

uint to_fixed(float v, float scale) {
	return uint(int(floor(clamp(v * scale, 0.0 - 2000000000.0, 2000000000.0) + 0.5)));
}

uint hash(uint x) {
	x = (x ^ uint(61)) ^ (x >> uint(16));
	x = x * uint(9);
	x = x ^ (x >> uint(4));
	x = x * uint(668265261);
	x = x ^ (x >> uint(15));
	return x;
}

float random(uint x) {
	return float(hash(x) >> uint(8)) / 16777216.0;
}

void add_fixed(int field, int i, float v, float scale) {
	uint old = atomic_add(data, slot(field) + uint(i), to_fixed(v, scale));
}

void max_bits(int scalar, float v) {
	uint old = atomic_max(data, slot(S_SCAL) + uint(scalar), asuint(v));
}

float scalar(int i) {
	return ld(slot(S_SCAL), i);
}

// Queued ahead the substeps are sized on the gpu, see SIM_GPU_QUEUE_AHEAD. Else the cpu gives the size
float sub_dt() {
#ifdef _QUEUE_AHEAD
	return scalar(C_SUB);
#else
	return constant_buffer.f1.w;
#endif
}

bool solved() {
	return data[slot(S_SCAL) + uint(C_DONE)] != uint(0);
}

// Faces

void closed_faces(int t, int a) {
	int3 n   = face_dims(a);
	int3 c   = unflatten(t, n);
	int  hi  = 0;
	int  lo  = 0;
	uint sol = slot(S_SOLID);
	if (icomp(c, a) < icomp(int3(nx(), ny(), nz()), a)) {
		hi = int(data[sol + uint(cell_index(c.x, c.y, c.z))]);
	}
	int3 b = c;
	if (a == 0) {
		b.x = b.x - 1;
	}
	if (a == 1) {
		b.y = b.y - 1;
	}
	if (a == 2) {
		b.z = b.z - 1;
	}
	if (icomp(b, a) >= 0) {
		lo = int(data[sol + uint(cell_index(b.x, b.y, b.z))]);
	}
	uint v = uint(0);
	if (hi != 0) {
		v = uint(1);
	}
	else if (lo != 0) {
		v = uint(2);
	}
	data[slot(S_CLOSED + a) + uint(t)] = v;
}

// Faces beside solid cells move with the collider
void close_faces(int t, int a) {
	uint closed = data[slot(S_CLOSED + a) + uint(t)];
	if (closed == uint(0)) {
		return;
	}
	int3 c = unflatten(t, face_dims(a));
	if (closed == uint(2)) {
		if (a == 0) {
			c.x = c.x - 1;
		}
		if (a == 1) {
			c.y = c.y - 1;
		}
		if (a == 2) {
			c.z = c.z - 1;
		}
	}
	int cell = cell_index(c.x, c.y, c.z);
	st(slot(S_VEL + a), t, ld(slot(S_SVEL) + uint(a * cells()), cell));
}

void advect_air(int t, int a) {
	int3   n = face_dims(a);
	int3   c = unflatten(t, n);
	float3 p = float3(float(c.x), float(c.y), float(c.z)) + face_offset(a);
	float3 v = velocity_at(S_TMP, p);
	float  s = sub_dt() / cell_size();
	st(slot(S_VEL + a), t, trilinear_face(S_TMP, a, p - v * s));
}

// Up to eight fields at once, their copies in the scratch fields
void advect_cells(int t) {
	int3   c = unflatten(t, int3(nx(), ny(), nz()));
	float3 p = float3(float(c.x), float(c.y), float(c.z)) + 0.5;
	float3 v = center_velocity(c.x, c.y, c.z);
	float3 q = p - v * (sub_dt() / cell_size());
	int3   n = int3(nx(), ny(), nz());
	float3 o = float3(0.5, 0.5, 0.5);
	int    k = int(constant_buffer.op.w);
	uint   s = slot(S_CT);
	for (int f = 0; f < k; f += 1) {
		uint dst = constant_buffer.u0.x;
		if (f == 1) {
			dst = constant_buffer.u0.y;
		}
		if (f == 2) {
			dst = constant_buffer.u0.z;
		}
		if (f == 3) {
			dst = constant_buffer.u0.w;
		}
		if (f == 4) {
			dst = constant_buffer.u1.x;
		}
		if (f == 5) {
			dst = constant_buffer.u1.y;
		}
		if (f == 6) {
			dst = constant_buffer.u1.z;
		}
		if (f == 7) {
			dst = constant_buffer.u1.w;
		}
		st(dst, t, trilinear(s + uint(f * cells()), n, o, q));
	}
}

// Flame burns out into heat and soot, smoke fades, heat cools
void burn(int t) {
	int    gases = int(constant_buffer.u0.x);
	float  dt    = sub_dt();
	float  keep  = 1.0 / (1.0 + dt * constant_buffer.f0.x);
	float  fade  = 1.0 / (1.0 + dt * constant_buffer.f0.w);
	uint   temp  = slot(S_TEMP);
	float  heat  = ld(temp, t);
	for (int g = 0; g < gases; g += 1) {
		uint  df     = slot(S_DENS) + uint(g * cells());
		uint  ff     = slot(S_FLAME) + uint(g * cells());
		float flame  = ld(ff, t);
		float burned = flame * (1.0 - keep);
		float tip    = 1.0 - min(flame * 4.0, 1.0);
		st(ff, t, flame - burned);
		st(df, t, (ld(df, t) + burned * constant_buffer.f0.y * tip) * fade);
		heat = heat + burned * constant_buffer.f0.z;
	}
	st(temp, t, heat / (1.0 + dt * constant_buffer.f2.x));
}

void emit(int t) {
	uint e = data[slot(S_EMIT) + uint(t)];
	if (e == uint(0) || ((constant_buffer.u0.x >> (e - uint(1))) & uint(1)) == uint(0)) {
		return;
	}
	int  g   = int(e) - 1;
	int3 c   = unflatten(t, int3(nx(), ny(), nz()));
	uint sol = slot(S_SOLID);
	// An emitter resting on a collider is in its solid cells, release the gas just above them
	int ke = c.z;
	while (data[sol + uint(cell_index(c.x, c.y, ke))] != uint(0) && ke < c.z + 3 && ke + 1 < nz()) {
		ke += 1;
	}
	int cell = cell_index(c.x, c.y, ke);
	if (data[sol + uint(cell)] != uint(0)) {
		return;
	}
	uint seed = hash(uint(t) ^ hash(constant_buffer.u0.w));
	if (((constant_buffer.u0.y >> uint(g)) & uint(1)) != uint(0)) {
		// Flickers, the flame breaks into tongues as it rises
		uint ff = slot(S_FLAME) + uint(g * cells());
		st(ff, cell, max(ld(ff, cell), 0.5 + 0.5 * random(seed)));
	}
	else {
		uint df = slot(S_DENS) + uint(g * cells());
		st(df, cell, max(ld(df, cell), 1.0));
	}
	st(slot(S_TEMP), cell, max(ld(slot(S_TEMP), cell), 1.0));
	int3 fc = int3(c.x, c.y, ke);
	for (int a = 0; a < 3; a += 1) {
		uint vf = slot(S_VEL + a);
		int  id = flatten(fc, face_dims(a));
		st(vf, id, ld(vf, id) + (random(seed + uint(a + 1)) - 0.5) * constant_buffer.f0.x * sub_dt());
	}
}

void buoyancy(int t) {
	int3  c   = unflatten(t, face_dims(2));
	uint  tf  = slot(S_TEMP);
	float t0  = 0.0;
	float t1  = 0.0;
	if (c.z > 0) {
		t0 = ld(tf, cell_index(c.x, c.y, c.z - 1));
	}
	if (c.z < nz()) {
		t1 = ld(tf, cell_index(c.x, c.y, c.z));
	}
	float air = AIR / (ld(slot(S_MASS + 2), t) + AIR);
	uint  w   = slot(S_VEL + 2);
	st(w, t, ld(w, t) + sub_dt() * constant_buffer.f0.x * (t0 + t1) * 0.5 * air);
}

// Vorticity into scratch 0 - 2, its length into 3
void curl(int t) {
	int3 c   = unflatten(t, int3(nx(), ny(), nz()));
	uint s   = slot(S_CT);
	int  n   = cells();
	float3 o = float3(0.0, 0.0, 0.0);
	if (c.x >= 1 && c.y >= 1 && c.z >= 1 && c.x < nx() - 1 && c.y < ny() - 1 && c.z < nz() - 1) {
		float  h  = 0.5 / cell_size();
		float3 xp = center_velocity(c.x + 1, c.y, c.z);
		float3 xm = center_velocity(c.x - 1, c.y, c.z);
		float3 yp = center_velocity(c.x, c.y + 1, c.z);
		float3 ym = center_velocity(c.x, c.y - 1, c.z);
		float3 zp = center_velocity(c.x, c.y, c.z + 1);
		float3 zm = center_velocity(c.x, c.y, c.z - 1);
		o.x       = ((yp.z - ym.z) - (zp.y - zm.y)) * h;
		o.y       = ((zp.x - zm.x) - (xp.z - xm.z)) * h;
		o.z       = ((xp.y - xm.y) - (yp.x - ym.x)) * h;
	}
	st(s, t, o.x);
	st(s + uint(n), t, o.y);
	st(s + uint(n * 2), t, o.z);
	st(s + uint(n * 3), t, length(o));
}

// Confinement force per cell into scratch 4 - 6
void vorticity_force(int t) {
	int3   c = unflatten(t, int3(nx(), ny(), nz()));
	uint   s = slot(S_CT);
	int    n = cells();
	float3 f = float3(0.0, 0.0, 0.0);
	if (c.x >= 2 && c.y >= 2 && c.z >= 2 && c.x < nx() - 2 && c.y < ny() - 2 && c.z < nz() - 2) {
		uint   len = s + uint(n * 3);
		int    dj  = nx();
		int    dk  = nx() * ny();
		float3 g   = float3(ld(len, t + 1) - ld(len, t - 1), ld(len, t + dj) - ld(len, t - dj), ld(len, t + dk) - ld(len, t - dk)) * 0.5;
		float  gl  = length(g);
		if (gl >= 0.000000001) {
			float3 o = float3(ld(s, t), ld(s + uint(n), t), ld(s + uint(n * 2), t));
			f        = cross(g / gl, o) * (constant_buffer.f0.x * sub_dt());
		}
	}
	st(s + uint(n * 4), t, f.x);
	st(s + uint(n * 5), t, f.y);
	st(s + uint(n * 6), t, f.z);
}

// Half of the force of the cells on both sides of the face
void vorticity_apply(int t, int a) {
	int3  c   = unflatten(t, face_dims(a));
	uint  f   = slot(S_CT) + uint((4 + a) * cells());
	int   ca  = icomp(c, a);
	float sum = 0.0;
	int3  n   = int3(nx(), ny(), nz());
	if (ca < icomp(n, a)) {
		sum = sum + ld(f, cell_index(c.x, c.y, c.z));
	}
	if (ca > 0) {
		int3 b = c;
		if (a == 0) {
			b.x = b.x - 1;
		}
		if (a == 1) {
			b.y = b.y - 1;
		}
		if (a == 2) {
			b.z = b.z - 1;
		}
		sum = sum + ld(f, cell_index(b.x, b.y, b.z));
	}
	float air = AIR / (ld(slot(S_MASS + a), t) + AIR);
	uint  vf  = slot(S_VEL + a);
	st(vf, t, ld(vf, t) + sum * 0.5 * air);
}

// Attractors and wind, sampled at the faces on the cpu
void fields(int t, int a) {
	float m   = ld(slot(S_MASS + a), t);
	float dt  = sub_dt();
	uint  vf  = slot(S_VEL + a);
	float v   = ld(vf, t);
	float air = 0.0;
	if (constant_buffer.u0.x != uint(0)) {
		air = AIR / (m + AIR);
	}
	if (constant_buffer.u0.y != uint(0) && m > 0.0) {
		v = v + ld(slot(S_FORCE + a), t) * dt * (1.0 - air);
	}
	if (constant_buffer.u0.z != uint(0)) {
		v = v + (ld(slot(S_WIND + a), t) - v) * min(dt * WIND_RATE, 1.0) * air;
	}
	st(vf, t, v);
}

// Liquid and air blended by mass
void blend(int t, int a) {
	float m     = float(ldi(slot(S_PMS + a), t)) / MASS_FIXED;
	float mom   = float(ldi(slot(S_PM + a), t)) / MASS_FIXED;
	float g     = comp(constant_buffer.f1.xyz, a);
	float dt    = sub_dt();
	uint  vf    = slot(S_VEL + a);
	st(slot(S_MASS + a), t, m);
	if (constant_buffer.u0.x == uint(0)) {
		float v = 0.0;
		if (m > 0.0) {
			v = mom / m + g * dt;
		}
		st(vf, t, v);
		return;
	}
	float total = m + AIR;
	st(vf, t, (mom + AIR * ld(vf, t)) / total + g * dt * m / total);
}

void viscosity(int t, int a) {
	int3 n = face_dims(a);
	int3 c = unflatten(t, n);
	if (c.x < 1 || c.y < 1 || c.z < 1 || c.x >= n.x - 1 || c.y >= n.y - 1 || c.z >= n.z - 1) {
		return;
	}
	float liquid = min(ld(slot(S_MASS + a), t), 1.0);
	if (liquid <= 0.0) {
		return;
	}
	uint  src = slot(S_TMP + a);
	int   dj  = n.x;
	int   dk  = n.x * n.y;
	float avg = (ld(src, t - 1) + ld(src, t + 1) + ld(src, t - dj) + ld(src, t + dj) + ld(src, t - dk) + ld(src, t + dk)) / 6.0;
	uint  vf  = slot(S_VEL + a);
	st(vf, t, ld(vf, t) + (avg - ld(src, t)) * constant_buffer.f0.x * liquid);
}

// Particles

struct rows3 {
	float3 r0;
	float3 r1;
	float3 r2;
};

struct eigen3 {
	float3 val;
	float3 v0; // Vectors in the columns
	float3 v1;
	float3 v2;
};

float3 weights(float fx) {
	return float3(0.5 * (1.5 - fx) * (1.5 - fx), 0.75 - (fx - 1.0) * (fx - 1.0), 0.5 * (fx - 0.5) * (fx - 0.5));
}

float3 row(rows3 m, int i) {
	if (i == 0) {
		return m.r0;
	}
	if (i == 1) {
		return m.r1;
	}
	return m.r2;
}

rows3 load_mat(int field, int p) {
	uint o = slot(field) + uint(p * 9);
	rows3 m;
	m.r0 = float3(asfloat(data[o]), asfloat(data[o + uint(1)]), asfloat(data[o + uint(2)]));
	m.r1 = float3(asfloat(data[o + uint(3)]), asfloat(data[o + uint(4)]), asfloat(data[o + uint(5)]));
	m.r2 = float3(asfloat(data[o + uint(6)]), asfloat(data[o + uint(7)]), asfloat(data[o + uint(8)]));
	return m;
}

void store_row(int field, int p, int r, float3 v) {
	uint o = slot(field) + uint(p * 9 + r * 3);
	data[o]            = asuint(v.x);
	data[o + uint(1)]  = asuint(v.y);
	data[o + uint(2)]  = asuint(v.z);
}

void store_mat(int field, int p, rows3 m) {
	store_row(field, p, 0, m.r0);
	store_row(field, p, 1, m.r1);
	store_row(field, p, 2, m.r2);
}

float3 load3(int field, int p) {
	uint o = slot(field) + uint(p * 3);
	return float3(asfloat(data[o]), asfloat(data[o + uint(1)]), asfloat(data[o + uint(2)]));
}

void store3(int field, int p, float3 v) {
	uint o = slot(field) + uint(p * 3);
	data[o]           = asuint(v.x);
	data[o + uint(1)] = asuint(v.y);
	data[o + uint(2)] = asuint(v.z);
}

// a times b
rows3 rows_mul(rows3 a, rows3 b) {
	rows3 m;
	m.r0 = b.r0 * a.r0.x + b.r1 * a.r0.y + b.r2 * a.r0.z;
	m.r1 = b.r0 * a.r1.x + b.r1 * a.r1.y + b.r2 * a.r1.z;
	m.r2 = b.r0 * a.r2.x + b.r1 * a.r2.y + b.r2 * a.r2.z;
	return m;
}

// Sum of s_e v_e v_e^T over the eigen3 vectors
rows3 compose(eigen3 e, float3 s) {
	rows3 m;
	m.r0 = e.v0 * (s.x * e.v0.x) + e.v1 * (s.y * e.v1.x) + e.v2 * (s.z * e.v2.x);
	m.r1 = e.v0 * (s.x * e.v0.y) + e.v1 * (s.y * e.v1.y) + e.v2 * (s.z * e.v2.y);
	m.r2 = e.v0 * (s.x * e.v0.z) + e.v1 * (s.y * e.v1.z) + e.v2 * (s.z * e.v2.z);
	return m;
}

// Jacobi, of a symmetric matrix
eigen3 eigen_sym(rows3 m) {
	float  a00 = m.r0.x;
	float  a11 = m.r1.y;
	float  a22 = m.r2.z;
	float  a01 = m.r0.y;
	float  a02 = m.r0.z;
	float  a12 = m.r1.z;
	float3 v0  = float3(1.0, 0.0, 0.0);
	float3 v1  = float3(0.0, 1.0, 0.0);
	float3 v2  = float3(0.0, 0.0, 1.0);
	for (int sweep = 0; sweep < 8; sweep += 1) {
		if (abs(a01) >= 0.000000000001) {
			float th = (a11 - a00) / (2.0 * a01);
			float tt = sgn(th) / (abs(th) + sqrt(th * th + 1.0));
			float c  = 1.0 / sqrt(tt * tt + 1.0);
			float s  = tt * c;
			a00      = a00 - tt * a01;
			a11      = a11 + tt * a01;
			a01      = 0.0;
			float rp = c * a02 - s * a12;
			float rq = s * a02 + c * a12;
			a02      = rp;
			a12      = rq;
			float3 vp = v0 * c - v1 * s;
			float3 vq = v0 * s + v1 * c;
			v0        = vp;
			v1        = vq;
		}
		if (abs(a02) >= 0.000000000001) {
			float th = (a22 - a00) / (2.0 * a02);
			float tt = sgn(th) / (abs(th) + sqrt(th * th + 1.0));
			float c  = 1.0 / sqrt(tt * tt + 1.0);
			float s  = tt * c;
			a00      = a00 - tt * a02;
			a22      = a22 + tt * a02;
			a02      = 0.0;
			float rp = c * a01 - s * a12;
			float rq = s * a01 + c * a12;
			a01      = rp;
			a12      = rq;
			float3 vp = v0 * c - v2 * s;
			float3 vq = v0 * s + v2 * c;
			v0        = vp;
			v2        = vq;
		}
		if (abs(a12) >= 0.000000000001) {
			float th = (a22 - a11) / (2.0 * a12);
			float tt = sgn(th) / (abs(th) + sqrt(th * th + 1.0));
			float c  = 1.0 / sqrt(tt * tt + 1.0);
			float s  = tt * c;
			a11      = a11 - tt * a12;
			a22      = a22 + tt * a12;
			a12      = 0.0;
			float rp = c * a01 - s * a02;
			float rq = s * a01 + c * a02;
			a01      = rp;
			a02      = rq;
			float3 vp = v1 * c - v2 * s;
			float3 vq = v1 * s + v2 * c;
			v1        = vp;
			v2        = vq;
		}
	}
	eigen3 e;
	e.val = float3(a00, a11, a22);
	e.v0  = v0;
	e.v1  = v1;
	e.v2  = v2;
	return e;
}

// Updates the deformation and stores the stress over density, jelly is elastic, sand slides past its friction angle
void solid_stress(int p, rows3 f, int material, int owner) {
	float e      = ld(slot(S_STIFF), owner);
	float mu     = e / (2.0 * (1.0 + POISSON));
	float lambda = e * POISSON / ((1.0 + POISSON) * (1.0 - 2.0 * POISSON));
	rows3  b;
	b.r0      = float3(dot(f.r0, f.r0), dot(f.r0, f.r1), dot(f.r0, f.r2));
	b.r1      = float3(dot(f.r1, f.r0), dot(f.r1, f.r1), dot(f.r1, f.r2));
	b.r2      = float3(dot(f.r2, f.r0), dot(f.r2, f.r1), dot(f.r2, f.r2));
	eigen3 eg  = eigen_sym(b);
	rows3  tau;
	if (material == 1) {
		float3 sig = clamp(sqrt(max(eg.val, float3(0.0, 0.0, 0.0))), float3(JELLY_MIN, JELLY_MIN, JELLY_MIN), float3(JELLY_MAX, JELLY_MAX, JELLY_MAX));
		f          = compose(eg, sig);
		tau        = compose(eg, sig * sig);
		float lnj  = log(sig.x * sig.y * sig.z);
		tau.r0     = (tau.r0 - float3(1.0, 0.0, 0.0)) * mu + float3(lambda * lnj, 0.0, 0.0);
		tau.r1     = (tau.r1 - float3(0.0, 1.0, 0.0)) * mu + float3(0.0, lambda * lnj, 0.0);
		tau.r2     = (tau.r2 - float3(0.0, 0.0, 1.0)) * mu + float3(0.0, 0.0, lambda * lnj);
	}
	else {
		float3 ev    = log(max(eg.val, float3(0.00000001, 0.00000001, 0.00000001))) * 0.5;
		float  tr    = ev.x + ev.y + ev.z;
		float3 dev   = ev - float3(tr / 3.0, tr / 3.0, tr / 3.0);
		float  norm  = length(dev);
		float  alpha = 0.32659863; // sqrt(2 / 3) 2 sin(phi) / (3 - sin(phi)), 30 degrees
		float  slide = norm + (3.0 * lambda + 2.0 * mu) / (2.0 * mu) * tr * alpha;
		float3 ep    = ev;
		if (slide > 0.0) {
			if (tr >= 0.0 || norm < 0.000000001) {
				ep = float3(0.0, 0.0, 0.0);
			}
			else {
				ep = ev - dev * (slide / norm);
			}
		}
		float trp = ep.x + ep.y + ep.z;
		tau       = compose(eg, ep * (2.0 * mu) + float3(trp * lambda, trp * lambda, trp * lambda));
		f         = rows_mul(compose(eg, exp(ep - ev)), f);
	}
	store_mat(S_PDEF, p, f);
	store_mat(S_PSTR, p, tau);
}

int particle_info(int p) {
	return int(data[slot(S_PINFO) + uint(p)]);
}

// Grid coordinates of a particle for a face axis
float3 particle_grid(float3 pos, int a) {
	return (pos - constant_buffer.grid.xyz) / cell_size() - face_offset(a);
}

void p2g(int p) {
	int info = particle_info(p);
	if ((info & 65536) != 0) {
		return;
	}
	float3 pos = load3(S_PPOS, p);
	float3 pv  = load3(S_PVEL, p);
	rows3   aff = load_mat(S_PAFF, p);
	float  h   = cell_size();
	if ((info & 255) != 0) {
		rows3  s = load_mat(S_PSTR, p);
		float k = sub_dt() * 4.0 / (h * h);
		aff.r0  = aff.r0 - s.r0 * k;
		aff.r1  = aff.r1 - s.r1 * k;
		aff.r2  = aff.r2 - s.r2 * k;
	}
	for (int a = 0; a < 3; a += 1) {
		int3   n    = face_dims(a);
		float3 x    = particle_grid(pos, a);
		int3   base = int3(int(floor(x.x - 0.5)), int(floor(x.y - 0.5)), int(floor(x.z - 0.5)));
		float3 wx   = weights(x.x - float(base.x));
		float3 wy   = weights(x.y - float(base.y));
		float3 wz   = weights(x.z - float(base.z));
		float3 c    = row(aff, a);
		float  vp   = comp(pv, a);
		for (int dk = 0; dk < 3; dk += 1) {
			int k = base.z + dk;
			if (k >= 0 && k < n.z) {
				for (int dj = 0; dj < 3; dj += 1) {
					int j = base.y + dj;
					if (j >= 0 && j < n.y) {
						for (int di = 0; di < 3; di += 1) {
							int i = base.x + di;
							if (i >= 0 && i < n.x) {
								float  wt = comp(wx, di) * comp(wy, dj) * comp(wz, dk) * PARTICLE_MASS;
								float3 d  = (float3(float(i), float(j), float(k)) - x) * h;
								int    id = i + n.x * (j + n.y * k);
								add_fixed(S_PMS + a, id, wt, MASS_FIXED);
								add_fixed(S_PM + a, id, wt * (vp + dot(c, d)), MASS_FIXED);
							}
						}
					}
				}
			}
		}
	}
}

void g2p(int p) {
	int info = particle_info(p);
	if ((info & 65536) != 0) {
		return;
	}
	float3 pos = load3(S_PPOS, p);
	float  h   = cell_size();
	float3 v   = float3(0.0, 0.0, 0.0);
	rows3   aff;
	for (int a = 0; a < 3; a += 1) {
		int3   n    = face_dims(a);
		uint   vf   = slot(S_VEL + a);
		float3 x    = particle_grid(pos, a);
		int3   base = int3(int(floor(x.x - 0.5)), int(floor(x.y - 0.5)), int(floor(x.z - 0.5)));
		float3 wx   = weights(x.x - float(base.x));
		float3 wy   = weights(x.y - float(base.y));
		float3 wz   = weights(x.z - float(base.z));
		float  va   = 0.0;
		float3 c    = float3(0.0, 0.0, 0.0);
		for (int dk = 0; dk < 3; dk += 1) {
			int k = clamp(base.z + dk, 0, n.z - 1);
			for (int dj = 0; dj < 3; dj += 1) {
				int j = clamp(base.y + dj, 0, n.y - 1);
				for (int di = 0; di < 3; di += 1) {
					int   i  = clamp(base.x + di, 0, n.x - 1);
					float wt = comp(wx, di) * comp(wy, dj) * comp(wz, dk);
					float u  = ld(vf, i + n.x * (j + n.y * k)) * wt;
					va       = va + u;
					c        = c + (float3(float(base.x + di), float(base.y + dj), float(base.z + dk)) - x) * (u * h);
				}
			}
		}
		c = c * (4.0 / (h * h));
		if (a == 0) {
			v.x    = va;
			aff.r0 = c;
		}
		if (a == 1) {
			v.y    = va;
			aff.r1 = c;
		}
		if (a == 2) {
			v.z    = va;
			aff.r2 = c;
		}
	}
	store3(S_PVEL, p, v);
	store_mat(S_PAFF, p, aff);

	// F = (I + dt C) F
	int material = info & 255;
	if (material != 0) {
		rows3 f  = load_mat(S_PDEF, p);
		rows3 cf = rows_mul(aff, f);
		float dt = sub_dt();
		f.r0     = f.r0 + cf.r0 * dt;
		f.r1     = f.r1 + cf.r1 * dt;
		f.r2     = f.r2 + cf.r2 * dt;
		solid_stress(p, f, material, (info >> 8) & 255);
	}
}

// Distance to the colliders and the way out, from the cells around
float4 collider_at(float3 pos) {
	float3 g = (pos - constant_buffer.grid.xyz) / cell_size();
	int3   n = int3(nx(), ny(), nz());
	float3 o = float3(0.5, 0.5, 0.5);
	uint   s = slot(S_SDF);
	int    c = cells();
	return float4(trilinear(s, n, o, g), trilinear(s + uint(c), n, o, g), trilinear(s + uint(c * 2), n, o, g), trilinear(s + uint(c * 3), n, o, g));
}

int nearest_cell(float3 pos) {
	float3 g = (pos - constant_buffer.grid.xyz) / cell_size();
	int    i = clamp(int(floor(g.x)), 0, nx() - 1);
	int    j = clamp(int(floor(g.y)), 0, ny() - 1);
	int    k = clamp(int(floor(g.z)), 0, nz() - 1);
	return cell_index(i, j, k);
}

float3 collider_velocity(int c) {
	uint s = slot(S_SVEL);
	int  n = cells();
	return float3(ld(s, c), ld(s + uint(n), c), ld(s + uint(n * 2), c));
}

// Pushes the point out to the radius from the colliders, the push
float3 push_out(float3 pos, float radius) {
	float4 col = collider_at(pos);
	float  nl  = length(col.yzw);
	if (col.x >= radius || nl < 0.000001) {
		return float3(0.0, 0.0, 0.0);
	}
	return col.yzw / nl * (radius - col.x);
}

float3 body_center(int body) {
	return load3(S_BPOS, body * 3);
}

// Of the body as it is now, the pushes of the substeps before slowed it
float3 body_velocity(int body, float3 at) {
	return load3(S_BPOS, body * 3 + 1) + cross(load3(S_BPOS, body * 3 + 2), at - body_center(body));
}

void add_body(int body, int part, float3 j, float3 at) {
	float3 r  = at - body_center(body);
	float3 tq = cross(r, j);
	int    b  = body * 12 + part * 6;
	add_fixed(S_BACC, b, j.x, BODY_FIXED);
	add_fixed(S_BACC, b + 1, j.y, BODY_FIXED);
	add_fixed(S_BACC, b + 2, j.z, BODY_FIXED);
	add_fixed(S_BACC, b + 3, tq.x, BODY_FIXED);
	add_fixed(S_BACC, b + 4, tq.y, BODY_FIXED);
	add_fixed(S_BACC, b + 5, tq.z, BODY_FIXED);
}

void move_particle(int p) {
	int info = particle_info(p);
	if ((info & 65536) != 0) {
		return;
	}
	int    material = info & 255;
	float  dt       = sub_dt();
	float  h        = cell_size();
	float3 x        = load3(S_PPOS, p);
	float3 v        = load3(S_PVEL, p);
	float3 pos      = x + v * dt;
	if (material == 2) {
		// Sand sticks to colliders
		float3 n = push_out(pos, h * 2.0);
		int    c = nearest_cell(pos);
		float  l = dot(n, n);
		if (l > 0.000000000000000001 && data[slot(S_BODY) + uint(c)] == uint(0)) {
			float3 vc  = collider_velocity(c);
			float  dn  = dot(pos - x, n) / l;
			float  vn  = dot(v, n) / l;
			float  vcn = dot(vc, n) / l;
			v          = n * vn + vc - n * vcn;
			pos        = x + n * dn + (vc - n * vcn) * dt;
		}
	}
	float3 push = push_out(pos, h * 0.25);
	if (dot(push, push) > 0.0) {
		pos      = pos + push;
		int c    = nearest_cell(pos);
		int body = int(data[slot(S_BODY) + uint(c)]) - 1;
		if (body >= 0 && material != 0) {
			// Grains push bodies by hitting them
			float  len = length(push);
			float  vn  = dot(v - body_velocity(body, pos), push) / len;
			if (vn < 0.0) {
				add_body(body, 1, push * (vn / len), pos);
			}
		}
	}
	float3 lo = constant_buffer.grid.xyz;
	float3 hi = lo + float3(float(nx()), float(ny()), float(nz())) * h;
	if (pos.x < lo.x || pos.y < lo.y || pos.z < lo.z || pos.x >= hi.x || pos.y >= hi.y || pos.z >= hi.z) {
		data[slot(S_PINFO) + uint(p)] = uint(info | 65536);
		store3(S_PVEL, p, float3(0.0, 0.0, 0.0));
		return;
	}
	store3(S_PPOS, p, pos);
	store3(S_PVEL, p, v);
}

// Pressure

void mark_fixed(int t) {
	uint v = uint(1);
	if (constant_buffer.u0.x == uint(0)) {
		v = data[slot(S_SOLID) + uint(t)];
	}
	data[slot(S_FIXED) + uint(t)] = v;
}

void mark_liquid(int p) {
	int info = particle_info(p);
	if ((info & 65536) != 0 || (info & 255) != 0) {
		return;
	}
	int c = nearest_cell(load3(S_PPOS, p));
	data[slot(S_FIXED) + uint(c)] = data[slot(S_SOLID) + uint(c)];
}

void coefficients(int t, int a) {
	float c = 0.0;
	if (data[slot(S_CLOSED + a) + uint(t)] == uint(0)) {
		c = 1.0;
		if (constant_buffer.u0.x == uint(0)) {
			c = 1.0 / (ld(slot(S_MASS + a), t) + AIR);
		}
	}
	st(slot(S_COEF + a), t, c);
}

// Sum over the faces of coef * (q_c - q_neighbor)
float apply(uint q, int c, int3 p) {
	uint  cu  = slot(S_COEF);
	uint  cv  = slot(S_COEF + 1);
	uint  cw  = slot(S_COEF + 2);
	int   u   = p.x + (nx() + 1) * (p.y + ny() * p.z);
	int   v   = p.x + nx() * (p.y + (ny() + 1) * p.z);
	int   nxy = nx() * ny();
	float qc  = ld(q, c);
	float qn  = 0.0;
	float sum = 0.0;
	if (p.x > 0) {
		qn = ld(q, c - 1);
	}
	sum = sum + ld(cu, u) * (qc - qn);
	qn  = 0.0;
	if (p.x < nx() - 1) {
		qn = ld(q, c + 1);
	}
	sum = sum + ld(cu, u + 1) * (qc - qn);
	qn  = 0.0;
	if (p.y > 0) {
		qn = ld(q, c - nx());
	}
	sum = sum + ld(cv, v) * (qc - qn);
	qn  = 0.0;
	if (p.y < ny() - 1) {
		qn = ld(q, c + nx());
	}
	sum = sum + ld(cv, v + nx()) * (qc - qn);
	qn  = 0.0;
	if (p.z > 0) {
		qn = ld(q, c - nxy);
	}
	sum = sum + ld(cw, c) * (qc - qn);
	qn  = 0.0;
	if (p.z < nz() - 1) {
		qn = ld(q, c + nxy);
	}
	sum = sum + ld(cw, c + nxy) * (qc - qn);
	return sum + PRESSURE_EPS * qc;
}

bool fixed_cell(int c) {
	return data[slot(S_FIXED) + uint(c)] != uint(0);
}

// Threads stride over the cells, each keeps a partial sum for the reduction
void pcg_init(int t) {
	uint  q    = slot(S_PRESS);
	uint  r    = slot(S_R);
	uint  d    = slot(S_D);
	uint  ad   = slot(S_AD);
	uint  di   = slot(S_IDIAG);
	uint  u    = slot(S_VEL);
	uint  v    = slot(S_VEL + 1);
	uint  w    = slot(S_VEL + 2);
	uint  cu   = slot(S_COEF);
	uint  cv   = slot(S_COEF + 1);
	uint  cw   = slot(S_COEF + 2);
	int   nxy  = nx() * ny();
	int3  n    = int3(nx(), ny(), nz());
	float bmax = 0.0;
	float err  = 0.0;
	for (int c = t; c < cells(); c += int(constant_buffer.op.y)) {
		if (fixed_cell(c)) {
			st(r, c, 0.0);
			st(d, c, 0.0);
			st(ad, c, 0.0);
			st(di, c, 0.0);
		}
		else {
			int3  p   = unflatten(c, n);
			int   fu  = p.x + (nx() + 1) * (p.y + ny() * p.z);
			int   fv  = p.x + nx() * (p.y + (ny() + 1) * p.z);
			float div = ld(u, fu + 1) - ld(u, fu) + ld(v, fv + nx()) - ld(v, fv) + ld(w, c + nxy) - ld(w, c);
			float rc  = 0.0 - div - apply(q, c, p);
			float dg  = ld(cu, fu) + ld(cu, fu + 1) + ld(cv, fv) + ld(cv, fv + nx()) + ld(cw, c) + ld(cw, c + nxy) + PRESSURE_EPS;
			st(r, c, rc);
			st(d, c, 0.0);
			st(di, c, 1.0 / dg);
			bmax = max(bmax, abs(div));
			err  = max(err, abs(rc));
		}
	}
	max_bits(C_BMAX, bmax);
	max_bits(C_ERR, err);
}

void pcg_ad(int t) {
	if (solved()) {
		return;
	}
	uint  d   = slot(S_D);
	uint  ad  = slot(S_AD);
	int3  n   = int3(nx(), ny(), nz());
	float sum = 0.0;
	for (int c = t; c < cells(); c += int(constant_buffer.op.y)) {
		if (!fixed_cell(c)) {
			float a = apply(d, c, unflatten(c, n));
			st(ad, c, a);
			sum = sum + ld(d, c) * a;
		}
	}
	st(slot(S_RED), t, sum);
}

void pcg_update(int t) {
	if (solved()) {
		return;
	}
	uint  q     = slot(S_PRESS);
	uint  r     = slot(S_R);
	uint  d     = slot(S_D);
	uint  ad    = slot(S_AD);
	float alpha = scalar(C_ALPHA);
	float err   = 0.0;
	for (int c = t; c < cells(); c += int(constant_buffer.op.y)) {
		if (!fixed_cell(c)) {
			st(q, c, ld(q, c) + alpha * ld(d, c));
			float rc = ld(r, c) - alpha * ld(ad, c);
			st(r, c, rc);
			err = max(err, abs(rc));
		}
	}
	max_bits(C_ERR, err);
}

// Partial sums of r . z
void pcg_dot(int t) {
	if (solved()) {
		return;
	}
	uint  r   = slot(S_R);
	uint  z   = slot(S_Z);
	float sum = 0.0;
	for (int c = t; c < cells(); c += int(constant_buffer.op.y)) {
		sum = sum + ld(r, c) * ld(z, c);
	}
	st(slot(S_RED), t, sum);
}

void pcg_dir(int t) {
	if (solved() || fixed_cell(t)) {
		return;
	}
	uint d = slot(S_D);
	st(d, t, ld(slot(S_Z), t) + scalar(C_BETA) * ld(d, t));
}

// Each thread sums its share of the partial sums at u0.x, u0.z of them, into u0.y
void reduce(int t) {
	uint  src     = constant_buffer.u0.x;
	int   count   = int(constant_buffer.u0.z);
	int   threads = int(constant_buffer.op.y);
	float sum     = 0.0;
	for (int i = t; i < count; i += threads) {
		sum = sum + ld(src, i);
	}
	st(constant_buffer.u0.y, t, sum);
}

float reduced() {
	float sum = 0.0;
	for (int i = 0; i < 64; i += 1) {
		sum = sum + scalar(C_RED2 + i);
	}
	return sum;
}


// Multigrid preconditioner: a V-cycle of red black Gauss-Seidel, symmetric for the conjugate gradients. Trilinear prolongation,
// its transpose restricts, coarse faces sum the fine ones. Level kernels take their dims in u0, coarse dims in u1

struct mg_level {
	int3 n;
	uint cu;
	uint cv;
	uint cw;
	uint idiag;
	uint fixed;
	uint x;
	uint b;
	uint res;
};

mg_level level_at() {
	mg_level l;
	l.n     = int3(int(constant_buffer.u0.x), int(constant_buffer.u0.y), int(constant_buffer.u0.z));
	l.cu    = constant_buffer.u1.x;
	l.cv    = constant_buffer.u1.y;
	l.cw    = constant_buffer.u1.z;
	l.idiag = constant_buffer.u1.w;
	l.fixed = constant_buffer.u2.x;
	l.x     = constant_buffer.u2.y;
	l.b     = constant_buffer.u2.z;
	l.res   = constant_buffer.u2.w;
	return l;
}

// Sum over the faces of coef * x_neighbor
float neighbors(mg_level l, int c, int3 p) {
	int   u   = p.x + (l.n.x + 1) * (p.y + l.n.y * p.z);
	int   v   = p.x + l.n.x * (p.y + (l.n.y + 1) * p.z);
	int   nxy = l.n.x * l.n.y;
	float sum = 0.0;
	if (p.x > 0) {
		sum = sum + ld(l.cu, u) * ld(l.x, c - 1);
	}
	if (p.x < l.n.x - 1) {
		sum = sum + ld(l.cu, u + 1) * ld(l.x, c + 1);
	}
	if (p.y > 0) {
		sum = sum + ld(l.cv, v) * ld(l.x, c - l.n.x);
	}
	if (p.y < l.n.y - 1) {
		sum = sum + ld(l.cv, v + l.n.x) * ld(l.x, c + l.n.x);
	}
	if (p.z > 0) {
		sum = sum + ld(l.cw, c) * ld(l.x, c - nxy);
	}
	if (p.z < l.n.z - 1) {
		sum = sum + ld(l.cw, c + nxy) * ld(l.x, c + nxy);
	}
	return sum;
}

void mg_smooth(int t, int color) {
	mg_level l  = level_at();
	int3     p  = unflatten(t, l.n);
	float    di = ld(l.idiag, t);
	if (((p.x + p.y + p.z) & 1) != color || di == 0.0) {
		return;
	}
	st(l.x, t, (ld(l.b, t) + neighbors(l, t, p)) * di);
}

void mg_residual(int t) {
	mg_level l  = level_at();
	float    di = ld(l.idiag, t);
	float    r  = 0.0;
	if (di != 0.0) {
		r = ld(l.b, t) - (ld(l.x, t) / di - neighbors(l, t, unflatten(t, l.n)));
	}
	st(l.res, t, r);
}

float prolong_weight(int i, int c) {
	// Fine i lies between coarse (i + 1) / 2 - 1 and the one after, nearer to the second when even
	if (c == (i + 1) / 2 - 1) {
		if ((i & 1) == 0) {
			return 0.25;
		}
		return 0.75;
	}
	if ((i & 1) == 0) {
		return 0.75;
	}
	return 0.25;
}

void mg_restrict(int t) {
	int3  nf  = int3(int(constant_buffer.u0.x), int(constant_buffer.u0.y), int(constant_buffer.u0.z));
	int3  nc  = int3(int(constant_buffer.u1.x), int(constant_buffer.u1.y), int(constant_buffer.u1.z));
	int3  c   = unflatten(t, nc);
	uint  res = constant_buffer.u2.x;
	float sum = 0.0;
	for (int dk = 0 - 1; dk <= 2; dk += 1) {
		int k = c.z * 2 + dk;
		if (k >= 0 && k < nf.z) {
			float wk = prolong_weight(k, c.z);
			for (int dj = 0 - 1; dj <= 2; dj += 1) {
				int j = c.y * 2 + dj;
				if (j >= 0 && j < nf.y) {
					float wj = prolong_weight(j, c.y) * wk;
					for (int di = 0 - 1; di <= 2; di += 1) {
						int i = c.x * 2 + di;
						if (i >= 0 && i < nf.x) {
							sum = sum + ld(res, i + nf.x * (j + nf.y * k)) * prolong_weight(i, c.x) * wj;
						}
					}
				}
			}
		}
	}
	st(constant_buffer.u2.y, t, sum);
}

void mg_prolong(int t) {
	int3 nf = int3(int(constant_buffer.u0.x), int(constant_buffer.u0.y), int(constant_buffer.u0.z));
	int3 nc = int3(int(constant_buffer.u1.x), int(constant_buffer.u1.y), int(constant_buffer.u1.z));
	if (ld(constant_buffer.u2.z, t) == 0.0) {
		return; // Fixed, no inverse diagonal
	}
	int3  f   = unflatten(t, nf);
	uint  xc  = constant_buffer.u2.y;
	int3  c0  = int3((f.x + 1) / 2 - 1, (f.y + 1) / 2 - 1, (f.z + 1) / 2 - 1);
	float sum = 0.0;
	for (int dk = 0; dk < 2; dk += 1) {
		int k = c0.z + dk;
		if (k >= 0 && k < nc.z) {
			float wk = prolong_weight(f.z, k);
			for (int dj = 0; dj < 2; dj += 1) {
				int j = c0.y + dj;
				if (j >= 0 && j < nc.y) {
					float wj = prolong_weight(f.y, j) * wk;
					for (int di = 0; di < 2; di += 1) {
						int i = c0.x + di;
						if (i >= 0 && i < nc.x) {
							sum = sum + ld(xc, i + nc.x * (j + nc.y * k)) * prolong_weight(f.x, i) * wj;
						}
					}
				}
			}
		}
	}
	uint xf = constant_buffer.u2.x;
	st(xf, t, ld(xf, t) + sum);
}

// A coarse cell is fixed when all its fine cells are
void mg_fixed(int t) {
	int3 nf    = int3(int(constant_buffer.u0.x), int(constant_buffer.u0.y), int(constant_buffer.u0.z));
	int3 nc    = int3(int(constant_buffer.u1.x), int(constant_buffer.u1.y), int(constant_buffer.u1.z));
	int3 c     = unflatten(t, nc);
	uint fixed = uint(1);
	for (int d = 0; d < 8; d += 1) {
		int i = c.x * 2 + (d & 1);
		int j = c.y * 2 + ((d >> 1) & 1);
		int k = c.z * 2 + (d >> 2);
		if (i < nf.x && j < nf.y && k < nf.z) {
			if (data[constant_buffer.u2.x + uint(i + nf.x * (j + nf.y * k))] == uint(0)) {
				fixed = uint(0); // Read inside of the field only, the && evaluates both sides
			}
		}
	}
	data[constant_buffer.u2.y + uint(t)] = fixed;
}

// Half the sum of the fine faces a coarse face covers
void mg_faces(int t, int a) {
	int3 nf  = int3(int(constant_buffer.u0.x), int(constant_buffer.u0.y), int(constant_buffer.u0.z));
	int3 nc  = int3(int(constant_buffer.u1.x), int(constant_buffer.u1.y), int(constant_buffer.u1.z));
	int3 fnf = nf;
	int3 fnc = nc;
	if (a == 0) {
		fnf.x = fnf.x + 1;
		fnc.x = fnc.x + 1;
	}
	if (a == 1) {
		fnf.y = fnf.y + 1;
		fnc.y = fnc.y + 1;
	}
	if (a == 2) {
		fnf.z = fnf.z + 1;
		fnc.z = fnc.z + 1;
	}
	int3  c   = unflatten(t, fnc);
	float sum = 0.0;
	for (int d = 0; d < 4; d += 1) {
		int3 f = int3(c.x * 2, c.y * 2, c.z * 2);
		int  e0 = d & 1;
		int  e1 = d >> 1;
		if (a == 0) {
			f.x = min(f.x, nf.x);
			f.y = f.y + e0;
			f.z = f.z + e1;
		}
		if (a == 1) {
			f.y = min(f.y, nf.y);
			f.x = f.x + e0;
			f.z = f.z + e1;
		}
		if (a == 2) {
			f.z = min(f.z, nf.z);
			f.x = f.x + e0;
			f.y = f.y + e1;
		}
		if (f.x < fnf.x && f.y < fnf.y && f.z < fnf.z) {
			sum = sum + ld(constant_buffer.u2.x, f.x + fnf.x * (f.y + fnf.y * f.z));
		}
	}
	st(constant_buffer.u2.y, t, sum * 0.5);
}

void mg_diag(int t) {
	mg_level l = level_at();
	float d = 0.0;
	if (data[l.fixed + uint(t)] == uint(0)) {
		int3 p   = unflatten(t, l.n);
		int  u   = p.x + (l.n.x + 1) * (p.y + l.n.y * p.z);
		int  v   = p.x + l.n.x * (p.y + (l.n.y + 1) * p.z);
		int  nxy = l.n.x * l.n.y;
		d        = 1.0 / (ld(l.cu, u) + ld(l.cu, u + 1) + ld(l.cv, v) + ld(l.cv, v + l.n.x) + ld(l.cw, t) + ld(l.cw, t + nxy) + constant_buffer.f0.x);
	}
	st(l.idiag, t, d);
}

// One thread, the scalars of the iteration
void scalars() {
	uint s    = slot(S_SCAL);
	int  mode = int(constant_buffer.u0.x);
	if (mode == 0) {
		float bmax = scalar(C_BMAX);
		st(s, C_RZ, reduced());
		st(s, C_BETA, 0.0);
		st(s, C_TOL, constant_buffer.f0.x * bmax);
		st(s, C_ITERS, 0.0);
		data[s + uint(C_DONE)] = uint(0);
		if (bmax < 0.000000001) {
			data[s + uint(C_DONE)] = uint(2);
		}
		else if (scalar(C_ERR) < scalar(C_TOL)) {
			data[s + uint(C_DONE)] = uint(1);
		}
		data[s + uint(C_ERR)] = uint(0);
		return;
	}
	if (solved()) {
		return;
	}
	if (mode == 1) {
		float dad = reduced();
		if (dad <= 0.0) {
			data[s + uint(C_DONE)] = uint(1);
			return;
		}
		st(s, C_ALPHA, scalar(C_RZ) / dad);
		return;
	}
	float rz = reduced();
	st(s, C_BETA, rz / scalar(C_RZ));
	st(s, C_RZ, rz);
	st(s, C_ITERS, scalar(C_ITERS) + 1.0);
#ifdef _QUEUE_AHEAD
	st(s, C_ITMAX, max(scalar(C_ITMAX), scalar(C_ITERS)));
#endif
	if (scalar(C_ERR) < scalar(C_TOL)) {
		data[s + uint(C_DONE)] = uint(1);
	}
	data[s + uint(C_ERR)] = uint(0);
}

// Before the solve, fixed cells hold no pressure
void pcg_zero(int t) {
	if (fixed_cell(t)) {
		st(slot(S_PRESS), t, 0.0);
	}
}

// Nothing to solve: the pressure starts over
void pcg_reset(int t) {
	if (data[slot(S_SCAL) + uint(C_DONE)] == uint(2)) {
		st(slot(S_PRESS), t, 0.0);
	}
}

void gradient(int t, int a) {
	if (data[slot(S_SCAL) + uint(C_DONE)] == uint(2)) {
		return;
	}
	int3  c   = unflatten(t, face_dims(a));
	int   ca  = icomp(c, a);
	int   na  = icomp(int3(nx(), ny(), nz()), a);
	uint  q   = slot(S_PRESS);
	float qhi = 0.0;
	float qlo = 0.0;
	if (ca < na) {
		qhi = ld(q, cell_index(c.x, c.y, c.z));
	}
	if (ca > 0) {
		int3 b = c;
		if (a == 0) {
			b.x = b.x - 1;
		}
		if (a == 1) {
			b.y = b.y - 1;
		}
		if (a == 2) {
			b.z = b.z - 1;
		}
		qlo = ld(q, cell_index(b.x, b.y, b.z));
	}
	uint vf = slot(S_VEL + a);
	st(vf, t, ld(vf, t) - ld(slot(S_COEF + a), t) * (qhi - qlo));
}

// Fastest face or particle, and whether there is gas
void stats(int t) {
	float speed = 0.0;
	float gas   = 0.0;
	uint  vel   = slot(S_VEL);
	int   faces = int(constant_buffer.u0.x);
	for (int i = t; i < faces; i += int(constant_buffer.op.y)) {
		speed = max(speed, abs(ld(vel, i)));
	}
	uint pv    = slot(S_PVEL);
	int  count = int(constant_buffer.u0.y) * 3;
	for (int i = t; i < count; i += int(constant_buffer.op.y)) {
		speed = max(speed, abs(ld(pv, i)));
	}
	uint gf     = slot(S_TEMP);
	int  amount = int(constant_buffer.u0.z);
	for (int i = t; i < amount; i += int(constant_buffer.op.y)) {
		if (ld(gf, i) > 0.001) {
			gas = 1.0;
		}
	}
	max_bits(C_SPEED, speed);
	max_bits(C_GAS, gas);
}

#ifdef _QUEUE_AHEAD
// One thread: the size of the substep from the stats, CFL limited. u0 step and most steps, f0 frame time, CFL, elastic CFL, wave speed
void substep() {
	uint  s  = slot(S_SCAL);
	float dt = constant_buffer.f0.x;
	float t  = scalar(C_T);
	if (t >= dt - 0.000001) {
		data[s + uint(C_IDLE)] = uint(1);
		return;
	}
	float h     = cell_size();
	float speed = scalar(C_SPEED) * sqrt(3.0) + sqrt(h * 9.81);
	float sub   = min(dt - t, constant_buffer.f0.y * h / speed);
	if (constant_buffer.f0.w > 0.0) {
		sub = min(sub, constant_buffer.f0.z * h / (constant_buffer.f0.w + speed));
	}
	if (constant_buffer.u0.x + uint(1) >= constant_buffer.u0.y) {
		sub = dt - t; // The last one allowed
	}
	st(s, C_SUB, sub);
	st(s, C_T, t + sub);
	st(s, C_STEPS, scalar(C_STEPS) + 1.0);
}
#endif

float liquid_in(int i, int j, int k) {
	int   c = cell_index(i, j, k);
	float m = ld(slot(S_MASS), i + (nx() + 1) * (j + ny() * k)) + ld(slot(S_MASS), i + 1 + (nx() + 1) * (j + ny() * k));
	m       = m + ld(slot(S_MASS + 1), i + nx() * (j + (ny() + 1) * k)) + ld(slot(S_MASS + 1), i + nx() * (j + 1 + (ny() + 1) * k));
	m       = m + ld(slot(S_MASS + 2), c) + ld(slot(S_MASS + 2), c + nx() * ny());
	return min(m / 6.0, 1.0);
}

// Buoyancy and drag of the liquid around the cells of the bodies
void push_bodies(int t) {
	int  c    = int(data[slot(S_BCELL) + uint(t)]);
	uint body = data[slot(S_BODY) + uint(c)];
	int3 p    = unflatten(c, int3(nx(), ny(), nz()));
	uint sol  = slot(S_SOLID);
	float  wet = 0.0;
	float3 u   = float3(0.0, 0.0, 0.0);
	for (int dir = 0; dir < 6; dir += 1) {
		if (dir != 4) {
			int a    = dir / 2;
			int step = 1;
			if (dir % 2 == 0) {
				step = 0 - 1;
			}
			int3 q    = p;
			bool done = false;
			for (int m = 0; m < 16 && !done; m += 1) {
				if (a == 0) {
					q.x = q.x + step;
				}
				if (a == 1) {
					q.y = q.y + step;
				}
				if (a == 2) {
					q.z = q.z + step;
				}
				if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= nx() || q.y >= ny() || q.z >= nz()) {
					done = true;
				}
				else {
					int  nb    = cell_index(q.x, q.y, q.z);
					bool solid = data[sol + uint(nb)] != uint(0);
					if (data[sol + uint(nb)] != uint(1) || data[slot(S_BODY) + uint(nb)] != body) {
						float l = 0.0;
						if (!solid) {
							l = liquid_in(q.x, q.y, q.z);
						}
						if (l > wet) {
							wet = l;
							u   = center_velocity(q.x, q.y, q.z);
						}
						done = true;
					}
				}
			}
		}
	}
	if (wet <= 0.0) {
		return;
	}
	float3 g = constant_buffer.f1.xyz;
	float3 j = ((u - collider_velocity(c)) * BODY_DRAG - g) * (wet * sub_dt());
	add_body(int(body) - 1, 0, j, cell_center(c));
}

#[compute, threads(64, 1, 1)]
void sim() {
	uint3 id = dispatch_thread_id();
	int   t  = int(id.y * constant_buffer.op.z + id.x);
	if (t >= int(constant_buffer.op.y)) {
		return;
	}
	int k = int(constant_buffer.op.x);
	int a = int(constant_buffer.op.w);
	if (constant_buffer.u3.w != uint(0)) {
		if (solved()) {
			return; // Iterations queued past convergence
		}
	}
#ifdef _QUEUE_AHEAD
	if (constant_buffer.u3.z != uint(0)) {
		if (data[slot(S_SCAL) + uint(C_IDLE)] != uint(0)) {
			return; // Substeps queued past the end of the frame
		}
	}
#endif
	if (k == K_CLEAR) {
		data[constant_buffer.u0.x + uint(t)] = constant_buffer.u0.y;
	}
	else if (k == K_COPY) {
		data[constant_buffer.u0.y + uint(t)] = data[constant_buffer.u0.x + uint(t)];
	}
	else if (k == K_CLOSED) {
		closed_faces(t, a);
	}
	else if (k == K_CLOSE_FACES) {
		close_faces(t, a);
	}
	else if (k == K_ADVECT_AIR) {
		advect_air(t, a);
	}
	else if (k == K_ADVECT) {
		advect_cells(t);
	}
	else if (k == K_BURN) {
		burn(t);
	}
	else if (k == K_EMIT) {
		emit(t);
	}
	else if (k == K_BUOYANCY) {
		buoyancy(t);
	}
	else if (k == K_CURL) {
		curl(t);
	}
	else if (k == K_VORT_FORCE) {
		vorticity_force(t);
	}
	else if (k == K_VORT_APPLY) {
		vorticity_apply(t, a);
	}
	else if (k == K_FIELDS) {
		fields(t, a);
	}
	else if (k == K_P2G) {
		p2g(t);
	}
	else if (k == K_BLEND) {
		blend(t, a);
	}
	else if (k == K_VISCOSITY) {
		viscosity(t, a);
	}
	else if (k == K_G2P) {
		g2p(t);
	}
	else if (k == K_MOVE) {
		move_particle(t);
	}
	else if (k == K_FIXED) {
		mark_fixed(t);
	}
	else if (k == K_FIXED_LIQ) {
		mark_liquid(t);
	}
	else if (k == K_COEF) {
		coefficients(t, a);
	}
	else if (k == K_PCG_INIT) {
		pcg_init(t);
	}
	else if (k == K_REDUCE) {
		reduce(t);
	}
	else if (k == K_SCALARS) {
		scalars();
	}
	else if (k == K_PCG_AD) {
		pcg_ad(t);
	}
	else if (k == K_PCG_UPDATE) {
		pcg_update(t);
	}
	else if (k == K_PCG_DIR) {
		pcg_dir(t);
	}
	else if (k == K_GRADIENT) {
		gradient(t, a);
	}
	else if (k == K_STATS) {
		stats(t);
	}
	else if (k == K_PUSH_BODIES) {
		push_bodies(t);
	}
	else if (k == K_PCG_RESET) {
		pcg_reset(t);
	}
	else if (k == K_PCG_ZERO) {
		pcg_zero(t);
	}
	else if (k == K_MG_FIXED) {
		mg_fixed(t);
	}
	else if (k == K_MG_FACES) {
		mg_faces(t, a);
	}
	else if (k == K_MG_DIAG) {
		mg_diag(t);
	}
	else if (k == K_MG_SMOOTH) {
		mg_smooth(t, a);
	}
	else if (k == K_MG_RESIDUAL) {
		mg_residual(t);
	}
	else if (k == K_MG_RESTRICT) {
		mg_restrict(t);
	}
	else if (k == K_MG_PROLONG) {
		mg_prolong(t);
	}
	else if (k == K_PCG_DOT) {
		pcg_dot(t);
	}
#ifdef _QUEUE_AHEAD
	else if (k == K_SUBSTEP) {
		substep();
	}
#endif
}
