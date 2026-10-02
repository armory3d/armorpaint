struct _kong_ray {
	origin: vec3<f32>,
	direction: vec3<f32>,
	min: f32,
	max: f32,
};

struct _kong_ray_query {
	hit: bool,
	front_face: bool,
	t: f32,
	barycentrics: vec2<f32>,
	primitive: u32,
	instance: u32,
};

struct _kong_node {
	min: vec3<f32>,
	first: u32, // Left child, or the first triangle of a leaf
	max: vec3<f32>,
	count: u32, // Triangles, 0 for inner nodes
};

struct _kong_instance {
	world_to_object: mat4x3<f32>,
	object_to_world: mat3x3<f32>,
	root: u32,
	geometry: u32,
};

struct _1_type {
	v0: vec4<f32>,
	v1: vec4<f32>,
	v2: vec4<f32>,
	v3: vec4<f32>,
	v4: vec4<f32>,
};

const _11: i32 = 4;

const _12: f32 = 6.28318548;

@group(0) @binding(0) var<uniform> _set0_1: _1_type;

@group(0) @binding(1) var<storage, read> _kong_nodes: array<_kong_node>;

@group(0) @binding(2) var _set0_3: texture_storage_2d<rgba32float, write>;

@group(0) @binding(3) var _set0_4: texture_2d<f32>;

@group(0) @binding(4) var _set0_5: texture_2d<f32>;

@group(0) @binding(5) var _set0_6: texture_2d<f32>;

@group(0) @binding(6) var _set0_7: texture_2d<f32>;

@group(0) @binding(7) var _set0_8: texture_2d<f32>;

@group(0) @binding(8) var _set0_9: texture_2d<f32>;

@group(0) @binding(9) var _set0_10: texture_2d<f32>;

@group(0) @binding(18) var _kong_prev: texture_2d<f32>;

@group(0) @binding(12) var<storage, read> _kong_indices: array<u32>;

@group(0) @binding(13) var<storage, read> _kong_vertices: array<vec4<u32>>;

@group(0) @binding(14) var<storage, read> _kong_instances: array<_kong_instance>;

fn _kong_position(v: u32) -> vec3<f32> {
	let raw = _kong_vertices[v];
	return vec3<f32>(unpack2x16snorm(raw.x), unpack2x16snorm(raw.y).x);
}

// Entry distance into the box, or -1 on a miss
fn _kong_box(n: u32, origin: vec3<f32>, inv_dir: vec3<f32>, tmin: f32, tmax: f32) -> f32 {
	let t0 = (_kong_nodes[n].min - origin) * inv_dir;
	let t1 = (_kong_nodes[n].max - origin) * inv_dir;
	let lo = min(t0, t1);
	let hi = max(t0, t1);
	let enter = max(max(lo.x, lo.y), max(lo.z, tmin));
	let exit = min(min(hi.x, hi.y), min(hi.z, tmax));
	return select(-1.0, enter, enter <= exit);
}

fn _kong_trace(r: _kong_ray, any_hit: bool) -> _kong_ray_query {
	var q: _kong_ray_query;
	q.hit = false;
	q.t = r.max;
	var stack: array<u32, 64>;
	let instance_count = arrayLength(&_kong_instances);
	for (var i = 0u; i < instance_count; i += 1u) {
		let instance = _kong_instances[i];
		let origin = instance.world_to_object * vec4<f32>(r.origin, 1.0);
		var dir = instance.world_to_object * vec4<f32>(r.direction, 0.0);
		dir = select(dir, vec3<f32>(1e-20), abs(dir) < vec3<f32>(1e-20));
		let inv_dir = 1.0 / dir;
		if (_kong_box(instance.root, origin, inv_dir, r.min, q.t) < 0.0) {
			continue;
		}
		var node = instance.root;
		var sp = 0u;
		loop {
			let count = _kong_nodes[node].count;
			let first = _kong_nodes[node].first;
			if (count > 0u) {
				for (var k = first; k < first + count; k += 1u) {
					let p0 = _kong_position(_kong_indices[k * 3u]);
					let e1 = _kong_position(_kong_indices[k * 3u + 1u]) - p0;
					let e2 = _kong_position(_kong_indices[k * 3u + 2u]) - p0;
					let pv = cross(dir, e2);
					let det = dot(e1, pv);
					if (det == 0.0) {
						continue;
					}
					let inv_det = 1.0 / det;
					let tv = origin - p0;
					let u = dot(tv, pv) * inv_det;
					let qv = cross(tv, e1);
					let v = dot(dir, qv) * inv_det;
					let t = dot(e2, qv) * inv_det;
					if (u >= 0.0 && v >= 0.0 && u + v <= 1.0 && t > r.min && t < q.t) {
						q.hit = true;
						q.front_face = det > 0.0;
						q.t = t;
						q.barycentrics = vec2<f32>(u, v);
						q.primitive = k;
						q.instance = i;
						if (any_hit) {
							return q;
						}
					}
				}
			}
			else {
				let t_left = _kong_box(first, origin, inv_dir, r.min, q.t);
				let t_right = _kong_box(first + 1u, origin, inv_dir, r.min, q.t);
				if (t_left >= 0.0 && t_right >= 0.0) {
					let near_left = t_left <= t_right;
					stack[sp] = select(first, first + 1u, near_left);
					sp += 1u;
					node = select(first + 1u, first, near_left);
					continue;
				}
				if (t_left >= 0.0) {
					node = first;
					continue;
				}
				if (t_right >= 0.0) {
					node = first + 1u;
					continue;
				}
			}
			if (sp == 0u) {
				break;
			}
			sp -= 1u;
			node = stack[sp];
		}
	}
	return q;
}

@compute @workgroup_size(8, 8, 1) fn main(@builtin(global_invocation_id) _kong_dispatch_thread_id: vec3<u32>) {
	var _187: vec3<u32> = _kong_dispatch_thread_id;
	var _176: vec3<u32>;
	_176 = _187;
	var _188: vec2<u32> = textureDimensions(_set0_3);
	var _177: vec2<u32>;
	_177 = _188;
	var _189: u32 = _177.y;
	var _190: u32 = _176.y;
	var _191: bool = _190 >= _189;
	var _192: u32 = _177.x;
	var _193: u32 = _176.x;
	var _194: bool = _193 >= _192;
	var _195: bool = _194 || _191;
	if (_195)
	{
		return;
	}
	var _199: vec2<u32> = _176.xy;
	var _198: vec4<f32> = textureLoad(_set0_4, vec2<u32>(u32(_199.x), u32(_199.y)), 0);
	var _178: vec4<f32>;
	_178 = _198;
	var _200: f32 = 0.000000;
	var _201: f32 = _178.w;
	var _202: bool = _201 == _200;
	if (_202)
	{
		var _206: f32 = 0.000000;
		var _207: f32 = 0.000000;
		var _208: f32 = 0.000000;
		var _209: f32 = 0.000000;
		var _205: vec4<f32> = vec4<f32>(_206, _207, _208, _209);
		var _210: vec2<u32> = _176.xy;
		textureStore(_set0_3, vec2<u32>(u32(_210.x), u32(_210.y)), _205);
		return;
	}
	var _211: vec3<f32> = _178.xyz;
	var _179: vec3<f32>;
	_179 = _211;
	var _213: vec2<u32> = _176.xy;
	var _212: vec4<f32> = textureLoad(_set0_5, vec2<u32>(u32(_213.x), u32(_213.y)), 0);
	var _180: vec4<f32>;
	_180 = _212;
	var _214: vec3<f32> = _180.xyz;
	var _181: vec3<f32>;
	_181 = _214;
	var _182: _kong_ray;
	var _215: f32 = 0.010000;
	var _216: f32 = _set0_1.v0.w;
	var _217: f32 = _216 * _215;
	_182.min = _217;
	var _218: f32 = 10.000000;
	var _219: f32 = _set0_1.v0.z;
	var _220: f32 = _219 * _218;
	_182.max = _220;
	_182.origin = _179;
	var _222: f32 = 0.000000;
	var _223: f32 = 0.000000;
	var _224: f32 = 0.000000;
	var _221: vec3<f32> = vec3<f32>(_222, _223, _224);
	var _183: vec3<f32>;
	_183 = _221;
	var _225: i32 = 0;
	var _184: i32;
	_184 = _225;
	{
		var _229: i32 = 0;
		var _226: i32;
		_226 = _229;
		while (true)
{
		var _233: bool = _226 < _11;
		if (!_233) { break; }
		{
			var _239: f32 = _set0_1.v0.x;
			var _238: i32 = i32(_239);
			var _237: vec3<f32> = cos_weighted_hemisphere_direction(_176, _181, _226, _184, _238);
			_182.direction = _237;
			var _240: i32 = 1;
			_184 += _240;
			var _234: _kong_ray_query;
			_234 = _kong_trace(_182, false);
			var _242: bool = _234.hit;
			var _243: bool = !_242;
			if (_243)
			{
				var _247: f32 = 1.000000;
				var _248: f32 = 1.000000;
				var _249: f32 = 1.000000;
				var _246: vec3<f32> = vec3<f32>(_247, _248, _249);
				_183 += _246;
			}
			var _250: i32 = 1;
			_226 += _250;
		}
		}
	}
	var _252: f32 = f32(_11);
	var _251: f32 = f32(_252);
	var _253: vec3<f32> = _183 / _251;
	_183 = _253;
	var _255: vec2<u32> = _176.xy;
	var _254: vec4<f32> = textureLoad(_kong_prev, vec2<u32>(u32(_255.x), u32(_255.y)), 0);
	var _185: vec4<f32>;
	_185 = _254;
	var _256: vec3<f32> = _185.xyz;
	var _186: vec3<f32>;
	_186 = _256;
	var _257: f32 = 0.000000;
	var _258: f32 = _set0_1.v0.x;
	var _259: bool = _258 == _257;
	if (_259)
	{
		_186 = _183;
	}
	var _262: bool = !_259;
	if (_262)
	{
		var _266: f32 = _set0_1.v0.x;
		var _267: f32 = 1.000000;
		var _268: f32 = _267 / _266;
		var _263: f32;
		_263 = _268;
		var _270: vec3<f32> = vec3<f32>(_263, _263, _263);
var _269: vec3<f32> = mix(_186, _183, _270);
		_186 = _269;
	}
	var _272: f32 = 1.000000;
	var _271: vec4<f32> = vec4<f32>(_186, _272);
	var _273: vec2<u32> = _176.xy;
	textureStore(_set0_3, vec2<u32>(u32(_273.x), u32(_273.y)), _271);
}

fn cos_weighted_hemisphere_direction(_136_in: vec3<u32>, _137_in: vec3<f32>, _138_in: i32, _139_in: i32, _140_in: i32) -> vec3<f32> {
	var _136: vec3<u32> = _136_in;
	var _137: vec3<f32> = _137_in;
	var _138: i32 = _138_in;
	var _139: i32 = _139_in;
	var _140: i32 = _140_in;
	var _150: u32 = _136.x;
	var _149: i32 = i32(_150);
	var _152: u32 = _136.y;
	var _151: i32 = i32(_152);
	var _148: f32 = rand(_149, _151, _138, _139, _140);
	var _141: f32;
	_141 = _148;
	var _155: u32 = _136.x;
	var _154: i32 = i32(_155);
	var _157: u32 = _136.y;
	var _156: i32 = i32(_157);
	var _158: i32 = 1;
	var _159: i32 = _139 + _158;
	var _153: f32 = rand(_154, _156, _138, _159, _140);
	var _142: f32;
	_142 = _153;
	var _160: f32 = 1.000000;
	var _161: f32 = 2.000000;
	var _162: f32 = _141 * _161;
	var _163: f32 = _162 - _160;
	var _143: f32;
	_143 = _163;
	var _164: f32 = _142 * _12;
	var _144: f32;
	_144 = _164;
	var _166: f32 = _143 * _143;
	var _167: f32 = 1.000000;
	var _168: f32 = _167 - _166;
	var _165: f32 = sqrt(_168);
	var _145: f32;
	_145 = _165;
	var _169: f32 = cos(_144);
	var _170: f32 = _145 * _169;
	var _146: f32;
	_146 = _170;
	var _171: f32 = sin(_144);
	var _172: f32 = _145 * _171;
	var _147: f32;
	_147 = _172;
	var _174: vec3<f32> = vec3<f32>(_146, _147, _143);
	var _175: vec3<f32> = _137 + _174;
	var _173: vec3<f32> = normalize(_175);
	return _173;
}

fn rand(_56_in: i32, _57_in: i32, _58_in: i32, _59_in: i32, _60_in: i32) -> f32 {
	var _56: i32 = _56_in;
	var _57: i32 = _57_in;
	var _58: i32 = _58_in;
	var _59: i32 = _59_in;
	var _60: i32 = _60_in;
	var _70: i32 = 8;
	var _71: i32 = 128;
	var _72: i32 = 127;
	var _73: i32 = 11;
	var _74: i32 = _60 * _73;
	var _75: i32 = _57 + _74;
	var _76: i32 = _75 & _72;
	var _77: i32 = _76 * _71;
	var _78: i32 = 127;
	var _79: i32 = 9;
	var _80: i32 = _60 * _79;
	var _81: i32 = _56 + _80;
	var _82: i32 = _81 & _78;
	var _83: i32 = _82 + _77;
	var _84: i32 = _83 * _70;
	var _85: i32 = 8;
	var _86: i32 = 255;
	var _87: i32 = _59 & _86;
	var _88: i32 = _87 % _85;
	var _89: i32 = _88 + _84;
	var _61: i32;
	_61 = _89;
	var _91: vec2<u32> = table_texel(_61);
	var _90: vec4<f32> = textureLoad(_set0_9, vec2<u32>(u32(_91.x), u32(_91.y)), 0);
	var _62: vec4<f32>;
	_62 = _90;
	var _92: u32 = table_channel(_62, _61);
	var _63: u32;
	_63 = _92;
	var _93: i32 = 8;
	var _94: i32 = 128;
	var _95: i32 = 127;
	var _96: i32 = 11;
	var _97: i32 = _60 * _96;
	var _98: i32 = _57 + _97;
	var _99: i32 = _98 & _95;
	var _100: i32 = _99 * _94;
	var _101: i32 = 127;
	var _102: i32 = 9;
	var _103: i32 = _60 * _102;
	var _104: i32 = _56 + _103;
	var _105: i32 = _104 & _101;
	var _106: i32 = _105 + _100;
	var _107: i32 = _106 * _93;
	var _108: i32 = 255;
	var _109: i32 = _59 & _108;
	var _110: i32 = _109 + _107;
	var _64: i32;
	_64 = _110;
	var _112: vec2<u32> = table_texel(_64);
	var _111: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_112.x), u32(_112.y)), 0);
	var _65: vec4<f32>;
	_65 = _111;
	var _113: u32 = table_channel(_65, _64);
	var _66: u32;
	_66 = _113;
	var _114: i32 = 255;
	var _115: i32 = _58 & _114;
	_58 = _115;
	var _116: i32 = 255;
	var _117: i32 = _59 & _116;
	_59 = _117;
	var _118: i32 = i32(_66);
	var _119: i32 = _58 ^ _118;
	var _67: i32;
	_67 = _119;
	var _122: u32 = u32(_67);
	var _123: u32 = u32(_59);
	var _121: vec2<u32> = vec2<u32>(_122, _123);
	var _120: vec4<f32> = textureLoad(_set0_8, vec2<u32>(u32(_121.x), u32(_121.y)), 0);
	var _68: vec4<f32>;
	_68 = _120;
	var _125: f32 = 255.000000;
	var _126: f32 = _68.x;
	var _127: f32 = _126 * _125;
	var _124: i32 = i32(_127);
	var _69: i32;
	_69 = _124;
	var _128: i32 = i32(_63);
	var _129: i32 = _69 ^ _128;
	_69 = _129;
	var _130: f32 = 256.000000;
	var _132: f32 = f32(_69);
	var _131: f32 = f32(_132);
	var _133: f32 = 0.500000;
	var _134: f32 = _133 + _131;
	var _135: f32 = _134 / _130;
	return _135;
}

fn table_texel(_13_in: i32) -> vec2<u32> {
	var _13: i32 = _13_in;
	var _15: i32 = 2;
	var _16: i32 = 131071;
	var _17: i32 = _13 & _16;
	var _18: i32 = _17 >> u32(_15);
	var _14: i32;
	_14 = _18;
	var _21: i32 = 127;
	var _22: i32 = _14 & _21;
	var _20: u32 = u32(_22);
	var _24: i32 = 7;
	var _25: i32 = _14 >> u32(_24);
	var _23: u32 = u32(_25);
	var _19: vec2<u32> = vec2<u32>(_20, _23);
	return _19;
}

fn table_channel(_26_in: vec4<f32>, _27_in: i32) -> u32 {
	var _26: vec4<f32> = _26_in;
	var _27: i32 = _27_in;
	var _30: i32 = 3;
	var _31: i32 = _27 & _30;
	var _28: i32;
	_28 = _31;
	var _32: f32 = _26.w;
	var _29: f32;
	_29 = _32;
	var _33: i32 = 0;
	var _34: bool = _28 == _33;
	if (_34)
	{
		var _37: f32 = _26.x;
		_29 = _37;
	}
	var _38: bool = !_34;
	var _39: i32 = 1;
	var _40: bool = _28 == _39;
	var _41: bool = _38 && _40;
	if (_41)
	{
		var _44: f32 = _26.y;
		_29 = _44;
	}
	var _45: bool = !_40;
	var _46: bool = _38 && _45;
	var _47: i32 = 2;
	var _48: bool = _28 == _47;
	var _49: bool = _46 && _48;
	if (_49)
	{
		var _52: f32 = _26.z;
		_29 = _52;
	}
	var _54: f32 = 255.000000;
	var _55: f32 = _29 * _54;
	var _53: u32 = u32(_55);
	return _53;
}

