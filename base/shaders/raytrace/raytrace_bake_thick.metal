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

constant float _12 = 6.28318548;

constant int _11 = 4;

float3 cos_weighted_hemisphere_direction(uint3 _136, float3 _137, int _138, int _139, int _140, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10);
float rand(int _56, int _57, int _58, int _59, int _60, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10);
uint2 table_texel(int _13);
uint table_channel(float4 _26, int _27);

uint2 table_texel(int _13) {
	int _15 = 2;
	int _16 = 131071;
	int _17 = _13 & _16;
	int _18 = _17 >> _15;
	int _14;
	_14 = _18;
	int _21 = 127;
	int _22 = _14 & _21;
	uint _20 = uint(_22);
	int _24 = 7;
	int _25 = _14 >> _24;
	uint _23 = uint(_25);
	uint2 _19 = uint2(_20, _23);
	return _19;
}

uint table_channel(float4 _26, int _27) {
	int _30 = 3;
	int _31 = _27 & _30;
	int _28;
	_28 = _31;
	float _32 = _26.w;
	float _29;
	_29 = _32;
	int _33 = 0;
	bool _34 = _28 == _33;
	if (_34)
	{
		float _37 = _26.x;
		_29 = _37;
	}
	bool _38 = !_34;
	int _39 = 1;
	bool _40 = _28 == _39;
	bool _41 = _38 && _40;
	if (_41)
	{
		float _44 = _26.y;
		_29 = _44;
	}
	bool _45 = !_40;
	bool _46 = _38 && _45;
	int _47 = 2;
	bool _48 = _28 == _47;
	bool _49 = _46 && _48;
	if (_49)
	{
		float _52 = _26.z;
		_29 = _52;
	}
	float _54 = 255.000000;
	float _55 = _29 * _54;
	uint _53 = uint(_55);
	return _53;
}

float rand(int _56, int _57, int _58, int _59, int _60, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10) {
	int _70 = 8;
	int _71 = 128;
	int _72 = 127;
	int _73 = 11;
	int _74 = _60 * _73;
	int _75 = _57 + _74;
	int _76 = _75 & _72;
	int _77 = _76 * _71;
	int _78 = 127;
	int _79 = 9;
	int _80 = _60 * _79;
	int _81 = _56 + _80;
	int _82 = _81 & _78;
	int _83 = _82 + _77;
	int _84 = _83 * _70;
	int _85 = 8;
	int _86 = 255;
	int _87 = _59 & _86;
	int _88 = _87 % _85;
	int _89 = _88 + _84;
	int _61;
	_61 = _89;
	uint2 _91 = table_texel(_61);
	float4 _90 = _9.read(_91);
	float4 _62;
	_62 = _90;
	uint _92 = table_channel(_62, _61);
	uint _63;
	_63 = _92;
	int _93 = 8;
	int _94 = 128;
	int _95 = 127;
	int _96 = 11;
	int _97 = _60 * _96;
	int _98 = _57 + _97;
	int _99 = _98 & _95;
	int _100 = _99 * _94;
	int _101 = 127;
	int _102 = 9;
	int _103 = _60 * _102;
	int _104 = _56 + _103;
	int _105 = _104 & _101;
	int _106 = _105 + _100;
	int _107 = _106 * _93;
	int _108 = 255;
	int _109 = _59 & _108;
	int _110 = _109 + _107;
	int _64;
	_64 = _110;
	uint2 _112 = table_texel(_64);
	float4 _111 = _10.read(_112);
	float4 _65;
	_65 = _111;
	uint _113 = table_channel(_65, _64);
	uint _66;
	_66 = _113;
	int _114 = 255;
	int _115 = _58 & _114;
	_58 = _115;
	int _116 = 255;
	int _117 = _59 & _116;
	_59 = _117;
	int _118 = int(_66);
	int _119 = _58 ^ _118;
	int _67;
	_67 = _119;
	uint _122 = uint(_67);
	uint _123 = uint(_59);
	uint2 _121 = uint2(_122, _123);
	float4 _120 = _8.read(_121);
	float4 _68;
	_68 = _120;
	float _125 = 255.000000;
	float _126 = _68.x;
	float _127 = _126 * _125;
	int _124 = int(_127);
	int _69;
	_69 = _124;
	int _128 = int(_63);
	int _129 = _69 ^ _128;
	_69 = _129;
	float _130 = 256.000000;
	float _132 = float(_69);
	float _131 = float(_132);
	float _133 = 0.500000;
	float _134 = _133 + _131;
	float _135 = _134 / _130;
	return _135;
}

float3 cos_weighted_hemisphere_direction(uint3 _136, float3 _137, int _138, int _139, int _140, texture2d<float> _8, texture2d<float> _9, texture2d<float> _10) {
	uint _150 = _136.x;
	int _149 = int(_150);
	uint _152 = _136.y;
	int _151 = int(_152);
	float _148 = rand(_149, _151, _138, _139, _140, _8, _9, _10);
	float _141;
	_141 = _148;
	uint _155 = _136.x;
	int _154 = int(_155);
	uint _157 = _136.y;
	int _156 = int(_157);
	int _158 = 1;
	int _159 = _139 + _158;
	float _153 = rand(_154, _156, _138, _159, _140, _8, _9, _10);
	float _142;
	_142 = _153;
	float _160 = 1.000000;
	float _161 = 2.000000;
	float _162 = _141 * _161;
	float _163 = _162 - _160;
	float _143;
	_143 = _163;
	float _164 = _142 * _12;
	float _144;
	_144 = _164;
	float _166 = _143 * _143;
	float _167 = 1.000000;
	float _168 = _167 - _166;
	float _165 = sqrt(_168);
	float _145;
	_145 = _165;
	float _169 = cos(_144);
	float _170 = _145 * _169;
	float _146;
	_146 = _170;
	float _171 = sin(_144);
	float _172 = _145 * _171;
	float _147;
	_147 = _172;
	float3 _174 = float3(_146, _147, _143);
	float3 _175 = _137 + _174;
	float3 _173 = normalize(_175);
	return _173;
}

kernel void raytrace(uint3 _kong_dispatch_thread_id [[thread_position_in_grid]], constant _1_type &_1 [[buffer(0)]], instance_acceleration_structure _2 [[buffer(1)]], texture2d<float, access::read_write> _3 [[texture(0)]], texture2d<float> _4 [[texture(1)]], texture2d<float> _5 [[texture(2)]], texture2d<float> _8 [[texture(5)]], texture2d<float> _9 [[texture(6)]], texture2d<float> _10 [[texture(7)]]) {
	uint3 _187 = _kong_dispatch_thread_id;
	uint3 _176;
	_176 = _187;
	uint2 _188 = uint2(_3.get_width(), _3.get_height());
	uint2 _177;
	_177 = _188;
	uint _189 = _177.y;
	uint _190 = _176.y;
	bool _191 = _190 >= _189;
	uint _192 = _177.x;
	uint _193 = _176.x;
	bool _194 = _193 >= _192;
	bool _195 = _194 || _191;
	if (_195)
	{
		return;
	}
	uint2 _199 = _176.xy;
	float4 _198 = _4.read(_199);
	float4 _178;
	_178 = _198;
	float _200 = 0.000000;
	float _201 = _178.w;
	bool _202 = _201 == _200;
	if (_202)
	{
		float _206 = 0.000000;
		float _207 = 0.000000;
		float _208 = 0.000000;
		float _209 = 0.000000;
		float4 _205 = float4(_206, _207, _208, _209);
		uint2 _210 = _176.xy;
		_3.write(_205, _210);
		return;
	}
	float3 _211 = _178.xyz;
	float3 _179;
	_179 = _211;
	uint2 _213 = _176.xy;
	float4 _212 = _5.read(_213);
	float4 _180;
	_180 = _212;
	float3 _214 = _180.xyz;
	float3 _181;
	_181 = _214;
	ray _182;
	float _215 = 0.010000;
	float _216 = _1.v0.w;
	float _217 = _216 * _215;
	_182.min_distance = _217;
	float _218 = 10.000000;
	float _219 = _1.v0.z;
	float _220 = _219 * _218;
	_182.max_distance = _220;
	_182.origin = _179;
	float _222 = 0.000000;
	float _223 = 0.000000;
	float _224 = 0.000000;
	float3 _221 = float3(_222, _223, _224);
	float3 _183;
	_183 = _221;
	int _225 = 0;
	int _184;
	_184 = _225;
	{
		int _229 = 0;
		int _226;
		_226 = _229;
		while (true)
{
		bool _233 = _226 < _11;
		if (!_233) { break; }
		{
			float3 _238 = -_181;
			float _240 = _1.v0.x;
			int _239 = int(_240);
			float3 _237 = cos_weighted_hemisphere_direction(_176, _238, _226, _184, _239, _8, _9, _10);
			_182.direction = _237;
			int _241 = 1;
			_184 += _241;
			_kong_intersector::result_type _234;
			{ _kong_intersector i; i.assume_geometry_type(geometry_type::triangle); i.force_opacity(forced_opacity::opaque); i.accept_any_intersection(false); _234 = i.intersect(_182, _2); }
			bool _243 = _234.type == intersection_type::triangle;
			if (_243)
			{
				float _247 = 2.000000;
				float _248 = _234.distance;
				float _249 = _248 * _247;
				float _244;
				_244 = _249;
				float3 _250 = float3(_244, _244, _244);
				_183 += _250;
			}
			int _251 = 1;
			_226 += _251;
		}
		}
	}
	float _253 = float(_11);
	float _252 = float(_253);
	float3 _254 = _183 / _252;
	_183 = _254;
	uint2 _256 = _176.xy;
	float4 _255 = _3.read(_256);
	float4 _185;
	_185 = _255;
	float3 _257 = _185.xyz;
	float3 _186;
	_186 = _257;
	float _258 = 0.000000;
	float _259 = _1.v0.x;
	bool _260 = _259 == _258;
	if (_260)
	{
		_186 = _183;
	}
	bool _263 = !_260;
	if (_263)
	{
		float _267 = _1.v0.x;
		float _268 = 1.000000;
		float _269 = _268 / _267;
		float _264;
		_264 = _269;
		float3 _271 = float3(_264, _264, _264);
		float3 _270 = mix(_186, _183, _271);
		_186 = _270;
	}
	float _273 = 1.000000;
	float4 _272 = float4(_186, _273);
	uint2 _274 = _176.xy;
	_3.write(_272, _274);
}
