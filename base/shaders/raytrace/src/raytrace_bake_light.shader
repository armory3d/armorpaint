
cbuffer constant_buffer {
	float4 v0; // frame, strength, radius, offset
	float4 v1; // envstr, upaxis, envangle, env sampling enabled
	float4 v2; // light only (skip the layer base multiply)
	float4 v3;
	float4 v4;
};

bvh     scene;
tex2d   render_target;
tex2d   mytexture0;
tex2d   mytexture1;
tex2d   mytexture2;
tex2d   mytexture_env;
tex2d   mytexture_sobol;
tex2d   mytexture_scramble;
tex2d   mytexture_rank;
tex2d   mytexture_env_cdf;
sampler sampler_linear;

// Diffuse paths per dispatch
#ifdef _LOW_SAMPLES
const int SAMPLES = 2;
#else
const int SAMPLES = 8;
#endif

const int BOUNCES = 5; // Path vertices, direct light plus 4 diffuse bounces

// Sample dimensions of a bounce: light.xy, dir.xy
const int DIM_PER_BOUNCE = 4;

const float OFFSET    = 0.0005;
const float PI        = 3.1415926535;
const float PI2       = 6.283185307;
const float PI_SQ2    = 19.739208802;
const int   ENV_CDF_W = 256;
const int   ENV_CDF_H = 128;
const int   ENV_CDF_N = 32768;

struct tangent_basis {
	float3 tangent;
	float3 binormal;
};

float2 s16_to_f32(uint val) {
	int a = int(val << 16) >> 16;
	int b = int(val) >> 16;
	return float2(float(a), float(b)) / 32767.0;
}

float3 hit_attribute(float3 a0, float3 a1, float3 a2, float2 barycentrics) {
	return a0 + barycentrics.x * (a1 - a0) + barycentrics.y * (a2 - a0);
}

float2 hit_attribute2d(float2 a0, float2 a1, float2 a2, float2 barycentrics) {
	return a0 + barycentrics.x * (a1 - a0) + barycentrics.y * (a2 - a0);
}

uint2 table_texel(int i) {
	int t = (i & 131071) >> 2;
	return uint2(uint(t & 127), uint(t >> 7));
}

uint table_channel(float4 c, int i) {
	int   ch = i & 3;
	float v  = c.a;
	if (ch == 0) {
		v = c.r;
	}
	else if (ch == 1) {
		v = c.g;
	}
	else if (ch == 2) {
		v = c.b;
	}
	return uint(v * 255.0);
}

float rand(int pixel_i, int pixel_j, int sample_index, int sample_dimension, int frame) {
	int    i              = ((sample_dimension & 255) % 8) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	float4 scramble       = mytexture_scramble[table_texel(i)];
	uint   scramble_value = table_channel(scramble, i);

	int    r          = (sample_dimension & 255) + (((pixel_i + frame * 9) & 127) + ((pixel_j + frame * 11) & 127) * 128) * 8;
	float4 rank       = mytexture_rank[table_texel(r)];
	uint   rank_value = table_channel(rank, r);

	sample_index            = sample_index & 255;
	sample_dimension        = sample_dimension & 255;
	int    ranked_sample_index = sample_index ^ int(rank_value);
	float4 sobol           = mytexture_sobol[uint2(uint(ranked_sample_index), uint(sample_dimension))];
	int    value           = int(sobol.r * 255.0);
	value                  = value ^ int(scramble_value);
	return (0.5 + float(value)) / 256.0;
}

tangent_basis create_basis(float3 normal) {
	float s = 1.0;
	if (normal.z < 0.0) {
		s = -1.0;
	}
	float a = -1.0 / (s + normal.z);
	float b = normal.x * normal.y * a;
	tangent_basis result;
	result.tangent  = float3(1.0 + s * normal.x * normal.x * a, s * b, -s * normal.x);
	result.binormal = float3(b, s + normal.y * normal.y * a, -normal.y);
	return result;
}

float3 cos_weighted_direction(float3 n, float u1, float u2) {
	tangent_basis basis = create_basis(n);
	float         r     = sqrt(u1);
	float         phi   = PI2 * u2;
	return basis.tangent * (r * cos(phi)) + basis.binormal * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1));
}

float2 equirect(float3 normal, float angle) {
	float phi   = acos(clamp(normal.z, -1.0, 1.0));
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

float3 env_radiance(float3 dir) {
	float2 tex_coord = equirect(dir, constant_buffer.v1.z);
	float4 env       = sample_lod(mytexture_env, sampler_linear, tex_coord, 0.0);
	return env.rgb * constant_buffer.v1.x;
}

int clamp_int(int x, int low, int high) {
	if (x < low) {
		return low;
	}
	if (x > high) {
		return high;
	}
	return x;
}

float3 env_dir(float2 uv) {
	float phi = uv.y * PI;
	float a   = uv.x * PI2 - PI - constant_buffer.v1.z;
	float s   = sin(phi);
	return float3(s * cos(a), -s * sin(a), cos(phi));
}

// Direction and pdf, importance sampled from the envmap luminance (alias table)
float4 sample_env(float u1, float u2) {
	float  fi = u1 * float(ENV_CDF_N);
	int    i  = clamp_int(int(fi), 0, ENV_CDF_N - 1);
	float  ju = fi - float(i);
	float4 a  = mytexture_env_cdf[uint2(uint(i % 256), uint(i / 256))];

	int   cell;
	float jv = 0.0;
	float pdf_uv;
	if (u2 < a.x) {
		cell = i;
		if (a.x > 0.0) {
			jv = u2 / a.x;
		}
		pdf_uv = a.z;
	}
	else {
		cell = clamp_int(int(a.y), 0, ENV_CDF_N - 1);
		if (a.x < 1.0) {
			jv = (u2 - a.x) / (1.0 - a.x);
		}
		pdf_uv = a.w;
	}

	float2 uv      = float2((float(cell % 256) + ju) / float(ENV_CDF_W), (float(cell / 256) + jv) / float(ENV_CDF_H));
	float  sin_phi = sin(uv.y * PI);
	float  pdf     = 0.0;
	if (sin_phi > 0.000001) {
		pdf = pdf_uv / (PI_SQ2 * sin_phi);
	}
	return float4(env_dir(uv), pdf);
}

float env_pdf(float3 dir) {
	float2 uv      = equirect(dir, constant_buffer.v1.z);
	int    x       = clamp_int(int(frac(uv.x) * float(ENV_CDF_W)), 0, ENV_CDF_W - 1);
	int    y       = clamp_int(int(uv.y * float(ENV_CDF_H)), 0, ENV_CDF_H - 1);
	float4 cdf     = mytexture_env_cdf[uint2(uint(x), uint(y))];
	float  sin_phi = sin(uv.y * PI);
	if (sin_phi > 0.000001) {
		return cdf.z / (PI_SQ2 * sin_phi);
	}
	return 0.0;
}

float mis_weight(float pdf_a, float pdf_b) {
	float a = pdf_a * pdf_a;
	float b = pdf_b * pdf_b;
	return a / max(a + b, 0.000000001);
}

bool occluded(float3 origin, float3 dir) {
	ray shadow_ray;
	shadow_ray.origin    = origin;
	shadow_ray.direction = dir;
	shadow_ray.min       = 0.0001;
	shadow_ray.max       = 100.0;
	ray_query q;
	ray_query_trace_any(q, scene, shadow_ray);
	return ray_query_hit(q);
}

// Diffuse light arriving at each texel: direct envmap light with shadows plus diffuse interreflections.
// The light is the cosine weighted average of the incoming radiance, so a lit surface is albedo * light.
#[compute, threads(8, 8, 1)]
void raytrace() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(render_target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	float4 tex0 = mytexture0[id.xy];
	if (tex0.a == 0.0) {
		render_target[id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	float3 pos0 = tex0.rgb;
	float4 tex1 = mytexture1[id.xy];
	float3 nor0 = normalize(tex1.rgb);

	int  frame        = int(constant_buffer.v0.x);
	bool env_sampling = constant_buffer.v1.w != 0.0;
	int  px           = int(id.x);
	int  py           = int(id.y);

	float3 accum = float3(0.0, 0.0, 0.0);

	for (int j = 0; j < SAMPLES; j += 1) {
		int    sample_index = frame * SAMPLES + j;
		float3 pos          = pos0;
		float3 n            = nor0;
		float3 ng           = nor0;
		float3 throughput   = float3(1.0, 1.0, 1.0);
		float3 contrib      = float3(0.0, 0.0, 0.0);

		for (int b = 0; b < BOUNCES; b += 1) {
			int dim_base = b * DIM_PER_BOUNCE;

			// Direct envmap light
			if (env_sampling) {
				float4 light     = sample_env(rand(px, py, sample_index, dim_base, frame), rand(px, py, sample_index, dim_base + 1, frame));
				float3 wl        = light.xyz;
				float  pdf_light = light.w;
				float  ndotl     = dot(n, wl);
				if (pdf_light > 0.0 && ndotl > 0.0 && dot(ng, wl) > 0.0) {
					if (!occluded(pos + ng * OFFSET, wl)) {
						float pdf_diffuse = ndotl / PI;
						contrib += throughput * env_radiance(wl) * (pdf_diffuse / pdf_light) * mis_weight(pdf_light, pdf_diffuse);
					}
				}
			}

			// Diffuse bounce
			float3 dir = cos_weighted_direction(n, rand(px, py, sample_index, dim_base + 2, frame), rand(px, py, sample_index, dim_base + 3, frame));
			if (dot(dir, ng) <= 0.0) {
				break;
			}
			float pdf_diffuse = max(dot(n, dir), 0.0) / PI;

			ray r;
			r.origin    = pos + ng * OFFSET;
			r.direction = dir;
			r.min       = 0.0001;
			r.max       = 100.0;

			ray_query q;
			ray_query_trace(q, scene, r);

			if (!ray_query_hit(q)) {
				float weight = 1.0;
				if (env_sampling) {
					weight = mis_weight(pdf_diffuse, env_pdf(dir));
				}
				contrib += throughput * env_radiance(dir) * weight;
				break;
			}

			uint   g            = ray_query_geometry(q);
			float2 barycentrics = ray_query_barycentrics(q);
			uint4  a0           = ray_query_vertex(q, 0); // posxy, poszw, nor, tex
			uint4  a1           = ray_query_vertex(q, 1);
			uint4  a2           = ray_query_vertex(q, 2);

			float2 tex_coord = hit_attribute2d(s16_to_f32(a0.w), s16_to_f32(a1.w), s16_to_f32(a2.w), barycentrics);
			uint2  size      = geometry_texture2_size(g);
			float4 texpaint2 = geometry_texture2(g, uint2(frac(tex_coord) * float2(size))); // Base color
			throughput *= pow(texpaint2.rgb, float3(2.2, 2.2, 2.2));
			if (max(max(throughput.r, throughput.g), throughput.b) <= 0.0) {
				break;
			}

			float2 zw0 = s16_to_f32(a0.y);
			float2 zw1 = s16_to_f32(a1.y);
			float2 zw2 = s16_to_f32(a2.y);
			float3 vp0 = float3(s16_to_f32(a0.x), zw0.x);
			float3 vp1 = float3(s16_to_f32(a1.x), zw1.x);
			float3 vp2 = float3(s16_to_f32(a2.x), zw2.x);
			float3 vn0 = float3(s16_to_f32(a0.z), zw0.y);
			float3 vn1 = float3(s16_to_f32(a1.z), zw1.y);
			float3 vn2 = float3(s16_to_f32(a2.z), zw2.y);

			float3x3 obj_to_world = ray_query_object_to_world(q);
			n                     = normalize(obj_to_world * hit_attribute(vn0, vn1, vn2, barycentrics));
			ng                    = cross(vp1 - vp0, vp2 - vp0);
			if (dot(ng, ng) > 0.00000000000000000001) {
				ng = normalize(obj_to_world * ng);
			}
			else {
				ng = n;
			}

			// Two-sided surfaces
			if (dot(ng, dir) > 0.0) {
				ng = -ng;
			}
			if (dot(n, ng) < 0.0) {
				n = -n;
			}

			pos = r.origin + dir * ray_query_distance(q);
		}

		accum += min(contrib, float3(32.0, 32.0, 32.0));
	}

	accum = accum / float(SAMPLES);

	if (constant_buffer.v2.x == 0.0) {
		float4 texpaint2 = mytexture2[id.xy]; // Layer base
		accum *= texpaint2.rgb;
	}

	float3 color = accum;
	if (frame > 0) {
		float4 target = render_target[id.xy];
		color         = lerp(target.rgb, accum, 1.0 / float(frame + 1));
	}

	render_target[id.xy] = float4(color, 1.0);
}
