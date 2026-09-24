
cbuffer constants {
	float mask_opacity;
};

sampler sampler_linear;

tex2d tex0;

tex2d texa;

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
	float mask = sample_lod(texa, sampler_linear, input.tex, 0.0).r;
	mask = lerp(1.0, mask, constants.mask_opacity);
	return float4(col0.rgb, col0.a * mask);
}
