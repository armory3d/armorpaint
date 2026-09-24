
cbuffer constants {
	float2 texel_size;
};

sampler sampler_linear;

tex2d height_map;

struct vert_in {
	float2 pos;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float4 frag(vert_out input) {
	float2 ts = constants.texel_size;
	float h00 = sample_lod(height_map, sampler_linear, input.tex + float2(-2.0, -2.0) * ts, 0.0).r;
	float h01 = sample_lod(height_map, sampler_linear, input.tex + float2(-1.0, -2.0) * ts, 0.0).r;
	float h02 = sample_lod(height_map, sampler_linear, input.tex + float2( 0.0, -2.0) * ts, 0.0).r;
	float h03 = sample_lod(height_map, sampler_linear, input.tex + float2( 1.0, -2.0) * ts, 0.0).r;
	float h04 = sample_lod(height_map, sampler_linear, input.tex + float2( 2.0, -2.0) * ts, 0.0).r;
	float h10 = sample_lod(height_map, sampler_linear, input.tex + float2(-2.0, -1.0) * ts, 0.0).r;
	float h11 = sample_lod(height_map, sampler_linear, input.tex + float2(-1.0, -1.0) * ts, 0.0).r;
	float h12 = sample_lod(height_map, sampler_linear, input.tex + float2( 0.0, -1.0) * ts, 0.0).r;
	float h13 = sample_lod(height_map, sampler_linear, input.tex + float2( 1.0, -1.0) * ts, 0.0).r;
	float h14 = sample_lod(height_map, sampler_linear, input.tex + float2( 2.0, -1.0) * ts, 0.0).r;
	float h20 = sample_lod(height_map, sampler_linear, input.tex + float2(-2.0,  0.0) * ts, 0.0).r;
	float h21 = sample_lod(height_map, sampler_linear, input.tex + float2(-1.0,  0.0) * ts, 0.0).r;
	float h23 = sample_lod(height_map, sampler_linear, input.tex + float2( 1.0,  0.0) * ts, 0.0).r;
	float h24 = sample_lod(height_map, sampler_linear, input.tex + float2( 2.0,  0.0) * ts, 0.0).r;
	float h30 = sample_lod(height_map, sampler_linear, input.tex + float2(-2.0,  1.0) * ts, 0.0).r;
	float h31 = sample_lod(height_map, sampler_linear, input.tex + float2(-1.0,  1.0) * ts, 0.0).r;
	float h32 = sample_lod(height_map, sampler_linear, input.tex + float2( 0.0,  1.0) * ts, 0.0).r;
	float h33 = sample_lod(height_map, sampler_linear, input.tex + float2( 1.0,  1.0) * ts, 0.0).r;
	float h34 = sample_lod(height_map, sampler_linear, input.tex + float2( 2.0,  1.0) * ts, 0.0).r;
	float h40 = sample_lod(height_map, sampler_linear, input.tex + float2(-2.0,  2.0) * ts, 0.0).r;
	float h41 = sample_lod(height_map, sampler_linear, input.tex + float2(-1.0,  2.0) * ts, 0.0).r;
	float h42 = sample_lod(height_map, sampler_linear, input.tex + float2( 0.0,  2.0) * ts, 0.0).r;
	float h43 = sample_lod(height_map, sampler_linear, input.tex + float2( 1.0,  2.0) * ts, 0.0).r;
	float h44 = sample_lod(height_map, sampler_linear, input.tex + float2( 2.0,  2.0) * ts, 0.0).r;
	float dx = 2.0 * (h00 - h04) + 1.0 * (h01 - h03)
	              + 3.0 * (h10 - h14) + 2.0 * (h11 - h13)
	              + 4.0 * (h20 - h24) + 3.0 * (h21 - h23)
	              + 3.0 * (h30 - h34) + 2.0 * (h31 - h33)
	              + 2.0 * (h40 - h44) + 1.0 * (h41 - h43);
	float dy = 2.0 * (h40 - h00) + 3.0 * (h41 - h01) + 4.0 * (h42 - h02) + 3.0 * (h43 - h03) + 2.0 * (h44 - h04)
	              + 1.0 * (h30 - h10) + 2.0 * (h31 - h11) + 3.0 * (h32 - h12) + 2.0 * (h33 - h13) + 1.0 * (h34 - h14);
	float3 normal = normalize(float3(dx, dy, 1.4375));
	normal = normal * 0.5 + 0.5;
	return float4(normal.x, normal.y, normal.z, 1.0);
}
