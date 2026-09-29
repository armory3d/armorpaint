#include <metal_stdlib>

using namespace metal;
using namespace raytracing;

typedef intersector<triangle_data, instancing, world_space_data> _kong_intersector;

struct _1_type {
	float4 v0;
	float4 v1;
	float4 v2;
	float4 v3;
	float4 v4;
};

struct tangent_basis {
	float3 tangent;
	float3 binormal;
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

constant float _18 = 6.28318548;

constant float _17 = 3.14159274;

constant int _22 = 32768;

constant int _20 = 256;

constant int _21 = 128;

constant float _19 = 19.739208;

constant int _13 = 2;

constant int _14 = 5;

constant int _15 = 4;

constant float _16 = 0.000500;

float rand(int _108, int _109, int _110, int _111, int _112, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10);
uint2 table_texel(int _65);
uint table_channel(float4 _78, int _79);
float4 sample_env(float _309, float _310, constant _1_type &_1, texture2d<float> _11);
int clamp_int(int _281, int _282, int _283);
float3 env_dir(float2 _290, constant _1_type &_1);
bool occluded(float3 _455, float3 _456, instance_acceleration_structure _2);
float mis_weight(float _445, float _446);
float3 env_radiance(float3 _271, constant _1_type &_1, texture2d<float> _7, sampler _12);
float2 equirect(float3 _253, float _254);
float3 cos_weighted_direction(float3 _228, float _229, float _230);
tangent_basis create_basis(float3 _188);
float env_pdf(float3 _403, constant _1_type &_1, texture2d<float> _11);
float2 s16_to_f32(uint _23);
float2 hit_attribute2d(float2 _53, float2 _54, float2 _55, float2 _56);
float3 hit_attribute(float3 _41, float3 _42, float3 _43, float2 _44);

float2 s16_to_f32(uint _23) {
	int _26 = 16;
	int _28 = 16;
	uint _29 = _23 << _28;
	int _27 = int(_29);
	int _30 = _27 >> _26;
	int _24;
	_24 = _30;
	int _31 = 16;
	int _32 = int(_23);
	int _33 = _32 >> _31;
	int _25;
	_25 = _33;
	float _34 = 32767.000000;
	float _37 = float(_24);
	float _36 = float(_37);
	float _39 = float(_25);
	float _38 = float(_39);
	float2 _35 = float2(_36, _38);
	float2 _40 = _35 / _34;
	return _40;
}

float3 hit_attribute(float3 _41, float3 _42, float3 _43, float2 _44) {
	float3 _45 = _43 - _41;
	float _46 = _44.y;
	float3 _47 = _46 * _45;
	float3 _48 = _42 - _41;
	float _49 = _44.x;
	float3 _50 = _49 * _48;
	float3 _51 = _41 + _50;
	float3 _52 = _51 + _47;
	return _52;
}

float2 hit_attribute2d(float2 _53, float2 _54, float2 _55, float2 _56) {
	float2 _57 = _55 - _53;
	float _58 = _56.y;
	float2 _59 = _58 * _57;
	float2 _60 = _54 - _53;
	float _61 = _56.x;
	float2 _62 = _61 * _60;
	float2 _63 = _53 + _62;
	float2 _64 = _63 + _59;
	return _64;
}

uint2 table_texel(int _65) {
	int _67 = 2;
	int _68 = 131071;
	int _69 = _65 & _68;
	int _70 = _69 >> _67;
	int _66;
	_66 = _70;
	int _73 = 127;
	int _74 = _66 & _73;
	uint _72 = uint(_74);
	int _76 = 7;
	int _77 = _66 >> _76;
	uint _75 = uint(_77);
	uint2 _71 = uint2(_72, _75);
	return _71;
}

uint table_channel(float4 _78, int _79) {
	int _82 = 3;
	int _83 = _79 & _82;
	int _80;
	_80 = _83;
	float _84 = _78.w;
	float _81;
	_81 = _84;
	int _85 = 0;
	bool _86 = _80 == _85;
	if (_86)
	{
		float _89 = _78.x;
		_81 = _89;
	}
	bool _90 = !_86;
	int _91 = 1;
	bool _92 = _80 == _91;
	bool _93 = _90 && _92;
	if (_93)
	{
		float _96 = _78.y;
		_81 = _96;
	}
	bool _97 = !_92;
	bool _98 = _90 && _97;
	int _99 = 2;
	bool _100 = _80 == _99;
	bool _101 = _98 && _100;
	if (_101)
	{
		float _104 = _78.z;
		_81 = _104;
	}
	float _106 = 255.000000;
	float _107 = _81 * _106;
	uint _105 = uint(_107);
	return _105;
}

float rand(int _108, int _109, int _110, int _111, int _112, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10) {
	int _122 = 8;
	int _123 = 128;
	int _124 = 127;
	int _125 = 11;
	int _126 = _112 * _125;
	int _127 = _109 + _126;
	int _128 = _127 & _124;
	int _129 = _128 * _123;
	int _130 = 127;
	int _131 = 9;
	int _132 = _112 * _131;
	int _133 = _108 + _132;
	int _134 = _133 & _130;
	int _135 = _134 + _129;
	int _136 = _135 * _122;
	int _137 = 8;
	int _138 = 255;
	int _139 = _111 & _138;
	int _140 = _139 % _137;
	int _141 = _140 + _136;
	int _113;
	_113 = _141;
	uint2 _143 = table_texel(_113);
	float4 _142 = _9.read(_143);
	float4 _114;
	_114 = _142;
	uint _144 = table_channel(_114, _113);
	uint _115;
	_115 = _144;
	int _145 = 8;
	int _146 = 128;
	int _147 = 127;
	int _148 = 11;
	int _149 = _112 * _148;
	int _150 = _109 + _149;
	int _151 = _150 & _147;
	int _152 = _151 * _146;
	int _153 = 127;
	int _154 = 9;
	int _155 = _112 * _154;
	int _156 = _108 + _155;
	int _157 = _156 & _153;
	int _158 = _157 + _152;
	int _159 = _158 * _145;
	int _160 = 255;
	int _161 = _111 & _160;
	int _162 = _161 + _159;
	int _116;
	_116 = _162;
	uint2 _164 = table_texel(_116);
	float4 _163 = _10.read(_164);
	float4 _117;
	_117 = _163;
	uint _165 = table_channel(_117, _116);
	uint _118;
	_118 = _165;
	int _166 = 255;
	int _167 = _110 & _166;
	_110 = _167;
	int _168 = 255;
	int _169 = _111 & _168;
	_111 = _169;
	int _170 = int(_118);
	int _171 = _110 ^ _170;
	int _119;
	_119 = _171;
	uint _174 = uint(_119);
	uint _175 = uint(_111);
	uint2 _173 = uint2(_174, _175);
	float4 _172 = _8.read(_173);
	float4 _120;
	_120 = _172;
	float _177 = 255.000000;
	float _178 = _120.x;
	float _179 = _178 * _177;
	int _176 = int(_179);
	int _121;
	_121 = _176;
	int _180 = int(_115);
	int _181 = _121 ^ _180;
	_121 = _181;
	float _182 = 256.000000;
	float _184 = float(_121);
	float _183 = float(_184);
	float _185 = 0.500000;
	float _186 = _185 + _183;
	float _187 = _186 / _182;
	return _187;
}

tangent_basis create_basis(float3 _188) {
	float _193 = 1.000000;
	float _189;
	_189 = _193;
	float _194 = 0.000000;
	float _195 = _188.z;
	bool _196 = _195 < _194;
	if (_196)
	{
		float _199 = -1.000000;
		_189 = _199;
	}
	float _200 = _188.z;
	float _201 = _189 + _200;
	float _202 = -1.000000;
	float _203 = _202 / _201;
	float _190;
	_190 = _203;
	float _204 = _188.y;
	float _205 = _188.x;
	float _206 = _205 * _204;
	float _207 = _206 * _190;
	float _191;
	_191 = _207;
	tangent_basis _192;
	float _209 = _188.x;
	float _210 = _188.x;
	float _211 = _189 * _210;
	float _212 = _211 * _209;
	float _213 = _212 * _190;
	float _214 = 1.000000;
	float _215 = _214 + _213;
	float _216 = _189 * _191;
	float _217 = _188.x;
	float _218 = -_189;
	float _219 = _218 * _217;
	float3 _208 = float3(_215, _216, _219);
	_192.tangent = _208;
	float _221 = _188.y;
	float _222 = _188.y;
	float _223 = _222 * _221;
	float _224 = _223 * _190;
	float _225 = _189 + _224;
	float _226 = _188.y;
	float _227 = -_226;
	float3 _220 = float3(_191, _225, _227);
	_192.binormal = _220;
	return _192;
}

float3 cos_weighted_direction(float3 _228, float _229, float _230) {
	tangent_basis _234 = create_basis(_228);
	tangent_basis _231;
	_231 = _234;
	float _235 = sqrt(_229);
	float _232;
	_232 = _235;
	float _236 = _18 * _230;
	float _233;
	_233 = _236;
	float _239 = 0.000000;
	float _240 = 1.000000;
	float _241 = _240 - _229;
	float _238 = max(_239, _241);
	float _237 = sqrt(_238);
	float3 _242 = _228 * _237;
	float _243 = sin(_233);
	float _244 = _232 * _243;
	float3 _245 = _231.binormal;
	float3 _246 = _245 * _244;
	float _247 = cos(_233);
	float _248 = _232 * _247;
	float3 _249 = _231.tangent;
	float3 _250 = _249 * _248;
	float3 _251 = _250 + _246;
	float3 _252 = _251 + _242;
	return _252;
}

float2 equirect(float3 _253, float _254) {
	float _259 = _253.z;
	float _260 = -1.000000;
	float _261 = 1.000000;
	float _258 = clamp(_259, _260, _261);
	float _257 = acos(_258);
	float _255;
	_255 = _257;
	float _263 = _253.y;
	float _264 = -_263;
	float _265 = _253.x;
	float _262 = atan2(_264, _265);
	float _266 = _262 + _17;
	float _267 = _266 + _254;
	float _256;
	_256 = _267;
	float _269 = _256 / _18;
	float _270 = _255 / _17;
	float2 _268 = float2(_269, _270);
	return _268;
}

float3 env_radiance(float3 _271, constant _1_type &_1, texture2d<float> _7, sampler _12) {
	float _275 = _1.v1.z;
	float2 _274 = equirect(_271, _275);
	float2 _272;
	_272 = _274;
	float _277 = 0.000000;
	float4 _276 = _7.sample(_12, _272, level(_277));
	float4 _273;
	_273 = _276;
	float _278 = _1.v1.x;
	float3 _279 = _273.xyz;
	float3 _280 = _279 * _278;
	return _280;
}

int clamp_int(int _281, int _282, int _283) {
	bool _284 = _281 < _282;
	if (_284)
	{
		return _282;
	}
	bool _287 = _281 > _283;
	if (_287)
	{
		return _283;
	}
	return _281;
}

float3 env_dir(float2 _290, constant _1_type &_1) {
	float _294 = _290.y;
	float _295 = _294 * _17;
	float _291;
	_291 = _295;
	float _296 = _1.v1.z;
	float _297 = _290.x;
	float _298 = _297 * _18;
	float _299 = _298 - _17;
	float _300 = _299 - _296;
	float _292;
	_292 = _300;
	float _301 = sin(_291);
	float _293;
	_293 = _301;
	float _303 = cos(_292);
	float _304 = _293 * _303;
	float _305 = sin(_292);
	float _306 = -_293;
	float _307 = _306 * _305;
	float _308 = cos(_291);
	float3 _302 = float3(_304, _307, _308);
	return _302;
}

float4 sample_env(float _309, float _310, constant _1_type &_1, texture2d<float> _11) {
	float _322 = float(_22);
	float _321 = float(_322);
	float _323 = _309 * _321;
	float _311;
	_311 = _323;
	int _325 = int(_311);
	int _326 = 0;
	int _327 = 1;
	int _328 = _22 - _327;
	int _324 = clamp_int(_325, _326, _328);
	int _312;
	_312 = _324;
	float _330 = float(_312);
	float _329 = float(_330);
	float _331 = _311 - _329;
	float _313;
	_313 = _331;
	int _335 = 256;
	int _336 = _312 % _335;
	uint _334 = uint(_336);
	int _338 = 256;
	int _339 = _312 / _338;
	uint _337 = uint(_339);
	uint2 _333 = uint2(_334, _337);
	float4 _332 = _11.read(_333);
	float4 _314;
	_314 = _332;
	int _315;
	float _340 = 0.000000;
	float _316;
	_316 = _340;
	float _317;
	float _341 = _314.x;
	bool _342 = _310 < _341;
	if (_342)
	{
		_315 = _312;
		float _345 = 0.000000;
		float _346 = _314.x;
		bool _347 = _346 > _345;
		if (_347)
		{
			float _350 = _314.x;
			float _351 = _310 / _350;
			_316 = _351;
		}
		float _352 = _314.z;
		_317 = _352;
	}
	bool _353 = !_342;
	if (_353)
	{
		float _358 = _314.y;
		int _357 = int(_358);
		int _359 = 0;
		int _360 = 1;
		int _361 = _22 - _360;
		int _356 = clamp_int(_357, _359, _361);
		_315 = _356;
		float _362 = 1.000000;
		float _363 = _314.x;
		bool _364 = _363 < _362;
		if (_364)
		{
			float _367 = _314.x;
			float _368 = 1.000000;
			float _369 = _368 - _367;
			float _370 = _314.x;
			float _371 = _310 - _370;
			float _372 = _371 / _369;
			_316 = _372;
		}
		float _373 = _314.w;
		_317 = _373;
	}
	float _376 = float(_20);
	float _375 = float(_376);
	int _379 = 256;
	int _380 = _315 % _379;
	float _378 = float(_380);
	float _377 = float(_378);
	float _381 = _377 + _313;
	float _382 = _381 / _375;
	float _384 = float(_21);
	float _383 = float(_384);
	int _387 = 256;
	int _388 = _315 / _387;
	float _386 = float(_388);
	float _385 = float(_386);
	float _389 = _385 + _316;
	float _390 = _389 / _383;
	float2 _374 = float2(_382, _390);
	float2 _318;
	_318 = _374;
	float _392 = _318.y;
	float _393 = _392 * _17;
	float _391 = sin(_393);
	float _319;
	_319 = _391;
	float _394 = 0.000000;
	float _320;
	_320 = _394;
	float _395 = 0.000001;
	bool _396 = _319 > _395;
	if (_396)
	{
		float _399 = _19 * _319;
		float _400 = _317 / _399;
		_320 = _400;
	}
	float3 _402 = env_dir(_318, _1);
	float4 _401 = float4(_402, _320);
	return _401;
}

float env_pdf(float3 _403, constant _1_type &_1, texture2d<float> _11) {
	float _410 = _1.v1.z;
	float2 _409 = equirect(_403, _410);
	float2 _404;
	_404 = _409;
	float _414 = float(_20);
	float _413 = float(_414);
	float _416 = _404.x;
	float _415 = fract(_416);
	float _417 = _415 * _413;
	int _412 = int(_417);
	int _418 = 0;
	int _419 = 1;
	int _420 = _20 - _419;
	int _411 = clamp_int(_412, _418, _420);
	int _405;
	_405 = _411;
	float _424 = float(_21);
	float _423 = float(_424);
	float _425 = _404.y;
	float _426 = _425 * _423;
	int _422 = int(_426);
	int _427 = 0;
	int _428 = 1;
	int _429 = _21 - _428;
	int _421 = clamp_int(_422, _427, _429);
	int _406;
	_406 = _421;
	uint _432 = uint(_405);
	uint _433 = uint(_406);
	uint2 _431 = uint2(_432, _433);
	float4 _430 = _11.read(_431);
	float4 _407;
	_407 = _430;
	float _435 = _404.y;
	float _436 = _435 * _17;
	float _434 = sin(_436);
	float _408;
	_408 = _434;
	float _437 = 0.000001;
	bool _438 = _408 > _437;
	if (_438)
	{
		float _441 = _19 * _408;
		float _442 = _407.z;
		float _443 = _442 / _441;
		return _443;
	}
	float _444 = 0.000000;
	return _444;
}

float mis_weight(float _445, float _446) {
	float _449 = _445 * _445;
	float _447;
	_447 = _449;
	float _450 = _446 * _446;
	float _448;
	_448 = _450;
	float _452 = _447 + _448;
	float _453 = 9.99999972e-10;
	float _451 = max(_452, _453);
	float _454 = _447 / _451;
	return _454;
}

bool occluded(float3 _455, float3 _456, instance_acceleration_structure _2) {
	ray _457;
	_457.origin = _455;
	_457.direction = _456;
	float _459 = 0.000100;
	_457.min_distance = _459;
	float _460 = 100.000000;
	_457.max_distance = _460;
	_kong_intersector::result_type _458;
	{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(true); _458 = i.intersect(_457, _2); }
	bool _462 = _458.type == intersection_type::triangle;
	return _462;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _4 [[texture(1)]], texture2d<float> _5 [[texture(2)]], texture2d<float> _6 [[texture(3)]], texture2d<float> _7 [[texture(4)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]], texture2d<float> _11 [[texture(8)]], sampler _12 [[sampler(0)]], constant _kong_instance *_kong_instances [[buffer(2)]], constant _kong_geometry_textures *_kong_geometry_textures [[buffer(3)]]) {
	uint3 _475 = _kong_dispatch_thread_id;
	uint3 _463;
	_463 = _475;
	uint2 _476 = uint2(_3.get_width(), _3.get_height());
	uint2 _464;
	_464 = _476;
	uint _477 = _464.y;
	uint _478 = _463.y;
	bool _479 = _478 >= _477;
	uint _480 = _464.x;
	uint _481 = _463.x;
	bool _482 = _481 >= _480;
	bool _483 = _482 || _479;
	if (_483)
	{
		return;
	}
	uint2 _487 = _463.xy;
	float4 _486 = _4.read(_487);
	float4 _465;
	_465 = _486;
	float _488 = 0.000000;
	float _489 = _465.w;
	bool _490 = _489 == _488;
	if (_490)
	{
		float _494 = 0.000000;
		float _495 = 0.000000;
		float _496 = 0.000000;
		float _497 = 0.000000;
		float4 _493 = float4(_494, _495, _496, _497);
		uint2 _498 = _463.xy;
		_3.write(_493, _498);
		return;
	}
	float3 _499 = _465.xyz;
	float3 _466;
	_466 = _499;
	uint2 _501 = _463.xy;
	float4 _500 = _5.read(_501);
	float4 _467;
	_467 = _500;
	float3 _503 = _467.xyz;
	float3 _502 = normalize(_503);
	float3 _468;
	_468 = _502;
	float _505 = _1.v0.x;
	int _504 = int(_505);
	int _469;
	_469 = _504;
	float _506 = 0.000000;
	float _507 = _1.v1.w;
	bool _508 = _507 != _506;
	bool _470;
	_470 = _508;
	uint _510 = _463.x;
	int _509 = int(_510);
	int _471;
	_471 = _509;
	uint _512 = _463.y;
	int _511 = int(_512);
	int _472;
	_472 = _511;
	float _514 = 0.000000;
	float _515 = 0.000000;
	float _516 = 0.000000;
	float3 _513 = float3(_514, _515, _516);
	float3 _473;
	_473 = _513;
	{
		int _520 = 0;
		int _517;
		_517 = _520;
		while (true)
{
		bool _524 = _517 < _13;
		if (!_524) { break; }
		{
			int _533 = _469 * _13;
			int _534 = _533 + _517;
			int _525;
			_525 = _534;
			float3 _526;
			_526 = _466;
			float3 _527;
			_527 = _468;
			float3 _528;
			_528 = _468;
			float _536 = 1.000000;
			float _537 = 1.000000;
			float _538 = 1.000000;
			float3 _535 = float3(_536, _537, _538);
			float3 _529;
			_529 = _535;
			float _540 = 0.000000;
			float _541 = 0.000000;
			float _542 = 0.000000;
			float3 _539 = float3(_540, _541, _542);
			float3 _530;
			_530 = _539;
			{
				int _546 = 0;
				int _543;
				_543 = _546;
				while (true)
{
				bool _550 = _543 < _14;
				if (!_550) { break; }
				{
					int _576 = _543 * _15;
					int _551;
					_551 = _576;
					if (_470)
					{
						float _584 = rand(_471, _472, _525, _551, _469, _8, _9, _10);
						int _586 = 1;
						int _587 = _551 + _586;
						float _585 = rand(_471, _472, _525, _587, _469, _8, _9, _10);
						float4 _583 = sample_env(_584, _585, _1, _11);
						float4 _577;
						_577 = _583;
						float3 _588 = _577.xyz;
						float3 _578;
						_578 = _588;
						float _589 = _577.w;
						float _579;
						_579 = _589;
						float _590 = dot(_527, _578);
						float _580;
						_580 = _590;
						float _591 = 0.000000;
						float _592 = dot(_528, _578);
						bool _593 = _592 > _591;
						float _594 = 0.000000;
						bool _595 = _580 > _594;
						float _596 = 0.000000;
						bool _597 = _579 > _596;
						bool _598 = _597 && _595;
						bool _599 = _598 && _593;
						if (_599)
						{
							float3 _603 = _528 * _16;
							float3 _604 = _526 + _603;
							bool _602 = occluded(_604, _578, _2);
							bool _605 = !_602;
							if (_605)
							{
								float _609 = _580 / _17;
								float _606;
								_606 = _609;
								float _610 = mis_weight(_579, _606);
								float _611 = _606 / _579;
								float3 _612 = env_radiance(_578, _1, _7, _12);
								float3 _613 = _529 * _612;
								float3 _614 = _613 * _611;
								float3 _615 = _614 * _610;
								_530 += _615;
							}
						}
					}
					int _618 = 2;
					int _619 = _551 + _618;
					float _617 = rand(_471, _472, _525, _619, _469, _8, _9, _10);
					int _621 = 3;
					int _622 = _551 + _621;
					float _620 = rand(_471, _472, _525, _622, _469, _8, _9, _10);
					float3 _616 = cos_weighted_direction(_527, _617, _620);
					float3 _552;
					_552 = _616;
					float _623 = 0.000000;
					float _624 = dot(_552, _528);
					bool _625 = _624 <= _623;
					if (_625)
					{
						break;
					}
					float _629 = dot(_527, _552);
					float _630 = 0.000000;
					float _628 = max(_629, _630);
					float _631 = _628 / _17;
					float _553;
					_553 = _631;
					ray _554;
					float3 _632 = _528 * _16;
					float3 _633 = _526 + _632;
					_554.origin = _633;
					_554.direction = _552;
					float _634 = 0.000100;
					_554.min_distance = _634;
					float _635 = 100.000000;
					_554.max_distance = _635;
					_kong_intersector::result_type _555;
					{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _555 = i.intersect(_554, _2); }
					bool _637 = _555.type == intersection_type::triangle;
					bool _638 = !_637;
					if (_638)
					{
						float _642 = 1.000000;
						float _639;
						_639 = _642;
						if (_470)
						{
							float _646 = env_pdf(_552, _1, _11);
							float _645 = mis_weight(_553, _646);
							_639 = _645;
						}
						float3 _647 = env_radiance(_552, _1, _7, _12);
						float3 _648 = _529 * _647;
						float3 _649 = _648 * _639;
						_530 += _649;
						break;
					}
					uint _650 = _kong_instances[_555.user_instance_id].geometry;
					uint _556;
					_556 = _650;
					float2 _651 = _555.triangle_barycentric_coord;
					float2 _557;
					_557 = _651;
					int _653 = 0;
					uint4 _652 = _kong_vertex(_kong_instances[_555.user_instance_id], _555.primitive_id * 3 + _653);
					uint4 _558;
					_558 = _652;
					int _655 = 1;
					uint4 _654 = _kong_vertex(_kong_instances[_555.user_instance_id], _555.primitive_id * 3 + _655);
					uint4 _559;
					_559 = _654;
					int _657 = 2;
					uint4 _656 = _kong_vertex(_kong_instances[_555.user_instance_id], _555.primitive_id * 3 + _657);
					uint4 _560;
					_560 = _656;
					uint _660 = _558.w;
					float2 _659 = s16_to_f32(_660);
					uint _662 = _559.w;
					float2 _661 = s16_to_f32(_662);
					uint _664 = _560.w;
					float2 _663 = s16_to_f32(_664);
					float2 _658 = hit_attribute2d(_659, _661, _663, _557);
					float2 _561;
					_561 = _658;
					uint2 _665 = uint2(_kong_geometry_textures[_556].texpaint2.get_width(), _kong_geometry_textures[_556].texpaint2.get_height());
					uint2 _562;
					_562 = _665;
					float2 _668 = float2(_562);
					float2 _669 = fract(_561);
					float2 _670 = _669 * _668;
					uint2 _667 = uint2(_670);
					float4 _666 = _kong_geometry_textures[_556].texpaint2.read(_667);
					float4 _563;
					_563 = _666;
					float3 _672 = _563.xyz;
					float _674 = 2.200000;
					float _675 = 2.200000;
					float _676 = 2.200000;
					float3 _673 = float3(_674, _675, _676);
					float3 _671 = pow(_672, _673);
					_529 *= _671;
					float _677 = 0.000000;
					float _680 = _529.x;
					float _681 = _529.y;
					float _679 = max(_680, _681);
					float _682 = _529.z;
					float _678 = max(_679, _682);
					bool _683 = _678 <= _677;
					if (_683)
					{
						break;
					}
					uint _687 = _558.y;
					float2 _686 = s16_to_f32(_687);
					float2 _564;
					_564 = _686;
					uint _689 = _559.y;
					float2 _688 = s16_to_f32(_689);
					float2 _565;
					_565 = _688;
					uint _691 = _560.y;
					float2 _690 = s16_to_f32(_691);
					float2 _566;
					_566 = _690;
					uint _694 = _558.x;
					float2 _693 = s16_to_f32(_694);
					float _695 = _564.x;
					float3 _692 = float3(_693, _695);
					float3 _567;
					_567 = _692;
					uint _698 = _559.x;
					float2 _697 = s16_to_f32(_698);
					float _699 = _565.x;
					float3 _696 = float3(_697, _699);
					float3 _568;
					_568 = _696;
					uint _702 = _560.x;
					float2 _701 = s16_to_f32(_702);
					float _703 = _566.x;
					float3 _700 = float3(_701, _703);
					float3 _569;
					_569 = _700;
					uint _706 = _558.z;
					float2 _705 = s16_to_f32(_706);
					float _707 = _564.y;
					float3 _704 = float3(_705, _707);
					float3 _570;
					_570 = _704;
					uint _710 = _559.z;
					float2 _709 = s16_to_f32(_710);
					float _711 = _565.y;
					float3 _708 = float3(_709, _711);
					float3 _571;
					_571 = _708;
					uint _714 = _560.z;
					float2 _713 = s16_to_f32(_714);
					float _715 = _566.y;
					float3 _712 = float3(_713, _715);
					float3 _572;
					_572 = _712;
					float3x3 _716 = float3x3(_555.object_to_world_transform[0], _555.object_to_world_transform[1], _555.object_to_world_transform[2]);
					float3x3 _573;
					_573 = _716;
					float3 _718 = hit_attribute(_570, _571, _572, _557);
					float3 _719 = _573 * _718;
					float3 _717 = normalize(_719);
					_527 = _717;
					float3 _721 = _568 - _567;
					float3 _722 = _569 - _567;
					float3 _720 = cross(_721, _722);
					_528 = _720;
					float _723 = 9.99999968e-21;
					float _724 = dot(_528, _528);
					bool _725 = _724 > _723;
					if (_725)
					{
						float3 _729 = _573 * _528;
						float3 _728 = normalize(_729);
						_528 = _728;
					}
					bool _730 = !_725;
					if (_730)
					{
						_528 = _527;
					}
					float _733 = 0.000000;
					float _734 = dot(_528, _552);
					bool _735 = _734 > _733;
					if (_735)
					{
						float3 _738 = -_528;
						_528 = _738;
					}
					float _739 = 0.000000;
					float _740 = dot(_527, _528);
					bool _741 = _740 < _739;
					if (_741)
					{
						float3 _744 = -_527;
						_527 = _744;
					}
					float _745 = _555.distance;
					float3 _746 = _552 * _745;
					float3 _747 = _554.origin;
					float3 _748 = _747 + _746;
					_526 = _748;
					int _749 = 1;
					_543 += _749;
				}
				}
			}
			float _752 = 32.000000;
			float _753 = 32.000000;
			float _754 = 32.000000;
			float3 _751 = float3(_752, _753, _754);
			float3 _750 = min(_530, _751);
			_473 += _750;
			int _755 = 1;
			_517 += _755;
		}
		}
	}
	float _757 = float(_13);
	float _756 = float(_757);
	float3 _758 = _473 / _756;
	_473 = _758;
	float _759 = 0.000000;
	float _760 = _1.v2.x;
	bool _761 = _760 == _759;
	if (_761)
	{
		uint2 _766 = _463.xy;
		float4 _765 = _6.read(_766);
		float4 _762;
		_762 = _765;
		float3 _767 = _762.xyz;
		_473 *= _767;
	}
	float3 _474;
	_474 = _473;
	int _768 = 0;
	bool _769 = _469 > _768;
	if (_769)
	{
		uint2 _774 = _463.xy;
		float4 _773 = _3.read(_774);
		float4 _770;
		_770 = _773;
		float3 _776 = _770.xyz;
		int _779 = 1;
		int _780 = _469 + _779;
		float _778 = float(_780);
		float _777 = float(_778);
		float _781 = 1.000000;
		float _782 = _781 / _777;
		float3 _783 = float3(_782, _782, _782);
		float3 _775 = mix(_776, _473, _783);
		_474 = _775;
	}
	float _785 = 1.000000;
	float4 _784 = float4(_474, _785);
	uint2 _786 = _463.xy;
	_3.write(_784, _786);
}
