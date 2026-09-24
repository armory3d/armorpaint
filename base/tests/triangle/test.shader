
struct vert_in {
	float3 pos;
};

struct vert_out {
	float4 pos;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = float4(input.pos, 1.0);
	return output;
}

float4 frag(vert_out input) {
	return float4(1.0, 0.0, 0.0, 1.0);
}
