
cbuffer constants {
	float4 color;
	float2 tex_step;
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
	float2 s = constants.tex_step;
	float2 d = s * 0.7071;
	float2 lo = s * 0.5;
	float2 hi = float2(1.0, 1.0) - lo;
	float inside = sample_lod(tex, sampler_linear, clamp(input.tex, lo, hi), 0.0).r;
	float around = sample_lod(tex, sampler_linear, clamp(input.tex + float2(s.x, 0.0), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(-s.x, 0.0), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(0.0, s.y), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(0.0, -s.y), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(d.x, d.y), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(-d.x, d.y), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(d.x, -d.y), lo, hi), 0.0).r;
	around += sample_lod(tex, sampler_linear, clamp(input.tex + float2(-d.x, -d.y), lo, hi), 0.0).r;
	float alpha = (1.0 - inside) * clamp(around * 0.5, 0.0, 1.0);
	return float4(constants.color.rgb, constants.color.a * alpha);
}
