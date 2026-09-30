
cbuffer constants {
	float4x4 VP;
	float3 color;
};

struct vert_in {
	float3 pos;
};

struct vert_out {
	float4 pos;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = constants.VP * float4(input.pos, 1.0);
	return output;
}

float4[2] frag(vert_out input) {
	float4 color[2];
	color[0] = float4(1.0, 1.0, 0.0, 1.0);
	color[1] = float4(constants.color, 1.0);
	return color;
}
