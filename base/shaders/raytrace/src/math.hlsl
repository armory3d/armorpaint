
#ifndef _MATH_HLSL_
#define _MATH_HLSL_

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

// A Low-Discrepancy Sampler that Distributes Monte Carlo Errors as a Blue Noise in Screen Space
// Eric Heitz, Laurent Belcour, Victor Ostromoukhov, David Coeurjolly and Jean-Claude Iehl
// https://eheitzresearch.wordpress.com/762-2/
float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame, Texture2D<float4> sobol, Texture2D<float4> scramble, Texture2D<float4> rank) {
	// wrap arguments
	pixel_i += frame * 9;
	pixel_j += frame * 11;
	pixel_i = pixel_i & 127;
	pixel_j = pixel_j & 127;
	sample_index = sample_index & 255;
	sample_dimension = sample_dimension & 255;

	// xor index based on optimized ranking
	int i = sample_dimension + (pixel_i + pixel_j * 128) * 8;
	int ranked_sample_index = sample_index ^ int(rank.Load(uint3(i % 128, uint(i / 128), 0)).r * 255);

	// fetch value in sequence
	i = sample_dimension + ranked_sample_index * 256;
	int value = int(sobol.Load(uint3(i % 256, uint(i / 256), 0)).r * 255);

	// If the dimension is optimized, xor sequence value based on optimized scrambling
	i = (sample_dimension % 8) + (pixel_i + pixel_j * 128) * 8;
	value = value ^ int(scramble.Load(uint3(i % 128, uint(i / 128), 0)).r * 255);

	// convert to float and return
	float v = (0.5f + value) / 256.0f;
	return v;
}

float3x3 create_basis(float3 normal) {
	float3 tangent = float3(0, 0, 0);
	float3 binormal = float3(0, 0, 0);
	float3 v = cross(normal, float3(0.0, 0.0, 1.0));
	if (dot(v, v) > 0.0001) {
		tangent = normalize(v);
	}
	else {
		v = cross(normal, float3(0.0, 1.0, 0.0));
		tangent = normalize(v);
	}
	binormal = cross(tangent, normal);
	return float3x3(tangent, binormal, normal);
}

void generate_camera_ray(float2 screen_pos, out float3 ray_origin, out float3 ray_dir, float3 eye, float4x4 inv_vp) {
	screen_pos.y = -screen_pos.y;
	float4 world = mul(float4(screen_pos, 0, 1), inv_vp);
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

float3 surface_albedo(const float3 base_color, const float metalness) {
	return lerp(base_color, float3(0.0, 0.0, 0.0), metalness);
}

float3 surface_specular(const float3 base_color, const float metalness) {
	return lerp(float3(0.04, 0.04, 0.04), base_color, metalness);
}

float fresnel(float3 normal, float3 incident) {
	return lerp(0.5, 1.0, pow(1.0 + dot(normal, incident), 5.0));
}

#endif
