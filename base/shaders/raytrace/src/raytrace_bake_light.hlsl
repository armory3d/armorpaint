struct Vertex {
	uint posxy;
	uint poszw;
	uint nor;
	uint tex;
};

struct RayGenConstantBuffer {
	float4 v0; // frame, strength, radius, offset
	float4 v1; // envstr, upaxis, envangle
	float4 v2;
	float4 v3;
	float4 v4;
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

static const int SAMPLES = 64;
static uint seed = 0;

float2 s16_to_f32(uint val) {
	int a = (int)(val << 16) >> 16;
	int b = (int)(val & 0xffff0000) >> 16;
	return float2(a, b) / 32767.0f;
}

float3 hit_attribute(float3 vertex_attribute[3], BuiltInTriangleIntersectionAttributes attr) {
	return vertex_attribute[0] +
		attr.barycentrics.x * (vertex_attribute[1] - vertex_attribute[0]) +
		attr.barycentrics.y * (vertex_attribute[2] - vertex_attribute[0]);
}

float2 hit_attribute2d(float2 vertex_attribute[3], BuiltInTriangleIntersectionAttributes attr) {
	return vertex_attribute[0] +
		attr.barycentrics.x * (vertex_attribute[1] - vertex_attribute[0]) +
		attr.barycentrics.y * (vertex_attribute[2] - vertex_attribute[0]);
}

uint table_byte(Texture2D<float4> tex, int i) {
	int t = (i & 131071) >> 2;
	float4 c = tex.Load(uint3(uint(t & 127), uint(t >> 7), 0));
	int ch = i & 3;
	return uint((ch == 0 ? c.r : (ch == 1 ? c.g : (ch == 2 ? c.b : c.a))) * 255);
}

uint rank_value_at(int pixel_i, int pixel_j, int sample_dimension, int frame, Texture2D<float4> rank) {
	int i = (sample_dimension & 255) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	return table_byte(rank, i);
}

float rand_indexed(int sample_index, int sample_dimension, uint rank_value, uint scramble_value, Texture2D<float4> sobol) {
	sample_index = sample_index & 255;
	sample_dimension = sample_dimension & 255;
	int ranked_sample_index = sample_index ^ int(rank_value);
	int value = int(sobol.Load(uint3(ranked_sample_index, sample_dimension, 0)).r * 255);
	value = value ^ int(scramble_value);
	float v = (0.5f + value) / 256.0f;
	return v;
}

float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame, Texture2D<float4> sobol, Texture2D<float4> scramble, Texture2D<float4> rank) {
	int i = ((sample_dimension & 255) % 8) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	uint scramble_value = table_byte(scramble, i);
	uint rank_value = rank_value_at(pixel_i, pixel_j, sample_dimension, frame, rank);
	return rand_indexed(sample_index, sample_dimension, rank_value, scramble_value, sobol);
}

float2 equirect(float3 normal, float angle) {
	const float PI = 3.1415926535;
	const float PI2 = PI * 2.0;
	float phi = acos(clamp(normal.z, -1.0, 1.0));
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

float3 cos_weighted_hemisphere_direction(uint3 id, float3 n, uint sample, uint seed, int frame, Texture2D<float4> sobol, Texture2D<float4> scramble, Texture2D<float4> rank) {
	const float PI = 3.1415926535;
	const float PI2 = PI * 2.0;
	float f0 = rand(id.x, id.y, sample, seed, frame, sobol, scramble, rank);
	float f1 = rand(id.x, id.y, sample, seed + 1, frame, sobol, scramble, rank);
	float z = f0 * 2.0f - 1.0f;
	float a = f1 * PI2;
	float r = sqrt(1.0f - z * z);
	float x = r * cos(a);
	float y = r * sin(a);
	return normalize(n + float3(x, y, z));
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
	uint2 dim;
	render_target.GetDimensions(dim.x, dim.y);
	if (id.x >= dim.x || id.y >= dim.y) return;

	float2 xy = id.xy + 0.5f;
	float4 tex0 = mytexture0.Load(uint3(xy, 0));
	if (tex0.a == 0.0) {
		render_target[id.xy] = half4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	float3 pos = tex0.rgb;
	float3 nor = mytexture1.Load(uint3(xy, 0)).rgb;

	RayDesc ray;
	ray.TMin = constant_buffer.v0.w * 0.01;
	ray.TMax = constant_buffer.v0.z * 10.0;
	ray.Origin = pos;

	float3 accum = 0;

	for (int i = 0; i < SAMPLES; ++i) {
		ray.Direction = cos_weighted_hemisphere_direction(id, nor, i, seed, constant_buffer.v0.x, mytexture_sobol, mytexture_scramble, mytexture_rank);
		seed += 1;

		RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
		q.TraceRayInline(scene, RAY_FLAG_NONE, ~0, ray);
		q.Proceed();

		float3 sample_color;

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
			uint base_index = q.CommittedPrimitiveIndex() * 12;
			uint3 indices_sample = indices.Load3(base_index);

			BuiltInTriangleIntersectionAttributes attr;
			attr.barycentrics = q.CommittedTriangleBarycentrics();

			float3 vertex_normals[3] = {
				float3(s16_to_f32(vertices[indices_sample[0]].nor), s16_to_f32(vertices[indices_sample[0]].poszw).y),
				float3(s16_to_f32(vertices[indices_sample[1]].nor), s16_to_f32(vertices[indices_sample[1]].poszw).y),
				float3(s16_to_f32(vertices[indices_sample[2]].nor), s16_to_f32(vertices[indices_sample[2]].poszw).y)
			};
			float3 n = normalize(hit_attribute(vertex_normals, attr));

			float2 vertex_uvs[3] = {
				s16_to_f32(vertices[indices_sample[0]].tex),
				s16_to_f32(vertices[indices_sample[1]].tex),
				s16_to_f32(vertices[indices_sample[2]].tex)
			};
			float2 tex_coord = hit_attribute2d(vertex_uvs, attr);

			uint2 size;
			mytexture2.GetDimensions(size.x, size.y);
			sample_color = pow(mytexture2.Load(uint3(tex_coord * size, 0)).rgb, 2.2);
		}
		else {
			float2 tex_coord = equirect(ray.Direction, constant_buffer.v1.z);
			uint2 size;
			mytexture_env.GetDimensions(size.x, size.y);
			sample_color = mytexture_env.Load(uint3(tex_coord * size, 0)).rgb * constant_buffer.v1.x;
		}

		accum += sample_color;
	}

	accum /= SAMPLES;

	float3 texpaint2 = mytexture2.Load(uint3(xy, 0)).rgb; // layer base
	accum *= texpaint2;

	float3 color = render_target[id.xy].xyz;
	if (constant_buffer.v0.x == 0) {
		color = accum;
	}
	else {
		float a = 1.0 / constant_buffer.v0.x;
		color = lerp(color, accum, a);
	}

	render_target[id.xy] = half4(color, 1.0f);
}
