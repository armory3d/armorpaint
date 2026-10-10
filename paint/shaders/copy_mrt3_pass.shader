
cbuffer constants {
	float4 empty;
};

sampler sampler_linear;

tex2d tex0;

tex2d tex1;

tex2d tex2;

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

float4[3] frag(vert_out input) {
	float4 color[3];
	color[0] = sample_lod(tex0, sampler_linear, input.tex, 0.0);
	color[1] = sample_lod(tex1, sampler_linear, input.tex, 0.0);
	color[2] = sample_lod(tex2, sampler_linear, input.tex, 0.0);
	return color;
}
