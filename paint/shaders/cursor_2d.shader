
cbuffer constants {
	float2 center;
	float2 size;
	float radius;
	float width;
};

struct vert_in {
	float2 pos;
	float2 tex;
	float4 col;
};

struct vert_out {
	float4 pos;
	float2 ndc;
	float4 col;
};

vert_out vert(vert_in input) {
	float keep = input.tex.x; // hlsl

	vert_out output;
	output.pos = float4(input.pos, 0.0, 1.0);
	output.ndc = input.pos;
	output.col = input.col;
	return output;
}

float4 frag(vert_out input) {
	// Pixel distance from the ring line, fixed width at any radius with a 1 px soft edge
	float2 p = float2(input.ndc.x * 0.5 + 0.5, 0.5 - input.ndc.y * 0.5) * constants.size;
	float d = abs(distance(p, constants.center) - constants.radius);
	float a = 1.0 - smoothstep(constants.width * 0.5 - 0.75, constants.width * 0.5 + 0.75, d);
	return float4(input.col.rgb, input.col.a * a);
}
