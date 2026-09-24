
cbuffer constants {
	float empty;
};

struct vert_in {
	float4 pos;
	float2 nor;
	float2 tex;
};

struct vert_out {
	float4 pos;
};

vert_out vert(vert_in input) {
	vert_out output;
	float2 tex_coord = float2(input.tex.x * 2.0 - 1.0, (1.0 - input.tex.y) * 2.0 - 1.0);
	output.pos = float4(tex_coord, 0.0, 1.0);
	float keep = input.pos.x + input.nor.x;
	return output;
}

float frag(vert_out input) {
	return 1.0;
}
