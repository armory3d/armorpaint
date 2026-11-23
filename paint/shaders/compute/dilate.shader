
#ifdef FORMAT_RGBA32
tex2d<rgba32> target;
#endif
#ifdef FORMAT_RGBA64
tex2d<rgba64> target;
#endif
#ifdef FORMAT_RGBA128
tex2d<rgba128> target;
#endif

tex2d tex;      // Layer texture
tex2d texindex; // Dilation index map

float4 load_wrap(int x, int y, int w, int h) {
	int wx = x;
	if (wx < 0) {
		wx = wx + w;
	}
	if (wx >= w) {
		wx = wx - w;
	}
	int wy = y;
	if (wy < 0) {
		wy = wy + h;
	}
	if (wy >= h) {
		wy = wy - h;
	}
	return tex[uint2(uint(wx), uint(wy))];
}

float4 sample_bilinear(float2 pos, float2 tex_size) {
	float  fx     = floor(pos.x);
	float  fy     = floor(pos.y);
	int    x      = int(fx);
	int    y      = int(fy);
	int    w      = int(tex_size.x);
	int    h      = int(tex_size.y);
	float4 top    = lerp(load_wrap(x, y, w, h), load_wrap(x + 1, y, w, h), pos.x - fx);
	float4 bottom = lerp(load_wrap(x, y + 1, w, h), load_wrap(x + 1, y + 1, w, h), pos.x - fx);
	return lerp(top, bottom, pos.y - fy);
}

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
void dilate() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(tex);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	float4 index4 = texindex[id.xy];
	int    index  = int(index4.r);
	if (index == 0) {
		return;
	}
	int i = (index - 1) / 8 + 1;
	int j = (index - 1) % 8;

	// Nearest island texel, inside of the texture as checked by the index pass
	float2 dir        = get_offset(j) * float2(float(i), float(i));
	int    x          = int(id.x) + int(dir.x);
	int    y          = int(id.y) + int(dir.y);
	float4 offset_col = tex[uint2(uint(x), uint(y))];
	float4 color      = offset_col;

	// Extrapolate from the texel a quarter of the distance further into the island
	float2 pos       = float2(float(id.x), float(id.y)) + dir * float2(1.25, 1.25);
	float4 direction = sample_bilinear(pos, float2(float(dim.x), float(dim.y)));
	if (direction.x != 0.0 || direction.y != 0.0 || direction.z != 0.0) {
		float4 delta = offset_col - direction;
		color        = offset_col + delta * float4(4.0, 4.0, 4.0, 4.0);
	}

	target[id.xy] = color;
}
