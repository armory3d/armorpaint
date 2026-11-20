#include <metal_stdlib>

using namespace metal;
using namespace raytracing;

typedef intersector<triangle_data, instancing, world_space_data> _kong_intersector;

struct _1_type {
	float4 eye;
	float4x4 inv_vp;
	float4 params;
};

struct tangent_basis {
	float3 tangent;
	float3 binormal;
};

struct sampler_cache {
	uint2 scramble;
	uint4 rank;
};

struct _kong_instance {
	constant uint *vertex_buffer;
	constant uint *index_buffer;
	uint stride; // Vertex size in bytes
	uint geometry;
};

struct _kong_geometry_textures {
	texture2d<float, access::read> texpaint0;
	texture2d<float, access::read> texpaint1;
	texture2d<float, access::read> texpaint2;
};

uint4 _kong_vertex(constant _kong_instance &instance, uint corner) {
	constant uint *v = instance.vertex_buffer + instance.index_buffer[corner] * instance.stride / 4;
	return uint4(v[0], v[1], v[2], v[3]);
}

constant float _26 = 3.14159274;

constant float _27 = 6.28318548;

constant int _32 = 16;

constant int _31 = 32768;

constant int _29 = 256;

constant int _30 = 128;

constant float _28 = 19.739208;

constant int _13 = 8;

constant int _14 = 32;

constant int _22 = 8;

constant int _15 = 2;

constant int _20 = 5;

constant int _23 = 2;

constant float _25 = 0.050000;

constant float _24 = 0.500000;

constant int _16 = 0;

constant int _19 = 3;

constant int _17 = 1;

constant int _18 = 2;

constant int _21 = 6;

sampler_cache init_sampler(uint2 _661, int _662, texture2d<float> _9, texture2d<float> _10);
uint2 table_texel(int _75);
uint table_word(float4 _118);
float rnd(uint2 _714, int _715, int _716, sampler_cache _717, int _718, texture2d<float> _8, texture2d<float> _10);
uint rank_value_at(int _144, int _145, int _146, int _147, texture2d<float> _10);
uint table_byte(float4 _88, int _89);
float rand_indexed(int _171, int _172, uint _173, uint _174, texture2d<float> _8);
float3 camera_ray_direction(float2 _258, float3 _259, constant _1_type &_1);
float3 env_radiance(float3 _782, constant _1_type &_1, texture2d<float> _7, sampler _12);
float2 equirect(float3 _200, float _201);
float env_pdf(float3 _915, constant _1_type &_1, texture2d<float> _11);
int clamp_int(int _793, int _794, int _795);
float mis_weight(float _1016, float _1017);
float2 s16_to_f32(uint _33);
float2 hit_attribute2d(float2 _63, float2 _64, float2 _65, float2 _66);
float3 uniform_sphere(float _626, float _627);
float3 hit_attribute(float3 _51, float3 _52, float3 _53, float2 _54);
float3 srgb_to_linear(float3 _297);
tangent_basis create_basis(float3 _218);
float3 sample_ggx_vndf(float3 _476, float _477, float _478, float _479);
float3 refract_dir(float3 _598, float3 _599, float _600);
float fresnel_dielectric(float _564, float _565);
float3 offset_ray(float3 _647, float3 _648, float3 _649);
float3 cos_weighted_direction(float3 _453, float3 _454, float3 _455, float _456, float _457);
tangent_basis create_uv_basis(float3 _379, float3 _380, float3 _381, float2 _382, float2 _383, float2 _384, float3 _385);
float3 surface_albedo(float3 _275, float _276);
float3 surface_specular(float3 _283, float _284);
float3 spec_directional_albedo(float3 _320, float _321, float _322);
float luma(float3 _291);
float rnd_fine(uint2 _985, int _986, int _987, sampler_cache _988, int _989, texture2d<float> _8, texture2d<float> _10);
uint hash_u32(uint _963);
float4 sample_env(float _821, float _822, constant _1_type &_1, texture2d<float> _11);
float3 env_dir(float2 _802, constant _1_type &_1);
float4 bsdf_eval(float3 _1026, float3 _1027, float3 _1028, float _1029, float3 _1030, float3 _1031, float _1032, float _1033);
float smith_lambda(float _362, float _363);
float3 f_schlick(float3 _306, float _307);
bool occluded(float3 _1110, float3 _1111, instance_acceleration_structure _2);

float2 s16_to_f32(uint _33) {
	int _36 = 16;
	int _38 = 16;
	uint _39 = _33 << _38;
	int _37 = int(_39);
	int _40 = _37 >> _36;
	int _34;
	_34 = _40;
	int _41 = 16;
	int _42 = int(_33);
	int _43 = _42 >> _41;
	int _35;
	_35 = _43;
	float _44 = 32767.000000;
	float _47 = float(_34);
	float _46 = float(_47);
	float _49 = float(_35);
	float _48 = float(_49);
	float2 _45 = float2(_46, _48);
	float2 _50 = _45 / _44;
	return _50;
}

float3 hit_attribute(float3 _51, float3 _52, float3 _53, float2 _54) {
	float3 _55 = _53 - _51;
	float _56 = _54.y;
	float3 _57 = _56 * _55;
	float3 _58 = _52 - _51;
	float _59 = _54.x;
	float3 _60 = _59 * _58;
	float3 _61 = _51 + _60;
	float3 _62 = _61 + _57;
	return _62;
}

float2 hit_attribute2d(float2 _63, float2 _64, float2 _65, float2 _66) {
	float2 _67 = _65 - _63;
	float _68 = _66.y;
	float2 _69 = _68 * _67;
	float2 _70 = _64 - _63;
	float _71 = _66.x;
	float2 _72 = _71 * _70;
	float2 _73 = _63 + _72;
	float2 _74 = _73 + _69;
	return _74;
}

uint2 table_texel(int _75) {
	int _77 = 2;
	int _78 = 131071;
	int _79 = _75 & _78;
	int _80 = _79 >> _77;
	int _76;
	_76 = _80;
	int _83 = 127;
	int _84 = _76 & _83;
	uint _82 = uint(_84);
	int _86 = 7;
	int _87 = _76 >> _86;
	uint _85 = uint(_87);
	uint2 _81 = uint2(_82, _85);
	return _81;
}

uint table_byte(float4 _88, int _89) {
	int _92 = 3;
	int _93 = _89 & _92;
	int _90;
	_90 = _93;
	float _94 = _88.w;
	float _91;
	_91 = _94;
	int _95 = 0;
	bool _96 = _90 == _95;
	if (_96)
	{
		float _99 = _88.x;
		_91 = _99;
	}
	bool _100 = !_96;
	int _101 = 1;
	bool _102 = _90 == _101;
	bool _103 = _100 && _102;
	if (_103)
	{
		float _106 = _88.y;
		_91 = _106;
	}
	bool _107 = !_102;
	bool _108 = _100 && _107;
	int _109 = 2;
	bool _110 = _90 == _109;
	bool _111 = _108 && _110;
	if (_111)
	{
		float _114 = _88.z;
		_91 = _114;
	}
	float _116 = 255.000000;
	float _117 = _91 * _116;
	uint _115 = uint(_117);
	return _115;
}

uint table_word(float4 _118) {
	int _119 = 24;
	float _121 = 255.000000;
	float _122 = _118.w;
	float _123 = _122 * _121;
	uint _120 = uint(_123);
	uint _124 = _120 << _119;
	int _125 = 16;
	float _127 = 255.000000;
	float _128 = _118.z;
	float _129 = _128 * _127;
	uint _126 = uint(_129);
	uint _130 = _126 << _125;
	int _131 = 8;
	float _133 = 255.000000;
	float _134 = _118.y;
	float _135 = _134 * _133;
	uint _132 = uint(_135);
	uint _136 = _132 << _131;
	float _138 = 255.000000;
	float _139 = _118.x;
	float _140 = _139 * _138;
	uint _137 = uint(_140);
	uint _141 = _137 | _136;
	uint _142 = _141 | _130;
	uint _143 = _142 | _124;
	return _143;
}

uint rank_value_at(int _144, int _145, int _146, int _147, texture2d<float> _10) {
	int _150 = 8;
	int _151 = 128;
	int _152 = 127;
	int _153 = 11;
	int _154 = _147 * _153;
	int _155 = _145 + _154;
	int _156 = _155 & _152;
	int _157 = _156 * _151;
	int _158 = 127;
	int _159 = 9;
	int _160 = _147 * _159;
	int _161 = _144 + _160;
	int _162 = _161 & _158;
	int _163 = _162 + _157;
	int _164 = _163 * _150;
	int _165 = 255;
	int _166 = _146 & _165;
	int _167 = _166 + _164;
	int _148;
	_148 = _167;
	uint2 _169 = table_texel(_148);
	float4 _168 = _10.read(_169);
	float4 _149;
	_149 = _168;
	uint _170 = table_byte(_149, _148);
	return _170;
}

float rand_indexed(int _171, int _172, uint _173, uint _174, texture2d<float> _8) {
	int _178 = 255;
	int _179 = _171 & _178;
	_171 = _179;
	int _180 = 255;
	int _181 = _172 & _180;
	_172 = _181;
	int _182 = int(_173);
	int _183 = _171 ^ _182;
	int _175;
	_175 = _183;
	uint _186 = uint(_175);
	uint _187 = uint(_172);
	uint2 _185 = uint2(_186, _187);
	float4 _184 = _8.read(_185);
	float4 _176;
	_176 = _184;
	float _189 = 255.000000;
	float _190 = _176.x;
	float _191 = _190 * _189;
	int _188 = int(_191);
	int _177;
	_177 = _188;
	int _192 = int(_174);
	int _193 = _177 ^ _192;
	_177 = _193;
	float _194 = 256.000000;
	float _196 = float(_177);
	float _195 = float(_196);
	float _197 = 0.500000;
	float _198 = _197 + _195;
	float _199 = _198 / _194;
	return _199;
}

float2 equirect(float3 _200, float _201) {
	float _206 = _200.z;
	float _207 = -1.000000;
	float _208 = 1.000000;
	float _205 = clamp(_206, _207, _208);
	float _204 = acos(_205);
	float _202;
	_202 = _204;
	float _210 = _200.y;
	float _211 = -_210;
	float _212 = _200.x;
	float _209 = atan2(_211, _212);
	float _213 = _209 + _26;
	float _214 = _213 + _201;
	float _203;
	_203 = _214;
	float _216 = _203 / _27;
	float _217 = _202 / _26;
	float2 _215 = float2(_216, _217);
	return _215;
}

tangent_basis create_basis(float3 _218) {
	float _223 = 1.000000;
	float _219;
	_219 = _223;
	float _224 = 0.000000;
	float _225 = _218.z;
	bool _226 = _225 < _224;
	if (_226)
	{
		float _229 = -1.000000;
		_219 = _229;
	}
	float _230 = _218.z;
	float _231 = _219 + _230;
	float _232 = -1.000000;
	float _233 = _232 / _231;
	float _220;
	_220 = _233;
	float _234 = _218.y;
	float _235 = _218.x;
	float _236 = _235 * _234;
	float _237 = _236 * _220;
	float _221;
	_221 = _237;
	tangent_basis _222;
	float _239 = _218.x;
	float _240 = _218.x;
	float _241 = _219 * _240;
	float _242 = _241 * _239;
	float _243 = _242 * _220;
	float _244 = 1.000000;
	float _245 = _244 + _243;
	float _246 = _219 * _221;
	float _247 = _218.x;
	float _248 = -_219;
	float _249 = _248 * _247;
	float3 _238 = float3(_245, _246, _249);
	_222.tangent = _238;
	float _251 = _218.y;
	float _252 = _218.y;
	float _253 = _252 * _251;
	float _254 = _253 * _220;
	float _255 = _219 + _254;
	float _256 = _218.y;
	float _257 = -_256;
	float3 _250 = float3(_221, _255, _257);
	_222.binormal = _250;
	return _222;
}

float3 camera_ray_direction(float2 _258, float3 _259, constant _1_type &_1) {
	float _263 = _258.x;
	float _264 = _258.y;
	float _265 = -_264;
	float _266 = 0.000000;
	float _267 = 1.000000;
	float4 _262 = float4(_263, _265, _266, _267);
	float4x4 _268 = _1.inv_vp;
	float4 _269 = _268 * _262;
	float4 _260;
	_260 = _269;
	float _270 = _260.w;
	float3 _271 = _260.xyz;
	float3 _272 = _271 / _270;
	float3 _261;
	_261 = _272;
	float3 _274 = _261 - _259;
	float3 _273 = normalize(_274);
	return _273;
}

float3 surface_albedo(float3 _275, float _276) {
	float _279 = 0.000000;
	float _280 = 0.000000;
	float _281 = 0.000000;
	float3 _278 = float3(_279, _280, _281);
	float3 _282 = float3(_276, _276, _276);
	float3 _277 = mix(_275, _278, _282);
	return _277;
}

float3 surface_specular(float3 _283, float _284) {
	float _287 = 0.040000;
	float _288 = 0.040000;
	float _289 = 0.040000;
	float3 _286 = float3(_287, _288, _289);
	float3 _290 = float3(_284, _284, _284);
	float3 _285 = mix(_286, _283, _290);
	return _285;
}

float luma(float3 _291) {
	float _294 = 0.212600;
	float _295 = 0.715200;
	float _296 = 0.072200;
	float3 _293 = float3(_294, _295, _296);
	float _292 = dot(_291, _293);
	return _292;
}

float3 srgb_to_linear(float3 _297) {
	float _298 = 0.0125228781;
	float _299 = 0.682171106;
	float _300 = 0.305306017;
	float3 _301 = _297 * _300;
	float3 _302 = _301 + _299;
	float3 _303 = _297 * _302;
	float3 _304 = _303 + _298;
	float3 _305 = _297 * _304;
	return _305;
}

float3 f_schlick(float3 _306, float _307) {
	float _311 = 1.000000;
	float _312 = _311 - _307;
	float _310 = saturate(_312);
	float _308;
	_308 = _310;
	float _313 = _308 * _308;
	float _309;
	_309 = _313;
	float _314 = _309 * _309;
	float _315 = _314 * _308;
	float _316 = 1.000000;
	float3 _317 = _316 - _306;
	float3 _318 = _317 * _315;
	float3 _319 = _306 + _318;
	return _319;
}

float3 spec_directional_albedo(float3 _320, float _321, float _322) {
	float _329 = -1.000000;
	float _330 = -0.027500;
	float _331 = -0.572000;
	float _332 = 0.022000;
	float4 _328 = float4(_329, _330, _331, _332);
	float4 _323;
	_323 = _328;
	float _334 = 1.000000;
	float _335 = 0.042500;
	float _336 = 1.040000;
	float _337 = -0.040000;
	float4 _333 = float4(_334, _335, _336, _337);
	float4 _324;
	_324 = _333;
	float4 _338 = _323 * _322;
	float4 _339 = _338 + _324;
	float4 _325;
	_325 = _339;
	float _340 = _325.y;
	float _341 = _325.x;
	float _343 = _325.x;
	float _344 = _325.x;
	float _345 = _344 * _343;
	float _347 = -9.280000;
	float _348 = _347 * _321;
	float _346 = exp2(_348);
	float _342 = min(_345, _346);
	float _349 = _342 * _341;
	float _350 = _349 + _340;
	float _326;
	_326 = _350;
	float2 _351 = _325.zw;
	float _353 = -1.040000;
	float _354 = 1.040000;
	float2 _352 = float2(_353, _354);
	float2 _355 = _352 * _326;
	float2 _356 = _355 + _351;
	float2 _327;
	_327 = _356;
	float _358 = _327.y;
	float _359 = _327.x;
	float3 _360 = _320 * _359;
	float3 _361 = _360 + _358;
	float3 _357 = saturate(_361);
	return _357;
}

float smith_lambda(float _362, float _363) {
	float _365 = _362 * _362;
	float _364;
	_364 = _365;
	float _366 = 1.000000;
	float _369 = 1.00000001e-07;
	float _368 = max(_364, _369);
	float _370 = 1.000000;
	float _371 = _370 - _364;
	float _372 = _363 * _371;
	float _373 = _372 / _368;
	float _374 = 1.000000;
	float _375 = _374 + _373;
	float _367 = sqrt(_375);
	float _376 = _367 - _366;
	float _377 = 0.500000;
	float _378 = _377 * _376;
	return _378;
}

tangent_basis create_uv_basis(float3 _379, float3 _380, float3 _381, float2 _382, float2 _383, float2 _384, float3 _385) {
	float3 _391 = _380 - _379;
	float3 _386;
	_386 = _391;
	float3 _392 = _381 - _379;
	float3 _387;
	_387 = _392;
	float2 _393 = _383 - _382;
	float2 _388;
	_388 = _393;
	float2 _394 = _384 - _382;
	float2 _389;
	_389 = _394;
	float _395 = _388.y;
	float _396 = _389.x;
	float _397 = _396 * _395;
	float _398 = _389.y;
	float _399 = _388.x;
	float _400 = _399 * _398;
	float _401 = _400 - _397;
	float _390;
	_390 = _401;
	float _402 = 9.99999996e-13;
	float _403 = abs(_390);
	bool _404 = _403 > _402;
	if (_404)
	{
		float _412 = 1.000000;
		float _413 = _412 / _390;
		float _405;
		_405 = _413;
		float _414 = _388.y;
		float3 _415 = _387 * _414;
		float _416 = _389.y;
		float3 _417 = _386 * _416;
		float3 _418 = _417 - _415;
		float3 _419 = _418 * _405;
		float3 _406;
		_406 = _419;
		float _420 = _389.x;
		float3 _421 = _386 * _420;
		float _422 = _388.x;
		float3 _423 = _387 * _422;
		float3 _424 = _423 - _421;
		float3 _425 = _424 * _405;
		float3 _407;
		_407 = _425;
		float _426 = dot(_385, _406);
		float3 _427 = _385 * _426;
		float3 _428 = _406 - _427;
		float3 _408;
		_408 = _428;
		float _429 = dot(_408, _408);
		float _409;
		_409 = _429;
		float _430 = 1.00000002e-16;
		bool _431 = _409 > _430;
		if (_431)
		{
			float _436 = rsqrt(_409);
			float3 _437 = _408 * _436;
			_408 = _437;
			float _438 = dot(_385, _407);
			float3 _439 = _385 * _438;
			float3 _440 = _407 - _439;
			float3 _432;
			_432 = _440;
			float _441 = dot(_408, _432);
			float3 _442 = _408 * _441;
			float3 _443 = _432 - _442;
			_432 = _443;
			float _444 = dot(_432, _432);
			float _433;
			_433 = _444;
			float _445 = 1.00000002e-16;
			bool _446 = _433 > _445;
			if (_446)
			{
				tangent_basis _447;
				_447.tangent = _408;
				float _450 = rsqrt(_433);
				float3 _451 = _432 * _450;
				_447.binormal = _451;
				return _447;
			}
		}
	}
	tangent_basis _452 = create_basis(_385);
	return _452;
}

float3 cos_weighted_direction(float3 _453, float3 _454, float3 _455, float _456, float _457) {
	float _460 = sqrt(_456);
	float _458;
	_458 = _460;
	float _461 = _27 * _457;
	float _459;
	_459 = _461;
	float _464 = 0.000000;
	float _465 = 1.000000;
	float _466 = _465 - _456;
	float _463 = max(_464, _466);
	float _462 = sqrt(_463);
	float3 _467 = _455 * _462;
	float _468 = sin(_459);
	float _469 = _458 * _468;
	float3 _470 = _454 * _469;
	float _471 = cos(_459);
	float _472 = _458 * _471;
	float3 _473 = _453 * _472;
	float3 _474 = _473 + _470;
	float3 _475 = _474 + _467;
	return _475;
}

float3 sample_ggx_vndf(float3 _476, float _477, float _478, float _479) {
	float _492 = _476.x;
	float _493 = _477 * _492;
	float _494 = _476.y;
	float _495 = _477 * _494;
	float _496 = _476.z;
	float3 _491 = float3(_493, _495, _496);
	float3 _490 = normalize(_491);
	float3 _480;
	_480 = _490;
	float _497 = _480.y;
	float _498 = _480.y;
	float _499 = _498 * _497;
	float _500 = _480.x;
	float _501 = _480.x;
	float _502 = _501 * _500;
	float _503 = _502 + _499;
	float _481;
	_481 = _503;
	float _505 = 1.000000;
	float _506 = 0.000000;
	float _507 = 0.000000;
	float3 _504 = float3(_505, _506, _507);
	float3 _482;
	_482 = _504;
	float _508 = 0.000000;
	bool _509 = _481 > _508;
	if (_509)
	{
		float _512 = rsqrt(_481);
		float _514 = _480.y;
		float _515 = -_514;
		float _516 = _480.x;
		float _517 = 0.000000;
		float3 _513 = float3(_515, _516, _517);
		float3 _518 = _513 * _512;
		_482 = _518;
	}
	float3 _519 = cross(_480, _482);
	float3 _483;
	_483 = _519;
	float _520 = sqrt(_478);
	float _484;
	_484 = _520;
	float _521 = _27 * _479;
	float _485;
	_485 = _521;
	float _522 = cos(_485);
	float _523 = _484 * _522;
	float _486;
	_486 = _523;
	float _524 = sin(_485);
	float _525 = _484 * _524;
	float _487;
	_487 = _525;
	float _526 = _480.z;
	float _527 = 1.000000;
	float _528 = _527 + _526;
	float _529 = 0.500000;
	float _530 = _529 * _528;
	float _488;
	_488 = _530;
	float _531 = _488 * _487;
	float _534 = 0.000000;
	float _535 = _486 * _486;
	float _536 = 1.000000;
	float _537 = _536 - _535;
	float _533 = max(_534, _537);
	float _532 = sqrt(_533);
	float _538 = 1.000000;
	float _539 = _538 - _488;
	float _540 = _539 * _532;
	float _541 = _540 + _531;
	_487 = _541;
	float _544 = 0.000000;
	float _545 = _487 * _487;
	float _546 = _486 * _486;
	float _547 = 1.000000;
	float _548 = _547 - _546;
	float _549 = _548 - _545;
	float _543 = max(_544, _549);
	float _542 = sqrt(_543);
	float3 _550 = _542 * _480;
	float3 _551 = _487 * _483;
	float3 _552 = _486 * _482;
	float3 _553 = _552 + _551;
	float3 _554 = _553 + _550;
	float3 _489;
	_489 = _554;
	float _557 = _489.x;
	float _558 = _477 * _557;
	float _559 = _489.y;
	float _560 = _477 * _559;
	float _562 = 0.000001;
	float _563 = _489.z;
	float _561 = max(_562, _563);
	float3 _556 = float3(_558, _560, _561);
	float3 _555 = normalize(_556);
	return _555;
}

float fresnel_dielectric(float _564, float _565) {
	float _570 = _564 * _564;
	float _571 = 1.000000;
	float _572 = _571 - _570;
	float _573 = _565 * _565;
	float _574 = _573 * _572;
	float _566;
	_566 = _574;
	float _575 = 1.000000;
	bool _576 = _566 >= _575;
	if (_576)
	{
		float _579 = 1.000000;
		return _579;
	}
	float _581 = 1.000000;
	float _582 = _581 - _566;
	float _580 = sqrt(_582);
	float _567;
	_567 = _580;
	float _583 = _565 * _564;
	float _584 = _583 + _567;
	float _585 = _565 * _564;
	float _586 = _585 - _567;
	float _587 = _586 / _584;
	float _568;
	_568 = _587;
	float _588 = _565 * _567;
	float _589 = _564 + _588;
	float _590 = _565 * _567;
	float _591 = _564 - _590;
	float _592 = _591 / _589;
	float _569;
	_569 = _592;
	float _593 = _569 * _569;
	float _594 = _568 * _568;
	float _595 = _594 + _593;
	float _596 = 0.500000;
	float _597 = _596 * _595;
	return _597;
}

float3 refract_dir(float3 _598, float3 _599, float _600) {
	float _603 = dot(_598, _599);
	float _604 = -_603;
	float _601;
	_601 = _604;
	float _605 = _601 * _601;
	float _606 = 1.000000;
	float _607 = _606 - _605;
	float _608 = _600 * _600;
	float _609 = _608 * _607;
	float _610 = 1.000000;
	float _611 = _610 - _609;
	float _602;
	_602 = _611;
	float _612 = 0.000000;
	bool _613 = _602 < _612;
	if (_613)
	{
		float _617 = 0.000000;
		float _618 = 0.000000;
		float _619 = 0.000000;
		float3 _616 = float3(_617, _618, _619);
		return _616;
	}
	float _620 = sqrt(_602);
	float _621 = _600 * _601;
	float _622 = _621 - _620;
	float3 _623 = _599 * _622;
	float3 _624 = _598 * _600;
	float3 _625 = _624 + _623;
	return _625;
}

float3 uniform_sphere(float _626, float _627) {
	float _631 = 2.000000;
	float _632 = _631 * _626;
	float _633 = 1.000000;
	float _634 = _633 - _632;
	float _628;
	_628 = _634;
	float _637 = 0.000000;
	float _638 = _628 * _628;
	float _639 = 1.000000;
	float _640 = _639 - _638;
	float _636 = max(_637, _640);
	float _635 = sqrt(_636);
	float _629;
	_629 = _635;
	float _641 = _27 * _627;
	float _630;
	_630 = _641;
	float _643 = cos(_630);
	float _644 = _629 * _643;
	float _645 = sin(_630);
	float _646 = _629 * _645;
	float3 _642 = float3(_644, _646, _628);
	return _642;
}

float3 offset_ray(float3 _647, float3 _648, float3 _649) {
	float _650 = 0.000000;
	float _651 = dot(_649, _648);
	bool _652 = _651 < _650;
	if (_652)
	{
		float _655 = 0.000100;
		float3 _656 = _648 * _655;
		float3 _657 = _647 - _656;
		return _657;
	}
	float _658 = 0.000100;
	float3 _659 = _648 * _658;
	float3 _660 = _647 + _659;
	return _660;
}

sampler_cache init_sampler(uint2 _661, int _662, texture2d<float> _9, texture2d<float> _10) {
	int _667 = 127;
	int _668 = 9;
	int _669 = _662 * _668;
	uint _671 = _661.x;
	int _670 = int(_671);
	int _672 = _670 + _669;
	int _673 = _672 & _667;
	int _663;
	_663 = _673;
	int _674 = 127;
	int _675 = 11;
	int _676 = _662 * _675;
	uint _678 = _661.y;
	int _677 = int(_678);
	int _679 = _677 + _676;
	int _680 = _679 & _674;
	int _664;
	_664 = _680;
	int _681 = 8;
	int _682 = 128;
	int _683 = _664 * _682;
	int _684 = _663 + _683;
	int _685 = _684 * _681;
	int _665;
	_665 = _685;
	sampler_cache _666;
	uint2 _689 = table_texel(_665);
	float4 _688 = _9.read(_689);
	uint _687 = table_word(_688);
	int _693 = 4;
	int _694 = _665 + _693;
	uint2 _692 = table_texel(_694);
	float4 _691 = _9.read(_692);
	uint _690 = table_word(_691);
	uint2 _686 = uint2(_687, _690);
	_666.scramble = _686;
	uint2 _698 = table_texel(_665);
	float4 _697 = _10.read(_698);
	uint _696 = table_word(_697);
	int _702 = 4;
	int _703 = _665 + _702;
	uint2 _701 = table_texel(_703);
	float4 _700 = _10.read(_701);
	uint _699 = table_word(_700);
	int _707 = 8;
	int _708 = _665 + _707;
	uint2 _706 = table_texel(_708);
	float4 _705 = _10.read(_706);
	uint _704 = table_word(_705);
	int _712 = 12;
	int _713 = _665 + _712;
	uint2 _711 = table_texel(_713);
	float4 _710 = _10.read(_711);
	uint _709 = table_word(_710);
	uint4 _695 = uint4(_696, _699, _704, _709);
	_666.rank = _695;
	return _666;
}

float rnd(uint2 _714, int _715, int _716, sampler_cache _717, int _718, texture2d<float> _8, texture2d<float> _10) {
	int _723 = 7;
	int _724 = _716 & _723;
	int _719;
	_719 = _724;
	uint _725 = _717.scramble.y;
	uint _720;
	_720 = _725;
	int _726 = 4;
	bool _727 = _719 < _726;
	if (_727)
	{
		uint _730 = _717.scramble.x;
		_720 = _730;
	}
	int _731 = 255;
	int _732 = 8;
	int _733 = 3;
	int _734 = _719 & _733;
	int _735 = _734 * _732;
	uint _736 = _720 >> _735;
	uint _737 = _736 & _731;
	uint _721;
	_721 = _737;
	uint _722;
	bool _738 = _716 < _32;
	if (_738)
	{
		int _743 = 2;
		int _744 = _716 >> _743;
		int _739;
		_739 = _744;
		uint _745 = _717.rank.w;
		uint _740;
		_740 = _745;
		int _746 = 0;
		bool _747 = _739 == _746;
		if (_747)
		{
			uint _750 = _717.rank.x;
			_740 = _750;
		}
		bool _751 = !_747;
		int _752 = 1;
		bool _753 = _739 == _752;
		bool _754 = _751 && _753;
		if (_754)
		{
			uint _757 = _717.rank.y;
			_740 = _757;
		}
		bool _758 = !_753;
		bool _759 = _751 && _758;
		int _760 = 2;
		bool _761 = _739 == _760;
		bool _762 = _759 && _761;
		if (_762)
		{
			uint _765 = _717.rank.z;
			_740 = _765;
		}
		int _766 = 255;
		int _767 = 8;
		int _768 = 3;
		int _769 = _716 & _768;
		int _770 = _769 * _767;
		uint _771 = _740 >> _770;
		uint _772 = _771 & _766;
		_722 = _772;
	}
	bool _773 = !_738;
	if (_773)
	{
		uint _778 = _714.x;
		int _777 = int(_778);
		uint _780 = _714.y;
		int _779 = int(_780);
		uint _776 = rank_value_at(_777, _779, _716, _718, _10);
		_722 = _776;
	}
	float _781 = rand_indexed(_715, _716, _722, _721, _8);
	return _781;
}

float3 env_radiance(float3 _782, constant _1_type &_1, texture2d<float> _7, sampler _12) {
	float _786 = _1.params.y;
	float2 _785 = equirect(_782, _786);
	float2 _783;
	_783 = _785;
	float _788 = 0.000000;
	float4 _787 = _7.sample(_12, _783, level(_788));
	float4 _784;
	_784 = _787;
	float _790 = _1.params.x;
	float _789 = abs(_790);
	float3 _791 = _784.xyz;
	float3 _792 = _791 * _789;
	return _792;
}

int clamp_int(int _793, int _794, int _795) {
	bool _796 = _793 < _794;
	if (_796)
	{
		return _794;
	}
	bool _799 = _793 > _795;
	if (_799)
	{
		return _795;
	}
	return _793;
}

float3 env_dir(float2 _802, constant _1_type &_1) {
	float _806 = _802.y;
	float _807 = _806 * _26;
	float _803;
	_803 = _807;
	float _808 = _1.params.y;
	float _809 = _802.x;
	float _810 = _809 * _27;
	float _811 = _810 - _26;
	float _812 = _811 - _808;
	float _804;
	_804 = _812;
	float _813 = sin(_803);
	float _805;
	_805 = _813;
	float _815 = cos(_804);
	float _816 = _805 * _815;
	float _817 = sin(_804);
	float _818 = -_805;
	float _819 = _818 * _817;
	float _820 = cos(_803);
	float3 _814 = float3(_816, _819, _820);
	return _814;
}

float4 sample_env(float _821, float _822, constant _1_type &_1, texture2d<float> _11) {
	float _834 = float(_31);
	float _833 = float(_834);
	float _835 = _821 * _833;
	float _823;
	_823 = _835;
	int _837 = int(_823);
	int _838 = 0;
	int _839 = 1;
	int _840 = _31 - _839;
	int _836 = clamp_int(_837, _838, _840);
	int _824;
	_824 = _836;
	float _842 = float(_824);
	float _841 = float(_842);
	float _843 = _823 - _841;
	float _825;
	_825 = _843;
	int _847 = 256;
	int _848 = _824 % _847;
	uint _846 = uint(_848);
	int _850 = 256;
	int _851 = _824 / _850;
	uint _849 = uint(_851);
	uint2 _845 = uint2(_846, _849);
	float4 _844 = _11.read(_845);
	float4 _826;
	_826 = _844;
	int _827;
	float _852 = 0.000000;
	float _828;
	_828 = _852;
	float _829;
	float _853 = _826.x;
	bool _854 = _822 < _853;
	if (_854)
	{
		_827 = _824;
		float _857 = 0.000000;
		float _858 = _826.x;
		bool _859 = _858 > _857;
		if (_859)
		{
			float _862 = _826.x;
			float _863 = _822 / _862;
			_828 = _863;
		}
		float _864 = _826.z;
		_829 = _864;
	}
	bool _865 = !_854;
	if (_865)
	{
		float _870 = _826.y;
		int _869 = int(_870);
		int _871 = 0;
		int _872 = 1;
		int _873 = _31 - _872;
		int _868 = clamp_int(_869, _871, _873);
		_827 = _868;
		float _874 = 1.000000;
		float _875 = _826.x;
		bool _876 = _875 < _874;
		if (_876)
		{
			float _879 = _826.x;
			float _880 = 1.000000;
			float _881 = _880 - _879;
			float _882 = _826.x;
			float _883 = _822 - _882;
			float _884 = _883 / _881;
			_828 = _884;
		}
		float _885 = _826.w;
		_829 = _885;
	}
	float _888 = float(_29);
	float _887 = float(_888);
	int _891 = 256;
	int _892 = _827 % _891;
	float _890 = float(_892);
	float _889 = float(_890);
	float _893 = _889 + _825;
	float _894 = _893 / _887;
	float _896 = float(_30);
	float _895 = float(_896);
	int _899 = 256;
	int _900 = _827 / _899;
	float _898 = float(_900);
	float _897 = float(_898);
	float _901 = _897 + _828;
	float _902 = _901 / _895;
	float2 _886 = float2(_894, _902);
	float2 _830;
	_830 = _886;
	float _904 = _830.y;
	float _905 = _904 * _26;
	float _903 = sin(_905);
	float _831;
	_831 = _903;
	float _906 = 0.000000;
	float _832;
	_832 = _906;
	float _907 = 0.000001;
	bool _908 = _831 > _907;
	if (_908)
	{
		float _911 = _28 * _831;
		float _912 = _829 / _911;
		_832 = _912;
	}
	float3 _914 = env_dir(_830, _1);
	float4 _913 = float4(_914, _832);
	return _913;
}

float env_pdf(float3 _915, constant _1_type &_1, texture2d<float> _11) {
	float _921 = 0.000000;
	float _922 = _1.params.w;
	bool _923 = _922 == _921;
	if (_923)
	{
		float _926 = 0.000000;
		return _926;
	}
	float _928 = _1.params.y;
	float2 _927 = equirect(_915, _928);
	float2 _916;
	_916 = _927;
	float _932 = float(_29);
	float _931 = float(_932);
	float _934 = _916.x;
	float _933 = fract(_934);
	float _935 = _933 * _931;
	int _930 = int(_935);
	int _936 = 0;
	int _937 = 1;
	int _938 = _29 - _937;
	int _929 = clamp_int(_930, _936, _938);
	int _917;
	_917 = _929;
	float _942 = float(_30);
	float _941 = float(_942);
	float _943 = _916.y;
	float _944 = _943 * _941;
	int _940 = int(_944);
	int _945 = 0;
	int _946 = 1;
	int _947 = _30 - _946;
	int _939 = clamp_int(_940, _945, _947);
	int _918;
	_918 = _939;
	uint _950 = uint(_917);
	uint _951 = uint(_918);
	uint2 _949 = uint2(_950, _951);
	float4 _948 = _11.read(_949);
	float4 _919;
	_919 = _948;
	float _953 = _916.y;
	float _954 = _953 * _26;
	float _952 = sin(_954);
	float _920;
	_920 = _952;
	float _955 = 0.000001;
	bool _956 = _920 > _955;
	if (_956)
	{
		float _959 = _28 * _920;
		float _960 = _919.z;
		float _961 = _960 / _959;
		return _961;
	}
	float _962 = 0.000000;
	return _962;
}

uint hash_u32(uint _963) {
	int _965 = 16;
	uint _964 = uint(_965);
	uint _966 = _963 >> _964;
	int _968 = 61;
	uint _967 = uint(_968);
	uint _969 = _963 ^ _967;
	uint _970 = _969 ^ _966;
	_963 = _970;
	int _972 = 9;
	uint _971 = uint(_972);
	uint _973 = _963 * _971;
	_963 = _973;
	int _975 = 4;
	uint _974 = uint(_975);
	uint _976 = _963 >> _974;
	uint _977 = _963 ^ _976;
	_963 = _977;
	int _979 = 668265261;
	uint _978 = uint(_979);
	uint _980 = _963 * _978;
	_963 = _980;
	int _982 = 15;
	uint _981 = uint(_982);
	uint _983 = _963 >> _981;
	uint _984 = _963 ^ _983;
	_963 = _984;
	return _963;
}

float rnd_fine(uint2 _985, int _986, int _987, sampler_cache _988, int _989, texture2d<float> _8, texture2d<float> _10) {
	uint _996 = uint(_987);
	uint _995 = hash_u32(_996);
	uint _997 = uint(_986);
	uint _998 = _997 ^ _995;
	uint _994 = hash_u32(_998);
	uint _999 = _985.y;
	uint _1000 = _999 ^ _994;
	uint _993 = hash_u32(_1000);
	uint _1001 = _985.x;
	uint _1002 = _1001 ^ _993;
	uint _992 = hash_u32(_1002);
	uint _990;
	_990 = _992;
	float _1003 = 16777216.000000;
	int _1007 = 8;
	uint _1006 = uint(_1007);
	uint _1008 = _990 >> _1006;
	float _1005 = float(_1008);
	float _1004 = float(_1005);
	float _1009 = _1004 / _1003;
	float _991;
	_991 = _1009;
	float _1010 = 256.000000;
	float _1011 = 0.500000;
	float _1012 = _991 - _1011;
	float _1013 = _1012 / _1010;
	float _1014 = rnd(_985, _986, _987, _988, _989, _8, _10);
	float _1015 = _1014 + _1013;
	return _1015;
}

float mis_weight(float _1016, float _1017) {
	float _1020 = _1016 * _1016;
	float _1018;
	_1018 = _1020;
	float _1021 = _1017 * _1017;
	float _1019;
	_1019 = _1021;
	float _1023 = _1018 + _1019;
	float _1024 = 9.99999972e-10;
	float _1022 = max(_1023, _1024);
	float _1025 = _1018 / _1022;
	return _1025;
}

float4 bsdf_eval(float3 _1026, float3 _1027, float3 _1028, float _1029, float3 _1030, float3 _1031, float _1032, float _1033) {
	float _1048 = dot(_1027, _1026);
	float _1034;
	_1034 = _1048;
	float _1049 = 0.000000;
	bool _1050 = _1034 <= _1049;
	if (_1050)
	{
		float _1054 = 0.000000;
		float _1055 = 0.000000;
		float _1056 = 0.000000;
		float _1057 = 0.000000;
		float4 _1053 = float4(_1054, _1055, _1056, _1057);
		return _1053;
	}
	float3 _1059 = _1028 + _1026;
	float3 _1058 = normalize(_1059);
	float3 _1035;
	_1035 = _1058;
	float _1061 = dot(_1027, _1035);
	float _1062 = 0.000000;
	float _1060 = max(_1061, _1062);
	float _1036;
	_1036 = _1060;
	float _1064 = dot(_1028, _1035);
	float _1065 = 0.000000;
	float _1063 = max(_1064, _1065);
	float _1037;
	_1037 = _1063;
	float _1066 = _1032 * _1032;
	float _1038;
	_1038 = _1066;
	float _1067 = 1.000000;
	float _1068 = 1.000000;
	float _1069 = _1038 - _1068;
	float _1070 = _1036 * _1036;
	float _1071 = _1070 * _1069;
	float _1072 = _1071 + _1067;
	float _1039;
	_1039 = _1072;
	float _1074 = _26 * _1039;
	float _1075 = _1074 * _1039;
	float _1076 = 9.99999972e-10;
	float _1073 = max(_1075, _1076);
	float _1077 = _1038 / _1073;
	float _1040;
	_1040 = _1077;
	float _1078 = smith_lambda(_1029, _1038);
	float _1041;
	_1041 = _1078;
	float _1079 = smith_lambda(_1034, _1038);
	float _1042;
	_1042 = _1079;
	float3 _1080 = f_schlick(_1031, _1037);
	float3 _1043;
	_1043 = _1080;
	float _1082 = 4.000000;
	float _1083 = _1082 * _1029;
	float _1084 = _1083 * _1034;
	float _1085 = 9.99999972e-10;
	float _1081 = max(_1084, _1085);
	float _1086 = 1.000000;
	float _1087 = _1086 + _1041;
	float _1088 = _1087 + _1042;
	float _1089 = _1088 * _1081;
	float _1090 = _1040 / _1089;
	float3 _1091 = _1043 * _1090;
	float3 _1044;
	_1044 = _1091;
	float3 _1092 = _1030 / _26;
	float3 _1045;
	_1045 = _1092;
	float _1094 = 4.000000;
	float _1095 = _1094 * _1029;
	float _1096 = 9.99999972e-10;
	float _1093 = max(_1095, _1096);
	float _1097 = 1.000000;
	float _1098 = _1097 + _1041;
	float _1099 = _1098 * _1093;
	float _1100 = _1040 / _1099;
	float _1046;
	_1046 = _1100;
	float _1101 = _1034 / _26;
	float _1047;
	_1047 = _1101;
	float3 _1103 = _1045 + _1044;
	float3 _1104 = _1103 * _1034;
	float _1105 = 1.000000;
	float _1106 = _1105 - _1033;
	float _1107 = _1106 * _1047;
	float _1108 = _1033 * _1046;
	float _1109 = _1108 + _1107;
	float4 _1102 = float4(_1104, _1109);
	return _1102;
}

bool occluded(float3 _1110, float3 _1111, instance_acceleration_structure _2) {
	ray _1112;
	_1112.origin = _1110;
	_1112.direction = _1111;
	float _1114 = 0.000100;
	_1112.min_distance = _1114;
	float _1115 = 100.000000;
	_1112.max_distance = _1115;
	_kong_intersector::result_type _1113;
	{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(true); _1113 = i.intersect(_1112, _2); }
	bool _1117 = _1113.type == intersection_type::triangle;
	return _1117;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _7 [[texture(4)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]], texture2d<float> _11 [[texture(8)]], sampler _12 [[sampler(0)]], constant _kong_instance *_kong_instances [[buffer(2)]], constant _kong_geometry_textures *_kong_geometry_textures [[buffer(3)]]) {
	uint3 _1126 = _kong_dispatch_thread_id;
	uint3 _1118;
	_1118 = _1126;
	uint2 _1127 = uint2(_3.get_width(), _3.get_height());
	uint2 _1119;
	_1119 = _1127;
	uint _1128 = _1119.y;
	uint _1129 = _1118.y;
	bool _1130 = _1129 >= _1128;
	uint _1131 = _1119.x;
	uint _1132 = _1118.x;
	bool _1133 = _1132 >= _1131;
	bool _1134 = _1133 || _1130;
	if (_1134)
	{
		return;
	}
	float _1138 = _1.eye.w;
	int _1137 = int(_1138);
	int _1120;
	_1120 = _1137;
	uint2 _1140 = _1118.xy;
	sampler_cache _1139 = init_sampler(_1140, _1120, _9, _10);
	sampler_cache _1121;
	_1121 = _1139;
	float _1142 = 0.000000;
	float _1143 = 0.000000;
	float _1144 = 0.000000;
	float3 _1141 = float3(_1142, _1143, _1144);
	float3 _1122;
	_1122 = _1141;
	{
		int _1148 = 0;
		int _1145;
		_1145 = _1148;
		while (true)
{
		bool _1152 = _1145 < _13;
		if (!_1152) { break; }
		{
			int _1164 = _1120 * _13;
			int _1165 = _1164 + _1145;
			int _1153;
			_1153 = _1165;
			uint2 _1167 = _1118.xy;
			float2 _1166 = float2(_1167);
			float2 _1154;
			_1154 = _1166;
			uint2 _1169 = _1118.xy;
			int _1170 = 0;
			float _1168 = rnd(_1169, _1153, _1170, _1121, _1120, _8, _10);
			_1154.x += _1168;
			uint2 _1172 = _1118.xy;
			int _1173 = 1;
			float _1171 = rnd(_1172, _1153, _1173, _1121, _1120, _8, _10);
			_1154.y += _1171;
			ray _1155;
			float _1174 = 0.000100;
			_1155.min_distance = _1174;
			float _1175 = 100.000000;
			_1155.max_distance = _1175;
			float3 _1176 = _1.eye.xyz;
			_1155.origin = _1176;
			float _1178 = 1.000000;
			float _1179 = 2.000000;
			float2 _1180 = float2(_1119);
			float2 _1181 = _1154 / _1180;
			float2 _1182 = _1181 * _1179;
			float2 _1183 = _1182 - _1178;
			float3 _1184 = _1.eye.xyz;
			float3 _1177 = camera_ray_direction(_1183, _1184, _1);
			_1155.direction = _1177;
			float _1186 = 1.000000;
			float _1187 = 1.000000;
			float _1188 = 1.000000;
			float3 _1185 = float3(_1186, _1187, _1188);
			float3 _1156;
			_1156 = _1185;
			float _1189 = -1.000000;
			float _1157;
			_1157 = _1189;
			int _1190 = 0;
			int _1158;
			_1158 = _1190;
			int _1191 = 0;
			int _1159;
			_1159 = _1191;
			float _1192 = 0.000000;
			float _1160;
			_1160 = _1192;
			float _1194 = 1.000000;
			float _1195 = 1.000000;
			float _1196 = 1.000000;
			float3 _1193 = float3(_1194, _1195, _1196);
			float3 _1161;
			_1161 = _1193;
			{
				int _1200 = 0;
				int _1197;
				_1197 = _1200;
				while (true)
{
				bool _1204 = _1197 < _14;
				if (!_1204) { break; }
				{
					int _1260 = _1197 * _22;
					int _1261 = _15 + _1260;
					int _1205;
					_1205 = _1261;
					_kong_intersector::result_type _1206;
					{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _1206 = i.intersect(_1155, _2); }
					bool _1263 = _1206.type == intersection_type::triangle;
					bool _1264 = !_1263;
					if (_1264)
					{
						float3 _1265;
						float _1269 = 1.000000;
						float _1266;
						_1266 = _1269;
						float _1270 = 0.000000;
						float _1271 = _1.params.x;
						bool _1272 = _1271 < _1270;
						int _1273 = 0;
						bool _1274 = _1197 == _1273;
						bool _1275 = _1274 && _1272;
						if (_1275)
						{
							float _1279 = 0.027500;
							float _1280 = 0.027500;
							float _1281 = 0.027500;
							float3 _1278 = float3(_1279, _1280, _1281);
							_1265 = _1278;
						}
						bool _1282 = !_1275;
						if (_1282)
						{
							float3 _1286 = _1155.direction;
							float3 _1285 = env_radiance(_1286, _1, _7, _12);
							_1265 = _1285;
							float _1287 = 0.000000;
							bool _1288 = _1157 > _1287;
							if (_1288)
							{
								float3 _1293 = _1155.direction;
								float _1292 = env_pdf(_1293, _1, _11);
								float _1291 = mis_weight(_1157, _1292);
								_1266 = _1291;
							}
						}
						float3 _1295 = _1156 * _1265;
						float3 _1296 = _1295 * _1266;
						float _1297 = 0.000000;
						float3 _1298 = float3(_1297, _1297, _1297);
						float _1299 = 8.000000;
						float3 _1300 = float3(_1299, _1299, _1299);
						float3 _1294 = clamp(_1296, _1298, _1300);
						_1122 += _1294;
						break;
					}
					uint _1301 = _kong_instances[_1206.user_instance_id].geometry;
					uint _1207;
					_1207 = _1301;
					float2 _1302 = _1206.triangle_barycentric_coord;
					float2 _1208;
					_1208 = _1302;
					int _1304 = 0;
					uint4 _1303 = _kong_vertex(_kong_instances[_1206.user_instance_id], _1206.primitive_id * 3 + _1304);
					uint4 _1209;
					_1209 = _1303;
					int _1306 = 1;
					uint4 _1305 = _kong_vertex(_kong_instances[_1206.user_instance_id], _1206.primitive_id * 3 + _1306);
					uint4 _1210;
					_1210 = _1305;
					int _1308 = 2;
					uint4 _1307 = _kong_vertex(_kong_instances[_1206.user_instance_id], _1206.primitive_id * 3 + _1308);
					uint4 _1211;
					_1211 = _1307;
					uint _1310 = _1209.w;
					float2 _1309 = s16_to_f32(_1310);
					float2 _1212;
					_1212 = _1309;
					uint _1312 = _1210.w;
					float2 _1311 = s16_to_f32(_1312);
					float2 _1213;
					_1213 = _1311;
					uint _1314 = _1211.w;
					float2 _1313 = s16_to_f32(_1314);
					float2 _1214;
					_1214 = _1313;
					float _1315 = _1.params.z;
					float2 _1316 = hit_attribute2d(_1212, _1213, _1214, _1208);
					float2 _1317 = _1316 * _1315;
					float2 _1215;
					_1215 = _1317;
					uint2 _1318 = uint2(_kong_geometry_textures[_1207].texpaint0.get_width(), _kong_geometry_textures[_1207].texpaint0.get_height());
					uint2 _1216;
					_1216 = _1318;
					float2 _1320 = float2(_1216);
					float2 _1321 = fract(_1215);
					float2 _1322 = _1321 * _1320;
					uint2 _1319 = uint2(_1322);
					uint2 _1217;
					_1217 = _1319;
					float4 _1323 = _kong_geometry_textures[_1207].texpaint0.read(_1217);
					float4 _1218;
					_1218 = _1323;
					float _1324 = _1206.distance;
					float _1219;
					_1219 = _1324;
					float3 _1325 = _1155.direction;
					float3 _1326 = _1325 * _1219;
					float3 _1327 = _1155.origin;
					float3 _1328 = _1327 + _1326;
					float3 _1220;
					_1220 = _1328;
					int _1329 = 0;
					bool _1330 = _1159 > _1329;
					if (_1330)
					{
						float _1336 = float(_1159);
						float _1335 = float(_1336);
						float _1337 = _1160 * _1335;
						float _1331;
						_1331 = _1337;
						float _1338 = 1.000000;
						float _1339 = _1219 + _1338;
						float _1332;
						_1332 = _1339;
						float _1340 = 0.000000;
						bool _1341 = _1331 > _1340;
						if (_1341)
						{
							uint2 _1347 = _1118.xy;
							int _1348 = _1205 + _20;
							float _1346 = rnd(_1347, _1153, _1348, _1121, _1120, _8, _10);
							float _1349 = 1.000000;
							float _1350 = _1349 - _1346;
							float _1351 = 1.00000001e-07;
							float _1345 = max(_1350, _1351);
							float _1344 = log(_1345);
							float _1352 = -_1344;
							float _1353 = _1352 / _1331;
							_1332 = _1353;
						}
						bool _1354 = _1332 < _1219;
						if (_1354)
						{
							float _1360 = 0.001000;
							float _1361 = 0.001000;
							float _1362 = 0.001000;
							float3 _1359 = float3(_1360, _1361, _1362);
							float3 _1358 = max(_1161, _1359);
							float3 _1363 = float3(_1332, _1332, _1332);
							float3 _1357 = pow(_1358, _1363);
							_1156 *= _1357;
							bool _1364 = _1158 >= _23;
							if (_1364)
							{
								float _1371 = _1156.x;
								float _1372 = _1156.y;
								float _1370 = max(_1371, _1372);
								float _1373 = _1156.z;
								float _1369 = max(_1370, _1373);
								float _1368 = clamp(_1369, _25, _24);
								float _1365;
								_1365 = _1368;
								uint2 _1375 = _1118.xy;
								int _1376 = _1205 + _16;
								float _1374 = rnd(_1375, _1153, _1376, _1121, _1120, _8, _10);
								bool _1377 = _1374 > _1365;
								if (_1377)
								{
									break;
								}
								float3 _1380 = _1156 / _1365;
								_1156 = _1380;
							}
							int _1381 = 1;
							_1158 += _1381;
							float3 _1382 = _1155.direction;
							float3 _1383 = _1382 * _1332;
							float3 _1384 = _1155.origin;
							float3 _1385 = _1384 + _1383;
							_1155.origin = _1385;
							uint2 _1388 = _1118.xy;
							int _1389 = 1;
							int _1390 = _1205 + _20;
							int _1391 = _1390 + _1389;
							float _1387 = rnd(_1388, _1153, _1391, _1121, _1120, _8, _10);
							uint2 _1393 = _1118.xy;
							int _1394 = 2;
							int _1395 = _1205 + _20;
							int _1396 = _1395 + _1394;
							float _1392 = rnd(_1393, _1153, _1396, _1121, _1120, _8, _10);
							float3 _1386 = uniform_sphere(_1387, _1392);
							_1155.direction = _1386;
							float _1397 = -1.000000;
							_1157 = _1397;
							int _1398 = 1;
							_1197 += _1398;
							continue;
						}
						float _1402 = 0.001000;
						float _1403 = 0.001000;
						float _1404 = 0.001000;
						float3 _1401 = float3(_1402, _1403, _1404);
						float3 _1400 = max(_1161, _1401);
						float3 _1405 = float3(_1219, _1219, _1219);
						float3 _1399 = pow(_1400, _1405);
						_1156 *= _1399;
					}
					uint _1407 = _1209.y;
					float2 _1406 = s16_to_f32(_1407);
					float2 _1221;
					_1221 = _1406;
					uint _1409 = _1210.y;
					float2 _1408 = s16_to_f32(_1409);
					float2 _1222;
					_1222 = _1408;
					uint _1411 = _1211.y;
					float2 _1410 = s16_to_f32(_1411);
					float2 _1223;
					_1223 = _1410;
					uint _1414 = _1209.x;
					float2 _1413 = s16_to_f32(_1414);
					float _1415 = _1221.x;
					float3 _1412 = float3(_1413, _1415);
					float3 _1224;
					_1224 = _1412;
					uint _1418 = _1210.x;
					float2 _1417 = s16_to_f32(_1418);
					float _1419 = _1222.x;
					float3 _1416 = float3(_1417, _1419);
					float3 _1225;
					_1225 = _1416;
					uint _1422 = _1211.x;
					float2 _1421 = s16_to_f32(_1422);
					float _1423 = _1223.x;
					float3 _1420 = float3(_1421, _1423);
					float3 _1226;
					_1226 = _1420;
					uint _1426 = _1209.z;
					float2 _1425 = s16_to_f32(_1426);
					float _1427 = _1221.y;
					float3 _1424 = float3(_1425, _1427);
					float3 _1227;
					_1227 = _1424;
					uint _1430 = _1210.z;
					float2 _1429 = s16_to_f32(_1430);
					float _1431 = _1222.y;
					float3 _1428 = float3(_1429, _1431);
					float3 _1228;
					_1228 = _1428;
					uint _1434 = _1211.z;
					float2 _1433 = s16_to_f32(_1434);
					float _1435 = _1223.y;
					float3 _1432 = float3(_1433, _1435);
					float3 _1229;
					_1229 = _1432;
					float3 _1437 = hit_attribute(_1227, _1228, _1229, _1208);
					float3 _1436 = normalize(_1437);
					float3 _1230;
					_1230 = _1436;
					float3 _1439 = _1225 - _1224;
					float3 _1440 = _1226 - _1224;
					float3 _1438 = cross(_1439, _1440);
					float3 _1231;
					_1231 = _1438;
					float _1441 = 9.99999968e-21;
					float _1442 = dot(_1231, _1231);
					bool _1443 = _1442 > _1441;
					if (_1443)
					{
						float3 _1446 = normalize(_1231);
						_1231 = _1446;
					}
					bool _1447 = !_1443;
					if (_1447)
					{
						_1231 = _1230;
					}
					float _1450 = 0.000000;
					float _1451 = dot(_1231, _1230);
					bool _1452 = _1451 < _1450;
					if (_1452)
					{
						float3 _1455 = -_1231;
						_1231 = _1455;
					}
					float3 _1232;
					_1232 = _1230;
					float3x3 _1456 = float3x3(_1206.object_to_world_transform[0], _1206.object_to_world_transform[1], _1206.object_to_world_transform[2]);
					float3x3 _1233;
					_1233 = _1456;
					float3 _1458 = _1233 * _1230;
					float3 _1457 = normalize(_1458);
					_1230 = _1457;
					float3 _1460 = _1233 * _1231;
					float3 _1459 = normalize(_1460);
					_1231 = _1459;
					float _1461 = 0.000000;
					float3 _1463 = _1155.direction;
					float _1462 = dot(_1231, _1463);
					bool _1464 = _1462 > _1461;
					bool _1234;
					_1234 = _1464;
					if (_1234)
					{
						float3 _1467 = -_1231;
						_1231 = _1467;
						float3 _1468 = -_1230;
						_1230 = _1468;
					}
					bool _1469 = true;
					bool _1235;
					_1235 = _1469;
					float _1471 = 0.000000;
					float _1472 = 0.000000;
					float _1473 = 0.000000;
					float _1474 = 0.000000;
					float4 _1470 = float4(_1471, _1472, _1473, _1474);
					float4 _1236;
					_1236 = _1470;
					if (_1235)
					{
						float4 _1477 = _kong_geometry_textures[_1207].texpaint1.read(_1217);
						_1236 = _1477;
					}
					float3 _1479 = _1218.xyz;
					float3 _1478 = srgb_to_linear(_1479);
					float3 _1237;
					_1237 = _1478;
					int _1480 = 2;
					int _1481 = 3;
					float _1483 = 255.000000;
					float _1484 = _1236.w;
					float _1485 = _1484 * _1483;
					int _1482 = int(_1485);
					int _1486 = _1482 % _1481;
					bool _1487 = _1486 == _1480;
					bool _1238;
					_1238 = _1487;
					bool _1488 = _1206.triangle_front_facing;
					bool _1489 = !_1488;
					bool _1490 = !_1238;
					bool _1491 = _1490 && _1489;
					if (_1491)
					{
						float _1496 = 0.001000;
						float3 _1497 = float3(_1496, _1496, _1496);
						float3 _1495 = max(_1237, _1497);
						float _1498 = _1218.w;
						float _1499 = _1219 * _1498;
						float3 _1500 = float3(_1499, _1499, _1499);
						float3 _1494 = pow(_1495, _1500);
						_1156 *= _1494;
					}
					int _1501 = 1;
					int _1502 = 3;
					float _1504 = 255.000000;
					float _1505 = _1236.w;
					float _1506 = _1505 * _1504;
					int _1503 = int(_1506);
					int _1507 = _1503 % _1502;
					bool _1508 = _1507 == _1501;
					if (_1508)
					{
						float _1511 = 100.000000;
						float3 _1512 = _1156 * _1237;
						float3 _1513 = _1512 * _1511;
						_1122 += _1513;
						break;
					}
					float4 _1514 = _kong_geometry_textures[_1207].texpaint2.read(_1217);
					float4 _1239;
					_1239 = _1514;
					if (_1238)
					{
						float _1521 = _1239.z;
						float _1522 = 1.000000;
						float _1523 = _1522 + _1521;
						float _1515;
						_1515 = _1523;
						bool _1524 = !_1234;
						bool _1516;
						_1516 = _1524;
						bool _1525 = true;
						bool _1517;
						_1517 = _1525;
						float3 _1526 = _1155.direction;
						float3 _1518;
						_1518 = _1526;
						float _1527 = 1.001000;
						bool _1528 = _1515 > _1527;
						if (_1528)
						{
							float3 _1540 = _1155.direction;
							float3 _1541 = -_1540;
							float3 _1529;
							_1529 = _1541;
							tangent_basis _1542 = create_basis(_1230);
							tangent_basis _1530;
							_1530 = _1542;
							float _1544 = _1239.y;
							float _1545 = _1239.y;
							float _1546 = _1545 * _1544;
							float _1547 = 0.001000;
							float _1543 = max(_1546, _1547);
							float _1531;
							_1531 = _1543;
							float3 _1550 = _1530.tangent;
							float _1549 = dot(_1529, _1550);
							float3 _1552 = _1530.binormal;
							float _1551 = dot(_1529, _1552);
							float _1554 = dot(_1529, _1230);
							float _1555 = 0.001000;
							float _1553 = max(_1554, _1555);
							float3 _1548 = float3(_1549, _1551, _1553);
							float3 _1532;
							_1532 = _1548;
							uint2 _1558 = _1118.xy;
							int _1559 = _1205 + _19;
							float _1557 = rnd(_1558, _1153, _1559, _1121, _1120, _8, _10);
							uint2 _1561 = _1118.xy;
							int _1562 = 1;
							int _1563 = _1205 + _19;
							int _1564 = _1563 + _1562;
							float _1560 = rnd(_1561, _1153, _1564, _1121, _1120, _8, _10);
							float3 _1556 = sample_ggx_vndf(_1532, _1531, _1557, _1560);
							float3 _1533;
							_1533 = _1556;
							float _1566 = _1533.z;
							float3 _1567 = _1230 * _1566;
							float _1568 = _1533.y;
							float3 _1569 = _1530.binormal;
							float3 _1570 = _1569 * _1568;
							float _1571 = _1533.x;
							float3 _1572 = _1530.tangent;
							float3 _1573 = _1572 * _1571;
							float3 _1574 = _1573 + _1570;
							float3 _1575 = _1574 + _1567;
							float3 _1565 = normalize(_1575);
							float3 _1534;
							_1534 = _1565;
							float _1576 = 1.000000;
							float _1577 = _1576 / _1515;
							float _1535;
							_1535 = _1577;
							bool _1578 = !_1516;
							if (_1578)
							{
								_1535 = _1515;
							}
							float _1582 = dot(_1529, _1534);
							float _1583 = 0.000000;
							float _1581 = max(_1582, _1583);
							float _1536;
							_1536 = _1581;
							float3 _1585 = _1155.direction;
							float3 _1584 = refract_dir(_1585, _1534, _1535);
							float3 _1537;
							_1537 = _1584;
							float _1586 = 0.000000;
							float _1587 = dot(_1537, _1537);
							bool _1588 = _1587 == _1586;
							float _1589 = fresnel_dielectric(_1536, _1535);
							uint2 _1591 = _1118.xy;
							int _1592 = _1205 + _17;
							float _1590 = rnd(_1591, _1153, _1592, _1121, _1120, _8, _10);
							bool _1593 = _1590 < _1589;
							bool _1594 = _1593 || _1588;
							if (_1594)
							{
								float3 _1598 = _1155.direction;
								float3 _1597 = reflect(_1598, _1534);
								_1518 = _1597;
								bool _1599 = false;
								_1517 = _1599;
								float _1600 = 0.000000;
								float _1601 = dot(_1518, _1231);
								bool _1602 = _1601 <= _1600;
								if (_1602)
								{
									float3 _1606 = _1155.direction;
									float3 _1605 = reflect(_1606, _1231);
									_1518 = _1605;
								}
							}
							bool _1607 = !_1594;
							if (_1607)
							{
								float3 _1610 = normalize(_1537);
								_1518 = _1610;
								float _1611 = 0.000000;
								float _1612 = dot(_1518, _1231);
								bool _1613 = _1612 >= _1611;
								if (_1613)
								{
									float3 _1616 = _1155.direction;
									_1518 = _1616;
								}
							}
						}
						if (_1517)
						{
							if (_1516)
							{
								int _1621 = 1;
								_1159 += _1621;
								float _1622 = 16.000000;
								float _1623 = _1239.x;
								float _1624 = 1.000000;
								float _1625 = _1624 - _1623;
								float _1626 = _1239.x;
								float _1627 = 1.000000;
								float _1628 = _1627 - _1626;
								float _1629 = _1628 * _1625;
								float _1630 = _1629 * _1622;
								_1160 = _1630;
								_1161 = _1237;
							}
							bool _1631 = !_1516;
							int _1632 = 0;
							bool _1633 = _1159 > _1632;
							bool _1634 = _1631 && _1633;
							if (_1634)
							{
								int _1637 = 1;
								_1159 -= _1637;
							}
						}
						_1155.direction = _1518;
						float3 _1638 = offset_ray(_1220, _1231, _1518);
						_1155.origin = _1638;
						float _1639 = -1.000000;
						_1157 = _1639;
						int _1640 = 1;
						_1197 += _1640;
						continue;
					}
					uint2 _1642 = _1118.xy;
					int _1643 = _1205 + _17;
					float _1641 = rnd(_1642, _1153, _1643, _1121, _1120, _8, _10);
					float _1240;
					_1240 = _1641;
					float _1644 = _1218.w;
					bool _1645 = _1240 > _1644;
					if (_1645)
					{
						float3 _1651 = _1155.direction;
						tangent_basis _1650 = create_basis(_1651);
						tangent_basis _1646;
						_1646 = _1650;
						float3 _1653 = _1646.tangent;
						float3 _1654 = _1646.binormal;
						float3 _1655 = _1155.direction;
						uint2 _1657 = _1118.xy;
						int _1658 = _1205 + _19;
						float _1656 = rnd(_1657, _1153, _1658, _1121, _1120, _8, _10);
						uint2 _1660 = _1118.xy;
						int _1661 = 1;
						int _1662 = _1205 + _19;
						int _1663 = _1662 + _1661;
						float _1659 = rnd(_1660, _1153, _1663, _1121, _1120, _8, _10);
						float3 _1652 = cos_weighted_direction(_1653, _1654, _1655, _1656, _1659);
						float3 _1647;
						_1647 = _1652;
						float3 _1666 = _1155.direction;
						float _1667 = 0.500000;
						float _1668 = _1239.y;
						float _1669 = _1239.y;
						float _1670 = _1669 * _1668;
						float _1671 = _1670 * _1667;
						float3 _1672 = float3(_1671, _1671, _1671);
						float3 _1665 = mix(_1666, _1647, _1672);
						float3 _1664 = normalize(_1665);
						_1155.direction = _1664;
						float3 _1674 = _1155.direction;
						float3 _1673 = offset_ray(_1220, _1231, _1674);
						_1155.origin = _1673;
						float _1675 = -1.000000;
						_1157 = _1675;
						int _1676 = 1;
						_1197 += _1676;
						continue;
					}
					uint2 _1678 = _1118.xy;
					int _1679 = _1205 + _18;
					float _1677 = rnd(_1678, _1153, _1679, _1121, _1120, _8, _10);
					_1240 = _1677;
					bool _1680 = _1158 >= _23;
					if (_1680)
					{
						float _1687 = _1156.x;
						float _1688 = _1156.y;
						float _1686 = max(_1687, _1688);
						float _1689 = _1156.z;
						float _1685 = max(_1686, _1689);
						float _1684 = clamp(_1685, _25, _24);
						float _1681;
						_1681 = _1684;
						uint2 _1691 = _1118.xy;
						int _1692 = _1205 + _16;
						float _1690 = rnd(_1691, _1153, _1692, _1121, _1120, _8, _10);
						bool _1693 = _1690 > _1681;
						if (_1693)
						{
							break;
						}
						float3 _1696 = _1156 / _1681;
						_1156 = _1696;
					}
					int _1697 = 1;
					_1158 += _1697;
					float3 _1241;
					float3 _1242;
					if (_1235)
					{
						tangent_basis _1702 = create_uv_basis(_1224, _1225, _1226, _1212, _1213, _1214, _1232);
						tangent_basis _1698;
						_1698 = _1702;
						float3 _1703 = _1698.tangent;
						float3 _1704 = _1233 * _1703;
						_1241 = _1704;
						float3 _1705 = _1698.binormal;
						float3 _1706 = _1233 * _1705;
						_1242 = _1706;
						float _1708 = dot(_1230, _1241);
						float3 _1709 = _1230 * _1708;
						float3 _1710 = _1241 - _1709;
						float3 _1707 = normalize(_1710);
						_1241 = _1707;
						float _1712 = dot(_1241, _1242);
						float3 _1713 = _1241 * _1712;
						float _1714 = dot(_1230, _1242);
						float3 _1715 = _1230 * _1714;
						float3 _1716 = _1242 - _1715;
						float3 _1717 = _1716 - _1713;
						float3 _1711 = normalize(_1717);
						_1242 = _1711;
						if (_1234)
						{
							float3 _1720 = -_1242;
							_1242 = _1720;
						}
						float _1722 = 1.000000;
						float _1723 = 2.000000;
						float3 _1724 = _1236.xyz;
						float3 _1725 = _1724 * _1723;
						float3 _1726 = _1725 - _1722;
						float3 _1721 = normalize(_1726);
						float3 _1699;
						_1699 = _1721;
						float _1728 = _1699.z;
						float3 _1729 = _1230 * _1728;
						float _1730 = _1699.y;
						float3 _1731 = _1242 * _1730;
						float _1732 = _1699.x;
						float3 _1733 = _1241 * _1732;
						float3 _1734 = _1733 - _1731;
						float3 _1735 = _1734 + _1729;
						float3 _1727 = normalize(_1735);
						_1230 = _1727;
						float _1736 = 0.000100;
						float _1737 = dot(_1230, _1231);
						bool _1738 = _1737 < _1736;
						if (_1738)
						{
							float _1742 = dot(_1230, _1231);
							float _1743 = 0.000100;
							float _1744 = _1743 - _1742;
							float3 _1745 = _1231 * _1744;
							float3 _1746 = _1230 + _1745;
							float3 _1741 = normalize(_1746);
							_1230 = _1741;
						}
					}
					float3 _1747 = _1155.direction;
					float3 _1748 = -_1747;
					float3 _1243;
					_1243 = _1748;
					float _1749 = dot(_1230, _1243);
					float _1244;
					_1244 = _1749;
					float _1750 = 0.001000;
					bool _1751 = _1244 < _1750;
					if (_1751)
					{
						int _1755 = 0;
						bool _1756 = _1197 > _1755;
						if (_1756)
						{
							break;
						}
						float _1760 = 0.001000;
						float _1761 = _1760 - _1244;
						float3 _1762 = _1243 * _1761;
						float3 _1763 = _1230 + _1762;
						float3 _1759 = normalize(_1763);
						_1230 = _1759;
						float _1764 = dot(_1230, _1231);
						float _1752;
						_1752 = _1764;
						float _1765 = 0.001000;
						bool _1766 = _1752 < _1765;
						if (_1766)
						{
							float _1770 = 0.001000;
							float _1771 = _1770 - _1752;
							float3 _1772 = _1231 * _1771;
							float3 _1773 = _1230 + _1772;
							float3 _1769 = normalize(_1773);
							_1230 = _1769;
						}
						float _1775 = dot(_1230, _1243);
						float _1776 = 0.001000;
						float _1774 = max(_1775, _1776);
						_1244 = _1774;
					}
					tangent_basis _1777 = create_basis(_1230);
					tangent_basis _1245;
					_1245 = _1777;
					float3 _1778 = _1245.tangent;
					_1241 = _1778;
					float3 _1779 = _1245.binormal;
					_1242 = _1779;
					float _1781 = _1239.z;
					float3 _1780 = surface_albedo(_1237, _1781);
					float3 _1246;
					_1246 = _1780;
					float _1783 = _1239.z;
					float3 _1782 = surface_specular(_1237, _1783);
					float3 _1247;
					_1247 = _1782;
					float _1784 = _1239.y;
					float _1248;
					_1248 = _1784;
					float3 _1785 = spec_directional_albedo(_1247, _1244, _1248);
					float3 _1249;
					_1249 = _1785;
					float _1786 = 1.000000;
					float3 _1787 = _1786 - _1249;
					float3 _1788 = _1246 * _1787;
					float3 _1250;
					_1250 = _1788;
					float _1789 = luma(_1249);
					float _1251;
					_1251 = _1789;
					float _1790 = luma(_1250);
					float _1252;
					_1252 = _1790;
					float _1793 = _1251 + _1252;
					float _1794 = 0.000010;
					float _1792 = max(_1793, _1794);
					float _1795 = _1251 / _1792;
					float _1796 = 0.050000;
					float _1797 = 0.995000;
					float _1791 = clamp(_1795, _1796, _1797);
					float _1253;
					_1253 = _1791;
					float _1799 = _1248 * _1248;
					float _1800 = 0.001000;
					float _1798 = max(_1799, _1800);
					float _1254;
					_1254 = _1798;
					float _1801 = 0.000000;
					float _1802 = _1.params.w;
					bool _1803 = _1802 != _1801;
					if (_1803)
					{
						uint2 _1811 = _1118.xy;
						int _1812 = _1205 + _21;
						float _1810 = rnd_fine(_1811, _1153, _1812, _1121, _1120, _8, _10);
						uint2 _1814 = _1118.xy;
						int _1815 = 1;
						int _1816 = _1205 + _21;
						int _1817 = _1816 + _1815;
						float _1813 = rnd_fine(_1814, _1153, _1817, _1121, _1120, _8, _10);
						float4 _1809 = sample_env(_1810, _1813, _1, _11);
						float4 _1804;
						_1804 = _1809;
						float3 _1818 = _1804.xyz;
						float3 _1805;
						_1805 = _1818;
						float _1819 = _1804.w;
						float _1806;
						_1806 = _1819;
						float _1820 = 0.000000;
						float _1821 = dot(_1805, _1231);
						bool _1822 = _1821 > _1820;
						float _1823 = 0.000000;
						bool _1824 = _1806 > _1823;
						bool _1825 = _1824 && _1822;
						if (_1825)
						{
							float4 _1830 = bsdf_eval(_1805, _1230, _1243, _1244, _1250, _1247, _1254, _1253);
							float4 _1826;
							_1826 = _1830;
							float3 _1831 = _1826.xyz;
							float3 _1827;
							_1827 = _1831;
							float _1832 = 0.000000;
							float _1835 = _1827.x;
							float _1836 = _1827.y;
							float _1834 = max(_1835, _1836);
							float _1837 = _1827.z;
							float _1833 = max(_1834, _1837);
							bool _1838 = _1833 > _1832;
							if (_1838)
							{
								float3 _1842 = offset_ray(_1220, _1231, _1805);
								bool _1841 = occluded(_1842, _1805, _2);
								bool _1843 = !_1841;
								if (_1843)
								{
									float _1848 = _1826.w;
									float _1847 = mis_weight(_1806, _1848);
									float _1849 = _1847 / _1806;
									float3 _1850 = env_radiance(_1805, _1, _7, _12);
									float3 _1851 = _1156 * _1827;
									float3 _1852 = _1851 * _1850;
									float3 _1853 = _1852 * _1849;
									float _1854 = 0.000000;
									float3 _1855 = float3(_1854, _1854, _1854);
									float _1856 = 8.000000;
									float3 _1857 = float3(_1856, _1856, _1856);
									float3 _1846 = clamp(_1853, _1855, _1857);
									_1122 += _1846;
								}
							}
						}
					}
					uint2 _1859 = _1118.xy;
					int _1860 = _1205 + _19;
					float _1858 = rnd(_1859, _1153, _1860, _1121, _1120, _8, _10);
					float _1255;
					_1255 = _1858;
					uint2 _1862 = _1118.xy;
					int _1863 = 1;
					int _1864 = _1205 + _19;
					int _1865 = _1864 + _1863;
					float _1861 = rnd(_1862, _1153, _1865, _1121, _1120, _8, _10);
					float _1256;
					_1256 = _1861;
					bool _1866 = _1240 < _1253;
					if (_1866)
					{
						float _1876 = _1254 * _1254;
						float _1867;
						_1867 = _1876;
						float _1878 = dot(_1243, _1241);
						float _1879 = dot(_1243, _1242);
						float3 _1877 = float3(_1878, _1879, _1244);
						float3 _1868;
						_1868 = _1877;
						float3 _1880 = sample_ggx_vndf(_1868, _1254, _1255, _1256);
						float3 _1869;
						_1869 = _1880;
						float3 _1882 = -_1868;
						float3 _1881 = reflect(_1882, _1869);
						float3 _1870;
						_1870 = _1881;
						float _1883 = 0.000000;
						float _1884 = _1870.z;
						bool _1885 = _1884 <= _1883;
						if (_1885)
						{
							break;
						}
						float _1888 = _1870.z;
						float3 _1889 = _1230 * _1888;
						float _1890 = _1870.y;
						float3 _1891 = _1242 * _1890;
						float _1892 = _1870.x;
						float3 _1893 = _1241 * _1892;
						float3 _1894 = _1893 + _1891;
						float3 _1895 = _1894 + _1889;
						_1155.direction = _1895;
						float _1897 = _1868.z;
						float _1896 = smith_lambda(_1897, _1867);
						float _1871;
						_1871 = _1896;
						float _1899 = _1870.z;
						float _1898 = smith_lambda(_1899, _1867);
						float _1872;
						_1872 = _1898;
						float _1900 = 1.000000;
						float _1901 = _1900 + _1871;
						float _1902 = _1901 + _1872;
						float _1903 = 1.000000;
						float _1904 = _1903 + _1871;
						float _1905 = _1904 / _1902;
						float _1873;
						_1873 = _1905;
						float _1906 = _1873 / _1253;
						float _1909 = dot(_1868, _1869);
						float _1910 = 0.000000;
						float _1908 = max(_1909, _1910);
						float3 _1907 = f_schlick(_1247, _1908);
						float3 _1911 = _1907 * _1906;
						_1156 *= _1911;
					}
					bool _1912 = !_1866;
					if (_1912)
					{
						float3 _1915 = cos_weighted_direction(_1241, _1242, _1230, _1255, _1256);
						_1155.direction = _1915;
						float _1916 = 1.000000;
						float _1917 = _1916 - _1253;
						float3 _1918 = _1250 / _1917;
						_1156 *= _1918;
					}
					float _1919 = 0.000000;
					float3 _1921 = _1155.direction;
					float _1920 = dot(_1921, _1231);
					bool _1922 = _1920 <= _1919;
					if (_1922)
					{
						break;
					}
					float _1925 = 0.000000;
					float _1928 = _1156.x;
					float _1929 = _1156.y;
					float _1927 = max(_1928, _1929);
					float _1930 = _1156.z;
					float _1926 = max(_1927, _1930);
					bool _1931 = _1926 <= _1925;
					if (_1931)
					{
						break;
					}
					float3 _1935 = _1155.direction;
					float3 _1934 = offset_ray(_1220, _1231, _1935);
					_1155.origin = _1934;
					float3 _1937 = _1155.direction;
					float4 _1936 = bsdf_eval(_1937, _1230, _1243, _1244, _1250, _1247, _1254, _1253);
					float4 _1257;
					_1257 = _1936;
					float _1938 = _1257.w;
					_1157 = _1938;
					int _1939 = 1;
					_1197 += _1939;
				}
				}
			}
			int _1940 = 1;
			_1145 += _1940;
		}
		}
	}
	uint2 _1942 = _1118.xy;
	float4 _1941 = _3.read(_1942);
	float4 _1123;
	_1123 = _1941;
	float3 _1943 = _1123.xyz;
	float3 _1124;
	_1124 = _1943;
	float _1945 = float(_13);
	float _1944 = float(_1945);
	float3 _1946 = _1122 / _1944;
	_1122 = _1946;
	float _1947 = 1.000000;
	float _1948 = _1.eye.w;
	float _1949 = _1948 + _1947;
	float _1950 = 1.000000;
	float _1951 = _1950 / _1949;
	float _1125;
	_1125 = _1951;
	float3 _1953 = float3(_1125, _1125, _1125);
	float3 _1952 = mix(_1124, _1122, _1953);
	_1124 = _1952;
	float _1955 = 1.000000;
	float4 _1954 = float4(_1124, _1955);
	uint2 _1956 = _1118.xy;
	_3.write(_1954, _1956);
}
