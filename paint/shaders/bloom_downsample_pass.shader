
cbuffer constants {
	float2 screen_size_inv;
	int current_mip_level;
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

const float bloom_knee = 0.5;
const float bloom_threshold = 0.8;
const float epsilon = 0.000062;

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float2 clamp_coord(float2 coord, float2 texel_size) {
	float2 lo = texel_size * 0.5;
	float2 hi = float2(1.0, 1.0) - lo;
	return float2(clamp(coord.x, lo.x, hi.x), clamp(coord.y, lo.y, hi.y));
}

float3 downsample_dual_filter(float2 tex_coord, float2 texel_size) {
	float3 delta = float3(texel_size.xy, texel_size.x) * float3(0.5, 0.5, -0.5);

	float3 result;
	result  = sample_lod(tex, sampler_linear, clamp_coord(tex_coord,            texel_size), 0.0).rgb * 4.0;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord - delta.xy, texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord - delta.zy, texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + delta.zy, texel_size), 0.0).rgb;
	result += sample_lod(tex, sampler_linear, clamp_coord(tex_coord + delta.xy, texel_size), 0.0).rgb;

	return result * (1.0 / 8.0);
}

float4 frag(vert_out input) {
	float4 color;
	color.rgb = downsample_dual_filter(input.tex, constants.screen_size_inv);

	if (constants.current_mip_level == 0) {
		float brightness = max(color.r, max(color.g, color.b));

		float softening_curve = brightness - bloom_threshold + bloom_knee;
		softening_curve = clamp(softening_curve, 0.0, 2.0 * bloom_knee);
		softening_curve = softening_curve * softening_curve / (4.0 * bloom_knee + epsilon);

		float contribution_factor = max(softening_curve, brightness - bloom_threshold);

		contribution_factor /= max(epsilon, brightness);

		color.rgb = color.rgb * contribution_factor;
	}

	color.a = 1.0;
	return color;
}
