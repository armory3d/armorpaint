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
float mis_weight(float _927, float _928);
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
float rnd_fine(uint2 _896, int _897, int _898, sampler_cache _899, int _900, texture2d<float> _8, texture2d<float> _10);
uint hash_u32(uint _874);
float4 sample_env(float _732, float _733, constant _1_type &_1, texture2d<float> _11);
float3 env_dir(float2 _713, constant _1_type &_1);
float4 bsdf_eval(float3 _937, float3 _938, float3 _939, float _940, float3 _941, float3 _942, float _943, float _944);
float smith_lambda(float _356, float _357);
float3 f_schlick(float3 _300, float _301);
float3 offset_ray(float3 _558, float3 _559, float3 _560);
bool occluded(float3 _1021, float3 _1022, instance_acceleration_structure _2);
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

uint hash_u32(uint _874) {
	int _876 = 16;
	uint _875 = uint(_876);
	uint _877 = _874 >> _875;
	int _879 = 61;
	uint _878 = uint(_879);
	uint _880 = _874 ^ _878;
	uint _881 = _880 ^ _877;
	_874 = _881;
	int _883 = 9;
	uint _882 = uint(_883);
	uint _884 = _874 * _882;
	_874 = _884;
	int _886 = 4;
	uint _885 = uint(_886);
	uint _887 = _874 >> _885;
	uint _888 = _874 ^ _887;
	_874 = _888;
	int _890 = 668265261;
	uint _889 = uint(_890);
	uint _891 = _874 * _889;
	_874 = _891;
	int _893 = 15;
	uint _892 = uint(_893);
	uint _894 = _874 >> _892;
	uint _895 = _874 ^ _894;
	_874 = _895;
	return _874;
}

float rnd_fine(uint2 _896, int _897, int _898, sampler_cache _899, int _900, texture2d<float> _8, texture2d<float> _10) {
	uint _907 = uint(_898);
	uint _906 = hash_u32(_907);
	uint _908 = uint(_897);
	uint _909 = _908 ^ _906;
	uint _905 = hash_u32(_909);
	uint _910 = _896.y;
	uint _911 = _910 ^ _905;
	uint _904 = hash_u32(_911);
	uint _912 = _896.x;
	uint _913 = _912 ^ _904;
	uint _903 = hash_u32(_913);
	uint _901;
	_901 = _903;
	float _914 = 16777216.000000;
	int _918 = 8;
	uint _917 = uint(_918);
	uint _919 = _901 >> _917;
	float _916 = float(_919);
	float _915 = float(_916);
	float _920 = _915 / _914;
	float _902;
	_902 = _920;
	float _921 = 256.000000;
	float _922 = 0.500000;
	float _923 = _902 - _922;
	float _924 = _923 / _921;
	float _925 = rnd(_896, _897, _898, _899, _900, _8, _10);
	float _926 = _925 + _924;
	return _926;
}

float mis_weight(float _927, float _928) {
	float _931 = _927 * _927;
	float _929;
	_929 = _931;
	float _932 = _928 * _928;
	float _930;
	_930 = _932;
	float _934 = _929 + _930;
	float _935 = 9.99999972e-10;
	float _933 = max(_934, _935);
	float _936 = _929 / _933;
	return _936;
}

float4 bsdf_eval(float3 _937, float3 _938, float3 _939, float _940, float3 _941, float3 _942, float _943, float _944) {
	float _959 = dot(_938, _937);
	float _945;
	_945 = _959;
	float _960 = 0.000000;
	bool _961 = _945 <= _960;
	if (_961)
	{
		float _965 = 0.000000;
		float _966 = 0.000000;
		float _967 = 0.000000;
		float _968 = 0.000000;
		float4 _964 = float4(_965, _966, _967, _968);
		return _964;
	}
	float3 _970 = _939 + _937;
	float3 _969 = normalize(_970);
	float3 _946;
	_946 = _969;
	float _972 = dot(_938, _946);
	float _973 = 0.000000;
	float _971 = max(_972, _973);
	float _947;
	_947 = _971;
	float _975 = dot(_939, _946);
	float _976 = 0.000000;
	float _974 = max(_975, _976);
	float _948;
	_948 = _974;
	float _977 = _943 * _943;
	float _949;
	_949 = _977;
	float _978 = 1.000000;
	float _979 = 1.000000;
	float _980 = _949 - _979;
	float _981 = _947 * _947;
	float _982 = _981 * _980;
	float _983 = _982 + _978;
	float _950;
	_950 = _983;
	float _985 = _20 * _950;
	float _986 = _985 * _950;
	float _987 = 9.99999972e-10;
	float _984 = max(_986, _987);
	float _988 = _949 / _984;
	float _951;
	_951 = _988;
	float _989 = smith_lambda(_940, _949);
	float _952;
	_952 = _989;
	float _990 = smith_lambda(_945, _949);
	float _953;
	_953 = _990;
	float3 _991 = f_schlick(_942, _948);
	float3 _954;
	_954 = _991;
	float _993 = 4.000000;
	float _994 = _993 * _940;
	float _995 = _994 * _945;
	float _996 = 9.99999972e-10;
	float _992 = max(_995, _996);
	float _997 = 1.000000;
	float _998 = _997 + _952;
	float _999 = _998 + _953;
	float _1000 = _999 * _992;
	float _1001 = _951 / _1000;
	float3 _1002 = _954 * _1001;
	float3 _955;
	_955 = _1002;
	float3 _1003 = _941 / _20;
	float3 _956;
	_956 = _1003;
	float _1005 = 4.000000;
	float _1006 = _1005 * _940;
	float _1007 = 9.99999972e-10;
	float _1004 = max(_1006, _1007);
	float _1008 = 1.000000;
	float _1009 = _1008 + _952;
	float _1010 = _1009 * _1004;
	float _1011 = _951 / _1010;
	float _957;
	_957 = _1011;
	float _1012 = _945 / _20;
	float _958;
	_958 = _1012;
	float3 _1014 = _956 + _955;
	float3 _1015 = _1014 * _945;
	float _1016 = 1.000000;
	float _1017 = _1016 - _944;
	float _1018 = _1017 * _958;
	float _1019 = _944 * _957;
	float _1020 = _1019 + _1018;
	float4 _1013 = float4(_1015, _1020);
	return _1013;
}

bool occluded(float3 _1021, float3 _1022, instance_acceleration_structure _2) {
	ray _1023;
	_1023.origin = _1021;
	_1023.direction = _1022;
	float _1025 = 0.000100;
	_1023.min_distance = _1025;
	float _1026 = 100.000000;
	_1023.max_distance = _1026;
	_kong_intersector::result_type _1024;
	{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(true); _1024 = i.intersect(_1023, _2); }
	bool _1028 = _1024.type == intersection_type::triangle;
	return _1028;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _7 [[texture(4)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]], texture2d<float> _11 [[texture(8)]], sampler _12 [[sampler(0)]], constant _kong_instance *_kong_instances [[buffer(2)]], constant _kong_geometry_textures *_kong_geometry_textures [[buffer(3)]]) {
	uint3 _1037 = _kong_dispatch_thread_id;
	uint3 _1029;
	_1029 = _1037;
	uint2 _1038 = uint2(_3.get_width(), _3.get_height());
	uint2 _1030;
	_1030 = _1038;
	uint _1039 = _1030.y;
	uint _1040 = _1029.y;
	bool _1041 = _1040 >= _1039;
	uint _1042 = _1030.x;
	uint _1043 = _1029.x;
	bool _1044 = _1043 >= _1042;
	bool _1045 = _1044 || _1041;
	if (_1045)
	{
		return;
	}
	float _1049 = _1.eye.w;
	int _1048 = int(_1049);
	int _1031;
	_1031 = _1048;
	uint2 _1051 = _1029.xy;
	sampler_cache _1050 = init_sampler(_1051, _1031, _9, _10);
	sampler_cache _1032;
	_1032 = _1050;
	float _1053 = 0.000000;
	float _1054 = 0.000000;
	float _1055 = 0.000000;
	float3 _1052 = float3(_1053, _1054, _1055);
	float3 _1033;
	_1033 = _1052;
	{
		int _1059 = 0;
		int _1056;
		_1056 = _1059;
		while (true)
{
		bool _1063 = _1056 < _13;
		if (!_1063) { break; }
		{
			int _1071 = _1031 * _13;
			int _1072 = _1071 + _1056;
			int _1064;
			_1064 = _1072;
			uint2 _1074 = _1029.xy;
			float2 _1073 = float2(_1074);
			float2 _1065;
			_1065 = _1073;
			uint2 _1076 = _1029.xy;
			int _1077 = 0;
			float _1075 = rnd(_1076, _1064, _1077, _1032, _1031, _8, _10);
			_1065.x += _1075;
			uint2 _1079 = _1029.xy;
			int _1080 = 1;
			float _1078 = rnd(_1079, _1064, _1080, _1032, _1031, _8, _10);
			_1065.y += _1078;
			ray _1066;
			float _1081 = 0.000100;
			_1066.min_distance = _1081;
			float _1082 = 100.000000;
			_1066.max_distance = _1082;
			float3 _1083 = _1.eye.xyz;
			_1066.origin = _1083;
			float _1085 = 1.000000;
			float _1086 = 2.000000;
			float2 _1087 = float2(_1030);
			float2 _1088 = _1065 / _1087;
			float2 _1089 = _1088 * _1086;
			float2 _1090 = _1089 - _1085;
			float3 _1091 = _1.eye.xyz;
			float3 _1084 = camera_ray_direction(_1090, _1091, _1);
			_1066.direction = _1084;
			float _1093 = 1.000000;
			float _1094 = 1.000000;
			float _1095 = 1.000000;
			float3 _1092 = float3(_1093, _1094, _1095);
			float3 _1067;
			_1067 = _1092;
			float _1096 = -1.000000;
			float _1068;
			_1068 = _1096;
			{
				int _1100 = 0;
				int _1097;
				_1097 = _1100;
				while (true)
{
				bool _1104 = _1097 < _14;
				if (!_1104) { break; }
				{
					int _1159 = _1097 * _19;
					int _1160 = _15 + _1159;
					int _1105;
					_1105 = _1160;
					_kong_intersector::result_type _1106;
					{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _1106 = i.intersect(_1066, _2); }
					bool _1162 = _1106.type == intersection_type::triangle;
					bool _1163 = !_1162;
					if (_1163)
					{
						float3 _1164;
						float _1168 = 1.000000;
						float _1165;
						_1165 = _1168;
						float _1169 = 0.000000;
						float _1170 = _1.params.x;
						bool _1171 = _1170 < _1169;
						int _1172 = 0;
						bool _1173 = _1097 == _1172;
						bool _1174 = _1173 && _1171;
						if (_1174)
						{
							float _1178 = 0.027500;
							float _1179 = 0.027500;
							float _1180 = 0.027500;
							float3 _1177 = float3(_1178, _1179, _1180);
							_1164 = _1177;
						}
						bool _1181 = !_1174;
						if (_1181)
						{
							float3 _1185 = _1066.direction;
							float3 _1184 = env_radiance(_1185, _1, _7, _12);
							_1164 = _1184;
							float _1186 = 0.000000;
							bool _1187 = _1068 > _1186;
							if (_1187)
							{
								float3 _1192 = _1066.direction;
								float _1191 = env_pdf(_1192, _1, _11);
								float _1190 = mis_weight(_1068, _1191);
								_1165 = _1190;
							}
						}
						float3 _1194 = _1067 * _1164;
						float3 _1195 = _1194 * _1165;
						float _1196 = 0.000000;
						float3 _1197 = float3(_1196, _1196, _1196);
						float _1198 = 8.000000;
						float3 _1199 = float3(_1198, _1198, _1198);
						float3 _1193 = clamp(_1195, _1197, _1199);
						_1033 += _1193;
						break;
					}
					uint _1200 = _kong_instances[_1106.user_instance_id].geometry;
					uint _1107;
					_1107 = _1200;
					float2 _1201 = _1106.triangle_barycentric_coord;
					float2 _1108;
					_1108 = _1201;
					int _1203 = 0;
					uint4 _1202 = _kong_vertex(_kong_instances[_1106.user_instance_id], _1106.primitive_id * 3 + _1203);
					uint4 _1109;
					_1109 = _1202;
					int _1205 = 1;
					uint4 _1204 = _kong_vertex(_kong_instances[_1106.user_instance_id], _1106.primitive_id * 3 + _1205);
					uint4 _1110;
					_1110 = _1204;
					int _1207 = 2;
					uint4 _1206 = _kong_vertex(_kong_instances[_1106.user_instance_id], _1106.primitive_id * 3 + _1207);
					uint4 _1111;
					_1111 = _1206;
					uint _1209 = _1109.w;
					float2 _1208 = s16_to_f32(_1209);
					float2 _1112;
					_1112 = _1208;
					uint _1211 = _1110.w;
					float2 _1210 = s16_to_f32(_1211);
					float2 _1113;
					_1113 = _1210;
					uint _1213 = _1111.w;
					float2 _1212 = s16_to_f32(_1213);
					float2 _1114;
					_1114 = _1212;
					float _1214 = _1.params.z;
					float2 _1215 = hit_attribute2d(_1112, _1113, _1114, _1108);
					float2 _1216 = _1215 * _1214;
					float2 _1115;
					_1115 = _1216;
					uint2 _1217 = uint2(_kong_geometry_textures[_1107].texpaint0.get_width(), _kong_geometry_textures[_1107].texpaint0.get_height());
					uint2 _1116;
					_1116 = _1217;
					float2 _1219 = float2(_1116);
					float2 _1220 = fract(_1115);
					float2 _1221 = _1220 * _1219;
					uint2 _1218 = uint2(_1221);
					uint2 _1117;
					_1117 = _1218;
					float4 _1222 = _kong_geometry_textures[_1107].texpaint0.read(_1117);
					float4 _1118;
					_1118 = _1222;
					float _1223 = _1106.distance;
					float _1119;
					_1119 = _1223;
					float3 _1224 = _1066.direction;
					float3 _1225 = _1224 * _1119;
					float3 _1226 = _1066.origin;
					float3 _1227 = _1226 + _1225;
					float3 _1120;
					_1120 = _1227;
					uint _1229 = _1109.y;
					float2 _1228 = s16_to_f32(_1229);
					float2 _1121;
					_1121 = _1228;
					uint _1231 = _1110.y;
					float2 _1230 = s16_to_f32(_1231);
					float2 _1122;
					_1122 = _1230;
					uint _1233 = _1111.y;
					float2 _1232 = s16_to_f32(_1233);
					float2 _1123;
					_1123 = _1232;
					uint _1236 = _1109.x;
					float2 _1235 = s16_to_f32(_1236);
					float _1237 = _1121.x;
					float3 _1234 = float3(_1235, _1237);
					float3 _1124;
					_1124 = _1234;
					uint _1240 = _1110.x;
					float2 _1239 = s16_to_f32(_1240);
					float _1241 = _1122.x;
					float3 _1238 = float3(_1239, _1241);
					float3 _1125;
					_1125 = _1238;
					uint _1244 = _1111.x;
					float2 _1243 = s16_to_f32(_1244);
					float _1245 = _1123.x;
					float3 _1242 = float3(_1243, _1245);
					float3 _1126;
					_1126 = _1242;
					uint _1248 = _1109.z;
					float2 _1247 = s16_to_f32(_1248);
					float _1249 = _1121.y;
					float3 _1246 = float3(_1247, _1249);
					float3 _1127;
					_1127 = _1246;
					uint _1252 = _1110.z;
					float2 _1251 = s16_to_f32(_1252);
					float _1253 = _1122.y;
					float3 _1250 = float3(_1251, _1253);
					float3 _1128;
					_1128 = _1250;
					uint _1256 = _1111.z;
					float2 _1255 = s16_to_f32(_1256);
					float _1257 = _1123.y;
					float3 _1254 = float3(_1255, _1257);
					float3 _1129;
					_1129 = _1254;
					float3 _1259 = hit_attribute(_1127, _1128, _1129, _1108);
					float3 _1258 = normalize(_1259);
					float3 _1130;
					_1130 = _1258;
					float3 _1261 = _1125 - _1124;
					float3 _1262 = _1126 - _1124;
					float3 _1260 = cross(_1261, _1262);
					float3 _1131;
					_1131 = _1260;
					float _1263 = 9.99999968e-21;
					float _1264 = dot(_1131, _1131);
					bool _1265 = _1264 > _1263;
					if (_1265)
					{
						float3 _1268 = normalize(_1131);
						_1131 = _1268;
					}
					bool _1269 = !_1265;
					if (_1269)
					{
						_1131 = _1130;
					}
					float _1272 = 0.000000;
					float _1273 = dot(_1131, _1130);
					bool _1274 = _1273 < _1272;
					if (_1274)
					{
						float3 _1277 = -_1131;
						_1131 = _1277;
					}
					float3 _1132;
					_1132 = _1130;
					float3x3 _1278 = float3x3(_1106.object_to_world_transform[0], _1106.object_to_world_transform[1], _1106.object_to_world_transform[2]);
					float3x3 _1133;
					_1133 = _1278;
					float3 _1280 = _1133 * _1130;
					float3 _1279 = normalize(_1280);
					_1130 = _1279;
					float3 _1282 = _1133 * _1131;
					float3 _1281 = normalize(_1282);
					_1131 = _1281;
					float _1283 = 0.000000;
					float3 _1285 = _1066.direction;
					float _1284 = dot(_1131, _1285);
					bool _1286 = _1284 > _1283;
					bool _1134;
					_1134 = _1286;
					if (_1134)
					{
						float3 _1289 = -_1131;
						_1131 = _1289;
						float3 _1290 = -_1130;
						_1130 = _1290;
					}
					int _1291 = 0;
					bool _1292 = _1097 == _1291;
					bool _1135;
					_1135 = _1292;
					float _1294 = 0.000000;
					float _1295 = 0.000000;
					float _1296 = 0.000000;
					float _1297 = 0.000000;
					float4 _1293 = float4(_1294, _1295, _1296, _1297);
					float4 _1136;
					_1136 = _1293;
					if (_1135)
					{
						float4 _1300 = _kong_geometry_textures[_1107].texpaint1.read(_1117);
						_1136 = _1300;
					}
					float3 _1302 = _1118.xyz;
					float3 _1301 = srgb_to_linear(_1302);
					float3 _1137;
					_1137 = _1301;
					float4 _1303 = _kong_geometry_textures[_1107].texpaint2.read(_1117);
					float4 _1138;
					_1138 = _1303;
					uint2 _1305 = _1029.xy;
					int _1306 = _1105 + _16;
					float _1304 = rnd(_1305, _1064, _1306, _1032, _1031, _8, _10);
					float _1139;
					_1139 = _1304;
					float3 _1140;
					float3 _1141;
					if (_1135)
					{
						tangent_basis _1311 = create_uv_basis(_1124, _1125, _1126, _1112, _1113, _1114, _1132);
						tangent_basis _1307;
						_1307 = _1311;
						float3 _1312 = _1307.tangent;
						float3 _1313 = _1133 * _1312;
						_1140 = _1313;
						float3 _1314 = _1307.binormal;
						float3 _1315 = _1133 * _1314;
						_1141 = _1315;
						float _1317 = dot(_1130, _1140);
						float3 _1318 = _1130 * _1317;
						float3 _1319 = _1140 - _1318;
						float3 _1316 = normalize(_1319);
						_1140 = _1316;
						float _1321 = dot(_1140, _1141);
						float3 _1322 = _1140 * _1321;
						float _1323 = dot(_1130, _1141);
						float3 _1324 = _1130 * _1323;
						float3 _1325 = _1141 - _1324;
						float3 _1326 = _1325 - _1322;
						float3 _1320 = normalize(_1326);
						_1141 = _1320;
						if (_1134)
						{
							float3 _1329 = -_1141;
							_1141 = _1329;
						}
						float _1331 = 1.000000;
						float _1332 = 2.000000;
						float3 _1333 = _1136.xyz;
						float3 _1334 = _1333 * _1332;
						float3 _1335 = _1334 - _1331;
						float3 _1330 = normalize(_1335);
						float3 _1308;
						_1308 = _1330;
						float _1337 = _1308.z;
						float3 _1338 = _1130 * _1337;
						float _1339 = _1308.y;
						float3 _1340 = _1141 * _1339;
						float _1341 = _1308.x;
						float3 _1342 = _1140 * _1341;
						float3 _1343 = _1342 - _1340;
						float3 _1344 = _1343 + _1338;
						float3 _1336 = normalize(_1344);
						_1130 = _1336;
						float _1345 = 0.000100;
						float _1346 = dot(_1130, _1131);
						bool _1347 = _1346 < _1345;
						if (_1347)
						{
							float _1351 = dot(_1130, _1131);
							float _1352 = 0.000100;
							float _1353 = _1352 - _1351;
							float3 _1354 = _1131 * _1353;
							float3 _1355 = _1130 + _1354;
							float3 _1350 = normalize(_1355);
							_1130 = _1350;
						}
					}
					float3 _1356 = _1066.direction;
					float3 _1357 = -_1356;
					float3 _1142;
					_1142 = _1357;
					float _1358 = dot(_1130, _1142);
					float _1143;
					_1143 = _1358;
					float _1359 = 0.001000;
					bool _1360 = _1143 < _1359;
					if (_1360)
					{
						int _1364 = 0;
						bool _1365 = _1097 > _1364;
						if (_1365)
						{
							break;
						}
						float _1369 = 0.001000;
						float _1370 = _1369 - _1143;
						float3 _1371 = _1142 * _1370;
						float3 _1372 = _1130 + _1371;
						float3 _1368 = normalize(_1372);
						_1130 = _1368;
						float _1373 = dot(_1130, _1131);
						float _1361;
						_1361 = _1373;
						float _1374 = 0.001000;
						bool _1375 = _1361 < _1374;
						if (_1375)
						{
							float _1379 = 0.001000;
							float _1380 = _1379 - _1361;
							float3 _1381 = _1131 * _1380;
							float3 _1382 = _1130 + _1381;
							float3 _1378 = normalize(_1382);
							_1130 = _1378;
						}
						float _1384 = dot(_1130, _1142);
						float _1385 = 0.001000;
						float _1383 = max(_1384, _1385);
						_1143 = _1383;
					}
					tangent_basis _1386 = create_basis(_1130);
					tangent_basis _1144;
					_1144 = _1386;
					float3 _1387 = _1144.tangent;
					_1140 = _1387;
					float3 _1388 = _1144.binormal;
					_1141 = _1388;
					float _1390 = _1138.z;
					float3 _1389 = surface_albedo(_1137, _1390);
					float3 _1145;
					_1145 = _1389;
					float _1392 = _1138.z;
					float3 _1391 = surface_specular(_1137, _1392);
					float3 _1146;
					_1146 = _1391;
					float _1393 = _1138.y;
					float _1147;
					_1147 = _1393;
					float3 _1394 = spec_directional_albedo(_1146, _1143, _1147);
					float3 _1148;
					_1148 = _1394;
					float _1395 = 1.000000;
					float3 _1396 = _1395 - _1148;
					float3 _1397 = _1145 * _1396;
					float3 _1149;
					_1149 = _1397;
					float _1398 = luma(_1148);
					float _1150;
					_1150 = _1398;
					float _1399 = luma(_1149);
					float _1151;
					_1151 = _1399;
					float _1402 = _1150 + _1151;
					float _1403 = 0.000010;
					float _1401 = max(_1402, _1403);
					float _1404 = _1150 / _1401;
					float _1405 = 0.050000;
					float _1406 = 0.995000;
					float _1400 = clamp(_1404, _1405, _1406);
					float _1152;
					_1152 = _1400;
					float _1408 = _1147 * _1147;
					float _1409 = 0.001000;
					float _1407 = max(_1408, _1409);
					float _1153;
					_1153 = _1407;
					float _1410 = 0.000000;
					float _1411 = _1.params.w;
					bool _1412 = _1411 != _1410;
					if (_1412)
					{
						uint2 _1420 = _1029.xy;
						int _1421 = _1105 + _18;
						float _1419 = rnd_fine(_1420, _1064, _1421, _1032, _1031, _8, _10);
						uint2 _1423 = _1029.xy;
						int _1424 = 1;
						int _1425 = _1105 + _18;
						int _1426 = _1425 + _1424;
						float _1422 = rnd_fine(_1423, _1064, _1426, _1032, _1031, _8, _10);
						float4 _1418 = sample_env(_1419, _1422, _1, _11);
						float4 _1413;
						_1413 = _1418;
						float3 _1427 = _1413.xyz;
						float3 _1414;
						_1414 = _1427;
						float _1428 = _1413.w;
						float _1415;
						_1415 = _1428;
						float _1429 = 0.000000;
						float _1430 = dot(_1414, _1131);
						bool _1431 = _1430 > _1429;
						float _1432 = 0.000000;
						bool _1433 = _1415 > _1432;
						bool _1434 = _1433 && _1431;
						if (_1434)
						{
							float4 _1439 = bsdf_eval(_1414, _1130, _1142, _1143, _1149, _1146, _1153, _1152);
							float4 _1435;
							_1435 = _1439;
							float3 _1440 = _1435.xyz;
							float3 _1436;
							_1436 = _1440;
							float _1441 = 0.000000;
							float _1444 = _1436.x;
							float _1445 = _1436.y;
							float _1443 = max(_1444, _1445);
							float _1446 = _1436.z;
							float _1442 = max(_1443, _1446);
							bool _1447 = _1442 > _1441;
							if (_1447)
							{
								float3 _1451 = offset_ray(_1120, _1131, _1414);
								bool _1450 = occluded(_1451, _1414, _2);
								bool _1452 = !_1450;
								if (_1452)
								{
									float _1457 = _1435.w;
									float _1456 = mis_weight(_1415, _1457);
									float _1458 = _1456 / _1415;
									float3 _1459 = env_radiance(_1414, _1, _7, _12);
									float3 _1460 = _1067 * _1436;
									float3 _1461 = _1460 * _1459;
									float3 _1462 = _1461 * _1458;
									float _1463 = 0.000000;
									float3 _1464 = float3(_1463, _1463, _1463);
									float _1465 = 8.000000;
									float3 _1466 = float3(_1465, _1465, _1465);
									float3 _1455 = clamp(_1462, _1464, _1466);
									_1033 += _1455;
								}
							}
						}
					}
					uint2 _1468 = _1029.xy;
					int _1469 = _1105 + _17;
					float _1467 = rnd(_1468, _1064, _1469, _1032, _1031, _8, _10);
					float _1154;
					_1154 = _1467;
					uint2 _1471 = _1029.xy;
					int _1472 = 1;
					int _1473 = _1105 + _17;
					int _1474 = _1473 + _1472;
					float _1470 = rnd(_1471, _1064, _1474, _1032, _1031, _8, _10);
					float _1155;
					_1155 = _1470;
					bool _1475 = _1139 < _1152;
					if (_1475)
					{
						float _1485 = _1153 * _1153;
						float _1476;
						_1476 = _1485;
						float _1487 = dot(_1142, _1140);
						float _1488 = dot(_1142, _1141);
						float3 _1486 = float3(_1487, _1488, _1143);
						float3 _1477;
						_1477 = _1486;
						float3 _1489 = sample_ggx_vndf(_1477, _1153, _1154, _1155);
						float3 _1478;
						_1478 = _1489;
						float3 _1491 = -_1477;
						float3 _1490 = reflect(_1491, _1478);
						float3 _1479;
						_1479 = _1490;
						float _1492 = 0.000000;
						float _1493 = _1479.z;
						bool _1494 = _1493 <= _1492;
						if (_1494)
						{
							break;
						}
						float _1497 = _1479.z;
						float3 _1498 = _1130 * _1497;
						float _1499 = _1479.y;
						float3 _1500 = _1141 * _1499;
						float _1501 = _1479.x;
						float3 _1502 = _1140 * _1501;
						float3 _1503 = _1502 + _1500;
						float3 _1504 = _1503 + _1498;
						_1066.direction = _1504;
						float _1506 = _1477.z;
						float _1505 = smith_lambda(_1506, _1476);
						float _1480;
						_1480 = _1505;
						float _1508 = _1479.z;
						float _1507 = smith_lambda(_1508, _1476);
						float _1481;
						_1481 = _1507;
						float _1509 = 1.000000;
						float _1510 = _1509 + _1480;
						float _1511 = _1510 + _1481;
						float _1512 = 1.000000;
						float _1513 = _1512 + _1480;
						float _1514 = _1513 / _1511;
						float _1482;
						_1482 = _1514;
						float _1515 = _1482 / _1152;
						float _1518 = dot(_1477, _1478);
						float _1519 = 0.000000;
						float _1517 = max(_1518, _1519);
						float3 _1516 = f_schlick(_1146, _1517);
						float3 _1520 = _1516 * _1515;
						_1067 *= _1520;
					}
					bool _1521 = !_1475;
					if (_1521)
					{
						float3 _1524 = cos_weighted_direction(_1140, _1141, _1130, _1154, _1155);
						_1066.direction = _1524;
						float _1525 = 1.000000;
						float _1526 = _1525 - _1152;
						float3 _1527 = _1149 / _1526;
						_1067 *= _1527;
					}
					float _1528 = 0.000000;
					float3 _1530 = _1066.direction;
					float _1529 = dot(_1530, _1131);
					bool _1531 = _1529 <= _1528;
					if (_1531)
					{
						break;
					}
					float _1534 = 0.000000;
					float _1537 = _1067.x;
					float _1538 = _1067.y;
					float _1536 = max(_1537, _1538);
					float _1539 = _1067.z;
					float _1535 = max(_1536, _1539);
					bool _1540 = _1535 <= _1534;
					if (_1540)
					{
						break;
					}
					float3 _1544 = _1066.direction;
					float3 _1543 = offset_ray(_1120, _1131, _1544);
					_1066.origin = _1543;
					float3 _1546 = _1066.direction;
					float4 _1545 = bsdf_eval(_1546, _1130, _1142, _1143, _1149, _1146, _1153, _1152);
					float4 _1156;
					_1156 = _1545;
					float _1547 = _1156.w;
					_1068 = _1547;
					int _1548 = 1;
					_1097 += _1548;
				}
				}
			}
			int _1549 = 1;
			_1056 += _1549;
		}
		}
	}
	uint2 _1551 = _1029.xy;
	float4 _1550 = _3.read(_1551);
	float4 _1034;
	_1034 = _1550;
	float3 _1552 = _1034.xyz;
	float3 _1035;
	_1035 = _1552;
	float _1554 = float(_13);
	float _1553 = float(_1554);
	float3 _1555 = _1033 / _1553;
	_1033 = _1555;
	float _1556 = 1.000000;
	float _1557 = _1.eye.w;
	float _1558 = _1557 + _1556;
	float _1559 = 1.000000;
	float _1560 = _1559 / _1558;
	float _1036;
	_1036 = _1560;
	float3 _1562 = float3(_1036, _1036, _1036);
	float3 _1561 = mix(_1035, _1033, _1562);
	_1035 = _1561;
	float _1564 = 1.000000;
	float4 _1563 = float4(_1035, _1564);
	uint2 _1565 = _1029.xy;
	_3.write(_1563, _1565);
}
