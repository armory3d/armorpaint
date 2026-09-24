
cbuffer constants {
	float4 empty;
};

struct vert_in {
	float2 pos;
	float2 tex;
	float4 col;
};

struct vert_out {
	float4 pos;
	float4 col;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = float4(input.pos, 0.0, 1.0);
	output.col = input.col;
	return output;
}

float4 frag(vert_out input) {
	return input.col;
}
