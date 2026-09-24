
cbuffer constants {
	float4x4 VP;
	float4x4 invVP;
	float2 mouse;
	float2 tex_step;
	float radius;
	float3 camera_right;
	float3 tint;
	float3 camera_up;
	float camera_align;
};

sampler sampler_linear;

tex2d gbufferD;

struct vert_in {
	float4 pos;
	float2 nor;
	float2 tex;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

float3 get_pos(float2 uv) {
	float depth = sample_lod(gbufferD, sampler_linear, float2(uv.x, 1.0 - uv.y), 0.0).r;
	float4 wpos = float4(uv * 2.0 - 1.0, depth, 1.0);
	wpos = constants.invVP * wpos;
	return wpos.xyz / wpos.w;
}

float3 get_normal(float3 p0, float2 uv) {
	float3 p1 = get_pos(uv + float2(constants.tex_step.x * 4.0, 0.0));
	float3 p2 = get_pos(uv + float2(0.0, constants.tex_step.y * 4.0));
	return normalize(cross(p2 - p0, p1 - p0));
}

vert_out vert(vert_in input) {
	float keep = input.pos.x + input.nor.x; // hlsl

	vert_out output;
	output.tex = input.tex;
	float3 wpos = get_pos(constants.mouse);
	float2 uv1 = constants.mouse + constants.tex_step * float2(4.0, 4.0);
	float2 uv2 = constants.mouse - constants.tex_step * float2(4.0, 4.0);
	float3 wpos1 = get_pos(uv1);
	float3 wpos2 = get_pos(uv2);
	float3 n = normalize(get_normal(wpos, constants.mouse) + get_normal(wpos1, uv1) + get_normal(wpos2, uv2));
	float3 n_tan;
	float3 n_bin;

	if (constants.camera_align > 0.0) {
		n_tan = constants.camera_right;
		n_bin = constants.camera_up;
	}
	else {
		n_tan = normalize(constants.camera_right - n * dot(constants.camera_right, n));
		n_bin = cross(n_tan, n);
	}

	if (vertex_id() == 0) {
		wpos += normalize(-n_tan - n_bin) * 0.7 * constants.radius;
	}
	/*else */if (vertex_id() == 1) {
		wpos += normalize( n_tan - n_bin) * 0.7 * constants.radius;
	}
	/*else */if (vertex_id() == 2) {
		wpos += normalize( n_tan + n_bin) * 0.7 * constants.radius;
	}
	/*else */if (vertex_id() == 3) {
		wpos += normalize(-n_tan + n_bin) * 0.7 * constants.radius;
	}

	output.pos = constants.VP * float4(wpos, 1.0);
	return output;
}

float4 frag(vert_out input) {
	float radius = 0.45;
	float thickness = 0.03;
	float dist = distance(input.tex, float2(0.5, 0.5));
	float ring = smoothstep(radius - thickness, radius, dist) - smoothstep(radius, radius + thickness, dist);
	return float4(constants.tint, min(ring, 0.6));
}
