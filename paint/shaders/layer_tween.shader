cbuffer constants {
	float factor;
};

sampler sampler_linear;

tex2d tex0;

tex2d tex1;

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

float4 frag(vert_out input) {
	float4 col0 = sample_lod(tex0, sampler_linear, input.tex, 0.0);
	float4 col1 = sample_lod(tex1, sampler_linear, input.tex, 0.0);
	return lerp(col0, col1, constants.factor);
}
