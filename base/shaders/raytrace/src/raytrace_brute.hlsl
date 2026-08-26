#ifdef _FULL
#define _EMISSION
#define _SUBSURFACE
#define _TRANSLUCENCY
#define _ROULETTE
#endif
#ifndef _FULL
#define _ENV_SAMPLING
#endif
// #define _RENDER

#if !defined(_EMISSION) && !defined(_SUBSURFACE)
#define _INDIRECT_SKIP_NORMAL
#endif

#include "math.hlsl"

struct Vertex {
	uint posxy;
	uint poszw;
	uint nor;
	uint tex;
};

struct RayGenConstantBuffer {
	float4 eye; // xyz, frame
	float4x4 inv_vp;
	float4 params; // envstr, envangle, uvscale, env sampling enabled
};

RWTexture2D<half4> render_target : register(u0);
RaytracingAccelerationStructure scene : register(t0);
ByteAddressBuffer indices : register(t1);
StructuredBuffer<Vertex> vertices : register(t2);
ConstantBuffer<RayGenConstantBuffer> constant_buffer : register(b0);

Texture2D<float4> mytexture0 : register(t3);
Texture2D<float4> mytexture1 : register(t4);
Texture2D<float4> mytexture2 : register(t5);
Texture2D<float4> mytexture_env : register(t6);
Texture2D<float4> mytexture_sobol : register(t7);
Texture2D<float4> mytexture_scramble : register(t8);
Texture2D<float4> mytexture_rank : register(t9);
#ifdef _ENV_SAMPLING
Texture2D<float4> mytexture_env_cdf : register(t10);
#endif
SamplerState sampler_linear : register(s0);

#ifdef _FULL
static const int SAMPLES = 64;
#else
static const int SAMPLES = 32;
#endif
#ifdef _TRANSLUCENCY
static const int DEPTH = 16;
#else
static const int DEPTH = 3; // Opaque hits
#endif

static const int DIM_BOUNCE = 2;
#ifdef _ROULETTE
static const int DIM_RR = 0;
static const int DIM_SELECT = 1;
#else
static const int DIM_SELECT = 0;
#endif
#ifdef _TRANSLUCENCY
static const int DIM_SELECT2 = DIM_SELECT + 1;
static const int DIM_DIR = DIM_SELECT + 2;
#else
static const int DIM_DIR = DIM_SELECT + 1;
#endif
#ifdef _ENV_SAMPLING
static const int DIM_LIGHT = DIM_DIR + 2;
static const int DIM_PER_BOUNCE = DIM_LIGHT + 2;
#else
static const int DIM_PER_BOUNCE = DIM_DIR + 2;
#endif

#ifdef _ROULETTE
static const int rr_start = 2;
static const float rr_max = 0.5;
static const float rr_min = 0.05;
#endif

static const float PI = 3.1415926535;
static const float PI2 = 6.283185307;
#ifdef _ENV_SAMPLING
static const float PI_SQ2 = 19.739208802;
static const int ENV_CDF_W = 256;
static const int ENV_CDF_H = 128;
static const int ENV_CDF_N = ENV_CDF_W * ENV_CDF_H;
#endif

static const int RANK_CACHE_DIMS = 16;

static uint2 scramble_cache;
static uint4 rank_cache;

uint table_word(Texture2D<float4> tex, int i) {
	int t = (i & 131071) >> 2;
	float4 c = tex.Load(uint3(uint(t & 127), uint(t >> 7), 0));
	return uint(c.r * 255) | (uint(c.g * 255) << 8) | (uint(c.b * 255) << 16) | (uint(c.a * 255) << 24);
}

void init_sampler(uint2 pixel_coord, int frame) {
	int pixel_i = (int(pixel_coord.x) + frame * 9) & 127;
	int pixel_j = (int(pixel_coord.y) + frame * 11) & 127;
	int base = (pixel_i + pixel_j * 128) * 8;

	scramble_cache = uint2(table_word(mytexture_scramble, base),
		table_word(mytexture_scramble, base + 4));

	rank_cache = uint4(table_word(mytexture_rank, base),
		table_word(mytexture_rank, base + 4),
		table_word(mytexture_rank, base + 8),
		table_word(mytexture_rank, base + 12));
}

float rnd(uint2 pixel_coord, int sample_index, int dim) {
	int k = dim & 7;
	uint scramble_value = ((k < 4 ? scramble_cache.x : scramble_cache.y) >> ((k & 3) * 8)) & 255;
	uint rank_value;
	if (dim < RANK_CACHE_DIMS) {
		int slot = dim >> 2;
		uint packed = slot == 0 ? rank_cache.x : (slot == 1 ? rank_cache.y : (slot == 2 ? rank_cache.z : rank_cache.w));
		rank_value = (packed >> ((dim & 3) * 8)) & 255;
	}
	else {
		rank_value = rank_value_at(int(pixel_coord.x), int(pixel_coord.y), dim, int(constant_buffer.eye.w), mytexture_rank);
	}
	return rand_indexed(sample_index, dim, rank_value, scramble_value, mytexture_sobol);
}

float3 env_radiance(float3 dir) {
	float2 tex_coord = equirect(dir, constant_buffer.params.y);
	return mytexture_env.SampleLevel(sampler_linear, tex_coord, 0).rgb * abs(constant_buffer.params.x);
}

#ifdef _ENV_SAMPLING
float3 env_dir(float2 uv) {
	float phi = uv.y * PI;
	float a = uv.x * PI2 - PI - constant_buffer.params.y;
	float s = sin(phi);
	return float3(s * cos(a), -s * sin(a), cos(phi));
}

float3 sample_env(float u1, float u2, out float pdf) {
	float fi = u1 * float(ENV_CDF_N);
	int i = clamp(int(fi), 0, ENV_CDF_N - 1);
	float ju = fi - float(i);
	float4 a = mytexture_env_cdf.Load(uint3(uint(i % ENV_CDF_W), uint(i / ENV_CDF_W), 0));

	int cell;
	float jv;
	float pdf_uv;
	if (u2 < a.x) {
		cell = i;
		jv = a.x > 0.0 ? u2 / a.x : 0.0;
		pdf_uv = a.z;
	}
	else {
		cell = clamp(int(a.y), 0, ENV_CDF_N - 1);
		jv = a.x < 1.0 ? (u2 - a.x) / (1.0 - a.x) : 0.0;
		pdf_uv = a.w;
	}

	float2 uv = float2((float(cell % ENV_CDF_W) + ju) / float(ENV_CDF_W),
		(float(cell / ENV_CDF_W) + jv) / float(ENV_CDF_H));
	float sin_phi = sin(uv.y * PI);
	pdf = sin_phi > 1e-6 ? pdf_uv / (PI_SQ2 * sin_phi) : 0.0;
	return env_dir(uv);
}

float env_pdf(float3 dir) {
	if (constant_buffer.params.w == 0.0) {
		return 0.0;
	}
	float2 uv = equirect(dir, constant_buffer.params.y);
	int x = clamp(int(frac(uv.x) * float(ENV_CDF_W)), 0, ENV_CDF_W - 1);
	int y = clamp(int(uv.y * float(ENV_CDF_H)), 0, ENV_CDF_H - 1);
	float pdf_uv = mytexture_env_cdf.Load(uint3(uint(x), uint(y), 0)).z;
	float sin_phi = sin(uv.y * PI);
	return sin_phi > 1e-6 ? pdf_uv / (PI_SQ2 * sin_phi) : 0.0;
}

float mis_weight(float pdf_a, float pdf_b) {
	float a = pdf_a * pdf_a;
	float b = pdf_b * pdf_b;
	return a / max(a + b, 1e-9);
}

void bsdf_eval(float3 wi, float3 n, float3 wo, float ndotv, float3 diffuse_weight, float3 f0,
               float alpha, float specular_chance, out float3 f_cos, out float pdf) {
	float ndotl = dot(n, wi);
	if (ndotl <= 0.0) {
		f_cos = 0;
		pdf = 0.0;
		return;
	}

	float3 h = normalize(wo + wi);
	float ndoth = max(dot(n, h), 0.0);
	float vdoth = max(dot(wo, h), 0.0);
	float alpha2 = alpha * alpha;

	float den = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
	float d = alpha2 / max(PI * den * den, 1e-9);
	float lambda_v = smith_lambda(ndotv, alpha2);
	float lambda_l = smith_lambda(ndotl, alpha2);
	float3 fresnel = f_schlick(f0, vdoth);

	float3 spec = fresnel * (d / ((1.0 + lambda_v + lambda_l) * max(4.0 * ndotv * ndotl, 1e-9)));
	float3 diff = diffuse_weight / PI;

	float pdf_spec = d / ((1.0 + lambda_v) * max(4.0 * ndotv, 1e-9));
	float pdf_diff = ndotl / PI;

	f_cos = (diff + spec) * ndotl;
	pdf = specular_chance * pdf_spec + (1.0 - specular_chance) * pdf_diff;
}

bool occluded(float3 origin, float3 dir) {
	RayDesc shadow_ray;
	shadow_ray.Origin = origin;
	shadow_ray.Direction = dir;
	shadow_ray.TMin = 0.0001;
	shadow_ray.TMax = 100.0;
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
	sq.TraceRayInline(scene, RAY_FLAG_NONE, ~0, shadow_ray);
	sq.Proceed();
	return sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
#endif

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
	uint2 dim;
	render_target.GetDimensions(dim.x, dim.y);
	if (id.x >= dim.x || id.y >= dim.y) return;

	init_sampler(id.xy, int(constant_buffer.eye.w));

	uint2 sz;
	mytexture0.GetDimensions(sz.x, sz.y);

	float3 accum = 0;
	for (int j = 0; j < SAMPLES; ++j) {
		// AA
		float2 xy = float2(id.xy);
		xy.x += rnd(id.xy, j, 0);
		xy.y += rnd(id.xy, j, 1);

		RayDesc ray;
		ray.TMin = 0.0001;
		ray.TMax = 100.0;
		generate_camera_ray(xy / float2(dim) * 2.0 - 1.0, ray.Origin, ray.Direction, constant_buffer.eye.xyz, constant_buffer.inv_vp);

		float3 throughput = float3(1, 1, 1);
		#ifdef _ENV_SAMPLING
		float bsdf_pdf = -1.0;
		#endif

		for (int i = 0; i < DEPTH; ++i) {
			int dim_base = DIM_BOUNCE + i * DIM_PER_BOUNCE;

			#ifdef _ROULETTE
			if (i >= rr_start) {
				float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
				if (rnd(id.xy, j, dim_base + DIM_RR) > rr_probability) break;
				throughput /= rr_probability;
			}
			#endif

			RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
			q.TraceRayInline(scene, RAY_FLAG_NONE, ~0, ray);
			q.Proceed();

			if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
				float3 texenv;
				float weight = 1.0;
				if (i == 0 && constant_buffer.params.x < 0) {
					texenv = 0.0275;
				}
				else {
					texenv = env_radiance(ray.Direction);
					#ifdef _ENV_SAMPLING
					if (bsdf_pdf > 0.0) {
						weight = mis_weight(bsdf_pdf, env_pdf(ray.Direction));
					}
					#endif
				}
				accum += clamp(throughput * texenv * weight, 0.0, 8.0);
				break;
			}

			uint base = q.CommittedPrimitiveIndex() * 12;
			#ifdef _MULTI
			base += q.CommittedInstanceID(); // Offset to index buffer of this instance
			#endif
			uint3 idx = indices.Load3(base);

			BuiltInTriangleIntersectionAttributes attr;
			attr.barycentrics = q.CommittedTriangleBarycentrics();

			Vertex a0 = vertices[idx[0]];
			Vertex a1 = vertices[idx[1]];
			Vertex a2 = vertices[idx[2]];

			float2 uv[3] = {s16_to_f32(a0.tex), s16_to_f32(a1.tex), s16_to_f32(a2.tex)};
			float2 tc = hit_attribute2d(uv, attr) * constant_buffer.params.z;

			uint3 texel = uint3((tc - uint2(tc)) * sz, 0);
			float4 tex0 = mytexture0.Load(texel);

			float ray_t = q.CommittedRayT();
			float3 hit = ray.Origin + ray.Direction * ray_t;

			float2 zw0 = s16_to_f32(a0.poszw);
			float2 zw1 = s16_to_f32(a1.poszw);
			float2 zw2 = s16_to_f32(a2.poszw);
			float3 vp[3] = {
				float3(s16_to_f32(a0.posxy), zw0.x),
				float3(s16_to_f32(a1.posxy), zw1.x),
				float3(s16_to_f32(a2.posxy), zw2.x)
			};
			float3 vn[3] = {
				float3(s16_to_f32(a0.nor), zw0.y),
				float3(s16_to_f32(a1.nor), zw1.y),
				float3(s16_to_f32(a2.nor), zw2.y)
			};
			float3 n = normalize(hit_attribute(vn, attr));

			float3 ng = cross(vp[1] - vp[0], vp[2] - vp[0]);
			ng = dot(ng, ng) > 1e-20 ? normalize(ng) : n;
			ng *= dot(ng, n) < 0.0 ? -1.0 : 1.0;

			float3 n_object = n;

			#ifdef _MULTI
			float3x4 objToWorld = q.CommittedObjectToWorld3x4();
			float3x3 obj_to_world = float3x3(objToWorld[0].xyz, objToWorld[1].xyz, objToWorld[2].xyz);
			n = normalize(mul(obj_to_world, n));
			ng = normalize(mul(obj_to_world, ng));
			#endif

			bool back_face = dot(ng, ray.Direction) > 0.0;
			if (back_face) {
				ng = -ng;
				n = -n;
			}

			#ifdef _INDIRECT_SKIP_NORMAL
			bool normal_map = i == 0;
			#else
			const bool normal_map = true;
			#endif

			float4 tex1 = 0;
			if (normal_map) {
				tex1 = mytexture1.Load(texel);
			}

			float3 texcolor = srgb_to_linear(tex0.rgb);

			#ifdef _TRANSLUCENCY
			if (!q.CommittedTriangleFrontFace()) {
				throughput *= pow(max(texcolor, 0.001), ray_t * tex0.a);
			}
			#endif

			#ifdef _EMISSION
			if (int(tex1.a * 255.0f) % 3 == 1) { // matid
				accum += throughput * texcolor * 100.0f;
				break;
			}
			#endif

			float4 tex2 = mytexture2.Load(texel);

			float f = rnd(id.xy, j, dim_base + DIM_SELECT);

			float3 tangent, binormal;

			#ifdef _TRANSLUCENCY
			if (f > tex0.a) {
				float3x3 sbasis = create_basis(ray.Direction);
				float3 sdir = cos_weighted_direction(sbasis[0], sbasis[1], ray.Direction,
					rnd(id.xy, j, dim_base + DIM_DIR), rnd(id.xy, j, dim_base + DIM_DIR + 1));
				ray.Direction = normalize(lerp(ray.Direction, sdir, tex2.g * tex2.g * 0.5));
				ray.Origin = offset_ray(hit, ng, ray.Direction);
				continue;
			}

			f = rnd(id.xy, j, dim_base + DIM_SELECT2);
			#endif

			if (normal_map) {
				create_uv_basis(vp[0], vp[1], vp[2], uv[0], uv[1], uv[2], n_object, tangent, binormal);

				#ifdef _MULTI
				tangent = mul(obj_to_world, tangent);
				binormal = mul(obj_to_world, binormal);
				tangent = normalize(tangent - n * dot(n, tangent));
				binormal = normalize(binormal - n * dot(n, binormal) - tangent * dot(tangent, binormal));
				#endif

				if (back_face) {
					binormal = -binormal;
				}

				tex1.rgb = normalize(tex1.rgb * 2.0 - 1.0);
				tex1.g = -tex1.g;
				n = normalize(mul(tex1.rgb, float3x3(tangent, binormal, n)));

				if (dot(n, ng) < 1e-4) {
					n = normalize(n + ng * (1e-4 - dot(n, ng)));
				}
			}

			float3 wo = -ray.Direction;
			float ndotv = dot(n, wo);
			if (ndotv < 1e-3) {
				if (i > 0) break;
				n = normalize(n + wo * (1e-3 - ndotv));
				float ndotg = dot(n, ng);
				if (ndotg < 1e-3) {
					n = normalize(n + ng * (1e-3 - ndotg));
				}
				ndotv = max(dot(n, wo), 1e-3);
			}

			float3x3 basis = create_basis(n);
			tangent = basis[0];
			binormal = basis[1];

			float3 albedo = surface_albedo(texcolor, tex2.b);
			float3 f0 = surface_specular(texcolor, tex2.b);
			float3 fresnel = f_schlick(f0, ndotv);
			float3 diffuse_weight = albedo * (1.0 - fresnel);

			float ls = luma(fresnel);
			float ld = luma(diffuse_weight);
			float specular_chance = clamp(ls / max(ls + ld, 1e-5), 0.05, 0.995);

			float alpha = max(tex2.g * tex2.g, 1e-3);

			#ifdef _ENV_SAMPLING
			if (constant_buffer.params.w != 0.0) {
				float pdf_light;
				float3 wl = sample_env(rnd(id.xy, j, dim_base + DIM_LIGHT),
					rnd(id.xy, j, dim_base + DIM_LIGHT + 1), pdf_light);
				if (pdf_light > 0.0 && dot(wl, ng) > 0.0) {
					float3 f_cos;
					float pdf_brdf;
					bsdf_eval(wl, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance, f_cos, pdf_brdf);
					if (max(max(f_cos.r, f_cos.g), f_cos.b) > 0.0 &&
						!occluded(offset_ray(hit, ng, wl), wl)) {
						accum += clamp(throughput * f_cos * env_radiance(wl) *
							(mis_weight(pdf_light, pdf_brdf) / pdf_light), 0.0, 8.0);
					}
				}
			}
			#endif

			float u1 = rnd(id.xy, j, dim_base + DIM_DIR);
			float u2 = rnd(id.xy, j, dim_base + DIM_DIR + 1);

			if (f < specular_chance) {
				float alpha2 = alpha * alpha;
				float3 v_local = float3(dot(wo, tangent), dot(wo, binormal), ndotv);
				float3 h_local = sample_ggx_vndf(v_local, alpha, u1, u2);
				float3 l_local = reflect(-v_local, h_local);
				if (l_local.z <= 0.0) break;
				ray.Direction = tangent * l_local.x + binormal * l_local.y + n * l_local.z;

				float lambda_v = smith_lambda(v_local.z, alpha2);
				float lambda_l = smith_lambda(l_local.z, alpha2);
				float g2_over_g1 = (1.0 + lambda_v) / (1.0 + lambda_v + lambda_l);
				throughput *= f_schlick(f0, max(dot(v_local, h_local), 0.0)) * (g2_over_g1 / specular_chance);
			}
			else {
				ray.Direction = cos_weighted_direction(tangent, binormal, n, u1, u2);
				throughput *= diffuse_weight / (1.0 - specular_chance);
			}

			if (dot(ray.Direction, ng) <= 0.0) break;
			if (max(max(throughput.r, throughput.g), throughput.b) <= 0.0) break;

			ray.Origin = offset_ray(hit, ng, ray.Direction);

			#ifdef _ENV_SAMPLING
			{
				float3 f_cos;
				float pdf_brdf;
				bsdf_eval(ray.Direction, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance, f_cos, pdf_brdf);
				bsdf_pdf = pdf_brdf;
			}
			#endif

			#ifdef _SUBSURFACE
			if (int(tex1.a * 255.0f) % 3 == 2) {
				float d = min(1.0 / min(ray_t * 2.0, 1.0) / 10.0, 0.5);
				throughput += throughput * d;
				if (f < 0.5) ray.Origin += ray.Direction * f * 0.001;
			}
			#endif
		}
	}

	float3 color = render_target[id.xy].xyz;
	accum /= SAMPLES;

	#ifdef _RENDER
	float a = 1.0 / (constant_buffer.eye.w + 1);
	color = color * (1.0 - a) + accum * a;
	#else
	if (constant_buffer.eye.w == 0) color = accum;
	else color = lerp(color, accum, 0.25);
	#endif

	render_target[id.xy] = half4(color, 1.0f);
}
