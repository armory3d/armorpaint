#ifdef _FULL
#define _EMISSION
#define _SUBSURFACE
#define _TRANSLUCENCY
// #define _TRANSPARENCY
#define _ROULETTE
// #define _FRESNEL
#endif
// #define _RENDER


#include "std/rand.hlsl"
#include "std/attrib.hlsl"
#include "std/math.hlsl"

struct Vertex {
	uint posxy;
	uint poszw;
	uint nor;
	uint tex;
};

struct RayGenConstantBuffer {
	float4 eye; // xyz, frame
	float4x4 inv_vp;
	float4 params; // envstr, envangle, uvscale, empty
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
SamplerState sampler_linear : register(s0);

static const int SAMPLES = 64;
#ifdef _TRANSLUCENCY
static const int DEPTH = 16;
#else
static const int DEPTH = 3; // Opaque hits
#endif
static uint thread_seed = 0;
#ifdef _TRANSPARENCY
static const int DEPTH_TRANSPARENT = 16; // Transparent hits
#endif
#ifdef _ROULETTE
static const int rr_start = 2;
static const float rr_probability = 0.5; // Map to albedo
#endif

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
	uint2 dim;
	render_target.GetDimensions(dim.x, dim.y);
	if (id.x >= dim.x || id.y >= dim.y) return;

	float3 accum = 0;
	for (int j = 0; j < SAMPLES; ++j) {
		float2 xy = id.xy + 0.5f;
		// AA
		xy.x += rand(id.x, id.y, j, thread_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank);
		thread_seed += 1;
		xy.y += rand(id.x, id.y, j, thread_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank);

		RayDesc ray;
		ray.TMin = 0.0001;
		ray.TMax = 100.0;
		generate_camera_ray(xy / float2(dim) * 2.0 - 1.0, ray.Origin, ray.Direction, constant_buffer.eye.xyz, constant_buffer.inv_vp);

		float4 payload = float4(1, 1, 1, j);

		#ifdef _TRANSPARENCY
		int transparent_hits = 0;
		#endif

		for (int i = 0; i < DEPTH; ++i) {
			#ifdef _ROULETTE
			float rr_factor = 1.0;
			if (i >= rr_start) {
				if (rand(id.x, id.y, j, thread_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank) <= rr_probability) break;
				rr_factor = 1.0 / (1.0 - rr_probability);
			}
			#endif

			// #ifdef _SUBSURFACE
			// RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q;
			// #else
			RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
			// #endif
			q.TraceRayInline(scene, RAY_FLAG_NONE, ~0, ray);
			q.Proceed();
			// while (q.Proceed()) {}

			if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
				#ifdef _EMISSION
				if (payload.a == -3.0) {
					accum += payload.rgb;
					break;
				}
				#endif

				float2 uv = equirect(ray.Direction, constant_buffer.params.y);
				float3 env = mytexture_env.SampleLevel(sampler_linear, uv, 0.0).rgb * abs(constant_buffer.params.x);
				if (i == 0 && constant_buffer.params.x < 0) env = 0.0275;
				accum += clamp(payload.rgb * env, 0.0, 8.0);
				break;
			}

			uint base = q.CommittedPrimitiveIndex() * 12;
			#ifdef _MULTI
			base += q.CommittedInstanceID(); // Offset to index buffer of this instance
			#endif
			uint3 idx = indices.Load3(base);

			BuiltInTriangleIntersectionAttributes attr;
			attr.barycentrics = q.CommittedTriangleBarycentrics();

			float2 uv[3] = {s16_to_f32(vertices[idx[0]].tex), s16_to_f32(vertices[idx[1]].tex), s16_to_f32(vertices[idx[2]].tex)};
			float2 tc = hit_attribute2d(uv, attr) * constant_buffer.params.z;

			uint2 sz;
			mytexture0.GetDimensions(sz.x, sz.y);
			float4 tex0 = mytexture0.Load(uint3((tc - uint2(tc)) * sz, 0));

			float3 hit = ray.Origin + ray.Direction * q.CommittedRayT();

			#ifdef _TRANSPARENCY
			if (tex0.a <= 0.01) {
				ray.Origin = hit + ray.Direction * 0.0001f;
				if (transparent_hits < DEPTH_TRANSPARENT) {
					payload.a = j;
					transparent_hits++;
					i--;
				}
				else {
					payload.a = -2;
				}
				continue;
			}
			#endif

			float3 vn[3] = {
				float3(s16_to_f32(vertices[idx[0]].nor), s16_to_f32(vertices[idx[0]].poszw).y),
				float3(s16_to_f32(vertices[idx[1]].nor), s16_to_f32(vertices[idx[1]].poszw).y),
				float3(s16_to_f32(vertices[idx[2]].nor), s16_to_f32(vertices[idx[2]].poszw).y)
			};
			float3 n = normalize(hit_attribute(vn, attr));

			#ifdef _MULTI
			float3x4 objToWorld = q.CommittedObjectToWorld3x4();
			n = normalize(mul(float3x3(objToWorld[0].xyz, objToWorld[1].xyz, objToWorld[2].xyz), n));
			#endif

			mytexture1.GetDimensions(sz.x, sz.y);
			float4 tex1 = mytexture1.Load(uint3((tc - uint2(tc)) * sz, 0));
			mytexture2.GetDimensions(sz.x, sz.y);
			float4 tex2 = mytexture2.Load(uint3((tc - uint2(tc)) * sz, 0));

			float3 color = pow(tex0.rgb, 2.2);

			#ifdef _TRANSLUCENCY
			if (!q.CommittedTriangleFrontFace()) {
				payload.rgb *= pow(max(color, 0.001), q.CommittedRayT() * tex0.a);
			}
			#endif

			tex1.rgb = normalize(tex1.rgb * 2.0 - 1.0);
			tex1.g = -tex1.g;
			n = mul(tex1.rgb, create_basis(n));

			uint bounce_seed = 0;

			float f = rand(id.x, id.y, payload.a, bounce_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank);
			bounce_seed += 1;

			#ifdef _TRANSLUCENCY
			bool scatter = false;
			if (f > tex0.a) {
				float3 sdir = cos_weighted_hemisphere_direction(id, ray.Direction, payload.a, bounce_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank);
				ray.Direction = normalize(lerp(ray.Direction, sdir, tex2.g * tex2.g * 0.5));
				ray.Origin = hit + ray.Direction * 0.0001f;
				scatter = true;
			}
			if (!scatter) {
				f = rand(id.x, id.y, payload.a, bounce_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank);
				bounce_seed += 1;
			#endif

				float3 diff = cos_weighted_hemisphere_direction(id, n, payload.a, bounce_seed, constant_buffer.eye.w, mytexture_sobol, mytexture_scramble, mytexture_rank);
				#ifdef _FRESNEL
				float spec_chance = fresnel(n, ray.Direction);
				#else
				const float spec_chance = 0.5;
				#endif

				if (f < spec_chance) {
					ray.Direction = lerp(reflect(ray.Direction, n), diff, tex2.g * tex2.g);
					payload.xyz *= surface_specular(color, tex2.b);
					#ifdef _FRESNEL
					payload.xyz /= spec_chance;
					#endif
				}
				else {
					ray.Direction = diff;
					payload.xyz *= surface_albedo(color, tex2.b);
					#ifdef _FRESNEL
					payload.xyz /= 1.0 - spec_chance;
					#endif
				}

				#ifdef _FRESNEL
				payload.xyz *= 0.5;
				#endif

				ray.Origin = hit + ray.Direction * 0.0001f;

			#ifdef _TRANSLUCENCY
			}
			#endif

			#ifdef _EMISSION
			if (int(tex1.a * 255.0f) % 3 == 1) { // matid
				payload.xyz *= 100.0f;
				payload.a = -3.0;
			}
			#endif

			#ifdef _SUBSURFACE
			if (int(tex1.a * 255.0f) % 3 == 2) {
				float d = min(1.0 / min(q.CommittedRayT() * 2.0, 1.0) / 10.0, 0.5);
				payload.xyz += payload.xyz * d;
				if (f < 0.5) ray.Origin += ray.Direction * f * 0.001;
			}
			#endif

			#ifdef _EMISSION
			if (payload.a == -3) {
				accum += payload.rgb;
				break;
			}
			#endif

			#ifdef _ROULETTE
			payload.rgb *= rr_factor;
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
