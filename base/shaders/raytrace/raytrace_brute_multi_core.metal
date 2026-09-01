#define _MULTI
#ifdef _FULL
#define _EMISSION
#define _SUBSURFACE
#define _TRANSLUCENCY
#define _ROULETTE
#endif
#ifndef _FULL
#define _ENV_SAMPLING
#endif

#if !defined(_EMISSION) && !defined(_SUBSURFACE)
#define _INDIRECT_SKIP_NORMAL
#endif

using namespace metal;
using namespace raytracing;

struct Vertex {
	uint posxy;
	uint poszw;
	uint nor;
	uint tex;
};

#ifdef _MULTI
typedef intersector<triangle_data, instancing, world_space_data> intersector_t;
#else
typedef intersector<triangle_data, instancing> intersector_t;
#endif

struct RayGenConstantBuffer {
	float4 eye; // xyz, frame
	float4x4 inv_vp;
	float4 params; // envstr, envangle, uvscale, env sampling enabled
};

constant float PI = 3.1415926535;
constant float PI2 = 6.283185307;
#ifdef _ENV_SAMPLING
constant float PI_SQ2 = 19.739208802;
constant int ENV_CDF_W = 256;
constant int ENV_CDF_H = 128;
constant int ENV_CDF_N = ENV_CDF_W * ENV_CDF_H;
#endif

#ifdef _FULL
constant int SAMPLES = 8;
#else
constant int SAMPLES = 4;
#endif
#ifdef _TRANSLUCENCY
constant int DEPTH = 16;
#else
constant int DEPTH = 3; // Opaque hits
#endif

#define DIM_BOUNCE 2
#ifdef _ROULETTE
#define DIM_RR 0
#define DIM_SELECT 1
#else
#define DIM_SELECT 0
#endif
#ifdef _TRANSLUCENCY
#define DIM_SELECT2 (DIM_SELECT + 1)
#define DIM_DIR (DIM_SELECT + 2)
#else
#define DIM_DIR (DIM_SELECT + 1)
#endif
#ifdef _ENV_SAMPLING
#define DIM_LIGHT (DIM_DIR + 2)
#define DIM_PER_BOUNCE (DIM_LIGHT + 2)
#else
#define DIM_PER_BOUNCE (DIM_DIR + 2)
#endif

#ifdef _ROULETTE
constant int rr_start = 2;
constant float rr_max = 0.5;
constant float rr_min = 0.05;
#endif

void generate_camera_ray(float2 screen_pos, thread float3 & ray_origin, thread float3 & ray_dir, float3 eye, float4x4 inv_vp) {
	screen_pos.y = -screen_pos.y;
	float4 world = inv_vp * float4(screen_pos, 0, 1);
	world.xyz /= world.w;
	ray_origin = eye;
	ray_dir = normalize(world.xyz - ray_origin);
}

float2 equirect(float3 normal, float angle) {
	float phi = acos(clamp(normal.z, -1.0, 1.0));
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

constant int RANK_CACHE_DIMS = 16;

uint table_word(texture2d<float, access::read> tex, int i) {
	int t = (i & 131071) >> 2;
	float4 c = tex.read(uint2(uint(t & 127), uint(t >> 7)), 0);
	return uint(c.r * 255) | (uint(c.g * 255) << 8) | (uint(c.b * 255) << 16) | (uint(c.a * 255) << 24);
}

uint table_byte(texture2d<float, access::read> tex, int i) {
	return (table_word(tex, i) >> ((i & 3) * 8)) & 255;
}

uint2 init_scramble(uint2 tid, int frame, texture2d<float, access::read> scramble) {
	int pixel_i = (int(tid.x) + frame * 9) & 127;
	int pixel_j = (int(tid.y) + frame * 11) & 127;
	int base = (pixel_i + pixel_j * 128) * 8;
	return uint2(table_word(scramble, base), table_word(scramble, base + 4));
}

uint4 init_rank(uint2 tid, int frame, texture2d<float, access::read> rank) {
	int pixel_i = (int(tid.x) + frame * 9) & 127;
	int pixel_j = (int(tid.y) + frame * 11) & 127;
	int base = (pixel_i + pixel_j * 128) * 8;
	return uint4(table_word(rank, base),
		table_word(rank, base + 4),
		table_word(rank, base + 8),
		table_word(rank, base + 12));
}

float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame, uint2 scramble_cache, uint4 rank_cache, texture2d<float, access::read> sobol, texture2d<float, access::read> rank) {
	pixel_i += frame * 9;
	pixel_j += frame * 11;
	pixel_i = pixel_i & 127;
	pixel_j = pixel_j & 127;
	sample_index = sample_index & 255;
	sample_dimension = sample_dimension & 255;

	int ranked_sample_index;
	if (sample_dimension < RANK_CACHE_DIMS) {
		int slot = sample_dimension >> 2;
		uint packed = slot == 0 ? rank_cache.x : (slot == 1 ? rank_cache.y : (slot == 2 ? rank_cache.z : rank_cache.w));
		ranked_sample_index = sample_index ^ int((packed >> ((sample_dimension & 3) * 8)) & 255);
	}
	else {
		ranked_sample_index = sample_index ^
			int(table_byte(rank, sample_dimension + (pixel_i + pixel_j * 128) * 8));
	}

	int value = int(sobol.read(uint2(ranked_sample_index, sample_dimension), 0).r * 255);

	int k = sample_dimension & 7;
	uint packed = k < 4 ? scramble_cache.x : scramble_cache.y;
	value = value ^ int((packed >> ((k & 3) * 8)) & 255);

	float v = (0.5f + value) / 256.0f;
	return v;
}

float3 cos_weighted_direction(float3 tangent, float3 binormal, float3 n, float u1, float u2) {
	const float PI2 = 6.283185307;
	float r = sqrt(u1);
	float phi = PI2 * u2;
	return tangent * (r * cos(phi)) + binormal * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1));
}

float3 sample_ggx_vndf(float3 ve, float alpha, float u1, float u2) {
	const float PI2 = 6.283185307;
	float3 vh = normalize(float3(alpha * ve.x, alpha * ve.y, ve.z));
	float lensq = vh.x * vh.x + vh.y * vh.y;
	float3 t1 = lensq > 0.0 ? float3(-vh.y, vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
	float3 t2 = cross(vh, t1);
	float r = sqrt(u1);
	float phi = PI2 * u2;
	float p1 = r * cos(phi);
	float p2 = r * sin(phi);
	float s = 0.5 * (1.0 + vh.z);
	p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
	float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
	return normalize(float3(alpha * nh.x, alpha * nh.y, max(1e-6, nh.z)));
}

float smith_lambda(float cos_theta, float alpha2) {
	float c2 = cos_theta * cos_theta;
	return 0.5 * (sqrt(1.0 + alpha2 * (1.0 - c2) / max(c2, 1e-7)) - 1.0);
}

float3 f_schlick(float3 f0, float u) {
	float m = saturate(1.0 - u);
	float m2 = m * m;
	return f0 + (1.0 - f0) * (m2 * m2 * m);
}

float luma(float3 c) {
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 offset_ray(float3 p, float3 ng, float3 dir) {
	return p + ng * (dot(dir, ng) < 0.0 ? -1e-4 : 1e-4);
}

float2 s16_to_f32(uint val) {
	int a = (int)(val << 16) >> 16;
	int b = (int)(val & 0xffff0000) >> 16;
	return float2(a, b) / 32767.0f;
}

float3 srgb_to_linear(float3 c) {
	return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

float3 hit_world_position(ray ray, intersector_t::result_type intersection) {
	return ray.origin + ray.direction * intersection.distance;
}

float3 hit_attribute(float3 vertex_attribute[3], float2 barycentrics) {
	return vertex_attribute[0] +
		barycentrics.x * (vertex_attribute[1] - vertex_attribute[0]) +
		barycentrics.y * (vertex_attribute[2] - vertex_attribute[0]);
}

float2 hit_attribute2d(float2 vertex_attribute[3], float2 barycentrics) {
	return vertex_attribute[0] +
		barycentrics.x * (vertex_attribute[1] - vertex_attribute[0]) +
		barycentrics.y * (vertex_attribute[2] - vertex_attribute[0]);
}

void create_basis(float3 normal, thread float3 & tangent, thread float3 & binormal) {
	float s = normal.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + normal.z);
	float b = normal.x * normal.y * a;
	tangent = float3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
	binormal = float3(b, s + normal.y * normal.y * a, -normal.y);
}

void create_uv_basis(float3 p0, float3 p1, float3 p2, float2 uv0, float2 uv1, float2 uv2,
	float3 n, thread float3 & tangent, thread float3 & binormal) {
	float3 e1 = p1 - p0;
	float3 e2 = p2 - p0;
	float2 d1 = uv1 - uv0;
	float2 d2 = uv2 - uv0;
	float det = d1.x * d2.y - d2.x * d1.y;
	if (abs(det) > 1e-12) {
		float r = 1.0 / det;
		float3 tu = (e1 * d2.y - e2 * d1.y) * r;
		float3 tv = (e2 * d1.x - e1 * d2.x) * r;
		float3 t = tu - n * dot(n, tu);
		float tl = dot(t, t);
		if (tl > 1e-16) {
			t *= rsqrt(tl);
			float3 b = tv - n * dot(n, tv);
			b -= t * dot(t, b);
			float bl = dot(b, b);
			if (bl > 1e-16) {
				tangent = t;
				binormal = b * rsqrt(bl);
				return;
			}
		}
	}
	create_basis(n, tangent, binormal);
}

float3 surface_albedo(const float3 base_color, const float metalness) {
	return mix(base_color, float3(0.0, 0.0, 0.0), metalness);
}

float3 surface_specular(const float3 base_color, const float metalness) {
	return mix(float3(0.04, 0.04, 0.04), base_color, metalness);
}

float3 env_radiance(float3 dir, float angle, float strength,
                    texture2d<float, access::sample> env, sampler linear_sampler) {
	float2 tex_coord = equirect(dir, angle);
	return env.sample(linear_sampler, tex_coord, level(0)).rgb * abs(strength);
}

#ifdef _ENV_SAMPLING
float3 env_dir(float2 uv, float angle) {
	float phi = uv.y * PI;
	float a = uv.x * PI2 - PI - angle;
	float s = sin(phi);
	return float3(s * cos(a), -s * sin(a), cos(phi));
}

float3 sample_env(float u1, float u2, float angle, texture2d<float, access::read> cdf, thread float &pdf) {
	float fi = u1 * float(ENV_CDF_N);
	int i = clamp(int(fi), 0, ENV_CDF_N - 1);
	float ju = fi - float(i);
	float4 a = cdf.read(uint2(uint(i % ENV_CDF_W), uint(i / ENV_CDF_W)), 0);

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
	return env_dir(uv, angle);
}

float env_pdf(float3 dir, float angle, float enabled, texture2d<float, access::read> cdf) {
	if (enabled == 0.0) {
		return 0.0;
	}
	float2 uv = equirect(dir, angle);
	int x = clamp(int(fract(uv.x) * float(ENV_CDF_W)), 0, ENV_CDF_W - 1);
	int y = clamp(int(uv.y * float(ENV_CDF_H)), 0, ENV_CDF_H - 1);
	float pdf_uv = cdf.read(uint2(uint(x), uint(y)), 0).z;
	float sin_phi = sin(uv.y * PI);
	return sin_phi > 1e-6 ? pdf_uv / (PI_SQ2 * sin_phi) : 0.0;
}

float mis_weight(float pdf_a, float pdf_b) {
	float a = pdf_a * pdf_a;
	float b = pdf_b * pdf_b;
	return a / max(a + b, 1e-9);
}

void bsdf_eval(float3 wi, float3 n, float3 wo, float ndotv, float3 diffuse_weight, float3 f0,
               float alpha, float specular_chance, thread float3 &f_cos, thread float &pdf) {
	float ndotl = dot(n, wi);
	if (ndotl <= 0.0) {
		f_cos = float3(0.0);
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

bool occluded(float3 origin, float3 dir, instance_acceleration_structure scene) {
	ray shadow_ray;
	shadow_ray.origin = origin;
	shadow_ray.direction = dir;
	shadow_ray.min_distance = 0.0001;
	shadow_ray.max_distance = 100.0;

	intersector_t in;
	in.assume_geometry_type(geometry_type::triangle);
	in.force_opacity(forced_opacity::opaque);
	in.accept_any_intersection(true);
	return in.intersect(shadow_ray, scene).type != intersection_type::none;
}
#endif

uint2 texel_coord(float2 tex_coord, float2 tex_size) {
	return uint2(fract(tex_coord) * tex_size);
}

kernel void raytracingKernel(
	uint2 tid [[thread_position_in_grid]],
	constant RayGenConstantBuffer &constant_buffer [[buffer(0)]],
	texture2d<float, access::read_write> render_target [[texture(0)]],
	texture2d<float, access::read> mytexture0 [[texture(1)]],
	texture2d<float, access::read> mytexture1 [[texture(2)]],
	texture2d<float, access::read> mytexture2 [[texture(3)]],
	texture2d<float, access::sample> mytexture_env [[texture(4)]],
	texture2d<float, access::read> mytexture_sobol [[texture(5)]],
	texture2d<float, access::read> mytexture_scramble [[texture(6)]],
	texture2d<float, access::read> mytexture_rank [[texture(7)]],
#ifdef _ENV_SAMPLING
	texture2d<float, access::read> mytexture_env_cdf [[texture(8)]],
#endif
	sampler linear_sampler [[sampler(0)]],
	instance_acceleration_structure scene [[buffer(1)]],
	device void *indices [[buffer(2)]],
	device void *vertices [[buffer(3)]]
) {
	uint2 dim = uint2(render_target.get_width(), render_target.get_height());
	if (tid.x >= dim.x || tid.y >= dim.y) {
		return;
	}

	int frame = int(constant_buffer.eye.w);
	uint2 scramble_cache = init_scramble(tid, frame, mytexture_scramble);
	uint4 rank_cache = init_rank(tid, frame, mytexture_rank);

	float2 tex_size = float2(mytexture0.get_width(), mytexture0.get_height());
	float3 accum = float3(0, 0, 0);

	for (int j = 0; j < SAMPLES; ++j) {
		int sample_index = frame * SAMPLES + j;

		// AA
		float2 xy = float2(tid);
		xy.x += rand(tid.x, tid.y, sample_index, 0, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank);
		xy.y += rand(tid.x, tid.y, sample_index, 1, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank);

		float2 screen_pos = xy / float2(dim) * 2.0 - 1.0;
		ray ray;
		ray.min_distance = 0.0001;
		ray.max_distance = 100.0;
		generate_camera_ray(screen_pos, ray.origin, ray.direction, constant_buffer.eye.xyz, constant_buffer.inv_vp);

		float3 throughput = float3(1, 1, 1);

		#ifdef _ENV_SAMPLING
		float bsdf_pdf = -1.0;
		#endif

		for (int i = 0; i < DEPTH; ++i) {
			int dim_base = DIM_BOUNCE + i * DIM_PER_BOUNCE;

			#ifdef _ROULETTE
			if (i >= rr_start) {
				float rr_probability = clamp(max(max(throughput.r, throughput.g), throughput.b), rr_min, rr_max);
				if (rand(tid.x, tid.y, sample_index, dim_base + DIM_RR, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank) > rr_probability) {
					break;
				}
				throughput /= rr_probability;
			}
			#endif

			intersector_t in;
			in.assume_geometry_type(geometry_type::triangle);
			in.force_opacity(forced_opacity::opaque);
			in.accept_any_intersection(false);

			intersector_t::result_type intersection;
			intersection = in.intersect(ray, scene);

			// Miss
			if (intersection.type == intersection_type::none) {
				float3 texenv;
				float weight = 1.0;
				if (i == 0 && constant_buffer.params.x < 0.0) { // No envmap
					texenv = float3(0.0275, 0.0275, 0.0275);
				}
				else {
					texenv = env_radiance(ray.direction, constant_buffer.params.y, constant_buffer.params.x,
						mytexture_env, linear_sampler);
					#ifdef _ENV_SAMPLING
					if (bsdf_pdf > 0.0) {
						weight = mis_weight(bsdf_pdf, env_pdf(ray.direction, constant_buffer.params.y,
							constant_buffer.params.w, mytexture_env_cdf));
					}
					#endif
				}
				accum += clamp(throughput * texenv * weight, 0.0, 8.0);
				break;
			}

			device uint32_t *inda = (device uint32_t *)(indices);
			uint base_index = intersection.primitive_id * 3;

			#ifdef _MULTI
			base_index += intersection.user_instance_id;
			#endif

			uint3 indices_sample = uint3(
				inda[base_index],
				inda[base_index + 1],
				inda[base_index + 2]
			);

			device Vertex *verta = (device Vertex *)(vertices);
			Vertex a0 = verta[indices_sample[0]];
			Vertex a1 = verta[indices_sample[1]];
			Vertex a2 = verta[indices_sample[2]];

			float2 vertex_uvs[3] = {
				s16_to_f32(a0.tex),
				s16_to_f32(a1.tex),
				s16_to_f32(a2.tex)
			};
			float2 barycentrics = intersection.triangle_barycentric_coord;
			float2 tex_coord = hit_attribute2d(vertex_uvs, barycentrics) * constant_buffer.params.z;

			float3 hit = hit_world_position(ray, intersection);
			uint2 texel = texel_coord(tex_coord, tex_size);
			float4 texpaint0 = mytexture0.read(texel, 0);

			float2 zw0 = s16_to_f32(a0.poszw);
			float2 zw1 = s16_to_f32(a1.poszw);
			float2 zw2 = s16_to_f32(a2.poszw);
			float3 vertex_positions[3] = {
				float3(s16_to_f32(a0.posxy), zw0.x),
				float3(s16_to_f32(a1.posxy), zw1.x),
				float3(s16_to_f32(a2.posxy), zw2.x)
			};
			float3 vertex_normals[3] = {
				float3(s16_to_f32(a0.nor), zw0.y),
				float3(s16_to_f32(a1.nor), zw1.y),
				float3(s16_to_f32(a2.nor), zw2.y)
			};
			float3 n = normalize(hit_attribute(vertex_normals, barycentrics));

			float3 ng = cross(vertex_positions[1] - vertex_positions[0], vertex_positions[2] - vertex_positions[0]);
			ng = dot(ng, ng) > 1e-20 ? normalize(ng) : n;
			ng *= dot(ng, n) < 0.0 ? -1.0 : 1.0;

			float3 n_object = n;

			#ifdef _MULTI
			float4x3 o2w = intersection.object_to_world_transform;
			float3x3 obj_to_world = float3x3(o2w[0], o2w[1], o2w[2]);
			n = normalize(obj_to_world * n);
			ng = normalize(obj_to_world * ng);
			#endif

			bool back_face = dot(ng, ray.direction) > 0.0;
			if (back_face) {
				ng = -ng;
				n = -n;
			}

			#ifdef _INDIRECT_SKIP_NORMAL
			bool normal_map = i == 0;
			#else
			const bool normal_map = true;
			#endif

			float4 texpaint1 = float4(0.0);
			if (normal_map) {
				texpaint1 = mytexture1.read(texel, 0);
			}
			float3 texcolor = srgb_to_linear(texpaint0.rgb);

			#ifdef _TRANSLUCENCY
			if (!intersection.triangle_front_facing) {
				float3 absorption = pow(max(texcolor, float3(0.001)), float3(intersection.distance * texpaint0.a));
				throughput *= absorption;
			}
			#endif

			#ifdef _EMISSION
			if (int(texpaint1.a * 255.0f) % 3 == 1) { // matid
				accum += throughput * texcolor * 100.0f;
				break;
			}
			#endif

			float4 texpaint2 = mytexture2.read(texel, 0);

			float f = rand(tid.x, tid.y, sample_index, dim_base + DIM_SELECT, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank);

			float3 tangent = float3(0, 0, 0);
			float3 binormal = float3(0, 0, 0);

			#ifdef _TRANSLUCENCY
			if (f > texpaint0.a) {
				float roughness = texpaint2.g;
				float3 st = float3(0, 0, 0);
				float3 sb = float3(0, 0, 0);
				create_basis(ray.direction, st, sb);
				float3 scatter_dir = cos_weighted_direction(st, sb, ray.direction,
					rand(tid.x, tid.y, sample_index, dim_base + DIM_DIR, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank),
					rand(tid.x, tid.y, sample_index, dim_base + DIM_DIR + 1, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank));
				ray.direction = normalize(mix(ray.direction, scatter_dir, roughness * roughness * 0.5));
				ray.origin = offset_ray(hit, ng, ray.direction);
				continue;
			}

			f = rand(tid.x, tid.y, sample_index, dim_base + DIM_SELECT2, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank);
			#endif

			if (normal_map) {
				create_uv_basis(vertex_positions[0], vertex_positions[1], vertex_positions[2],
					vertex_uvs[0], vertex_uvs[1], vertex_uvs[2], n_object, tangent, binormal);

				#ifdef _MULTI
				tangent = obj_to_world * tangent;
				binormal = obj_to_world * binormal;
				tangent = normalize(tangent - n * dot(n, tangent));
				binormal = normalize(binormal - n * dot(n, binormal) - tangent * dot(tangent, binormal));
				#endif

				if (back_face) {
					binormal = -binormal;
				}

				texpaint1.rgb = normalize(texpaint1.rgb * 2.0 - 1.0);
				texpaint1.g = -texpaint1.g;
				n = normalize(float3x3(tangent, binormal, n) * texpaint1.rgb);

				if (dot(n, ng) < 1e-4) {
					n = normalize(n + ng * (1e-4 - dot(n, ng)));
				}
			}

			float3 wo = -ray.direction;
			float ndotv = dot(n, wo);
			if (ndotv < 1e-3) {
				if (i > 0) {
					break;
				}
				n = normalize(n + wo * (1e-3 - ndotv));
				float ndotg = dot(n, ng);
				if (ndotg < 1e-3) {
					n = normalize(n + ng * (1e-3 - ndotg));
				}
				ndotv = max(dot(n, wo), 1e-3);
			}

			create_basis(n, tangent, binormal);

			float3 albedo = surface_albedo(texcolor, texpaint2.b);
			float3 f0 = surface_specular(texcolor, texpaint2.b);
			float3 fresnel = f_schlick(f0, ndotv);
			float3 diffuse_weight = albedo * (1.0 - fresnel);

			float ls = luma(fresnel);
			float ld = luma(diffuse_weight);
			float specular_chance = clamp(ls / max(ls + ld, 1e-5), 0.05, 0.995);

			float alpha = max(texpaint2.g * texpaint2.g, 1e-3);

			#ifdef _ENV_SAMPLING
			if (constant_buffer.params.w != 0.0) {
				float pdf_light;
				float3 wl = sample_env(
					rand(tid.x, tid.y, sample_index, dim_base + DIM_LIGHT, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank),
					rand(tid.x, tid.y, sample_index, dim_base + DIM_LIGHT + 1, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank),
					constant_buffer.params.y, mytexture_env_cdf, pdf_light);
				if (pdf_light > 0.0 && dot(wl, ng) > 0.0) {
					float3 f_cos;
					float pdf_brdf;
					bsdf_eval(wl, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance, f_cos, pdf_brdf);
					if (max(max(f_cos.r, f_cos.g), f_cos.b) > 0.0 &&
						!occluded(offset_ray(hit, ng, wl), wl, scene)) {
						accum += clamp(throughput * f_cos *
							env_radiance(wl, constant_buffer.params.y, constant_buffer.params.x, mytexture_env, linear_sampler) *
							(mis_weight(pdf_light, pdf_brdf) / pdf_light), 0.0, 8.0);
					}
				}
			}
			#endif

			float u1 = rand(tid.x, tid.y, sample_index, dim_base + DIM_DIR, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank);
			float u2 = rand(tid.x, tid.y, sample_index, dim_base + DIM_DIR + 1, frame, scramble_cache, rank_cache, mytexture_sobol, mytexture_rank);

			if (f < specular_chance) {
				float alpha2 = alpha * alpha;
				float3 v_local = float3(dot(wo, tangent), dot(wo, binormal), ndotv);
				float3 h_local = sample_ggx_vndf(v_local, alpha, u1, u2);
				float3 l_local = reflect(-v_local, h_local);
				if (l_local.z <= 0.0) {
					break;
				}
				ray.direction = tangent * l_local.x + binormal * l_local.y + n * l_local.z;

				float lambda_v = smith_lambda(v_local.z, alpha2);
				float lambda_l = smith_lambda(l_local.z, alpha2);
				float g2_over_g1 = (1.0 + lambda_v) / (1.0 + lambda_v + lambda_l);
				throughput *= f_schlick(f0, max(dot(v_local, h_local), 0.0)) * (g2_over_g1 / specular_chance);
			}
			else {
				ray.direction = cos_weighted_direction(tangent, binormal, n, u1, u2);
				throughput *= diffuse_weight / (1.0 - specular_chance);
			}

			if (dot(ray.direction, ng) <= 0.0) {
				break;
			}
			if (max(max(throughput.r, throughput.g), throughput.b) <= 0.0) {
				break;
			}

			ray.origin = offset_ray(hit, ng, ray.direction);

			#ifdef _ENV_SAMPLING
			{
				float3 f_cos;
				float pdf_brdf;
				bsdf_eval(ray.direction, n, wo, ndotv, diffuse_weight, f0, alpha, specular_chance, f_cos, pdf_brdf);
				bsdf_pdf = pdf_brdf;
			}
			#endif

			#ifdef _SUBSURFACE
			if (int(texpaint1.a * 255.0f) % 3 == 2) {
				float d = min(1.0 / min(intersection.distance * 2.0, 1.0) / 10.0, 0.5);
				throughput += throughput * d;
				if (f < 0.5) {
					ray.origin += ray.direction * f * 0.001;
				}
			}
			#endif
		}
	}

	float3 color = render_target.read(tid).xyz;
	accum = accum / SAMPLES;

	float a = 1.0 / (constant_buffer.eye.w + 1);
	color = mix(color, accum, a);
	render_target.write(float4(color, 1.0f), tid);
}
