
cbuffer constants {
	float4 empty;
};

sampler sampler_linear;

tex2d height_map;

tex2d normal_map;

const float num_dirs   = 64.0;  // cosine-weighted hemisphere directions
const float num_steps  = 24.0;  // height-field march steps per direction
const float max_radius = 0.04;  // search radius
const float height_scale = 1.0;
const float ao_bias    = 0.0015; // ignore occluders within this height of the surface
const float ao_max_diff = 0.3;   // ignore occluders taller than this
const float ao_power   = 1.6;    // contrast of the final occlusion
const float PI2        = 6.28318530718;

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

float hash(float2 p) {
	return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float3 tangent(float3 n) {
	float3 t1 = cross(n, float3(0.0, 0.0, 1.0));
	float3 t2 = cross(n, float3(0.0, 1.0, 0.0));
	if (length(t1) > length(t2)) {
		return normalize(t1);
	}
	return normalize(t2);
}

float ao_ray(float3 dir, float2 uv, float h0) {
	float step_size = max_radius / num_steps;
	float t = step_size;
	int i = 0;
	while (i < int(num_steps)) {
		float2 coord = uv + dir.xy * t;
		if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
			return 0.0; // walked off the map
		}
		float ray_z = h0 + dir.z * t;
		float surf = sample_lod(height_map, sampler_linear, coord, 0.0).r * height_scale;
		float delta = surf - ray_z;
		if (delta > ao_bias && delta < ao_max_diff) {
			return 1.0 - t / max_radius; // linear distance falloff
		}
		t += step_size;
		i += 1;
	}
	return 0.0;
}

float4 frag(vert_out input) {
	float height = sample_lod(height_map, sampler_linear, input.tex, 0.0).r * height_scale;
	float3 normal = sample_lod(normal_map, sampler_linear, input.tex, 0.0).rgb * 2.0 - 1.0;
	float3 n = normalize(normal);

	float3 t1 = tangent(n);
	float3 t2 = cross(n, t1);

	float jitter = hash(input.tex);
	float h0 = height + ao_bias;

	float occ = 0.0;
	int i = 0;
	while (i < int(num_dirs)) {
		// Cosine-weighted hemisphere sample
		float u1 = (float(i) + 0.5) / num_dirs;
		float u2 = frac(float(i) * 0.61803398875 + jitter);
		float r = sqrt(u1);
		float phi = PI2 * u2;
		float lx = r * cos(phi);
		float ly = r * sin(phi);
		float lz = sqrt(max(0.0, 1.0 - u1));
		float3 dir = lx * t1 + ly * t2 + lz * n;

		occ += ao_ray(dir, input.tex, h0);
		i += 1;
	}

	float ao = 1.0 - occ / num_dirs;
	ao = pow(max(0.0, ao), ao_power);
	return float4(ao, ao, ao, 1.0);
}
