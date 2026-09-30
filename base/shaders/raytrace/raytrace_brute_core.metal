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

constant float _20 = 3.14159274;

constant float _21 = 6.28318548;

constant int _26 = 16;

constant int _25 = 32768;

constant int _23 = 256;

constant int _24 = 128;

constant float _22 = 19.739208;

constant int _13 = 4;

constant int _14 = 3;

constant int _19 = 5;

constant int _15 = 2;

constant int _16 = 0;

constant int _18 = 3;

constant int _17 = 1;

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
float env_pdf(float3 _826, constant _1_type &_1, texture2d<float> _11);
int clamp_int(int _704, int _705, int _706);
float mis_weight(float _874, float _875);
float2 s16_to_f32(uint _27);
float2 hit_attribute2d(float2 _57, float2 _58, float2 _59, float2 _60);
float3 hit_attribute(float3 _45, float3 _46, float3 _47, float2 _48);
float3 srgb_to_linear(float3 _291);
tangent_basis create_uv_basis(float3 _373, float3 _374, float3 _375, float2 _376, float2 _377, float2 _378, float3 _379);
tangent_basis create_basis(float3 _212);
float3 surface_albedo(float3 _269, float _270);
float3 surface_specular(float3 _277, float _278);
float3 spec_directional_albedo(float3 _314, float _315, float _316);
float luma(float3 _285);
float4 sample_env(float _732, float _733, constant _1_type &_1, texture2d<float> _11);
float3 env_dir(float2 _713, constant _1_type &_1);
float4 bsdf_eval(float3 _884, float3 _885, float3 _886, float _887, float3 _888, float3 _889, float _890, float _891);
float smith_lambda(float _356, float _357);
float3 f_schlick(float3 _300, float _301);
float3 offset_ray(float3 _558, float3 _559, float3 _560);
bool occluded(float3 _968, float3 _969, instance_acceleration_structure _2);
float3 sample_ggx_vndf(float3 _470, float _471, float _472, float _473);
float3 cos_weighted_direction(float3 _447, float3 _448, float3 _449, float _450, float _451);

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
	float _207 = _203 + _20;
	float _208 = _207 + _195;
	float _197;
	_197 = _208;
	float _210 = _197 / _21;
	float _211 = _196 / _20;
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
	float _455 = _21 * _451;
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
	float _515 = _21 * _473;
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

int clamp_int(int _704, int _705, int _706) {
	bool _707 = _704 < _705;
	if (_707)
	{
		return _705;
	}
	bool _710 = _704 > _706;
	if (_710)
	{
		return _706;
	}
	return _704;
}

float3 env_dir(float2 _713, constant _1_type &_1) {
	float _717 = _713.y;
	float _718 = _717 * _20;
	float _714;
	_714 = _718;
	float _719 = _1.params.y;
	float _720 = _713.x;
	float _721 = _720 * _21;
	float _722 = _721 - _20;
	float _723 = _722 - _719;
	float _715;
	_715 = _723;
	float _724 = sin(_714);
	float _716;
	_716 = _724;
	float _726 = cos(_715);
	float _727 = _716 * _726;
	float _728 = sin(_715);
	float _729 = -_716;
	float _730 = _729 * _728;
	float _731 = cos(_714);
	float3 _725 = float3(_727, _730, _731);
	return _725;
}

float4 sample_env(float _732, float _733, constant _1_type &_1, texture2d<float> _11) {
	float _745 = float(_25);
	float _744 = float(_745);
	float _746 = _732 * _744;
	float _734;
	_734 = _746;
	int _748 = int(_734);
	int _749 = 0;
	int _750 = 1;
	int _751 = _25 - _750;
	int _747 = clamp_int(_748, _749, _751);
	int _735;
	_735 = _747;
	float _753 = float(_735);
	float _752 = float(_753);
	float _754 = _734 - _752;
	float _736;
	_736 = _754;
	int _758 = 256;
	int _759 = _735 % _758;
	uint _757 = uint(_759);
	int _761 = 256;
	int _762 = _735 / _761;
	uint _760 = uint(_762);
	uint2 _756 = uint2(_757, _760);
	float4 _755 = _11.read(_756);
	float4 _737;
	_737 = _755;
	int _738;
	float _763 = 0.000000;
	float _739;
	_739 = _763;
	float _740;
	float _764 = _737.x;
	bool _765 = _733 < _764;
	if (_765)
	{
		_738 = _735;
		float _768 = 0.000000;
		float _769 = _737.x;
		bool _770 = _769 > _768;
		if (_770)
		{
			float _773 = _737.x;
			float _774 = _733 / _773;
			_739 = _774;
		}
		float _775 = _737.z;
		_740 = _775;
	}
	bool _776 = !_765;
	if (_776)
	{
		float _781 = _737.y;
		int _780 = int(_781);
		int _782 = 0;
		int _783 = 1;
		int _784 = _25 - _783;
		int _779 = clamp_int(_780, _782, _784);
		_738 = _779;
		float _785 = 1.000000;
		float _786 = _737.x;
		bool _787 = _786 < _785;
		if (_787)
		{
			float _790 = _737.x;
			float _791 = 1.000000;
			float _792 = _791 - _790;
			float _793 = _737.x;
			float _794 = _733 - _793;
			float _795 = _794 / _792;
			_739 = _795;
		}
		float _796 = _737.w;
		_740 = _796;
	}
	float _799 = float(_23);
	float _798 = float(_799);
	int _802 = 256;
	int _803 = _738 % _802;
	float _801 = float(_803);
	float _800 = float(_801);
	float _804 = _800 + _736;
	float _805 = _804 / _798;
	float _807 = float(_24);
	float _806 = float(_807);
	int _810 = 256;
	int _811 = _738 / _810;
	float _809 = float(_811);
	float _808 = float(_809);
	float _812 = _808 + _739;
	float _813 = _812 / _806;
	float2 _797 = float2(_805, _813);
	float2 _741;
	_741 = _797;
	float _815 = _741.y;
	float _816 = _815 * _20;
	float _814 = sin(_816);
	float _742;
	_742 = _814;
	float _817 = 0.000000;
	float _743;
	_743 = _817;
	float _818 = 0.000001;
	bool _819 = _742 > _818;
	if (_819)
	{
		float _822 = _22 * _742;
		float _823 = _740 / _822;
		_743 = _823;
	}
	float3 _825 = env_dir(_741, _1);
	float4 _824 = float4(_825, _743);
	return _824;
}

float env_pdf(float3 _826, constant _1_type &_1, texture2d<float> _11) {
	float _832 = 0.000000;
	float _833 = _1.params.w;
	bool _834 = _833 == _832;
	if (_834)
	{
		float _837 = 0.000000;
		return _837;
	}
	float _839 = _1.params.y;
	float2 _838 = equirect(_826, _839);
	float2 _827;
	_827 = _838;
	float _843 = float(_23);
	float _842 = float(_843);
	float _845 = _827.x;
	float _844 = fract(_845);
	float _846 = _844 * _842;
	int _841 = int(_846);
	int _847 = 0;
	int _848 = 1;
	int _849 = _23 - _848;
	int _840 = clamp_int(_841, _847, _849);
	int _828;
	_828 = _840;
	float _853 = float(_24);
	float _852 = float(_853);
	float _854 = _827.y;
	float _855 = _854 * _852;
	int _851 = int(_855);
	int _856 = 0;
	int _857 = 1;
	int _858 = _24 - _857;
	int _850 = clamp_int(_851, _856, _858);
	int _829;
	_829 = _850;
	uint _861 = uint(_828);
	uint _862 = uint(_829);
	uint2 _860 = uint2(_861, _862);
	float4 _859 = _11.read(_860);
	float4 _830;
	_830 = _859;
	float _864 = _827.y;
	float _865 = _864 * _20;
	float _863 = sin(_865);
	float _831;
	_831 = _863;
	float _866 = 0.000001;
	bool _867 = _831 > _866;
	if (_867)
	{
		float _870 = _22 * _831;
		float _871 = _830.z;
		float _872 = _871 / _870;
		return _872;
	}
	float _873 = 0.000000;
	return _873;
}

float mis_weight(float _874, float _875) {
	float _878 = _874 * _874;
	float _876;
	_876 = _878;
	float _879 = _875 * _875;
	float _877;
	_877 = _879;
	float _881 = _876 + _877;
	float _882 = 9.99999972e-10;
	float _880 = max(_881, _882);
	float _883 = _876 / _880;
	return _883;
}

float4 bsdf_eval(float3 _884, float3 _885, float3 _886, float _887, float3 _888, float3 _889, float _890, float _891) {
	float _906 = dot(_885, _884);
	float _892;
	_892 = _906;
	float _907 = 0.000000;
	bool _908 = _892 <= _907;
	if (_908)
	{
		float _912 = 0.000000;
		float _913 = 0.000000;
		float _914 = 0.000000;
		float _915 = 0.000000;
		float4 _911 = float4(_912, _913, _914, _915);
		return _911;
	}
	float3 _917 = _886 + _884;
	float3 _916 = normalize(_917);
	float3 _893;
	_893 = _916;
	float _919 = dot(_885, _893);
	float _920 = 0.000000;
	float _918 = max(_919, _920);
	float _894;
	_894 = _918;
	float _922 = dot(_886, _893);
	float _923 = 0.000000;
	float _921 = max(_922, _923);
	float _895;
	_895 = _921;
	float _924 = _890 * _890;
	float _896;
	_896 = _924;
	float _925 = 1.000000;
	float _926 = 1.000000;
	float _927 = _896 - _926;
	float _928 = _894 * _894;
	float _929 = _928 * _927;
	float _930 = _929 + _925;
	float _897;
	_897 = _930;
	float _932 = _20 * _897;
	float _933 = _932 * _897;
	float _934 = 9.99999972e-10;
	float _931 = max(_933, _934);
	float _935 = _896 / _931;
	float _898;
	_898 = _935;
	float _936 = smith_lambda(_887, _896);
	float _899;
	_899 = _936;
	float _937 = smith_lambda(_892, _896);
	float _900;
	_900 = _937;
	float3 _938 = f_schlick(_889, _895);
	float3 _901;
	_901 = _938;
	float _940 = 4.000000;
	float _941 = _940 * _887;
	float _942 = _941 * _892;
	float _943 = 9.99999972e-10;
	float _939 = max(_942, _943);
	float _944 = 1.000000;
	float _945 = _944 + _899;
	float _946 = _945 + _900;
	float _947 = _946 * _939;
	float _948 = _898 / _947;
	float3 _949 = _901 * _948;
	float3 _902;
	_902 = _949;
	float3 _950 = _888 / _20;
	float3 _903;
	_903 = _950;
	float _952 = 4.000000;
	float _953 = _952 * _887;
	float _954 = 9.99999972e-10;
	float _951 = max(_953, _954);
	float _955 = 1.000000;
	float _956 = _955 + _899;
	float _957 = _956 * _951;
	float _958 = _898 / _957;
	float _904;
	_904 = _958;
	float _959 = _892 / _20;
	float _905;
	_905 = _959;
	float3 _961 = _903 + _902;
	float3 _962 = _961 * _892;
	float _963 = 1.000000;
	float _964 = _963 - _891;
	float _965 = _964 * _905;
	float _966 = _891 * _904;
	float _967 = _966 + _965;
	float4 _960 = float4(_962, _967);
	return _960;
}

bool occluded(float3 _968, float3 _969, instance_acceleration_structure _2) {
	ray _970;
	_970.origin = _968;
	_970.direction = _969;
	float _972 = 0.000100;
	_970.min_distance = _972;
	float _973 = 100.000000;
	_970.max_distance = _973;
	_kong_intersector::result_type _971;
	{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(true); _971 = i.intersect(_970, _2); }
	bool _975 = _971.type == intersection_type::triangle;
	return _975;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _7 [[texture(4)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]], texture2d<float> _11 [[texture(8)]], sampler _12 [[sampler(0)]], constant _kong_instance *_kong_instances [[buffer(2)]], constant _kong_geometry_textures *_kong_geometry_textures [[buffer(3)]]) {
	uint3 _984 = _kong_dispatch_thread_id;
	uint3 _976;
	_976 = _984;
	uint2 _985 = uint2(_3.get_width(), _3.get_height());
	uint2 _977;
	_977 = _985;
	uint _986 = _977.y;
	uint _987 = _976.y;
	bool _988 = _987 >= _986;
	uint _989 = _977.x;
	uint _990 = _976.x;
	bool _991 = _990 >= _989;
	bool _992 = _991 || _988;
	if (_992)
	{
		return;
	}
	float _996 = _1.eye.w;
	int _995 = int(_996);
	int _978;
	_978 = _995;
	uint2 _998 = _976.xy;
	sampler_cache _997 = init_sampler(_998, _978, _9, _10);
	sampler_cache _979;
	_979 = _997;
	float _1000 = 0.000000;
	float _1001 = 0.000000;
	float _1002 = 0.000000;
	float3 _999 = float3(_1000, _1001, _1002);
	float3 _980;
	_980 = _999;
	{
		int _1006 = 0;
		int _1003;
		_1003 = _1006;
		while (true)
{
		bool _1010 = _1003 < _13;
		if (!_1010) { break; }
		{
			int _1018 = _978 * _13;
			int _1019 = _1018 + _1003;
			int _1011;
			_1011 = _1019;
			uint2 _1021 = _976.xy;
			float2 _1020 = float2(_1021);
			float2 _1012;
			_1012 = _1020;
			uint2 _1023 = _976.xy;
			int _1024 = 0;
			float _1022 = rnd(_1023, _1011, _1024, _979, _978, _8, _10);
			_1012.x += _1022;
			uint2 _1026 = _976.xy;
			int _1027 = 1;
			float _1025 = rnd(_1026, _1011, _1027, _979, _978, _8, _10);
			_1012.y += _1025;
			ray _1013;
			float _1028 = 0.000100;
			_1013.min_distance = _1028;
			float _1029 = 100.000000;
			_1013.max_distance = _1029;
			float3 _1030 = _1.eye.xyz;
			_1013.origin = _1030;
			float _1032 = 1.000000;
			float _1033 = 2.000000;
			float2 _1034 = float2(_977);
			float2 _1035 = _1012 / _1034;
			float2 _1036 = _1035 * _1033;
			float2 _1037 = _1036 - _1032;
			float3 _1038 = _1.eye.xyz;
			float3 _1031 = camera_ray_direction(_1037, _1038, _1);
			_1013.direction = _1031;
			float _1040 = 1.000000;
			float _1041 = 1.000000;
			float _1042 = 1.000000;
			float3 _1039 = float3(_1040, _1041, _1042);
			float3 _1014;
			_1014 = _1039;
			float _1043 = -1.000000;
			float _1015;
			_1015 = _1043;
			{
				int _1047 = 0;
				int _1044;
				_1044 = _1047;
				while (true)
{
				bool _1051 = _1044 < _14;
				if (!_1051) { break; }
				{
					int _1106 = _1044 * _19;
					int _1107 = _15 + _1106;
					int _1052;
					_1052 = _1107;
					_kong_intersector::result_type _1053;
					{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _1053 = i.intersect(_1013, _2); }
					bool _1109 = _1053.type == intersection_type::triangle;
					bool _1110 = !_1109;
					if (_1110)
					{
						float3 _1111;
						float _1115 = 1.000000;
						float _1112;
						_1112 = _1115;
						float _1116 = 0.000000;
						float _1117 = _1.params.x;
						bool _1118 = _1117 < _1116;
						int _1119 = 0;
						bool _1120 = _1044 == _1119;
						bool _1121 = _1120 && _1118;
						if (_1121)
						{
							float _1125 = 0.027500;
							float _1126 = 0.027500;
							float _1127 = 0.027500;
							float3 _1124 = float3(_1125, _1126, _1127);
							_1111 = _1124;
						}
						bool _1128 = !_1121;
						if (_1128)
						{
							float3 _1132 = _1013.direction;
							float3 _1131 = env_radiance(_1132, _1, _7, _12);
							_1111 = _1131;
							float _1133 = 0.000000;
							bool _1134 = _1015 > _1133;
							if (_1134)
							{
								float3 _1139 = _1013.direction;
								float _1138 = env_pdf(_1139, _1, _11);
								float _1137 = mis_weight(_1015, _1138);
								_1112 = _1137;
							}
						}
						float3 _1141 = _1014 * _1111;
						float3 _1142 = _1141 * _1112;
						float _1143 = 0.000000;
						float3 _1144 = float3(_1143, _1143, _1143);
						float _1145 = 8.000000;
						float3 _1146 = float3(_1145, _1145, _1145);
						float3 _1140 = clamp(_1142, _1144, _1146);
						_980 += _1140;
						break;
					}
					uint _1147 = _kong_instances[_1053.user_instance_id].geometry;
					uint _1054;
					_1054 = _1147;
					float2 _1148 = _1053.triangle_barycentric_coord;
					float2 _1055;
					_1055 = _1148;
					int _1150 = 0;
					uint4 _1149 = _kong_vertex(_kong_instances[_1053.user_instance_id], _1053.primitive_id * 3 + _1150);
					uint4 _1056;
					_1056 = _1149;
					int _1152 = 1;
					uint4 _1151 = _kong_vertex(_kong_instances[_1053.user_instance_id], _1053.primitive_id * 3 + _1152);
					uint4 _1057;
					_1057 = _1151;
					int _1154 = 2;
					uint4 _1153 = _kong_vertex(_kong_instances[_1053.user_instance_id], _1053.primitive_id * 3 + _1154);
					uint4 _1058;
					_1058 = _1153;
					uint _1156 = _1056.w;
					float2 _1155 = s16_to_f32(_1156);
					float2 _1059;
					_1059 = _1155;
					uint _1158 = _1057.w;
					float2 _1157 = s16_to_f32(_1158);
					float2 _1060;
					_1060 = _1157;
					uint _1160 = _1058.w;
					float2 _1159 = s16_to_f32(_1160);
					float2 _1061;
					_1061 = _1159;
					float _1161 = _1.params.z;
					float2 _1162 = hit_attribute2d(_1059, _1060, _1061, _1055);
					float2 _1163 = _1162 * _1161;
					float2 _1062;
					_1062 = _1163;
					uint2 _1164 = uint2(_kong_geometry_textures[_1054].texpaint0.get_width(), _kong_geometry_textures[_1054].texpaint0.get_height());
					uint2 _1063;
					_1063 = _1164;
					float2 _1166 = float2(_1063);
					float2 _1167 = fract(_1062);
					float2 _1168 = _1167 * _1166;
					uint2 _1165 = uint2(_1168);
					uint2 _1064;
					_1064 = _1165;
					float4 _1169 = _kong_geometry_textures[_1054].texpaint0.read(_1064);
					float4 _1065;
					_1065 = _1169;
					float _1170 = _1053.distance;
					float _1066;
					_1066 = _1170;
					float3 _1171 = _1013.direction;
					float3 _1172 = _1171 * _1066;
					float3 _1173 = _1013.origin;
					float3 _1174 = _1173 + _1172;
					float3 _1067;
					_1067 = _1174;
					uint _1176 = _1056.y;
					float2 _1175 = s16_to_f32(_1176);
					float2 _1068;
					_1068 = _1175;
					uint _1178 = _1057.y;
					float2 _1177 = s16_to_f32(_1178);
					float2 _1069;
					_1069 = _1177;
					uint _1180 = _1058.y;
					float2 _1179 = s16_to_f32(_1180);
					float2 _1070;
					_1070 = _1179;
					uint _1183 = _1056.x;
					float2 _1182 = s16_to_f32(_1183);
					float _1184 = _1068.x;
					float3 _1181 = float3(_1182, _1184);
					float3 _1071;
					_1071 = _1181;
					uint _1187 = _1057.x;
					float2 _1186 = s16_to_f32(_1187);
					float _1188 = _1069.x;
					float3 _1185 = float3(_1186, _1188);
					float3 _1072;
					_1072 = _1185;
					uint _1191 = _1058.x;
					float2 _1190 = s16_to_f32(_1191);
					float _1192 = _1070.x;
					float3 _1189 = float3(_1190, _1192);
					float3 _1073;
					_1073 = _1189;
					uint _1195 = _1056.z;
					float2 _1194 = s16_to_f32(_1195);
					float _1196 = _1068.y;
					float3 _1193 = float3(_1194, _1196);
					float3 _1074;
					_1074 = _1193;
					uint _1199 = _1057.z;
					float2 _1198 = s16_to_f32(_1199);
					float _1200 = _1069.y;
					float3 _1197 = float3(_1198, _1200);
					float3 _1075;
					_1075 = _1197;
					uint _1203 = _1058.z;
					float2 _1202 = s16_to_f32(_1203);
					float _1204 = _1070.y;
					float3 _1201 = float3(_1202, _1204);
					float3 _1076;
					_1076 = _1201;
					float3 _1206 = hit_attribute(_1074, _1075, _1076, _1055);
					float3 _1205 = normalize(_1206);
					float3 _1077;
					_1077 = _1205;
					float3 _1208 = _1072 - _1071;
					float3 _1209 = _1073 - _1071;
					float3 _1207 = cross(_1208, _1209);
					float3 _1078;
					_1078 = _1207;
					float _1210 = 9.99999968e-21;
					float _1211 = dot(_1078, _1078);
					bool _1212 = _1211 > _1210;
					if (_1212)
					{
						float3 _1215 = normalize(_1078);
						_1078 = _1215;
					}
					bool _1216 = !_1212;
					if (_1216)
					{
						_1078 = _1077;
					}
					float _1219 = 0.000000;
					float _1220 = dot(_1078, _1077);
					bool _1221 = _1220 < _1219;
					if (_1221)
					{
						float3 _1224 = -_1078;
						_1078 = _1224;
					}
					float3 _1079;
					_1079 = _1077;
					float3x3 _1225 = float3x3(_1053.object_to_world_transform[0], _1053.object_to_world_transform[1], _1053.object_to_world_transform[2]);
					float3x3 _1080;
					_1080 = _1225;
					float3 _1227 = _1080 * _1077;
					float3 _1226 = normalize(_1227);
					_1077 = _1226;
					float3 _1229 = _1080 * _1078;
					float3 _1228 = normalize(_1229);
					_1078 = _1228;
					float _1230 = 0.000000;
					float3 _1232 = _1013.direction;
					float _1231 = dot(_1078, _1232);
					bool _1233 = _1231 > _1230;
					bool _1081;
					_1081 = _1233;
					if (_1081)
					{
						float3 _1236 = -_1078;
						_1078 = _1236;
						float3 _1237 = -_1077;
						_1077 = _1237;
					}
					int _1238 = 0;
					bool _1239 = _1044 == _1238;
					bool _1082;
					_1082 = _1239;
					float _1241 = 0.000000;
					float _1242 = 0.000000;
					float _1243 = 0.000000;
					float _1244 = 0.000000;
					float4 _1240 = float4(_1241, _1242, _1243, _1244);
					float4 _1083;
					_1083 = _1240;
					if (_1082)
					{
						float4 _1247 = _kong_geometry_textures[_1054].texpaint1.read(_1064);
						_1083 = _1247;
					}
					float3 _1249 = _1065.xyz;
					float3 _1248 = srgb_to_linear(_1249);
					float3 _1084;
					_1084 = _1248;
					float4 _1250 = _kong_geometry_textures[_1054].texpaint2.read(_1064);
					float4 _1085;
					_1085 = _1250;
					uint2 _1252 = _976.xy;
					int _1253 = _1052 + _16;
					float _1251 = rnd(_1252, _1011, _1253, _979, _978, _8, _10);
					float _1086;
					_1086 = _1251;
					float3 _1087;
					float3 _1088;
					if (_1082)
					{
						tangent_basis _1258 = create_uv_basis(_1071, _1072, _1073, _1059, _1060, _1061, _1079);
						tangent_basis _1254;
						_1254 = _1258;
						float3 _1259 = _1254.tangent;
						float3 _1260 = _1080 * _1259;
						_1087 = _1260;
						float3 _1261 = _1254.binormal;
						float3 _1262 = _1080 * _1261;
						_1088 = _1262;
						float _1264 = dot(_1077, _1087);
						float3 _1265 = _1077 * _1264;
						float3 _1266 = _1087 - _1265;
						float3 _1263 = normalize(_1266);
						_1087 = _1263;
						float _1268 = dot(_1087, _1088);
						float3 _1269 = _1087 * _1268;
						float _1270 = dot(_1077, _1088);
						float3 _1271 = _1077 * _1270;
						float3 _1272 = _1088 - _1271;
						float3 _1273 = _1272 - _1269;
						float3 _1267 = normalize(_1273);
						_1088 = _1267;
						if (_1081)
						{
							float3 _1276 = -_1088;
							_1088 = _1276;
						}
						float _1278 = 1.000000;
						float _1279 = 2.000000;
						float3 _1280 = _1083.xyz;
						float3 _1281 = _1280 * _1279;
						float3 _1282 = _1281 - _1278;
						float3 _1277 = normalize(_1282);
						float3 _1255;
						_1255 = _1277;
						float _1284 = _1255.z;
						float3 _1285 = _1077 * _1284;
						float _1286 = _1255.y;
						float3 _1287 = _1088 * _1286;
						float _1288 = _1255.x;
						float3 _1289 = _1087 * _1288;
						float3 _1290 = _1289 - _1287;
						float3 _1291 = _1290 + _1285;
						float3 _1283 = normalize(_1291);
						_1077 = _1283;
						float _1292 = 0.000100;
						float _1293 = dot(_1077, _1078);
						bool _1294 = _1293 < _1292;
						if (_1294)
						{
							float _1298 = dot(_1077, _1078);
							float _1299 = 0.000100;
							float _1300 = _1299 - _1298;
							float3 _1301 = _1078 * _1300;
							float3 _1302 = _1077 + _1301;
							float3 _1297 = normalize(_1302);
							_1077 = _1297;
						}
					}
					float3 _1303 = _1013.direction;
					float3 _1304 = -_1303;
					float3 _1089;
					_1089 = _1304;
					float _1305 = dot(_1077, _1089);
					float _1090;
					_1090 = _1305;
					float _1306 = 0.001000;
					bool _1307 = _1090 < _1306;
					if (_1307)
					{
						int _1311 = 0;
						bool _1312 = _1044 > _1311;
						if (_1312)
						{
							break;
						}
						float _1316 = 0.001000;
						float _1317 = _1316 - _1090;
						float3 _1318 = _1089 * _1317;
						float3 _1319 = _1077 + _1318;
						float3 _1315 = normalize(_1319);
						_1077 = _1315;
						float _1320 = dot(_1077, _1078);
						float _1308;
						_1308 = _1320;
						float _1321 = 0.001000;
						bool _1322 = _1308 < _1321;
						if (_1322)
						{
							float _1326 = 0.001000;
							float _1327 = _1326 - _1308;
							float3 _1328 = _1078 * _1327;
							float3 _1329 = _1077 + _1328;
							float3 _1325 = normalize(_1329);
							_1077 = _1325;
						}
						float _1331 = dot(_1077, _1089);
						float _1332 = 0.001000;
						float _1330 = max(_1331, _1332);
						_1090 = _1330;
					}
					tangent_basis _1333 = create_basis(_1077);
					tangent_basis _1091;
					_1091 = _1333;
					float3 _1334 = _1091.tangent;
					_1087 = _1334;
					float3 _1335 = _1091.binormal;
					_1088 = _1335;
					float _1337 = _1085.z;
					float3 _1336 = surface_albedo(_1084, _1337);
					float3 _1092;
					_1092 = _1336;
					float _1339 = _1085.z;
					float3 _1338 = surface_specular(_1084, _1339);
					float3 _1093;
					_1093 = _1338;
					float _1340 = _1085.y;
					float _1094;
					_1094 = _1340;
					float3 _1341 = spec_directional_albedo(_1093, _1090, _1094);
					float3 _1095;
					_1095 = _1341;
					float _1342 = 1.000000;
					float3 _1343 = _1342 - _1095;
					float3 _1344 = _1092 * _1343;
					float3 _1096;
					_1096 = _1344;
					float _1345 = luma(_1095);
					float _1097;
					_1097 = _1345;
					float _1346 = luma(_1096);
					float _1098;
					_1098 = _1346;
					float _1349 = _1097 + _1098;
					float _1350 = 0.000010;
					float _1348 = max(_1349, _1350);
					float _1351 = _1097 / _1348;
					float _1352 = 0.050000;
					float _1353 = 0.995000;
					float _1347 = clamp(_1351, _1352, _1353);
					float _1099;
					_1099 = _1347;
					float _1355 = _1094 * _1094;
					float _1356 = 0.001000;
					float _1354 = max(_1355, _1356);
					float _1100;
					_1100 = _1354;
					float _1357 = 0.000000;
					float _1358 = _1.params.w;
					bool _1359 = _1358 != _1357;
					if (_1359)
					{
						uint2 _1367 = _976.xy;
						int _1368 = _1052 + _18;
						float _1366 = rnd(_1367, _1011, _1368, _979, _978, _8, _10);
						uint2 _1370 = _976.xy;
						int _1371 = 1;
						int _1372 = _1052 + _18;
						int _1373 = _1372 + _1371;
						float _1369 = rnd(_1370, _1011, _1373, _979, _978, _8, _10);
						float4 _1365 = sample_env(_1366, _1369, _1, _11);
						float4 _1360;
						_1360 = _1365;
						float3 _1374 = _1360.xyz;
						float3 _1361;
						_1361 = _1374;
						float _1375 = _1360.w;
						float _1362;
						_1362 = _1375;
						float _1376 = 0.000000;
						float _1377 = dot(_1361, _1078);
						bool _1378 = _1377 > _1376;
						float _1379 = 0.000000;
						bool _1380 = _1362 > _1379;
						bool _1381 = _1380 && _1378;
						if (_1381)
						{
							float4 _1386 = bsdf_eval(_1361, _1077, _1089, _1090, _1096, _1093, _1100, _1099);
							float4 _1382;
							_1382 = _1386;
							float3 _1387 = _1382.xyz;
							float3 _1383;
							_1383 = _1387;
							float _1388 = 0.000000;
							float _1391 = _1383.x;
							float _1392 = _1383.y;
							float _1390 = max(_1391, _1392);
							float _1393 = _1383.z;
							float _1389 = max(_1390, _1393);
							bool _1394 = _1389 > _1388;
							if (_1394)
							{
								float3 _1398 = offset_ray(_1067, _1078, _1361);
								bool _1397 = occluded(_1398, _1361, _2);
								bool _1399 = !_1397;
								if (_1399)
								{
									float _1404 = _1382.w;
									float _1403 = mis_weight(_1362, _1404);
									float _1405 = _1403 / _1362;
									float3 _1406 = env_radiance(_1361, _1, _7, _12);
									float3 _1407 = _1014 * _1383;
									float3 _1408 = _1407 * _1406;
									float3 _1409 = _1408 * _1405;
									float _1410 = 0.000000;
									float3 _1411 = float3(_1410, _1410, _1410);
									float _1412 = 8.000000;
									float3 _1413 = float3(_1412, _1412, _1412);
									float3 _1402 = clamp(_1409, _1411, _1413);
									_980 += _1402;
								}
							}
						}
					}
					uint2 _1415 = _976.xy;
					int _1416 = _1052 + _17;
					float _1414 = rnd(_1415, _1011, _1416, _979, _978, _8, _10);
					float _1101;
					_1101 = _1414;
					uint2 _1418 = _976.xy;
					int _1419 = 1;
					int _1420 = _1052 + _17;
					int _1421 = _1420 + _1419;
					float _1417 = rnd(_1418, _1011, _1421, _979, _978, _8, _10);
					float _1102;
					_1102 = _1417;
					bool _1422 = _1086 < _1099;
					if (_1422)
					{
						float _1432 = _1100 * _1100;
						float _1423;
						_1423 = _1432;
						float _1434 = dot(_1089, _1087);
						float _1435 = dot(_1089, _1088);
						float3 _1433 = float3(_1434, _1435, _1090);
						float3 _1424;
						_1424 = _1433;
						float3 _1436 = sample_ggx_vndf(_1424, _1100, _1101, _1102);
						float3 _1425;
						_1425 = _1436;
						float3 _1438 = -_1424;
						float3 _1437 = reflect(_1438, _1425);
						float3 _1426;
						_1426 = _1437;
						float _1439 = 0.000000;
						float _1440 = _1426.z;
						bool _1441 = _1440 <= _1439;
						if (_1441)
						{
							break;
						}
						float _1444 = _1426.z;
						float3 _1445 = _1077 * _1444;
						float _1446 = _1426.y;
						float3 _1447 = _1088 * _1446;
						float _1448 = _1426.x;
						float3 _1449 = _1087 * _1448;
						float3 _1450 = _1449 + _1447;
						float3 _1451 = _1450 + _1445;
						_1013.direction = _1451;
						float _1453 = _1424.z;
						float _1452 = smith_lambda(_1453, _1423);
						float _1427;
						_1427 = _1452;
						float _1455 = _1426.z;
						float _1454 = smith_lambda(_1455, _1423);
						float _1428;
						_1428 = _1454;
						float _1456 = 1.000000;
						float _1457 = _1456 + _1427;
						float _1458 = _1457 + _1428;
						float _1459 = 1.000000;
						float _1460 = _1459 + _1427;
						float _1461 = _1460 / _1458;
						float _1429;
						_1429 = _1461;
						float _1462 = _1429 / _1099;
						float _1465 = dot(_1424, _1425);
						float _1466 = 0.000000;
						float _1464 = max(_1465, _1466);
						float3 _1463 = f_schlick(_1093, _1464);
						float3 _1467 = _1463 * _1462;
						_1014 *= _1467;
					}
					bool _1468 = !_1422;
					if (_1468)
					{
						float3 _1471 = cos_weighted_direction(_1087, _1088, _1077, _1101, _1102);
						_1013.direction = _1471;
						float _1472 = 1.000000;
						float _1473 = _1472 - _1099;
						float3 _1474 = _1096 / _1473;
						_1014 *= _1474;
					}
					float _1475 = 0.000000;
					float3 _1477 = _1013.direction;
					float _1476 = dot(_1477, _1078);
					bool _1478 = _1476 <= _1475;
					if (_1478)
					{
						break;
					}
					float _1481 = 0.000000;
					float _1484 = _1014.x;
					float _1485 = _1014.y;
					float _1483 = max(_1484, _1485);
					float _1486 = _1014.z;
					float _1482 = max(_1483, _1486);
					bool _1487 = _1482 <= _1481;
					if (_1487)
					{
						break;
					}
					float3 _1491 = _1013.direction;
					float3 _1490 = offset_ray(_1067, _1078, _1491);
					_1013.origin = _1490;
					float3 _1493 = _1013.direction;
					float4 _1492 = bsdf_eval(_1493, _1077, _1089, _1090, _1096, _1093, _1100, _1099);
					float4 _1103;
					_1103 = _1492;
					float _1494 = _1103.w;
					_1015 = _1494;
					int _1495 = 1;
					_1044 += _1495;
				}
				}
			}
			int _1496 = 1;
			_1003 += _1496;
		}
		}
	}
	uint2 _1498 = _976.xy;
	float4 _1497 = _3.read(_1498);
	float4 _981;
	_981 = _1497;
	float3 _1499 = _981.xyz;
	float3 _982;
	_982 = _1499;
	float _1501 = float(_13);
	float _1500 = float(_1501);
	float3 _1502 = _980 / _1500;
	_980 = _1502;
	float _1503 = 1.000000;
	float _1504 = _1.eye.w;
	float _1505 = _1504 + _1503;
	float _1506 = 1.000000;
	float _1507 = _1506 / _1505;
	float _983;
	_983 = _1507;
	float3 _1509 = float3(_983, _983, _983);
	float3 _1508 = mix(_982, _980, _1509);
	_982 = _1508;
	float _1511 = 1.000000;
	float4 _1510 = float4(_982, _1511);
	uint2 _1512 = _976.xy;
	_3.write(_1510, _1512);
}
