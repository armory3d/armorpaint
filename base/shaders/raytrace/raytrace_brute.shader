
#ifdef _FULL
#define _EMISSION
#define _SUBSURFACE
#define _TRANSLUCENCY
#define _ROULETTE
#define _GRAINS
#define _VOLUMES
#define _FOG
#endif
#define _ENV_SAMPLING
// #define _FAST
#ifndef _FULL
#define _INDIRECT_SKIP_NORMAL
#endif

cbuffer constant_buffer {
	float4   eye; // xyz, frame
	float4x4 inv_vp;
	float4   params; // envstr, envangle, uvscale, env sampling enabled in bit 0 | grain geometries as a bit per geometry above
	float4   params2; // volume geometries as a bit per geometry, volume count, their geometry indices 5 bits each
	float4   fog; // linear rgb, density (0 = disabled)
};

bvh     scene;
tex2d   render_target;
tex2d   mytexture0;
tex2d   mytexture1;
tex2d   mytexture2;
tex2d   mytexture_env;
tex2d   mytexture_sobol;
tex2d   mytexture_scramble;
tex2d   mytexture_rank;
tex2d   mytexture_env_cdf;
sampler sampler_linear;

#ifdef _FULL
#ifdef _LOW_SAMPLES
const int SAMPLES = 8;
#else
const int SAMPLES = 64;
#endif
#else
#ifdef _LOW_SAMPLES
const int SAMPLES = 4;
#else
#ifdef _FAST
const int SAMPLES = 16;
#else
const int SAMPLES = 32;
#endif
#endif
#endif

#ifdef _SUBSURFACE
const int DEPTH = 32;
#else
#ifdef _TRANSLUCENCY
const int DEPTH = 16;
#else
#ifdef _FAST
const int DEPTH = 2; // Opaque hits
#else
const int DEPTH = 3; // Opaque hits
#endif
#endif
#endif

// Sample dimensions of a bounce: [roulette], select, [select2], dir.xy, [medium.xyz], light.xy
const int DIM_BOUNCE = 2;
#ifdef _FULL
const int DIM_RR         = 0;
const int DIM_SELECT     = 1;
const int DIM_SELECT2    = 2;
const int DIM_DIR        = 3;
const int DIM_MEDIUM     = 5; // Free flight, scatter.xy
const int DIM_LIGHT      = 6; // Shares scatter.xy, a medium scatter and a surface light sample never happen in the same bounce
const int DIM_PER_BOUNCE = 8;
#else
const int DIM_SELECT     = 0;
const int DIM_DIR        = 1;
const int DIM_LIGHT      = 3;
const int DIM_PER_BOUNCE = 5;
#endif

#ifdef _ROULETTE
const int   rr_start = 2;
const float rr_max   = 0.5;
const float rr_min   = 0.05;
#endif

const float PI  = 3.1415926535;
const float PI2 = 6.283185307;
#ifdef _ENV_SAMPLING
const float PI_SQ2    = 19.739208802;
const int   ENV_CDF_W = 256;
const int   ENV_CDF_H = 128;
const int   ENV_CDF_N = 32768;
#endif

const int RANK_CACHE_DIMS = 16;

struct tangent_basis {
	float3 tangent;
	float3 binormal;
};

struct sampler_cache {
	uint2 scramble;
	uint4 rank;
};

float2 s16_to_f32(uint val) {
	int a = int(val << 16) >> 16;
	int b = int(val) >> 16;
	return float2(float(a), float(b)) / 32767.0;
}

float3 hit_attribute(float3 a0, float3 a1, float3 a2, float2 barycentrics) {
	return a0 + barycentrics.x * (a1 - a0) + barycentrics.y * (a2 - a0);
}

float2 hit_attribute2d(float2 a0, float2 a1, float2 a2, float2 barycentrics) {
	return a0 + barycentrics.x * (a1 - a0) + barycentrics.y * (a2 - a0);
}

uint2 table_texel(int i) {
	int t = (i & 131071) >> 2;
	return uint2(uint(t & 127), uint(t >> 7));
}

uint table_byte(float4 c, int i) {
	int   ch = i & 3;
	float v  = c.a;
	if (ch == 0) {
		v = c.r;
	}
	else if (ch == 1) {
		v = c.g;
	}
	else if (ch == 2) {
		v = c.b;
	}
	return uint(v * 255.0);
}

uint table_word(float4 c) {
	return uint(c.r * 255.0) | (uint(c.g * 255.0) << 8) | (uint(c.b * 255.0) << 16) | (uint(c.a * 255.0) << 24);
}

uint rank_value_at(int pixel_i, int pixel_j, int sample_dimension, int frame) {
	int    i = (sample_dimension & 255) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	float4 c = mytexture_rank[table_texel(i)];
	return table_byte(c, i);
}

float rand_indexed(int sample_index, int sample_dimension, uint rank_value, uint scramble_value) {
	sample_index            = sample_index & 255;
	sample_dimension        = sample_dimension & 255;
	int    ranked_sample_index = sample_index ^ int(rank_value);
	float4 sobol           = mytexture_sobol[uint2(uint(ranked_sample_index), uint(sample_dimension))];
	int    value           = int(sobol.r * 255.0);
	value                  = value ^ int(scramble_value);
	return (0.5 + float(value)) / 256.0;
}

float2 equirect(float3 normal, float angle) {
	float phi   = acos(clamp(normal.z, -1.0, 1.0));
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

tangent_basis create_basis(float3 normal) {
	float s = 1.0;
	if (normal.z < 0.0) {
		s = -1.0;
	}
	float a = -1.0 / (s + normal.z);
	float b = normal.x * normal.y * a;
	tangent_basis result;
	result.tangent  = float3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
	result.binormal = float3(b, s + normal.y * normal.y * a, -normal.y);
	return result;
}

float3 camera_ray_direction(float2 screen_pos, float3 eye) {
	float4 world = constant_buffer.inv_vp * float4(screen_pos.x, -screen_pos.y, 0.0, 1.0);
	float3 p     = world.xyz / world.w;
	return normalize(p - eye);
}

float3 surface_albedo(float3 base_color, float metalness) {
	return lerp(base_color, float3(0.0, 0.0, 0.0), metalness);
}

float3 surface_specular(float3 base_color, float metalness) {
	return lerp(float3(0.04, 0.04, 0.04), base_color, metalness);
}

float luma(float3 c) {
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 srgb_to_linear(float3 c) {
	return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

float3 f_schlick(float3 f0, float u) {
	float m  = saturate(1.0 - u);
	float m2 = m * m;
	return f0 + (1.0 - f0) * (m2 * m2 * m);
}

float3 spec_directional_albedo(float3 f0, float ndotv, float roughness) {
	float4 c0   = float4(-1.0, -0.0275, -0.572, 0.022);
	float4 c1   = float4(1.0, 0.0425, 1.04, -0.04);
	float4 r    = c0 * roughness + c1;
	float  a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
	float2 ab   = float2(-1.04, 1.04) * a004 + r.zw;
	return saturate(f0 * ab.x + ab.y);
}

float smith_lambda(float cos_theta, float alpha2) {
	float c2 = cos_theta * cos_theta;
	return 0.5 * (sqrt(1.0 + alpha2 * (1.0 - c2) / max(c2, 0.0000001)) - 1.0);
}

tangent_basis create_uv_basis(float3 p0, float3 p1, float3 p2, float2 uv0, float2 uv1, float2 uv2, float3 n) {
	float3 e1  = p1 - p0;
	float3 e2  = p2 - p0;
	float2 d1  = uv1 - uv0;
	float2 d2  = uv2 - uv0;
	float  det = d1.x * d2.y - d2.x * d1.y;
	if (abs(det) > 0.000000000001) {
		float  r  = 1.0 / det;
		float3 tu = (e1 * d2.y - e2 * d1.y) * r;
		float3 tv = (e2 * d1.x - e1 * d2.x) * r;
		float3 t  = tu - n * dot(n, tu);
		float  tl = dot(t, t);
		if (tl > 0.0000000000000001) {
			t        = t * rsqrt(tl);
			float3 b = tv - n * dot(n, tv);
			b        = b - t * dot(t, b);
			float bl = dot(b, b);
			if (bl > 0.0000000000000001) {
				tangent_basis result;
				result.tangent  = t;
				result.binormal = b * rsqrt(bl);
				return result;
			}
		}
	}
	return create_basis(n);
}

float3 cos_weighted_direction(float3 tangent, float3 binormal, float3 n, float u1, float u2) {
	float r   = sqrt(u1);
	float phi = PI2 * u2;
	return tangent * (r * cos(phi)) + binormal * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1));
}

float3 sample_ggx_vndf(float3 ve, float alpha, float u1, float u2) {
	float3 vh    = normalize(float3(alpha * ve.x, alpha * ve.y, ve.z));
	float  lensq = vh.x * vh.x + vh.y * vh.y;
	float3 t1    = float3(1.0, 0.0, 0.0);
	if (lensq > 0.0) {
		t1 = float3(-vh.y, vh.x, 0.0) * rsqrt(lensq);
	}
	float3 t2  = cross(vh, t1);
	float  r   = sqrt(u1);
	float  phi = PI2 * u2;
	float  p1  = r * cos(phi);
	float  p2  = r * sin(phi);
	float  s   = 0.5 * (1.0 + vh.z);
	p2         = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
	float3 nh  = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
	return normalize(float3(alpha * nh.x, alpha * nh.y, max(0.000001, nh.z)));
}

#ifdef _SUBSURFACE
float fresnel_dielectric(float cos_i, float eta) {
	float sin2_t = eta * eta * (1.0 - cos_i * cos_i);
	if (sin2_t >= 1.0) {
		return 1.0;
	}
	float cos_t = sqrt(1.0 - sin2_t);
	float rs    = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
	float rp    = (cos_i - eta * cos_t) / (cos_i + eta * cos_t);
	return 0.5 * (rs * rs + rp * rp);
}

float3 refract_dir(float3 i, float3 n, float eta) {
	float c = -dot(i, n);
	float k = 1.0 - eta * eta * (1.0 - c * c);
	if (k < 0.0) {
		return float3(0.0, 0.0, 0.0);
	}
	return i * eta + n * (eta * c - sqrt(k));
}

float3 uniform_sphere(float u1, float u2) {
	float z   = 1.0 - 2.0 * u1;
	float r   = sqrt(max(0.0, 1.0 - z * z));
	float phi = PI2 * u2;
	return float3(r * cos(phi), r * sin(phi), z);
}
#endif

float3 offset_ray(float3 p, float3 ng, float3 dir) {
	if (dot(dir, ng) < 0.0) {
		return p - ng * 0.0001;
	}
	return p + ng * 0.0001;
}

sampler_cache init_sampler(uint2 pixel_coord, int frame) {
	int pixel_i = (int(pixel_coord.x) + frame * 9) & 127;
	int pixel_j = (int(pixel_coord.y) + frame * 11) & 127;
	int base    = (pixel_i + pixel_j * 128) * 8;

	sampler_cache cache;
	cache.scramble = uint2(table_word(mytexture_scramble[table_texel(base)]), table_word(mytexture_scramble[table_texel(base + 4)]));
	cache.rank     = uint4(table_word(mytexture_rank[table_texel(base)]), table_word(mytexture_rank[table_texel(base + 4)]),
	                       table_word(mytexture_rank[table_texel(base + 8)]), table_word(mytexture_rank[table_texel(base + 12)]));
	return cache;
}

float rnd(uint2 pixel_coord, int sample_index, int dim, sampler_cache cache, int frame) {
	int  k             = dim & 7;
	uint scramble_word = cache.scramble.y;
	if (k < 4) {
		scramble_word = cache.scramble.x;
	}
	uint scramble_value = (scramble_word >> ((k & 3) * 8)) & 255;

	uint rank_value;
	if (dim < RANK_CACHE_DIMS) {
		int  slot   = dim >> 2;
		uint packed = cache.rank.w;
		if (slot == 0) {
			packed = cache.rank.x;
		}
		else if (slot == 1) {
			packed = cache.rank.y;
		}
		else if (slot == 2) {
			packed = cache.rank.z;
		}
		rank_value = (packed >> ((dim & 3) * 8)) & 255;
	}
	else {
		rank_value = rank_value_at(int(pixel_coord.x), int(pixel_coord.y), dim, frame);
	}
	return rand_indexed(sample_index, dim, rank_value, scramble_value);
}

float3 env_radiance(float3 dir) {
	float2 tex_coord = equirect(dir, constant_buffer.params.y);
	float4 env       = sample_lod(mytexture_env, sampler_linear, tex_coord, 0.0);
	return env.rgb * abs(constant_buffer.params.x);
}

#ifdef _ENV_SAMPLING
int clamp_int(int x, int low, int high) {
	if (x < low) {
		return low;
	}
	if (x > high) {
		return high;
	}
	return x;
}

float3 env_dir(float2 uv) {
	float phi = uv.y * PI;
	float a   = uv.x * PI2 - PI - constant_buffer.params.y;
	float s   = sin(phi);
	return float3(s * cos(a), -s * sin(a), cos(phi));
}

// Direction and pdf
float4 sample_env(float u1, float u2) {
	float  fi = u1 * float(ENV_CDF_N);
	int    i  = clamp_int(int(fi), 0, ENV_CDF_N - 1);
	float  ju = fi - float(i);
	float4 a  = mytexture_env_cdf[uint2(uint(i % 256), uint(i / 256))];

	int   cell;
	float jv = 0.0;
	float pdf_uv;
	if (u2 < a.x) {
		cell = i;
		if (a.x > 0.0) {
			jv = u2 / a.x;
		}
		pdf_uv = a.z;
	}
	else {
		cell = clamp_int(int(a.y), 0, ENV_CDF_N - 1);
		if (a.x < 1.0) {
			jv = (u2 - a.x) / (1.0 - a.x);
		}
		pdf_uv = a.w;
	}

	float2 uv      = float2((float(cell % 256) + ju) / float(ENV_CDF_W), (float(cell / 256) + jv) / float(ENV_CDF_H));
	float  sin_phi = sin(uv.y * PI);
	float  pdf     = 0.0;
	if (sin_phi > 0.000001) {
		pdf = pdf_uv / (PI_SQ2 * sin_phi);
	}
	return float4(env_dir(uv), pdf);
}

bool env_sampling() {
	return (int(constant_buffer.params.w) & 1) != 0;
}

float env_pdf(float3 dir) {
	if (!env_sampling()) {
		return 0.0;
	}
	float2 uv      = equirect(dir, constant_buffer.params.y);
	int    x       = clamp_int(int(frac(uv.x) * float(ENV_CDF_W)), 0, ENV_CDF_W - 1);
	int    y       = clamp_int(int(uv.y * float(ENV_CDF_H)), 0, ENV_CDF_H - 1);
	float4 cdf     = mytexture_env_cdf[uint2(uint(x), uint(y))];
	float  sin_phi = sin(uv.y * PI);
	if (sin_phi > 0.000001) {
		return cdf.z / (PI_SQ2 * sin_phi);
	}
	return 0.0;
}

uint hash_u32(uint x) {
	x = (x ^ uint(61)) ^ (x >> uint(16));
	x = x * uint(9);
	x = x ^ (x >> uint(4));
	x = x * uint(668265261);
	x = x ^ (x >> uint(15));
	return x;
}

uint pixel_hash(uint2 pixel_coord, int sample_index, int dim) {
	return hash_u32(pixel_coord.x ^ hash_u32(pixel_coord.y ^ hash_u32(uint(sample_index) ^ hash_u32(uint(dim)))));
}

float hash_unorm(uint h) {
	return float(h >> uint(8)) / 16777216.0;
}

float rnd_fine(uint2 pixel_coord, int sample_index, int dim, sampler_cache cache, int frame) {
	uint  h = hash_u32(pixel_coord.x ^ hash_u32(pixel_coord.y ^ hash_u32(uint(sample_index) ^ hash_u32(uint(dim)))));
	float j = float(h >> uint(8)) / 16777216.0;
	return rnd(pixel_coord, sample_index, dim, cache, frame) + (j - 0.5) / 256.0;
}

float mis_weight(float pdf_a, float pdf_b) {
	float a = pdf_a * pdf_a;
	float b = pdf_b * pdf_b;
	return a / max(a + b, 0.000000001);
}

// Cosine weighted bsdf and pdf
float4 bsdf_eval(float3 wi, float3 n, float3 wo, float ndotv, float3 diffuse_weight, float3 f0, float alpha, float specular_chance) {
	float ndotl = dot(n, wi);
	if (ndotl <= 0.0) {
		return float4(0.0, 0.0, 0.0, 0.0);
	}

	float3 h      = normalize(wo + wi);
	float  ndoth  = max(dot(n, h), 0.0);
	float  vdoth  = max(dot(wo, h), 0.0);
	float  alpha2 = alpha * alpha;

	float  den      = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
	float  d        = alpha2 / max(PI * den * den, 0.000000001);
	float  lambda_v = smith_lambda(ndotv, alpha2);
	float  lambda_l = smith_lambda(ndotl, alpha2);
	float3 fresnel  = f_schlick(f0, vdoth);

	float3 spec = fresnel * (d / ((1.0 + lambda_v + lambda_l) * max(4.0 * ndotv * ndotl, 0.000000001)));
	float3 diff = diffuse_weight / PI;

	float pdf_spec = d / ((1.0 + lambda_v) * max(4.0 * ndotv, 0.000000001));
	float pdf_diff = ndotl / PI;

	return float4((diff + spec) * ndotl, specular_chance * pdf_spec + (1.0 - specular_chance) * pdf_diff);
}

bool occluded(float3 origin, float3 dir) {
	ray shadow_ray;
	shadow_ray.origin    = origin;
	shadow_ray.direction = dir;
	shadow_ray.min       = 0.0001;
	shadow_ray.max       = 100.0;
	ray_query q;
	ray_query_trace_any(q, scene, shadow_ray);
	return ray_query_hit(q);
}
#endif

#ifdef _GRAINS
const float GRAIN_CELL   = 0.004; // Grain spacing in world units
const float GRAIN_TOP    = 0.002; // Highest grain point above the plane, half a cell
const int   GRAIN_STEPS  = 64;    // Half cells walked before a ray is lost in the packing
// Fast grain
const int GRAIN_BOUNCES = 3; // Grains a path scatters off, the last one only gathers direct light
//
const float GRAIN_BOTTOM = 0.04;  // Rays this far below the plane are lost

struct grain_hit {
	float3 pos;
	float3 nor;
	uint   id;
	int    state; // 0 lost, 1 grain hit, 2 left the packing, above the plane or at the limit
};

uint grain_hash(float3 cell) {
	return hash_u32(uint(int(cell.x)) ^ hash_u32(uint(int(cell.y)) ^ hash_u32(uint(int(cell.z)) ^ uint(1013904223))));
}

// Distance to the grain of a cell along the ray, -1 on miss; normal in xyz
float4 grain_intersect(float3 cell, float3 o, float3 d, float3 p0, float3 pn) {
	// Two hashes sliced into the grain's random values, 10 bits for the center, 8 for the rest
	uint   h      = grain_hash(cell);
	float3 jitter = float3(float(h & uint(1023)), float((h >> uint(10)) & uint(1023)), float((h >> uint(20)) & uint(1023)));
	float3 center = (cell + (jitter + 0.5) / 1024.0) * GRAIN_CELL;
	if (dot(center - p0, pn) > 0.0) {
		return float4(0.0, 0.0, 0.0, -1.0);
	}
	uint  h1     = hash_u32(h);
	float radius = GRAIN_CELL * (0.38 + 0.12 * float(h1 & uint(255)) / 255.0);

	// Bounding sphere first, most candidates miss
	float3 oc = o - center;
	float  bs = dot(oc, d);
	float  cs = dot(oc, oc) - radius * radius;
	if (bs * bs - dot(d, d) * cs < 0.0 || (cs > 0.0 && bs > 0.0)) {
		return float4(0.0, 0.0, 0.0, -1.0);
	}

	// Squashed along a random axis: x + k (x.a) a maps the ellipsoid to a sphere of the radius
	float  az   = float((h1 >> uint(8)) & uint(255)) / 127.5 - 1.0;
	float  aphi = PI2 * float((h1 >> uint(16)) & uint(255)) / 256.0;
	float  ar   = sqrt(max(0.0, 1.0 - az * az));
	float3 axis = float3(ar * cos(aphi), ar * sin(aphi), az);
	float  k    = 1.0 / (0.55 + 0.45 * float(h1 >> uint(24)) / 255.0) - 1.0;

	float3 os   = oc + axis * (k * dot(oc, axis));
	float3 ds   = d + axis * (k * dot(d, axis));
	float  a    = dot(ds, ds);
	float  b    = dot(os, ds);
	float  c    = dot(os, os) - radius * radius;
	float  disc = b * b - a * c;
	if (disc < 0.0) {
		return float4(0.0, 0.0, 0.0, -1.0);
	}
	float t = (-b - sqrt(disc)) / a;
	if (t < 0.0) {
		return float4(0.0, 0.0, 0.0, -1.0);
	}
	float3 ps = os + ds * t;
	float3 nr = normalize(ps + axis * (k * dot(ps, axis)));
	return float4(nr, t);
}

grain_hit trace_grains(float3 o, float3 d, float3 p0, float3 pn, bool any, float t_limit) {
	float  hc   = GRAIN_CELL * 0.5;
	float3 half = floor(o / hc);
	float3 stp  = step(float3(0.0, 0.0, 0.0), d) * 2.0 - 1.0;
	float3 ad     = max(abs(d), float3(0.0000001, 0.0000001, 0.0000001));
	float3 tdelta = float3(hc, hc, hc) / ad;
	float3 bound  = (half + max(stp, float3(0.0, 0.0, 0.0))) * hc;
	float3 tmax   = (bound - o) * stp / ad;
	float  rise   = dot(d, pn);

	grain_hit result;
	result.pos   = o;
	result.nor   = pn;
	result.id    = uint(0);
	result.state = 0;

	// Nearest hit among the candidates, kept while the next half cells share them: every other step on an axis does
	float3 last_lo = float3(0.5, 0.5, 0.5); // No lattice cell
	float  near_t  = 1000000.0;
	float3 near_n  = pn;
	float3 near_c  = float3(0.0, 0.0, 0.0);

	float t_enter = 0.0;
	for (int s = 0; s < GRAIN_STEPS; s += 1) {
		float t_exit = min(min(tmax.x, tmax.y), tmax.z);

		// Cells whose grain can reach into this half cell
		float3 full = floor(half * 0.5);
		float3 lo   = full - float3(1.0, 1.0, 1.0) + (half - full * 2.0);
		if (lo.x != last_lo.x || lo.y != last_lo.y || lo.z != last_lo.z) {
			last_lo = lo;
			near_t  = 1000000.0;
			for (int k = 0; k < 8; k += 1) {
				float3 cell = lo + float3(float(k & 1), float((k >> 1) & 1), float((k >> 2) & 1));
				float4 g    = grain_intersect(cell, o, d, p0, pn);
				if (g.w >= 0.0) {
					if (any && g.w <= t_limit) {
						result.state = 1;
						return result;
					}
					if (g.w >= t_enter - 0.000001 && g.w < near_t) {
						near_t = g.w;
						near_n = g.xyz;
						near_c = cell;
					}
				}
			}
		}
		if (near_t <= min(t_exit, t_limit) + 0.000001) {
			result.pos   = o + d * near_t;
			result.nor   = near_n;
			result.id    = grain_hash(near_c);
			result.state = 1;
			return result;
		}

		if (t_exit >= t_limit) {
			result.pos   = o + d * t_limit;
			result.state = 2;
			return result;
		}

		float3 p = o + d * t_exit;
		float  h = dot(p - p0, pn);
		if (rise > 0.0 && h > GRAIN_TOP) {
			result.pos   = p;
			result.state = 2;
			return result;
		}
		if (h + GRAIN_BOTTOM < 0.0) {
			return result;
		}

		if (tmax.x < tmax.y && tmax.x < tmax.z) {
			half.x += stp.x;
			tmax.x += tdelta.x;
		}
		else if (tmax.y < tmax.z) {
			half.y += stp.y;
			tmax.y += tdelta.y;
		}
		else {
			half.z += stp.z;
			tmax.z += tdelta.z;
		}
		t_enter = t_exit;
	}

	// Skimming along the surface, out in the open
	float3 p = o + d * t_enter;
	if (dot(p - p0, pn) > 0.0) {
		result.pos   = p;
		result.state = 2;
	}
	return result;
}

// Whether the ray crosses the hit triangle from inside, the geometric normal facing the way of the vertex normals
bool leaving_mesh(uint4 a0, uint4 a1, uint4 a2, float2 barycentrics, float3x3 obj_to_world, float3 dir) {
	float2 zw0 = s16_to_f32(a0.y);
	float2 zw1 = s16_to_f32(a1.y);
	float2 zw2 = s16_to_f32(a2.y);
	float3 vp0 = float3(s16_to_f32(a0.x), zw0.x);
	float3 vp1 = float3(s16_to_f32(a1.x), zw1.x);
	float3 vp2 = float3(s16_to_f32(a2.x), zw2.x);
	float3 n   = hit_attribute(float3(s16_to_f32(a0.z), zw0.y), float3(s16_to_f32(a1.z), zw1.y), float3(s16_to_f32(a2.z), zw2.y), barycentrics);
	float3 ng  = cross(vp1 - vp0, vp2 - vp0);
	if (dot(ng, n) < 0.0) {
		ng = -ng;
	}
	return dot(obj_to_world * ng, dir) > 0.0;
}

// Nearest texel the material was drawn to: slivers of the projected uvs can miss every texel center
uint2 drawn_texel(uint g, uint2 texel, uint2 size) {
	if (geometry_texture0(g, texel).a > 0.0) {
		return texel;
	}
	for (int ring = 1; ring < 4; ring += 1) {
		for (int y = -ring; y <= ring; y += 1) {
			for (int x = -ring; x <= ring; x += 1) {
				if (abs(x) == ring || abs(y) == ring) {
					int2  c = int2(int(texel.x) + x, int(texel.y) + y);
					uint2 t = uint2(uint(clamp_int(c.x, 0, int(size.x) - 1)), uint(clamp_int(c.y, 0, int(size.y) - 1)));
					if (geometry_texture0(g, t).a > 0.0) {
						return t;
					}
				}
			}
		}
	}
	return texel;
}

bool grain_geometry(uint g) {
	if (g > uint(21)) {
		return false;
	}
	return ((int(constant_buffer.params.w) >> (int(g) + 1)) & 1) != 0;
}

// Mineral mix around the material color: mostly quartz and feldspar tones, some white, iron stained and dark grains
float3 grain_color(float3 base, uint id) {
	float  u1 = hash_unorm(hash_u32(id + uint(11)));
	float  u2 = hash_unorm(hash_u32(id + uint(12)));
	float  u3 = hash_unorm(hash_u32(id + uint(13)));
	float3 c  = base * (0.7 + 0.6 * u1);
	c         = lerp(c, float3(luma(c), luma(c), luma(c)), 0.3 * u3);
	if (u2 < 0.06) {
		float l = luma(base) * (0.1 + 0.15 * u3);
		c       = float3(l, l, l * 0.95);
	}
	else if (u2 < 0.16) {
		c = lerp(c, float3(0.92, 0.9, 0.86), 0.6);
	}
	else if (u2 < 0.26) {
		c = c * float3(1.12, 0.8, 0.6);
	}
	return saturate(c);
}
#endif

#ifdef _VOLUMES
const int   VOLUME_MAX          = 4;
const int   VOLUME_STEPS        = 128;  // Most samples per segment
const int   VOLUME_ITERS        = 192;  // Samples and skips of empty blocks per segment
const float VOLUME_BLOCK        = 4.0;  // Cells per side of the blocks marked empty in blue, MESH_VOLUME_BLOCK
const float VOLUME_DENSITY      = 3.0;  // Optical depth scale of the gas at full density
const float FIRE_EMISSION_SCALE = 1000.0;
const float FIRE_LEVEL          = 0.04; // Flame where it starts to glow, full heat 16 times that
const float VOLUME_PHASE        = 0.0795775; // Isotropic scattering, 1 / (4 pi)

bool volume_geometry(uint g) {
	if (g > uint(23)) {
		return false;
	}
	return ((int(constant_buffer.params2.x) >> int(g)) & 1) != 0;
}

// Geometry of a volume, its texture 1 holds the record of the volume, a texel per row:
// grid corner xyz, cell size | cells xyz | transmittance per unit of distance rgb, scattering | box around the gas min xyz,
// flame glow per unit of distance | box max xyz
uint vol_geometry(int v) {
	return (uint(constant_buffer.params2.z) >> uint(v * 5)) & uint(31);
}

float4 vol_record(uint g, int row) {
	return geometry_texture1(g, uint2(uint(row), uint(0)));
}

float3 fire_color(float heat) {
	float k = 1300.0 + 1000.0 * saturate(heat);
	return float3(0.5961, 1.0, 2.3149) * exp(float3(11.3739, 11.3739, 11.3739) - float3(23586.9, 26160.0, 30941.9) / k);
}

float4 volume_cell(uint g, int i, int j, int k, int nx, int ny, int tiles) {
	uint2 t = uint2(uint((k % tiles) * nx + i), uint((k / tiles) * ny + j));
	return geometry_texture0(g, t);
}

struct volume_path {
	float3 emission; // Glow reaching the start of the segment
	float3 trans;    // Transmittance up to the end, or to the scattering
	float  t;        // Where it scatters
	int    state;    // 0 passed, 1 scattered, 2 absorbed
};

volume_path volume_march(float3 o, float3 d, float seg, float tau_target, float jitter, float step_cells, uint seed, bool shadow) {
	volume_path res;
	res.emission = float3(0.0, 0.0, 0.0);
	res.trans    = float3(1.0, 1.0, 1.0);
	res.t        = seg;
	res.state    = 0;

	float3 dd = d;
	if (abs(dd.x) < 0.00000001) {
		dd.x = 0.00000001;
	}
	if (abs(dd.y) < 0.00000001) {
		dd.y = 0.00000001;
	}
	if (abs(dd.z) < 0.00000001) {
		dd.z = 0.00000001;
	}
	float3 inv    = float3(1.0, 1.0, 1.0) / dd;
	float3 toward = step(float3(0.0, 0.0, 0.0), dd); // Far side of a block per axis
	int    count  = int(constant_buffer.params2.y);
	int    passed = 0;
	float  tau    = 0.0;
	for (int n = 0; n < VOLUME_MAX; n += 1) {
		// Nearest volume left
		int   v     = -1;
		float enter = seg;
		float leave = 0.0;
		for (int u = 0; u < VOLUME_MAX; u += 1) {
			if (u < count && ((passed >> u) & 1) == 0) {
				uint   gu  = vol_geometry(u);
				float3 ta  = (vol_record(gu, 3).xyz - o) * inv;
				float3 tb  = (vol_record(gu, 4).xyz - o) * inv;
				float3 tmn = min(ta, tb);
				float3 tmx = max(ta, tb);
				float  en  = max(max(tmn.x, tmn.y), max(tmn.z, 0.0));
				float  le  = min(min(tmx.x, tmx.y), min(tmx.z, seg));
				if (en < le && en < enter) {
					v     = u;
					enter = en;
					leave = le;
				}
			}
		}
		if (v < 0) {
			return res;
		}
		passed           = passed + (1 << v);
		uint   g         = vol_geometry(v);
		float4 grid      = vol_record(g, 0);
		float4 cells     = vol_record(g, 1);
		float4 smoke     = vol_record(g, 2);
		float  glow_unit = vol_record(g, 3).w;
		int    nx        = int(cells.x);
		int    ny        = int(cells.y);
		int    nz        = int(cells.z);
		int    tiles     = int(geometry_texture0_size(g).x) / nx;
		float3 absorb    = log(max(smoke.xyz, float3(0.001, 0.001, 0.001))) * VOLUME_DENSITY; // Per unit of density and distance
		float  scatter   = smoke.w * VOLUME_DENSITY;
		if (shadow) {
			absorb = absorb - scatter;
		}
		float3 dq = d / grid.w; // Cells per unit of distance

		// Cells apart, fewer for long segments, the jitter evens the steps out over the samples
		float dt = grid.w * step_cells;
		if (leave - enter > dt * float(VOLUME_STEPS)) {
			dt = (leave - enter) / float(VOLUME_STEPS);
		}
		float tb = enter; // Start of the interval the next sample stands for
		for (int s = 0; s < VOLUME_ITERS && tb < leave; s += 1) {
			float  t  = tb + jitter * dt;
			float3 q  = (o - grid.xyz) / grid.w + dq * t;
			uint   h  = hash_u32(seed ^ uint(s * 7919 + v * 104729));
			float3 qs = q + float3(float(h & uint(1023)), float((h >> uint(10)) & uint(1023)), float((h >> uint(20)) & uint(1023))) / 1024.0 - 0.5;
			float3 fl = floor(qs);
			int    i  = clamp_int(int(fl.x), 0, nx - 1);
			int    j  = clamp_int(int(fl.y), 0, ny - 1);
			int    k  = clamp_int(int(fl.z), 0, nz - 1);
			float4 c  = volume_cell(g, i, j, k, nx, ny, tiles);
			if (c.b > 0.5) {
				// Empty block, gas free a cell around it too, on to where the ray leaves it
				float3 lo = floor(float3(float(i), float(j), float(k)) / VOLUME_BLOCK) * VOLUME_BLOCK;
				float3 ex = (lo + toward * VOLUME_BLOCK - q) * grid.w * inv;
				tb        = t + max(min(min(ex.x, ex.y), ex.z), 0.0) + grid.w * 0.01;
			}
			else {
				float3 glow = float3(0.0, 0.0, 0.0);
				if (c.r > FIRE_LEVEL && !shadow) {
					float heat = saturate(log(c.r / FIRE_LEVEL) / 2.7725887); // log(16)
					glow       = fire_color(heat) * (glow_unit * heat);
				}
				// Thin smoke changes little, twice as far
				float h = dt;
				if (c.r <= FIRE_LEVEL && c.g < 0.05) {
					h = dt * 2.0;
				}
				// Glow of the step, dimmed by the gas before it
				res.emission += res.trans * glow * h;
				if (!shadow) {
					tau += scatter * c.g * h;
				}
				if (tau >= tau_target) {
					res.t     = t;
					res.state = 1;
					return res;
				}
				res.trans = res.trans * exp(absorb * (c.g * h));
				if (max(max(res.trans.x, res.trans.y), res.trans.z) < 0.002) {
					res.state = 2;
					return res;
				}
				tb += h;
			}
		}
	}
	return res;
}
#endif

#ifdef _FOG
const float FOG_DENSITY_SCALE  = 0.5;
const float FOG_HEIGHT_FALLOFF = 0.25;
const float FOG_BASE_Z         = 0.0;
const float FOG_MAX_DIST       = 25.0;
const float FOG_ANISOTROPY     = 0.5;

float fog_tau_above(float oz, float dz, float a, float b, float sigma0) {
	float len = b - a;
	if (len <= 0.0) {
		return 0.0;
	}
	float za = oz + a * dz;
	float zb = oz + b * dz;
	float sa = sigma0 * exp(-FOG_HEIGHT_FALLOFF * max(za - FOG_BASE_Z, 0.0));
	float c  = FOG_HEIGHT_FALLOFF * dz;
	if (abs(c) < 0.0001) {
		return sa * len;
	}
	float sb = sigma0 * exp(-FOG_HEIGHT_FALLOFF * max(zb - FOG_BASE_Z, 0.0));
	return max((sa - sb) / c, 0.0);
}

float fog_sample_above(float oz, float dz, float a, float b, float sigma0, float tau_target) {
	float za = oz + a * dz;
	float sa = sigma0 * exp(-FOG_HEIGHT_FALLOFF * max(za - FOG_BASE_Z, 0.0));
	float c  = FOG_HEIGHT_FALLOFF * dz;
	if (abs(c) < 0.0001) {
		return min(a + tau_target / max(sa, 0.0000001), b);
	}
	float st = max(sa - c * tau_target, 0.0000001);
	return clamp(a - log(st / max(sa, 0.0000001)) / c, a, b);
}

float fog_optical_depth(float3 o, float3 d, float seg) {
	float sigma0 = constant_buffer.fog.w * FOG_DENSITY_SCALE;
	float len    = min(seg, FOG_MAX_DIST);
	if (sigma0 <= 0.0 || len <= 0.0) {
		return 0.0;
	}
	float oz = o.z;
	float dz = d.z;
	float z1 = oz + len * dz;
	if (oz <= FOG_BASE_Z && z1 <= FOG_BASE_Z) {
		return sigma0 * len;
	}
	if (oz >= FOG_BASE_Z && z1 >= FOG_BASE_Z) {
		return fog_tau_above(oz, dz, 0.0, len, sigma0);
	}
	float t_plane = clamp((FOG_BASE_Z - oz) / dz, 0.0, len);
	if (oz < FOG_BASE_Z) {
		return sigma0 * t_plane + fog_tau_above(oz, dz, t_plane, len, sigma0);
	}
	return fog_tau_above(oz, dz, 0.0, t_plane, sigma0) + sigma0 * (len - t_plane);
}

float fog_sample_distance(float3 o, float3 d, float seg, float tau_target) {
	float sigma0 = constant_buffer.fog.w * FOG_DENSITY_SCALE;
	float len    = min(seg, FOG_MAX_DIST);
	if (sigma0 <= 0.0 || len <= 0.0) {
		return -1.0;
	}
	float oz = o.z;
	float dz = d.z;
	float z1 = oz + len * dz;
	if (oz <= FOG_BASE_Z && z1 <= FOG_BASE_Z) {
		float t = tau_target / sigma0;
		if (t < len) {
			return t;
		}
		return -1.0;
	}
	if (oz >= FOG_BASE_Z && z1 >= FOG_BASE_Z) {
		if (tau_target < fog_tau_above(oz, dz, 0.0, len, sigma0)) {
			return fog_sample_above(oz, dz, 0.0, len, sigma0, tau_target);
		}
		return -1.0;
	}
	float t_plane = clamp((FOG_BASE_Z - oz) / dz, 0.0, len);
	if (oz < FOG_BASE_Z) {
		float tau0 = sigma0 * t_plane;
		if (tau_target < tau0) {
			return tau_target / sigma0;
		}
		float rem = tau_target - tau0;
		if (rem < fog_tau_above(oz, dz, t_plane, len, sigma0)) {
			return fog_sample_above(oz, dz, t_plane, len, sigma0, rem);
		}
		return -1.0;
	}
	float tau0 = fog_tau_above(oz, dz, 0.0, t_plane, sigma0);
	if (tau_target < tau0) {
		return fog_sample_above(oz, dz, 0.0, t_plane, sigma0, tau_target);
	}
	float rem = tau_target - tau0;
	if (rem < sigma0 * (len - t_plane)) {
		return t_plane + rem / sigma0;
	}
	return -1.0;
}

float hg_phase(float cos_theta, float g) {
	float g2    = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001);
	return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

float3 sample_hg(float3 dir, float g, float u1, float u2) {
	float cos_theta;
	if (abs(g) < 0.001) {
		cos_theta = 1.0 - 2.0 * u1;
	}
	else {
		float s   = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1);
		cos_theta = clamp((1.0 + g * g - s * s) / (2.0 * g), -1.0, 1.0);
	}
	float         sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
	float         phi       = PI2 * u2;
	tangent_basis b         = create_basis(dir);
	return normalize(b.tangent * (sin_theta * cos(phi)) + b.binormal * (sin_theta * sin(phi)) + dir * cos_theta);
}
#endif

#ifdef _ENV_SAMPLING
// Of the gas between a point and the environment, 0 when anything solid is in the way
float3 shadow_transmittance(float3 origin, float3 dir, uint seed) {
	if (occluded(origin, dir)) {
		return float3(0.0, 0.0, 0.0);
	}
	float3 tr = float3(1.0, 1.0, 1.0);
#ifdef _VOLUMES
	if (constant_buffer.params2.y > 0.0) {
		float       jitter = hash_unorm(hash_u32(seed ^ uint(1640531527)));
		volume_path vp     = volume_march(origin, dir, 100.0, 1000000.0, jitter, 4.0, seed, true);
		tr                 = vp.trans;
	}
#endif
#ifdef _FOG
	if (constant_buffer.fog.w > 0.0) {
		tr = tr * exp(-fog_optical_depth(origin, dir, FOG_MAX_DIST));
	}
#endif
	return tr;
}
#endif

#[compute, threads(8, 8, 1)]
void raytrace() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(render_target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	int           frame = int(constant_buffer.eye.w);
	sampler_cache cache = init_sampler(id.xy, frame);

#ifndef _FULL
#ifdef _FAST
	int samples      = SAMPLES;
	int prev_samples = frame * SAMPLES;
	if (SAMPLES > 4) {
		int first_samples = 4;
		if (frame == 0) {
			samples      = first_samples;
			prev_samples = 0;
		}
		else {
			prev_samples = first_samples + (frame - 1) * SAMPLES;
		}
	}
#endif
#endif

	float3 accum = float3(0.0, 0.0, 0.0);
#ifndef _FULL
#ifdef _FAST
	for (int j = 0; j < samples; j += 1) {
		int sample_index = prev_samples + j;
#else
	for (int j = 0; j < SAMPLES; j += 1) {
		int sample_index = frame * SAMPLES + j;
#endif
#else
	for (int j = 0; j < SAMPLES; j += 1) {
		int sample_index = frame * SAMPLES + j;
#endif

		// AA
		float2 xy = float2(id.xy);
		xy.x += rnd(id.xy, sample_index, 0, cache, frame);
		xy.y += rnd(id.xy, sample_index, 1, cache, frame);

		ray r;
		r.min       = 0.0001;
		r.max       = 100.0;
		r.origin    = constant_buffer.eye.xyz;
		r.direction = camera_ray_direction(xy / float2(dim) * 2.0 - 1.0, constant_buffer.eye.xyz);

		float3 throughput = float3(1.0, 1.0, 1.0);
#ifdef _ENV_SAMPLING
		float bsdf_pdf = -1.0;
#endif

#ifdef _ROULETTE
		int scatters = 0;
#endif

#ifdef _SUBSURFACE
		int    medium_depth = 0;
		float  medium_sigma = 0.0;
		float3 medium_color = float3(1.0, 1.0, 1.0);
#endif

#ifdef _GRAINS
		bool   in_grains   = false;
		float3 grain_p0    = float3(0.0, 0.0, 0.0);
		float3 grain_n     = float3(0.0, 0.0, 1.0);
		float3 grain_base  = float3(0.0, 0.0, 0.0);
		float  grain_rough = 0.0;
		float  grain_metal = 0.0;
#endif
		int lead = 0; // Iterations the camera ray spent crossing into the grains, before it met anything
		// Fast grain
		int grain_bounces = 0;
		//

		for (int i = 0; i < DEPTH; i += 1) {
			int dim_base = DIM_BOUNCE + i * DIM_PER_BOUNCE;

			// Shading point, from a surface or a grain
			float3 hit         = float3(0.0, 0.0, 0.0);
			float3 n           = float3(0.0, 0.0, 1.0);
			float3 ng          = float3(0.0, 0.0, 1.0);
			float3 texcolor    = float3(0.0, 0.0, 0.0);
			float4 tex0        = float4(0.0, 0.0, 0.0, 1.0);
			float4 tex1        = float4(0.0, 0.0, 0.0, 0.0);
			float4 tex2        = float4(0.0, 0.0, 0.0, 0.0);
			bool   back_face   = false;
			bool   normal_map  = false;
			float3 uv_tangent  = float3(1.0, 0.0, 0.0);
			float3 uv_binormal = float3(0.0, 1.0, 0.0);
			bool   grain_shade = false;

			ray_query q;
			ray_query_trace(q, scene, r);
			r.min          = 0.0001;
			bool scene_hit = ray_query_hit(q);

#ifdef _FOG
			float fog_t = -1.0;
			if (constant_buffer.fog.w > 0.0 && !in_grains && medium_depth == 0) {
				float fseg = r.max;
				if (scene_hit) {
					fseg = ray_query_distance(q);
				}
				float fog_tau = 0.0 - log(max(1.0 - rnd(id.xy, sample_index, dim_base + DIM_MEDIUM, cache, frame), 0.0000001));
				fog_t         = fog_sample_distance(r.origin, r.direction, fseg, fog_tau);
			}
#endif

#ifdef _VOLUMES
			// Through the smoke and flame on the way to the hit
			if (constant_buffer.params2.y > 0.0 && !in_grains) {
				float seg = r.max;
				if (scene_hit) {
					seg = ray_query_distance(q);
				}
#ifdef _FOG
				if (fog_t >= 0.0) {
					seg = min(seg, fog_t);
				}
#endif
				float u_vol = rnd(id.xy, sample_index, dim_base + DIM_MEDIUM, cache, frame);
#ifdef _FOG
				if (constant_buffer.fog.w > 0.0) {
					u_vol = hash_unorm(pixel_hash(id.xy, sample_index, dim_base + 2713));
				}
#endif
				float tau_target = 0.0 - log(max(1.0 - u_vol, 0.0000001));
				float jitter = hash_unorm(pixel_hash(id.xy, sample_index, dim_base + 977));
				float step_cells = 4.0; // Coarser after a bounce
				if (i == lead) {
					step_cells = 2.0;
				}
				uint seed = pixel_hash(id.xy, sample_index, dim_base + 1931);
				volume_path vp = volume_march(r.origin, r.direction, seg, tau_target, jitter, step_cells, seed, false);
				accum += throughput * vp.emission;
				if (vp.state == 2) {
					break;
				}
				throughput *= vp.trans;
				if (vp.state == 1) {
#ifdef _ROULETTE
					if (scatters >= rr_start) {
						float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
						if (rnd(id.xy, sample_index, dim_base + DIM_RR, cache, frame) > rr_probability) {
							break;
						}
						throughput = throughput / rr_probability;
					}
					scatters += 1;
#endif
					r.origin = r.origin + r.direction * vp.t;
#ifdef _ENV_SAMPLING
					// Light of the environment scattered toward the ray, through the gas around
					if (env_sampling()) {
						float4 light = sample_env(rnd_fine(id.xy, sample_index, dim_base + DIM_LIGHT, cache, frame),
						                          rnd_fine(id.xy, sample_index, dim_base + DIM_LIGHT + 1, cache, frame));
						if (light.w > 0.0) {
							float3 tr = shadow_transmittance(r.origin, light.xyz, hash_u32(seed ^ uint(374761393)));
							accum += clamp(throughput * tr * env_radiance(light.xyz) * (VOLUME_PHASE * mis_weight(light.w, VOLUME_PHASE) / light.w), 0.0, 8.0);
						}
					}
#endif
					// Scattered evenly in all directions, the dimensions of a surface bounce are free here
					r.direction = uniform_sphere(rnd(id.xy, sample_index, dim_base + DIM_DIR, cache, frame),
					                             rnd(id.xy, sample_index, dim_base + DIM_DIR + 1, cache, frame));
#ifdef _ENV_SAMPLING
					bsdf_pdf = VOLUME_PHASE;
#endif
					continue;
				}
			}
#endif

#ifdef _FOG
			if (fog_t >= 0.0) {
				throughput *= min(constant_buffer.fog.rgb, float3(0.95, 0.95, 0.95));
#ifdef _ROULETTE
				if (scatters >= rr_start) {
					float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
					if (rnd(id.xy, sample_index, dim_base + DIM_RR, cache, frame) > rr_probability) {
						break;
					}
					throughput = throughput / rr_probability;
				}
				scatters += 1;
#endif
				r.origin = r.origin + r.direction * fog_t;
#ifdef _ENV_SAMPLING
				if (env_sampling()) {
					float4 light = sample_env(rnd_fine(id.xy, sample_index, dim_base + DIM_LIGHT, cache, frame),
					                          rnd_fine(id.xy, sample_index, dim_base + DIM_LIGHT + 1, cache, frame));
					if (light.w > 0.0) {
						float  phase = hg_phase(dot(r.direction, light.xyz), FOG_ANISOTROPY);
						uint   fseed = pixel_hash(id.xy, sample_index, dim_base + 4219);
						float3 tr    = shadow_transmittance(r.origin, light.xyz, fseed);
						accum += clamp(throughput * tr * env_radiance(light.xyz) * (phase * mis_weight(light.w, phase) / light.w), 0.0, 8.0);
					}
				}
#endif
				float3 prev_dir = r.direction;
				r.direction     = sample_hg(prev_dir, FOG_ANISOTROPY, rnd(id.xy, sample_index, dim_base + DIM_DIR, cache, frame),
				                            rnd(id.xy, sample_index, dim_base + DIM_DIR + 1, cache, frame));
#ifdef _ENV_SAMPLING
				bsdf_pdf = hg_phase(dot(prev_dir, r.direction), FOG_ANISOTROPY);
#endif
				continue;
			}
#endif

#ifdef _GRAINS
			grain_hit gh;
			gh.pos   = r.origin;
			gh.nor   = float3(0.0, 0.0, 1.0);
			gh.id    = uint(0);
			gh.state = 0;
			if (in_grains) {
				float t_limit = r.max;
				if (scene_hit) {
					t_limit = ray_query_distance(q);
				}
				gh = trace_grains(r.origin, r.direction, grain_p0, grain_n, false, t_limit);
				if (gh.state == 0) {
					break;
				}
				if (gh.state == 1) {
					grain_shade = true;
				}
				else {
					in_grains = false; // On to whatever the ray meets
				}
			}
#endif

			if (!grain_shade) {
				if (!scene_hit) {
					float3 texenv;
					float  weight = 1.0;
					if (i == lead && constant_buffer.params.x < 0.0) {
						texenv = float3(0.0275, 0.0275, 0.0275);
					}
					else {
						texenv = env_radiance(r.direction);
#ifdef _ENV_SAMPLING
						if (bsdf_pdf > 0.0) {
							weight = mis_weight(bsdf_pdf, env_pdf(r.direction));
						}
#endif
					}
					accum += clamp(throughput * texenv * weight, 0.0, 8.0);
					break;
				}

				uint   g            = ray_query_geometry(q);
				float2 barycentrics = ray_query_barycentrics(q);
				uint4  a0           = ray_query_vertex(q, 0); // posxy, poszw, nor, tex
				uint4  a1           = ray_query_vertex(q, 1);
				uint4  a2           = ray_query_vertex(q, 2);

				float2 uv0 = s16_to_f32(a0.w);
				float2 uv1 = s16_to_f32(a1.w);
				float2 uv2 = s16_to_f32(a2.w);
				float2 tc  = hit_attribute2d(uv0, uv1, uv2, barycentrics) * constant_buffer.params.z;

				uint2  size  = geometry_texture0_size(g);
				uint2  texel = uint2(frac(tc) * float2(size));
				tex0         = geometry_texture0(g, texel);

				float  ray_t = ray_query_distance(q);
				hit          = r.origin + r.direction * ray_t;

#ifdef _SUBSURFACE
				if (medium_depth > 0) {
					// Free flight through the medium, scatter before the surface or absorb along the way
					float sigma = medium_sigma * float(medium_depth);
					float ts    = ray_t + 1.0;
					if (sigma > 0.0) {
						ts = -log(max(1.0 - rnd(id.xy, sample_index, dim_base + DIM_MEDIUM, cache, frame), 0.0000001)) / sigma;
					}
					if (ts < ray_t) {
						throughput *= pow(max(medium_color, float3(0.001, 0.001, 0.001)), ts);
#ifdef _ROULETTE
						if (scatters >= rr_start) {
							float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
							if (rnd(id.xy, sample_index, dim_base + DIM_RR, cache, frame) > rr_probability) {
								break;
							}
							throughput = throughput / rr_probability;
						}
						scatters += 1;
#endif
						r.origin    = r.origin + r.direction * ts;
						r.direction = uniform_sphere(rnd(id.xy, sample_index, dim_base + DIM_MEDIUM + 1, cache, frame),
						                             rnd(id.xy, sample_index, dim_base + DIM_MEDIUM + 2, cache, frame));
						bsdf_pdf    = -1.0; // Not covered by light sampling
						continue;
					}
					throughput *= pow(max(medium_color, float3(0.001, 0.001, 0.001)), ray_t);
				}
#endif

				float2 zw0 = s16_to_f32(a0.y);
				float2 zw1 = s16_to_f32(a1.y);
				float2 zw2 = s16_to_f32(a2.y);
				float3 vp0 = float3(s16_to_f32(a0.x), zw0.x);
				float3 vp1 = float3(s16_to_f32(a1.x), zw1.x);
				float3 vp2 = float3(s16_to_f32(a2.x), zw2.x);
				float3 vn0 = float3(s16_to_f32(a0.z), zw0.y);
				float3 vn1 = float3(s16_to_f32(a1.z), zw1.y);
				float3 vn2 = float3(s16_to_f32(a2.z), zw2.y);
				n          = normalize(hit_attribute(vn0, vn1, vn2, barycentrics));

				ng = cross(vp1 - vp0, vp2 - vp0);
				if (dot(ng, ng) > 0.00000000000000000001) {
					ng = normalize(ng);
				}
				else {
					ng = n;
				}
				if (dot(ng, n) < 0.0) {
					ng = -ng;
				}

				float3 n_object = n;

				float3x3 obj_to_world = ray_query_object_to_world(q);
				n                     = normalize(obj_to_world * n);
				ng                    = normalize(obj_to_world * ng);

				back_face = dot(ng, r.direction) > 0.0;
				if (back_face) {
					ng = -ng;
					n  = -n;
				}

#ifdef _VOLUMES
				if (volume_geometry(g)) {
					float2 vuv = hit_attribute2d(uv0, uv1, uv2, barycentrics);
					// Spark, the only triangles of the gas the path tracer gets
					accum += throughput * fire_color(vuv.x) * ((0.0 - max(vuv.y, -1.0)) * FIRE_EMISSION_SCALE);
					break;
				}
#endif

#ifdef _GRAINS
				if (grain_geometry(g)) {
					if (lead == i) {
						lead += 1;
					}
					if (back_face) {
						// Leaving the sand, not a surface
						r.origin = offset_ray(hit, ng, r.direction);
						continue;
					}
					uint2  gtexel = drawn_texel(g, texel, size);
					float4 gtex0  = geometry_texture0(g, gtexel);
					float4 gtex2  = geometry_texture2(g, gtexel);
					// The smooth normal bounds the grains, facets of the quantized mesh can be far off.
					// Where the ray would not run into the grains below it, the facet it hit does
					in_grains    = true;
					grain_p0     = hit;
					grain_n      = n;
					if (dot(r.direction, n) > -0.05) {
						grain_n = ng;
					}
					grain_base   = srgb_to_linear(gtex0.rgb);
					grain_rough  = gtex2.g;
					grain_metal  = gtex2.b;
					// Back up to where the ray crossed the top of the grains, the way there is clear,
					// and trace again past this hit for where the ray leaves the mesh
					float back = min(GRAIN_TOP / max(-dot(r.direction, grain_n), 0.05), ray_t * 0.999);
					r.origin   = hit - r.direction * back;
					r.min      = back + 0.0001;
					continue;
				}
#endif

#ifdef _INDIRECT_SKIP_NORMAL
				normal_map = i == 0;
#else
				normal_map = true;
#endif

				if (normal_map) {
					tex1 = geometry_texture1(g, texel);

					tangent_basis uv_basis = create_uv_basis(vp0, vp1, vp2, uv0, uv1, uv2, n_object);
					uv_tangent             = obj_to_world * uv_basis.tangent;
					uv_binormal            = obj_to_world * uv_basis.binormal;
					uv_tangent             = normalize(uv_tangent - n * dot(n, uv_tangent));
					uv_binormal            = normalize(uv_binormal - n * dot(n, uv_binormal) - uv_tangent * dot(uv_tangent, uv_binormal));
					if (back_face) {
						uv_binormal = -uv_binormal;
					}
				}

				texcolor = srgb_to_linear(tex0.rgb);

#ifdef _SUBSURFACE
				bool medium_surface = int(tex1.a * 255.0 + 0.5) % 3 == 2;
#else
				bool medium_surface = false;
#endif

#ifdef _TRANSLUCENCY
				if (!medium_surface && !ray_query_front_face(q)) {
					throughput *= pow(max(texcolor, 0.001), ray_t * tex0.a);
				}
#endif

#ifdef _EMISSION
				if (int(tex1.a * 255.0 + 0.5) % 3 == 1) { // matid
					accum += throughput * texcolor * 100.0;
					break;
				}
#endif

				tex2 = geometry_texture2(g, texel);

#ifdef _SUBSURFACE
				if (medium_surface) {
					// Smooth to rough dielectric with ior = 1 + metallic
					// Inside, 1 - occlusion sets the scattering density and base color the transmittance per unit of distance
					float  ior      = 1.0 + tex2.b;
					bool   entering = !back_face;
					bool   crosses  = true;
					float3 sdir     = r.direction;
					if (ior > 1.001) {
						float3        wo      = -r.direction;
						tangent_basis mbasis  = create_basis(n);
						float         malpha  = max(tex2.g * tex2.g, 0.001);
						float3        v_local = float3(dot(wo, mbasis.tangent), dot(wo, mbasis.binormal), max(dot(wo, n), 0.001));
						float3        h_local = sample_ggx_vndf(v_local, malpha, rnd(id.xy, sample_index, dim_base + DIM_DIR, cache, frame),
						                                        rnd(id.xy, sample_index, dim_base + DIM_DIR + 1, cache, frame));
						float3        h       = normalize(mbasis.tangent * h_local.x + mbasis.binormal * h_local.y + n * h_local.z);
						float         eta     = 1.0 / ior;
						if (!entering) {
							eta = ior;
						}
						float  cos_i = max(dot(wo, h), 0.0);
						float3 t     = refract_dir(r.direction, h, eta);
						if (rnd(id.xy, sample_index, dim_base + DIM_SELECT, cache, frame) < fresnel_dielectric(cos_i, eta) || dot(t, t) == 0.0) {
							sdir    = reflect(r.direction, h);
							crosses = false;
							if (dot(sdir, ng) <= 0.0) {
								sdir = reflect(r.direction, ng); // Microfacet pointed it below the surface
							}
						}
						else {
							sdir = normalize(t);
							if (dot(sdir, ng) >= 0.0) {
								sdir = r.direction;
							}
						}
					}
					if (crosses) {
						if (entering) {
							medium_depth += 1;
							medium_sigma = (1.0 - tex2.r) * (1.0 - tex2.r) * 16.0;
							medium_color = texcolor;
						}
						else if (medium_depth > 0) {
							medium_depth -= 1;
						}
					}
					r.direction = sdir;
					r.origin    = offset_ray(hit, ng, sdir);
					bsdf_pdf    = -1.0; // Shadow rays stop at media boundaries, only this path reaches the light
					continue;
				}
#endif
			}

#ifdef _GRAINS
			if (grain_shade) {
				hit      = gh.pos;
				n        = gh.nor;
				ng       = gh.nor;
				texcolor = grain_color(grain_base, gh.id);
				// Rounded, partly polished grains: some glint, most stay matte
				float u  = hash_unorm(hash_u32(gh.id + uint(14)));
				tex2     = float4(1.0, saturate(grain_rough * (0.45 + 0.5 * u)), grain_metal, 0.0);
			}
#endif

			float f = rnd(id.xy, sample_index, dim_base + DIM_SELECT, cache, frame);

#ifdef _TRANSLUCENCY
			if (f > tex0.a) {
				tangent_basis sbasis = create_basis(r.direction);
				float3 sdir = cos_weighted_direction(sbasis.tangent, sbasis.binormal, r.direction, rnd(id.xy, sample_index, dim_base + DIM_DIR, cache, frame),
				                                     rnd(id.xy, sample_index, dim_base + DIM_DIR + 1, cache, frame));
				r.direction = normalize(lerp(r.direction, sdir, tex2.g * tex2.g * 0.5));
				r.origin    = offset_ray(hit, ng, r.direction);
				bsdf_pdf    = -1.0; // Shadow rays stop at translucent surfaces, only this path reaches the light
				continue;
			}

			f = rnd(id.xy, sample_index, dim_base + DIM_SELECT2, cache, frame);
#endif

#ifdef _ROULETTE
			if (scatters >= rr_start) {
				float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
				if (rnd(id.xy, sample_index, dim_base + DIM_RR, cache, frame) > rr_probability) {
					break;
				}
				throughput = throughput / rr_probability;
			}
			scatters += 1;
#endif

			float3 tangent;
			float3 binormal;

			if (normal_map) {
				tangent  = uv_tangent;
				binormal = uv_binormal;

				float3 tn = normalize(tex1.rgb * 2.0 - 1.0);
				n         = normalize(tangent * tn.x - binormal * tn.y + n * tn.z);

				if (dot(n, ng) < 0.0001) {
					n = normalize(n + ng * (0.0001 - dot(n, ng)));
				}
			}

			float3 wo    = -r.direction;
			float  ndotv = dot(n, wo);
			if (ndotv < 0.001) {
				if (i > lead) {
					break;
				}
				n           = normalize(n + wo * (0.001 - ndotv));
				float ndotg = dot(n, ng);
				if (ndotg < 0.001) {
					n = normalize(n + ng * (0.001 - ndotg));
				}
				ndotv = max(dot(n, wo), 0.001);
			}

			tangent_basis nbasis = create_basis(n);
			tangent              = nbasis.tangent;
			binormal             = nbasis.binormal;

			float3 albedo         = surface_albedo(texcolor, tex2.b);
			float3 f0             = surface_specular(texcolor, tex2.b);
			float  roughness      = tex2.g;
			float3 spec_weight    = spec_directional_albedo(f0, ndotv, roughness);
			float3 diffuse_weight = albedo * (1.0 - spec_weight);

			float ls              = luma(spec_weight);
			float ld              = luma(diffuse_weight);
			float specular_chance = clamp(ls / max(ls + ld, 0.00001), 0.05, 0.995);

			float alpha = max(roughness * roughness, 0.001);

#ifdef _ENV_SAMPLING
			if (env_sampling()) {
				float4 light = sample_env(rnd_fine(id.xy, sample_index, dim_base + DIM_LIGHT, cache, frame), rnd_fine(id.xy, sample_index, dim_base + DIM_LIGHT + 1, cache, frame));
				float3 wl        = light.xyz;
				float  pdf_light = light.w;
				if (pdf_light > 0.0 && dot(wl, ng) > 0.0) {
					float4 eval  = bsdf_eval(wl, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance);
					float3 f_cos = eval.xyz;
					if (max(max(f_cos.r, f_cos.g), f_cos.b) > 0.0) {
						float3 shadow_origin = offset_ray(hit, ng, wl);
						bool   blocked       = false;
#ifdef _GRAINS
						if (grain_shade) {
							// Through the grains up to what the ray meets, past the sand mesh when leaving it
							ray sr;
							sr.origin    = shadow_origin;
							sr.direction = wl;
							sr.min       = 0.0001;
							sr.max       = 100.0;
							ray_query sq;
							ray_query_trace(sq, scene, sr);
							float t_limit = sr.max;
							if (ray_query_hit(sq)) {
								t_limit = ray_query_distance(sq);
							}
							grain_hit sh = trace_grains(shadow_origin, wl, grain_p0, grain_n, true, t_limit);
							blocked      = sh.state != 2;
							if (!blocked && ray_query_hit(sq)) {
								blocked = !grain_geometry(ray_query_geometry(sq)) ||
								          !leaving_mesh(ray_query_vertex(sq, 0), ray_query_vertex(sq, 1), ray_query_vertex(sq, 2),
								                        ray_query_barycentrics(sq), ray_query_object_to_world(sq), wl);
								shadow_origin = shadow_origin + wl * (t_limit + 0.0001);
							}
						}
#endif
						if (!blocked) {
							// Through the gas on the way, if any
							uint   sseed = pixel_hash(id.xy, sample_index, dim_base + 3301);
							float3 tr    = shadow_transmittance(shadow_origin, wl, sseed);
							accum += clamp(throughput * f_cos * env_radiance(wl) * tr * (mis_weight(pdf_light, eval.w) / pdf_light), 0.0, 8.0);
						}
					}
				}
			}
#endif

			// Fast grain
			if (grain_shade) {
				grain_bounces += 1;
				if (grain_bounces >= GRAIN_BOUNCES) {
					break;
				}
			}
			//

#ifndef _FULL
			if (i + 1 >= DEPTH) {
				break;
			}
#endif

			float u1 = rnd(id.xy, sample_index, dim_base + DIM_DIR, cache, frame);
			float u2 = rnd(id.xy, sample_index, dim_base + DIM_DIR + 1, cache, frame);

			if (f < specular_chance) {
				float  alpha2  = alpha * alpha;
				float3 v_local = float3(dot(wo, tangent), dot(wo, binormal), ndotv);
				float3 h_local = sample_ggx_vndf(v_local, alpha, u1, u2);
				float3 l_local = reflect(-v_local, h_local);
				if (l_local.z <= 0.0) {
					break;
				}
				r.direction = tangent * l_local.x + binormal * l_local.y + n * l_local.z;

				float lambda_v   = smith_lambda(v_local.z, alpha2);
				float lambda_l   = smith_lambda(l_local.z, alpha2);
				float g2_over_g1 = (1.0 + lambda_v) / (1.0 + lambda_v + lambda_l);
				throughput *= f_schlick(f0, max(dot(v_local, h_local), 0.0)) * (g2_over_g1 / specular_chance);
			}
			else {
				r.direction = cos_weighted_direction(tangent, binormal, n, u1, u2);
				throughput *= diffuse_weight / (1.0 - specular_chance);
			}

			if (dot(r.direction, ng) <= 0.0) {
				break;
			}
			if (max(max(throughput.r, throughput.g), throughput.b) <= 0.0) {
				break;
			}

			r.origin = offset_ray(hit, ng, r.direction);

#ifdef _ENV_SAMPLING
			float4 next_eval = bsdf_eval(r.direction, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance);
			bsdf_pdf         = next_eval.w;
#endif

		}
	}

#ifndef _FULL
#ifdef _FAST
	accum        = accum / float(samples);
	float3 color = accum;
	if (frame > 0) {
		float4 target = render_target[id.xy];
		float  a      = float(samples) / float(prev_samples + samples);
		color         = lerp(target.xyz, accum, a);
	}
#else
	accum        = accum / float(SAMPLES);
	float3 color = accum;
	if (frame > 0) {
		float4 target = render_target[id.xy];
		float  a      = 1.0 / (constant_buffer.eye.w + 1.0);
		color         = lerp(target.xyz, accum, a);
	}
#endif
#else
	float4 target = render_target[id.xy];
	float3 color  = target.xyz;
	accum         = accum / float(SAMPLES);

	float a = 1.0 / (constant_buffer.eye.w + 1.0);
	color   = lerp(color, accum, a);
#endif

	render_target[id.xy] = float4(color, 1.0);
}
