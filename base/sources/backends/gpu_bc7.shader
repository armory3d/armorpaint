// BC7 mode 6 encoder, a port of libs/bc7enc.c
// One thread per 4x4 block (dispatched as ceil(blocks_x / 64) x blocks_y groups), rgba8 pixels in, 128-bit blocks out
// Run gpu_bc7.sh after editing to regenerate vulkan_bc7.h, direct3d12_bc7.h and metal_bc7.h

cbuffer constant_buffer {
	float4 size; // width, height, blocks_x, blocks_y
};

uint src[];
uint dst[];

// One row of 4 packed rgba8 pixels, read once: src lives in host memory
uint4 load_row(uint bx, uint by, int y) {
	uint width  = uint(constant_buffer.size.x);
	uint height = uint(constant_buffer.size.y);
	uint sy     = by * uint(4) + uint(y);
	if (sy >= height) {
		sy = height - uint(1);
	}
	uint4 row = uint4(uint(0), uint(0), uint(0), uint(0));
	uint  sx  = bx * uint(4);
	row.x     = src[sy * width + sx];
	if (sx + uint(1) < width) {
		sx = sx + uint(1);
	}
	row.y = src[sy * width + sx];
	if (sx + uint(1) < width) {
		sx = sx + uint(1);
	}
	row.z = src[sy * width + sx];
	if (sx + uint(1) < width) {
		sx = sx + uint(1);
	}
	row.w = src[sy * width + sx];
	return row;
}

// Pixel i of the block from its rows, no local arrays in kong
uint4 pixel(uint4 r0, uint4 r1, uint4 r2, uint4 r3, int i) {
	uint4 row = r0;
	if (i >= 4) {
		row = r1;
	}
	if (i >= 8) {
		row = r2;
	}
	if (i >= 12) {
		row = r3;
	}
	int  x = i & 3;
	uint v = row.x;
	if (x == 1) {
		v = row.y;
	}
	if (x == 2) {
		v = row.z;
	}
	if (x == 3) {
		v = row.w;
	}
	return uint4(v & uint(255), (v >> uint(8)) & uint(255), (v >> uint(16)) & uint(255), v >> uint(24));
}

float4 to_float4(uint4 p) {
	return float4(float(p.x), float(p.y), float(p.z), float(p.w));
}

// find_optimal_solution: 7-bit endpoint with a p-bit, returns the 8-bit value (endpoint << 1 | p)
int quantize(float x, int p) {
	int v = int((x * 255.0 - float(p)) / 2.0 + 0.5) * 2 + p;
	if (v < p) {
		v = p;
	}
	if (v > 254 + p) {
		v = 254 + p;
	}
	return v;
}

float quantize_error(float4 x, int p, bool has_alpha) {
	float d   = float(quantize(x.x, p)) - x.x * 255.0;
	float err = d * d;
	d         = float(quantize(x.y, p)) - x.y * 255.0;
	err       = err + d * d;
	d         = float(quantize(x.z, p)) - x.z * 255.0;
	err       = err + d * d;
	if (has_alpha) {
		d   = float(quantize(x.w, p)) - x.w * 255.0;
		err = err + d * d;
	}
	return err;
}

int best_pbit(float4 x, bool has_alpha) {
	if (quantize_error(x, 1, has_alpha) < quantize_error(x, 0, has_alpha)) {
		return 1;
	}
	return 0;
}

int interpolate(int a, int b, int j) {
	int w = (j * 64 + 7) / 15; // 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64
	return (a * (64 - w) + b * w + 32) >> 6;
}

uint selector(uint lo, uint hi, int i) {
	if (i < 8) {
		return (lo >> uint(i * 4)) & uint(15);
	}
	return (hi >> uint((i - 8) * 4)) & uint(15);
}

// Part of a bit field at bit offset ofs that lands in 32-bit word
uint bits_in_word(uint val, int ofs, int word) {
	int shift = ofs - word * 32;
	if (shift >= 0 && shift < 32) {
		return val << uint(shift);
	}
	if (shift < 0 && shift > -32) {
		return val >> uint(0 - shift);
	}
	return uint(0);
}

#[compute, threads(64, 1, 1)]
void bc7_encode() {
	uint3 id = dispatch_thread_id();
	uint  bx = id.x;
	uint  by = id.y;
	if (bx >= uint(constant_buffer.size.z) || by >= uint(constant_buffer.size.w)) {
		return;
	}

	uint4 r0 = load_row(bx, by, 0);
	uint4 r1 = load_row(bx, by, 1);
	uint4 r2 = load_row(bx, by, 2);
	uint4 r3 = load_row(bx, by, 3);

	bool   has_alpha = false;
	float4 mean_sum  = float4(0.0, 0.0, 0.0, 0.0);
	for (int i = 0; i < 16; i += 1) {
		uint4 p = pixel(r0, r1, r2, r3, i);
		if (p.w < uint(255)) {
			has_alpha = true;
		}
		mean_sum = mean_sum + to_float4(p);
	}

	// Mean color and principal axis (color_cell_compression)
	float4 mean_scaled = mean_sum * 0.0625;
	float4 mean_color  = clamp(mean_sum * 0.00024509803921568627, float4(0.0, 0.0, 0.0, 0.0), float4(1.0, 1.0, 1.0, 1.0)); // 1 / (16 * 255)

	float4 axis = float4(0.0, 0.0, 0.0, 0.0);
	if (has_alpha) {
		// Incremental PCA
		for (int i = 0; i < 16; i += 1) {
			float4 color = to_float4(pixel(r0, r1, r2, r3, i)) - mean_scaled;
			float4 n     = axis;
			if (i == 0) {
				n = color;
			}
			float s = n.x * n.x + n.y * n.y + n.z * n.z + n.w * n.w;
			if (s != 0.0) {
				n = n * (1.0 / sqrt(s));
			}
			float4 a = color * color.x;
			float4 b = color * color.y;
			float4 c = color * color.z;
			float4 d = color * color.w;
			axis.x   = axis.x + (a.x * n.x + a.y * n.y + a.z * n.z + a.w * n.w);
			axis.y   = axis.y + (b.x * n.x + b.y * n.y + b.z * n.z + b.w * n.w);
			axis.z   = axis.z + (c.x * n.x + c.y * n.y + c.z * n.z + c.w * n.w);
			axis.w   = axis.w + (d.x * n.x + d.y * n.y + d.z * n.z + d.w * n.w);
		}
		float s = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z + axis.w * axis.w;
		if (s != 0.0) {
			axis = axis * (1.0 / sqrt(s));
		}
	}
	else {
		// Covariance, summed in 4 lanes like the simd path: (lane0 + lane1) + (lane2 + lane3)
		float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
		float4 c1 = float4(0.0, 0.0, 0.0, 0.0);
		float4 c2 = float4(0.0, 0.0, 0.0, 0.0);
		float4 c3 = float4(0.0, 0.0, 0.0, 0.0);
		float4 c4 = float4(0.0, 0.0, 0.0, 0.0);
		float4 c5 = float4(0.0, 0.0, 0.0, 0.0);
		for (int i = 0; i < 16; i += 4) {
			float4 p0 = to_float4(pixel(r0, r1, r2, r3, i));
			float4 p1 = to_float4(pixel(r0, r1, r2, r3, i + 1));
			float4 p2 = to_float4(pixel(r0, r1, r2, r3, i + 2));
			float4 p3 = to_float4(pixel(r0, r1, r2, r3, i + 3));
			float4 r  = float4(p0.x, p1.x, p2.x, p3.x) - float4(mean_scaled.x, mean_scaled.x, mean_scaled.x, mean_scaled.x);
			float4 g  = float4(p0.y, p1.y, p2.y, p3.y) - float4(mean_scaled.y, mean_scaled.y, mean_scaled.y, mean_scaled.y);
			float4 b  = float4(p0.z, p1.z, p2.z, p3.z) - float4(mean_scaled.z, mean_scaled.z, mean_scaled.z, mean_scaled.z);
			c0        = c0 + r * r;
			c1        = c1 + r * g;
			c2        = c2 + r * b;
			c3        = c3 + g * g;
			c4        = c4 + g * b;
			c5        = c5 + b * b;
		}
		float cov0 = (c0.x + c0.y) + (c0.z + c0.w);
		float cov1 = (c1.x + c1.y) + (c1.z + c1.w);
		float cov2 = (c2.x + c2.y) + (c2.z + c2.w);
		float cov3 = (c3.x + c3.y) + (c3.z + c3.w);
		float cov4 = (c4.x + c4.y) + (c4.z + c4.w);
		float cov5 = (c5.x + c5.y) + (c5.z + c5.w);

		float vfr = 0.9;
		float vfg = 1.0;
		float vfb = 0.7;
		for (int iter = 0; iter < 3; iter += 1) {
			float r = vfr * cov0 + vfg * cov1 + vfb * cov2;
			float g = vfr * cov1 + vfg * cov3 + vfb * cov4;
			float b = vfr * cov2 + vfg * cov4 + vfb * cov5;
			float m = max(max(abs(r), abs(g)), abs(b));
			if (m > 0.0000000001) {
				m = 1.0 / m;
				r = r * m;
				g = g * m;
				b = b * m;
			}
			vfr = r;
			vfg = g;
			vfb = b;
		}

		float len = vfr * vfr + vfg * vfg + vfb * vfb;
		if (len >= 0.0000000001) {
			len  = 1.0 / sqrt(len);
			axis = float4(vfr * len, vfg * len, vfb * len, 0.0);
		}
	}

	if (axis.x * axis.x + axis.y * axis.y + axis.z * axis.z + axis.w * axis.w < 0.5) {
		axis = float4(0.213, 0.715, 0.072, 0.0);
		if (has_alpha) {
			axis.w = 0.715;
		}
		float s = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z + axis.w * axis.w;
		axis    = axis * (1.0 / sqrt(s));
	}

	float l = 1000000000.0;
	float h = -1000000000.0;
	for (int i = 0; i < 16; i += 1) {
		float4 q = to_float4(pixel(r0, r1, r2, r3, i)) - mean_scaled;
		float  d = q.x * axis.x + q.y * axis.y + q.z * axis.z;
		if (has_alpha) {
			d = d + q.w * axis.w;
		}
		l = min(l, d);
		h = max(h, d);
	}
	l = l * 0.00392156862745098; // 1 / 255
	h = h * 0.00392156862745098;

	float4 zero      = float4(0.0, 0.0, 0.0, 0.0);
	float4 one       = float4(1.0, 1.0, 1.0, 1.0);
	float4 min_color = clamp(mean_color + axis * l, zero, one);
	float4 max_color = clamp(mean_color + axis * h, zero, one);
	if (min_color.x + min_color.y + min_color.z + min_color.w > max_color.x + max_color.y + max_color.z + max_color.w) {
		float4 t  = min_color;
		min_color = max_color;
		max_color = t;
	}

	int pbit0 = best_pbit(min_color, has_alpha);
	int pbit1 = best_pbit(max_color, has_alpha);
	int lo_r  = quantize(min_color.x, pbit0);
	int lo_g  = quantize(min_color.y, pbit0);
	int lo_b  = quantize(min_color.z, pbit0);
	int lo_a  = quantize(min_color.w, pbit0);
	int hi_r  = quantize(max_color.x, pbit1);
	int hi_g  = quantize(max_color.y, pbit1);
	int hi_b  = quantize(max_color.z, pbit1);
	int hi_a  = quantize(max_color.w, pbit1);

	// evaluate_solution: 16 interpolated colors, pick the closest per pixel
	uint sel_lo = uint(0);
	uint sel_hi = uint(0);
	for (int i = 0; i < 16; i += 1) {
		uint4 p        = pixel(r0, r1, r2, r3, i);
		int   pix_lum  = int(p.x) * 109 + int(p.y) * 366 + int(p.z) * 37;
		float pix_l    = float(pix_lum);
		float pix_cr   = float(int(p.x) * 512 - pix_lum);
		float pix_cb   = float(int(p.z) * 512 - pix_lum);
		float pix_a    = float(p.w);
		float best_err = 1000000000000000000000000000000.0;
		int   best_sel = 0;
		for (int j = 0; j < 16; j += 1) {
			int   r   = interpolate(lo_r, hi_r, j);
			int   g   = interpolate(lo_g, hi_g, j);
			int   b   = interpolate(lo_b, hi_b, j);
			int   lum = r * 109 + g * 366 + b * 37;
			float dr  = (float(lum) - pix_l) * 0.00390625;
			float dg  = (float(r * 512 - lum) - pix_cr) * 0.00390625;
			float db  = (float(b * 512 - lum) - pix_cb) * 0.00390625;
			float err = 512.0 * (dr * dr);
			err       = err + 103.0 * (dg * dg);
			err       = err + 18.0 * (db * db);
			if (has_alpha) {
				float da = float(interpolate(lo_a, hi_a, j)) - pix_a;
				err      = err + 128.0 * (da * da);
			}
			if (err < best_err) {
				best_err = err;
				best_sel = j;
			}
		}
		if (i < 8) {
			sel_lo = sel_lo | (uint(best_sel) << uint(i * 4));
		}
		else {
			sel_hi = sel_hi | (uint(best_sel) << uint((i - 8) * 4));
		}
	}

	// encode_bc7_block: the anchor selector must have its high bit clear
	int e_lo_r = lo_r >> 1;
	int e_lo_g = lo_g >> 1;
	int e_lo_b = lo_b >> 1;
	int e_lo_a = lo_a >> 1;
	int e_hi_r = hi_r >> 1;
	int e_hi_g = hi_g >> 1;
	int e_hi_b = hi_b >> 1;
	int e_hi_a = hi_a >> 1;
	if ((sel_lo & uint(8)) != uint(0)) {
		uint all_ones = uint(65535) | (uint(65535) << uint(16));
		sel_lo        = sel_lo ^ all_ones;
		sel_hi        = sel_hi ^ all_ones;
		e_lo_r        = hi_r >> 1;
		e_lo_g        = hi_g >> 1;
		e_lo_b        = hi_b >> 1;
		e_lo_a        = hi_a >> 1;
		e_hi_r        = lo_r >> 1;
		e_hi_g        = lo_g >> 1;
		e_hi_b        = lo_b >> 1;
		e_hi_a        = lo_a >> 1;
		int t         = pbit0;
		pbit0         = pbit1;
		pbit1         = t;
	}

	// Mode 6 layout: mode bit 6, r g b a endpoint pairs (7 bits), p-bits, 3-bit anchor index, 15 4-bit indices
	uint block_index = by * uint(constant_buffer.size.z) + bx;
	for (int w = 0; w < 4; w += 1) {
		uint word = bits_in_word(uint(64), 0, w);
		word      = word | bits_in_word(uint(e_lo_r), 7, w);
		word      = word | bits_in_word(uint(e_hi_r), 14, w);
		word      = word | bits_in_word(uint(e_lo_g), 21, w);
		word      = word | bits_in_word(uint(e_hi_g), 28, w);
		word      = word | bits_in_word(uint(e_lo_b), 35, w);
		word      = word | bits_in_word(uint(e_hi_b), 42, w);
		word      = word | bits_in_word(uint(e_lo_a), 49, w);
		word      = word | bits_in_word(uint(e_hi_a), 56, w);
		word      = word | bits_in_word(uint(pbit0), 63, w);
		word      = word | bits_in_word(uint(pbit1), 64, w);
		word      = word | bits_in_word(sel_lo & uint(7), 65, w);
		for (int i = 1; i < 16; i += 1) {
			word = word | bits_in_word(selector(sel_lo, sel_hi, i), 68 + (i - 1) * 4, w);
		}
		dst[block_index * uint(4) + uint(w)] = word;
	}
}
