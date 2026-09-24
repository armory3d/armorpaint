
cbuffer constants {
	float4x4 SMVP;
	float4 envmap_data_world; // angle, tonemap_strength, empty, strength
};

sampler sampler_linear;

tex2d envmap;

const float PI = 3.1415926535;
const float PI2 = 6.283185307;

struct vert_in {
	float3 pos;
	float3 nor;
};

struct vert_out {
	float4 pos;
	float3 nor;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = constants.SMVP * float4(input.pos, 1.0);
	output.nor = input.nor;
	return output;
}

float2 envmap_equirect(float3 normal, float angle) {
	float phi = acos(normal.z);
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

float3 tonemap_filmic(float3 color) {
	// Based on Filmic Tonemapping Operators http://filmicgames.com/archives/75
	// float3 x = max(float3(0.0, 0.0, 0.0), color - 0.004);
	float3 x;
	x.x = max(0.0, color.x - 0.004);
	x.y = max(0.0, color.y - 0.004);
	x.z = max(0.0, color.z - 0.004);
	return (x * (x * 6.2 + 0.5)) / (x * (x * 6.2 + 1.7) + 0.06);
}

float4 frag(vert_out input) {
	float3 n = normalize(input.nor);
	float4 color;
	color.rgb = sample(envmap, sampler_linear, envmap_equirect(-n, constants.envmap_data_world.x)).rgb * constants.envmap_data_world.w;

	// Tonemap with gamma - non-lit modes
	color.rgb = lerp3(color.rgb, tonemap_filmic(color.rgb), constants.envmap_data_world.y);

	color.a = 0.0; // Mark as non-opaque
	return color;
}
