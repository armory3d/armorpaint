
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

float rand_scrambled(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame, uint scramble_value, Texture2D<float4> sobol, Texture2D<float4> rank) {
	return rand_indexed(sample_index, sample_dimension, rank_value_at(pixel_i, pixel_j, sample_dimension, frame, rank), scramble_value, sobol);
}

float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame, Texture2D<float4> sobol, Texture2D<float4> scramble, Texture2D<float4> rank) {
	int i = ((sample_dimension & 255) % 8) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	uint scramble_value = table_byte(scramble, i);
	return rand_scrambled(pixel_i, pixel_j, sample_index, sample_dimension, frame, scramble_value, sobol, rank);
}

float3x3 create_basis(float3 normal) {
	float s = normal.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + normal.z);
	float b = normal.x * normal.y * a;
	float3 tangent = float3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
	float3 binormal = float3(b, s + normal.y * normal.y * a, -normal.y);
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

float3 surface_albedo(const float3 base_color, const float metalness) {
	return lerp(base_color, float3(0.0, 0.0, 0.0), metalness);
}

float3 surface_specular(const float3 base_color, const float metalness) {
	return lerp(float3(0.04, 0.04, 0.04), base_color, metalness);
}

float luma(float3 c) {
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 unpack_position(uint posxy, uint posz_norz) {
	return float3(s16_to_f32(posxy), s16_to_f32(posz_norz).x);
}

float3 srgb_to_linear(float3 c) {
	return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

float3 f_schlick(float3 f0, float u) {
	float m = saturate(1.0 - u);
	float m2 = m * m;
	return f0 + (1.0 - f0) * (m2 * m2 * m);
}

float smith_lambda(float cos_theta, float alpha2) {
	float c2 = cos_theta * cos_theta;
	return 0.5 * (sqrt(1.0 + alpha2 * (1.0 - c2) / max(c2, 1e-7)) - 1.0);
}

void create_uv_basis(float3 p0, float3 p1, float3 p2, float2 uv0, float2 uv1, float2 uv2,
	float3 n, out float3 tangent, out float3 binormal) {
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
	float3x3 basis = create_basis(n);
	tangent = basis[0];
	binormal = basis[1];
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

float3 offset_ray(float3 p, float3 ng, float3 dir) {
	return p + ng * (dot(dir, ng) < 0.0 ? -1e-4 : 1e-4);
}

#endif
