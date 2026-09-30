
cbuffer constants {
	float2 screen_size_inv;
	int current_mip_level;
	float sample_scale;
	float bloom_strength;
};

sampler sampler_linear;

tex2d tex;

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

float2 clamp_coord(float2 coord, float2 texel_size) {
	float2 lo = texel_size * 1.5;
	float2 hi = float2(1.0, 1.0) - lo;
	return float2(clamp(coord.x, lo.x, hi.x), clamp(coord.y, lo.y, hi.y));
}

float3 upsample_dual_filter(float2 tex_coord, float2 texel_size) {
	float2 delta = texel_size * constants.sample_scale;

	float3 result;
	result  = sample_lod(tex, sampler_linear, clamp_coord(tex_coord + float2(-delta.x * 2.0, 0.0), texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + float2(-delta.x, delta.y),   texel_size), 0.0).rgb * 2.0;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + float2(0.0, delta.y * 2.0),  texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + delta,                       texel_size), 0.0).rgb * 2.0;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + float2(delta.x * 2.0, 0.0),  texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + float2(delta.x, -delta.y),   texel_size), 0.0).rgb * 2.0;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + float2(0.0, -delta.y * 2.0), texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord - delta,                       texel_size), 0.0).rgb * 2.0;

	return result * (1.0 / 12.0);
}

float4 frag(vert_out input) {
	float4 color;
	color.rgb = upsample_dual_filter(input.tex, constants.screen_size_inv);

	if (constants.current_mip_level == 0) {
		color.rgb = color.rgb * float3(constants.bloom_strength, constants.bloom_strength, constants.bloom_strength);
	}

	color.a = 1.0;
	return color;
}
