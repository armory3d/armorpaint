
#ifdef _FULL
#define _EMISSION
#define _SUBSURFACE
#define _TRANSLUCENCY
#define _ROULETTE
#else
#define _ENV_SAMPLING
#endif

#ifndef _EMISSION
#ifndef _SUBSURFACE
#define _INDIRECT_SKIP_NORMAL
#endif
#endif

cbuffer constant_buffer {
	float4   eye; // xyz, frame
	float4x4 inv_vp;
	float4   params; // envstr, envangle, uvscale, env sampling enabled
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
#ifdef _METAL
const int SAMPLES = 8;
#else
const int SAMPLES = 64;
#endif
#else
#ifdef _METAL
const int SAMPLES = 4;
#else
const int SAMPLES = 32;
#endif
#endif

#ifdef _TRANSLUCENCY
const int DEPTH = 16;
#else
const int DEPTH = 3; // Opaque hits
#endif

// Sample dimensions of a bounce: [roulette], select, [select2], dir.xy, [light.xy]
const int DIM_BOUNCE = 2;
#ifdef _FULL
const int DIM_RR         = 0;
const int DIM_SELECT     = 1;
const int DIM_SELECT2    = 2;
const int DIM_DIR        = 3;
const int DIM_PER_BOUNCE = 5;
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

float env_pdf(float3 dir) {
	if (constant_buffer.params.w == 0.0) {
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

#[compute, threads(8, 8, 1)]
void raytrace() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(render_target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	int           frame = int(constant_buffer.eye.w);
	sampler_cache cache = init_sampler(id.xy, frame);

	float3 accum = float3(0.0, 0.0, 0.0);
	for (int j = 0; j < SAMPLES; j += 1) {
		int sample_index = frame * SAMPLES + j;

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

		for (int i = 0; i < DEPTH; i += 1) {
			int dim_base = DIM_BOUNCE + i * DIM_PER_BOUNCE;

#ifdef _ROULETTE
			if (i >= rr_start) {
				float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
				if (rnd(id.xy, sample_index, dim_base + DIM_RR, cache, frame) > rr_probability) {
					break;
				}
				throughput = throughput / rr_probability;
			}
#endif

			ray_query q;
			ray_query_trace(q, scene, r);

			if (!ray_query_hit(q)) {
				float3 texenv;
				float  weight = 1.0;
				if (i == 0 && constant_buffer.params.x < 0.0) {
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
			float4 tex0  = geometry_texture0(g, texel);

			float  ray_t = ray_query_distance(q);
			float3 hit   = r.origin + r.direction * ray_t;

			float2 zw0 = s16_to_f32(a0.y);
			float2 zw1 = s16_to_f32(a1.y);
			float2 zw2 = s16_to_f32(a2.y);
			float3 vp0 = float3(s16_to_f32(a0.x), zw0.x);
			float3 vp1 = float3(s16_to_f32(a1.x), zw1.x);
			float3 vp2 = float3(s16_to_f32(a2.x), zw2.x);
			float3 vn0 = float3(s16_to_f32(a0.z), zw0.y);
			float3 vn1 = float3(s16_to_f32(a1.z), zw1.y);
			float3 vn2 = float3(s16_to_f32(a2.z), zw2.y);
			float3 n   = normalize(hit_attribute(vn0, vn1, vn2, barycentrics));

			float3 ng = cross(vp1 - vp0, vp2 - vp0);
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

			bool back_face = dot(ng, r.direction) > 0.0;
			if (back_face) {
				ng = -ng;
				n  = -n;
			}

#ifdef _INDIRECT_SKIP_NORMAL
			bool normal_map = i == 0;
#else
			bool normal_map = true;
#endif

			float4 tex1 = float4(0.0, 0.0, 0.0, 0.0);
			if (normal_map) {
				tex1 = geometry_texture1(g, texel);
			}

			float3 texcolor = srgb_to_linear(tex0.rgb);

#ifdef _TRANSLUCENCY
			if (!ray_query_front_face(q)) {
				throughput *= pow(max(texcolor, 0.001), ray_t * tex0.a);
			}
#endif

#ifdef _EMISSION
			if (int(tex1.a * 255.0) % 3 == 1) { // matid
				accum += throughput * texcolor * 100.0;
				break;
			}
#endif

			float4 tex2 = geometry_texture2(g, texel);

			float f = rnd(id.xy, sample_index, dim_base + DIM_SELECT, cache, frame);

#ifdef _TRANSLUCENCY
			if (f > tex0.a) {
				tangent_basis sbasis = create_basis(r.direction);
				float3 sdir = cos_weighted_direction(sbasis.tangent, sbasis.binormal, r.direction, rnd(id.xy, sample_index, dim_base + DIM_DIR, cache, frame),
				                                     rnd(id.xy, sample_index, dim_base + DIM_DIR + 1, cache, frame));
				r.direction = normalize(lerp(r.direction, sdir, tex2.g * tex2.g * 0.5));
				r.origin    = offset_ray(hit, ng, r.direction);
				continue;
			}

			f = rnd(id.xy, sample_index, dim_base + DIM_SELECT2, cache, frame);
#endif

			float3 tangent;
			float3 binormal;

			if (normal_map) {
				tangent_basis uv_basis = create_uv_basis(vp0, vp1, vp2, uv0, uv1, uv2, n_object);

				tangent  = obj_to_world * uv_basis.tangent;
				binormal = obj_to_world * uv_basis.binormal;
				tangent  = normalize(tangent - n * dot(n, tangent));
				binormal = normalize(binormal - n * dot(n, binormal) - tangent * dot(tangent, binormal));

				if (back_face) {
					binormal = -binormal;
				}

				float3 tn = normalize(tex1.rgb * 2.0 - 1.0);
				n         = normalize(tangent * tn.x - binormal * tn.y + n * tn.z);

				if (dot(n, ng) < 0.0001) {
					n = normalize(n + ng * (0.0001 - dot(n, ng)));
				}
			}

			float3 wo    = -r.direction;
			float  ndotv = dot(n, wo);
			if (ndotv < 0.001) {
				if (i > 0) {
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
			if (constant_buffer.params.w != 0.0) {
				float4 light = sample_env(rnd(id.xy, sample_index, dim_base + DIM_LIGHT, cache, frame), rnd(id.xy, sample_index, dim_base + DIM_LIGHT + 1, cache, frame));
				float3 wl        = light.xyz;
				float  pdf_light = light.w;
				if (pdf_light > 0.0 && dot(wl, ng) > 0.0) {
					float4 eval  = bsdf_eval(wl, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance);
					float3 f_cos = eval.xyz;
					if (max(max(f_cos.r, f_cos.g), f_cos.b) > 0.0) {
						if (!occluded(offset_ray(hit, ng, wl), wl)) {
							accum += clamp(throughput * f_cos * env_radiance(wl) * (mis_weight(pdf_light, eval.w) / pdf_light), 0.0, 8.0);
						}
					}
				}
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

#ifdef _SUBSURFACE
			if (int(tex1.a * 255.0) % 3 == 2) {
				float d = min(1.0 / min(ray_t * 2.0, 1.0) / 10.0, 0.5);
				throughput += throughput * d;
				if (f < 0.5) {
					r.origin += r.direction * f * 0.001;
				}
			}
#endif
		}
	}

	float4 target = render_target[id.xy];
	float3 color  = target.xyz;
	accum         = accum / float(SAMPLES);

	float a = 1.0 / (constant_buffer.eye.w + 1.0);
	color   = lerp(color, accum, a);

	render_target[id.xy] = float4(color, 1.0);
}
