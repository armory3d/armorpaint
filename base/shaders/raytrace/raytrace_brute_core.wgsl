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

struct sampler_cache {
	scramble: vec2<u32>,
	rank: vec4<u32>,
};

struct tangent_basis {
	tangent: vec3<f32>,
	binormal: vec3<f32>,
};

struct _1_type {
	eye: vec4<f32>,
	inv_vp: mat4x4<f32>,
	params: vec4<f32>,
};

const _13: i32 = 4;

const _14: i32 = 3;

const _15: i32 = 2;

const _16: i32 = 0;

const _17: i32 = 1;

const _18: i32 = 3;

const _19: i32 = 5;

const _20: f32 = 3.14159274;

const _21: f32 = 6.28318548;

const _22: f32 = 19.739208;

const _23: i32 = 256;

const _24: i32 = 128;

const _25: i32 = 32768;

const _26: i32 = 16;

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

@group(0) @binding(10) var _set0_11: texture_2d<f32>;

@group(0) @binding(11) var _set0_12: sampler;

@group(0) @binding(18) var _kong_prev: texture_2d<f32>;

@group(0) @binding(12) var<storage, read> _kong_indices: array<u32>;

@group(0) @binding(13) var<storage, read> _kong_vertices: array<vec4<u32>>;

@group(0) @binding(14) var<storage, read> _kong_instances: array<_kong_instance>;

@group(0) @binding(15) var _kong_geometry_texture0: texture_2d<f32>;

@group(0) @binding(16) var _kong_geometry_texture1: texture_2d<f32>;

@group(0) @binding(17) var _kong_geometry_texture2: texture_2d<f32>;

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
	var _984: vec3<u32> = _kong_dispatch_thread_id;
	var _976: vec3<u32>;
	_976 = _984;
	var _985: vec2<u32> = textureDimensions(_set0_3);
	var _977: vec2<u32>;
	_977 = _985;
	var _986: u32 = _977.y;
	var _987: u32 = _976.y;
	var _988: bool = _987 >= _986;
	var _989: u32 = _977.x;
	var _990: u32 = _976.x;
	var _991: bool = _990 >= _989;
	var _992: bool = _991 || _988;
	if (_992)
	{
		return;
	}
	var _996: f32 = _set0_1.eye.w;
	var _995: i32 = i32(_996);
	var _978: i32;
	_978 = _995;
	var _998: vec2<u32> = _976.xy;
	var _997: sampler_cache = init_sampler(_998, _978);
	var _979: sampler_cache;
	_979 = _997;
	var _1000: f32 = 0.000000;
	var _1001: f32 = 0.000000;
	var _1002: f32 = 0.000000;
	var _999: vec3<f32> = vec3<f32>(_1000, _1001, _1002);
	var _980: vec3<f32>;
	_980 = _999;
	{
		var _1006: i32 = 0;
		var _1003: i32;
		_1003 = _1006;
		while (true)
{
		var _1010: bool = _1003 < _13;
		if (!_1010) { break; }
		{
			var _1018: i32 = _978 * _13;
			var _1019: i32 = _1018 + _1003;
			var _1011: i32;
			_1011 = _1019;
			var _1021: vec2<u32> = _976.xy;
			var _1020: vec2<f32> = vec2<f32>(_1021);
			var _1012: vec2<f32>;
			_1012 = _1020;
			var _1023: vec2<u32> = _976.xy;
			var _1024: i32 = 0;
			var _1022: f32 = rnd(_1023, _1011, _1024, _979, _978);
			_1012.x += _1022;
			var _1026: vec2<u32> = _976.xy;
			var _1027: i32 = 1;
			var _1025: f32 = rnd(_1026, _1011, _1027, _979, _978);
			_1012.y += _1025;
			var _1013: _kong_ray;
			var _1028: f32 = 0.000100;
			_1013.min = _1028;
			var _1029: f32 = 100.000000;
			_1013.max = _1029;
			var _1030: vec3<f32> = _set0_1.eye.xyz;
			_1013.origin = _1030;
			var _1032: f32 = 1.000000;
			var _1033: f32 = 2.000000;
			var _1034: vec2<f32> = vec2<f32>(_977);
			var _1035: vec2<f32> = _1012 / _1034;
			var _1036: vec2<f32> = _1035 * _1033;
			var _1037: vec2<f32> = _1036 - _1032;
			var _1038: vec3<f32> = _set0_1.eye.xyz;
			var _1031: vec3<f32> = camera_ray_direction(_1037, _1038);
			_1013.direction = _1031;
			var _1040: f32 = 1.000000;
			var _1041: f32 = 1.000000;
			var _1042: f32 = 1.000000;
			var _1039: vec3<f32> = vec3<f32>(_1040, _1041, _1042);
			var _1014: vec3<f32>;
			_1014 = _1039;
			var _1043: f32 = -1.000000;
			var _1015: f32;
			_1015 = _1043;
			{
				var _1047: i32 = 0;
				var _1044: i32;
				_1044 = _1047;
				while (true)
{
				var _1051: bool = _1044 < _14;
				if (!_1051) { break; }
				{
					var _1106: i32 = _1044 * _19;
					var _1107: i32 = _15 + _1106;
					var _1052: i32;
					_1052 = _1107;
					var _1053: _kong_ray_query;
					_1053 = _kong_trace(_1013, false);
					var _1109: bool = _1053.hit;
					var _1110: bool = !_1109;
					if (_1110)
					{
						var _1111: vec3<f32>;
						var _1115: f32 = 1.000000;
						var _1112: f32;
						_1112 = _1115;
						var _1116: f32 = 0.000000;
						var _1117: f32 = _set0_1.params.x;
						var _1118: bool = _1117 < _1116;
						var _1119: i32 = 0;
						var _1120: bool = _1044 == _1119;
						var _1121: bool = _1120 && _1118;
						if (_1121)
						{
							var _1125: f32 = 0.027500;
							var _1126: f32 = 0.027500;
							var _1127: f32 = 0.027500;
							var _1124: vec3<f32> = vec3<f32>(_1125, _1126, _1127);
							_1111 = _1124;
						}
						var _1128: bool = !_1121;
						if (_1128)
						{
							var _1132: vec3<f32> = _1013.direction;
							var _1131: vec3<f32> = env_radiance(_1132);
							_1111 = _1131;
							var _1133: f32 = 0.000000;
							var _1134: bool = _1015 > _1133;
							if (_1134)
							{
								var _1139: vec3<f32> = _1013.direction;
								var _1138: f32 = env_pdf(_1139);
								var _1137: f32 = mis_weight(_1015, _1138);
								_1112 = _1137;
							}
						}
						var _1141: vec3<f32> = _1014 * _1111;
						var _1142: vec3<f32> = _1141 * _1112;
						var _1143: f32 = 0.000000;
						var _1144: vec3<f32> = vec3<f32>(_1143, _1143, _1143);
						var _1145: f32 = 8.000000;
						var _1146: vec3<f32> = vec3<f32>(_1145, _1145, _1145);
						var _1140: vec3<f32> = clamp(_1142, _1144, _1146);
						_980 += _1140;
						break;
					}
					var _1147: u32 = _kong_instances[_1053.instance].geometry;
					var _1054: u32;
					_1054 = _1147;
					var _1148: vec2<f32> = _1053.barycentrics;
					var _1055: vec2<f32>;
					_1055 = _1148;
					var _1150: i32 = 0;
					var _1149: vec4<u32> = _kong_vertices[_kong_indices[_1053.primitive * 3u + u32(_1150)]];
					var _1056: vec4<u32>;
					_1056 = _1149;
					var _1152: i32 = 1;
					var _1151: vec4<u32> = _kong_vertices[_kong_indices[_1053.primitive * 3u + u32(_1152)]];
					var _1057: vec4<u32>;
					_1057 = _1151;
					var _1154: i32 = 2;
					var _1153: vec4<u32> = _kong_vertices[_kong_indices[_1053.primitive * 3u + u32(_1154)]];
					var _1058: vec4<u32>;
					_1058 = _1153;
					var _1156: u32 = _1056.w;
					var _1155: vec2<f32> = s16_to_f32(_1156);
					var _1059: vec2<f32>;
					_1059 = _1155;
					var _1158: u32 = _1057.w;
					var _1157: vec2<f32> = s16_to_f32(_1158);
					var _1060: vec2<f32>;
					_1060 = _1157;
					var _1160: u32 = _1058.w;
					var _1159: vec2<f32> = s16_to_f32(_1160);
					var _1061: vec2<f32>;
					_1061 = _1159;
					var _1161: f32 = _set0_1.params.z;
					var _1162: vec2<f32> = hit_attribute2d(_1059, _1060, _1061, _1055);
					var _1163: vec2<f32> = _1162 * _1161;
					var _1062: vec2<f32>;
					_1062 = _1163;
					var _1164: vec2<u32> = textureDimensions(_kong_geometry_texture0);
					var _1063: vec2<u32>;
					_1063 = _1164;
					var _1166: vec2<f32> = vec2<f32>(_1063);
var _1167: vec2<f32> = fract(_1062);
					var _1168: vec2<f32> = _1167 * _1166;
					var _1165: vec2<u32> = vec2<u32>(_1168);
					var _1064: vec2<u32>;
					_1064 = _1165;
					var _1169: vec4<f32> = textureLoad(_kong_geometry_texture0, _1064, 0);
					var _1065: vec4<f32>;
					_1065 = _1169;
					var _1170: f32 = _1053.t;
					var _1066: f32;
					_1066 = _1170;
					var _1171: vec3<f32> = _1013.direction;
					var _1172: vec3<f32> = _1171 * _1066;
					var _1173: vec3<f32> = _1013.origin;
					var _1174: vec3<f32> = _1173 + _1172;
					var _1067: vec3<f32>;
					_1067 = _1174;
					var _1176: u32 = _1056.y;
					var _1175: vec2<f32> = s16_to_f32(_1176);
					var _1068: vec2<f32>;
					_1068 = _1175;
					var _1178: u32 = _1057.y;
					var _1177: vec2<f32> = s16_to_f32(_1178);
					var _1069: vec2<f32>;
					_1069 = _1177;
					var _1180: u32 = _1058.y;
					var _1179: vec2<f32> = s16_to_f32(_1180);
					var _1070: vec2<f32>;
					_1070 = _1179;
					var _1183: u32 = _1056.x;
					var _1182: vec2<f32> = s16_to_f32(_1183);
					var _1184: f32 = _1068.x;
					var _1181: vec3<f32> = vec3<f32>(_1182, _1184);
					var _1071: vec3<f32>;
					_1071 = _1181;
					var _1187: u32 = _1057.x;
					var _1186: vec2<f32> = s16_to_f32(_1187);
					var _1188: f32 = _1069.x;
					var _1185: vec3<f32> = vec3<f32>(_1186, _1188);
					var _1072: vec3<f32>;
					_1072 = _1185;
					var _1191: u32 = _1058.x;
					var _1190: vec2<f32> = s16_to_f32(_1191);
					var _1192: f32 = _1070.x;
					var _1189: vec3<f32> = vec3<f32>(_1190, _1192);
					var _1073: vec3<f32>;
					_1073 = _1189;
					var _1195: u32 = _1056.z;
					var _1194: vec2<f32> = s16_to_f32(_1195);
					var _1196: f32 = _1068.y;
					var _1193: vec3<f32> = vec3<f32>(_1194, _1196);
					var _1074: vec3<f32>;
					_1074 = _1193;
					var _1199: u32 = _1057.z;
					var _1198: vec2<f32> = s16_to_f32(_1199);
					var _1200: f32 = _1069.y;
					var _1197: vec3<f32> = vec3<f32>(_1198, _1200);
					var _1075: vec3<f32>;
					_1075 = _1197;
					var _1203: u32 = _1058.z;
					var _1202: vec2<f32> = s16_to_f32(_1203);
					var _1204: f32 = _1070.y;
					var _1201: vec3<f32> = vec3<f32>(_1202, _1204);
					var _1076: vec3<f32>;
					_1076 = _1201;
					var _1206: vec3<f32> = hit_attribute(_1074, _1075, _1076, _1055);
					var _1205: vec3<f32> = normalize(_1206);
					var _1077: vec3<f32>;
					_1077 = _1205;
					var _1208: vec3<f32> = _1072 - _1071;
					var _1209: vec3<f32> = _1073 - _1071;
					var _1207: vec3<f32> = cross(_1208, _1209);
					var _1078: vec3<f32>;
					_1078 = _1207;
					var _1210: f32 = 0.000000;
					var _1211: f32 = dot(_1078, _1078);
					var _1212: bool = _1211 > _1210;
					if (_1212)
					{
						var _1215: vec3<f32> = normalize(_1078);
						_1078 = _1215;
					}
					var _1216: bool = !_1212;
					if (_1216)
					{
						_1078 = _1077;
					}
					var _1219: f32 = 0.000000;
					var _1220: f32 = dot(_1078, _1077);
					var _1221: bool = _1220 < _1219;
					if (_1221)
					{
						var _1224: vec3<f32> = -_1078;
						_1078 = _1224;
					}
					var _1079: vec3<f32>;
					_1079 = _1077;
					var _1225: mat3x3<f32> = _kong_instances[_1053.instance].object_to_world;
					var _1080: mat3x3<f32>;
					_1080 = _1225;
					var _1227: vec3<f32> = _1080 * _1077;
					var _1226: vec3<f32> = normalize(_1227);
					_1077 = _1226;
					var _1229: vec3<f32> = _1080 * _1078;
					var _1228: vec3<f32> = normalize(_1229);
					_1078 = _1228;
					var _1230: f32 = 0.000000;
					var _1232: vec3<f32> = _1013.direction;
					var _1231: f32 = dot(_1078, _1232);
					var _1233: bool = _1231 > _1230;
					var _1081: bool;
					_1081 = _1233;
					if (_1081)
					{
						var _1236: vec3<f32> = -_1078;
						_1078 = _1236;
						var _1237: vec3<f32> = -_1077;
						_1077 = _1237;
					}
					var _1238: i32 = 0;
					var _1239: bool = _1044 == _1238;
					var _1082: bool;
					_1082 = _1239;
					var _1241: f32 = 0.000000;
					var _1242: f32 = 0.000000;
					var _1243: f32 = 0.000000;
					var _1244: f32 = 0.000000;
					var _1240: vec4<f32> = vec4<f32>(_1241, _1242, _1243, _1244);
					var _1083: vec4<f32>;
					_1083 = _1240;
					if (_1082)
					{
						var _1247: vec4<f32> = textureLoad(_kong_geometry_texture1, _1064, 0);
						_1083 = _1247;
					}
					var _1249: vec3<f32> = _1065.xyz;
					var _1248: vec3<f32> = srgb_to_linear(_1249);
					var _1084: vec3<f32>;
					_1084 = _1248;
					var _1250: vec4<f32> = textureLoad(_kong_geometry_texture2, _1064, 0);
					var _1085: vec4<f32>;
					_1085 = _1250;
					var _1252: vec2<u32> = _976.xy;
					var _1253: i32 = _1052 + _16;
					var _1251: f32 = rnd(_1252, _1011, _1253, _979, _978);
					var _1086: f32;
					_1086 = _1251;
					var _1087: vec3<f32>;
					var _1088: vec3<f32>;
					if (_1082)
					{
						var _1258: tangent_basis = create_uv_basis(_1071, _1072, _1073, _1059, _1060, _1061, _1079);
						var _1254: tangent_basis;
						_1254 = _1258;
						var _1259: vec3<f32> = _1254.tangent;
						var _1260: vec3<f32> = _1080 * _1259;
						_1087 = _1260;
						var _1261: vec3<f32> = _1254.binormal;
						var _1262: vec3<f32> = _1080 * _1261;
						_1088 = _1262;
						var _1264: f32 = dot(_1077, _1087);
						var _1265: vec3<f32> = _1077 * _1264;
						var _1266: vec3<f32> = _1087 - _1265;
						var _1263: vec3<f32> = normalize(_1266);
						_1087 = _1263;
						var _1268: f32 = dot(_1087, _1088);
						var _1269: vec3<f32> = _1087 * _1268;
						var _1270: f32 = dot(_1077, _1088);
						var _1271: vec3<f32> = _1077 * _1270;
						var _1272: vec3<f32> = _1088 - _1271;
						var _1273: vec3<f32> = _1272 - _1269;
						var _1267: vec3<f32> = normalize(_1273);
						_1088 = _1267;
						if (_1081)
						{
							var _1276: vec3<f32> = -_1088;
							_1088 = _1276;
						}
						var _1278: f32 = 1.000000;
						var _1279: f32 = 2.000000;
						var _1280: vec3<f32> = _1083.xyz;
						var _1281: vec3<f32> = _1280 * _1279;
						var _1282: vec3<f32> = _1281 - _1278;
						var _1277: vec3<f32> = normalize(_1282);
						var _1255: vec3<f32>;
						_1255 = _1277;
						var _1284: f32 = _1255.z;
						var _1285: vec3<f32> = _1077 * _1284;
						var _1286: f32 = _1255.y;
						var _1287: vec3<f32> = _1088 * _1286;
						var _1288: f32 = _1255.x;
						var _1289: vec3<f32> = _1087 * _1288;
						var _1290: vec3<f32> = _1289 - _1287;
						var _1291: vec3<f32> = _1290 + _1285;
						var _1283: vec3<f32> = normalize(_1291);
						_1077 = _1283;
						var _1292: f32 = 0.000100;
						var _1293: f32 = dot(_1077, _1078);
						var _1294: bool = _1293 < _1292;
						if (_1294)
						{
							var _1298: f32 = dot(_1077, _1078);
							var _1299: f32 = 0.000100;
							var _1300: f32 = _1299 - _1298;
							var _1301: vec3<f32> = _1078 * _1300;
							var _1302: vec3<f32> = _1077 + _1301;
							var _1297: vec3<f32> = normalize(_1302);
							_1077 = _1297;
						}
					}
					var _1303: vec3<f32> = _1013.direction;
					var _1304: vec3<f32> = -_1303;
					var _1089: vec3<f32>;
					_1089 = _1304;
					var _1305: f32 = dot(_1077, _1089);
					var _1090: f32;
					_1090 = _1305;
					var _1306: f32 = 0.001000;
					var _1307: bool = _1090 < _1306;
					if (_1307)
					{
						var _1311: i32 = 0;
						var _1312: bool = _1044 > _1311;
						if (_1312)
						{
							break;
						}
						var _1316: f32 = 0.001000;
						var _1317: f32 = _1316 - _1090;
						var _1318: vec3<f32> = _1089 * _1317;
						var _1319: vec3<f32> = _1077 + _1318;
						var _1315: vec3<f32> = normalize(_1319);
						_1077 = _1315;
						var _1320: f32 = dot(_1077, _1078);
						var _1308: f32;
						_1308 = _1320;
						var _1321: f32 = 0.001000;
						var _1322: bool = _1308 < _1321;
						if (_1322)
						{
							var _1326: f32 = 0.001000;
							var _1327: f32 = _1326 - _1308;
							var _1328: vec3<f32> = _1078 * _1327;
							var _1329: vec3<f32> = _1077 + _1328;
							var _1325: vec3<f32> = normalize(_1329);
							_1077 = _1325;
						}
						var _1331: f32 = dot(_1077, _1089);
						var _1332: f32 = 0.001000;
						var _1330: f32 = max(_1331, _1332);
						_1090 = _1330;
					}
					var _1333: tangent_basis = create_basis(_1077);
					var _1091: tangent_basis;
					_1091 = _1333;
					var _1334: vec3<f32> = _1091.tangent;
					_1087 = _1334;
					var _1335: vec3<f32> = _1091.binormal;
					_1088 = _1335;
					var _1337: f32 = _1085.z;
					var _1336: vec3<f32> = surface_albedo(_1084, _1337);
					var _1092: vec3<f32>;
					_1092 = _1336;
					var _1339: f32 = _1085.z;
					var _1338: vec3<f32> = surface_specular(_1084, _1339);
					var _1093: vec3<f32>;
					_1093 = _1338;
					var _1340: f32 = _1085.y;
					var _1094: f32;
					_1094 = _1340;
					var _1341: vec3<f32> = spec_directional_albedo(_1093, _1090, _1094);
					var _1095: vec3<f32>;
					_1095 = _1341;
					var _1342: f32 = 1.000000;
					var _1343: vec3<f32> = _1342 - _1095;
					var _1344: vec3<f32> = _1092 * _1343;
					var _1096: vec3<f32>;
					_1096 = _1344;
					var _1345: f32 = luma(_1095);
					var _1097: f32;
					_1097 = _1345;
					var _1346: f32 = luma(_1096);
					var _1098: f32;
					_1098 = _1346;
					var _1349: f32 = _1097 + _1098;
					var _1350: f32 = 0.000010;
					var _1348: f32 = max(_1349, _1350);
					var _1351: f32 = _1097 / _1348;
					var _1352: f32 = 0.050000;
					var _1353: f32 = 0.995000;
					var _1347: f32 = clamp(_1351, _1352, _1353);
					var _1099: f32;
					_1099 = _1347;
					var _1355: f32 = _1094 * _1094;
					var _1356: f32 = 0.001000;
					var _1354: f32 = max(_1355, _1356);
					var _1100: f32;
					_1100 = _1354;
					var _1357: f32 = 0.000000;
					var _1358: f32 = _set0_1.params.w;
					var _1359: bool = _1358 != _1357;
					if (_1359)
					{
						var _1367: vec2<u32> = _976.xy;
						var _1368: i32 = _1052 + _18;
						var _1366: f32 = rnd(_1367, _1011, _1368, _979, _978);
						var _1370: vec2<u32> = _976.xy;
						var _1371: i32 = 1;
						var _1372: i32 = _1052 + _18;
						var _1373: i32 = _1372 + _1371;
						var _1369: f32 = rnd(_1370, _1011, _1373, _979, _978);
						var _1365: vec4<f32> = sample_env(_1366, _1369);
						var _1360: vec4<f32>;
						_1360 = _1365;
						var _1374: vec3<f32> = _1360.xyz;
						var _1361: vec3<f32>;
						_1361 = _1374;
						var _1375: f32 = _1360.w;
						var _1362: f32;
						_1362 = _1375;
						var _1376: f32 = 0.000000;
						var _1377: f32 = dot(_1361, _1078);
						var _1378: bool = _1377 > _1376;
						var _1379: f32 = 0.000000;
						var _1380: bool = _1362 > _1379;
						var _1381: bool = _1380 && _1378;
						if (_1381)
						{
							var _1386: vec4<f32> = bsdf_eval(_1361, _1077, _1089, _1090, _1096, _1093, _1100, _1099);
							var _1382: vec4<f32>;
							_1382 = _1386;
							var _1387: vec3<f32> = _1382.xyz;
							var _1383: vec3<f32>;
							_1383 = _1387;
							var _1388: f32 = 0.000000;
							var _1391: f32 = _1383.x;
							var _1392: f32 = _1383.y;
							var _1390: f32 = max(_1391, _1392);
							var _1393: f32 = _1383.z;
							var _1389: f32 = max(_1390, _1393);
							var _1394: bool = _1389 > _1388;
							if (_1394)
							{
								var _1398: vec3<f32> = offset_ray(_1067, _1078, _1361);
								var _1397: bool = occluded(_1398, _1361);
								var _1399: bool = !_1397;
								if (_1399)
								{
									var _1404: f32 = _1382.w;
									var _1403: f32 = mis_weight(_1362, _1404);
									var _1405: f32 = _1403 / _1362;
									var _1406: vec3<f32> = env_radiance(_1361);
									var _1407: vec3<f32> = _1014 * _1383;
									var _1408: vec3<f32> = _1407 * _1406;
									var _1409: vec3<f32> = _1408 * _1405;
									var _1410: f32 = 0.000000;
									var _1411: vec3<f32> = vec3<f32>(_1410, _1410, _1410);
									var _1412: f32 = 8.000000;
									var _1413: vec3<f32> = vec3<f32>(_1412, _1412, _1412);
									var _1402: vec3<f32> = clamp(_1409, _1411, _1413);
									_980 += _1402;
								}
							}
						}
					}
					var _1415: vec2<u32> = _976.xy;
					var _1416: i32 = _1052 + _17;
					var _1414: f32 = rnd(_1415, _1011, _1416, _979, _978);
					var _1101: f32;
					_1101 = _1414;
					var _1418: vec2<u32> = _976.xy;
					var _1419: i32 = 1;
					var _1420: i32 = _1052 + _17;
					var _1421: i32 = _1420 + _1419;
					var _1417: f32 = rnd(_1418, _1011, _1421, _979, _978);
					var _1102: f32;
					_1102 = _1417;
					var _1422: bool = _1086 < _1099;
					if (_1422)
					{
						var _1432: f32 = _1100 * _1100;
						var _1423: f32;
						_1423 = _1432;
						var _1434: f32 = dot(_1089, _1087);
						var _1435: f32 = dot(_1089, _1088);
						var _1433: vec3<f32> = vec3<f32>(_1434, _1435, _1090);
						var _1424: vec3<f32>;
						_1424 = _1433;
						var _1436: vec3<f32> = sample_ggx_vndf(_1424, _1100, _1101, _1102);
						var _1425: vec3<f32>;
						_1425 = _1436;
						var _1438: vec3<f32> = -_1424;
						var _1437: vec3<f32> = reflect(_1438, _1425);
						var _1426: vec3<f32>;
						_1426 = _1437;
						var _1439: f32 = 0.000000;
						var _1440: f32 = _1426.z;
						var _1441: bool = _1440 <= _1439;
						if (_1441)
						{
							break;
						}
						var _1444: f32 = _1426.z;
						var _1445: vec3<f32> = _1077 * _1444;
						var _1446: f32 = _1426.y;
						var _1447: vec3<f32> = _1088 * _1446;
						var _1448: f32 = _1426.x;
						var _1449: vec3<f32> = _1087 * _1448;
						var _1450: vec3<f32> = _1449 + _1447;
						var _1451: vec3<f32> = _1450 + _1445;
						_1013.direction = _1451;
						var _1453: f32 = _1424.z;
						var _1452: f32 = smith_lambda(_1453, _1423);
						var _1427: f32;
						_1427 = _1452;
						var _1455: f32 = _1426.z;
						var _1454: f32 = smith_lambda(_1455, _1423);
						var _1428: f32;
						_1428 = _1454;
						var _1456: f32 = 1.000000;
						var _1457: f32 = _1456 + _1427;
						var _1458: f32 = _1457 + _1428;
						var _1459: f32 = 1.000000;
						var _1460: f32 = _1459 + _1427;
						var _1461: f32 = _1460 / _1458;
						var _1429: f32;
						_1429 = _1461;
						var _1462: f32 = _1429 / _1099;
						var _1465: f32 = dot(_1424, _1425);
						var _1466: f32 = 0.000000;
						var _1464: f32 = max(_1465, _1466);
						var _1463: vec3<f32> = f_schlick(_1093, _1464);
						var _1467: vec3<f32> = _1463 * _1462;
						_1014 *= _1467;
					}
					var _1468: bool = !_1422;
					if (_1468)
					{
						var _1471: vec3<f32> = cos_weighted_direction(_1087, _1088, _1077, _1101, _1102);
						_1013.direction = _1471;
						var _1472: f32 = 1.000000;
						var _1473: f32 = _1472 - _1099;
						var _1474: vec3<f32> = _1096 / _1473;
						_1014 *= _1474;
					}
					var _1475: f32 = 0.000000;
					var _1477: vec3<f32> = _1013.direction;
					var _1476: f32 = dot(_1477, _1078);
					var _1478: bool = _1476 <= _1475;
					if (_1478)
					{
						break;
					}
					var _1481: f32 = 0.000000;
					var _1484: f32 = _1014.x;
					var _1485: f32 = _1014.y;
					var _1483: f32 = max(_1484, _1485);
					var _1486: f32 = _1014.z;
					var _1482: f32 = max(_1483, _1486);
					var _1487: bool = _1482 <= _1481;
					if (_1487)
					{
						break;
					}
					var _1491: vec3<f32> = _1013.direction;
					var _1490: vec3<f32> = offset_ray(_1067, _1078, _1491);
					_1013.origin = _1490;
					var _1493: vec3<f32> = _1013.direction;
					var _1492: vec4<f32> = bsdf_eval(_1493, _1077, _1089, _1090, _1096, _1093, _1100, _1099);
					var _1103: vec4<f32>;
					_1103 = _1492;
					var _1494: f32 = _1103.w;
					_1015 = _1494;
					var _1495: i32 = 1;
					_1044 += _1495;
				}
				}
			}
			var _1496: i32 = 1;
			_1003 += _1496;
		}
		}
	}
	var _1498: vec2<u32> = _976.xy;
	var _1497: vec4<f32> = textureLoad(_kong_prev, vec2<u32>(u32(_1498.x), u32(_1498.y)), 0);
	var _981: vec4<f32>;
	_981 = _1497;
	var _1499: vec3<f32> = _981.xyz;
	var _982: vec3<f32>;
	_982 = _1499;
	var _1501: f32 = f32(_13);
	var _1500: f32 = f32(_1501);
	var _1502: vec3<f32> = _980 / _1500;
	_980 = _1502;
	var _1503: f32 = 1.000000;
	var _1504: f32 = _set0_1.eye.w;
	var _1505: f32 = _1504 + _1503;
	var _1506: f32 = 1.000000;
	var _1507: f32 = _1506 / _1505;
	var _983: f32;
	_983 = _1507;
	var _1509: vec3<f32> = vec3<f32>(_983, _983, _983);
var _1508: vec3<f32> = mix(_982, _980, _1509);
	_982 = _1508;
	var _1511: f32 = 1.000000;
	var _1510: vec4<f32> = vec4<f32>(_982, _1511);
	var _1512: vec2<u32> = _976.xy;
	textureStore(_set0_3, vec2<u32>(u32(_1512.x), u32(_1512.y)), _1510);
}

fn init_sampler(_572_in: vec2<u32>, _573_in: i32) -> sampler_cache {
	var _572: vec2<u32> = _572_in;
	var _573: i32 = _573_in;
	var _578: i32 = 127;
	var _579: i32 = 9;
	var _580: i32 = _573 * _579;
	var _582: u32 = _572.x;
	var _581: i32 = i32(_582);
	var _583: i32 = _581 + _580;
	var _584: i32 = _583 & _578;
	var _574: i32;
	_574 = _584;
	var _585: i32 = 127;
	var _586: i32 = 11;
	var _587: i32 = _573 * _586;
	var _589: u32 = _572.y;
	var _588: i32 = i32(_589);
	var _590: i32 = _588 + _587;
	var _591: i32 = _590 & _585;
	var _575: i32;
	_575 = _591;
	var _592: i32 = 8;
	var _593: i32 = 128;
	var _594: i32 = _575 * _593;
	var _595: i32 = _574 + _594;
	var _596: i32 = _595 * _592;
	var _576: i32;
	_576 = _596;
	var _577: sampler_cache;
	var _600: vec2<u32> = table_texel(_576);
	var _599: vec4<f32> = textureLoad(_set0_9, vec2<u32>(u32(_600.x), u32(_600.y)), 0);
	var _598: u32 = table_word(_599);
	var _604: i32 = 4;
	var _605: i32 = _576 + _604;
	var _603: vec2<u32> = table_texel(_605);
	var _602: vec4<f32> = textureLoad(_set0_9, vec2<u32>(u32(_603.x), u32(_603.y)), 0);
	var _601: u32 = table_word(_602);
	var _597: vec2<u32> = vec2<u32>(_598, _601);
	_577.scramble = _597;
	var _609: vec2<u32> = table_texel(_576);
	var _608: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_609.x), u32(_609.y)), 0);
	var _607: u32 = table_word(_608);
	var _613: i32 = 4;
	var _614: i32 = _576 + _613;
	var _612: vec2<u32> = table_texel(_614);
	var _611: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_612.x), u32(_612.y)), 0);
	var _610: u32 = table_word(_611);
	var _618: i32 = 8;
	var _619: i32 = _576 + _618;
	var _617: vec2<u32> = table_texel(_619);
	var _616: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_617.x), u32(_617.y)), 0);
	var _615: u32 = table_word(_616);
	var _623: i32 = 12;
	var _624: i32 = _576 + _623;
	var _622: vec2<u32> = table_texel(_624);
	var _621: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_622.x), u32(_622.y)), 0);
	var _620: u32 = table_word(_621);
	var _606: vec4<u32> = vec4<u32>(_607, _610, _615, _620);
	_577.rank = _606;
	return _577;
}

fn table_texel(_69_in: i32) -> vec2<u32> {
	var _69: i32 = _69_in;
	var _71: i32 = 2;
	var _72: i32 = 131071;
	var _73: i32 = _69 & _72;
	var _74: i32 = _73 >> u32(_71);
	var _70: i32;
	_70 = _74;
	var _77: i32 = 127;
	var _78: i32 = _70 & _77;
	var _76: u32 = u32(_78);
	var _80: i32 = 7;
	var _81: i32 = _70 >> u32(_80);
	var _79: u32 = u32(_81);
	var _75: vec2<u32> = vec2<u32>(_76, _79);
	return _75;
}

fn table_word(_112_in: vec4<f32>) -> u32 {
	var _112: vec4<f32> = _112_in;
	var _113: i32 = 24;
	var _115: f32 = 255.000000;
	var _116: f32 = _112.w;
	var _117: f32 = _116 * _115;
	var _114: u32 = u32(_117);
	var _118: u32 = _114 << u32(_113);
	var _119: i32 = 16;
	var _121: f32 = 255.000000;
	var _122: f32 = _112.z;
	var _123: f32 = _122 * _121;
	var _120: u32 = u32(_123);
	var _124: u32 = _120 << u32(_119);
	var _125: i32 = 8;
	var _127: f32 = 255.000000;
	var _128: f32 = _112.y;
	var _129: f32 = _128 * _127;
	var _126: u32 = u32(_129);
	var _130: u32 = _126 << u32(_125);
	var _132: f32 = 255.000000;
	var _133: f32 = _112.x;
	var _134: f32 = _133 * _132;
	var _131: u32 = u32(_134);
	var _135: u32 = _131 | _130;
	var _136: u32 = _135 | _124;
	var _137: u32 = _136 | _118;
	return _137;
}

fn rnd(_625_in: vec2<u32>, _626_in: i32, _627_in: i32, _628_in: sampler_cache, _629_in: i32) -> f32 {
	var _625: vec2<u32> = _625_in;
	var _626: i32 = _626_in;
	var _627: i32 = _627_in;
	var _628: sampler_cache = _628_in;
	var _629: i32 = _629_in;
	var _634: i32 = 7;
	var _635: i32 = _627 & _634;
	var _630: i32;
	_630 = _635;
	var _636: u32 = _628.scramble.y;
	var _631: u32;
	_631 = _636;
	var _637: i32 = 4;
	var _638: bool = _630 < _637;
	if (_638)
	{
		var _641: u32 = _628.scramble.x;
		_631 = _641;
	}
	var _642: i32 = 255;
	var _643: i32 = 8;
	var _644: i32 = 3;
	var _645: i32 = _630 & _644;
	var _646: i32 = _645 * _643;
	var _647: u32 = _631 >> u32(_646);
	var _648: u32 = _647 & u32(_642);
	var _632: u32;
	_632 = _648;
	var _633: u32;
	var _649: bool = _627 < _26;
	if (_649)
	{
		var _654: i32 = 2;
		var _655: i32 = _627 >> u32(_654);
		var _650: i32;
		_650 = _655;
		var _656: u32 = _628.rank.w;
		var _651: u32;
		_651 = _656;
		var _657: i32 = 0;
		var _658: bool = _650 == _657;
		if (_658)
		{
			var _661: u32 = _628.rank.x;
			_651 = _661;
		}
		var _662: bool = !_658;
		var _663: i32 = 1;
		var _664: bool = _650 == _663;
		var _665: bool = _662 && _664;
		if (_665)
		{
			var _668: u32 = _628.rank.y;
			_651 = _668;
		}
		var _669: bool = !_664;
		var _670: bool = _662 && _669;
		var _671: i32 = 2;
		var _672: bool = _650 == _671;
		var _673: bool = _670 && _672;
		if (_673)
		{
			var _676: u32 = _628.rank.z;
			_651 = _676;
		}
		var _677: i32 = 255;
		var _678: i32 = 8;
		var _679: i32 = 3;
		var _680: i32 = _627 & _679;
		var _681: i32 = _680 * _678;
		var _682: u32 = _651 >> u32(_681);
		var _683: u32 = _682 & u32(_677);
		_633 = _683;
	}
	var _684: bool = !_649;
	if (_684)
	{
		var _689: u32 = _625.x;
		var _688: i32 = i32(_689);
		var _691: u32 = _625.y;
		var _690: i32 = i32(_691);
		var _687: u32 = rank_value_at(_688, _690, _627, _629);
		_633 = _687;
	}
	var _692: f32 = rand_indexed(_626, _627, _633, _632);
	return _692;
}

fn rank_value_at(_138_in: i32, _139_in: i32, _140_in: i32, _141_in: i32) -> u32 {
	var _138: i32 = _138_in;
	var _139: i32 = _139_in;
	var _140: i32 = _140_in;
	var _141: i32 = _141_in;
	var _144: i32 = 8;
	var _145: i32 = 128;
	var _146: i32 = 127;
	var _147: i32 = 11;
	var _148: i32 = _141 * _147;
	var _149: i32 = _139 + _148;
	var _150: i32 = _149 & _146;
	var _151: i32 = _150 * _145;
	var _152: i32 = 127;
	var _153: i32 = 9;
	var _154: i32 = _141 * _153;
	var _155: i32 = _138 + _154;
	var _156: i32 = _155 & _152;
	var _157: i32 = _156 + _151;
	var _158: i32 = _157 * _144;
	var _159: i32 = 255;
	var _160: i32 = _140 & _159;
	var _161: i32 = _160 + _158;
	var _142: i32;
	_142 = _161;
	var _163: vec2<u32> = table_texel(_142);
	var _162: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_163.x), u32(_163.y)), 0);
	var _143: vec4<f32>;
	_143 = _162;
	var _164: u32 = table_byte(_143, _142);
	return _164;
}

fn table_byte(_82_in: vec4<f32>, _83_in: i32) -> u32 {
	var _82: vec4<f32> = _82_in;
	var _83: i32 = _83_in;
	var _86: i32 = 3;
	var _87: i32 = _83 & _86;
	var _84: i32;
	_84 = _87;
	var _88: f32 = _82.w;
	var _85: f32;
	_85 = _88;
	var _89: i32 = 0;
	var _90: bool = _84 == _89;
	if (_90)
	{
		var _93: f32 = _82.x;
		_85 = _93;
	}
	var _94: bool = !_90;
	var _95: i32 = 1;
	var _96: bool = _84 == _95;
	var _97: bool = _94 && _96;
	if (_97)
	{
		var _100: f32 = _82.y;
		_85 = _100;
	}
	var _101: bool = !_96;
	var _102: bool = _94 && _101;
	var _103: i32 = 2;
	var _104: bool = _84 == _103;
	var _105: bool = _102 && _104;
	if (_105)
	{
		var _108: f32 = _82.z;
		_85 = _108;
	}
	var _110: f32 = 255.000000;
	var _111: f32 = _85 * _110;
	var _109: u32 = u32(_111);
	return _109;
}

fn rand_indexed(_165_in: i32, _166_in: i32, _167_in: u32, _168_in: u32) -> f32 {
	var _165: i32 = _165_in;
	var _166: i32 = _166_in;
	var _167: u32 = _167_in;
	var _168: u32 = _168_in;
	var _172: i32 = 255;
	var _173: i32 = _165 & _172;
	_165 = _173;
	var _174: i32 = 255;
	var _175: i32 = _166 & _174;
	_166 = _175;
	var _176: i32 = i32(_167);
	var _177: i32 = _165 ^ _176;
	var _169: i32;
	_169 = _177;
	var _180: u32 = u32(_169);
	var _181: u32 = u32(_166);
	var _179: vec2<u32> = vec2<u32>(_180, _181);
	var _178: vec4<f32> = textureLoad(_set0_8, vec2<u32>(u32(_179.x), u32(_179.y)), 0);
	var _170: vec4<f32>;
	_170 = _178;
	var _183: f32 = 255.000000;
	var _184: f32 = _170.x;
	var _185: f32 = _184 * _183;
	var _182: i32 = i32(_185);
	var _171: i32;
	_171 = _182;
	var _186: i32 = i32(_168);
	var _187: i32 = _171 ^ _186;
	_171 = _187;
	var _188: f32 = 256.000000;
	var _190: f32 = f32(_171);
	var _189: f32 = f32(_190);
	var _191: f32 = 0.500000;
	var _192: f32 = _191 + _189;
	var _193: f32 = _192 / _188;
	return _193;
}

fn camera_ray_direction(_252_in: vec2<f32>, _253_in: vec3<f32>) -> vec3<f32> {
	var _252: vec2<f32> = _252_in;
	var _253: vec3<f32> = _253_in;
	var _257: f32 = _252.x;
	var _258: f32 = _252.y;
	var _259: f32 = -_258;
	var _260: f32 = 0.000000;
	var _261: f32 = 1.000000;
	var _256: vec4<f32> = vec4<f32>(_257, _259, _260, _261);
	var _262: mat4x4<f32> = _set0_1.inv_vp;
	var _263: vec4<f32> = _262 * _256;
	var _254: vec4<f32>;
	_254 = _263;
	var _264: f32 = _254.w;
	var _265: vec3<f32> = _254.xyz;
	var _266: vec3<f32> = _265 / _264;
	var _255: vec3<f32>;
	_255 = _266;
	var _268: vec3<f32> = _255 - _253;
	var _267: vec3<f32> = normalize(_268);
	return _267;
}

fn env_radiance(_693_in: vec3<f32>) -> vec3<f32> {
	var _693: vec3<f32> = _693_in;
	var _697: f32 = _set0_1.params.y;
	var _696: vec2<f32> = equirect(_693, _697);
	var _694: vec2<f32>;
	_694 = _696;
	var _699: f32 = 0.000000;
	var _698: vec4<f32> = textureSampleLevel(_set0_7, _set0_12, _694, _699);
	var _695: vec4<f32>;
	_695 = _698;
	var _701: f32 = _set0_1.params.x;
	var _700: f32 = abs(_701);
	var _702: vec3<f32> = _695.xyz;
	var _703: vec3<f32> = _702 * _700;
	return _703;
}

fn equirect(_194_in: vec3<f32>, _195_in: f32) -> vec2<f32> {
	var _194: vec3<f32> = _194_in;
	var _195: f32 = _195_in;
	var _200: f32 = _194.z;
	var _201: f32 = -1.000000;
	var _202: f32 = 1.000000;
	var _199: f32 = clamp(_200, _201, _202);
	var _198: f32 = acos(_199);
	var _196: f32;
	_196 = _198;
	var _204: f32 = _194.y;
	var _205: f32 = -_204;
	var _206: f32 = _194.x;
	var _203: f32 = atan2(_205, _206);
	var _207: f32 = _203 + _20;
	var _208: f32 = _207 + _195;
	var _197: f32;
	_197 = _208;
	var _210: f32 = _197 / _21;
	var _211: f32 = _196 / _20;
	var _209: vec2<f32> = vec2<f32>(_210, _211);
	return _209;
}

fn env_pdf(_826_in: vec3<f32>) -> f32 {
	var _826: vec3<f32> = _826_in;
	var _832: f32 = 0.000000;
	var _833: f32 = _set0_1.params.w;
	var _834: bool = _833 == _832;
	if (_834)
	{
		var _837: f32 = 0.000000;
		return _837;
	}
	var _839: f32 = _set0_1.params.y;
	var _838: vec2<f32> = equirect(_826, _839);
	var _827: vec2<f32>;
	_827 = _838;
	var _843: f32 = f32(_23);
	var _842: f32 = f32(_843);
	var _845: f32 = _827.x;
var _844: f32 = fract(_845);
	var _846: f32 = _844 * _842;
	var _841: i32 = i32(_846);
	var _847: i32 = 0;
	var _848: i32 = 1;
	var _849: i32 = _23 - _848;
	var _840: i32 = clamp_int(_841, _847, _849);
	var _828: i32;
	_828 = _840;
	var _853: f32 = f32(_24);
	var _852: f32 = f32(_853);
	var _854: f32 = _827.y;
	var _855: f32 = _854 * _852;
	var _851: i32 = i32(_855);
	var _856: i32 = 0;
	var _857: i32 = 1;
	var _858: i32 = _24 - _857;
	var _850: i32 = clamp_int(_851, _856, _858);
	var _829: i32;
	_829 = _850;
	var _861: u32 = u32(_828);
	var _862: u32 = u32(_829);
	var _860: vec2<u32> = vec2<u32>(_861, _862);
	var _859: vec4<f32> = textureLoad(_set0_11, vec2<u32>(u32(_860.x), u32(_860.y)), 0);
	var _830: vec4<f32>;
	_830 = _859;
	var _864: f32 = _827.y;
	var _865: f32 = _864 * _20;
	var _863: f32 = sin(_865);
	var _831: f32;
	_831 = _863;
	var _866: f32 = 0.000001;
	var _867: bool = _831 > _866;
	if (_867)
	{
		var _870: f32 = _22 * _831;
		var _871: f32 = _830.z;
		var _872: f32 = _871 / _870;
		return _872;
	}
	var _873: f32 = 0.000000;
	return _873;
}

fn clamp_int(_704_in: i32, _705_in: i32, _706_in: i32) -> i32 {
	var _704: i32 = _704_in;
	var _705: i32 = _705_in;
	var _706: i32 = _706_in;
	var _707: bool = _704 < _705;
	if (_707)
	{
		return _705;
	}
	var _710: bool = _704 > _706;
	if (_710)
	{
		return _706;
	}
	return _704;
}

fn mis_weight(_874_in: f32, _875_in: f32) -> f32 {
	var _874: f32 = _874_in;
	var _875: f32 = _875_in;
	var _878: f32 = _874 * _874;
	var _876: f32;
	_876 = _878;
	var _879: f32 = _875 * _875;
	var _877: f32;
	_877 = _879;
	var _881: f32 = _876 + _877;
	var _882: f32 = 0.000000;
	var _880: f32 = max(_881, _882);
	var _883: f32 = _876 / _880;
	return _883;
}

fn s16_to_f32(_27_in: u32) -> vec2<f32> {
	var _27: u32 = _27_in;
	var _30: i32 = 16;
	var _32: i32 = 16;
	var _33: u32 = _27 << u32(_32);
	var _31: i32 = i32(_33);
	var _34: i32 = _31 >> u32(_30);
	var _28: i32;
	_28 = _34;
	var _35: i32 = 16;
	var _36: i32 = i32(_27);
	var _37: i32 = _36 >> u32(_35);
	var _29: i32;
	_29 = _37;
	var _38: f32 = 32767.000000;
	var _41: f32 = f32(_28);
	var _40: f32 = f32(_41);
	var _43: f32 = f32(_29);
	var _42: f32 = f32(_43);
	var _39: vec2<f32> = vec2<f32>(_40, _42);
	var _44: vec2<f32> = _39 / _38;
	return _44;
}

fn hit_attribute2d(_57_in: vec2<f32>, _58_in: vec2<f32>, _59_in: vec2<f32>, _60_in: vec2<f32>) -> vec2<f32> {
	var _57: vec2<f32> = _57_in;
	var _58: vec2<f32> = _58_in;
	var _59: vec2<f32> = _59_in;
	var _60: vec2<f32> = _60_in;
	var _61: vec2<f32> = _59 - _57;
	var _62: f32 = _60.y;
	var _63: vec2<f32> = _62 * _61;
	var _64: vec2<f32> = _58 - _57;
	var _65: f32 = _60.x;
	var _66: vec2<f32> = _65 * _64;
	var _67: vec2<f32> = _57 + _66;
	var _68: vec2<f32> = _67 + _63;
	return _68;
}

fn hit_attribute(_45_in: vec3<f32>, _46_in: vec3<f32>, _47_in: vec3<f32>, _48_in: vec2<f32>) -> vec3<f32> {
	var _45: vec3<f32> = _45_in;
	var _46: vec3<f32> = _46_in;
	var _47: vec3<f32> = _47_in;
	var _48: vec2<f32> = _48_in;
	var _49: vec3<f32> = _47 - _45;
	var _50: f32 = _48.y;
	var _51: vec3<f32> = _50 * _49;
	var _52: vec3<f32> = _46 - _45;
	var _53: f32 = _48.x;
	var _54: vec3<f32> = _53 * _52;
	var _55: vec3<f32> = _45 + _54;
	var _56: vec3<f32> = _55 + _51;
	return _56;
}

fn srgb_to_linear(_291_in: vec3<f32>) -> vec3<f32> {
	var _291: vec3<f32> = _291_in;
	var _292: f32 = 0.012523;
	var _293: f32 = 0.682171;
	var _294: f32 = 0.305306;
	var _295: vec3<f32> = _291 * _294;
	var _296: vec3<f32> = _295 + _293;
	var _297: vec3<f32> = _291 * _296;
	var _298: vec3<f32> = _297 + _292;
	var _299: vec3<f32> = _291 * _298;
	return _299;
}

fn create_uv_basis(_373_in: vec3<f32>, _374_in: vec3<f32>, _375_in: vec3<f32>, _376_in: vec2<f32>, _377_in: vec2<f32>, _378_in: vec2<f32>, _379_in: vec3<f32>) -> tangent_basis {
	var _373: vec3<f32> = _373_in;
	var _374: vec3<f32> = _374_in;
	var _375: vec3<f32> = _375_in;
	var _376: vec2<f32> = _376_in;
	var _377: vec2<f32> = _377_in;
	var _378: vec2<f32> = _378_in;
	var _379: vec3<f32> = _379_in;
	var _385: vec3<f32> = _374 - _373;
	var _380: vec3<f32>;
	_380 = _385;
	var _386: vec3<f32> = _375 - _373;
	var _381: vec3<f32>;
	_381 = _386;
	var _387: vec2<f32> = _377 - _376;
	var _382: vec2<f32>;
	_382 = _387;
	var _388: vec2<f32> = _378 - _376;
	var _383: vec2<f32>;
	_383 = _388;
	var _389: f32 = _382.y;
	var _390: f32 = _383.x;
	var _391: f32 = _390 * _389;
	var _392: f32 = _383.y;
	var _393: f32 = _382.x;
	var _394: f32 = _393 * _392;
	var _395: f32 = _394 - _391;
	var _384: f32;
	_384 = _395;
	var _396: f32 = 0.000000;
	var _397: f32 = abs(_384);
	var _398: bool = _397 > _396;
	if (_398)
	{
		var _406: f32 = 1.000000;
		var _407: f32 = _406 / _384;
		var _399: f32;
		_399 = _407;
		var _408: f32 = _382.y;
		var _409: vec3<f32> = _381 * _408;
		var _410: f32 = _383.y;
		var _411: vec3<f32> = _380 * _410;
		var _412: vec3<f32> = _411 - _409;
		var _413: vec3<f32> = _412 * _399;
		var _400: vec3<f32>;
		_400 = _413;
		var _414: f32 = _383.x;
		var _415: vec3<f32> = _380 * _414;
		var _416: f32 = _382.x;
		var _417: vec3<f32> = _381 * _416;
		var _418: vec3<f32> = _417 - _415;
		var _419: vec3<f32> = _418 * _399;
		var _401: vec3<f32>;
		_401 = _419;
		var _420: f32 = dot(_379, _400);
		var _421: vec3<f32> = _379 * _420;
		var _422: vec3<f32> = _400 - _421;
		var _402: vec3<f32>;
		_402 = _422;
		var _423: f32 = dot(_402, _402);
		var _403: f32;
		_403 = _423;
		var _424: f32 = 0.000000;
		var _425: bool = _403 > _424;
		if (_425)
		{
var _430: f32 = inverseSqrt(_403);
			var _431: vec3<f32> = _402 * _430;
			_402 = _431;
			var _432: f32 = dot(_379, _401);
			var _433: vec3<f32> = _379 * _432;
			var _434: vec3<f32> = _401 - _433;
			var _426: vec3<f32>;
			_426 = _434;
			var _435: f32 = dot(_402, _426);
			var _436: vec3<f32> = _402 * _435;
			var _437: vec3<f32> = _426 - _436;
			_426 = _437;
			var _438: f32 = dot(_426, _426);
			var _427: f32;
			_427 = _438;
			var _439: f32 = 0.000000;
			var _440: bool = _427 > _439;
			if (_440)
			{
				var _441: tangent_basis;
				_441.tangent = _402;
var _444: f32 = inverseSqrt(_427);
				var _445: vec3<f32> = _426 * _444;
				_441.binormal = _445;
				return _441;
			}
		}
	}
	var _446: tangent_basis = create_basis(_379);
	return _446;
}

fn create_basis(_212_in: vec3<f32>) -> tangent_basis {
	var _212: vec3<f32> = _212_in;
	var _217: f32 = 1.000000;
	var _213: f32;
	_213 = _217;
	var _218: f32 = 0.000000;
	var _219: f32 = _212.z;
	var _220: bool = _219 < _218;
	if (_220)
	{
		var _223: f32 = -1.000000;
		_213 = _223;
	}
	var _224: f32 = _212.z;
	var _225: f32 = _213 + _224;
	var _226: f32 = -1.000000;
	var _227: f32 = _226 / _225;
	var _214: f32;
	_214 = _227;
	var _228: f32 = _212.y;
	var _229: f32 = _212.x;
	var _230: f32 = _229 * _228;
	var _231: f32 = _230 * _214;
	var _215: f32;
	_215 = _231;
	var _216: tangent_basis;
	var _233: f32 = _212.x;
	var _234: f32 = _212.x;
	var _235: f32 = _213 * _234;
	var _236: f32 = _235 * _233;
	var _237: f32 = _236 * _214;
	var _238: f32 = 1.000000;
	var _239: f32 = _238 + _237;
	var _240: f32 = _213 * _215;
	var _241: f32 = _212.x;
	var _242: f32 = -_213;
	var _243: f32 = _242 * _241;
	var _232: vec3<f32> = vec3<f32>(_239, _240, _243);
	_216.tangent = _232;
	var _245: f32 = _212.y;
	var _246: f32 = _212.y;
	var _247: f32 = _246 * _245;
	var _248: f32 = _247 * _214;
	var _249: f32 = _213 + _248;
	var _250: f32 = _212.y;
	var _251: f32 = -_250;
	var _244: vec3<f32> = vec3<f32>(_215, _249, _251);
	_216.binormal = _244;
	return _216;
}

fn surface_albedo(_269_in: vec3<f32>, _270_in: f32) -> vec3<f32> {
	var _269: vec3<f32> = _269_in;
	var _270: f32 = _270_in;
	var _273: f32 = 0.000000;
	var _274: f32 = 0.000000;
	var _275: f32 = 0.000000;
	var _272: vec3<f32> = vec3<f32>(_273, _274, _275);
	var _276: vec3<f32> = vec3<f32>(_270, _270, _270);
var _271: vec3<f32> = mix(_269, _272, _276);
	return _271;
}

fn surface_specular(_277_in: vec3<f32>, _278_in: f32) -> vec3<f32> {
	var _277: vec3<f32> = _277_in;
	var _278: f32 = _278_in;
	var _281: f32 = 0.040000;
	var _282: f32 = 0.040000;
	var _283: f32 = 0.040000;
	var _280: vec3<f32> = vec3<f32>(_281, _282, _283);
	var _284: vec3<f32> = vec3<f32>(_278, _278, _278);
var _279: vec3<f32> = mix(_280, _277, _284);
	return _279;
}

fn spec_directional_albedo(_314_in: vec3<f32>, _315_in: f32, _316_in: f32) -> vec3<f32> {
	var _314: vec3<f32> = _314_in;
	var _315: f32 = _315_in;
	var _316: f32 = _316_in;
	var _323: f32 = -1.000000;
	var _324: f32 = -0.027500;
	var _325: f32 = -0.572000;
	var _326: f32 = 0.022000;
	var _322: vec4<f32> = vec4<f32>(_323, _324, _325, _326);
	var _317: vec4<f32>;
	_317 = _322;
	var _328: f32 = 1.000000;
	var _329: f32 = 0.042500;
	var _330: f32 = 1.040000;
	var _331: f32 = -0.040000;
	var _327: vec4<f32> = vec4<f32>(_328, _329, _330, _331);
	var _318: vec4<f32>;
	_318 = _327;
	var _332: vec4<f32> = _317 * _316;
	var _333: vec4<f32> = _332 + _318;
	var _319: vec4<f32>;
	_319 = _333;
	var _334: f32 = _319.y;
	var _335: f32 = _319.x;
	var _337: f32 = _319.x;
	var _338: f32 = _319.x;
	var _339: f32 = _338 * _337;
	var _341: f32 = -9.280000;
	var _342: f32 = _341 * _315;
	var _340: f32 = exp2(_342);
	var _336: f32 = min(_339, _340);
	var _343: f32 = _336 * _335;
	var _344: f32 = _343 + _334;
	var _320: f32;
	_320 = _344;
	var _345: vec2<f32> = _319.zw;
	var _347: f32 = -1.040000;
	var _348: f32 = 1.040000;
	var _346: vec2<f32> = vec2<f32>(_347, _348);
	var _349: vec2<f32> = _346 * _320;
	var _350: vec2<f32> = _349 + _345;
	var _321: vec2<f32>;
	_321 = _350;
	var _352: f32 = _321.y;
	var _353: f32 = _321.x;
	var _354: vec3<f32> = _314 * _353;
	var _355: vec3<f32> = _354 + _352;
	var _351: vec3<f32> = saturate(_355);
	return _351;
}

fn luma(_285_in: vec3<f32>) -> f32 {
	var _285: vec3<f32> = _285_in;
	var _288: f32 = 0.212600;
	var _289: f32 = 0.715200;
	var _290: f32 = 0.072200;
	var _287: vec3<f32> = vec3<f32>(_288, _289, _290);
	var _286: f32 = dot(_285, _287);
	return _286;
}

fn sample_env(_732_in: f32, _733_in: f32) -> vec4<f32> {
	var _732: f32 = _732_in;
	var _733: f32 = _733_in;
	var _745: f32 = f32(_25);
	var _744: f32 = f32(_745);
	var _746: f32 = _732 * _744;
	var _734: f32;
	_734 = _746;
	var _748: i32 = i32(_734);
	var _749: i32 = 0;
	var _750: i32 = 1;
	var _751: i32 = _25 - _750;
	var _747: i32 = clamp_int(_748, _749, _751);
	var _735: i32;
	_735 = _747;
	var _753: f32 = f32(_735);
	var _752: f32 = f32(_753);
	var _754: f32 = _734 - _752;
	var _736: f32;
	_736 = _754;
	var _758: i32 = 256;
	var _759: i32 = _735 % _758;
	var _757: u32 = u32(_759);
	var _761: i32 = 256;
	var _762: i32 = _735 / _761;
	var _760: u32 = u32(_762);
	var _756: vec2<u32> = vec2<u32>(_757, _760);
	var _755: vec4<f32> = textureLoad(_set0_11, vec2<u32>(u32(_756.x), u32(_756.y)), 0);
	var _737: vec4<f32>;
	_737 = _755;
	var _738: i32;
	var _763: f32 = 0.000000;
	var _739: f32;
	_739 = _763;
	var _740: f32;
	var _764: f32 = _737.x;
	var _765: bool = _733 < _764;
	if (_765)
	{
		_738 = _735;
		var _768: f32 = 0.000000;
		var _769: f32 = _737.x;
		var _770: bool = _769 > _768;
		if (_770)
		{
			var _773: f32 = _737.x;
			var _774: f32 = _733 / _773;
			_739 = _774;
		}
		var _775: f32 = _737.z;
		_740 = _775;
	}
	var _776: bool = !_765;
	if (_776)
	{
		var _781: f32 = _737.y;
		var _780: i32 = i32(_781);
		var _782: i32 = 0;
		var _783: i32 = 1;
		var _784: i32 = _25 - _783;
		var _779: i32 = clamp_int(_780, _782, _784);
		_738 = _779;
		var _785: f32 = 1.000000;
		var _786: f32 = _737.x;
		var _787: bool = _786 < _785;
		if (_787)
		{
			var _790: f32 = _737.x;
			var _791: f32 = 1.000000;
			var _792: f32 = _791 - _790;
			var _793: f32 = _737.x;
			var _794: f32 = _733 - _793;
			var _795: f32 = _794 / _792;
			_739 = _795;
		}
		var _796: f32 = _737.w;
		_740 = _796;
	}
	var _799: f32 = f32(_23);
	var _798: f32 = f32(_799);
	var _802: i32 = 256;
	var _803: i32 = _738 % _802;
	var _801: f32 = f32(_803);
	var _800: f32 = f32(_801);
	var _804: f32 = _800 + _736;
	var _805: f32 = _804 / _798;
	var _807: f32 = f32(_24);
	var _806: f32 = f32(_807);
	var _810: i32 = 256;
	var _811: i32 = _738 / _810;
	var _809: f32 = f32(_811);
	var _808: f32 = f32(_809);
	var _812: f32 = _808 + _739;
	var _813: f32 = _812 / _806;
	var _797: vec2<f32> = vec2<f32>(_805, _813);
	var _741: vec2<f32>;
	_741 = _797;
	var _815: f32 = _741.y;
	var _816: f32 = _815 * _20;
	var _814: f32 = sin(_816);
	var _742: f32;
	_742 = _814;
	var _817: f32 = 0.000000;
	var _743: f32;
	_743 = _817;
	var _818: f32 = 0.000001;
	var _819: bool = _742 > _818;
	if (_819)
	{
		var _822: f32 = _22 * _742;
		var _823: f32 = _740 / _822;
		_743 = _823;
	}
	var _825: vec3<f32> = env_dir(_741);
	var _824: vec4<f32> = vec4<f32>(_825, _743);
	return _824;
}

fn env_dir(_713_in: vec2<f32>) -> vec3<f32> {
	var _713: vec2<f32> = _713_in;
	var _717: f32 = _713.y;
	var _718: f32 = _717 * _20;
	var _714: f32;
	_714 = _718;
	var _719: f32 = _set0_1.params.y;
	var _720: f32 = _713.x;
	var _721: f32 = _720 * _21;
	var _722: f32 = _721 - _20;
	var _723: f32 = _722 - _719;
	var _715: f32;
	_715 = _723;
	var _724: f32 = sin(_714);
	var _716: f32;
	_716 = _724;
	var _726: f32 = cos(_715);
	var _727: f32 = _716 * _726;
	var _728: f32 = sin(_715);
	var _729: f32 = -_716;
	var _730: f32 = _729 * _728;
	var _731: f32 = cos(_714);
	var _725: vec3<f32> = vec3<f32>(_727, _730, _731);
	return _725;
}

fn bsdf_eval(_884_in: vec3<f32>, _885_in: vec3<f32>, _886_in: vec3<f32>, _887_in: f32, _888_in: vec3<f32>, _889_in: vec3<f32>, _890_in: f32, _891_in: f32) -> vec4<f32> {
	var _884: vec3<f32> = _884_in;
	var _885: vec3<f32> = _885_in;
	var _886: vec3<f32> = _886_in;
	var _887: f32 = _887_in;
	var _888: vec3<f32> = _888_in;
	var _889: vec3<f32> = _889_in;
	var _890: f32 = _890_in;
	var _891: f32 = _891_in;
	var _906: f32 = dot(_885, _884);
	var _892: f32;
	_892 = _906;
	var _907: f32 = 0.000000;
	var _908: bool = _892 <= _907;
	if (_908)
	{
		var _912: f32 = 0.000000;
		var _913: f32 = 0.000000;
		var _914: f32 = 0.000000;
		var _915: f32 = 0.000000;
		var _911: vec4<f32> = vec4<f32>(_912, _913, _914, _915);
		return _911;
	}
	var _917: vec3<f32> = _886 + _884;
	var _916: vec3<f32> = normalize(_917);
	var _893: vec3<f32>;
	_893 = _916;
	var _919: f32 = dot(_885, _893);
	var _920: f32 = 0.000000;
	var _918: f32 = max(_919, _920);
	var _894: f32;
	_894 = _918;
	var _922: f32 = dot(_886, _893);
	var _923: f32 = 0.000000;
	var _921: f32 = max(_922, _923);
	var _895: f32;
	_895 = _921;
	var _924: f32 = _890 * _890;
	var _896: f32;
	_896 = _924;
	var _925: f32 = 1.000000;
	var _926: f32 = 1.000000;
	var _927: f32 = _896 - _926;
	var _928: f32 = _894 * _894;
	var _929: f32 = _928 * _927;
	var _930: f32 = _929 + _925;
	var _897: f32;
	_897 = _930;
	var _932: f32 = _20 * _897;
	var _933: f32 = _932 * _897;
	var _934: f32 = 0.000000;
	var _931: f32 = max(_933, _934);
	var _935: f32 = _896 / _931;
	var _898: f32;
	_898 = _935;
	var _936: f32 = smith_lambda(_887, _896);
	var _899: f32;
	_899 = _936;
	var _937: f32 = smith_lambda(_892, _896);
	var _900: f32;
	_900 = _937;
	var _938: vec3<f32> = f_schlick(_889, _895);
	var _901: vec3<f32>;
	_901 = _938;
	var _940: f32 = 4.000000;
	var _941: f32 = _940 * _887;
	var _942: f32 = _941 * _892;
	var _943: f32 = 0.000000;
	var _939: f32 = max(_942, _943);
	var _944: f32 = 1.000000;
	var _945: f32 = _944 + _899;
	var _946: f32 = _945 + _900;
	var _947: f32 = _946 * _939;
	var _948: f32 = _898 / _947;
	var _949: vec3<f32> = _901 * _948;
	var _902: vec3<f32>;
	_902 = _949;
	var _950: vec3<f32> = _888 / _20;
	var _903: vec3<f32>;
	_903 = _950;
	var _952: f32 = 4.000000;
	var _953: f32 = _952 * _887;
	var _954: f32 = 0.000000;
	var _951: f32 = max(_953, _954);
	var _955: f32 = 1.000000;
	var _956: f32 = _955 + _899;
	var _957: f32 = _956 * _951;
	var _958: f32 = _898 / _957;
	var _904: f32;
	_904 = _958;
	var _959: f32 = _892 / _20;
	var _905: f32;
	_905 = _959;
	var _961: vec3<f32> = _903 + _902;
	var _962: vec3<f32> = _961 * _892;
	var _963: f32 = 1.000000;
	var _964: f32 = _963 - _891;
	var _965: f32 = _964 * _905;
	var _966: f32 = _891 * _904;
	var _967: f32 = _966 + _965;
	var _960: vec4<f32> = vec4<f32>(_962, _967);
	return _960;
}

fn smith_lambda(_356_in: f32, _357_in: f32) -> f32 {
	var _356: f32 = _356_in;
	var _357: f32 = _357_in;
	var _359: f32 = _356 * _356;
	var _358: f32;
	_358 = _359;
	var _360: f32 = 1.000000;
	var _363: f32 = 0.000000;
	var _362: f32 = max(_358, _363);
	var _364: f32 = 1.000000;
	var _365: f32 = _364 - _358;
	var _366: f32 = _357 * _365;
	var _367: f32 = _366 / _362;
	var _368: f32 = 1.000000;
	var _369: f32 = _368 + _367;
	var _361: f32 = sqrt(_369);
	var _370: f32 = _361 - _360;
	var _371: f32 = 0.500000;
	var _372: f32 = _371 * _370;
	return _372;
}

fn f_schlick(_300_in: vec3<f32>, _301_in: f32) -> vec3<f32> {
	var _300: vec3<f32> = _300_in;
	var _301: f32 = _301_in;
	var _305: f32 = 1.000000;
	var _306: f32 = _305 - _301;
	var _304: f32 = saturate(_306);
	var _302: f32;
	_302 = _304;
	var _307: f32 = _302 * _302;
	var _303: f32;
	_303 = _307;
	var _308: f32 = _303 * _303;
	var _309: f32 = _308 * _302;
	var _310: f32 = 1.000000;
	var _311: vec3<f32> = _310 - _300;
	var _312: vec3<f32> = _311 * _309;
	var _313: vec3<f32> = _300 + _312;
	return _313;
}

fn offset_ray(_558_in: vec3<f32>, _559_in: vec3<f32>, _560_in: vec3<f32>) -> vec3<f32> {
	var _558: vec3<f32> = _558_in;
	var _559: vec3<f32> = _559_in;
	var _560: vec3<f32> = _560_in;
	var _561: f32 = 0.000000;
	var _562: f32 = dot(_560, _559);
	var _563: bool = _562 < _561;
	if (_563)
	{
		var _566: f32 = 0.000100;
		var _567: vec3<f32> = _559 * _566;
		var _568: vec3<f32> = _558 - _567;
		return _568;
	}
	var _569: f32 = 0.000100;
	var _570: vec3<f32> = _559 * _569;
	var _571: vec3<f32> = _558 + _570;
	return _571;
}

fn occluded(_968_in: vec3<f32>, _969_in: vec3<f32>) -> bool {
	var _968: vec3<f32> = _968_in;
	var _969: vec3<f32> = _969_in;
	var _970: _kong_ray;
	_970.origin = _968;
	_970.direction = _969;
	var _972: f32 = 0.000100;
	_970.min = _972;
	var _973: f32 = 100.000000;
	_970.max = _973;
	var _971: _kong_ray_query;
	_971 = _kong_trace(_970, true);
	var _975: bool = _971.hit;
	return _975;
}

fn sample_ggx_vndf(_470_in: vec3<f32>, _471_in: f32, _472_in: f32, _473_in: f32) -> vec3<f32> {
	var _470: vec3<f32> = _470_in;
	var _471: f32 = _471_in;
	var _472: f32 = _472_in;
	var _473: f32 = _473_in;
	var _486: f32 = _470.x;
	var _487: f32 = _471 * _486;
	var _488: f32 = _470.y;
	var _489: f32 = _471 * _488;
	var _490: f32 = _470.z;
	var _485: vec3<f32> = vec3<f32>(_487, _489, _490);
	var _484: vec3<f32> = normalize(_485);
	var _474: vec3<f32>;
	_474 = _484;
	var _491: f32 = _474.y;
	var _492: f32 = _474.y;
	var _493: f32 = _492 * _491;
	var _494: f32 = _474.x;
	var _495: f32 = _474.x;
	var _496: f32 = _495 * _494;
	var _497: f32 = _496 + _493;
	var _475: f32;
	_475 = _497;
	var _499: f32 = 1.000000;
	var _500: f32 = 0.000000;
	var _501: f32 = 0.000000;
	var _498: vec3<f32> = vec3<f32>(_499, _500, _501);
	var _476: vec3<f32>;
	_476 = _498;
	var _502: f32 = 0.000000;
	var _503: bool = _475 > _502;
	if (_503)
	{
var _506: f32 = inverseSqrt(_475);
		var _508: f32 = _474.y;
		var _509: f32 = -_508;
		var _510: f32 = _474.x;
		var _511: f32 = 0.000000;
		var _507: vec3<f32> = vec3<f32>(_509, _510, _511);
		var _512: vec3<f32> = _507 * _506;
		_476 = _512;
	}
	var _513: vec3<f32> = cross(_474, _476);
	var _477: vec3<f32>;
	_477 = _513;
	var _514: f32 = sqrt(_472);
	var _478: f32;
	_478 = _514;
	var _515: f32 = _21 * _473;
	var _479: f32;
	_479 = _515;
	var _516: f32 = cos(_479);
	var _517: f32 = _478 * _516;
	var _480: f32;
	_480 = _517;
	var _518: f32 = sin(_479);
	var _519: f32 = _478 * _518;
	var _481: f32;
	_481 = _519;
	var _520: f32 = _474.z;
	var _521: f32 = 1.000000;
	var _522: f32 = _521 + _520;
	var _523: f32 = 0.500000;
	var _524: f32 = _523 * _522;
	var _482: f32;
	_482 = _524;
	var _525: f32 = _482 * _481;
	var _528: f32 = 0.000000;
	var _529: f32 = _480 * _480;
	var _530: f32 = 1.000000;
	var _531: f32 = _530 - _529;
	var _527: f32 = max(_528, _531);
	var _526: f32 = sqrt(_527);
	var _532: f32 = 1.000000;
	var _533: f32 = _532 - _482;
	var _534: f32 = _533 * _526;
	var _535: f32 = _534 + _525;
	_481 = _535;
	var _538: f32 = 0.000000;
	var _539: f32 = _481 * _481;
	var _540: f32 = _480 * _480;
	var _541: f32 = 1.000000;
	var _542: f32 = _541 - _540;
	var _543: f32 = _542 - _539;
	var _537: f32 = max(_538, _543);
	var _536: f32 = sqrt(_537);
	var _544: vec3<f32> = _536 * _474;
	var _545: vec3<f32> = _481 * _477;
	var _546: vec3<f32> = _480 * _476;
	var _547: vec3<f32> = _546 + _545;
	var _548: vec3<f32> = _547 + _544;
	var _483: vec3<f32>;
	_483 = _548;
	var _551: f32 = _483.x;
	var _552: f32 = _471 * _551;
	var _553: f32 = _483.y;
	var _554: f32 = _471 * _553;
	var _556: f32 = 0.000001;
	var _557: f32 = _483.z;
	var _555: f32 = max(_556, _557);
	var _550: vec3<f32> = vec3<f32>(_552, _554, _555);
	var _549: vec3<f32> = normalize(_550);
	return _549;
}

fn cos_weighted_direction(_447_in: vec3<f32>, _448_in: vec3<f32>, _449_in: vec3<f32>, _450_in: f32, _451_in: f32) -> vec3<f32> {
	var _447: vec3<f32> = _447_in;
	var _448: vec3<f32> = _448_in;
	var _449: vec3<f32> = _449_in;
	var _450: f32 = _450_in;
	var _451: f32 = _451_in;
	var _454: f32 = sqrt(_450);
	var _452: f32;
	_452 = _454;
	var _455: f32 = _21 * _451;
	var _453: f32;
	_453 = _455;
	var _458: f32 = 0.000000;
	var _459: f32 = 1.000000;
	var _460: f32 = _459 - _450;
	var _457: f32 = max(_458, _460);
	var _456: f32 = sqrt(_457);
	var _461: vec3<f32> = _449 * _456;
	var _462: f32 = sin(_453);
	var _463: f32 = _452 * _462;
	var _464: vec3<f32> = _448 * _463;
	var _465: f32 = cos(_453);
	var _466: f32 = _452 * _465;
	var _467: vec3<f32> = _447 * _466;
	var _468: vec3<f32> = _467 + _464;
	var _469: vec3<f32> = _468 + _461;
	return _469;
}

