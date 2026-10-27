
cbuffer constants {
	float4x4 VP;
	float2 center;
};

struct vert_in {
	float4 pos;
	float4 col;
};

struct vert_out {
	float4 pos;
	float4 wpos;
	float4 col;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.wpos = input.pos;
	output.col = input.col;
	output.pos = constants.VP * float4(input.pos.xyz, 1.0);
	return output;
}

float4 frag(vert_out input) {
	// pos.w is the distance from the center the line fades out at
	float fade = clamp(1.0 - distance(input.wpos.xy, constants.center) / input.wpos.w, 0.0, 1.0);
	return float4(input.col.rgb, input.col.a * fade);
}
