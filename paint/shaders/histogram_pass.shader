
cbuffer constants {
	float4 empty;
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

const float auto_exposure_speed = 1.0;

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float4 frag(vert_out input) {
	float4 color;
	color.a = 0.01 * auto_exposure_speed;
	color.rgb = sample_lod(tex, sampler_linear, float2(0.5, 0.5), 0.0).rgb +
				sample_lod(tex, sampler_linear, float2(0.2, 0.2), 0.0).rgb +
				sample_lod(tex, sampler_linear, float2(0.8, 0.2), 0.0).rgb +
				sample_lod(tex, sampler_linear, float2(0.2, 0.8), 0.0).rgb +
				sample_lod(tex, sampler_linear, float2(0.8, 0.8), 0.0).rgb;
	color.rgb = color.rgb / 5.0;
	return color;
}
