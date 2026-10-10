
cbuffer constants {
	float4x4 WVP;
};

struct vert_in {
	float4 pos;
	float4 col;
};

struct vert_out {
	float4 pos;
	float3 vcolor;
};

vert_out vert(vert_in input) {
	vert_out output;
	float4 spos = float4(input.pos.xyz, 1.0);
	output.vcolor = input.col.rgb;
	output.pos = constants.WVP * spos;
	return output;
}

float4 frag(vert_out input) {
	float4 color;
	color = float4(input.vcolor, 1.0);
	// color.rgb = pow(color.rgb, vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
	color.r = pow(color.r, 1.0 / 2.2);
	color.g = pow(color.g, 1.0 / 2.2);
	color.b = pow(color.b, 1.0 / 2.2);
	return color;
}
