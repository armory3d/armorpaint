cbuffer constants {
	float2 tex_size;
};

sampler sampler_linear;

tex2d tex;
tex2d texnbr0;
tex2d texnbr1;

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

// One diffusion step, texnbr0/1 hold the uvs of the left, right, up and down neighbors,
// across uv seams they point into the neighboring island, negative when there is no neighbor
float4 frag(vert_out input) {
	float2 coordf = input.tex * constants.tex_size;
	uint2 coord = uint2(uint(coordf.x), uint(coordf.y));
	float4 n0 = texnbr0[coord];
	float4 n1 = texnbr1[coord];
	float4 sum = tex[coord];
	float count = 1.0;
	if (n0.x >= 0.0) {
		sum += sample_lod(tex, sampler_linear, n0.xy, 0.0);
		count += 1.0;
	}
	if (n0.z >= 0.0) {
		sum += sample_lod(tex, sampler_linear, n0.zw, 0.0);
		count += 1.0;
	}
	if (n1.x >= 0.0) {
		sum += sample_lod(tex, sampler_linear, n1.xy, 0.0);
		count += 1.0;
	}
	if (n1.z >= 0.0) {
		sum += sample_lod(tex, sampler_linear, n1.zw, 0.0);
		count += 1.0;
	}
	return sum / float4(count, count, count, count);
}
