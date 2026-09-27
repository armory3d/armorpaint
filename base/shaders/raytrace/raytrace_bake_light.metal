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

constant float _12 = 3.14159274;

constant float _13 = 6.28318548;

constant int _11 = 4;

float3 cos_weighted_hemisphere_direction(uint3 _185, float3 _186, int _187, int _188, int _189, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10);
float rand(int _105, int _106, int _107, int _108, int _109, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10);
uint2 table_texel(int _62);
uint table_channel(float4 _75, int _76);
float2 s16_to_f32(uint _14);
float2 hit_attribute2d(float2 _32, float2 _33, float2 _34, float2 _35);
float2 equirect(float3 _44, float _45);

float2 s16_to_f32(uint _14) {
	int _17 = 16;
	int _19 = 16;
	uint _20 = _14 << _19;
	int _18 = int(_20);
	int _21 = _18 >> _17;
	int _15;
	_15 = _21;
	int _22 = 16;
	int _23 = int(_14);
	int _24 = _23 >> _22;
	int _16;
	_16 = _24;
	float _25 = 32767.000000;
	float _28 = float(_15);
	float _27 = float(_28);
	float _30 = float(_16);
	float _29 = float(_30);
	float2 _26 = float2(_27, _29);
	float2 _31 = _26 / _25;
	return _31;
}

float2 hit_attribute2d(float2 _32, float2 _33, float2 _34, float2 _35) {
	float2 _36 = _34 - _32;
	float _37 = _35.y;
	float2 _38 = _37 * _36;
	float2 _39 = _33 - _32;
	float _40 = _35.x;
	float2 _41 = _40 * _39;
	float2 _42 = _32 + _41;
	float2 _43 = _42 + _38;
	return _43;
}

float2 equirect(float3 _44, float _45) {
	float _50 = _44.z;
	float _51 = -1.000000;
	float _52 = 1.000000;
	float _49 = clamp(_50, _51, _52);
	float _48 = acos(_49);
	float _46;
	_46 = _48;
	float _54 = _44.y;
	float _55 = -_54;
	float _56 = _44.x;
	float _53 = atan2(_55, _56);
	float _57 = _53 + _12;
	float _58 = _57 + _45;
	float _47;
	_47 = _58;
	float _60 = _47 / _13;
	float _61 = _46 / _12;
	float2 _59 = float2(_60, _61);
	return _59;
}

uint2 table_texel(int _62) {
	int _64 = 2;
	int _65 = 131071;
	int _66 = _62 & _65;
	int _67 = _66 >> _64;
	int _63;
	_63 = _67;
	int _70 = 127;
	int _71 = _63 & _70;
	uint _69 = uint(_71);
	int _73 = 7;
	int _74 = _63 >> _73;
	uint _72 = uint(_74);
	uint2 _68 = uint2(_69, _72);
	return _68;
}

uint table_channel(float4 _75, int _76) {
	int _79 = 3;
	int _80 = _76 & _79;
	int _77;
	_77 = _80;
	float _81 = _75.w;
	float _78;
	_78 = _81;
	int _82 = 0;
	bool _83 = _77 == _82;
	if (_83)
	{
		float _86 = _75.x;
		_78 = _86;
	}
	bool _87 = !_83;
	int _88 = 1;
	bool _89 = _77 == _88;
	bool _90 = _87 && _89;
	if (_90)
	{
		float _93 = _75.y;
		_78 = _93;
	}
	bool _94 = !_89;
	bool _95 = _87 && _94;
	int _96 = 2;
	bool _97 = _77 == _96;
	bool _98 = _95 && _97;
	if (_98)
	{
		float _101 = _75.z;
		_78 = _101;
	}
	float _103 = 255.000000;
	float _104 = _78 * _103;
	uint _102 = uint(_104);
	return _102;
}

float rand(int _105, int _106, int _107, int _108, int _109, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10) {
	int _119 = 8;
	int _120 = 128;
	int _121 = 127;
	int _122 = 11;
	int _123 = _109 * _122;
	int _124 = _106 + _123;
	int _125 = _124 & _121;
	int _126 = _125 * _120;
	int _127 = 127;
	int _128 = 9;
	int _129 = _109 * _128;
	int _130 = _105 + _129;
	int _131 = _130 & _127;
	int _132 = _131 + _126;
	int _133 = _132 * _119;
	int _134 = 8;
	int _135 = 255;
	int _136 = _108 & _135;
	int _137 = _136 % _134;
	int _138 = _137 + _133;
	int _110;
	_110 = _138;
	uint2 _140 = table_texel(_110);
	float4 _139 = _9.read(_140);
	float4 _111;
	_111 = _139;
	uint _141 = table_channel(_111, _110);
	uint _112;
	_112 = _141;
	int _142 = 8;
	int _143 = 128;
	int _144 = 127;
	int _145 = 11;
	int _146 = _109 * _145;
	int _147 = _106 + _146;
	int _148 = _147 & _144;
	int _149 = _148 * _143;
	int _150 = 127;
	int _151 = 9;
	int _152 = _109 * _151;
	int _153 = _105 + _152;
	int _154 = _153 & _150;
	int _155 = _154 + _149;
	int _156 = _155 * _142;
	int _157 = 255;
	int _158 = _108 & _157;
	int _159 = _158 + _156;
	int _113;
	_113 = _159;
	uint2 _161 = table_texel(_113);
	float4 _160 = _10.read(_161);
	float4 _114;
	_114 = _160;
	uint _162 = table_channel(_114, _113);
	uint _115;
	_115 = _162;
	int _163 = 255;
	int _164 = _107 & _163;
	_107 = _164;
	int _165 = 255;
	int _166 = _108 & _165;
	_108 = _166;
	int _167 = int(_115);
	int _168 = _107 ^ _167;
	int _116;
	_116 = _168;
	uint _171 = uint(_116);
	uint _172 = uint(_108);
	uint2 _170 = uint2(_171, _172);
	float4 _169 = _8.read(_170);
	float4 _117;
	_117 = _169;
	float _174 = 255.000000;
	float _175 = _117.x;
	float _176 = _175 * _174;
	int _173 = int(_176);
	int _118;
	_118 = _173;
	int _177 = int(_112);
	int _178 = _118 ^ _177;
	_118 = _178;
	float _179 = 256.000000;
	float _181 = float(_118);
	float _180 = float(_181);
	float _182 = 0.500000;
	float _183 = _182 + _180;
	float _184 = _183 / _179;
	return _184;
}

float3 cos_weighted_hemisphere_direction(uint3 _185, float3 _186, int _187, int _188, int _189, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10) {
	uint _199 = _185.x;
	int _198 = int(_199);
	uint _201 = _185.y;
	int _200 = int(_201);
	float _197 = rand(_198, _200, _187, _188, _189, _8, _9, _10);
	float _190;
	_190 = _197;
	uint _204 = _185.x;
	int _203 = int(_204);
	uint _206 = _185.y;
	int _205 = int(_206);
	int _207 = 1;
	int _208 = _188 + _207;
	float _202 = rand(_203, _205, _187, _208, _189, _8, _9, _10);
	float _191;
	_191 = _202;
	float _209 = 1.000000;
	float _210 = 2.000000;
	float _211 = _190 * _210;
	float _212 = _211 - _209;
	float _192;
	_192 = _212;
	float _213 = _191 * _13;
	float _193;
	_193 = _213;
	float _215 = _192 * _192;
	float _216 = 1.000000;
	float _217 = _216 - _215;
	float _214 = sqrt(_217);
	float _194;
	_194 = _214;
	float _218 = cos(_193);
	float _219 = _194 * _218;
	float _195;
	_195 = _219;
	float _220 = sin(_193);
	float _221 = _194 * _220;
	float _196;
	_196 = _221;
	float3 _223 = float3(_195, _196, _192);
	float3 _224 = _186 + _223;
	float3 _222 = normalize(_224);
	return _222;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _4 [[texture(1)]], texture2d<float> _5 [[texture(2)]], texture2d<float> _6 [[texture(3)]], texture2d<float> _7 [[texture(4)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]], constant _kong_instance *_kong_instances [[buffer(2)]], constant _kong_geometry_textures *_kong_geometry_textures [[buffer(3)]]) {
	uint3 _237 = _kong_dispatch_thread_id;
	uint3 _225;
	_225 = _237;
	uint2 _238 = uint2(_3.get_width(), _3.get_height());
	uint2 _226;
	_226 = _238;
	uint _239 = _226.y;
	uint _240 = _225.y;
	bool _241 = _240 >= _239;
	uint _242 = _226.x;
	uint _243 = _225.x;
	bool _244 = _243 >= _242;
	bool _245 = _244 || _241;
	if (_245)
	{
		return;
	}
	uint2 _249 = _225.xy;
	float4 _248 = _4.read(_249);
	float4 _227;
	_227 = _248;
	float _250 = 0.000000;
	float _251 = _227.w;
	bool _252 = _251 == _250;
	if (_252)
	{
		float _256 = 0.000000;
		float _257 = 0.000000;
		float _258 = 0.000000;
		float _259 = 0.000000;
		float4 _255 = float4(_256, _257, _258, _259);
		uint2 _260 = _225.xy;
		_3.write(_255, _260);
		return;
	}
	float3 _261 = _227.xyz;
	float3 _228;
	_228 = _261;
	uint2 _263 = _225.xy;
	float4 _262 = _5.read(_263);
	float4 _229;
	_229 = _262;
	float3 _264 = _229.xyz;
	float3 _230;
	_230 = _264;
	ray _231;
	float _265 = 0.010000;
	float _266 = _1.v0.w;
	float _267 = _266 * _265;
	_231.min_distance = _267;
	float _268 = 10.000000;
	float _269 = _1.v0.z;
	float _270 = _269 * _268;
	_231.max_distance = _270;
	_231.origin = _228;
	float _272 = 0.000000;
	float _273 = 0.000000;
	float _274 = 0.000000;
	float3 _271 = float3(_272, _273, _274);
	float3 _232;
	_232 = _271;
	int _275 = 0;
	int _233;
	_233 = _275;
	{
		int _279 = 0;
		int _276;
		_276 = _279;
		while (true)
{
		bool _283 = _276 < _11;
		if (!_283) { break; }
		{
			float _289 = _1.v0.x;
			int _288 = int(_289);
			float3 _287 = cos_weighted_hemisphere_direction(_225, _230, _276, _233, _288, _8, _9, _10);
			_231.direction = _287;
			int _290 = 1;
			_233 += _290;
			_kong_intersector::result_type _284;
			{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _284 = i.intersect(_231, _2); }
			bool _292 = _284.type == intersection_type::triangle;
			if (_292)
			{
				int _303 = 0;
				uint4 _302 = _kong_vertex(_kong_instances[_284.user_instance_id], _284.primitive_id * 3 + _303);
				uint4 _293;
				_293 = _302;
				int _305 = 1;
				uint4 _304 = _kong_vertex(_kong_instances[_284.user_instance_id], _284.primitive_id * 3 + _305);
				uint4 _294;
				_294 = _304;
				int _307 = 2;
				uint4 _306 = _kong_vertex(_kong_instances[_284.user_instance_id], _284.primitive_id * 3 + _307);
				uint4 _295;
				_295 = _306;
				uint _310 = _293.w;
				float2 _309 = s16_to_f32(_310);
				uint _312 = _294.w;
				float2 _311 = s16_to_f32(_312);
				uint _314 = _295.w;
				float2 _313 = s16_to_f32(_314);
				float2 _315 = _284.triangle_barycentric_coord;
				float2 _308 = hit_attribute2d(_309, _311, _313, _315);
				float2 _296;
				_296 = _308;
				uint _316 = _kong_instances[_284.user_instance_id].geometry;
				uint _297;
				_297 = _316;
				uint2 _317 = uint2(_kong_geometry_textures[_297].texpaint2.get_width(), _kong_geometry_textures[_297].texpaint2.get_height());
				uint2 _298;
				_298 = _317;
				float2 _320 = float2(_298);
				float2 _321 = _296 * _320;
				uint2 _319 = uint2(_321);
				float4 _318 = _kong_geometry_textures[_297].texpaint2.read(_319);
				float4 _299;
				_299 = _318;
				float3 _323 = _299.xyz;
				float _324 = 2.200000;
				float3 _325 = float3(_324, _324, _324);
				float3 _322 = pow(_323, _325);
				_232 += _322;
			}
			bool _326 = !_292;
			if (_326)
			{
				float3 _333 = _231.direction;
				float _334 = _1.v1.z;
				float2 _332 = equirect(_333, _334);
				float2 _327;
				_327 = _332;
				uint2 _335 = uint2(_7.get_width(), _7.get_height());
				uint2 _328;
				_328 = _335;
				float2 _338 = float2(_328);
				float2 _339 = _327 * _338;
				uint2 _337 = uint2(_339);
				float4 _336 = _7.read(_337);
				float4 _329;
				_329 = _336;
				float _340 = _1.v1.x;
				float3 _341 = _329.xyz;
				float3 _342 = _341 * _340;
				_232 += _342;
			}
			int _343 = 1;
			_276 += _343;
		}
		}
	}
	float _345 = float(_11);
	float _344 = float(_345);
	float3 _346 = _232 / _344;
	_232 = _346;
	uint2 _348 = _225.xy;
	float4 _347 = _6.read(_348);
	float4 _234;
	_234 = _347;
	float3 _349 = _234.xyz;
	_232 *= _349;
	uint2 _351 = _225.xy;
	float4 _350 = _3.read(_351);
	float4 _235;
	_235 = _350;
	float3 _352 = _235.xyz;
	float3 _236;
	_236 = _352;
	float _353 = 0.000000;
	float _354 = _1.v0.x;
	bool _355 = _354 == _353;
	if (_355)
	{
		_236 = _232;
	}
	bool _358 = !_355;
	if (_358)
	{
		float _362 = _1.v0.x;
		float _363 = 1.000000;
		float _364 = _363 / _362;
		float _359;
		_359 = _364;
		float3 _366 = float3(_359, _359, _359);
		float3 _365 = mix(_236, _232, _366);
		_236 = _365;
	}
	float _368 = 1.000000;
	float4 _367 = float4(_236, _368);
	uint2 _369 = _225.xy;
	_3.write(_367, _369);
}
