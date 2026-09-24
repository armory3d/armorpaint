
cbuffer constants {
	float4 empty;
	int channel;
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
	if (constants.channel == 1) {
		return sample_lod(tex, sampler_linear, input.tex, 0.0).rrra * input.col;
	}
	if (constants.channel == 2) {
		return sample_lod(tex, sampler_linear, input.tex, 0.0).ggga * input.col;
	}
	if (constants.channel == 3) {
		return sample_lod(tex, sampler_linear, input.tex, 0.0).bbba * input.col;
	}
	if (constants.channel == 4) {
		return sample_lod(tex, sampler_linear, input.tex, 0.0).aaaa * input.col;
	}
	if (constants.channel == 5) {
		return sample_lod(tex, sampler_linear, input.tex, 0.0).rgba * input.col;
	}
	if (constants.channel == 6) {
		// Apply mask as red area
		float4 tex_sample = sample_lod(tex, sampler_linear, input.tex, 0.0);
		if (tex_sample.r < 0.9 || tex_sample.g < 0.9 || tex_sample.b < 0.9) {
			return float4(1.0, 0.0, 0.0, 1.0);
		}
		discard;
	}
	// else {
		float4 tex_sample = sample_lod(tex, sampler_linear, input.tex, 0.0);
		tex_sample.rgb = tex_sample.rgb * tex_sample.a;
		return tex_sample * input.col;
	// }
}
