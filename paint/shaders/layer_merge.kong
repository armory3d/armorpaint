
cbuffer constants {
	float opac;
	int blending;
	float tex1w;
};

sampler sampler_linear;

tex2d tex0;

tex2d tex1;

tex2d texmask;

tex2d texa;

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

float3 hsv_to_rgb(float3 c) {
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs3(frac3(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp3(K.xxx, clamp3(p - K.xxx, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0)), c.y);
}

float3 rgb_to_hsv(float3 c) {
	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	float4 p = lerp4(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
	float4 q = lerp4(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 0.0000000001;
	return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float4 frag(vert_out input) {
	float4 col0 = sample_lod(tex0, sampler_linear, input.tex, 0.0);
	float4 cola = sample_lod(texa, sampler_linear, input.tex, 0.0);
	float maskr = sample_lod(texmask, sampler_linear, input.tex, 0.0).r;
	float str = col0.a * constants.opac;
	str *= maskr;

	if (constants.blending == 101) { // Merging _nor and _pack
		float4 col1 = sample_lod(tex1, sampler_linear, input.tex, 0.0);
		return lerp4(cola, col1, str);
	}
	if (constants.blending == 102) { // Merging _nor with normal blending
		float4 col1 = sample_lod(tex1, sampler_linear, input.tex, 0.0);
		// Whiteout blend
		float3 n1 = cola.rgb * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
		float3 n2 = lerp3(float3(0.5, 0.5, 1.0), col1.rgb, str) * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
		return float4(
			normalize(float3(n1.xy + n2.xy, n1.z * n2.z)) * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5),
			max(col1.a, cola.a));
	}
	if (constants.blending == 103) { // Merging _pack with height blending
		float4 col1 = sample_lod(tex1, sampler_linear, input.tex, 0.0);
		return float4(lerp3(cola.rgb, col1.rgb, str), cola.a + col1.a * str);
	}
	if (constants.blending == 104) { // Merge _pack.height into _nor
		float tex_step = 1.0 / constants.tex1w;
		float height0 = sample_lod(tex1, sampler_linear, float2(input.tex.x - tex_step, input.tex.y), 0.0).a;
		float height1 = sample_lod(tex1, sampler_linear, float2(input.tex.x + tex_step, input.tex.y), 0.0).a;
		float height2 = sample_lod(tex1, sampler_linear, float2(input.tex.x, input.tex.y - tex_step), 0.0).a;
		float height3 = sample_lod(tex1, sampler_linear, float2(input.tex.x, input.tex.y + tex_step), 0.0).a;
		float height_dx = height0 - height1;
		float height_dy = height2 - height3;
		// Whiteout blend
		float3 n1 = col0.rgb * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
		float3 n2 = normalize(float3(height_dx * 16.0, height_dy * 16.0, 1.0));
		return float4(
			normalize(float3(n1.xy + n2.xy, n1.z * n2.z)) * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5),
			col0.a);
	}
	if (constants.blending == 0) { // Mix
		return float4(
			lerp3(cola.rgb, col0.rgb, str), max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 1) { // Darken
		return float4(
			lerp3(cola.rgb, min3(cola.rgb, col0.rgb), str), max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 2) { // Multiply
		return float4(
			lerp3(cola.rgb, cola.rgb * col0.rgb, str), max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 3) { // Burn
		return float4(
			lerp3(
				cola.rgb,
				float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - cola.rgb) / col0.rgb,
				str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 4) { // Lighten
		return float4(
			max3(cola.rgb, col0.rgb * str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 5) { // Screen
		return float4(
			(float3(1.0, 1.0, 1.0) - (float3(1.0 - str, 1.0 - str, 1.0 - str) +
				str * (float3(1.0, 1.0, 1.0) - col0.rgb)) * (float3(1.0, 1.0, 1.0) - cola.rgb)),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 6) { // Dodge
		return float4(
			lerp3(cola.rgb, cola.rgb / (float3(1.0, 1.0, 1.0) - col0.rgb), str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 7) { // Add
		return float4(
			lerp3(cola.rgb, cola.rgb + col0.rgb, str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 8) { // Overlay
		float rr = lerp(2.0 * cola.r * col0.r, 1.0 - 2.0 * (1.0 - cola.r) * (1.0 - col0.r), step(0.5, cola.r));
		float gg = lerp(2.0 * cola.g * col0.g, 1.0 - 2.0 * (1.0 - cola.g) * (1.0 - col0.g), step(0.5, cola.g));
		float bb = lerp(2.0 * cola.b * col0.b, 1.0 - 2.0 * (1.0 - cola.b) * (1.0 - col0.b), step(0.5, cola.b));
		return float4(
			lerp3(cola.rgb, float3(rr, gg, bb), str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 9) { // Soft Light
		return float4(
			((1.0 - str) * cola.rgb +
				str * ((float3(1.0, 1.0, 1.0) - cola.rgb) * col0.rgb * cola.rgb +
				cola.rgb * (float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - col0.rgb) * (float3(1.0, 1.0, 1.0) - cola.rgb)))),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 10) { // Linear Light
		return float4(
			(cola.rgb + str * (float3(2.0, 2.0, 2.0) * (col0.rgb - float3(0.5, 0.5, 0.5)))),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 11) { // Difference
		return float4(
			lerp3(cola.rgb, abs3(cola.rgb - col0.rgb), str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 12) { // Subtract
		return float4(
			lerp3(cola.rgb, cola.rgb - col0.rgb, str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 13) { // Divide
		return float4(
			float3(1.0 - str, 1.0 - str, 1.0 - str) * cola.rgb + float3(str, str, str) * cola.rgb / col0.rgb,
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 14) { // Hue
		return float4(
			lerp3(
				cola.rgb,
				hsv_to_rgb(
					float3(rgb_to_hsv(col0.rgb).r,
					rgb_to_hsv(cola.rgb).g,
					rgb_to_hsv(cola.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 15) { // Saturation
		return float4(
			lerp3(
				cola.rgb,
				hsv_to_rgb(
					float3(rgb_to_hsv(cola.rgb).r,
					rgb_to_hsv(col0.rgb).g,
					rgb_to_hsv(cola.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 16) { // Color
		return float4(
			lerp3(
				cola.rgb,
				hsv_to_rgb(
					float3(rgb_to_hsv(col0.rgb).r,
					rgb_to_hsv(col0.rgb).g,
					rgb_to_hsv(cola.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}
	if (constants.blending == 17) { // Value
		return float4(
			lerp3(
				cola.rgb,
				hsv_to_rgb(
					float3(rgb_to_hsv(cola.rgb).r,
					rgb_to_hsv(cola.rgb).g,
					rgb_to_hsv(col0.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}

	return float4(1.0, 1.0, 1.0, 1.0);
}
