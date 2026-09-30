cbuffer constants {
	float4x4 invP;
	float3x3 V3;
	float2 camera_proj;
	float2 screen_size_inv;
	float frame_offset;
	float ssao_strength;
};

sampler sampler_linear;

tex2d gbufferD;

tex2d gbuffer0;

const float num_slices      = 3.0;
const float num_steps       = 5.0;
const float radius          = 0.15;
const float min_radius_px   = 2.0;
const float max_radius_frac = 0.35;
const float falloff_start   = 0.75;
const float ao_power        = 1.5;
const float ao_dim          = 0.6;
const float PI              = 3.14159265358979;
const float HALF_PI         = 1.57079632679;

struct vert_in {
	float2 pos;
};

struct vert_out {
	float4 pos;
	float2 tex;
	float3 view_ray;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;

	output.pos = float4(input.pos.xy, 0.0, 1.0);

	// NDC (at the back of cube)
	float4 v = float4(input.pos.x, input.pos.y, 1.0, 1.0);
	v = constants.invP * v;
	output.view_ray = float3(v.xy / v.z, 1.0);

	return output;
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
	r.x = abs(v.y);
	r.y = abs(v.x);
	r.x = 1.0 - r.x;
	r.y = 1.0 - r.y;
	return r * a;
}

float3 get_nor(float2 enc) {
	float3 n;
	n.z = 1.0 - abs(enc.x) - abs(enc.y);
	if (n.z >= 0.0) {
		n.x = enc.x;
		n.y = enc.y;
	}
	else {
		float2 f2 = octahedron_wrap(enc);
		n.x = f2.x;
		n.y = f2.y;
	}
	return normalize(n);
}

float get_linear_depth(float d) {
	return constants.camera_proj.y / (constants.camera_proj.x - d);
}

float gradient_noise(float x, float y) {
	return frac(52.9829189 * frac(0.06711056 * x + 0.00583715 * y));
}

float horizon_cos(float2 uv, float2 dndc, float3 ray_c, float2 rx, float2 ry, float3 vpos, float3 view_v, float fall_scale,
                float fall_bias) {
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		return -1.0;
	}

	float ds = sample_lod(gbufferD, sampler_linear, uv, 0.0).r;
	if (ds >= 1.0) {
		return -1.0;
	}

	float3 ray = float3(ray_c.x + dndc.x * rx.x + dndc.y * ry.x, ray_c.y + dndc.x * rx.y + dndc.y * ry.y, 1.0);
	float3 delta = ray * get_linear_depth(ds) - vpos;

	float d2 = dot(delta, delta);
	float inv_len = rsqrt(max(d2, 0.00000001));
	float len = d2 * inv_len;
	float ch = dot(delta, view_v) * inv_len;
	return lerp(ch, -1.0, clamp(len * fall_scale - fall_bias, 0.0, 1.0));
}

float frag(vert_out input) {
	float ao = 1.0;
	float d = sample_lod(gbufferD, sampler_linear, input.tex, 0.0).r;

	if (d < 1.0) {
		float4 p0 = constants.invP * float4(0.0, 0.0, 1.0, 1.0);
		float4 px = constants.invP * float4(1.0, 0.0, 1.0, 1.0);
		float4 py = constants.invP * float4(0.0, 1.0, 1.0, 1.0);
		float2 r0 = p0.xy / p0.z;
		float2 rx = (px.xy / px.z) - r0;
		float2 ry = (py.xy / py.z) - r0;

		float3 vpos = input.view_ray * get_linear_depth(d);
		float3 view_v = normalize(0.0 - vpos);
		float3 n = normalize(constants.V3 * get_nor(sample_lod(gbuffer0, sampler_linear, input.tex, 0.0).rg));

		float screen_w = 1.0 / constants.screen_size_inv.x;
		float screen_h = 1.0 / constants.screen_size_inv.y;
		float view_to_px = screen_w * 0.5 / (abs(rx.x) * abs(vpos.z));
		float radius_px = clamp(radius * view_to_px, min_radius_px, screen_h * max_radius_frac);

		float eff_radius = radius_px / view_to_px;
		float fall_scale = 1.0 / max(eff_radius * (1.0 - falloff_start), 0.0001);
		float fall_bias = eff_radius * falloff_start * fall_scale;

		float px_x = input.tex.x * screen_w;
		float px_y = input.tex.y * screen_h;
		float slice_noise = gradient_noise(px_x, px_y) + constants.frame_offset;
		float step_noise = frac(gradient_noise(px_y, px_x) + constants.frame_offset);

		float vis = 0.0;
		int slice = 0;
		while (slice < int(num_slices)) {
			float phi = (float(slice) + slice_noise) * (PI / num_slices);
			float cp = cos(phi);
			float sp = sin(phi);

			float2 dir_uv = float2(cp * constants.screen_size_inv.x, sp * constants.screen_size_inv.y);
			float3 dir_v = float3(cp, 0.0 - sp, 0.0);

			float3 axis = normalize(cross(dir_v, view_v));
			float3 proj_n = n - axis * dot(n, axis);
			float proj_len = max(length(proj_n), 0.0001);
			float cos_n = clamp(dot(proj_n, view_v) / proj_len, -1.0, 1.0);
			float3 ortho = cross(view_v, axis);
			float n_ang = sign(dot(proj_n, ortho)) * acos(cos_n);
			float sin_n = sin(n_ang);

			float h_pos = -1.0;
			float h_neg = -1.0;

			int s = 0;
			while (s < int(num_steps)) {
				float t = (float(s) + step_noise) / num_steps;
				float dist_px = max(t * t * radius_px, min_radius_px);
				float2 off = dir_uv * dist_px;
				float2 dndc = float2(off.x * 2.0, off.y * -2.0);

				h_pos = max(h_pos, horizon_cos(input.tex + off, dndc, input.view_ray, rx, ry, vpos, view_v, fall_scale, fall_bias));
				h_neg = max(h_neg, horizon_cos(input.tex - off, 0.0 - dndc, input.view_ray, rx, ry, vpos, view_v, fall_scale, fall_bias));
				s += 1;
			}

			float ang_pos = acos(clamp(h_pos, -1.0, 1.0));
			float ang_neg = 0.0 - acos(clamp(h_neg, -1.0, 1.0));
			ang_pos = n_ang + min(ang_pos - n_ang, HALF_PI);
			ang_neg = n_ang + max(ang_neg - n_ang, 0.0 - HALF_PI);

			vis += proj_len * 0.25 *
			       ((cos_n - cos(2.0 * ang_neg - n_ang) + 2.0 * ang_neg * sin_n) + (cos_n - cos(2.0 * ang_pos - n_ang) + 2.0 * ang_pos * sin_n));
			slice += 1;
		}

		ao = pow(clamp(vis / num_slices, 0.0, 1.0), ao_power);
	}

	return ao_dim * clamp(1.0 - (1.0 - ao) * constants.ssao_strength, 0.0, 1.0);
}
