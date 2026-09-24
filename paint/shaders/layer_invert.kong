
cbuffer constants {
	float4 empty;
};

sampler sampler_linear;

tex2d tex;

struct vert_in {
	float2 pos;
	float2 tex;
	float4 col;
};

struct vert_out {
	float4 pos;
	float2 tex;
	float4 col;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = float4(input.pos, 0.0, 1.0);
	output.tex = input.tex;
	output.col = input.col;
	return output;
}

float4 frag(vert_out input) {
	return float4(1.0, 1.0, 1.0, 1.0) - sample_lod(tex, sampler_linear, input.tex, 0.0) * input.col;
}
