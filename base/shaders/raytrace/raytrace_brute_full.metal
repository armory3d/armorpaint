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

constant float _24 = 3.14159274;

constant float _25 = 6.28318548;

constant int _26 = 16;

constant int _13 = 8;

constant int _14 = 16;

constant int _20 = 5;

constant int _15 = 2;

constant int _21 = 2;

constant float _23 = 0.050000;

constant float _22 = 0.500000;

constant int _16 = 0;

constant int _17 = 1;

constant int _19 = 3;

constant int _18 = 2;

sampler_cache init_sampler(uint2 _572, int _573, texture2d<float> _9, texture2d<float> _10);
uint2 table_texel(int _69);
uint table_word(float4 _112);
float rnd(uint2 _625, int _626, int _627, sampler_cache _628, int _629, texture2d<float> _8, texture2d<float> _10);
uint rank_value_at(int _138, int _139, int _140, int _141, texture2d<float> _10);
uint table_byte(float4 _82, int _83);
float rand_indexed(int _165, int _166, uint _167, uint _168, texture2d<float> _8);
float3 camera_ray_direction(float2 _252, float3 _253, constant _1_type &_1);
float3 env_radiance(float3 _693, constant _1_type &_1, texture2d<float> _7, sampler _12);
float2 equirect(float3 _194, float _195);
float2 s16_to_f32(uint _27);
float2 hit_attribute2d(float2 _57, float2 _58, float2 _59, float2 _60);
float3 hit_attribute(float3 _45, float3 _46, float3 _47, float2 _48);
float3 srgb_to_linear(float3 _291);
tangent_basis create_basis(float3 _212);
float3 cos_weighted_direction(float3 _447, float3 _448, float3 _449, float _450, float _451);
float3 offset_ray(float3 _558, float3 _559, float3 _560);
tangent_basis create_uv_basis(float3 _373, float3 _374, float3 _375, float2 _376, float2 _377, float2 _378, float3 _379);
float3 surface_albedo(float3 _269, float _270);
float3 surface_specular(float3 _277, float _278);
float3 spec_directional_albedo(float3 _314, float _315, float _316);
float luma(float3 _285);
float3 sample_ggx_vndf(float3 _470, float _471, float _472, float _473);
float smith_lambda(float _356, float _357);
float3 f_schlick(float3 _300, float _301);

float2 s16_to_f32(uint _27) {
	int _30 = 16;
	int _32 = 16;
	uint _33 = _27 << _32;
	int _31 = int(_33);
	int _34 = _31 >> _30;
	int _28;
	_28 = _34;
	int _35 = 16;
	int _36 = int(_27);
	int _37 = _36 >> _35;
	int _29;
	_29 = _37;
	float _38 = 32767.000000;
	float _41 = float(_28);
	float _40 = float(_41);
	float _43 = float(_29);
	float _42 = float(_43);
	float2 _39 = float2(_40, _42);
	float2 _44 = _39 / _38;
	return _44;
}

float3 hit_attribute(float3 _45, float3 _46, float3 _47, float2 _48) {
	float3 _49 = _47 - _45;
	float _50 = _48.y;
	float3 _51 = _50 * _49;
	float3 _52 = _46 - _45;
	float _53 = _48.x;
	float3 _54 = _53 * _52;
	float3 _55 = _45 + _54;
	float3 _56 = _55 + _51;
	return _56;
}

float2 hit_attribute2d(float2 _57, float2 _58, float2 _59, float2 _60) {
	float2 _61 = _59 - _57;
	float _62 = _60.y;
	float2 _63 = _62 * _61;
	float2 _64 = _58 - _57;
	float _65 = _60.x;
	float2 _66 = _65 * _64;
	float2 _67 = _57 + _66;
	float2 _68 = _67 + _63;
	return _68;
}

uint2 table_texel(int _69) {
	int _71 = 2;
	int _72 = 131071;
	int _73 = _69 & _72;
	int _74 = _73 >> _71;
	int _70;
	_70 = _74;
	int _77 = 127;
	int _78 = _70 & _77;
	uint _76 = uint(_78);
	int _80 = 7;
	int _81 = _70 >> _80;
	uint _79 = uint(_81);
	uint2 _75 = uint2(_76, _79);
	return _75;
}

uint table_byte(float4 _82, int _83) {
	int _86 = 3;
	int _87 = _83 & _86;
	int _84;
	_84 = _87;
	float _88 = _82.w;
	float _85;
	_85 = _88;
	int _89 = 0;
	bool _90 = _84 == _89;
	if (_90)
	{
		float _93 = _82.x;
		_85 = _93;
	}
	bool _94 = !_90;
	int _95 = 1;
	bool _96 = _84 == _95;
	bool _97 = _94 && _96;
	if (_97)
	{
		float _100 = _82.y;
		_85 = _100;
	}
	bool _101 = !_96;
	bool _102 = _94 && _101;
	int _103 = 2;
	bool _104 = _84 == _103;
	bool _105 = _102 && _104;
	if (_105)
	{
		float _108 = _82.z;
		_85 = _108;
	}
	float _110 = 255.000000;
	float _111 = _85 * _110;
	uint _109 = uint(_111);
	return _109;
}

uint table_word(float4 _112) {
	int _113 = 24;
	float _115 = 255.000000;
	float _116 = _112.w;
	float _117 = _116 * _115;
	uint _114 = uint(_117);
	uint _118 = _114 << _113;
	int _119 = 16;
	float _121 = 255.000000;
	float _122 = _112.z;
	float _123 = _122 * _121;
	uint _120 = uint(_123);
	uint _124 = _120 << _119;
	int _125 = 8;
	float _127 = 255.000000;
	float _128 = _112.y;
	float _129 = _128 * _127;
	uint _126 = uint(_129);
	uint _130 = _126 << _125;
	float _132 = 255.000000;
	float _133 = _112.x;
	float _134 = _133 * _132;
	uint _131 = uint(_134);
	uint _135 = _131 | _130;
	uint _136 = _135 | _124;
	uint _137 = _136 | _118;
	return _137;
}

uint rank_value_at(int _138, int _139, int _140, int _141, texture2d<float> _10) {
	int _144 = 8;
	int _145 = 128;
	int _146 = 127;
	int _147 = 11;
	int _148 = _141 * _147;
	int _149 = _139 + _148;
	int _150 = _149 & _146;
	int _151 = _150 * _145;
	int _152 = 127;
	int _153 = 9;
	int _154 = _141 * _153;
	int _155 = _138 + _154;
	int _156 = _155 & _152;
	int _157 = _156 + _151;
	int _158 = _157 * _144;
	int _159 = 255;
	int _160 = _140 & _159;
	int _161 = _160 + _158;
	int _142;
	_142 = _161;
	uint2 _163 = table_texel(_142);
	float4 _162 = _10.read(_163);
	float4 _143;
	_143 = _162;
	uint _164 = table_byte(_143, _142);
	return _164;
}

float rand_indexed(int _165, int _166, uint _167, uint _168, texture2d<float> _8) {
	int _172 = 255;
	int _173 = _165 & _172;
	_165 = _173;
	int _174 = 255;
	int _175 = _166 & _174;
	_166 = _175;
	int _176 = int(_167);
	int _177 = _165 ^ _176;
	int _169;
	_169 = _177;
	uint _180 = uint(_169);
	uint _181 = uint(_166);
	uint2 _179 = uint2(_180, _181);
	float4 _178 = _8.read(_179);
	float4 _170;
	_170 = _178;
	float _183 = 255.000000;
	float _184 = _170.x;
	float _185 = _184 * _183;
	int _182 = int(_185);
	int _171;
	_171 = _182;
	int _186 = int(_168);
	int _187 = _171 ^ _186;
	_171 = _187;
	float _188 = 256.000000;
	float _190 = float(_171);
	float _189 = float(_190);
	float _191 = 0.500000;
	float _192 = _191 + _189;
	float _193 = _192 / _188;
	return _193;
}

float2 equirect(float3 _194, float _195) {
	float _200 = _194.z;
	float _201 = -1.000000;
	float _202 = 1.000000;
	float _199 = clamp(_200, _201, _202);
	float _198 = acos(_199);
	float _196;
	_196 = _198;
	float _204 = _194.y;
	float _205 = -_204;
	float _206 = _194.x;
	float _203 = atan2(_205, _206);
	float _207 = _203 + _24;
	float _208 = _207 + _195;
	float _197;
	_197 = _208;
	float _210 = _197 / _25;
	float _211 = _196 / _24;
	float2 _209 = float2(_210, _211);
	return _209;
}

tangent_basis create_basis(float3 _212) {
	float _217 = 1.000000;
	float _213;
	_213 = _217;
	float _218 = 0.000000;
	float _219 = _212.z;
	bool _220 = _219 < _218;
	if (_220)
	{
		float _223 = -1.000000;
		_213 = _223;
	}
	float _224 = _212.z;
	float _225 = _213 + _224;
	float _226 = -1.000000;
	float _227 = _226 / _225;
	float _214;
	_214 = _227;
	float _228 = _212.y;
	float _229 = _212.x;
	float _230 = _229 * _228;
	float _231 = _230 * _214;
	float _215;
	_215 = _231;
	tangent_basis _216;
	float _233 = _212.x;
	float _234 = _212.x;
	float _235 = _213 * _234;
	float _236 = _235 * _233;
	float _237 = _236 * _214;
	float _238 = 1.000000;
	float _239 = _238 + _237;
	float _240 = _213 * _215;
	float _241 = _212.x;
	float _242 = -_213;
	float _243 = _242 * _241;
	float3 _232 = float3(_239, _240, _243);
	_216.tangent = _232;
	float _245 = _212.y;
	float _246 = _212.y;
	float _247 = _246 * _245;
	float _248 = _247 * _214;
	float _249 = _213 + _248;
	float _250 = _212.y;
	float _251 = -_250;
	float3 _244 = float3(_215, _249, _251);
	_216.binormal = _244;
	return _216;
}

float3 camera_ray_direction(float2 _252, float3 _253, constant _1_type &_1) {
	float _257 = _252.x;
	float _258 = _252.y;
	float _259 = -_258;
	float _260 = 0.000000;
	float _261 = 1.000000;
	float4 _256 = float4(_257, _259, _260, _261);
	float4x4 _262 = _1.inv_vp;
	float4 _263 = _262 * _256;
	float4 _254;
	_254 = _263;
	float _264 = _254.w;
	float3 _265 = _254.xyz;
	float3 _266 = _265 / _264;
	float3 _255;
	_255 = _266;
	float3 _268 = _255 - _253;
	float3 _267 = normalize(_268);
	return _267;
}

float3 surface_albedo(float3 _269, float _270) {
	float _273 = 0.000000;
	float _274 = 0.000000;
	float _275 = 0.000000;
	float3 _272 = float3(_273, _274, _275);
	float3 _276 = float3(_270, _270, _270);
	float3 _271 = mix(_269, _272, _276);
	return _271;
}

float3 surface_specular(float3 _277, float _278) {
	float _281 = 0.040000;
	float _282 = 0.040000;
	float _283 = 0.040000;
	float3 _280 = float3(_281, _282, _283);
	float3 _284 = float3(_278, _278, _278);
	float3 _279 = mix(_280, _277, _284);
	return _279;
}

float luma(float3 _285) {
	float _288 = 0.212600;
	float _289 = 0.715200;
	float _290 = 0.072200;
	float3 _287 = float3(_288, _289, _290);
	float _286 = dot(_285, _287);
	return _286;
}

float3 srgb_to_linear(float3 _291) {
	float _292 = 0.0125228781;
	float _293 = 0.682171106;
	float _294 = 0.305306017;
	float3 _295 = _291 * _294;
	float3 _296 = _295 + _293;
	float3 _297 = _291 * _296;
	float3 _298 = _297 + _292;
	float3 _299 = _291 * _298;
	return _299;
}

float3 f_schlick(float3 _300, float _301) {
	float _305 = 1.000000;
	float _306 = _305 - _301;
	float _304 = saturate(_306);
	float _302;
	_302 = _304;
	float _307 = _302 * _302;
	float _303;
	_303 = _307;
	float _308 = _303 * _303;
	float _309 = _308 * _302;
	float _310 = 1.000000;
	float3 _311 = _310 - _300;
	float3 _312 = _311 * _309;
	float3 _313 = _300 + _312;
	return _313;
}

float3 spec_directional_albedo(float3 _314, float _315, float _316) {
	float _323 = -1.000000;
	float _324 = -0.027500;
	float _325 = -0.572000;
	float _326 = 0.022000;
	float4 _322 = float4(_323, _324, _325, _326);
	float4 _317;
	_317 = _322;
	float _328 = 1.000000;
	float _329 = 0.042500;
	float _330 = 1.040000;
	float _331 = -0.040000;
	float4 _327 = float4(_328, _329, _330, _331);
	float4 _318;
	_318 = _327;
	float4 _332 = _317 * _316;
	float4 _333 = _332 + _318;
	float4 _319;
	_319 = _333;
	float _334 = _319.y;
	float _335 = _319.x;
	float _337 = _319.x;
	float _338 = _319.x;
	float _339 = _338 * _337;
	float _341 = -9.280000;
	float _342 = _341 * _315;
	float _340 = exp2(_342);
	float _336 = min(_339, _340);
	float _343 = _336 * _335;
	float _344 = _343 + _334;
	float _320;
	_320 = _344;
	float2 _345 = _319.zw;
	float _347 = -1.040000;
	float _348 = 1.040000;
	float2 _346 = float2(_347, _348);
	float2 _349 = _346 * _320;
	float2 _350 = _349 + _345;
	float2 _321;
	_321 = _350;
	float _352 = _321.y;
	float _353 = _321.x;
	float3 _354 = _314 * _353;
	float3 _355 = _354 + _352;
	float3 _351 = saturate(_355);
	return _351;
}

float smith_lambda(float _356, float _357) {
	float _359 = _356 * _356;
	float _358;
	_358 = _359;
	float _360 = 1.000000;
	float _363 = 1.00000001e-07;
	float _362 = max(_358, _363);
	float _364 = 1.000000;
	float _365 = _364 - _358;
	float _366 = _357 * _365;
	float _367 = _366 / _362;
	float _368 = 1.000000;
	float _369 = _368 + _367;
	float _361 = sqrt(_369);
	float _370 = _361 - _360;
	float _371 = 0.500000;
	float _372 = _371 * _370;
	return _372;
}

tangent_basis create_uv_basis(float3 _373, float3 _374, float3 _375, float2 _376, float2 _377, float2 _378, float3 _379) {
	float3 _385 = _374 - _373;
	float3 _380;
	_380 = _385;
	float3 _386 = _375 - _373;
	float3 _381;
	_381 = _386;
	float2 _387 = _377 - _376;
	float2 _382;
	_382 = _387;
	float2 _388 = _378 - _376;
	float2 _383;
	_383 = _388;
	float _389 = _382.y;
	float _390 = _383.x;
	float _391 = _390 * _389;
	float _392 = _383.y;
	float _393 = _382.x;
	float _394 = _393 * _392;
	float _395 = _394 - _391;
	float _384;
	_384 = _395;
	float _396 = 9.99999996e-13;
	float _397 = abs(_384);
	bool _398 = _397 > _396;
	if (_398)
	{
		float _406 = 1.000000;
		float _407 = _406 / _384;
		float _399;
		_399 = _407;
		float _408 = _382.y;
		float3 _409 = _381 * _408;
		float _410 = _383.y;
		float3 _411 = _380 * _410;
		float3 _412 = _411 - _409;
		float3 _413 = _412 * _399;
		float3 _400;
		_400 = _413;
		float _414 = _383.x;
		float3 _415 = _380 * _414;
		float _416 = _382.x;
		float3 _417 = _381 * _416;
		float3 _418 = _417 - _415;
		float3 _419 = _418 * _399;
		float3 _401;
		_401 = _419;
		float _420 = dot(_379, _400);
		float3 _421 = _379 * _420;
		float3 _422 = _400 - _421;
		float3 _402;
		_402 = _422;
		float _423 = dot(_402, _402);
		float _403;
		_403 = _423;
		float _424 = 1.00000002e-16;
		bool _425 = _403 > _424;
		if (_425)
		{
			float _430 = rsqrt(_403);
			float3 _431 = _402 * _430;
			_402 = _431;
			float _432 = dot(_379, _401);
			float3 _433 = _379 * _432;
			float3 _434 = _401 - _433;
			float3 _426;
			_426 = _434;
			float _435 = dot(_402, _426);
			float3 _436 = _402 * _435;
			float3 _437 = _426 - _436;
			_426 = _437;
			float _438 = dot(_426, _426);
			float _427;
			_427 = _438;
			float _439 = 1.00000002e-16;
			bool _440 = _427 > _439;
			if (_440)
			{
				tangent_basis _441;
				_441.tangent = _402;
				float _444 = rsqrt(_427);
				float3 _445 = _426 * _444;
				_441.binormal = _445;
				return _441;
			}
		}
	}
	tangent_basis _446 = create_basis(_379);
	return _446;
}

float3 cos_weighted_direction(float3 _447, float3 _448, float3 _449, float _450, float _451) {
	float _454 = sqrt(_450);
	float _452;
	_452 = _454;
	float _455 = _25 * _451;
	float _453;
	_453 = _455;
	float _458 = 0.000000;
	float _459 = 1.000000;
	float _460 = _459 - _450;
	float _457 = max(_458, _460);
	float _456 = sqrt(_457);
	float3 _461 = _449 * _456;
	float _462 = sin(_453);
	float _463 = _452 * _462;
	float3 _464 = _448 * _463;
	float _465 = cos(_453);
	float _466 = _452 * _465;
	float3 _467 = _447 * _466;
	float3 _468 = _467 + _464;
	float3 _469 = _468 + _461;
	return _469;
}

float3 sample_ggx_vndf(float3 _470, float _471, float _472, float _473) {
	float _486 = _470.x;
	float _487 = _471 * _486;
	float _488 = _470.y;
	float _489 = _471 * _488;
	float _490 = _470.z;
	float3 _485 = float3(_487, _489, _490);
	float3 _484 = normalize(_485);
	float3 _474;
	_474 = _484;
	float _491 = _474.y;
	float _492 = _474.y;
	float _493 = _492 * _491;
	float _494 = _474.x;
	float _495 = _474.x;
	float _496 = _495 * _494;
	float _497 = _496 + _493;
	float _475;
	_475 = _497;
	float _499 = 1.000000;
	float _500 = 0.000000;
	float _501 = 0.000000;
	float3 _498 = float3(_499, _500, _501);
	float3 _476;
	_476 = _498;
	float _502 = 0.000000;
	bool _503 = _475 > _502;
	if (_503)
	{
		float _506 = rsqrt(_475);
		float _508 = _474.y;
		float _509 = -_508;
		float _510 = _474.x;
		float _511 = 0.000000;
		float3 _507 = float3(_509, _510, _511);
		float3 _512 = _507 * _506;
		_476 = _512;
	}
	float3 _513 = cross(_474, _476);
	float3 _477;
	_477 = _513;
	float _514 = sqrt(_472);
	float _478;
	_478 = _514;
	float _515 = _25 * _473;
	float _479;
	_479 = _515;
	float _516 = cos(_479);
	float _517 = _478 * _516;
	float _480;
	_480 = _517;
	float _518 = sin(_479);
	float _519 = _478 * _518;
	float _481;
	_481 = _519;
	float _520 = _474.z;
	float _521 = 1.000000;
	float _522 = _521 + _520;
	float _523 = 0.500000;
	float _524 = _523 * _522;
	float _482;
	_482 = _524;
	float _525 = _482 * _481;
	float _528 = 0.000000;
	float _529 = _480 * _480;
	float _530 = 1.000000;
	float _531 = _530 - _529;
	float _527 = max(_528, _531);
	float _526 = sqrt(_527);
	float _532 = 1.000000;
	float _533 = _532 - _482;
	float _534 = _533 * _526;
	float _535 = _534 + _525;
	_481 = _535;
	float _538 = 0.000000;
	float _539 = _481 * _481;
	float _540 = _480 * _480;
	float _541 = 1.000000;
	float _542 = _541 - _540;
	float _543 = _542 - _539;
	float _537 = max(_538, _543);
	float _536 = sqrt(_537);
	float3 _544 = _536 * _474;
	float3 _545 = _481 * _477;
	float3 _546 = _480 * _476;
	float3 _547 = _546 + _545;
	float3 _548 = _547 + _544;
	float3 _483;
	_483 = _548;
	float _551 = _483.x;
	float _552 = _471 * _551;
	float _553 = _483.y;
	float _554 = _471 * _553;
	float _556 = 0.000001;
	float _557 = _483.z;
	float _555 = max(_556, _557);
	float3 _550 = float3(_552, _554, _555);
	float3 _549 = normalize(_550);
	return _549;
}

float3 offset_ray(float3 _558, float3 _559, float3 _560) {
	float _561 = 0.000000;
	float _562 = dot(_560, _559);
	bool _563 = _562 < _561;
	if (_563)
	{
		float _566 = 0.000100;
		float3 _567 = _559 * _566;
		float3 _568 = _558 - _567;
		return _568;
	}
	float _569 = 0.000100;
	float3 _570 = _559 * _569;
	float3 _571 = _558 + _570;
	return _571;
}

sampler_cache init_sampler(uint2 _572, int _573, texture2d<float> _9, texture2d<float> _10) {
	int _578 = 127;
	int _579 = 9;
	int _580 = _573 * _579;
	uint _582 = _572.x;
	int _581 = int(_582);
	int _583 = _581 + _580;
	int _584 = _583 & _578;
	int _574;
	_574 = _584;
	int _585 = 127;
	int _586 = 11;
	int _587 = _573 * _586;
	uint _589 = _572.y;
	int _588 = int(_589);
	int _590 = _588 + _587;
	int _591 = _590 & _585;
	int _575;
	_575 = _591;
	int _592 = 8;
	int _593 = 128;
	int _594 = _575 * _593;
	int _595 = _574 + _594;
	int _596 = _595 * _592;
	int _576;
	_576 = _596;
	sampler_cache _577;
	uint2 _600 = table_texel(_576);
	float4 _599 = _9.read(_600);
	uint _598 = table_word(_599);
	int _604 = 4;
	int _605 = _576 + _604;
	uint2 _603 = table_texel(_605);
	float4 _602 = _9.read(_603);
	uint _601 = table_word(_602);
	uint2 _597 = uint2(_598, _601);
	_577.scramble = _597;
	uint2 _609 = table_texel(_576);
	float4 _608 = _10.read(_609);
	uint _607 = table_word(_608);
	int _613 = 4;
	int _614 = _576 + _613;
	uint2 _612 = table_texel(_614);
	float4 _611 = _10.read(_612);
	uint _610 = table_word(_611);
	int _618 = 8;
	int _619 = _576 + _618;
	uint2 _617 = table_texel(_619);
	float4 _616 = _10.read(_617);
	uint _615 = table_word(_616);
	int _623 = 12;
	int _624 = _576 + _623;
	uint2 _622 = table_texel(_624);
	float4 _621 = _10.read(_622);
	uint _620 = table_word(_621);
	uint4 _606 = uint4(_607, _610, _615, _620);
	_577.rank = _606;
	return _577;
}

float rnd(uint2 _625, int _626, int _627, sampler_cache _628, int _629, texture2d<float> _8, texture2d<float> _10) {
	int _634 = 7;
	int _635 = _627 & _634;
	int _630;
	_630 = _635;
	uint _636 = _628.scramble.y;
	uint _631;
	_631 = _636;
	int _637 = 4;
	bool _638 = _630 < _637;
	if (_638)
	{
		uint _641 = _628.scramble.x;
		_631 = _641;
	}
	int _642 = 255;
	int _643 = 8;
	int _644 = 3;
	int _645 = _630 & _644;
	int _646 = _645 * _643;
	uint _647 = _631 >> _646;
	uint _648 = _647 & _642;
	uint _632;
	_632 = _648;
	uint _633;
	bool _649 = _627 < _26;
	if (_649)
	{
		int _654 = 2;
		int _655 = _627 >> _654;
		int _650;
		_650 = _655;
		uint _656 = _628.rank.w;
		uint _651;
		_651 = _656;
		int _657 = 0;
		bool _658 = _650 == _657;
		if (_658)
		{
			uint _661 = _628.rank.x;
			_651 = _661;
		}
		bool _662 = !_658;
		int _663 = 1;
		bool _664 = _650 == _663;
		bool _665 = _662 && _664;
		if (_665)
		{
			uint _668 = _628.rank.y;
			_651 = _668;
		}
		bool _669 = !_664;
		bool _670 = _662 && _669;
		int _671 = 2;
		bool _672 = _650 == _671;
		bool _673 = _670 && _672;
		if (_673)
		{
			uint _676 = _628.rank.z;
			_651 = _676;
		}
		int _677 = 255;
		int _678 = 8;
		int _679 = 3;
		int _680 = _627 & _679;
		int _681 = _680 * _678;
		uint _682 = _651 >> _681;
		uint _683 = _682 & _677;
		_633 = _683;
	}
	bool _684 = !_649;
	if (_684)
	{
		uint _689 = _625.x;
		int _688 = int(_689);
		uint _691 = _625.y;
		int _690 = int(_691);
		uint _687 = rank_value_at(_688, _690, _627, _629, _10);
		_633 = _687;
	}
	float _692 = rand_indexed(_626, _627, _633, _632, _8);
	return _692;
}

float3 env_radiance(float3 _693, constant _1_type &_1, texture2d<float> _7, sampler _12) {
	float _697 = _1.params.y;
	float2 _696 = equirect(_693, _697);
	float2 _694;
	_694 = _696;
	float _699 = 0.000000;
	float4 _698 = _7.sample(_12, _694, level(_699));
	float4 _695;
	_695 = _698;
	float _701 = _1.params.x;
	float _700 = abs(_701);
	float3 _702 = _695.xyz;
	float3 _703 = _702 * _700;
	return _703;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _7 [[texture(4)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]], sampler _12 [[sampler(0)]], constant _kong_instance *_kong_instances [[buffer(2)]], constant _kong_geometry_textures *_kong_geometry_textures [[buffer(3)]]) {
	uint3 _712 = _kong_dispatch_thread_id;
	uint3 _704;
	_704 = _712;
	uint2 _713 = uint2(_3.get_width(), _3.get_height());
	uint2 _705;
	_705 = _713;
	uint _714 = _705.y;
	uint _715 = _704.y;
	bool _716 = _715 >= _714;
	uint _717 = _705.x;
	uint _718 = _704.x;
	bool _719 = _718 >= _717;
	bool _720 = _719 || _716;
	if (_720)
	{
		return;
	}
	float _724 = _1.eye.w;
	int _723 = int(_724);
	int _706;
	_706 = _723;
	uint2 _726 = _704.xy;
	sampler_cache _725 = init_sampler(_726, _706, _9, _10);
	sampler_cache _707;
	_707 = _725;
	float _728 = 0.000000;
	float _729 = 0.000000;
	float _730 = 0.000000;
	float3 _727 = float3(_728, _729, _730);
	float3 _708;
	_708 = _727;
	{
		int _734 = 0;
		int _731;
		_731 = _734;
		while (true)
{
		bool _738 = _731 < _13;
		if (!_738) { break; }
		{
			int _745 = _706 * _13;
			int _746 = _745 + _731;
			int _739;
			_739 = _746;
			uint2 _748 = _704.xy;
			float2 _747 = float2(_748);
			float2 _740;
			_740 = _747;
			uint2 _750 = _704.xy;
			int _751 = 0;
			float _749 = rnd(_750, _739, _751, _707, _706, _8, _10);
			_740.x += _749;
			uint2 _753 = _704.xy;
			int _754 = 1;
			float _752 = rnd(_753, _739, _754, _707, _706, _8, _10);
			_740.y += _752;
			ray _741;
			float _755 = 0.000100;
			_741.min_distance = _755;
			float _756 = 100.000000;
			_741.max_distance = _756;
			float3 _757 = _1.eye.xyz;
			_741.origin = _757;
			float _759 = 1.000000;
			float _760 = 2.000000;
			float2 _761 = float2(_705);
			float2 _762 = _740 / _761;
			float2 _763 = _762 * _760;
			float2 _764 = _763 - _759;
			float3 _765 = _1.eye.xyz;
			float3 _758 = camera_ray_direction(_764, _765, _1);
			_741.direction = _758;
			float _767 = 1.000000;
			float _768 = 1.000000;
			float _769 = 1.000000;
			float3 _766 = float3(_767, _768, _769);
			float3 _742;
			_742 = _766;
			{
				int _773 = 0;
				int _770;
				_770 = _773;
				while (true)
{
				bool _777 = _770 < _14;
				if (!_777) { break; }
				{
					int _831 = _770 * _20;
					int _832 = _15 + _831;
					int _778;
					_778 = _832;
					bool _833 = _770 >= _21;
					if (_833)
					{
						float _840 = _742.x;
						float _841 = _742.y;
						float _839 = max(_840, _841);
						float _842 = _742.z;
						float _838 = max(_839, _842);
						float _837 = clamp(_838, _23, _22);
						float _834;
						_834 = _837;
						uint2 _844 = _704.xy;
						int _845 = _778 + _16;
						float _843 = rnd(_844, _739, _845, _707, _706, _8, _10);
						bool _846 = _843 > _834;
						if (_846)
						{
							break;
						}
						float3 _849 = _742 / _834;
						_742 = _849;
					}
					_kong_intersector::result_type _779;
					{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _779 = i.intersect(_741, _2); }
					bool _851 = _779.type == intersection_type::triangle;
					bool _852 = !_851;
					if (_852)
					{
						float3 _853;
						float _857 = 1.000000;
						float _854;
						_854 = _857;
						float _858 = 0.000000;
						float _859 = _1.params.x;
						bool _860 = _859 < _858;
						int _861 = 0;
						bool _862 = _770 == _861;
						bool _863 = _862 && _860;
						if (_863)
						{
							float _867 = 0.027500;
							float _868 = 0.027500;
							float _869 = 0.027500;
							float3 _866 = float3(_867, _868, _869);
							_853 = _866;
						}
						bool _870 = !_863;
						if (_870)
						{
							float3 _874 = _741.direction;
							float3 _873 = env_radiance(_874, _1, _7, _12);
							_853 = _873;
						}
						float3 _876 = _742 * _853;
						float3 _877 = _876 * _854;
						float _878 = 0.000000;
						float3 _879 = float3(_878, _878, _878);
						float _880 = 8.000000;
						float3 _881 = float3(_880, _880, _880);
						float3 _875 = clamp(_877, _879, _881);
						_708 += _875;
						break;
					}
					uint _882 = _kong_instances[_779.user_instance_id].geometry;
					uint _780;
					_780 = _882;
					float2 _883 = _779.triangle_barycentric_coord;
					float2 _781;
					_781 = _883;
					int _885 = 0;
					uint4 _884 = _kong_vertex(_kong_instances[_779.user_instance_id], _779.primitive_id * 3 + _885);
					uint4 _782;
					_782 = _884;
					int _887 = 1;
					uint4 _886 = _kong_vertex(_kong_instances[_779.user_instance_id], _779.primitive_id * 3 + _887);
					uint4 _783;
					_783 = _886;
					int _889 = 2;
					uint4 _888 = _kong_vertex(_kong_instances[_779.user_instance_id], _779.primitive_id * 3 + _889);
					uint4 _784;
					_784 = _888;
					uint _891 = _782.w;
					float2 _890 = s16_to_f32(_891);
					float2 _785;
					_785 = _890;
					uint _893 = _783.w;
					float2 _892 = s16_to_f32(_893);
					float2 _786;
					_786 = _892;
					uint _895 = _784.w;
					float2 _894 = s16_to_f32(_895);
					float2 _787;
					_787 = _894;
					float _896 = _1.params.z;
					float2 _897 = hit_attribute2d(_785, _786, _787, _781);
					float2 _898 = _897 * _896;
					float2 _788;
					_788 = _898;
					uint2 _899 = uint2(_kong_geometry_textures[_780].texpaint0.get_width(), _kong_geometry_textures[_780].texpaint0.get_height());
					uint2 _789;
					_789 = _899;
					float2 _901 = float2(_789);
					float2 _902 = fract(_788);
					float2 _903 = _902 * _901;
					uint2 _900 = uint2(_903);
					uint2 _790;
					_790 = _900;
					float4 _904 = _kong_geometry_textures[_780].texpaint0.read(_790);
					float4 _791;
					_791 = _904;
					float _905 = _779.distance;
					float _792;
					_792 = _905;
					float3 _906 = _741.direction;
					float3 _907 = _906 * _792;
					float3 _908 = _741.origin;
					float3 _909 = _908 + _907;
					float3 _793;
					_793 = _909;
					uint _911 = _782.y;
					float2 _910 = s16_to_f32(_911);
					float2 _794;
					_794 = _910;
					uint _913 = _783.y;
					float2 _912 = s16_to_f32(_913);
					float2 _795;
					_795 = _912;
					uint _915 = _784.y;
					float2 _914 = s16_to_f32(_915);
					float2 _796;
					_796 = _914;
					uint _918 = _782.x;
					float2 _917 = s16_to_f32(_918);
					float _919 = _794.x;
					float3 _916 = float3(_917, _919);
					float3 _797;
					_797 = _916;
					uint _922 = _783.x;
					float2 _921 = s16_to_f32(_922);
					float _923 = _795.x;
					float3 _920 = float3(_921, _923);
					float3 _798;
					_798 = _920;
					uint _926 = _784.x;
					float2 _925 = s16_to_f32(_926);
					float _927 = _796.x;
					float3 _924 = float3(_925, _927);
					float3 _799;
					_799 = _924;
					uint _930 = _782.z;
					float2 _929 = s16_to_f32(_930);
					float _931 = _794.y;
					float3 _928 = float3(_929, _931);
					float3 _800;
					_800 = _928;
					uint _934 = _783.z;
					float2 _933 = s16_to_f32(_934);
					float _935 = _795.y;
					float3 _932 = float3(_933, _935);
					float3 _801;
					_801 = _932;
					uint _938 = _784.z;
					float2 _937 = s16_to_f32(_938);
					float _939 = _796.y;
					float3 _936 = float3(_937, _939);
					float3 _802;
					_802 = _936;
					float3 _941 = hit_attribute(_800, _801, _802, _781);
					float3 _940 = normalize(_941);
					float3 _803;
					_803 = _940;
					float3 _943 = _798 - _797;
					float3 _944 = _799 - _797;
					float3 _942 = cross(_943, _944);
					float3 _804;
					_804 = _942;
					float _945 = 9.99999968e-21;
					float _946 = dot(_804, _804);
					bool _947 = _946 > _945;
					if (_947)
					{
						float3 _950 = normalize(_804);
						_804 = _950;
					}
					bool _951 = !_947;
					if (_951)
					{
						_804 = _803;
					}
					float _954 = 0.000000;
					float _955 = dot(_804, _803);
					bool _956 = _955 < _954;
					if (_956)
					{
						float3 _959 = -_804;
						_804 = _959;
					}
					float3 _805;
					_805 = _803;
					float3x3 _960 = float3x3(_779.object_to_world_transform[0], _779.object_to_world_transform[1], _779.object_to_world_transform[2]);
					float3x3 _806;
					_806 = _960;
					float3 _962 = _806 * _803;
					float3 _961 = normalize(_962);
					_803 = _961;
					float3 _964 = _806 * _804;
					float3 _963 = normalize(_964);
					_804 = _963;
					float _965 = 0.000000;
					float3 _967 = _741.direction;
					float _966 = dot(_804, _967);
					bool _968 = _966 > _965;
					bool _807;
					_807 = _968;
					if (_807)
					{
						float3 _971 = -_804;
						_804 = _971;
						float3 _972 = -_803;
						_803 = _972;
					}
					bool _973 = true;
					bool _808;
					_808 = _973;
					float _975 = 0.000000;
					float _976 = 0.000000;
					float _977 = 0.000000;
					float _978 = 0.000000;
					float4 _974 = float4(_975, _976, _977, _978);
					float4 _809;
					_809 = _974;
					if (_808)
					{
						float4 _981 = _kong_geometry_textures[_780].texpaint1.read(_790);
						_809 = _981;
					}
					float3 _983 = _791.xyz;
					float3 _982 = srgb_to_linear(_983);
					float3 _810;
					_810 = _982;
					bool _984 = _779.triangle_front_facing;
					bool _985 = !_984;
					if (_985)
					{
						float _990 = 0.001000;
						float3 _991 = float3(_990, _990, _990);
						float3 _989 = max(_810, _991);
						float _992 = _791.w;
						float _993 = _792 * _992;
						float3 _994 = float3(_993, _993, _993);
						float3 _988 = pow(_989, _994);
						_742 *= _988;
					}
					int _995 = 1;
					int _996 = 3;
					float _998 = 255.000000;
					float _999 = _809.w;
					float _1000 = _999 * _998;
					int _997 = int(_1000);
					int _1001 = _997 % _996;
					bool _1002 = _1001 == _995;
					if (_1002)
					{
						float _1005 = 100.000000;
						float3 _1006 = _742 * _810;
						float3 _1007 = _1006 * _1005;
						_708 += _1007;
						break;
					}
					float4 _1008 = _kong_geometry_textures[_780].texpaint2.read(_790);
					float4 _811;
					_811 = _1008;
					uint2 _1010 = _704.xy;
					int _1011 = _778 + _17;
					float _1009 = rnd(_1010, _739, _1011, _707, _706, _8, _10);
					float _812;
					_812 = _1009;
					float _1012 = _791.w;
					bool _1013 = _812 > _1012;
					if (_1013)
					{
						float3 _1019 = _741.direction;
						tangent_basis _1018 = create_basis(_1019);
						tangent_basis _1014;
						_1014 = _1018;
						float3 _1021 = _1014.tangent;
						float3 _1022 = _1014.binormal;
						float3 _1023 = _741.direction;
						uint2 _1025 = _704.xy;
						int _1026 = _778 + _19;
						float _1024 = rnd(_1025, _739, _1026, _707, _706, _8, _10);
						uint2 _1028 = _704.xy;
						int _1029 = 1;
						int _1030 = _778 + _19;
						int _1031 = _1030 + _1029;
						float _1027 = rnd(_1028, _739, _1031, _707, _706, _8, _10);
						float3 _1020 = cos_weighted_direction(_1021, _1022, _1023, _1024, _1027);
						float3 _1015;
						_1015 = _1020;
						float3 _1034 = _741.direction;
						float _1035 = 0.500000;
						float _1036 = _811.y;
						float _1037 = _811.y;
						float _1038 = _1037 * _1036;
						float _1039 = _1038 * _1035;
						float3 _1040 = float3(_1039, _1039, _1039);
						float3 _1033 = mix(_1034, _1015, _1040);
						float3 _1032 = normalize(_1033);
						_741.direction = _1032;
						float3 _1042 = _741.direction;
						float3 _1041 = offset_ray(_793, _804, _1042);
						_741.origin = _1041;
						int _1043 = 1;
						_770 += _1043;
						continue;
					}
					uint2 _1045 = _704.xy;
					int _1046 = _778 + _18;
					float _1044 = rnd(_1045, _739, _1046, _707, _706, _8, _10);
					_812 = _1044;
					float3 _813;
					float3 _814;
					if (_808)
					{
						tangent_basis _1051 = create_uv_basis(_797, _798, _799, _785, _786, _787, _805);
						tangent_basis _1047;
						_1047 = _1051;
						float3 _1052 = _1047.tangent;
						float3 _1053 = _806 * _1052;
						_813 = _1053;
						float3 _1054 = _1047.binormal;
						float3 _1055 = _806 * _1054;
						_814 = _1055;
						float _1057 = dot(_803, _813);
						float3 _1058 = _803 * _1057;
						float3 _1059 = _813 - _1058;
						float3 _1056 = normalize(_1059);
						_813 = _1056;
						float _1061 = dot(_813, _814);
						float3 _1062 = _813 * _1061;
						float _1063 = dot(_803, _814);
						float3 _1064 = _803 * _1063;
						float3 _1065 = _814 - _1064;
						float3 _1066 = _1065 - _1062;
						float3 _1060 = normalize(_1066);
						_814 = _1060;
						if (_807)
						{
							float3 _1069 = -_814;
							_814 = _1069;
						}
						float _1071 = 1.000000;
						float _1072 = 2.000000;
						float3 _1073 = _809.xyz;
						float3 _1074 = _1073 * _1072;
						float3 _1075 = _1074 - _1071;
						float3 _1070 = normalize(_1075);
						float3 _1048;
						_1048 = _1070;
						float _1077 = _1048.z;
						float3 _1078 = _803 * _1077;
						float _1079 = _1048.y;
						float3 _1080 = _814 * _1079;
						float _1081 = _1048.x;
						float3 _1082 = _813 * _1081;
						float3 _1083 = _1082 - _1080;
						float3 _1084 = _1083 + _1078;
						float3 _1076 = normalize(_1084);
						_803 = _1076;
						float _1085 = 0.000100;
						float _1086 = dot(_803, _804);
						bool _1087 = _1086 < _1085;
						if (_1087)
						{
							float _1091 = dot(_803, _804);
							float _1092 = 0.000100;
							float _1093 = _1092 - _1091;
							float3 _1094 = _804 * _1093;
							float3 _1095 = _803 + _1094;
							float3 _1090 = normalize(_1095);
							_803 = _1090;
						}
					}
					float3 _1096 = _741.direction;
					float3 _1097 = -_1096;
					float3 _815;
					_815 = _1097;
					float _1098 = dot(_803, _815);
					float _816;
					_816 = _1098;
					float _1099 = 0.001000;
					bool _1100 = _816 < _1099;
					if (_1100)
					{
						int _1104 = 0;
						bool _1105 = _770 > _1104;
						if (_1105)
						{
							break;
						}
						float _1109 = 0.001000;
						float _1110 = _1109 - _816;
						float3 _1111 = _815 * _1110;
						float3 _1112 = _803 + _1111;
						float3 _1108 = normalize(_1112);
						_803 = _1108;
						float _1113 = dot(_803, _804);
						float _1101;
						_1101 = _1113;
						float _1114 = 0.001000;
						bool _1115 = _1101 < _1114;
						if (_1115)
						{
							float _1119 = 0.001000;
							float _1120 = _1119 - _1101;
							float3 _1121 = _804 * _1120;
							float3 _1122 = _803 + _1121;
							float3 _1118 = normalize(_1122);
							_803 = _1118;
						}
						float _1124 = dot(_803, _815);
						float _1125 = 0.001000;
						float _1123 = max(_1124, _1125);
						_816 = _1123;
					}
					tangent_basis _1126 = create_basis(_803);
					tangent_basis _817;
					_817 = _1126;
					float3 _1127 = _817.tangent;
					_813 = _1127;
					float3 _1128 = _817.binormal;
					_814 = _1128;
					float _1130 = _811.z;
					float3 _1129 = surface_albedo(_810, _1130);
					float3 _818;
					_818 = _1129;
					float _1132 = _811.z;
					float3 _1131 = surface_specular(_810, _1132);
					float3 _819;
					_819 = _1131;
					float _1133 = _811.y;
					float _820;
					_820 = _1133;
					float3 _1134 = spec_directional_albedo(_819, _816, _820);
					float3 _821;
					_821 = _1134;
					float _1135 = 1.000000;
					float3 _1136 = _1135 - _821;
					float3 _1137 = _818 * _1136;
					float3 _822;
					_822 = _1137;
					float _1138 = luma(_821);
					float _823;
					_823 = _1138;
					float _1139 = luma(_822);
					float _824;
					_824 = _1139;
					float _1142 = _823 + _824;
					float _1143 = 0.000010;
					float _1141 = max(_1142, _1143);
					float _1144 = _823 / _1141;
					float _1145 = 0.050000;
					float _1146 = 0.995000;
					float _1140 = clamp(_1144, _1145, _1146);
					float _825;
					_825 = _1140;
					float _1148 = _820 * _820;
					float _1149 = 0.001000;
					float _1147 = max(_1148, _1149);
					float _826;
					_826 = _1147;
					uint2 _1151 = _704.xy;
					int _1152 = _778 + _19;
					float _1150 = rnd(_1151, _739, _1152, _707, _706, _8, _10);
					float _827;
					_827 = _1150;
					uint2 _1154 = _704.xy;
					int _1155 = 1;
					int _1156 = _778 + _19;
					int _1157 = _1156 + _1155;
					float _1153 = rnd(_1154, _739, _1157, _707, _706, _8, _10);
					float _828;
					_828 = _1153;
					bool _1158 = _812 < _825;
					if (_1158)
					{
						float _1168 = _826 * _826;
						float _1159;
						_1159 = _1168;
						float _1170 = dot(_815, _813);
						float _1171 = dot(_815, _814);
						float3 _1169 = float3(_1170, _1171, _816);
						float3 _1160;
						_1160 = _1169;
						float3 _1172 = sample_ggx_vndf(_1160, _826, _827, _828);
						float3 _1161;
						_1161 = _1172;
						float3 _1174 = -_1160;
						float3 _1173 = reflect(_1174, _1161);
						float3 _1162;
						_1162 = _1173;
						float _1175 = 0.000000;
						float _1176 = _1162.z;
						bool _1177 = _1176 <= _1175;
						if (_1177)
						{
							break;
						}
						float _1180 = _1162.z;
						float3 _1181 = _803 * _1180;
						float _1182 = _1162.y;
						float3 _1183 = _814 * _1182;
						float _1184 = _1162.x;
						float3 _1185 = _813 * _1184;
						float3 _1186 = _1185 + _1183;
						float3 _1187 = _1186 + _1181;
						_741.direction = _1187;
						float _1189 = _1160.z;
						float _1188 = smith_lambda(_1189, _1159);
						float _1163;
						_1163 = _1188;
						float _1191 = _1162.z;
						float _1190 = smith_lambda(_1191, _1159);
						float _1164;
						_1164 = _1190;
						float _1192 = 1.000000;
						float _1193 = _1192 + _1163;
						float _1194 = _1193 + _1164;
						float _1195 = 1.000000;
						float _1196 = _1195 + _1163;
						float _1197 = _1196 / _1194;
						float _1165;
						_1165 = _1197;
						float _1198 = _1165 / _825;
						float _1201 = dot(_1160, _1161);
						float _1202 = 0.000000;
						float _1200 = max(_1201, _1202);
						float3 _1199 = f_schlick(_819, _1200);
						float3 _1203 = _1199 * _1198;
						_742 *= _1203;
					}
					bool _1204 = !_1158;
					if (_1204)
					{
						float3 _1207 = cos_weighted_direction(_813, _814, _803, _827, _828);
						_741.direction = _1207;
						float _1208 = 1.000000;
						float _1209 = _1208 - _825;
						float3 _1210 = _822 / _1209;
						_742 *= _1210;
					}
					float _1211 = 0.000000;
					float3 _1213 = _741.direction;
					float _1212 = dot(_1213, _804);
					bool _1214 = _1212 <= _1211;
					if (_1214)
					{
						break;
					}
					float _1217 = 0.000000;
					float _1220 = _742.x;
					float _1221 = _742.y;
					float _1219 = max(_1220, _1221);
					float _1222 = _742.z;
					float _1218 = max(_1219, _1222);
					bool _1223 = _1218 <= _1217;
					if (_1223)
					{
						break;
					}
					float3 _1227 = _741.direction;
					float3 _1226 = offset_ray(_793, _804, _1227);
					_741.origin = _1226;
					int _1228 = 2;
					int _1229 = 3;
					float _1231 = 255.000000;
					float _1232 = _809.w;
					float _1233 = _1232 * _1231;
					int _1230 = int(_1233);
					int _1234 = _1230 % _1229;
					bool _1235 = _1234 == _1228;
					if (_1235)
					{
						float _1240 = 10.000000;
						float _1242 = 2.000000;
						float _1243 = _792 * _1242;
						float _1244 = 1.000000;
						float _1241 = min(_1243, _1244);
						float _1245 = 1.000000;
						float _1246 = _1245 / _1241;
						float _1247 = _1246 / _1240;
						float _1248 = 0.500000;
						float _1239 = min(_1247, _1248);
						float _1236;
						_1236 = _1239;
						float3 _1249 = _742 * _1236;
						_742 += _1249;
						float _1250 = 0.500000;
						bool _1251 = _812 < _1250;
						if (_1251)
						{
							float _1254 = 0.001000;
							float3 _1255 = _741.direction;
							float3 _1256 = _1255 * _812;
							float3 _1257 = _1256 * _1254;
							_741.origin += _1257;
						}
					}
					int _1258 = 1;
					_770 += _1258;
				}
				}
			}
			int _1259 = 1;
			_731 += _1259;
		}
		}
	}
	uint2 _1261 = _704.xy;
	float4 _1260 = _3.read(_1261);
	float4 _709;
	_709 = _1260;
	float3 _1262 = _709.xyz;
	float3 _710;
	_710 = _1262;
	float _1264 = float(_13);
	float _1263 = float(_1264);
	float3 _1265 = _708 / _1263;
	_708 = _1265;
	float _1266 = 1.000000;
	float _1267 = _1.eye.w;
	float _1268 = _1267 + _1266;
	float _1269 = 1.000000;
	float _1270 = _1269 / _1268;
	float _711;
	_711 = _1270;
	float3 _1272 = float3(_711, _711, _711);
	float3 _1271 = mix(_710, _708, _1272);
	_710 = _1271;
	float _1274 = 1.000000;
	float4 _1273 = float4(_710, _1274);
	uint2 _1275 = _704.xy;
	_3.write(_1273, _1275);
}
