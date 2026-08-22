#include "math.hlsl"

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
