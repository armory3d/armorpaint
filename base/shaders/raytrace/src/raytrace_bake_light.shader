
cbuffer constant_buffer {
	float4 v0; // frame, strength, radius, offset
	float4 v1; // envstr, upaxis, envangle
	float4 v2;
	float4 v3;
	float4 v4;
};

bvh  scene;
tex2d render_target;
tex2d mytexture0;
tex2d mytexture1;
tex2d mytexture2;
tex2d mytexture_env;
tex2d mytexture_sobol;
tex2d mytexture_scramble;
tex2d mytexture_rank;

#ifdef _METAL
const int SAMPLES = 4;
#else
const int SAMPLES = 64;
#endif

const float PI  = 3.1415926535;
const float PI2 = 6.283185307;

float2 s16_to_f32(uint val) {
	int a = int(val << 16) >> 16;
	int b = int(val) >> 16;
	return float2(float(a), float(b)) / 32767.0;
}

float2 hit_attribute2d(float2 a0, float2 a1, float2 a2, float2 barycentrics) {
	return a0 + barycentrics.x * (a1 - a0) + barycentrics.y * (a2 - a0);
}

float2 equirect(float3 normal, float angle) {
	float phi   = acos(clamp(normal.z, -1.0, 1.0));
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

uint2 table_texel(int i) {
	int t = (i & 131071) >> 2;
	return uint2(uint(t & 127), uint(t >> 7));
}

uint table_channel(float4 c, int i) {
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

float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame) {
	int    i        = ((sample_dimension & 255) % 8) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	float4 scramble = mytexture_scramble[table_texel(i)];
	uint   scramble_value = table_channel(scramble, i);

	int    r    = (sample_dimension & 255) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	float4 rank = mytexture_rank[table_texel(r)];
	uint   rank_value = table_channel(rank, r);

	sample_index            = sample_index & 255;
	sample_dimension        = sample_dimension & 255;
	int    ranked_sample_index = sample_index ^ int(rank_value);
	float4 sobol           = mytexture_sobol[uint2(uint(ranked_sample_index), uint(sample_dimension))];
	int    value           = int(sobol.r * 255.0);
	value                  = value ^ int(scramble_value);
	return (0.5 + float(value)) / 256.0;
}

float3 cos_weighted_hemisphere_direction(uint3 id, float3 n, int sample, int seed, int frame) {
	float f0 = rand(int(id.x), int(id.y), sample, seed, frame);
	float f1 = rand(int(id.x), int(id.y), sample, seed + 1, frame);
	float z  = f0 * 2.0 - 1.0;
	float a  = f1 * PI2;
	float r  = sqrt(1.0 - z * z);
	float x  = r * cos(a);
	float y  = r * sin(a);
	return normalize(n + float3(x, y, z));
}

#[compute, threads(8, 8, 1)]
void raytrace() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(render_target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	float4 tex0 = mytexture0[id.xy];
	if (tex0.a == 0.0) {
		render_target[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	float3 pos  = tex0.rgb;
	float4 tex1 = mytexture1[id.xy];
	float3 nor  = tex1.rgb;

	ray r;
	r.min    = constant_buffer.v0.w * 0.01;
	r.max    = constant_buffer.v0.z * 10.0;
	r.origin = pos;

	float3 accum = float3(0.0, 0.0, 0.0);
	int    seed  = 0;

	for (int i = 0; i < SAMPLES; i += 1) {
		r.direction = cos_weighted_hemisphere_direction(id, nor, i, seed, int(constant_buffer.v0.x));
		seed += 1;

		ray_query q;
		ray_query_trace(q, scene, r);

		if (ray_query_hit(q)) {
			// Texture coordinates are the last 4 bytes of the base vertex layout
			uint4  a0        = ray_query_vertex(q, 0);
			uint4  a1        = ray_query_vertex(q, 1);
			uint4  a2        = ray_query_vertex(q, 2);
			float2 tex_coord = hit_attribute2d(s16_to_f32(a0.w), s16_to_f32(a1.w), s16_to_f32(a2.w), ray_query_barycentrics(q));

			uint   g         = ray_query_geometry(q);
			uint2  size      = geometry_texture2_size(g);
			float4 texpaint2 = geometry_texture2(g, uint2(tex_coord * float2(size))); // Base color
			accum += pow(texpaint2.rgb, 2.2);
		}
		else {
			float2 tex_coord = equirect(r.direction, constant_buffer.v1.z);
			uint2  size      = texture_size(mytexture_env);
			float4 texenv    = mytexture_env[uint2(tex_coord * float2(size))];
			accum += texenv.rgb * constant_buffer.v1.x;
		}
	}

	accum = accum / float(SAMPLES);

	float4 texpaint2 = mytexture2[id.xy]; // Layer base
	accum *= texpaint2.rgb;

	float4 target = render_target[id.xy];
	float3 color  = target.xyz;
	if (constant_buffer.v0.x == 0.0) {
		color = accum;
	}
	else {
		float a = 1.0 / constant_buffer.v0.x;
		color   = lerp(color, accum, a);
	}

	render_target[id.xy] = float4(color, 1.0);
}
