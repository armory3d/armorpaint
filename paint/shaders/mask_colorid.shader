
// Turn picked color id into mask

cbuffer constants {
	float4 empty;
};

sampler sampler_linear;

tex2d texpaint_colorid; // 1x1 picked color

tex2d texcolorid;

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
	uint2 coord = uint2(uint(0.0), uint(0.0));
	float4 colorid_c1 = texpaint_colorid[coord];
	float4 colorid_c2 = sample_lod(texcolorid, sampler_linear, input.tex, 0.0);
	if (colorid_c1.x != colorid_c2.x || colorid_c1.y != colorid_c2.y || colorid_c1.z != colorid_c2.z) {
		discard;
	}
	return float4(1.0, 1.0, 1.0, 1.0);
}
