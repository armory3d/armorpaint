
cbuffer constants {
	float4x4 WVP;
};

struct vert_in {
	float4 pos;
};

struct vert_out {
	float4 pos;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = constants.WVP * float4(input.pos.xyz, 1.0);
	return output;
}

float4 frag(vert_out input) {
	return float4(1.0, 1.0, 1.0, 1.0);
}
