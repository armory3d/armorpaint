#define _FULL
#ifdef _FULL
#define _EMISSION
#define _SUBSURFACE
#define _TRANSLUCENCY
#define _ROULETTE
// #define _TRANSPARENCY
// #define _FRESNEL
#endif
// #define _RENDER

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
	float4 params; // envstr, envangle, uvscale
};

struct RayPayload {
	float4 color; // rgb, frame
	float3 ray_origin;
	float3 ray_dir;
};

constant int SAMPLES = 8;
#ifdef _TRANSLUCENCY
constant int DEPTH = 16;
#else
constant int DEPTH = 3; // Opaque hits
#endif
#ifdef _TRANSPARENCY
constant int DEPTH_TRANSPARENT = 16; // Transparent hits
#endif
#ifdef _ROULETTE
constant int rr_start = 2;
constant float rr_probability = 0.5; // Map to albedo
#endif

void generate_camera_ray(float2 screen_pos, thread float3 & ray_origin, thread float3 & ray_dir, float3 eye, float4x4 inv_vp) {
	screen_pos.y = -screen_pos.y;
	float4 world = inv_vp * float4(screen_pos, 0, 1);
	world.xyz /= world.w;
	ray_origin = eye;
	ray_dir = normalize(world.xyz - ray_origin);
}

float2 equirect(float3 normal, float angle) {
	const float PI = 3.1415926535;
	const float PI2 = PI * 2.0;
	float phi = acos(normal.z);
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame, texture2d<float, access::read> sobol, texture2d<float, access::read> scramble, texture2d<float, access::read> rank) {
	pixel_i += frame * 9;
	pixel_j += frame * 11;
	pixel_i = pixel_i & 127;
	pixel_j = pixel_j & 127;
	sample_index = sample_index & 255;
	sample_dimension = sample_dimension & 255;

	int i = sample_dimension + (pixel_i + pixel_j * 128) * 8;
	int ranked_sample_index = sample_index ^ int(rank.read(uint2(i % 128, uint(i / 128)), 0).r * 255);

	i = sample_dimension + ranked_sample_index * 256;
	int value = int(sobol.read(uint2(i % 256, uint(i / 256)), 0).r * 255);

	i = (sample_dimension % 8) + (pixel_i + pixel_j * 128) * 8;
	value = value ^ int(scramble.read(uint2(i % 128, uint(i / 128)), 0).r * 255);

	float v = (0.5f + value) / 256.0f;
	return v;
}

float3 cos_weighted_hemisphere_direction(uint2 tid, float3 n, uint sample, uint seed, int frame, texture2d<float, access::read> sobol, texture2d<float, access::read> scramble, texture2d<float, access::read> rank) {
	const float PI = 3.1415926535;
	const float PI2 = PI * 2.0;
	float f0 = rand(tid.x, tid.y, sample, seed, frame, sobol, scramble, rank);
	float f1 = rand(tid.x, tid.y, sample, seed + 1, frame, sobol, scramble, rank);
	float z = f0 * 2.0f - 1.0f;
	float a = f1 * PI2;
	float r = sqrt(1.0f - z * z);
	float x = r * cos(a);
	float y = r * sin(a);
	return normalize(n + float3(x, y, z));
}

float2 s16_to_f32(uint val) {
	int a = (int)(val << 16) >> 16;
	int b = (int)(val & 0xffff0000) >> 16;
	return float2(a, b) / 32767.0f;
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
	float3 v = cross(normal, float3(0.0, 0.0, 1.0));
	if (dot(v, v) > 0.0001) {
		tangent = normalize(v);
	}
	else {
		v = cross(normal, float3(0.0, 1.0, 0.0));
		tangent = normalize(v);
	}
	binormal = cross(tangent, normal);
}

float3 surface_albedo(const float3 base_color, const float metalness) {
	return mix(base_color, float3(0.0, 0.0, 0.0), metalness);
}

float3 surface_specular(const float3 base_color, const float metalness) {
	return mix(float3(0.04, 0.04, 0.04), base_color, metalness);
}

float fresnel(float3 normal, float3 incident) {
	return mix(0.5, 1.0, pow(1.0 + dot(normal, incident), 5.0));
}

float4 read_texel(texture2d<float, access::read> tex, float2 tex_coord) {
	uint2 size = uint2(tex.get_width(), tex.get_height());
	return tex.read(uint2(fract(tex_coord) * float2(size)), 0);
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
	uint thread_seed = 0;
	float3 accum = float3(0, 0, 0);

	for (int j = 0; j < SAMPLES; ++j) {
		int sample_index = frame * SAMPLES + j;

		// AA
		float2 xy = float2(tid) + float2(0.5f, 0.5f);
		xy.x += rand(tid.x, tid.y, sample_index, thread_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);
		thread_seed += 1;
		xy.y += rand(tid.x, tid.y, sample_index, thread_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);

		float2 screen_pos = xy / float2(dim) * 2.0 - 1.0;
		ray ray;
		ray.min_distance = 0.0001;
		ray.max_distance = 100.0;
		generate_camera_ray(screen_pos, ray.origin, ray.direction, constant_buffer.eye.xyz, constant_buffer.inv_vp);

		RayPayload payload;
		payload.color = float4(1, 1, 1, sample_index);

		#ifdef _TRANSPARENCY
		int transparent_hits = 0;
		#endif

		for (int i = 0; i < DEPTH; ++i) {

			#ifdef _ROULETTE
			float rr_factor = 1.0;
			if (i >= rr_start) {
				float f = rand(tid.x, tid.y, sample_index, thread_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);
				if (f <= rr_probability) {
					break;
				}
				rr_factor = 1.0 / (1.0 - rr_probability);
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
				#ifdef _EMISSION
				if (payload.color.a == -3.0) {
					accum += payload.color.rgb;
					break;
				}
				#endif

				if (i == 0 && constant_buffer.params.x < 0.0) { // No envmap
					payload.color.rgb = float3(0.0275, 0.0275, 0.0275);
				}
				else {
					float2 tex_coord = equirect(ray.direction, constant_buffer.params.y);
					float3 texenv = mytexture_env.sample(linear_sampler, tex_coord, level(0)).rgb * abs(constant_buffer.params.x);
					payload.color.rgb *= texenv;
				}

				accum += clamp(payload.color.rgb, 0.0, 8.0);
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
			float2 vertex_uvs[3] = {
				s16_to_f32(verta[indices_sample[0]].tex),
				s16_to_f32(verta[indices_sample[1]].tex),
				s16_to_f32(verta[indices_sample[2]].tex)
			};
			float2 barycentrics = intersection.triangle_barycentric_coord;
			float2 tex_coord = hit_attribute2d(vertex_uvs, barycentrics) * constant_buffer.params.z;

			float3 hit = hit_world_position(ray, intersection);
			float4 texpaint0 = read_texel(mytexture0, tex_coord);

			#ifdef _TRANSPARENCY
			if (texpaint0.a <= 0.01) {
				ray.origin = hit + ray.direction * 0.0001f;
				if (transparent_hits < DEPTH_TRANSPARENT) {
					payload.color.a = sample_index;
					transparent_hits++;
					i--;
				}
				else {
					payload.color.a = -2;
				}
				continue;
			}
			#endif

			float3 vertex_normals[3] = {
				float3(s16_to_f32(verta[indices_sample[0]].nor), s16_to_f32(verta[indices_sample[0]].poszw).y),
				float3(s16_to_f32(verta[indices_sample[1]].nor), s16_to_f32(verta[indices_sample[1]].poszw).y),
				float3(s16_to_f32(verta[indices_sample[2]].nor), s16_to_f32(verta[indices_sample[2]].poszw).y)
			};
			float3 n = normalize(hit_attribute(vertex_normals, barycentrics));

			#ifdef _MULTI
			float4x3 obj_to_world = intersection.object_to_world_transform;
			n = normalize(float3x3(obj_to_world[0], obj_to_world[1], obj_to_world[2]) * n);
			#endif

			float4 texpaint1 = read_texel(mytexture1, tex_coord);
			float4 texpaint2 = read_texel(mytexture2, tex_coord);
			float3 texcolor = pow(texpaint0.rgb, float3(2.2, 2.2, 2.2));

			#ifdef _TRANSLUCENCY
			if (!intersection.triangle_front_facing) {
				float3 absorption = pow(max(texcolor, float3(0.001)), float3(intersection.distance * texpaint0.a));
				payload.color.rgb *= absorption;
			}
			#endif

			float3 tangent = float3(0, 0, 0);
			float3 binormal = float3(0, 0, 0);
			create_basis(n, tangent, binormal);

			texpaint1.rgb = normalize(texpaint1.rgb * 2.0 - 1.0);
			texpaint1.g = -texpaint1.g;
			n = float3x3(tangent, binormal, n) * texpaint1.rgb;

			uint bounce_seed = 0;

			float f = rand(tid.x, tid.y, payload.color.a, bounce_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);
			bounce_seed += 1;

			bool scatter = false;

			#ifdef _TRANSLUCENCY
			if (f > texpaint0.a) {
				float roughness = texpaint2.g;
				float3 scatter_dir = cos_weighted_hemisphere_direction(tid, ray.direction, payload.color.a, bounce_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);
				payload.ray_dir = normalize(mix(ray.direction, scatter_dir, roughness * roughness * 0.5));
				payload.ray_origin = hit + payload.ray_dir * 0.0001f;
				scatter = true;
			}
			#endif

			if (!scatter) {
				#ifdef _TRANSLUCENCY
				f = rand(tid.x, tid.y, payload.color.a, bounce_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);
				bounce_seed += 1;
				#endif

				float3 diffuse_dir = cos_weighted_hemisphere_direction(tid, n, payload.color.a, bounce_seed, frame, mytexture_sobol, mytexture_scramble, mytexture_rank);

				#ifdef _FRESNEL
				float specular_chance = fresnel(n, ray.direction);
				#else
				const float specular_chance = 0.5;
				#endif

				if (f < specular_chance) {
					float3 specular_dir = reflect(ray.direction, n);
					payload.ray_dir = mix(specular_dir, diffuse_dir, texpaint2.g * texpaint2.g);
					float3 specular = surface_specular(texcolor, texpaint2.b);
					payload.color.xyz *= specular;

					#ifdef _FRESNEL
					payload.color.xyz /= specular_chance;
					#endif
				}
				else {
					payload.ray_dir = diffuse_dir;
					payload.color.xyz *= surface_albedo(texcolor, texpaint2.b);
					#ifdef _FRESNEL
					payload.color.xyz /= 1.0 - specular_chance;
					#endif
				}

				#ifdef _FRESNEL
				payload.color.xyz *= 0.5;
				#endif

				payload.ray_origin = hit + payload.ray_dir * 0.0001f;

				#ifdef _EMISSION
				if (int(texpaint1.a * 255.0f) % 3 == 1) { // matid
					payload.color.xyz *= 100.0f;
					payload.color.a = -3.0;
				}
				#endif

				#ifdef _SUBSURFACE
				if (int(texpaint1.a * 255.0f) % 3 == 2) {
					float d = min(1.0 / min(intersection.distance * 2.0, 1.0) / 10.0, 0.5);
					payload.color.xyz += payload.color.xyz * d;
					if (f < 0.5) {
						payload.ray_origin += ray.direction * f * 0.001;
					}
				}
				#endif
			}

			#ifdef _EMISSION
			if (payload.color.a == -3.0) {
				accum += payload.color.rgb;
				break;
			}
			#endif

			ray.origin = payload.ray_origin;
			ray.direction = payload.ray_dir;

			#ifdef _ROULETTE
			payload.color.rgb *= rr_factor;
			#endif
		}
	}

	float3 color = render_target.read(tid).xyz;
	accum = accum / SAMPLES;

	#ifdef _RENDER
	float a = 1.0 / (constant_buffer.eye.w + 1);
	float b = 1.0 - a;
	color = color * b + accum * a;
	render_target.write(float4(color, 1.0f), tid);
	#else
	if (constant_buffer.eye.w == 0) {
		color = accum;
	}
	render_target.write(float4(mix(color, accum, 1.0 / 4.0), 1.0f), tid);
	#endif
}
