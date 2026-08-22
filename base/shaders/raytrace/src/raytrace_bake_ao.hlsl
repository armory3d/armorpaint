#include "math.hlsl"

struct Vertex {
	uint posxy;
	uint poszw;
	uint nor;
	uint tex;
};

struct RayGenConstantBuffer {
	float4 v0; // frame, strength, radius, offset
	float4 v1;
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

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
			accum += 0;
		}
		else {
			accum += 1;
		}
	}

	accum /= SAMPLES;

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
