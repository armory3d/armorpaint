
cbuffer constants {
	float4 sun_dir_size;  // xyz: normalized sun direction (Z-up), w: sun angular radius (radians)
	float4 sun_params;    // x: sun intensity, y: sun disc toggle (0/1), z: observer altitude (km), w: ground albedo
	float4 atmo_params;   // x: air (Rayleigh), y: dust (Mie), z: ozone, w: Mie asymmetry g
	float4 cloud_params0; // x: coverage, y: density, z: scale, w: cloud base altitude (km)
	float4 cloud_params1; // x: seed, y: cloud thickness (km), z: cirrus amount, w: unused
};

#ifdef _TRANSMITTANCE
tex2d<rgba128> target;
#endif
#ifdef _MULTISCATTER
tex2d<rgba128> target;
tex2d          transmittance_lut;
#endif
#ifdef _ENVMAP
tex2d<rgba64> target;
tex2d         transmittance_lut;
tex2d         multiscatter_lut;
#endif

const float PI            = 3.14159265359;
const float PI2           = 6.28318530718;
const float PLANET_RADIUS = 6360.0;
const float ATMO_RADIUS   = 6460.0;

// Ray-sphere intersection centered at origin; returns float2(t_near, t_far), or (-1, -1) on miss
float2 ray_sphere(float3 ro, float3 rd, float rad) {
	float b = dot(ro, rd);
	float c = dot(ro, ro) - rad * rad;
	float d = b * b - c;
	if (d < 0.0) {
		return float2(-1.0, -1.0);
	}
	float s = sqrt(d);
	return float2(-b - s, -b + s);
}

// Atmosphere medium scattering and extinction at altitude h (km)
float3 get_rayleigh_scattering(float h) {
	float d = exp(-max(h, 0.0) / 8.0) * constants.atmo_params.x;
	return float3(0.005802, 0.013558, 0.033100) * d;
}

float get_mie_scattering(float h) {
	float d = exp(-max(h, 0.0) / 1.2) * constants.atmo_params.y;
	return 0.003996 * d;
}

float3 get_extinction(float h) {
	float  hh       = max(h, 0.0);
	float3 rayleigh = float3(0.005802, 0.013558, 0.033100) * exp(-hh / 8.0) * constants.atmo_params.x;
	float  mie_ext  = (0.003996 + 0.000440) * exp(-hh / 1.2) * constants.atmo_params.y;
	float  ozone_d  = max(0.0, 1.0 - abs(hh - 25.0) / 15.0) * constants.atmo_params.z;
	float3 ozone    = float3(0.000650, 0.001881, 0.000085) * ozone_d;
	return rayleigh + float3(mie_ext, mie_ext, mie_ext) + ozone;
}

float rayleigh_phase(float cos_theta) {
	return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

float hg_phase(float cos_theta, float g) {
	float g2    = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001);
	return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

// Transmittance LUT parameterization (Bruneton / Hillaire 2020)
float2 transmittance_params_to_uv(float r, float mu) {
	float H    = sqrt(ATMO_RADIUS * ATMO_RADIUS - PLANET_RADIUS * PLANET_RADIUS);
	float rho  = sqrt(max(0.0, r * r - PLANET_RADIUS * PLANET_RADIUS));
	float disc = r * r * (mu * mu - 1.0) + ATMO_RADIUS * ATMO_RADIUS;
	float d    = max(0.0, -r * mu + sqrt(max(0.0, disc)));
	float dmin = ATMO_RADIUS - r;
	float dmax = rho + H;
	float u    = (d - dmin) / max(dmax - dmin, 0.0001);
	float v    = rho / H;
	return float2(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0));
}

#ifndef _TRANSMITTANCE
float4 sample_lut_transmittance(float2 uv) {
	uint2  dim = texture_size(transmittance_lut);
	float2 p   = clamp(uv, float2(0.0, 0.0), float2(1.0, 1.0)) * float2(float(dim.x) - 1.0, float(dim.y) - 1.0);
	int    x0  = int(floor(p.x));
	int    y0  = int(floor(p.y));
	int    x1  = min(x0 + 1, int(dim.x) - 1);
	int    y1  = min(y0 + 1, int(dim.y) - 1);
	float  fx  = p.x - float(x0);
	float  fy  = p.y - float(y0);
	float4 c00 = transmittance_lut[uint2(uint(x0), uint(y0))];
	float4 c10 = transmittance_lut[uint2(uint(x1), uint(y0))];
	float4 c01 = transmittance_lut[uint2(uint(x0), uint(y1))];
	float4 c11 = transmittance_lut[uint2(uint(x1), uint(y1))];
	return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

float3 get_transmittance(float r, float mu) {
	return sample_lut_transmittance(transmittance_params_to_uv(r, mu)).rgb;
}

// Smooth horizon planet shadow for sun transmittance
float planet_shadow(float r, float mu_sun) {
	float sin_h = PLANET_RADIUS / max(r, PLANET_RADIUS);
	float cos_h = -sqrt(max(0.0, 1.0 - sin_h * sin_h));
	return smoothstep(cos_h - 0.015, cos_h + 0.015, mu_sun);
}
#endif

#ifdef _TRANSMITTANCE
#[compute, threads(8, 8, 1)]
void sky_transmittance() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	float2 uv   = (float2(float(id.x), float(id.y)) + float2(0.5, 0.5)) / float2(float(dim.x), float(dim.y));
	float  H    = sqrt(ATMO_RADIUS * ATMO_RADIUS - PLANET_RADIUS * PLANET_RADIUS);
	float  rho  = H * uv.y;
	float  r    = sqrt(rho * rho + PLANET_RADIUS * PLANET_RADIUS);
	float  dmin = ATMO_RADIUS - r;
	float  dmax = rho + H;
	float  d    = dmin + uv.x * (dmax - dmin);
	float  mu   = 1.0;
	if (d > 0.0001) {
		mu = clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
	}

	float3 ro = float3(0.0, 0.0, r);
	float3 rd = float3(sqrt(max(0.0, 1.0 - mu * mu)), 0.0, mu);

	float2 hit = ray_sphere(ro, rd, ATMO_RADIUS);
	float  t_m = max(hit.y, 0.0);

	int    steps = 40;
	float  dt    = t_m / float(steps);
	float3 od    = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < 40; i += 1) {
		float  t = (float(i) + 0.5) * dt;
		float3 p = ro + rd * t;
		float  h = length(p) - PLANET_RADIUS;
		od += get_extinction(h) * dt;
	}
	float3 trans  = exp(-od);
	target[id.xy] = float4(trans.r, trans.g, trans.b, 1.0);
}
#endif

#ifdef _MULTISCATTER
#[compute, threads(8, 8, 1)]
void sky_multiscatter() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	float2 uv      = (float2(float(id.x), float(id.y)) + float2(0.5, 0.5)) / float2(float(dim.x), float(dim.y));
	float  mu_sun  = uv.x * 2.0 - 1.0;
	float  r       = PLANET_RADIUS + clamp(uv.y, 0.001, 0.999) * (ATMO_RADIUS - PLANET_RADIUS);
	float3 ro      = float3(0.0, 0.0, r);
	float3 sun_dir = float3(sqrt(max(0.0, 1.0 - mu_sun * mu_sun)), 0.0, mu_sun);

	float3 lum_total  = float3(0.0, 0.0, 0.0);
	float3 f_ms_total = float3(0.0, 0.0, 0.0);
	float  iso_phase  = 1.0 / (4.0 * PI);
	float  golden     = 2.39996322973;
	float  ground_alb = constants.sun_params.w;

	// 64 Fibonacci sphere directions
	for (int s = 0; s < 64; s += 1) {
		float  y_dir = 1.0 - (float(s) + 0.5) * (2.0 / 64.0);
		float  rad   = sqrt(max(0.0, 1.0 - y_dir * y_dir));
		float  ang   = float(s) * golden;
		float3 rd    = float3(cos(ang) * rad, sin(ang) * rad, y_dir);

		float2 atmo_hit   = ray_sphere(ro, rd, ATMO_RADIUS);
		float2 planet_hit = ray_sphere(ro, rd, PLANET_RADIUS);
		float  t_max      = atmo_hit.y;
		bool   hit_ground = false;
		if (planet_hit.x > 0.0 && planet_hit.x < t_max) {
			t_max      = planet_hit.x;
			hit_ground = true;
		}

		float  dt    = t_max / 20.0;
		float3 trans = float3(1.0, 1.0, 1.0);
		float3 lum   = float3(0.0, 0.0, 0.0);
		float3 f_ms  = float3(0.0, 0.0, 0.0);

		for (int i = 0; i < 20; i += 1) {
			float  t       = (float(i) + 0.5) * dt;
			float3 p       = ro + rd * t;
			float  rp      = length(p);
			float  hp      = rp - PLANET_RADIUS;
			float3 ray_s   = get_rayleigh_scattering(hp);
			float  mie_s   = get_mie_scattering(hp);
			float3 scat    = ray_s + float3(mie_s, mie_s, mie_s);
			float3 ext     = max(get_extinction(hp), float3(0.0000001, 0.0000001, 0.0000001));
			float3 step_tr = exp(-ext * dt);

			float  mu_s  = dot(p / rp, sun_dir);
			float3 t_sun = get_transmittance(rp, mu_s) * planet_shadow(rp, mu_s);

			float3 s_in = scat * t_sun * iso_phase;
			lum += trans * (s_in - s_in * step_tr) / ext;
			f_ms += trans * (scat - scat * step_tr) / ext;
			trans *= step_tr;
		}

		if (hit_ground) {
			float3 p_g   = ro + rd * t_max;
			float  r_g   = length(p_g);
			float3 n_g   = p_g / r_g;
			float  ndotl = max(dot(n_g, sun_dir), 0.0);
			if (ndotl > 0.0) {
				float3 t_sun_g = get_transmittance(r_g, ndotl);
				lum += trans * t_sun_g * (ndotl * ground_alb / PI);
			}
		}

		lum_total += lum / 64.0;
		f_ms_total += f_ms / 64.0;
	}

	float3 psi    = lum_total / max(float3(1.0, 1.0, 1.0) - f_ms_total, float3(0.01, 0.01, 0.01));
	target[id.xy] = float4(psi.r, psi.g, psi.b, 1.0);
}
#endif

#ifdef _ENVMAP
float4 sample_lut_multiscatter(float2 uv) {
	uint2  dim = texture_size(multiscatter_lut);
	float2 p   = clamp(uv, float2(0.0, 0.0), float2(1.0, 1.0)) * float2(float(dim.x) - 1.0, float(dim.y) - 1.0);
	int    x0  = int(floor(p.x));
	int    y0  = int(floor(p.y));
	int    x1  = min(x0 + 1, int(dim.x) - 1);
	int    y1  = min(y0 + 1, int(dim.y) - 1);
	float  fx  = p.x - float(x0);
	float  fy  = p.y - float(y0);
	float4 c00 = multiscatter_lut[uint2(uint(x0), uint(y0))];
	float4 c10 = multiscatter_lut[uint2(uint(x1), uint(y0))];
	float4 c01 = multiscatter_lut[uint2(uint(x0), uint(y1))];
	float4 c11 = multiscatter_lut[uint2(uint(x1), uint(y1))];
	return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

float3 get_multiscatter(float r, float mu_sun) {
	float u = clamp(mu_sun * 0.5 + 0.5, 0.0, 1.0);
	float v = clamp((r - PLANET_RADIUS) / (ATMO_RADIUS - PLANET_RADIUS), 0.0, 1.0);
	return sample_lut_multiscatter(float2(u, v)).rgb;
}

// Exact integer PCG3D hash: zero float precision artifacts at any coordinate or resolution
float3 pcg3d_f(int x, int y, int z) {
	uint vx = uint(x) * uint(1664525) + uint(1013904223);
	uint vy = uint(y) * uint(1664525) + uint(1013904223);
	uint vz = uint(z) * uint(1664525) + uint(1013904223);
	vx += vy * vz;
	vy += vz * vx;
	vz += vx * vy;
	vx = vx ^ (vx >> uint(16));
	vy = vy ^ (vy >> uint(16));
	vz = vz ^ (vz >> uint(16));
	vx += vy * vz;
	vy += vz * vx;
	vz += vx * vy;
	float inv = 1.0 / 16777216.0;
	return float3(float(vx >> uint(8)) * inv, float(vy >> uint(8)) * inv, float(vz >> uint(8)) * inv);
}

// Orthonormal 3D rotation applied between every FBM octave to eliminate all grid/axis alignment
float3 rot3(float3 p) {
	return float3(0.80 * p.y + 0.60 * p.z, -0.80 * p.x + 0.36 * p.y - 0.48 * p.z, -0.60 * p.x - 0.48 * p.y + 0.64 * p.z);
}

// Smooth 3D Perlin gradient noise in [0, 1] (no blocky value-noise lattice plateaus)
float grad_dot(int ix, int iy, int iz, float3 f) {
	float3 g = pcg3d_f(ix, iy, iz) * 2.0 - float3(1.0, 1.0, 1.0);
	return dot(g, f);
}

float perlin3(float3 x) {
	float3 fl = floor(x);
	int    ix = int(fl.x);
	int    iy = int(fl.y);
	int    iz = int(fl.z);
	float3 f  = x - fl;
	float3 u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

	float n000 = grad_dot(ix, iy, iz, f - float3(0.0, 0.0, 0.0));
	float n100 = grad_dot(ix + 1, iy, iz, f - float3(1.0, 0.0, 0.0));
	float n010 = grad_dot(ix, iy + 1, iz, f - float3(0.0, 1.0, 0.0));
	float n110 = grad_dot(ix + 1, iy + 1, iz, f - float3(1.0, 1.0, 0.0));
	float n001 = grad_dot(ix, iy, iz + 1, f - float3(0.0, 0.0, 1.0));
	float n101 = grad_dot(ix + 1, iy, iz + 1, f - float3(1.0, 0.0, 1.0));
	float n011 = grad_dot(ix, iy + 1, iz + 1, f - float3(0.0, 1.0, 1.0));
	float n111 = grad_dot(ix + 1, iy + 1, iz + 1, f - float3(1.0, 1.0, 1.0));

	float nx00 = lerp(n000, n100, u.x);
	float nx10 = lerp(n010, n110, u.x);
	float nx01 = lerp(n001, n101, u.x);
	float nx11 = lerp(n011, n111, u.x);
	float v    = lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
	return clamp(v * 0.62 + 0.5, 0.0, 1.0);
}

// Rotated multi-octave 3D Perlin FBM
float perlin_fbm(float3 p, int octaves) {
	float  sum  = 0.0;
	float  amp  = 0.5;
	float  norm = 0.0;
	float3 q    = p;
	for (int i = 0; i < 6; i += 1) {
		if (i >= octaves) {
			break;
		}
		sum += amp * perlin3(q);
		norm += amp;
		q = rot3(q) * 2.03 + float3(17.13, 31.41, 11.79);
		amp *= 0.5;
	}
	return sum / max(norm, 0.0001);
}

// Inverted 3D Worley cellular billow noise (smooth rounded cumulus domes, PCG hashed)
float worley3(float3 x) {
	float3 fl = floor(x);
	int    ix = int(fl.x);
	int    iy = int(fl.y);
	int    iz = int(fl.z);
	float3 f  = x - fl;
	float  d  = 1.5;
	for (int dz = -1; dz <= 1; dz += 1) {
		for (int dy = -1; dy <= 1; dy += 1) {
			for (int dx = -1; dx <= 1; dx += 1) {
				float3 o = pcg3d_f(ix + dx, iy + dy, iz + dz);
				float3 r = float3(float(dx), float(dy), float(dz)) + o - f;
				float  l = dot(r, r);
				if (l < d) {
					d = l;
				}
			}
		}
	}
	float dist = clamp(sqrt(d), 0.0, 1.0);
	return 1.0 - dist * dist;
}

// Rotated multi-octave 3D Worley FBM for cauliflower cumulus billows
float worley_fbm(float3 p, int octaves) {
	float  sum  = 0.0;
	float  amp  = 0.625;
	float  norm = 0.0;
	float3 q    = p;
	for (int i = 0; i < 4; i += 1) {
		if (i >= octaves) {
			break;
		}
		sum += amp * worley3(q);
		norm += amp;
		q = rot3(q) * 2.07 + float3(9.37, 23.19, 14.53);
		amp *= 0.5;
	}
	return sum / max(norm, 0.0001);
}

// Interleaved Gradient Noise for blue-noise-like jitter with zero square artifacts
float ign(float2 xy) {
	return frac(52.9829189 * frac(0.06711056 * xy.x + 0.00583715 * xy.y));
}

float remap_clamp(float v, float lo0, float hi0, float lo1, float hi1) {
	float t = clamp((v - lo0) / max(hi0 - lo0, 0.0001), 0.0, 1.0);
	return lo1 + t * (hi1 - lo1);
}

// Volumetric cumulus density (Schneider / Nubis multi-scale Perlin-Worley + turbulent cauliflower erosion)
// detail_level: 0 = macro (far shadows / AO), 1 = medium (sun light march), 2 = full high-res billows
// dist_lod: distance from observer in km (smoothly anti-aliases sub-step micro billows far away)
float sample_cloud_density(float3 p, float cloud_r0, float cloud_thick, int detail_level, float dist_lod) {
	float h = (length(p) - cloud_r0) / max(cloud_thick, 0.001);
	if (h <= 0.0 || h >= 1.0) {
		return 0.0;
	}

	float coverage = constants.cloud_params0.x;
	float scale    = constants.cloud_params0.z;
	float seed     = constants.cloud_params1.x;

	// Tangent-plane coordinates centered over observer
	float3 rel      = p - float3(0.0, 0.0, PLANET_RADIUS);
	float3 seed_off = float3(seed * 19.37, seed * 31.13, seed * 7.89);

	// 1. Macro weather system: natural cloud streets, open sky channels, and cumulus height variation
	float3 w_coord  = float3(rel.x * 0.058 * scale, rel.y * 0.058 * scale, seed * 1.37) + float3(seed_off.x * 0.3, seed_off.y * 0.3, 0.0);
	float  w_warp_x = perlin3(w_coord * 0.72 + float3(4.2, 8.1, 1.3)) - 0.5;
	float  w_warp_y = perlin3(w_coord * 0.72 + float3(12.7, 3.4, 5.9)) - 0.5;
	float3 w_pos    = w_coord + float3(w_warp_x * 0.9, w_warp_y * 0.9, 0.0);

	float weather_cov  = perlin_fbm(w_pos, 4);
	float weather_type = perlin_fbm(w_pos * 1.25 + float3(27.4, 15.8, 3.2), 3);

	// Calibrated coverage curve: 0 -> clear, 0.45 -> rich scattered cumulus everywhere, 1.0 -> full overcast
	float cov_bias = lerp(-0.34, 0.64, pow(coverage, 0.82));
	float local_cv = clamp(weather_cov * 1.45 - 0.24 + cov_bias, 0.0, 1.0);
	if (local_cv <= 0.01) {
		return 0.0;
	}

	// 2. Sculptural dome-shaped vertical cumulus profile (flat condensation base, towering domes in cloud cores)
	float core_h    = clamp(local_cv * 0.65 + weather_type * 0.55, 0.0, 1.0);
	float cloud_top = lerp(0.28, 0.98, core_h);
	float h_bot     = smoothstep(0.0, 0.09, h);
	float h_top     = smoothstep(cloud_top, cloud_top * 0.38, h);
	float h_grad    = h_bot * h_top;
	if (h_grad <= 0.001) {
		return 0.0;
	}

	// 3. Base volumetric Perlin-Worley cumulus sculpting with gentle wind shear
	float3 q_base = rel * (0.27 * scale) + seed_off;
	q_base        = q_base + float3(h * 0.38, -h * 0.22, 0.0);

	int base_oct = 3;
	if (detail_level >= 1) {
		base_oct = 4;
	}
	float base_perlin = perlin_fbm(q_base, base_oct);
	float base_shape  = base_perlin;
	if (detail_level >= 1) {
		float base_worley = worley_fbm(q_base * 1.25 + float3(3.1, 7.4, 2.2), 3);
		// Nubis Perlin-Worley remap carves convex rounded cumulus turrets
		base_shape = remap_clamp(base_perlin, (1.0 - base_worley) * 0.52, 1.0, 0.0, 1.0);
	}

	// Carve base cloud volume by coverage and vertical profile
	float base_cloud = remap_clamp(base_shape * h_grad, 1.0 - local_cv, 1.0, 0.0, 1.0);
	if (base_cloud <= 0.001) {
		return 0.0;
	}

	// 4. High-frequency curl-warped cauliflower erosion (distance-LOD smoothed for zero slicing)
	float dens = base_cloud;
	if (detail_level >= 1) {
		float3 q_det = q_base * 4.10 + float3(11.3, 5.7, 19.1);
		float3 warp  = float3(perlin3(q_base * 1.95 + float3(1.7, 9.2, 4.3)) - 0.5,
		                      perlin3(q_base * 1.95 + float3(8.3, 2.8, 11.1)) - 0.5,
		                      perlin3(q_base * 1.95 + float3(5.1, 14.6, 2.4)) - 0.5);
		q_det        = q_det + warp * 0.95;

		int w_oct = 2;
		if (detail_level >= 2) {
			w_oct = 3;
		}
		float w_det = worley_fbm(q_det, w_oct);
		// Convex bulging cauliflower lobes at upper domes, soft sheared wisps at cloud bases
		float erode_n = lerp(w_det, 1.0 - w_det, smoothstep(0.05, 0.28, h));
		float e_str   = lerp(0.60, 0.52, smoothstep(0.0, 0.55, h)) * (1.0 - base_cloud * 0.38);
		dens          = remap_clamp(base_cloud, erode_n * e_str, 1.0, 0.0, 1.0);

		// Fine micro-billow pass for razor-sharp photographic cauliflower tufts (Nyquist LOD faded far away)
		if (detail_level >= 2 && dens > 0.001 && dens < 0.88) {
			float micro_w = 1.0 - smoothstep(18.0, 62.0, dist_lod);
			if (micro_w > 0.01) {
				float micro = worley_fbm(rot3(q_det) * 2.75 + float3(4.7, 13.2, 8.9), 2);
				float m_mod = 1.0 - micro;
				dens        = remap_clamp(dens, m_mod * 0.34 * micro_w * (1.0 - dens * 0.55), 1.0, 0.0, 1.0);
			}
		}
	}

	// Sharpen upper condensation boundary for crisp sunlit cumulus tops while keeping bases soft
	float edge_pow = lerp(1.05, 0.68, smoothstep(0.08, 0.45, h));
	dens           = pow(max(dens, 0.0), edge_pow);

	// Physical vertical density ramp: softer bases, dense sculptural upper cumulus cores
	float h_dens = lerp(0.48, 1.55, smoothstep(0.0, 0.40, h));
	return dens * h_dens * constants.cloud_params0.y;
}

// Integrate atmospheric scattering from ro along rd up to t_max
float4 integrate_atmosphere(float3 ro, float3 rd, float3 sun_dir, float t_max, int steps, float3 out_trans_init) {
	float  cos_theta = dot(rd, sun_dir);
	float  p_ray     = rayleigh_phase(cos_theta);
	float  p_mie     = hg_phase(cos_theta, constants.atmo_params.w);
	float3 lum       = float3(0.0, 0.0, 0.0);
	float3 trans     = out_trans_init;
	float  t_prev    = 0.0;

	for (int i = 0; i < 32; i += 1) {
		if (i >= steps) {
			break;
		}
		float f1   = float(i + 1) / float(steps);
		float t1   = t_max * lerp(f1, f1 * f1, 0.65);
		float dt   = t1 - t_prev;
		float tmid = t_prev + dt * 0.5;
		t_prev     = t1;

		float3 p       = ro + rd * tmid;
		float  rp      = length(p);
		float  hp      = rp - PLANET_RADIUS;
		float3 ray_s   = get_rayleigh_scattering(hp);
		float  mie_s   = get_mie_scattering(hp);
		float3 scat    = ray_s + float3(mie_s, mie_s, mie_s);
		float3 ext     = max(get_extinction(hp), float3(0.0000001, 0.0000001, 0.0000001));
		float3 step_tr = exp(-ext * dt);

		float  mu_s   = dot(p / rp, sun_dir);
		float3 t_sun  = get_transmittance(rp, mu_s) * planet_shadow(rp, mu_s);
		float3 ms     = get_multiscatter(rp, mu_s);
		float3 s_in   = (ray_s * p_ray + float3(mie_s * p_mie, mie_s * p_mie, mie_s * p_mie)) * t_sun + scat * ms;
		float3 s_step = (s_in - s_in * step_tr) / ext;

		lum += trans * s_step;
		trans *= step_tr;
	}
	float avg_tr = (trans.r + trans.g + trans.b) * (1.0 / 3.0);
	return float4(lum.r, lum.g, lum.b, avg_tr);
}

// Helper returning transmittance separately for exact aerial perspective
float3 integrate_atmosphere_trans(float3 ro, float3 rd, float t_max, int steps) {
	float3 od     = float3(0.0, 0.0, 0.0);
	float  t_prev = 0.0;
	for (int i = 0; i < 16; i += 1) {
		if (i >= steps) {
			break;
		}
		float f1   = float(i + 1) / float(steps);
		float t1   = t_max * lerp(f1, f1 * f1, 0.5);
		float dt   = t1 - t_prev;
		float tmid = t_prev + dt * 0.5;
		t_prev     = t1;

		float3 p = ro + rd * tmid;
		float  h = length(p) - PLANET_RADIUS;
		od += get_extinction(h) * dt;
	}
	return exp(-od);
}

#[compute, threads(8, 8, 1)]
void sky_envmap() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(target);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}

	float2 uv    = (float2(float(id.x), float(id.y)) + float2(0.5, 0.5)) / float2(float(dim.x), float(dim.y));
	float  theta = uv.x * PI2 - PI;
	float  phi   = uv.y * PI;
	// Z-up equirectangular direction matching ArmorPaint envmap convention
	float3 dir = float3(sin(phi) * cos(theta), -(sin(phi) * sin(theta)), cos(phi));

	float3 sun_dir       = normalize(constants.sun_dir_size.xyz);
	float  sun_rad       = constants.sun_dir_size.w;
	float  sun_intensity = constants.sun_params.x;
	float  sun_disc_on   = constants.sun_params.y;
	float  altitude      = clamp(constants.sun_params.z, 0.002, 60.0);
	float  ground_alb    = constants.sun_params.w;

	// Calibrated top-of-atmosphere solar irradiance for physically balanced HDRI output
	float3 solar_irr = float3(1.0, 0.96, 0.91) * (14.0 * sun_intensity);

	float3 ro         = float3(0.0, 0.0, PLANET_RADIUS + altitude);
	float2 atmo_hit   = ray_sphere(ro, dir, ATMO_RADIUS);
	float2 planet_hit = ray_sphere(ro, dir, PLANET_RADIUS);

	bool  hit_ground = planet_hit.x > 0.0;
	float t_sky_max  = atmo_hit.y;
	if (hit_ground && planet_hit.x < t_sky_max) {
		t_sky_max = planet_hit.x;
	}

	// 1. Atmosphere scattering
	float4 atmo_res = integrate_atmosphere(ro, dir, sun_dir, t_sky_max, 32, float3(1.0, 1.0, 1.0));
	float3 sky_rad  = atmo_res.rgb * solar_irr;
	float3 view_tr  = integrate_atmosphere_trans(ro, dir, t_sky_max, 12);

	// 2. Ground reflection when ray hits planet
	if (hit_ground) {
		float3 p_g        = ro + dir * t_sky_max;
		float  r_g        = length(p_g);
		float3 n_g        = p_g / r_g;
		float  ndotl      = max(dot(n_g, sun_dir), 0.0);
		float3 t_sun_g    = get_transmittance(r_g, dot(n_g, sun_dir)) * planet_shadow(r_g, dot(n_g, sun_dir));
		float3 ms_g       = get_multiscatter(r_g, dot(n_g, sun_dir));
		float3 g_col      = float3(ground_alb, ground_alb * 0.98, ground_alb * 0.95);
		float3 g_direct   = solar_irr * t_sun_g * (ndotl / PI);
		float3 g_amb      = solar_irr * ms_g * 0.35;
		float3 ground_rad = g_col * (g_direct + g_amb);
		sky_rad           = sky_rad + ground_rad * view_tr;
	}

	// 3. Solar disc with wavelength-dependent limb darkening
	float3 sun_disc_rad = float3(0.0, 0.0, 0.0);
	if (!hit_ground && sun_disc_on > 0.5) {
		float cos_s   = dot(dir, sun_dir);
		float ang     = acos(clamp(cos_s, -1.0, 1.0));
		float px_ang  = PI / float(dim.y);
		float edge_aa = max(px_ang * 0.9, sun_rad * 0.08);
		float lo_ang  = max(sun_rad - edge_aa, 0.0);
		float hi_ang  = max(sun_rad + edge_aa, px_ang * 1.1);
		float disc    = 1.0 - smoothstep(lo_ang, hi_ang, ang);
		if (disc > 0.0) {
			float  r_norm    = clamp(ang / max(sun_rad, 0.0001), 0.0, 1.0);
			float  mu_limb   = sqrt(max(0.0, 1.0 - r_norm * r_norm));
			float3 limb_u    = float3(0.42, 0.52, 0.64);
			float3 limb_col  = float3(1.0, 1.0, 1.0) - limb_u * (1.0 - mu_limb);
			float  ref_rad   = 0.545 * 0.5 * PI / 180.0;
			float  solid_scl = clamp((ref_rad * ref_rad) / max(sun_rad * sun_rad, 0.000001), 0.05, 8.0);
			sun_disc_rad     = solar_irr * view_tr * limb_col * (disc * 28.0 * solid_scl);
		}
	}

	// 4. High-Quality Volumetric 3D Cumulus + High-Altitude Fibrous Cirrus
	float  cloud_cov   = constants.cloud_params0.x;
	float  cloud_dens  = constants.cloud_params0.y;
	float  cloud_alpha = 0.0;
	float3 final_rad   = sky_rad + sun_disc_rad;

	if (cloud_cov > 0.001 && cloud_dens > 0.001) {
		float cloud_base  = constants.cloud_params0.w;
		float cloud_thick = constants.cloud_params1.y;
		float r_c0        = PLANET_RADIUS + cloud_base;
		float r_c1        = r_c0 + cloud_thick;

		float2 hit_c0 = ray_sphere(ro, dir, r_c0);
		float2 hit_c1 = ray_sphere(ro, dir, r_c1);

		float t_start = -1.0;
		float t_end   = -1.0;
		float r_obs   = PLANET_RADIUS + altitude;

		if (r_obs < r_c0) {
			if (!hit_ground && hit_c0.y > 0.0 && hit_c1.y > hit_c0.y) {
				t_start = hit_c0.y;
				t_end   = hit_c1.y;
			}
		}
		else if (r_obs > r_c1) {
			if (hit_c1.x > 0.0) {
				t_start = hit_c1.x;
				if (hit_c0.x > 0.0) {
					t_end = hit_c0.x;
				}
				else {
					t_end = hit_c1.y;
				}
			}
		}
		else {
			t_start = 0.0;
			if (hit_c0.x > 0.0) {
				t_end = hit_c0.x;
			}
			else {
				t_end = hit_c1.y;
			}
		}

		float max_cloud_dist = max(155.0, cloud_base * 36.0);
		if (hit_ground && t_sky_max < max_cloud_dist) {
			max_cloud_dist = t_sky_max;
		}

		float3 cloud_scat  = float3(0.0, 0.0, 0.0);
		float  cloud_trans = 1.0;
		float  cloud_t_avg = 0.0;
		float  cloud_w_sum = 0.0;
		float  cos_sun     = dot(dir, sun_dir);

		// Physically calibrated cool zenith sky ambient + warm ground bounce for dramatic 3D cloud contrast
		float  sun_elev_f = clamp(sun_dir.z * 3.0 + 0.10, 0.0, 1.0);
		float3 t_sun_ref  = get_transmittance(r_c0, sun_dir.z) * planet_shadow(r_c0, sun_dir.z);
		float3 ms_ref     = get_multiscatter(r_c0, sun_dir.z);
		float3 sky_top    = (ms_ref * 0.36 + float3(0.016, 0.044, 0.110) * sun_elev_f) * solar_irr;
		float3 sky_bot    = (ms_ref * 0.13 + float3(0.010, 0.015, 0.026) * sun_elev_f + t_sun_ref * (ground_alb * max(sun_dir.z, 0.0) * 0.022)) * solar_irr;

		// Physically normalized 4-octave multi-scattering phase lobes (prevents midday highlight clipping)
		float ph0 = lerp(hg_phase(cos_sun, 0.74), hg_phase(cos_sun, -0.36), 0.30);
		float ph1 = lerp(hg_phase(cos_sun, 0.36), hg_phase(cos_sun, -0.18), 0.35);
		float ph2 = lerp(hg_phase(cos_sun, 0.14), 1.0 / (4.0 * PI), 0.50);
		float ph3 = 1.0 / (4.0 * PI);

		if (t_start >= 0.0 && t_start < max_cloud_dist && t_end > t_start) {
			t_end      = min(t_end, max_cloud_dist);
			float span = min(t_end - t_start, 110.0);
			t_end      = t_start + span;

			// Continuous 256-step distance-adaptive volumetric raymarch (zero slicing or LOD jumps)
			float jitter  = ign(float2(float(id.x), float(id.y)));
			float base_ds = max(span / 240.0, 0.025);
			float t       = t_start + jitter * base_ds;
			float ext_mul = 8.2; // Crisp sculptural cumulus extinction scale (km^-1)

			for (int i = 0; i < 280; i += 1) {
				if (t >= t_end || cloud_trans < 0.004) {
					break;
				}
				// Smooth continuous step progression: fine near observer, gently wider near far horizon
				float ds = base_ds * (1.0 + (t - t_start) * 0.014);
				if (t + ds > t_end) {
					ds = t_end - t;
				}

				float  t_mid = t + ds * 0.5;
				float3 p     = ro + dir * t_mid;
				float  d     = sample_cloud_density(p, r_c0, cloud_thick, 2, t_mid);

				if (d > 0.001) {
					float sigma   = d * ext_mul;
					float step_tr = exp(-sigma * ds);

					// 8-step sun light march: 6 fine exponential internal steps + 2 long-range macro shadow steps
					float od_sun = 0.0;
					float lt     = 0.0;
					for (int j = 0; j < 8; j += 1) {
						float l_ds = cloud_thick * 0.018 * (1.0 + float(j) * float(j) * 0.65);
						lt += l_ds;
						float3 lp   = p + sun_dir * (lt - l_ds * 0.5);
						int    dlvl = 0;
						if (j < 4) {
							dlvl = 1;
						}
						od_sun += sample_cloud_density(lp, r_c0, cloud_thick, dlvl, t_mid + lt) * l_ds;
					}
					float tau_sun = od_sun * ext_mul;

					// 4-octave Wrenninge / Hillaire multi-scattering with crisp dark-edge powder effect
					float powder   = 1.0 - 0.82 * exp(-tau_sun * 2.2);
					float p_blend  = lerp(powder, 1.0, smoothstep(0.25, 0.88, cos_sun));
					float scat_dir = exp(-tau_sun) * ph0 * p_blend
					               + 0.44 * exp(-tau_sun * 0.46) * ph1
					               + 0.20 * exp(-tau_sun * 0.22) * ph2
					               + 0.08 * exp(-tau_sun * 0.10) * ph3;

					float  rp    = length(p);
					float  h_rel = clamp((rp - r_c0) / max(cloud_thick, 0.001), 0.0, 1.0);
					float3 up_n  = p / rp;
					float  mu_s  = dot(up_n, sun_dir);
					float3 t_sun = get_transmittance(rp, mu_s) * planet_shadow(rp, mu_s);

					// Volumetric vertical ambient occlusion (deep cool shadows in thick cloud interiors & bases)
					float d_up   = sample_cloud_density(p + up_n * (cloud_thick * 0.12), r_c0, cloud_thick, 0, t_mid);
					float d_up2  = sample_cloud_density(p + up_n * (cloud_thick * 0.30), r_c0, cloud_thick, 0, t_mid);
					float tau_up = (d_up * 0.14 + d_up2 * 0.24) * cloud_thick * ext_mul;
					float ao_top = exp(-tau_up * 0.60) * lerp(0.16, 1.0, smoothstep(0.0, 0.78, h_rel));
					float ao_bot = lerp(0.85, 0.15, smoothstep(0.0, 0.50, h_rel)) * exp(-tau_sun * 0.10);

					// Physically calibrated direct sun + volumetric ambient (preserves full 3D cauliflower contrast)
					float3 direct  = solar_irr * t_sun * (scat_dir * 1.48);
					float3 ambient = sky_top * ao_top + sky_bot * ao_bot;
					float3 s_rad   = (direct + ambient) * sigma;

					// Energy-conserving volumetric scattering integral
					float3 s_int = (s_rad - s_rad * step_tr) / max(sigma, 0.0001);
					cloud_scat += cloud_trans * s_int;

					float w = cloud_trans * (1.0 - step_tr);
					cloud_t_avg += t_mid * w;
					cloud_w_sum += w;
					cloud_trans *= step_tr;
				}

				t += ds;
			}
		}

		// 5. High-altitude fibrous cirrus streaks (~9.5 km) using rotated gradient FBM (zero grid artifacts)
		if (!hit_ground && r_obs < PLANET_RADIUS + 9.5 && cloud_trans > 0.01) {
			float2 hit_cir = ray_sphere(ro, dir, PLANET_RADIUS + 9.5);
			if (hit_cir.y > 0.0 && hit_cir.y < 240.0) {
				float3 pc    = ro + dir * hit_cir.y;
				float  scale = constants.cloud_params0.z;
				float  seed  = constants.cloud_params1.x;
				float3 q_raw = (pc - float3(0.0, 0.0, PLANET_RADIUS)) * (0.032 * scale) + float3(seed * 9.1, seed * 4.7, 2.3);
				float3 qc    = float3(q_raw.x * 0.45 + q_raw.y * 0.25, -q_raw.x * 0.25 + q_raw.y * 1.45, q_raw.z);
				float  w1    = perlin_fbm(qc * 0.7, 3) - 0.5;
				float  w2    = perlin_fbm(qc * 0.7 + float3(6.4, 11.2, 2.7), 3) - 0.5;
				float  c_f   = perlin_fbm(qc + float3(w1 * 1.5, w2 * 0.9, 0.0), 5);
				float  cir_d = remap_clamp(c_f, lerp(0.76, 0.50, cloud_cov), 0.92, 0.0, 1.0);
				cir_d *= clamp(cloud_dens, 0.0, 1.0) * constants.cloud_params1.z * smoothstep(0.01, 0.16, dir.z);
				if (cir_d > 0.001) {
					float  cir_a   = clamp(cir_d * 0.36, 0.0, 0.50);
					float  rc      = length(pc);
					float  mu_sc   = dot(pc / rc, sun_dir);
					float3 t_sun_c = get_transmittance(rc, mu_sc) * planet_shadow(rc, mu_sc);
					float  ph_c    = lerp(hg_phase(cos_sun, 0.62), 1.0 / (4.0 * PI), 0.50);
					float3 cir_rad = solar_irr * (t_sun_c * ph_c * 0.95 + get_multiscatter(rc, mu_sc) * 0.25);
					cloud_scat += cloud_trans * cir_rad * cir_a;
					cloud_w_sum += cloud_trans * cir_a;
					cloud_t_avg += hit_cir.y * cloud_trans * cir_a;
					cloud_trans *= (1.0 - cir_a);
				}
			}
		}

		// Smooth far-horizon fade + exact atmospheric aerial perspective
		if (cloud_trans < 0.005) {
			cloud_trans = 0.0;
		}
		cloud_alpha = clamp(1.0 - cloud_trans, 0.0, 1.0);
		if (cloud_w_sum > 0.0001) {
			float t_eff     = cloud_t_avg / cloud_w_sum;
			float dist_fade = 1.0 - smoothstep(max_cloud_dist * 0.55, max_cloud_dist, t_eff);
			cloud_alpha *= dist_fade;
			cloud_scat *= dist_fade;
			cloud_trans = 1.0 - cloud_alpha;

			float4 aerial_in = integrate_atmosphere(ro, dir, sun_dir, t_eff, 16, float3(1.0, 1.0, 1.0));
			float3 aerial_tr = integrate_atmosphere_trans(ro, dir, t_eff, 12);
			float3 cloud_out = cloud_scat * aerial_tr + aerial_in.rgb * solar_irr * cloud_alpha;

			// Composite clouds over sky + sun disc (opaque clouds fully block sun disc)
			final_rad = (sky_rad + sun_disc_rad * smoothstep(0.02, 0.15, cloud_trans)) * cloud_trans + cloud_out;
		}
	}

	// Clamp to finite positive HDR half-float safe range
	final_rad     = clamp(final_rad, float3(0.0, 0.0, 0.0), float3(60000.0, 60000.0, 60000.0));
	target[id.xy] = float4(final_rad.r, final_rad.g, final_rad.b, cloud_alpha);
}
#endif
