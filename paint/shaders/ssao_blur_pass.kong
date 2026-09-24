cbuffer constants {
	float2 dir_inv;
};

sampler sampler_linear;

tex2d tex;

tex2d gbuffer0;

const float num_taps      = 4.0;
const float blur_sigma    = 0.125;
const float nor_threshold = 0.8;
const float nor_scale     = 5.0;

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

float2 octahedron_wrap(float2 v) {
	float2 a;
	if (v.x >= 0.0) {
		a.x = 1.0;
	}
	else {
		a.x = -1.0;
	}

	if (v.y >= 0.0) {
		a.y = 1.0;
	}
	else {
		a.y = -1.0;
	}

	float2 r;
	r.x = abs(v.y);
	r.y = abs(v.x);
	r.x = 1.0 - r.x;
	r.y = 1.0 - r.y;
	return r * a;
}

float3 get_nor(float2 enc) {
	float3 n;
	n.z = 1.0 - abs(enc.x) - abs(enc.y);
	if (n.z >= 0.0) {
		n.xy = enc.xy;
	}
	else {
		n.xy = octahedron_wrap(enc.xy);
	}
	n = normalize(n);
	return n;
}

float frag(vert_out input) {

	float3 nor = get_nor(sample_lod(gbuffer0, sampler_linear, input.tex, 0.0).rg);
	float weight = 1.0;
	float color = sample_lod(tex, sampler_linear, input.tex, 0.0).r;

	for (int i = 1; i <= int(num_taps); i += 1) {
		float fi = float(i);
		float gw = exp(0.0 - fi * fi * blur_sigma);
		float2 off = fi * constants.dir_inv;

		float3 nor2 = get_nor(sample_lod(gbuffer0, sampler_linear, input.tex + off, 0.0).rg);
		float w = gw * clamp((dot(nor2, nor) - nor_threshold) * nor_scale, 0.0, 1.0);
		color += sample_lod(tex, sampler_linear, input.tex + off, 0.0).r * w;
		weight += w;

		nor2 = get_nor(sample_lod(gbuffer0, sampler_linear, input.tex - off, 0.0).rg);
		w = gw * clamp((dot(nor2, nor) - nor_threshold) * nor_scale, 0.0, 1.0);
		color += sample_lod(tex, sampler_linear, input.tex - off, 0.0).r * w;
		weight += w;
	}

	return color / weight;
}
