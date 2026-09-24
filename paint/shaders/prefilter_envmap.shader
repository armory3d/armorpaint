
cbuffer constants {
	float4 params; // level, samples, alpha, pass
};

sampler sampler_linear;

tex2d radiance;

tex2d noise;

const float PI = 3.14159265358979;
const float PI2 = 6.28318530718;

struct vert_in {
	float2 pos;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float rand(float2 co) {
	// return frac(sin(dot(co.xy, float2(12.9898, 78.233)) % 3.14) * 43758.5453);
	float2 uv = float2(frac(co.x * 12.34), frac(co.y * 12.34));
	return sample(noise, sampler_linear, uv).r;
}

float2 equirect(float3 normal) {
	float phi = acos(normal.z);
	float theta = atan2(-normal.y, normal.x) + PI;
	return float2(theta / PI2, phi / PI);
}

float3 reverse_equirect(float2 co) {
	float theta = co.x * PI2 - PI;
	float phi = co.y * PI;
	return float3(sin(phi) * cos(theta), -(sin(phi) * sin(theta)), cos(phi));
}

// float3 cos_weighted_hemisphere_direction(float3 n, float2 co, uint seed) {
float3 cos_weighted_hemisphere_direction(float3 n, float2 co, float seed) {
	float2 r = float2(rand(co * seed), rand(co * seed * 2.0));
	float3 uu = normalize(cross(n, float3(0.0, 1.0, 1.0)));
	float3 vv = cross(uu, n);
	float ra = sqrt(r.y);
	float rx = ra * cos(PI2 * r.x);
	float ry = ra * sin(PI2 * r.x);
	float rz = sqrt(1.0 - r.y);
	float3 rr = rx * uu + ry * vv + rz * n;
	return normalize(rr);
}

float4 frag(vert_out input) {
	float4 color = float4(0.0, 0.0, 0.0, constants.params.z);
	float3 n = reverse_equirect(input.tex);

	//for (int i = 0; i < int(constants.params.y); i += 1) {
	int i = 0;
	while (i < int(constants.params.y)) {
		float3 dir = normalize(lerp3(n, cos_weighted_hemisphere_direction(n, input.tex, float(i) + constants.params.w * constants.params.y), constants.params.x));
		float3 sampled = sample(radiance, sampler_linear, equirect(dir)).rgb;
		sampled = min3(sampled, float3(10.0, 10.0, 10.0));
		color.rgb = color.rgb + sampled;

		//
		i += 1;
		//
	}
	color.rgb = color.rgb / constants.params.y;
	return color;
}
