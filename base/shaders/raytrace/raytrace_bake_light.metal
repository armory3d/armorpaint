
using namespace metal;
using namespace raytracing;

struct Vertex {
	uint posxy;
	uint poszw;
	uint nor;
	uint tex;
};

struct Instance {
	constant uint *vertex_buffer;
	constant uint *index_buffer;
	uint stride; // Vertex size in bytes
	uint geometry; // Index into the geometry textures
};

struct GeometryTextures {
	texture2d<float, access::read> texpaint0;
	texture2d<float, access::read> texpaint1;
	texture2d<float, access::read> texpaint2;
};

struct RayGenConstantBuffer {
	float4 v0; // frame, strength, radius, offset
	float4 v1;
	float4 v2;
	float4 v3;
	float4 v4;
};

struct RayPayload {
	float4 color;
	float3 ray_origin;
	float3 ray_dir;
};

constant int SAMPLES = 4;//64;

float2 equirect(float3 normal, float angle) {
	const float PI = 3.1415926535;
	const float PI2 = PI * 2.0;
	float phi = acos(normal.z);
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

uint table_byte(texture2d<float, access::read> tex, int i) {
	int t = (i & 131071) >> 2;
	float4 c = tex.read(uint2(uint(t & 127), uint(t >> 7)), 0);
	int ch = i & 3;
	return uint((ch == 0 ? c.r : (ch == 1 ? c.g : (ch == 2 ? c.b : c.a))) * 255);
}

float rand(int pixel_i, int pixel_j, int sample_ndex, int sample_dimension, int frame, texture2d<float, access::read> sobol, texture2d<float, access::read> scramble, texture2d<float, access::read> rank) {
	pixel_i += frame * 9;
	pixel_j += frame * 11;
	pixel_i = pixel_i & 127;
	pixel_j = pixel_j & 127;
	sample_ndex = sample_ndex & 255;
	sample_dimension = sample_dimension & 255;

	int i = (sample_dimension + (pixel_i + pixel_j * 128) * 8) & 131071;
	int ranked_sample_index = sample_ndex ^ int(table_byte(rank, i));

	int value = int(sobol.read(uint2(ranked_sample_index, sample_dimension), 0).r * 255);

	i = (sample_dimension % 8) + (pixel_i + pixel_j * 128) * 8;
	value = value ^ int(table_byte(scramble, i));

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

float2 s16_to_f32(uint val) {
	int a = (int)(val << 16) >> 16;
	int b = (int)(val & 0xffff0000) >> 16;
	return float2(a, b) / 32767.0f;
}

kernel void raytracingKernel(
	uint2 tid [[thread_position_in_grid]],
	constant RayGenConstantBuffer &constant_buffer [[buffer(0)]],
	texture2d<float, access::read_write> render_target [[texture(0)]],
	texture2d<float, access::read> mytexture0 [[texture(1)]],
	texture2d<float, access::read> mytexture1 [[texture(2)]],
	texture2d<float, access::read> mytexture2 [[texture(3)]],
	texture2d<float, access::read> mytexture_env [[texture(4)]],
	texture2d<float, access::read> mytexture_sobol [[texture(5)]],
	texture2d<float, access::read> mytexture_scramble [[texture(6)]],
	texture2d<float, access::read> mytexture_rank [[texture(7)]],
	instance_acceleration_structure scene [[buffer(1)]],
	constant Instance *instances [[buffer(2)]],
	constant GeometryTextures *geometry_textures [[buffer(3)]]
) {
	uint seed = 0;

	float2 xy = float2(tid) + float2(0.5f, 0.5f);
	float4 tex0 = mytexture0.read(uint2(xy), 0);
	if (tex0.a == 0.0) {
		render_target.write(float4(0.0f, 0.0f, 0.0f, 0.0f), tid);
		return;
	}
	float3 pos = tex0.rgb;
	float3 nor = mytexture1.read(uint2(xy), 0).rgb;

	RayPayload payload;

	ray ray;
	ray.min_distance = constant_buffer.v0.w * 0.01;
	ray.max_distance = constant_buffer.v0.z * 10.0;
	ray.origin = pos;
	float3 accum = float3(0, 0, 0);

	for (int i = 0; i < SAMPLES; ++i) {
		ray.direction = cos_weighted_hemisphere_direction(tid, nor, i, seed, constant_buffer.v0.x, mytexture_sobol, mytexture_scramble, mytexture_rank);
		seed += 1;

		intersector<triangle_data, instancing> in;
		in.assume_geometry_type(geometry_type::triangle);
		in.force_opacity(forced_opacity::opaque);
		in.accept_any_intersection(false);

		typename intersector<triangle_data, instancing>::result_type intersection;
		intersection = in.intersect(ray, scene);
		if (intersection.type == intersection_type::none) {
			float2 tex_coord = equirect(ray.direction, constant_buffer.v1.z);
			uint2 size = uint2(mytexture_env.get_width(), mytexture_env.get_height());
			float3 texenv = mytexture_env.read(uint2(tex_coord * float2(size)), 0).rgb * constant_buffer.v1.x;
			payload.color = float4(texenv.rgb, -1);
		}
		else {
			constant Instance &inst = instances[intersection.user_instance_id];
			uint3 indices_sample = uint3(
				inst.index_buffer[intersection.primitive_id * 3],
				inst.index_buffer[intersection.primitive_id * 3 + 1],
				inst.index_buffer[intersection.primitive_id * 3 + 2]
			);

			// Texture coordinates are the last 4 bytes of the base vertex layout
			float2 barycentrics = intersection.triangle_barycentric_coord;
			float2 vertex_uvs[3] = {
				s16_to_f32(inst.vertex_buffer[(indices_sample[0] * inst.stride + 12) / 4]),
				s16_to_f32(inst.vertex_buffer[(indices_sample[1] * inst.stride + 12) / 4]),
				s16_to_f32(inst.vertex_buffer[(indices_sample[2] * inst.stride + 12) / 4])
			};
			float2 tex_coord = hit_attribute2d(vertex_uvs, barycentrics);

			texture2d<float, access::read> hit_texture2 = geometry_textures[inst.geometry].texpaint2;
			uint2 size = uint2(hit_texture2.get_width(), hit_texture2.get_height());
			float3 texpaint2 = pow(hit_texture2.read(uint2(tex_coord * float2(size)), 0).rgb, 2.2); // layer base
			payload.color.rgb = texpaint2.rgb;
		}

		accum += payload.color.rgb;
	}

	accum /= SAMPLES;

	float3 texpaint2 = mytexture2.read(uint2(xy), 0).rgb; // layer base
	accum *= texpaint2;

	float3 color = render_target.read(tid).xyz;
	if (constant_buffer.v0.x == 0) {
		color = accum.xyz;
	}
	else {
		float a = 1.0 / constant_buffer.v0.x;
		float b = 1.0 - a;
		color = color * b + accum.xyz * a;
	}
	render_target.write(float4(color.xyz, 1.0f), tid);
}
