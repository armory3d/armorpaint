
cbuffer constants {
	float4x4 VP;
	float4x4 invVP;
	float2 mouse;
	float2 tex_step;
	float radius;
	float3 camera_right;
	float opacity;
	float2 angle;
	float scale_x;
	float3 camera_up;
	float camera_align;
};

sampler sampler_linear;

tex2d gbufferD;

tex2d texdecal;

tex2d gbuffer0;

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

float2 octahedron_wrap(float2 v) {
	float2 a;
	if (v.x >= 0.0) {
		a.x = 1.0;
	}
	else {
		a.x = -1.0;
	}
	if (v.y >= 0.0) {
		a.y = 1.0;
	}
	else {
		a.y = -1.0;
	}
	float2 r;
	r.x = 1.0 - abs(v.y);
	r.y = 1.0 - abs(v.x);
	return r * a;
}

float3 get_normal(float2 uv) {
	float2 g0 = sample_lod(gbuffer0, sampler_linear, float2(uv.x, 1.0 - uv.y), 0.0).rg;
	float3 n;
	n.z = 1.0 - abs(g0.x) - abs(g0.y);
	if (n.z >= 0.0) {
		n.x = g0.x;
		n.y = g0.y;
	}
	else {
		float2 fw = octahedron_wrap(g0.xy);
		n.x = fw.x;
		n.y = fw.y;
	}
	return n;
}

vert_out vert(vert_in input) {
	float keep = input.pos.x + input.nor.x; // hlsl

	vert_out output;
	output.tex = input.tex;
	float3 wpos = get_pos(constants.mouse);

	float2 uv1 = constants.mouse + constants.tex_step * float2(4.0, 4.0);
	float2 uv2 = constants.mouse - constants.tex_step * float2(4.0, 4.0);
	float3 n = normalize(get_normal(constants.mouse) + get_normal(uv1) + get_normal(uv2));

	float3 n_tan;
	float3 n_bin;
	if (constants.camera_align > 0.0) {
		n_tan = constants.camera_right;
		n_bin = -constants.camera_up;
	}
	else {
		float3 cam_right = constants.camera_right;
		if (abs(dot(n, cam_right)) > 0.999) { cam_right = float3(0.0, 0.0, 1.0); }
		n_tan = normalize(cam_right - n * dot(cam_right, n));
		n_bin = cross(n_tan, n);
	}

	// Rotate tangent basis by brush angle
	float3 r_tan = constants.angle.x * n_tan + constants.angle.y * n_bin;
	float3 r_bin = -constants.angle.y * n_tan + constants.angle.x * n_bin;

	if (vertex_id() == 3) {
		wpos += (-constants.scale_x * r_tan - r_bin) * constants.radius;
	}
	/*else */if (vertex_id() == 2) {
		wpos += ( constants.scale_x * r_tan - r_bin) * constants.radius;
	}
	/*else */if (vertex_id() == 1) {
		wpos += ( constants.scale_x * r_tan + r_bin) * constants.radius;
	}
	/*else */if (vertex_id() == 0) {
		wpos += (-constants.scale_x * r_tan + r_bin) * constants.radius;
	}

	output.pos = constants.VP * float4(wpos, 1.0);
	return output;
}

float4 frag(vert_out input) {
	float4 col = sample_lod(texdecal, sampler_linear, input.tex, 0.0);
	return float4(col.rgb, col.a * constants.opacity);
}
