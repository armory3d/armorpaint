
cbuffer constants {
	float4 params; // x: dilate radius
};

tex2d      texdilate; // Uv island mask
tex2d<r32> target;    // Index map, ring * 8 + direction + 1, 0 inside of the islands or with no island in reach

float2 get_offset(int i) {
	if (i == 0) {
		return float2(-1.0, 0.0);
	}
	if (i == 1) {
		return float2(1.0, 0.0);
	}
	if (i == 2) {
		return float2(0.0, 1.0);
	}
	if (i == 3) {
		return float2(0.0, -1.0);
	}
	if (i == 4) {
		return float2(-1.0, 1.0);
	}
	if (i == 5) {
		return float2(1.0, 1.0);
	}
	if (i == 6) {
		return float2(1.0, -1.0);
	}
	return float2(-1.0, -1.0);
}

#[compute, threads(8, 8, 1)]
void dilate_index() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(texdilate);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	float4 mask4 = texdilate[id.xy];
	if (mask4.r > 0.0) {
		target[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	int    w          = int(dim.x);
	int    h          = int(dim.y);
	float2 texel_size = float2(1.0, 1.0) / float2(float(dim.x), float(dim.y));
	float  min_texel  = texel_size.x;
	if (texel_size.y < min_texel) {
		min_texel = texel_size.y;
	}
	float min_dist = 10000000.0;
	int   index    = 0;

	// Ring i is at least i texels away, stop once no further ring can be closer than the match found
	int i = 0;
	while (i < int(constants.params.x) && float(i + 1) * min_texel < min_dist) {
		i += 1;
		int j = 0;
		while (j < 8) {
			float2 offset = get_offset(j) * float2(float(i), float(i));
			int    x      = int(id.x) + int(offset.x);
			int    y      = int(id.y) + int(offset.y);
			if (x >= 0 && x < w && y >= 0 && y < h) {
				float4 offset_mask4 = texdilate[uint2(uint(x), uint(y))];
				if (offset_mask4.r != 0.0) {
					float cur_dist = length(offset * texel_size);
					if (cur_dist < min_dist) {
						min_dist = cur_dist;
						index    = (i - 1) * 8 + j + 1;
					}
				}
			}
			j += 1;
		}
	}

	target[id.xy] = float4(float(index), 0.0, 0.0, 0.0);
}
