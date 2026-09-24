
cbuffer constants {
	float4x4 WVP;
};

sampler sampler_linear;

tex2d my_texture;

struct vert_in {
	float4 pos;
	float2 nor;
	float2 tex;
};

struct vert_out {
	float4 pos;
	float3 nor;
	float2 tex;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.nor = float3(input.nor.xy, input.pos.w);
	output.tex = input.tex;
	output.pos = constants.WVP * float4(input.pos.xyz, 1.0);
    return output;
}

float4 frag(vert_out input) {
    float3 l = float3(0.5, 0.0, 0.5);
    float3 base_color = float3(1.0, 1.0, 1.0); // sample(my_texture, sampler_linear, input.tex).rgb;
    float3 ambient = base_color * 0.5;
    float3 n = normalize(input.nor);
    float dotnl = max(dot(n, l), 0.0);
    float3 diffuse = dotnl * base_color;
    return float4(ambient + diffuse, 1.0);
}
