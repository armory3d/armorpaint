
cbuffer constants {
	float4x4 invVP;
	float3 eye;
	float4 envmap_data; // angle, sin(angle), cos(angle), strength
	int envmap_num_mipmaps;
	float2 camera_proj;
	float3 eye_look;
	float4 shirr0;
	float4 shirr1;
	float4 shirr2;
	float4 shirr3;
	float4 shirr4;
	float4 shirr5;
	float4 shirr6;
};

sampler sampler_linear;

tex2d gbufferD;

tex2d gbuffer0;

tex2d gbuffer1;

tex2d senvmap_radiance;

tex2d senvmap_radiance0;

tex2d senvmap_radiance1;

tex2d senvmap_radiance2;

tex2d senvmap_radiance3;

tex2d senvmap_radiance4;

tex2d ssaotex;

const float PI = 3.14159265358979;
const float PI2 = 6.28318530718;

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
	float4 v = float4(input.pos.xy, 1.0, 1.0);
	v = constants.invVP * v;
	v.xyz = v.xyz / v.w;
	output.view_ray = v.xyz - constants.eye;

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

	// return (1.0 - abs(v.yx)) * (float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0));
}

// void unpack_f32_i16(float val, out float f, out uint i) {
// 	// Constant optimize by compiler
// 	const int num_bit_target = 16;
// 	const int num_bit_i = 4;
// 	const float prec = float(1 << num_bit_target);
// 	const float maxi = float(1 << num_bit_i);
// 	const float prec_minus_one = prec - 1.0;
// 	const float t1 = ((prec / maxi) - 1.0) / prec_minus_one;
// 	const float t2 = (prec / maxi) / prec_minus_one;
// 	// Code
// 	// extract integer part
// 	// + rcp(prec_minus_one) to deal with precision issue
// 	i = uint((val / t2) + (1.0 / prec_minus_one));
// 	// Now that we have i, solve formula in pack_f32_i16 for f
// 	//f = (val - t2 * float(i)) / t1 => convert in mads form
// 	f = clamp((-t2 * float(i) + val) / t1, 0.0, 1.0); // Saturate in case of precision issue
// }

float3 surface_albedo(float3 base_color, float metalness) {
	return lerp(base_color, float3(0.0, 0.0, 0.0), metalness);
}

float3 surface_f0(float3 base_color, float metalness) {
	return lerp(float3(0.04, 0.04, 0.04), base_color, metalness);
}

float3 get_pos(float3 eye, float3 eye_look, float3 view_ray, float depth, float2 camera_proj) {
	// eye_look, view_ray should be normalized
	float linear_depth = camera_proj.y / (depth - camera_proj.x);
	float view_z_dist = dot(eye_look, view_ray);
	float3 wposition = eye + view_ray * (linear_depth / view_z_dist);
	return wposition;
}

const float c1 = 0.429043;
const float c2 = 0.511664;
const float c3 = 0.743125;
const float c4 = 0.886227;
const float c5 = 0.247708;

float3 sh_irradiance(float3 nor) {
	// TODO: Use padding for 4th component and pass shirr[].xyz directly
	float3 cl00 = float3(constants.shirr0.x, constants.shirr0.y, constants.shirr0.z);
	float3 cl1m1 = float3(constants.shirr0.w, constants.shirr1.x, constants.shirr1.y);
	float3 cl10 = float3(constants.shirr1.z, constants.shirr1.w, constants.shirr2.x);
	float3 cl11 = float3(constants.shirr2.y, constants.shirr2.z, constants.shirr2.w);
	float3 cl2m2 = float3(constants.shirr3.x, constants.shirr3.y, constants.shirr3.z);
	float3 cl2m1 = float3(constants.shirr3.w, constants.shirr4.x, constants.shirr4.y);
	float3 cl20 = float3(constants.shirr4.z, constants.shirr4.w, constants.shirr5.x);
	float3 cl21 = float3(constants.shirr5.y, constants.shirr5.z, constants.shirr5.w);
	float3 cl22 = float3(constants.shirr6.x, constants.shirr6.y, constants.shirr6.z);
	return (
		cl22 * c1 * (nor.x * nor.x - nor.y * nor.y) +
		cl20 * c3 * nor.z * nor.z +
		cl00 * c4 -
		cl20 * c5 +
		cl2m2 * 2.0 * c1 * nor.x * nor.y +
		cl21  * 2.0 * c1 * nor.x * nor.z +
		cl2m1 * 2.0 * c1 * nor.y * nor.z +
		cl11  * 2.0 * c2 * nor.x +
		cl1m1 * 2.0 * c2 * nor.y +
		cl10  * 2.0 * c2 * nor.z
	);
}

float mip_from_roughness(float roughness, float num_mipmaps) {
	// First mipmap level = roughness 0, last = roughness = 1
	return roughness * num_mipmaps;
}

float2 envmap_equirect(float3 normal, float angle) {
	float phi = acos(normal.z);
	float theta = atan2(-normal.y, normal.x) + PI + angle;
	return float2(theta / PI2, phi / PI);
}

float3 envmap_sample(float lod, float2 coord) {
	if (lod == 0.0) {
		return sample_lod(senvmap_radiance, sampler_linear, coord, 0.0).rgb;
	}
	if (lod == 1.0) {
		return sample_lod(senvmap_radiance0, sampler_linear, coord, 0.0).rgb;
	}
	if (lod == 2.0) {
		return sample_lod(senvmap_radiance1, sampler_linear, coord, 0.0).rgb;
	}
	if (lod == 3.0) {
		return sample_lod(senvmap_radiance2, sampler_linear, coord, 0.0).rgb;
	}
	if (lod == 4.0) {
		return sample_lod(senvmap_radiance3, sampler_linear, coord, 0.0).rgb;
	}
	return sample_lod(senvmap_radiance4, sampler_linear, coord, 0.0).rgb;
}

float specular_occlusion(float dotnv, float ao, float roughness) {
	float e = exp((-16.0 * roughness - 1.0) * log(2.0)); // exp2()
	return clamp(pow(dotnv + ao, e) - 1.0 + ao, 0.0, 1.0);
}

// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
float3 env_brdf_approx(float3 specular, float roughness, float dotnv) {
	float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
	float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
	float4 r = c0 * roughness + c1;
	float a004 = min(r.x * r.x, exp((-9.28 * dotnv) * log(2.0))) * r.x + r.y;
	float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
	return specular * ab.x + ab.y;
}

float4 frag(vert_out input) {
	// normal.xy, roughness, metallic/matid
	float4 g0 = sample_lod(gbuffer0, sampler_linear, input.tex, 0.0);

	float3 n;
	n.z = 1.0 - abs(g0.x) - abs(g0.y);
	if (n.z >= 0.0) {
		//n.xy = g0.xy;
		n.x = g0.x;
		n.y = g0.y;
	}
	else {
		//n.xy = octahedron_wrap(g0.xy);
		float2 f2 = octahedron_wrap(g0.xy);
		n.x = f2.x;
		n.y = f2.y;
	}
	n = normalize(n);

	float roughness = g0.b;
	float metallic;
	uint matid;
	// unpack_f32_i16(g0.a, metallic, matid);
	matid = uint((g0.a / 0.06250047610269868710814625956118106842041015625) + (1.0 / 65535.0));
	metallic = clamp((-0.06250047610269868710814625956118106842041015625 * float(matid) + g0.a) / 0.062485207147583624058737396240234375, 0.0, 1.0);

	float4 g1 = sample_lod(gbuffer1, sampler_linear, input.tex, 0.0); // basecolor.rgb, occ
	float3 albedo = surface_albedo(g1.rgb, metallic);
	float3 f0 = surface_f0(g1.rgb, metallic);

	float depth = sample_lod(gbufferD, sampler_linear, input.tex, 0.0).r;
	float3 p = get_pos(constants.eye, constants.eye_look, normalize(input.view_ray), depth, constants.camera_proj);
	float3 v = normalize(constants.eye - p);

	float dotnv = dot(n, v);
	if (dotnv < 0.05) {
		n = normalize(n + v * (0.05 - dotnv));
		dotnv = dot(n, v);
	}
	dotnv = max(0.0001, dotnv);

	float ao = g1.a * sample_lod(ssaotex, sampler_linear, input.tex, 0.0).r;

	// Envmap
	float3 envl = sh_irradiance(
		float3(
			n.x * constants.envmap_data.z + n.y * constants.envmap_data.y,
			n.y * constants.envmap_data.z - n.x * constants.envmap_data.y,
			n.z
		)
	);
	envl = envl / PI;

	float3 reflection_world = reflect(-v, n);
	// float lod = mip_from_roughness(roughness, float(constants.envmap_num_mipmaps));
	// float3 prefiltered_color = sample_lod(senvmap_radiance, sampler_linear, envmap_equirect(reflection_world, constants.envmap_data.x), lod).rgb;
	float lod = mip_from_roughness(roughness, 5.0);
	float lod0 = floor(lod);
	float lod1 = ceil(lod);
	float lodf = lod - lod0;
	float2 envmap_coord = envmap_equirect(reflection_world, constants.envmap_data.x);
	float3 lodc0 = envmap_sample(lod0, envmap_coord);
	float3 lodc1 = envmap_sample(lod1, envmap_coord);
	float3 prefiltered_color = lerp(lodc0, lodc1, lodf);

	// Emission - basecolor holds the emitted color, it is not an albedo
	float3 emission = float3(0.0, 0.0, 0.0);
	if (matid == uint(1.0)) { // materialid
		emission = g1.rgb;
		albedo = float3(0.0, 0.0, 0.0);
	}

	envl.rgb = envl.rgb * albedo * ao;
	envl.rgb = envl.rgb + prefiltered_color * env_brdf_approx(f0, roughness, dotnv) * specular_occlusion(dotnv, ao, roughness);
	envl.rgb = envl.rgb * constants.envmap_data.w;

	float4 color;
	color.rgb = envl.rgb + emission;

	color.rgb = max(color.rgb, float3(0.0, 0.0, 0.0));
	color.a = 1.0; // Mark as opaque
	return color;
}
