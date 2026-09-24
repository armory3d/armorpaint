
cbuffer constants {
	float dilate_radius;
	float2 tex_size;
};

sampler sampler_linear;

tex2d tex;

tex2d texdilate;

// const float2 offsets[8] = {
// 	float2(-1.0,  0.0), float2( 1.0,  0.0), float2( 0.0,  1.0), float2( 0.0, -1.0),
// 	float2(-1.0,  1.0), float2( 1.0,  1.0), float2( 1.0, -1.0), float2(-1.0, -1.0)
// };

struct vert_in {
	float2 pos;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

float2 get_offset(int i) {
	if (i == 0) {
		return float2(-1.0,  0.0);
	}
	if (i == 1) {
		return float2( 1.0,  0.0);
	}
	if (i == 2) {
		return float2( 0.0,  1.0);
	}
	if (i == 3) {
		return float2( 0.0, -1.0);
	}
	if (i == 4) {
		return float2(-1.0,  1.0);
	}
	if (i == 5) {
		return float2( 1.0,  1.0);
	}
	if (i == 6) {
		return float2( 1.0, -1.0);
	}
	return float2(-1.0, -1.0);
}

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float4 frag(vert_out input) {
	// Based on https://shaderbits.com/blog/uv-dilation by Ryan Brucks
	float2 texel_size = float2(1.0, 1.0) / constants.tex_size;
	float min_dist = 10000000.0;
	//uint2 coord = uint2(input.tex * constants.tex_size);
	float2 coordf = float2(input.tex.x * constants.tex_size.x, input.tex.y * constants.tex_size.y);
	uint2 coord = uint2(uint(coordf.x), uint(coordf.y));
	//float mask = texdilate[coord].r;
	float4 mask4 = texdilate[coord];
	float mask = mask4.r;
	if (mask > 0.0) {
		discard;
	}

	float4 color = tex[coord];

	int i = 0;
	while (i < int(constants.dilate_radius)) {
		i += 1;
		int j = 0;
		while (j < 8) {
			float2 cur_uv = input.tex + get_offset(j) * texel_size * float2(float(i), float(i));
			coordf = cur_uv * constants.tex_size;
			coord = uint2(uint(coordf.x), uint(coordf.y));
			float4 offset_mask4 = texdilate[coord];
			float offset_mask = offset_mask4.r;
			float4 offset_col = tex[coord];

			if (offset_mask != 0.0) {
				float cur_dist = length(input.tex - cur_uv);
				if (cur_dist < min_dist) {
					float2 project_uv = cur_uv + get_offset(j) * texel_size * float2(float(i), float(i)) * float2(0.25, 0.25);
					float4 direction = sample_lod(tex, sampler_linear, project_uv, 0.0);
					min_dist = cur_dist;
					if (direction.x != 0.0 || direction.y != 0.0 || direction.z != 0.0) {
						float4 delta = offset_col - direction;
						color = offset_col + delta * float4(4.0, 4.0, 4.0, 4.0);
					}
					else {
						color = offset_col;
					}
				}
			}
			j += 1;
		}
	}

	return color;
}
