
cbuffer constants {
	float4x4 WVP;
	float3x3 N;
	float tex_unpack;
};

struct vert_in {
	float4 pos;
	float2 nor;
	float2 tex;
};

struct vert_out {
	float4 pos;
	float2 tex;
	float3 wnormal;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = constants.WVP * float4(input.pos.xyz, 1.0);
	output.tex = input.tex * constants.tex_unpack;
	output.wnormal = normalize(constants.N * float3(input.nor.xy, input.pos.w));
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

	// return (1.0 - abs(v.yx)) * (float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0));
}

float pack_f32_i16(float f, uint i) {
	// GBuffer helper - Sebastien Lagarde
	// https://seblagarde.wordpress.com/2018/09/02/gbuffer-helper-packing-integer-and-float-together/
	// const int num_bit_target = 16;
	// const int num_bit_i = 4;
	// const float prec = float(1 << num_bit_target);
	// const float maxi = float(1 << num_bit_i);
	// const float prec_minus_one = prec - 1.0;
	// const float t1 = ((prec / maxi) - 1.0) / prec_minus_one;
	// const float t2 = (prec / maxi) / prec_minus_one;
	// return t1 * f + t2 * float(i);
	return 0.062485207147583624 * min(f, 0.9990234375) + 0.062500476102698687 * float(i);
}

float4[3] frag(vert_out input) {
	float3 n = normalize(input.wnormal);
	float3 basecol = float3(0.2, 0.2, 0.2) + input.tex.x * 0.0000001; // keep tex_coord
	float roughness = 0.4;
	float metallic = 0.0;
	float occlusion = 1.0;

	// n /= abs(n.x) + abs(n.y) + abs(n.z);
	n = n / (abs(n.x) + abs(n.y) + abs(n.z));
	if (n.z >= 0.0) {
		n.xy = n.xy;
	}
	else {
		n.xy = octahedron_wrap(n.xy);
	}

	// uint matid = 0;
	uint matid = uint(0.0);
	float4 color[3];
	color[0] = float4(n.xy, roughness, pack_f32_i16(metallic, matid));
	color[1] = float4(basecol, occlusion);
	color[2] = float4(0.0, 0.0, 0.0, 0.0);
	return color;
}
