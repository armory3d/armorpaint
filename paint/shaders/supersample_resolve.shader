
cbuffer constants {
	float2 screen_size_inv;
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

float4 frag(vert_out input) {
	// 4X resolve
	float2 tex_step = constants.screen_size_inv / 4.0;
	float4 col = sample(tex, sampler_linear, input.tex);
	col += sample(tex, sampler_linear, input.tex + float2(1.5, 0.0) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(-1.5, 0.0) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(0.0, 1.5) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(0.0, -1.5) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(1.5, 1.5) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(-1.5, -1.5) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(1.5, -1.5) * tex_step);
	col += sample(tex, sampler_linear, input.tex + float2(-1.5, 1.5) * tex_step);
	return col / 9.0;
}
