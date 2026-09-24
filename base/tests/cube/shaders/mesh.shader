
cbuffer constants {
	float4x4 WVP;
};

sampler sampler_linear;

tex2d my_texture;

struct vert_in {
	float4 pos;
	float2 tex;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.tex;
	output.pos = constants.WVP * float4(input.pos.xyz, 1.0);
	return output;
}

float4 frag(vert_out input) {
	return sample(my_texture, sampler_linear, input.tex);
}
