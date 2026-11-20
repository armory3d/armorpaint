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

const _13: i32 = 8;

const _14: i32 = 32;

const _15: i32 = 2;

const _16: i32 = 0;

const _17: i32 = 1;

const _18: i32 = 2;

const _19: i32 = 3;

const _20: i32 = 5;

const _21: i32 = 6;

const _22: i32 = 8;

const _23: i32 = 2;

const _24: f32 = 0.500000;

const _25: f32 = 0.050000;

const _26: f32 = 3.14159274;

const _27: f32 = 6.28318548;

const _28: f32 = 19.739208;

const _29: i32 = 256;

const _30: i32 = 128;

const _31: i32 = 32768;

const _32: i32 = 16;

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
	var _1126: vec3<u32> = _kong_dispatch_thread_id;
	var _1118: vec3<u32>;
	_1118 = _1126;
	var _1127: vec2<u32> = textureDimensions(_set0_3);
	var _1119: vec2<u32>;
	_1119 = _1127;
	var _1128: u32 = _1119.y;
	var _1129: u32 = _1118.y;
	var _1130: bool = _1129 >= _1128;
	var _1131: u32 = _1119.x;
	var _1132: u32 = _1118.x;
	var _1133: bool = _1132 >= _1131;
	var _1134: bool = _1133 || _1130;
	if (_1134)
	{
		return;
	}
	var _1138: f32 = _set0_1.eye.w;
	var _1137: i32 = i32(_1138);
	var _1120: i32;
	_1120 = _1137;
	var _1140: vec2<u32> = _1118.xy;
	var _1139: sampler_cache = init_sampler(_1140, _1120);
	var _1121: sampler_cache;
	_1121 = _1139;
	var _1142: f32 = 0.000000;
	var _1143: f32 = 0.000000;
	var _1144: f32 = 0.000000;
	var _1141: vec3<f32> = vec3<f32>(_1142, _1143, _1144);
	var _1122: vec3<f32>;
	_1122 = _1141;
	{
		var _1148: i32 = 0;
		var _1145: i32;
		_1145 = _1148;
		while (true)
{
		var _1152: bool = _1145 < _13;
		if (!_1152) { break; }
		{
			var _1164: i32 = _1120 * _13;
			var _1165: i32 = _1164 + _1145;
			var _1153: i32;
			_1153 = _1165;
			var _1167: vec2<u32> = _1118.xy;
			var _1166: vec2<f32> = vec2<f32>(_1167);
			var _1154: vec2<f32>;
			_1154 = _1166;
			var _1169: vec2<u32> = _1118.xy;
			var _1170: i32 = 0;
			var _1168: f32 = rnd(_1169, _1153, _1170, _1121, _1120);
			_1154.x += _1168;
			var _1172: vec2<u32> = _1118.xy;
			var _1173: i32 = 1;
			var _1171: f32 = rnd(_1172, _1153, _1173, _1121, _1120);
			_1154.y += _1171;
			var _1155: _kong_ray;
			var _1174: f32 = 0.000100;
			_1155.min = _1174;
			var _1175: f32 = 100.000000;
			_1155.max = _1175;
			var _1176: vec3<f32> = _set0_1.eye.xyz;
			_1155.origin = _1176;
			var _1178: f32 = 1.000000;
			var _1179: f32 = 2.000000;
			var _1180: vec2<f32> = vec2<f32>(_1119);
			var _1181: vec2<f32> = _1154 / _1180;
			var _1182: vec2<f32> = _1181 * _1179;
			var _1183: vec2<f32> = _1182 - _1178;
			var _1184: vec3<f32> = _set0_1.eye.xyz;
			var _1177: vec3<f32> = camera_ray_direction(_1183, _1184);
			_1155.direction = _1177;
			var _1186: f32 = 1.000000;
			var _1187: f32 = 1.000000;
			var _1188: f32 = 1.000000;
			var _1185: vec3<f32> = vec3<f32>(_1186, _1187, _1188);
			var _1156: vec3<f32>;
			_1156 = _1185;
			var _1189: f32 = -1.000000;
			var _1157: f32;
			_1157 = _1189;
			var _1190: i32 = 0;
			var _1158: i32;
			_1158 = _1190;
			var _1191: i32 = 0;
			var _1159: i32;
			_1159 = _1191;
			var _1192: f32 = 0.000000;
			var _1160: f32;
			_1160 = _1192;
			var _1194: f32 = 1.000000;
			var _1195: f32 = 1.000000;
			var _1196: f32 = 1.000000;
			var _1193: vec3<f32> = vec3<f32>(_1194, _1195, _1196);
			var _1161: vec3<f32>;
			_1161 = _1193;
			{
				var _1200: i32 = 0;
				var _1197: i32;
				_1197 = _1200;
				while (true)
{
				var _1204: bool = _1197 < _14;
				if (!_1204) { break; }
				{
					var _1260: i32 = _1197 * _22;
					var _1261: i32 = _15 + _1260;
					var _1205: i32;
					_1205 = _1261;
					var _1206: _kong_ray_query;
					_1206 = _kong_trace(_1155, false);
					var _1263: bool = _1206.hit;
					var _1264: bool = !_1263;
					if (_1264)
					{
						var _1265: vec3<f32>;
						var _1269: f32 = 1.000000;
						var _1266: f32;
						_1266 = _1269;
						var _1270: f32 = 0.000000;
						var _1271: f32 = _set0_1.params.x;
						var _1272: bool = _1271 < _1270;
						var _1273: i32 = 0;
						var _1274: bool = _1197 == _1273;
						var _1275: bool = _1274 && _1272;
						if (_1275)
						{
							var _1279: f32 = 0.027500;
							var _1280: f32 = 0.027500;
							var _1281: f32 = 0.027500;
							var _1278: vec3<f32> = vec3<f32>(_1279, _1280, _1281);
							_1265 = _1278;
						}
						var _1282: bool = !_1275;
						if (_1282)
						{
							var _1286: vec3<f32> = _1155.direction;
							var _1285: vec3<f32> = env_radiance(_1286);
							_1265 = _1285;
							var _1287: f32 = 0.000000;
							var _1288: bool = _1157 > _1287;
							if (_1288)
							{
								var _1293: vec3<f32> = _1155.direction;
								var _1292: f32 = env_pdf(_1293);
								var _1291: f32 = mis_weight(_1157, _1292);
								_1266 = _1291;
							}
						}
						var _1295: vec3<f32> = _1156 * _1265;
						var _1296: vec3<f32> = _1295 * _1266;
						var _1297: f32 = 0.000000;
						var _1298: vec3<f32> = vec3<f32>(_1297, _1297, _1297);
						var _1299: f32 = 8.000000;
						var _1300: vec3<f32> = vec3<f32>(_1299, _1299, _1299);
						var _1294: vec3<f32> = clamp(_1296, _1298, _1300);
						_1122 += _1294;
						break;
					}
					var _1301: u32 = _kong_instances[_1206.instance].geometry;
					var _1207: u32;
					_1207 = _1301;
					var _1302: vec2<f32> = _1206.barycentrics;
					var _1208: vec2<f32>;
					_1208 = _1302;
					var _1304: i32 = 0;
					var _1303: vec4<u32> = _kong_vertices[_kong_indices[_1206.primitive * 3u + u32(_1304)]];
					var _1209: vec4<u32>;
					_1209 = _1303;
					var _1306: i32 = 1;
					var _1305: vec4<u32> = _kong_vertices[_kong_indices[_1206.primitive * 3u + u32(_1306)]];
					var _1210: vec4<u32>;
					_1210 = _1305;
					var _1308: i32 = 2;
					var _1307: vec4<u32> = _kong_vertices[_kong_indices[_1206.primitive * 3u + u32(_1308)]];
					var _1211: vec4<u32>;
					_1211 = _1307;
					var _1310: u32 = _1209.w;
					var _1309: vec2<f32> = s16_to_f32(_1310);
					var _1212: vec2<f32>;
					_1212 = _1309;
					var _1312: u32 = _1210.w;
					var _1311: vec2<f32> = s16_to_f32(_1312);
					var _1213: vec2<f32>;
					_1213 = _1311;
					var _1314: u32 = _1211.w;
					var _1313: vec2<f32> = s16_to_f32(_1314);
					var _1214: vec2<f32>;
					_1214 = _1313;
					var _1315: f32 = _set0_1.params.z;
					var _1316: vec2<f32> = hit_attribute2d(_1212, _1213, _1214, _1208);
					var _1317: vec2<f32> = _1316 * _1315;
					var _1215: vec2<f32>;
					_1215 = _1317;
					var _1318: vec2<u32> = textureDimensions(_kong_geometry_texture0);
					var _1216: vec2<u32>;
					_1216 = _1318;
					var _1320: vec2<f32> = vec2<f32>(_1216);
var _1321: vec2<f32> = fract(_1215);
					var _1322: vec2<f32> = _1321 * _1320;
					var _1319: vec2<u32> = vec2<u32>(_1322);
					var _1217: vec2<u32>;
					_1217 = _1319;
					var _1323: vec4<f32> = textureLoad(_kong_geometry_texture0, _1217, 0);
					var _1218: vec4<f32>;
					_1218 = _1323;
					var _1324: f32 = _1206.t;
					var _1219: f32;
					_1219 = _1324;
					var _1325: vec3<f32> = _1155.direction;
					var _1326: vec3<f32> = _1325 * _1219;
					var _1327: vec3<f32> = _1155.origin;
					var _1328: vec3<f32> = _1327 + _1326;
					var _1220: vec3<f32>;
					_1220 = _1328;
					var _1329: i32 = 0;
					var _1330: bool = _1159 > _1329;
					if (_1330)
					{
						var _1336: f32 = f32(_1159);
						var _1335: f32 = f32(_1336);
						var _1337: f32 = _1160 * _1335;
						var _1331: f32;
						_1331 = _1337;
						var _1338: f32 = 1.000000;
						var _1339: f32 = _1219 + _1338;
						var _1332: f32;
						_1332 = _1339;
						var _1340: f32 = 0.000000;
						var _1341: bool = _1331 > _1340;
						if (_1341)
						{
							var _1347: vec2<u32> = _1118.xy;
							var _1348: i32 = _1205 + _20;
							var _1346: f32 = rnd(_1347, _1153, _1348, _1121, _1120);
							var _1349: f32 = 1.000000;
							var _1350: f32 = _1349 - _1346;
							var _1351: f32 = 0.000000;
							var _1345: f32 = max(_1350, _1351);
							var _1344: f32 = log(_1345);
							var _1352: f32 = -_1344;
							var _1353: f32 = _1352 / _1331;
							_1332 = _1353;
						}
						var _1354: bool = _1332 < _1219;
						if (_1354)
						{
							var _1360: f32 = 0.001000;
							var _1361: f32 = 0.001000;
							var _1362: f32 = 0.001000;
							var _1359: vec3<f32> = vec3<f32>(_1360, _1361, _1362);
							var _1358: vec3<f32> = max(_1161, _1359);
							var _1363: vec3<f32> = vec3<f32>(_1332, _1332, _1332);
							var _1357: vec3<f32> = pow(_1358, _1363);
							_1156 *= _1357;
							var _1364: bool = _1158 >= _23;
							if (_1364)
							{
								var _1371: f32 = _1156.x;
								var _1372: f32 = _1156.y;
								var _1370: f32 = max(_1371, _1372);
								var _1373: f32 = _1156.z;
								var _1369: f32 = max(_1370, _1373);
								var _1368: f32 = clamp(_1369, _25, _24);
								var _1365: f32;
								_1365 = _1368;
								var _1375: vec2<u32> = _1118.xy;
								var _1376: i32 = _1205 + _16;
								var _1374: f32 = rnd(_1375, _1153, _1376, _1121, _1120);
								var _1377: bool = _1374 > _1365;
								if (_1377)
								{
									break;
								}
								var _1380: vec3<f32> = _1156 / _1365;
								_1156 = _1380;
							}
							var _1381: i32 = 1;
							_1158 += _1381;
							var _1382: vec3<f32> = _1155.direction;
							var _1383: vec3<f32> = _1382 * _1332;
							var _1384: vec3<f32> = _1155.origin;
							var _1385: vec3<f32> = _1384 + _1383;
							_1155.origin = _1385;
							var _1388: vec2<u32> = _1118.xy;
							var _1389: i32 = 1;
							var _1390: i32 = _1205 + _20;
							var _1391: i32 = _1390 + _1389;
							var _1387: f32 = rnd(_1388, _1153, _1391, _1121, _1120);
							var _1393: vec2<u32> = _1118.xy;
							var _1394: i32 = 2;
							var _1395: i32 = _1205 + _20;
							var _1396: i32 = _1395 + _1394;
							var _1392: f32 = rnd(_1393, _1153, _1396, _1121, _1120);
							var _1386: vec3<f32> = uniform_sphere(_1387, _1392);
							_1155.direction = _1386;
							var _1397: f32 = -1.000000;
							_1157 = _1397;
							var _1398: i32 = 1;
							_1197 += _1398;
							continue;
						}
						var _1402: f32 = 0.001000;
						var _1403: f32 = 0.001000;
						var _1404: f32 = 0.001000;
						var _1401: vec3<f32> = vec3<f32>(_1402, _1403, _1404);
						var _1400: vec3<f32> = max(_1161, _1401);
						var _1405: vec3<f32> = vec3<f32>(_1219, _1219, _1219);
						var _1399: vec3<f32> = pow(_1400, _1405);
						_1156 *= _1399;
					}
					var _1407: u32 = _1209.y;
					var _1406: vec2<f32> = s16_to_f32(_1407);
					var _1221: vec2<f32>;
					_1221 = _1406;
					var _1409: u32 = _1210.y;
					var _1408: vec2<f32> = s16_to_f32(_1409);
					var _1222: vec2<f32>;
					_1222 = _1408;
					var _1411: u32 = _1211.y;
					var _1410: vec2<f32> = s16_to_f32(_1411);
					var _1223: vec2<f32>;
					_1223 = _1410;
					var _1414: u32 = _1209.x;
					var _1413: vec2<f32> = s16_to_f32(_1414);
					var _1415: f32 = _1221.x;
					var _1412: vec3<f32> = vec3<f32>(_1413, _1415);
					var _1224: vec3<f32>;
					_1224 = _1412;
					var _1418: u32 = _1210.x;
					var _1417: vec2<f32> = s16_to_f32(_1418);
					var _1419: f32 = _1222.x;
					var _1416: vec3<f32> = vec3<f32>(_1417, _1419);
					var _1225: vec3<f32>;
					_1225 = _1416;
					var _1422: u32 = _1211.x;
					var _1421: vec2<f32> = s16_to_f32(_1422);
					var _1423: f32 = _1223.x;
					var _1420: vec3<f32> = vec3<f32>(_1421, _1423);
					var _1226: vec3<f32>;
					_1226 = _1420;
					var _1426: u32 = _1209.z;
					var _1425: vec2<f32> = s16_to_f32(_1426);
					var _1427: f32 = _1221.y;
					var _1424: vec3<f32> = vec3<f32>(_1425, _1427);
					var _1227: vec3<f32>;
					_1227 = _1424;
					var _1430: u32 = _1210.z;
					var _1429: vec2<f32> = s16_to_f32(_1430);
					var _1431: f32 = _1222.y;
					var _1428: vec3<f32> = vec3<f32>(_1429, _1431);
					var _1228: vec3<f32>;
					_1228 = _1428;
					var _1434: u32 = _1211.z;
					var _1433: vec2<f32> = s16_to_f32(_1434);
					var _1435: f32 = _1223.y;
					var _1432: vec3<f32> = vec3<f32>(_1433, _1435);
					var _1229: vec3<f32>;
					_1229 = _1432;
					var _1437: vec3<f32> = hit_attribute(_1227, _1228, _1229, _1208);
					var _1436: vec3<f32> = normalize(_1437);
					var _1230: vec3<f32>;
					_1230 = _1436;
					var _1439: vec3<f32> = _1225 - _1224;
					var _1440: vec3<f32> = _1226 - _1224;
					var _1438: vec3<f32> = cross(_1439, _1440);
					var _1231: vec3<f32>;
					_1231 = _1438;
					var _1441: f32 = 0.000000;
					var _1442: f32 = dot(_1231, _1231);
					var _1443: bool = _1442 > _1441;
					if (_1443)
					{
						var _1446: vec3<f32> = normalize(_1231);
						_1231 = _1446;
					}
					var _1447: bool = !_1443;
					if (_1447)
					{
						_1231 = _1230;
					}
					var _1450: f32 = 0.000000;
					var _1451: f32 = dot(_1231, _1230);
					var _1452: bool = _1451 < _1450;
					if (_1452)
					{
						var _1455: vec3<f32> = -_1231;
						_1231 = _1455;
					}
					var _1232: vec3<f32>;
					_1232 = _1230;
					var _1456: mat3x3<f32> = _kong_instances[_1206.instance].object_to_world;
					var _1233: mat3x3<f32>;
					_1233 = _1456;
					var _1458: vec3<f32> = _1233 * _1230;
					var _1457: vec3<f32> = normalize(_1458);
					_1230 = _1457;
					var _1460: vec3<f32> = _1233 * _1231;
					var _1459: vec3<f32> = normalize(_1460);
					_1231 = _1459;
					var _1461: f32 = 0.000000;
					var _1463: vec3<f32> = _1155.direction;
					var _1462: f32 = dot(_1231, _1463);
					var _1464: bool = _1462 > _1461;
					var _1234: bool;
					_1234 = _1464;
					if (_1234)
					{
						var _1467: vec3<f32> = -_1231;
						_1231 = _1467;
						var _1468: vec3<f32> = -_1230;
						_1230 = _1468;
					}
					var _1469: bool = true;
					var _1235: bool;
					_1235 = _1469;
					var _1471: f32 = 0.000000;
					var _1472: f32 = 0.000000;
					var _1473: f32 = 0.000000;
					var _1474: f32 = 0.000000;
					var _1470: vec4<f32> = vec4<f32>(_1471, _1472, _1473, _1474);
					var _1236: vec4<f32>;
					_1236 = _1470;
					if (_1235)
					{
						var _1477: vec4<f32> = textureLoad(_kong_geometry_texture1, _1217, 0);
						_1236 = _1477;
					}
					var _1479: vec3<f32> = _1218.xyz;
					var _1478: vec3<f32> = srgb_to_linear(_1479);
					var _1237: vec3<f32>;
					_1237 = _1478;
					var _1480: i32 = 2;
					var _1481: i32 = 3;
					var _1483: f32 = 255.000000;
					var _1484: f32 = _1236.w;
					var _1485: f32 = _1484 * _1483;
					var _1482: i32 = i32(_1485);
					var _1486: i32 = _1482 % _1481;
					var _1487: bool = _1486 == _1480;
					var _1238: bool;
					_1238 = _1487;
					var _1488: bool = _1206.front_face;
					var _1489: bool = !_1488;
					var _1490: bool = !_1238;
					var _1491: bool = _1490 && _1489;
					if (_1491)
					{
						var _1496: f32 = 0.001000;
						var _1497: vec3<f32> = vec3<f32>(_1496, _1496, _1496);
						var _1495: vec3<f32> = max(_1237, _1497);
						var _1498: f32 = _1218.w;
						var _1499: f32 = _1219 * _1498;
						var _1500: vec3<f32> = vec3<f32>(_1499, _1499, _1499);
						var _1494: vec3<f32> = pow(_1495, _1500);
						_1156 *= _1494;
					}
					var _1501: i32 = 1;
					var _1502: i32 = 3;
					var _1504: f32 = 255.000000;
					var _1505: f32 = _1236.w;
					var _1506: f32 = _1505 * _1504;
					var _1503: i32 = i32(_1506);
					var _1507: i32 = _1503 % _1502;
					var _1508: bool = _1507 == _1501;
					if (_1508)
					{
						var _1511: f32 = 100.000000;
						var _1512: vec3<f32> = _1156 * _1237;
						var _1513: vec3<f32> = _1512 * _1511;
						_1122 += _1513;
						break;
					}
					var _1514: vec4<f32> = textureLoad(_kong_geometry_texture2, _1217, 0);
					var _1239: vec4<f32>;
					_1239 = _1514;
					if (_1238)
					{
						var _1521: f32 = _1239.z;
						var _1522: f32 = 1.000000;
						var _1523: f32 = _1522 + _1521;
						var _1515: f32;
						_1515 = _1523;
						var _1524: bool = !_1234;
						var _1516: bool;
						_1516 = _1524;
						var _1525: bool = true;
						var _1517: bool;
						_1517 = _1525;
						var _1526: vec3<f32> = _1155.direction;
						var _1518: vec3<f32>;
						_1518 = _1526;
						var _1527: f32 = 1.001000;
						var _1528: bool = _1515 > _1527;
						if (_1528)
						{
							var _1540: vec3<f32> = _1155.direction;
							var _1541: vec3<f32> = -_1540;
							var _1529: vec3<f32>;
							_1529 = _1541;
							var _1542: tangent_basis = create_basis(_1230);
							var _1530: tangent_basis;
							_1530 = _1542;
							var _1544: f32 = _1239.y;
							var _1545: f32 = _1239.y;
							var _1546: f32 = _1545 * _1544;
							var _1547: f32 = 0.001000;
							var _1543: f32 = max(_1546, _1547);
							var _1531: f32;
							_1531 = _1543;
							var _1550: vec3<f32> = _1530.tangent;
							var _1549: f32 = dot(_1529, _1550);
							var _1552: vec3<f32> = _1530.binormal;
							var _1551: f32 = dot(_1529, _1552);
							var _1554: f32 = dot(_1529, _1230);
							var _1555: f32 = 0.001000;
							var _1553: f32 = max(_1554, _1555);
							var _1548: vec3<f32> = vec3<f32>(_1549, _1551, _1553);
							var _1532: vec3<f32>;
							_1532 = _1548;
							var _1558: vec2<u32> = _1118.xy;
							var _1559: i32 = _1205 + _19;
							var _1557: f32 = rnd(_1558, _1153, _1559, _1121, _1120);
							var _1561: vec2<u32> = _1118.xy;
							var _1562: i32 = 1;
							var _1563: i32 = _1205 + _19;
							var _1564: i32 = _1563 + _1562;
							var _1560: f32 = rnd(_1561, _1153, _1564, _1121, _1120);
							var _1556: vec3<f32> = sample_ggx_vndf(_1532, _1531, _1557, _1560);
							var _1533: vec3<f32>;
							_1533 = _1556;
							var _1566: f32 = _1533.z;
							var _1567: vec3<f32> = _1230 * _1566;
							var _1568: f32 = _1533.y;
							var _1569: vec3<f32> = _1530.binormal;
							var _1570: vec3<f32> = _1569 * _1568;
							var _1571: f32 = _1533.x;
							var _1572: vec3<f32> = _1530.tangent;
							var _1573: vec3<f32> = _1572 * _1571;
							var _1574: vec3<f32> = _1573 + _1570;
							var _1575: vec3<f32> = _1574 + _1567;
							var _1565: vec3<f32> = normalize(_1575);
							var _1534: vec3<f32>;
							_1534 = _1565;
							var _1576: f32 = 1.000000;
							var _1577: f32 = _1576 / _1515;
							var _1535: f32;
							_1535 = _1577;
							var _1578: bool = !_1516;
							if (_1578)
							{
								_1535 = _1515;
							}
							var _1582: f32 = dot(_1529, _1534);
							var _1583: f32 = 0.000000;
							var _1581: f32 = max(_1582, _1583);
							var _1536: f32;
							_1536 = _1581;
							var _1585: vec3<f32> = _1155.direction;
							var _1584: vec3<f32> = refract_dir(_1585, _1534, _1535);
							var _1537: vec3<f32>;
							_1537 = _1584;
							var _1586: f32 = 0.000000;
							var _1587: f32 = dot(_1537, _1537);
							var _1588: bool = _1587 == _1586;
							var _1589: f32 = fresnel_dielectric(_1536, _1535);
							var _1591: vec2<u32> = _1118.xy;
							var _1592: i32 = _1205 + _17;
							var _1590: f32 = rnd(_1591, _1153, _1592, _1121, _1120);
							var _1593: bool = _1590 < _1589;
							var _1594: bool = _1593 || _1588;
							if (_1594)
							{
								var _1598: vec3<f32> = _1155.direction;
								var _1597: vec3<f32> = reflect(_1598, _1534);
								_1518 = _1597;
								var _1599: bool = false;
								_1517 = _1599;
								var _1600: f32 = 0.000000;
								var _1601: f32 = dot(_1518, _1231);
								var _1602: bool = _1601 <= _1600;
								if (_1602)
								{
									var _1606: vec3<f32> = _1155.direction;
									var _1605: vec3<f32> = reflect(_1606, _1231);
									_1518 = _1605;
								}
							}
							var _1607: bool = !_1594;
							if (_1607)
							{
								var _1610: vec3<f32> = normalize(_1537);
								_1518 = _1610;
								var _1611: f32 = 0.000000;
								var _1612: f32 = dot(_1518, _1231);
								var _1613: bool = _1612 >= _1611;
								if (_1613)
								{
									var _1616: vec3<f32> = _1155.direction;
									_1518 = _1616;
								}
							}
						}
						if (_1517)
						{
							if (_1516)
							{
								var _1621: i32 = 1;
								_1159 += _1621;
								var _1622: f32 = 16.000000;
								var _1623: f32 = _1239.x;
								var _1624: f32 = 1.000000;
								var _1625: f32 = _1624 - _1623;
								var _1626: f32 = _1239.x;
								var _1627: f32 = 1.000000;
								var _1628: f32 = _1627 - _1626;
								var _1629: f32 = _1628 * _1625;
								var _1630: f32 = _1629 * _1622;
								_1160 = _1630;
								_1161 = _1237;
							}
							var _1631: bool = !_1516;
							var _1632: i32 = 0;
							var _1633: bool = _1159 > _1632;
							var _1634: bool = _1631 && _1633;
							if (_1634)
							{
								var _1637: i32 = 1;
								_1159 -= _1637;
							}
						}
						_1155.direction = _1518;
						var _1638: vec3<f32> = offset_ray(_1220, _1231, _1518);
						_1155.origin = _1638;
						var _1639: f32 = -1.000000;
						_1157 = _1639;
						var _1640: i32 = 1;
						_1197 += _1640;
						continue;
					}
					var _1642: vec2<u32> = _1118.xy;
					var _1643: i32 = _1205 + _17;
					var _1641: f32 = rnd(_1642, _1153, _1643, _1121, _1120);
					var _1240: f32;
					_1240 = _1641;
					var _1644: f32 = _1218.w;
					var _1645: bool = _1240 > _1644;
					if (_1645)
					{
						var _1651: vec3<f32> = _1155.direction;
						var _1650: tangent_basis = create_basis(_1651);
						var _1646: tangent_basis;
						_1646 = _1650;
						var _1653: vec3<f32> = _1646.tangent;
						var _1654: vec3<f32> = _1646.binormal;
						var _1655: vec3<f32> = _1155.direction;
						var _1657: vec2<u32> = _1118.xy;
						var _1658: i32 = _1205 + _19;
						var _1656: f32 = rnd(_1657, _1153, _1658, _1121, _1120);
						var _1660: vec2<u32> = _1118.xy;
						var _1661: i32 = 1;
						var _1662: i32 = _1205 + _19;
						var _1663: i32 = _1662 + _1661;
						var _1659: f32 = rnd(_1660, _1153, _1663, _1121, _1120);
						var _1652: vec3<f32> = cos_weighted_direction(_1653, _1654, _1655, _1656, _1659);
						var _1647: vec3<f32>;
						_1647 = _1652;
						var _1666: vec3<f32> = _1155.direction;
						var _1667: f32 = 0.500000;
						var _1668: f32 = _1239.y;
						var _1669: f32 = _1239.y;
						var _1670: f32 = _1669 * _1668;
						var _1671: f32 = _1670 * _1667;
						var _1672: vec3<f32> = vec3<f32>(_1671, _1671, _1671);
var _1665: vec3<f32> = mix(_1666, _1647, _1672);
						var _1664: vec3<f32> = normalize(_1665);
						_1155.direction = _1664;
						var _1674: vec3<f32> = _1155.direction;
						var _1673: vec3<f32> = offset_ray(_1220, _1231, _1674);
						_1155.origin = _1673;
						var _1675: f32 = -1.000000;
						_1157 = _1675;
						var _1676: i32 = 1;
						_1197 += _1676;
						continue;
					}
					var _1678: vec2<u32> = _1118.xy;
					var _1679: i32 = _1205 + _18;
					var _1677: f32 = rnd(_1678, _1153, _1679, _1121, _1120);
					_1240 = _1677;
					var _1680: bool = _1158 >= _23;
					if (_1680)
					{
						var _1687: f32 = _1156.x;
						var _1688: f32 = _1156.y;
						var _1686: f32 = max(_1687, _1688);
						var _1689: f32 = _1156.z;
						var _1685: f32 = max(_1686, _1689);
						var _1684: f32 = clamp(_1685, _25, _24);
						var _1681: f32;
						_1681 = _1684;
						var _1691: vec2<u32> = _1118.xy;
						var _1692: i32 = _1205 + _16;
						var _1690: f32 = rnd(_1691, _1153, _1692, _1121, _1120);
						var _1693: bool = _1690 > _1681;
						if (_1693)
						{
							break;
						}
						var _1696: vec3<f32> = _1156 / _1681;
						_1156 = _1696;
					}
					var _1697: i32 = 1;
					_1158 += _1697;
					var _1241: vec3<f32>;
					var _1242: vec3<f32>;
					if (_1235)
					{
						var _1702: tangent_basis = create_uv_basis(_1224, _1225, _1226, _1212, _1213, _1214, _1232);
						var _1698: tangent_basis;
						_1698 = _1702;
						var _1703: vec3<f32> = _1698.tangent;
						var _1704: vec3<f32> = _1233 * _1703;
						_1241 = _1704;
						var _1705: vec3<f32> = _1698.binormal;
						var _1706: vec3<f32> = _1233 * _1705;
						_1242 = _1706;
						var _1708: f32 = dot(_1230, _1241);
						var _1709: vec3<f32> = _1230 * _1708;
						var _1710: vec3<f32> = _1241 - _1709;
						var _1707: vec3<f32> = normalize(_1710);
						_1241 = _1707;
						var _1712: f32 = dot(_1241, _1242);
						var _1713: vec3<f32> = _1241 * _1712;
						var _1714: f32 = dot(_1230, _1242);
						var _1715: vec3<f32> = _1230 * _1714;
						var _1716: vec3<f32> = _1242 - _1715;
						var _1717: vec3<f32> = _1716 - _1713;
						var _1711: vec3<f32> = normalize(_1717);
						_1242 = _1711;
						if (_1234)
						{
							var _1720: vec3<f32> = -_1242;
							_1242 = _1720;
						}
						var _1722: f32 = 1.000000;
						var _1723: f32 = 2.000000;
						var _1724: vec3<f32> = _1236.xyz;
						var _1725: vec3<f32> = _1724 * _1723;
						var _1726: vec3<f32> = _1725 - _1722;
						var _1721: vec3<f32> = normalize(_1726);
						var _1699: vec3<f32>;
						_1699 = _1721;
						var _1728: f32 = _1699.z;
						var _1729: vec3<f32> = _1230 * _1728;
						var _1730: f32 = _1699.y;
						var _1731: vec3<f32> = _1242 * _1730;
						var _1732: f32 = _1699.x;
						var _1733: vec3<f32> = _1241 * _1732;
						var _1734: vec3<f32> = _1733 - _1731;
						var _1735: vec3<f32> = _1734 + _1729;
						var _1727: vec3<f32> = normalize(_1735);
						_1230 = _1727;
						var _1736: f32 = 0.000100;
						var _1737: f32 = dot(_1230, _1231);
						var _1738: bool = _1737 < _1736;
						if (_1738)
						{
							var _1742: f32 = dot(_1230, _1231);
							var _1743: f32 = 0.000100;
							var _1744: f32 = _1743 - _1742;
							var _1745: vec3<f32> = _1231 * _1744;
							var _1746: vec3<f32> = _1230 + _1745;
							var _1741: vec3<f32> = normalize(_1746);
							_1230 = _1741;
						}
					}
					var _1747: vec3<f32> = _1155.direction;
					var _1748: vec3<f32> = -_1747;
					var _1243: vec3<f32>;
					_1243 = _1748;
					var _1749: f32 = dot(_1230, _1243);
					var _1244: f32;
					_1244 = _1749;
					var _1750: f32 = 0.001000;
					var _1751: bool = _1244 < _1750;
					if (_1751)
					{
						var _1755: i32 = 0;
						var _1756: bool = _1197 > _1755;
						if (_1756)
						{
							break;
						}
						var _1760: f32 = 0.001000;
						var _1761: f32 = _1760 - _1244;
						var _1762: vec3<f32> = _1243 * _1761;
						var _1763: vec3<f32> = _1230 + _1762;
						var _1759: vec3<f32> = normalize(_1763);
						_1230 = _1759;
						var _1764: f32 = dot(_1230, _1231);
						var _1752: f32;
						_1752 = _1764;
						var _1765: f32 = 0.001000;
						var _1766: bool = _1752 < _1765;
						if (_1766)
						{
							var _1770: f32 = 0.001000;
							var _1771: f32 = _1770 - _1752;
							var _1772: vec3<f32> = _1231 * _1771;
							var _1773: vec3<f32> = _1230 + _1772;
							var _1769: vec3<f32> = normalize(_1773);
							_1230 = _1769;
						}
						var _1775: f32 = dot(_1230, _1243);
						var _1776: f32 = 0.001000;
						var _1774: f32 = max(_1775, _1776);
						_1244 = _1774;
					}
					var _1777: tangent_basis = create_basis(_1230);
					var _1245: tangent_basis;
					_1245 = _1777;
					var _1778: vec3<f32> = _1245.tangent;
					_1241 = _1778;
					var _1779: vec3<f32> = _1245.binormal;
					_1242 = _1779;
					var _1781: f32 = _1239.z;
					var _1780: vec3<f32> = surface_albedo(_1237, _1781);
					var _1246: vec3<f32>;
					_1246 = _1780;
					var _1783: f32 = _1239.z;
					var _1782: vec3<f32> = surface_specular(_1237, _1783);
					var _1247: vec3<f32>;
					_1247 = _1782;
					var _1784: f32 = _1239.y;
					var _1248: f32;
					_1248 = _1784;
					var _1785: vec3<f32> = spec_directional_albedo(_1247, _1244, _1248);
					var _1249: vec3<f32>;
					_1249 = _1785;
					var _1786: f32 = 1.000000;
					var _1787: vec3<f32> = _1786 - _1249;
					var _1788: vec3<f32> = _1246 * _1787;
					var _1250: vec3<f32>;
					_1250 = _1788;
					var _1789: f32 = luma(_1249);
					var _1251: f32;
					_1251 = _1789;
					var _1790: f32 = luma(_1250);
					var _1252: f32;
					_1252 = _1790;
					var _1793: f32 = _1251 + _1252;
					var _1794: f32 = 0.000010;
					var _1792: f32 = max(_1793, _1794);
					var _1795: f32 = _1251 / _1792;
					var _1796: f32 = 0.050000;
					var _1797: f32 = 0.995000;
					var _1791: f32 = clamp(_1795, _1796, _1797);
					var _1253: f32;
					_1253 = _1791;
					var _1799: f32 = _1248 * _1248;
					var _1800: f32 = 0.001000;
					var _1798: f32 = max(_1799, _1800);
					var _1254: f32;
					_1254 = _1798;
					var _1801: f32 = 0.000000;
					var _1802: f32 = _set0_1.params.w;
					var _1803: bool = _1802 != _1801;
					if (_1803)
					{
						var _1811: vec2<u32> = _1118.xy;
						var _1812: i32 = _1205 + _21;
						var _1810: f32 = rnd_fine(_1811, _1153, _1812, _1121, _1120);
						var _1814: vec2<u32> = _1118.xy;
						var _1815: i32 = 1;
						var _1816: i32 = _1205 + _21;
						var _1817: i32 = _1816 + _1815;
						var _1813: f32 = rnd_fine(_1814, _1153, _1817, _1121, _1120);
						var _1809: vec4<f32> = sample_env(_1810, _1813);
						var _1804: vec4<f32>;
						_1804 = _1809;
						var _1818: vec3<f32> = _1804.xyz;
						var _1805: vec3<f32>;
						_1805 = _1818;
						var _1819: f32 = _1804.w;
						var _1806: f32;
						_1806 = _1819;
						var _1820: f32 = 0.000000;
						var _1821: f32 = dot(_1805, _1231);
						var _1822: bool = _1821 > _1820;
						var _1823: f32 = 0.000000;
						var _1824: bool = _1806 > _1823;
						var _1825: bool = _1824 && _1822;
						if (_1825)
						{
							var _1830: vec4<f32> = bsdf_eval(_1805, _1230, _1243, _1244, _1250, _1247, _1254, _1253);
							var _1826: vec4<f32>;
							_1826 = _1830;
							var _1831: vec3<f32> = _1826.xyz;
							var _1827: vec3<f32>;
							_1827 = _1831;
							var _1832: f32 = 0.000000;
							var _1835: f32 = _1827.x;
							var _1836: f32 = _1827.y;
							var _1834: f32 = max(_1835, _1836);
							var _1837: f32 = _1827.z;
							var _1833: f32 = max(_1834, _1837);
							var _1838: bool = _1833 > _1832;
							if (_1838)
							{
								var _1842: vec3<f32> = offset_ray(_1220, _1231, _1805);
								var _1841: bool = occluded(_1842, _1805);
								var _1843: bool = !_1841;
								if (_1843)
								{
									var _1848: f32 = _1826.w;
									var _1847: f32 = mis_weight(_1806, _1848);
									var _1849: f32 = _1847 / _1806;
									var _1850: vec3<f32> = env_radiance(_1805);
									var _1851: vec3<f32> = _1156 * _1827;
									var _1852: vec3<f32> = _1851 * _1850;
									var _1853: vec3<f32> = _1852 * _1849;
									var _1854: f32 = 0.000000;
									var _1855: vec3<f32> = vec3<f32>(_1854, _1854, _1854);
									var _1856: f32 = 8.000000;
									var _1857: vec3<f32> = vec3<f32>(_1856, _1856, _1856);
									var _1846: vec3<f32> = clamp(_1853, _1855, _1857);
									_1122 += _1846;
								}
							}
						}
					}
					var _1859: vec2<u32> = _1118.xy;
					var _1860: i32 = _1205 + _19;
					var _1858: f32 = rnd(_1859, _1153, _1860, _1121, _1120);
					var _1255: f32;
					_1255 = _1858;
					var _1862: vec2<u32> = _1118.xy;
					var _1863: i32 = 1;
					var _1864: i32 = _1205 + _19;
					var _1865: i32 = _1864 + _1863;
					var _1861: f32 = rnd(_1862, _1153, _1865, _1121, _1120);
					var _1256: f32;
					_1256 = _1861;
					var _1866: bool = _1240 < _1253;
					if (_1866)
					{
						var _1876: f32 = _1254 * _1254;
						var _1867: f32;
						_1867 = _1876;
						var _1878: f32 = dot(_1243, _1241);
						var _1879: f32 = dot(_1243, _1242);
						var _1877: vec3<f32> = vec3<f32>(_1878, _1879, _1244);
						var _1868: vec3<f32>;
						_1868 = _1877;
						var _1880: vec3<f32> = sample_ggx_vndf(_1868, _1254, _1255, _1256);
						var _1869: vec3<f32>;
						_1869 = _1880;
						var _1882: vec3<f32> = -_1868;
						var _1881: vec3<f32> = reflect(_1882, _1869);
						var _1870: vec3<f32>;
						_1870 = _1881;
						var _1883: f32 = 0.000000;
						var _1884: f32 = _1870.z;
						var _1885: bool = _1884 <= _1883;
						if (_1885)
						{
							break;
						}
						var _1888: f32 = _1870.z;
						var _1889: vec3<f32> = _1230 * _1888;
						var _1890: f32 = _1870.y;
						var _1891: vec3<f32> = _1242 * _1890;
						var _1892: f32 = _1870.x;
						var _1893: vec3<f32> = _1241 * _1892;
						var _1894: vec3<f32> = _1893 + _1891;
						var _1895: vec3<f32> = _1894 + _1889;
						_1155.direction = _1895;
						var _1897: f32 = _1868.z;
						var _1896: f32 = smith_lambda(_1897, _1867);
						var _1871: f32;
						_1871 = _1896;
						var _1899: f32 = _1870.z;
						var _1898: f32 = smith_lambda(_1899, _1867);
						var _1872: f32;
						_1872 = _1898;
						var _1900: f32 = 1.000000;
						var _1901: f32 = _1900 + _1871;
						var _1902: f32 = _1901 + _1872;
						var _1903: f32 = 1.000000;
						var _1904: f32 = _1903 + _1871;
						var _1905: f32 = _1904 / _1902;
						var _1873: f32;
						_1873 = _1905;
						var _1906: f32 = _1873 / _1253;
						var _1909: f32 = dot(_1868, _1869);
						var _1910: f32 = 0.000000;
						var _1908: f32 = max(_1909, _1910);
						var _1907: vec3<f32> = f_schlick(_1247, _1908);
						var _1911: vec3<f32> = _1907 * _1906;
						_1156 *= _1911;
					}
					var _1912: bool = !_1866;
					if (_1912)
					{
						var _1915: vec3<f32> = cos_weighted_direction(_1241, _1242, _1230, _1255, _1256);
						_1155.direction = _1915;
						var _1916: f32 = 1.000000;
						var _1917: f32 = _1916 - _1253;
						var _1918: vec3<f32> = _1250 / _1917;
						_1156 *= _1918;
					}
					var _1919: f32 = 0.000000;
					var _1921: vec3<f32> = _1155.direction;
					var _1920: f32 = dot(_1921, _1231);
					var _1922: bool = _1920 <= _1919;
					if (_1922)
					{
						break;
					}
					var _1925: f32 = 0.000000;
					var _1928: f32 = _1156.x;
					var _1929: f32 = _1156.y;
					var _1927: f32 = max(_1928, _1929);
					var _1930: f32 = _1156.z;
					var _1926: f32 = max(_1927, _1930);
					var _1931: bool = _1926 <= _1925;
					if (_1931)
					{
						break;
					}
					var _1935: vec3<f32> = _1155.direction;
					var _1934: vec3<f32> = offset_ray(_1220, _1231, _1935);
					_1155.origin = _1934;
					var _1937: vec3<f32> = _1155.direction;
					var _1936: vec4<f32> = bsdf_eval(_1937, _1230, _1243, _1244, _1250, _1247, _1254, _1253);
					var _1257: vec4<f32>;
					_1257 = _1936;
					var _1938: f32 = _1257.w;
					_1157 = _1938;
					var _1939: i32 = 1;
					_1197 += _1939;
				}
				}
			}
			var _1940: i32 = 1;
			_1145 += _1940;
		}
		}
	}
	var _1942: vec2<u32> = _1118.xy;
	var _1941: vec4<f32> = textureLoad(_kong_prev, vec2<u32>(u32(_1942.x), u32(_1942.y)), 0);
	var _1123: vec4<f32>;
	_1123 = _1941;
	var _1943: vec3<f32> = _1123.xyz;
	var _1124: vec3<f32>;
	_1124 = _1943;
	var _1945: f32 = f32(_13);
	var _1944: f32 = f32(_1945);
	var _1946: vec3<f32> = _1122 / _1944;
	_1122 = _1946;
	var _1947: f32 = 1.000000;
	var _1948: f32 = _set0_1.eye.w;
	var _1949: f32 = _1948 + _1947;
	var _1950: f32 = 1.000000;
	var _1951: f32 = _1950 / _1949;
	var _1125: f32;
	_1125 = _1951;
	var _1953: vec3<f32> = vec3<f32>(_1125, _1125, _1125);
var _1952: vec3<f32> = mix(_1124, _1122, _1953);
	_1124 = _1952;
	var _1955: f32 = 1.000000;
	var _1954: vec4<f32> = vec4<f32>(_1124, _1955);
	var _1956: vec2<u32> = _1118.xy;
	textureStore(_set0_3, vec2<u32>(u32(_1956.x), u32(_1956.y)), _1954);
}

fn init_sampler(_661_in: vec2<u32>, _662_in: i32) -> sampler_cache {
	var _661: vec2<u32> = _661_in;
	var _662: i32 = _662_in;
	var _667: i32 = 127;
	var _668: i32 = 9;
	var _669: i32 = _662 * _668;
	var _671: u32 = _661.x;
	var _670: i32 = i32(_671);
	var _672: i32 = _670 + _669;
	var _673: i32 = _672 & _667;
	var _663: i32;
	_663 = _673;
	var _674: i32 = 127;
	var _675: i32 = 11;
	var _676: i32 = _662 * _675;
	var _678: u32 = _661.y;
	var _677: i32 = i32(_678);
	var _679: i32 = _677 + _676;
	var _680: i32 = _679 & _674;
	var _664: i32;
	_664 = _680;
	var _681: i32 = 8;
	var _682: i32 = 128;
	var _683: i32 = _664 * _682;
	var _684: i32 = _663 + _683;
	var _685: i32 = _684 * _681;
	var _665: i32;
	_665 = _685;
	var _666: sampler_cache;
	var _689: vec2<u32> = table_texel(_665);
	var _688: vec4<f32> = textureLoad(_set0_9, vec2<u32>(u32(_689.x), u32(_689.y)), 0);
	var _687: u32 = table_word(_688);
	var _693: i32 = 4;
	var _694: i32 = _665 + _693;
	var _692: vec2<u32> = table_texel(_694);
	var _691: vec4<f32> = textureLoad(_set0_9, vec2<u32>(u32(_692.x), u32(_692.y)), 0);
	var _690: u32 = table_word(_691);
	var _686: vec2<u32> = vec2<u32>(_687, _690);
	_666.scramble = _686;
	var _698: vec2<u32> = table_texel(_665);
	var _697: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_698.x), u32(_698.y)), 0);
	var _696: u32 = table_word(_697);
	var _702: i32 = 4;
	var _703: i32 = _665 + _702;
	var _701: vec2<u32> = table_texel(_703);
	var _700: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_701.x), u32(_701.y)), 0);
	var _699: u32 = table_word(_700);
	var _707: i32 = 8;
	var _708: i32 = _665 + _707;
	var _706: vec2<u32> = table_texel(_708);
	var _705: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_706.x), u32(_706.y)), 0);
	var _704: u32 = table_word(_705);
	var _712: i32 = 12;
	var _713: i32 = _665 + _712;
	var _711: vec2<u32> = table_texel(_713);
	var _710: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_711.x), u32(_711.y)), 0);
	var _709: u32 = table_word(_710);
	var _695: vec4<u32> = vec4<u32>(_696, _699, _704, _709);
	_666.rank = _695;
	return _666;
}

fn table_texel(_75_in: i32) -> vec2<u32> {
	var _75: i32 = _75_in;
	var _77: i32 = 2;
	var _78: i32 = 131071;
	var _79: i32 = _75 & _78;
	var _80: i32 = _79 >> u32(_77);
	var _76: i32;
	_76 = _80;
	var _83: i32 = 127;
	var _84: i32 = _76 & _83;
	var _82: u32 = u32(_84);
	var _86: i32 = 7;
	var _87: i32 = _76 >> u32(_86);
	var _85: u32 = u32(_87);
	var _81: vec2<u32> = vec2<u32>(_82, _85);
	return _81;
}

fn table_word(_118_in: vec4<f32>) -> u32 {
	var _118: vec4<f32> = _118_in;
	var _119: i32 = 24;
	var _121: f32 = 255.000000;
	var _122: f32 = _118.w;
	var _123: f32 = _122 * _121;
	var _120: u32 = u32(_123);
	var _124: u32 = _120 << u32(_119);
	var _125: i32 = 16;
	var _127: f32 = 255.000000;
	var _128: f32 = _118.z;
	var _129: f32 = _128 * _127;
	var _126: u32 = u32(_129);
	var _130: u32 = _126 << u32(_125);
	var _131: i32 = 8;
	var _133: f32 = 255.000000;
	var _134: f32 = _118.y;
	var _135: f32 = _134 * _133;
	var _132: u32 = u32(_135);
	var _136: u32 = _132 << u32(_131);
	var _138: f32 = 255.000000;
	var _139: f32 = _118.x;
	var _140: f32 = _139 * _138;
	var _137: u32 = u32(_140);
	var _141: u32 = _137 | _136;
	var _142: u32 = _141 | _130;
	var _143: u32 = _142 | _124;
	return _143;
}

fn rnd(_714_in: vec2<u32>, _715_in: i32, _716_in: i32, _717_in: sampler_cache, _718_in: i32) -> f32 {
	var _714: vec2<u32> = _714_in;
	var _715: i32 = _715_in;
	var _716: i32 = _716_in;
	var _717: sampler_cache = _717_in;
	var _718: i32 = _718_in;
	var _723: i32 = 7;
	var _724: i32 = _716 & _723;
	var _719: i32;
	_719 = _724;
	var _725: u32 = _717.scramble.y;
	var _720: u32;
	_720 = _725;
	var _726: i32 = 4;
	var _727: bool = _719 < _726;
	if (_727)
	{
		var _730: u32 = _717.scramble.x;
		_720 = _730;
	}
	var _731: i32 = 255;
	var _732: i32 = 8;
	var _733: i32 = 3;
	var _734: i32 = _719 & _733;
	var _735: i32 = _734 * _732;
	var _736: u32 = _720 >> u32(_735);
	var _737: u32 = _736 & u32(_731);
	var _721: u32;
	_721 = _737;
	var _722: u32;
	var _738: bool = _716 < _32;
	if (_738)
	{
		var _743: i32 = 2;
		var _744: i32 = _716 >> u32(_743);
		var _739: i32;
		_739 = _744;
		var _745: u32 = _717.rank.w;
		var _740: u32;
		_740 = _745;
		var _746: i32 = 0;
		var _747: bool = _739 == _746;
		if (_747)
		{
			var _750: u32 = _717.rank.x;
			_740 = _750;
		}
		var _751: bool = !_747;
		var _752: i32 = 1;
		var _753: bool = _739 == _752;
		var _754: bool = _751 && _753;
		if (_754)
		{
			var _757: u32 = _717.rank.y;
			_740 = _757;
		}
		var _758: bool = !_753;
		var _759: bool = _751 && _758;
		var _760: i32 = 2;
		var _761: bool = _739 == _760;
		var _762: bool = _759 && _761;
		if (_762)
		{
			var _765: u32 = _717.rank.z;
			_740 = _765;
		}
		var _766: i32 = 255;
		var _767: i32 = 8;
		var _768: i32 = 3;
		var _769: i32 = _716 & _768;
		var _770: i32 = _769 * _767;
		var _771: u32 = _740 >> u32(_770);
		var _772: u32 = _771 & u32(_766);
		_722 = _772;
	}
	var _773: bool = !_738;
	if (_773)
	{
		var _778: u32 = _714.x;
		var _777: i32 = i32(_778);
		var _780: u32 = _714.y;
		var _779: i32 = i32(_780);
		var _776: u32 = rank_value_at(_777, _779, _716, _718);
		_722 = _776;
	}
	var _781: f32 = rand_indexed(_715, _716, _722, _721);
	return _781;
}

fn rank_value_at(_144_in: i32, _145_in: i32, _146_in: i32, _147_in: i32) -> u32 {
	var _144: i32 = _144_in;
	var _145: i32 = _145_in;
	var _146: i32 = _146_in;
	var _147: i32 = _147_in;
	var _150: i32 = 8;
	var _151: i32 = 128;
	var _152: i32 = 127;
	var _153: i32 = 11;
	var _154: i32 = _147 * _153;
	var _155: i32 = _145 + _154;
	var _156: i32 = _155 & _152;
	var _157: i32 = _156 * _151;
	var _158: i32 = 127;
	var _159: i32 = 9;
	var _160: i32 = _147 * _159;
	var _161: i32 = _144 + _160;
	var _162: i32 = _161 & _158;
	var _163: i32 = _162 + _157;
	var _164: i32 = _163 * _150;
	var _165: i32 = 255;
	var _166: i32 = _146 & _165;
	var _167: i32 = _166 + _164;
	var _148: i32;
	_148 = _167;
	var _169: vec2<u32> = table_texel(_148);
	var _168: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_169.x), u32(_169.y)), 0);
	var _149: vec4<f32>;
	_149 = _168;
	var _170: u32 = table_byte(_149, _148);
	return _170;
}

fn table_byte(_88_in: vec4<f32>, _89_in: i32) -> u32 {
	var _88: vec4<f32> = _88_in;
	var _89: i32 = _89_in;
	var _92: i32 = 3;
	var _93: i32 = _89 & _92;
	var _90: i32;
	_90 = _93;
	var _94: f32 = _88.w;
	var _91: f32;
	_91 = _94;
	var _95: i32 = 0;
	var _96: bool = _90 == _95;
	if (_96)
	{
		var _99: f32 = _88.x;
		_91 = _99;
	}
	var _100: bool = !_96;
	var _101: i32 = 1;
	var _102: bool = _90 == _101;
	var _103: bool = _100 && _102;
	if (_103)
	{
		var _106: f32 = _88.y;
		_91 = _106;
	}
	var _107: bool = !_102;
	var _108: bool = _100 && _107;
	var _109: i32 = 2;
	var _110: bool = _90 == _109;
	var _111: bool = _108 && _110;
	if (_111)
	{
		var _114: f32 = _88.z;
		_91 = _114;
	}
	var _116: f32 = 255.000000;
	var _117: f32 = _91 * _116;
	var _115: u32 = u32(_117);
	return _115;
}

fn rand_indexed(_171_in: i32, _172_in: i32, _173_in: u32, _174_in: u32) -> f32 {
	var _171: i32 = _171_in;
	var _172: i32 = _172_in;
	var _173: u32 = _173_in;
	var _174: u32 = _174_in;
	var _178: i32 = 255;
	var _179: i32 = _171 & _178;
	_171 = _179;
	var _180: i32 = 255;
	var _181: i32 = _172 & _180;
	_172 = _181;
	var _182: i32 = i32(_173);
	var _183: i32 = _171 ^ _182;
	var _175: i32;
	_175 = _183;
	var _186: u32 = u32(_175);
	var _187: u32 = u32(_172);
	var _185: vec2<u32> = vec2<u32>(_186, _187);
	var _184: vec4<f32> = textureLoad(_set0_8, vec2<u32>(u32(_185.x), u32(_185.y)), 0);
	var _176: vec4<f32>;
	_176 = _184;
	var _189: f32 = 255.000000;
	var _190: f32 = _176.x;
	var _191: f32 = _190 * _189;
	var _188: i32 = i32(_191);
	var _177: i32;
	_177 = _188;
	var _192: i32 = i32(_174);
	var _193: i32 = _177 ^ _192;
	_177 = _193;
	var _194: f32 = 256.000000;
	var _196: f32 = f32(_177);
	var _195: f32 = f32(_196);
	var _197: f32 = 0.500000;
	var _198: f32 = _197 + _195;
	var _199: f32 = _198 / _194;
	return _199;
}

fn camera_ray_direction(_258_in: vec2<f32>, _259_in: vec3<f32>) -> vec3<f32> {
	var _258: vec2<f32> = _258_in;
	var _259: vec3<f32> = _259_in;
	var _263: f32 = _258.x;
	var _264: f32 = _258.y;
	var _265: f32 = -_264;
	var _266: f32 = 0.000000;
	var _267: f32 = 1.000000;
	var _262: vec4<f32> = vec4<f32>(_263, _265, _266, _267);
	var _268: mat4x4<f32> = _set0_1.inv_vp;
	var _269: vec4<f32> = _268 * _262;
	var _260: vec4<f32>;
	_260 = _269;
	var _270: f32 = _260.w;
	var _271: vec3<f32> = _260.xyz;
	var _272: vec3<f32> = _271 / _270;
	var _261: vec3<f32>;
	_261 = _272;
	var _274: vec3<f32> = _261 - _259;
	var _273: vec3<f32> = normalize(_274);
	return _273;
}

fn env_radiance(_782_in: vec3<f32>) -> vec3<f32> {
	var _782: vec3<f32> = _782_in;
	var _786: f32 = _set0_1.params.y;
	var _785: vec2<f32> = equirect(_782, _786);
	var _783: vec2<f32>;
	_783 = _785;
	var _788: f32 = 0.000000;
	var _787: vec4<f32> = textureSampleLevel(_set0_7, _set0_12, _783, _788);
	var _784: vec4<f32>;
	_784 = _787;
	var _790: f32 = _set0_1.params.x;
	var _789: f32 = abs(_790);
	var _791: vec3<f32> = _784.xyz;
	var _792: vec3<f32> = _791 * _789;
	return _792;
}

fn equirect(_200_in: vec3<f32>, _201_in: f32) -> vec2<f32> {
	var _200: vec3<f32> = _200_in;
	var _201: f32 = _201_in;
	var _206: f32 = _200.z;
	var _207: f32 = -1.000000;
	var _208: f32 = 1.000000;
	var _205: f32 = clamp(_206, _207, _208);
	var _204: f32 = acos(_205);
	var _202: f32;
	_202 = _204;
	var _210: f32 = _200.y;
	var _211: f32 = -_210;
	var _212: f32 = _200.x;
	var _209: f32 = atan2(_211, _212);
	var _213: f32 = _209 + _26;
	var _214: f32 = _213 + _201;
	var _203: f32;
	_203 = _214;
	var _216: f32 = _203 / _27;
	var _217: f32 = _202 / _26;
	var _215: vec2<f32> = vec2<f32>(_216, _217);
	return _215;
}

fn env_pdf(_915_in: vec3<f32>) -> f32 {
	var _915: vec3<f32> = _915_in;
	var _921: f32 = 0.000000;
	var _922: f32 = _set0_1.params.w;
	var _923: bool = _922 == _921;
	if (_923)
	{
		var _926: f32 = 0.000000;
		return _926;
	}
	var _928: f32 = _set0_1.params.y;
	var _927: vec2<f32> = equirect(_915, _928);
	var _916: vec2<f32>;
	_916 = _927;
	var _932: f32 = f32(_29);
	var _931: f32 = f32(_932);
	var _934: f32 = _916.x;
var _933: f32 = fract(_934);
	var _935: f32 = _933 * _931;
	var _930: i32 = i32(_935);
	var _936: i32 = 0;
	var _937: i32 = 1;
	var _938: i32 = _29 - _937;
	var _929: i32 = clamp_int(_930, _936, _938);
	var _917: i32;
	_917 = _929;
	var _942: f32 = f32(_30);
	var _941: f32 = f32(_942);
	var _943: f32 = _916.y;
	var _944: f32 = _943 * _941;
	var _940: i32 = i32(_944);
	var _945: i32 = 0;
	var _946: i32 = 1;
	var _947: i32 = _30 - _946;
	var _939: i32 = clamp_int(_940, _945, _947);
	var _918: i32;
	_918 = _939;
	var _950: u32 = u32(_917);
	var _951: u32 = u32(_918);
	var _949: vec2<u32> = vec2<u32>(_950, _951);
	var _948: vec4<f32> = textureLoad(_set0_11, vec2<u32>(u32(_949.x), u32(_949.y)), 0);
	var _919: vec4<f32>;
	_919 = _948;
	var _953: f32 = _916.y;
	var _954: f32 = _953 * _26;
	var _952: f32 = sin(_954);
	var _920: f32;
	_920 = _952;
	var _955: f32 = 0.000001;
	var _956: bool = _920 > _955;
	if (_956)
	{
		var _959: f32 = _28 * _920;
		var _960: f32 = _919.z;
		var _961: f32 = _960 / _959;
		return _961;
	}
	var _962: f32 = 0.000000;
	return _962;
}

fn clamp_int(_793_in: i32, _794_in: i32, _795_in: i32) -> i32 {
	var _793: i32 = _793_in;
	var _794: i32 = _794_in;
	var _795: i32 = _795_in;
	var _796: bool = _793 < _794;
	if (_796)
	{
		return _794;
	}
	var _799: bool = _793 > _795;
	if (_799)
	{
		return _795;
	}
	return _793;
}

fn mis_weight(_1016_in: f32, _1017_in: f32) -> f32 {
	var _1016: f32 = _1016_in;
	var _1017: f32 = _1017_in;
	var _1020: f32 = _1016 * _1016;
	var _1018: f32;
	_1018 = _1020;
	var _1021: f32 = _1017 * _1017;
	var _1019: f32;
	_1019 = _1021;
	var _1023: f32 = _1018 + _1019;
	var _1024: f32 = 0.000000;
	var _1022: f32 = max(_1023, _1024);
	var _1025: f32 = _1018 / _1022;
	return _1025;
}

fn s16_to_f32(_33_in: u32) -> vec2<f32> {
	var _33: u32 = _33_in;
	var _36: i32 = 16;
	var _38: i32 = 16;
	var _39: u32 = _33 << u32(_38);
	var _37: i32 = i32(_39);
	var _40: i32 = _37 >> u32(_36);
	var _34: i32;
	_34 = _40;
	var _41: i32 = 16;
	var _42: i32 = i32(_33);
	var _43: i32 = _42 >> u32(_41);
	var _35: i32;
	_35 = _43;
	var _44: f32 = 32767.000000;
	var _47: f32 = f32(_34);
	var _46: f32 = f32(_47);
	var _49: f32 = f32(_35);
	var _48: f32 = f32(_49);
	var _45: vec2<f32> = vec2<f32>(_46, _48);
	var _50: vec2<f32> = _45 / _44;
	return _50;
}

fn hit_attribute2d(_63_in: vec2<f32>, _64_in: vec2<f32>, _65_in: vec2<f32>, _66_in: vec2<f32>) -> vec2<f32> {
	var _63: vec2<f32> = _63_in;
	var _64: vec2<f32> = _64_in;
	var _65: vec2<f32> = _65_in;
	var _66: vec2<f32> = _66_in;
	var _67: vec2<f32> = _65 - _63;
	var _68: f32 = _66.y;
	var _69: vec2<f32> = _68 * _67;
	var _70: vec2<f32> = _64 - _63;
	var _71: f32 = _66.x;
	var _72: vec2<f32> = _71 * _70;
	var _73: vec2<f32> = _63 + _72;
	var _74: vec2<f32> = _73 + _69;
	return _74;
}

fn uniform_sphere(_626_in: f32, _627_in: f32) -> vec3<f32> {
	var _626: f32 = _626_in;
	var _627: f32 = _627_in;
	var _631: f32 = 2.000000;
	var _632: f32 = _631 * _626;
	var _633: f32 = 1.000000;
	var _634: f32 = _633 - _632;
	var _628: f32;
	_628 = _634;
	var _637: f32 = 0.000000;
	var _638: f32 = _628 * _628;
	var _639: f32 = 1.000000;
	var _640: f32 = _639 - _638;
	var _636: f32 = max(_637, _640);
	var _635: f32 = sqrt(_636);
	var _629: f32;
	_629 = _635;
	var _641: f32 = _27 * _627;
	var _630: f32;
	_630 = _641;
	var _643: f32 = cos(_630);
	var _644: f32 = _629 * _643;
	var _645: f32 = sin(_630);
	var _646: f32 = _629 * _645;
	var _642: vec3<f32> = vec3<f32>(_644, _646, _628);
	return _642;
}

fn hit_attribute(_51_in: vec3<f32>, _52_in: vec3<f32>, _53_in: vec3<f32>, _54_in: vec2<f32>) -> vec3<f32> {
	var _51: vec3<f32> = _51_in;
	var _52: vec3<f32> = _52_in;
	var _53: vec3<f32> = _53_in;
	var _54: vec2<f32> = _54_in;
	var _55: vec3<f32> = _53 - _51;
	var _56: f32 = _54.y;
	var _57: vec3<f32> = _56 * _55;
	var _58: vec3<f32> = _52 - _51;
	var _59: f32 = _54.x;
	var _60: vec3<f32> = _59 * _58;
	var _61: vec3<f32> = _51 + _60;
	var _62: vec3<f32> = _61 + _57;
	return _62;
}

fn srgb_to_linear(_297_in: vec3<f32>) -> vec3<f32> {
	var _297: vec3<f32> = _297_in;
	var _298: f32 = 0.012523;
	var _299: f32 = 0.682171;
	var _300: f32 = 0.305306;
	var _301: vec3<f32> = _297 * _300;
	var _302: vec3<f32> = _301 + _299;
	var _303: vec3<f32> = _297 * _302;
	var _304: vec3<f32> = _303 + _298;
	var _305: vec3<f32> = _297 * _304;
	return _305;
}

fn create_basis(_218_in: vec3<f32>) -> tangent_basis {
	var _218: vec3<f32> = _218_in;
	var _223: f32 = 1.000000;
	var _219: f32;
	_219 = _223;
	var _224: f32 = 0.000000;
	var _225: f32 = _218.z;
	var _226: bool = _225 < _224;
	if (_226)
	{
		var _229: f32 = -1.000000;
		_219 = _229;
	}
	var _230: f32 = _218.z;
	var _231: f32 = _219 + _230;
	var _232: f32 = -1.000000;
	var _233: f32 = _232 / _231;
	var _220: f32;
	_220 = _233;
	var _234: f32 = _218.y;
	var _235: f32 = _218.x;
	var _236: f32 = _235 * _234;
	var _237: f32 = _236 * _220;
	var _221: f32;
	_221 = _237;
	var _222: tangent_basis;
	var _239: f32 = _218.x;
	var _240: f32 = _218.x;
	var _241: f32 = _219 * _240;
	var _242: f32 = _241 * _239;
	var _243: f32 = _242 * _220;
	var _244: f32 = 1.000000;
	var _245: f32 = _244 + _243;
	var _246: f32 = _219 * _221;
	var _247: f32 = _218.x;
	var _248: f32 = -_219;
	var _249: f32 = _248 * _247;
	var _238: vec3<f32> = vec3<f32>(_245, _246, _249);
	_222.tangent = _238;
	var _251: f32 = _218.y;
	var _252: f32 = _218.y;
	var _253: f32 = _252 * _251;
	var _254: f32 = _253 * _220;
	var _255: f32 = _219 + _254;
	var _256: f32 = _218.y;
	var _257: f32 = -_256;
	var _250: vec3<f32> = vec3<f32>(_221, _255, _257);
	_222.binormal = _250;
	return _222;
}

fn sample_ggx_vndf(_476_in: vec3<f32>, _477_in: f32, _478_in: f32, _479_in: f32) -> vec3<f32> {
	var _476: vec3<f32> = _476_in;
	var _477: f32 = _477_in;
	var _478: f32 = _478_in;
	var _479: f32 = _479_in;
	var _492: f32 = _476.x;
	var _493: f32 = _477 * _492;
	var _494: f32 = _476.y;
	var _495: f32 = _477 * _494;
	var _496: f32 = _476.z;
	var _491: vec3<f32> = vec3<f32>(_493, _495, _496);
	var _490: vec3<f32> = normalize(_491);
	var _480: vec3<f32>;
	_480 = _490;
	var _497: f32 = _480.y;
	var _498: f32 = _480.y;
	var _499: f32 = _498 * _497;
	var _500: f32 = _480.x;
	var _501: f32 = _480.x;
	var _502: f32 = _501 * _500;
	var _503: f32 = _502 + _499;
	var _481: f32;
	_481 = _503;
	var _505: f32 = 1.000000;
	var _506: f32 = 0.000000;
	var _507: f32 = 0.000000;
	var _504: vec3<f32> = vec3<f32>(_505, _506, _507);
	var _482: vec3<f32>;
	_482 = _504;
	var _508: f32 = 0.000000;
	var _509: bool = _481 > _508;
	if (_509)
	{
var _512: f32 = inverseSqrt(_481);
		var _514: f32 = _480.y;
		var _515: f32 = -_514;
		var _516: f32 = _480.x;
		var _517: f32 = 0.000000;
		var _513: vec3<f32> = vec3<f32>(_515, _516, _517);
		var _518: vec3<f32> = _513 * _512;
		_482 = _518;
	}
	var _519: vec3<f32> = cross(_480, _482);
	var _483: vec3<f32>;
	_483 = _519;
	var _520: f32 = sqrt(_478);
	var _484: f32;
	_484 = _520;
	var _521: f32 = _27 * _479;
	var _485: f32;
	_485 = _521;
	var _522: f32 = cos(_485);
	var _523: f32 = _484 * _522;
	var _486: f32;
	_486 = _523;
	var _524: f32 = sin(_485);
	var _525: f32 = _484 * _524;
	var _487: f32;
	_487 = _525;
	var _526: f32 = _480.z;
	var _527: f32 = 1.000000;
	var _528: f32 = _527 + _526;
	var _529: f32 = 0.500000;
	var _530: f32 = _529 * _528;
	var _488: f32;
	_488 = _530;
	var _531: f32 = _488 * _487;
	var _534: f32 = 0.000000;
	var _535: f32 = _486 * _486;
	var _536: f32 = 1.000000;
	var _537: f32 = _536 - _535;
	var _533: f32 = max(_534, _537);
	var _532: f32 = sqrt(_533);
	var _538: f32 = 1.000000;
	var _539: f32 = _538 - _488;
	var _540: f32 = _539 * _532;
	var _541: f32 = _540 + _531;
	_487 = _541;
	var _544: f32 = 0.000000;
	var _545: f32 = _487 * _487;
	var _546: f32 = _486 * _486;
	var _547: f32 = 1.000000;
	var _548: f32 = _547 - _546;
	var _549: f32 = _548 - _545;
	var _543: f32 = max(_544, _549);
	var _542: f32 = sqrt(_543);
	var _550: vec3<f32> = _542 * _480;
	var _551: vec3<f32> = _487 * _483;
	var _552: vec3<f32> = _486 * _482;
	var _553: vec3<f32> = _552 + _551;
	var _554: vec3<f32> = _553 + _550;
	var _489: vec3<f32>;
	_489 = _554;
	var _557: f32 = _489.x;
	var _558: f32 = _477 * _557;
	var _559: f32 = _489.y;
	var _560: f32 = _477 * _559;
	var _562: f32 = 0.000001;
	var _563: f32 = _489.z;
	var _561: f32 = max(_562, _563);
	var _556: vec3<f32> = vec3<f32>(_558, _560, _561);
	var _555: vec3<f32> = normalize(_556);
	return _555;
}

fn refract_dir(_598_in: vec3<f32>, _599_in: vec3<f32>, _600_in: f32) -> vec3<f32> {
	var _598: vec3<f32> = _598_in;
	var _599: vec3<f32> = _599_in;
	var _600: f32 = _600_in;
	var _603: f32 = dot(_598, _599);
	var _604: f32 = -_603;
	var _601: f32;
	_601 = _604;
	var _605: f32 = _601 * _601;
	var _606: f32 = 1.000000;
	var _607: f32 = _606 - _605;
	var _608: f32 = _600 * _600;
	var _609: f32 = _608 * _607;
	var _610: f32 = 1.000000;
	var _611: f32 = _610 - _609;
	var _602: f32;
	_602 = _611;
	var _612: f32 = 0.000000;
	var _613: bool = _602 < _612;
	if (_613)
	{
		var _617: f32 = 0.000000;
		var _618: f32 = 0.000000;
		var _619: f32 = 0.000000;
		var _616: vec3<f32> = vec3<f32>(_617, _618, _619);
		return _616;
	}
	var _620: f32 = sqrt(_602);
	var _621: f32 = _600 * _601;
	var _622: f32 = _621 - _620;
	var _623: vec3<f32> = _599 * _622;
	var _624: vec3<f32> = _598 * _600;
	var _625: vec3<f32> = _624 + _623;
	return _625;
}

fn fresnel_dielectric(_564_in: f32, _565_in: f32) -> f32 {
	var _564: f32 = _564_in;
	var _565: f32 = _565_in;
	var _570: f32 = _564 * _564;
	var _571: f32 = 1.000000;
	var _572: f32 = _571 - _570;
	var _573: f32 = _565 * _565;
	var _574: f32 = _573 * _572;
	var _566: f32;
	_566 = _574;
	var _575: f32 = 1.000000;
	var _576: bool = _566 >= _575;
	if (_576)
	{
		var _579: f32 = 1.000000;
		return _579;
	}
	var _581: f32 = 1.000000;
	var _582: f32 = _581 - _566;
	var _580: f32 = sqrt(_582);
	var _567: f32;
	_567 = _580;
	var _583: f32 = _565 * _564;
	var _584: f32 = _583 + _567;
	var _585: f32 = _565 * _564;
	var _586: f32 = _585 - _567;
	var _587: f32 = _586 / _584;
	var _568: f32;
	_568 = _587;
	var _588: f32 = _565 * _567;
	var _589: f32 = _564 + _588;
	var _590: f32 = _565 * _567;
	var _591: f32 = _564 - _590;
	var _592: f32 = _591 / _589;
	var _569: f32;
	_569 = _592;
	var _593: f32 = _569 * _569;
	var _594: f32 = _568 * _568;
	var _595: f32 = _594 + _593;
	var _596: f32 = 0.500000;
	var _597: f32 = _596 * _595;
	return _597;
}

fn offset_ray(_647_in: vec3<f32>, _648_in: vec3<f32>, _649_in: vec3<f32>) -> vec3<f32> {
	var _647: vec3<f32> = _647_in;
	var _648: vec3<f32> = _648_in;
	var _649: vec3<f32> = _649_in;
	var _650: f32 = 0.000000;
	var _651: f32 = dot(_649, _648);
	var _652: bool = _651 < _650;
	if (_652)
	{
		var _655: f32 = 0.000100;
		var _656: vec3<f32> = _648 * _655;
		var _657: vec3<f32> = _647 - _656;
		return _657;
	}
	var _658: f32 = 0.000100;
	var _659: vec3<f32> = _648 * _658;
	var _660: vec3<f32> = _647 + _659;
	return _660;
}

fn cos_weighted_direction(_453_in: vec3<f32>, _454_in: vec3<f32>, _455_in: vec3<f32>, _456_in: f32, _457_in: f32) -> vec3<f32> {
	var _453: vec3<f32> = _453_in;
	var _454: vec3<f32> = _454_in;
	var _455: vec3<f32> = _455_in;
	var _456: f32 = _456_in;
	var _457: f32 = _457_in;
	var _460: f32 = sqrt(_456);
	var _458: f32;
	_458 = _460;
	var _461: f32 = _27 * _457;
	var _459: f32;
	_459 = _461;
	var _464: f32 = 0.000000;
	var _465: f32 = 1.000000;
	var _466: f32 = _465 - _456;
	var _463: f32 = max(_464, _466);
	var _462: f32 = sqrt(_463);
	var _467: vec3<f32> = _455 * _462;
	var _468: f32 = sin(_459);
	var _469: f32 = _458 * _468;
	var _470: vec3<f32> = _454 * _469;
	var _471: f32 = cos(_459);
	var _472: f32 = _458 * _471;
	var _473: vec3<f32> = _453 * _472;
	var _474: vec3<f32> = _473 + _470;
	var _475: vec3<f32> = _474 + _467;
	return _475;
}

fn create_uv_basis(_379_in: vec3<f32>, _380_in: vec3<f32>, _381_in: vec3<f32>, _382_in: vec2<f32>, _383_in: vec2<f32>, _384_in: vec2<f32>, _385_in: vec3<f32>) -> tangent_basis {
	var _379: vec3<f32> = _379_in;
	var _380: vec3<f32> = _380_in;
	var _381: vec3<f32> = _381_in;
	var _382: vec2<f32> = _382_in;
	var _383: vec2<f32> = _383_in;
	var _384: vec2<f32> = _384_in;
	var _385: vec3<f32> = _385_in;
	var _391: vec3<f32> = _380 - _379;
	var _386: vec3<f32>;
	_386 = _391;
	var _392: vec3<f32> = _381 - _379;
	var _387: vec3<f32>;
	_387 = _392;
	var _393: vec2<f32> = _383 - _382;
	var _388: vec2<f32>;
	_388 = _393;
	var _394: vec2<f32> = _384 - _382;
	var _389: vec2<f32>;
	_389 = _394;
	var _395: f32 = _388.y;
	var _396: f32 = _389.x;
	var _397: f32 = _396 * _395;
	var _398: f32 = _389.y;
	var _399: f32 = _388.x;
	var _400: f32 = _399 * _398;
	var _401: f32 = _400 - _397;
	var _390: f32;
	_390 = _401;
	var _402: f32 = 0.000000;
	var _403: f32 = abs(_390);
	var _404: bool = _403 > _402;
	if (_404)
	{
		var _412: f32 = 1.000000;
		var _413: f32 = _412 / _390;
		var _405: f32;
		_405 = _413;
		var _414: f32 = _388.y;
		var _415: vec3<f32> = _387 * _414;
		var _416: f32 = _389.y;
		var _417: vec3<f32> = _386 * _416;
		var _418: vec3<f32> = _417 - _415;
		var _419: vec3<f32> = _418 * _405;
		var _406: vec3<f32>;
		_406 = _419;
		var _420: f32 = _389.x;
		var _421: vec3<f32> = _386 * _420;
		var _422: f32 = _388.x;
		var _423: vec3<f32> = _387 * _422;
		var _424: vec3<f32> = _423 - _421;
		var _425: vec3<f32> = _424 * _405;
		var _407: vec3<f32>;
		_407 = _425;
		var _426: f32 = dot(_385, _406);
		var _427: vec3<f32> = _385 * _426;
		var _428: vec3<f32> = _406 - _427;
		var _408: vec3<f32>;
		_408 = _428;
		var _429: f32 = dot(_408, _408);
		var _409: f32;
		_409 = _429;
		var _430: f32 = 0.000000;
		var _431: bool = _409 > _430;
		if (_431)
		{
var _436: f32 = inverseSqrt(_409);
			var _437: vec3<f32> = _408 * _436;
			_408 = _437;
			var _438: f32 = dot(_385, _407);
			var _439: vec3<f32> = _385 * _438;
			var _440: vec3<f32> = _407 - _439;
			var _432: vec3<f32>;
			_432 = _440;
			var _441: f32 = dot(_408, _432);
			var _442: vec3<f32> = _408 * _441;
			var _443: vec3<f32> = _432 - _442;
			_432 = _443;
			var _444: f32 = dot(_432, _432);
			var _433: f32;
			_433 = _444;
			var _445: f32 = 0.000000;
			var _446: bool = _433 > _445;
			if (_446)
			{
				var _447: tangent_basis;
				_447.tangent = _408;
var _450: f32 = inverseSqrt(_433);
				var _451: vec3<f32> = _432 * _450;
				_447.binormal = _451;
				return _447;
			}
		}
	}
	var _452: tangent_basis = create_basis(_385);
	return _452;
}

fn surface_albedo(_275_in: vec3<f32>, _276_in: f32) -> vec3<f32> {
	var _275: vec3<f32> = _275_in;
	var _276: f32 = _276_in;
	var _279: f32 = 0.000000;
	var _280: f32 = 0.000000;
	var _281: f32 = 0.000000;
	var _278: vec3<f32> = vec3<f32>(_279, _280, _281);
	var _282: vec3<f32> = vec3<f32>(_276, _276, _276);
var _277: vec3<f32> = mix(_275, _278, _282);
	return _277;
}

fn surface_specular(_283_in: vec3<f32>, _284_in: f32) -> vec3<f32> {
	var _283: vec3<f32> = _283_in;
	var _284: f32 = _284_in;
	var _287: f32 = 0.040000;
	var _288: f32 = 0.040000;
	var _289: f32 = 0.040000;
	var _286: vec3<f32> = vec3<f32>(_287, _288, _289);
	var _290: vec3<f32> = vec3<f32>(_284, _284, _284);
var _285: vec3<f32> = mix(_286, _283, _290);
	return _285;
}

fn spec_directional_albedo(_320_in: vec3<f32>, _321_in: f32, _322_in: f32) -> vec3<f32> {
	var _320: vec3<f32> = _320_in;
	var _321: f32 = _321_in;
	var _322: f32 = _322_in;
	var _329: f32 = -1.000000;
	var _330: f32 = -0.027500;
	var _331: f32 = -0.572000;
	var _332: f32 = 0.022000;
	var _328: vec4<f32> = vec4<f32>(_329, _330, _331, _332);
	var _323: vec4<f32>;
	_323 = _328;
	var _334: f32 = 1.000000;
	var _335: f32 = 0.042500;
	var _336: f32 = 1.040000;
	var _337: f32 = -0.040000;
	var _333: vec4<f32> = vec4<f32>(_334, _335, _336, _337);
	var _324: vec4<f32>;
	_324 = _333;
	var _338: vec4<f32> = _323 * _322;
	var _339: vec4<f32> = _338 + _324;
	var _325: vec4<f32>;
	_325 = _339;
	var _340: f32 = _325.y;
	var _341: f32 = _325.x;
	var _343: f32 = _325.x;
	var _344: f32 = _325.x;
	var _345: f32 = _344 * _343;
	var _347: f32 = -9.280000;
	var _348: f32 = _347 * _321;
	var _346: f32 = exp2(_348);
	var _342: f32 = min(_345, _346);
	var _349: f32 = _342 * _341;
	var _350: f32 = _349 + _340;
	var _326: f32;
	_326 = _350;
	var _351: vec2<f32> = _325.zw;
	var _353: f32 = -1.040000;
	var _354: f32 = 1.040000;
	var _352: vec2<f32> = vec2<f32>(_353, _354);
	var _355: vec2<f32> = _352 * _326;
	var _356: vec2<f32> = _355 + _351;
	var _327: vec2<f32>;
	_327 = _356;
	var _358: f32 = _327.y;
	var _359: f32 = _327.x;
	var _360: vec3<f32> = _320 * _359;
	var _361: vec3<f32> = _360 + _358;
	var _357: vec3<f32> = saturate(_361);
	return _357;
}

fn luma(_291_in: vec3<f32>) -> f32 {
	var _291: vec3<f32> = _291_in;
	var _294: f32 = 0.212600;
	var _295: f32 = 0.715200;
	var _296: f32 = 0.072200;
	var _293: vec3<f32> = vec3<f32>(_294, _295, _296);
	var _292: f32 = dot(_291, _293);
	return _292;
}

fn rnd_fine(_985_in: vec2<u32>, _986_in: i32, _987_in: i32, _988_in: sampler_cache, _989_in: i32) -> f32 {
	var _985: vec2<u32> = _985_in;
	var _986: i32 = _986_in;
	var _987: i32 = _987_in;
	var _988: sampler_cache = _988_in;
	var _989: i32 = _989_in;
	var _996: u32 = u32(_987);
	var _995: u32 = hash_u32(_996);
	var _997: u32 = u32(_986);
	var _998: u32 = _997 ^ _995;
	var _994: u32 = hash_u32(_998);
	var _999: u32 = _985.y;
	var _1000: u32 = _999 ^ _994;
	var _993: u32 = hash_u32(_1000);
	var _1001: u32 = _985.x;
	var _1002: u32 = _1001 ^ _993;
	var _992: u32 = hash_u32(_1002);
	var _990: u32;
	_990 = _992;
	var _1003: f32 = 16777216.000000;
	var _1007: i32 = 8;
	var _1006: u32 = u32(_1007);
	var _1008: u32 = _990 >> u32(_1006);
	var _1005: f32 = f32(_1008);
	var _1004: f32 = f32(_1005);
	var _1009: f32 = _1004 / _1003;
	var _991: f32;
	_991 = _1009;
	var _1010: f32 = 256.000000;
	var _1011: f32 = 0.500000;
	var _1012: f32 = _991 - _1011;
	var _1013: f32 = _1012 / _1010;
	var _1014: f32 = rnd(_985, _986, _987, _988, _989);
	var _1015: f32 = _1014 + _1013;
	return _1015;
}

fn hash_u32(_963_in: u32) -> u32 {
	var _963: u32 = _963_in;
	var _965: i32 = 16;
	var _964: u32 = u32(_965);
	var _966: u32 = _963 >> u32(_964);
	var _968: i32 = 61;
	var _967: u32 = u32(_968);
	var _969: u32 = _963 ^ _967;
	var _970: u32 = _969 ^ _966;
	_963 = _970;
	var _972: i32 = 9;
	var _971: u32 = u32(_972);
	var _973: u32 = _963 * _971;
	_963 = _973;
	var _975: i32 = 4;
	var _974: u32 = u32(_975);
	var _976: u32 = _963 >> u32(_974);
	var _977: u32 = _963 ^ _976;
	_963 = _977;
	var _979: i32 = 668265261;
	var _978: u32 = u32(_979);
	var _980: u32 = _963 * _978;
	_963 = _980;
	var _982: i32 = 15;
	var _981: u32 = u32(_982);
	var _983: u32 = _963 >> u32(_981);
	var _984: u32 = _963 ^ _983;
	_963 = _984;
	return _963;
}

fn sample_env(_821_in: f32, _822_in: f32) -> vec4<f32> {
	var _821: f32 = _821_in;
	var _822: f32 = _822_in;
	var _834: f32 = f32(_31);
	var _833: f32 = f32(_834);
	var _835: f32 = _821 * _833;
	var _823: f32;
	_823 = _835;
	var _837: i32 = i32(_823);
	var _838: i32 = 0;
	var _839: i32 = 1;
	var _840: i32 = _31 - _839;
	var _836: i32 = clamp_int(_837, _838, _840);
	var _824: i32;
	_824 = _836;
	var _842: f32 = f32(_824);
	var _841: f32 = f32(_842);
	var _843: f32 = _823 - _841;
	var _825: f32;
	_825 = _843;
	var _847: i32 = 256;
	var _848: i32 = _824 % _847;
	var _846: u32 = u32(_848);
	var _850: i32 = 256;
	var _851: i32 = _824 / _850;
	var _849: u32 = u32(_851);
	var _845: vec2<u32> = vec2<u32>(_846, _849);
	var _844: vec4<f32> = textureLoad(_set0_11, vec2<u32>(u32(_845.x), u32(_845.y)), 0);
	var _826: vec4<f32>;
	_826 = _844;
	var _827: i32;
	var _852: f32 = 0.000000;
	var _828: f32;
	_828 = _852;
	var _829: f32;
	var _853: f32 = _826.x;
	var _854: bool = _822 < _853;
	if (_854)
	{
		_827 = _824;
		var _857: f32 = 0.000000;
		var _858: f32 = _826.x;
		var _859: bool = _858 > _857;
		if (_859)
		{
			var _862: f32 = _826.x;
			var _863: f32 = _822 / _862;
			_828 = _863;
		}
		var _864: f32 = _826.z;
		_829 = _864;
	}
	var _865: bool = !_854;
	if (_865)
	{
		var _870: f32 = _826.y;
		var _869: i32 = i32(_870);
		var _871: i32 = 0;
		var _872: i32 = 1;
		var _873: i32 = _31 - _872;
		var _868: i32 = clamp_int(_869, _871, _873);
		_827 = _868;
		var _874: f32 = 1.000000;
		var _875: f32 = _826.x;
		var _876: bool = _875 < _874;
		if (_876)
		{
			var _879: f32 = _826.x;
			var _880: f32 = 1.000000;
			var _881: f32 = _880 - _879;
			var _882: f32 = _826.x;
			var _883: f32 = _822 - _882;
			var _884: f32 = _883 / _881;
			_828 = _884;
		}
		var _885: f32 = _826.w;
		_829 = _885;
	}
	var _888: f32 = f32(_29);
	var _887: f32 = f32(_888);
	var _891: i32 = 256;
	var _892: i32 = _827 % _891;
	var _890: f32 = f32(_892);
	var _889: f32 = f32(_890);
	var _893: f32 = _889 + _825;
	var _894: f32 = _893 / _887;
	var _896: f32 = f32(_30);
	var _895: f32 = f32(_896);
	var _899: i32 = 256;
	var _900: i32 = _827 / _899;
	var _898: f32 = f32(_900);
	var _897: f32 = f32(_898);
	var _901: f32 = _897 + _828;
	var _902: f32 = _901 / _895;
	var _886: vec2<f32> = vec2<f32>(_894, _902);
	var _830: vec2<f32>;
	_830 = _886;
	var _904: f32 = _830.y;
	var _905: f32 = _904 * _26;
	var _903: f32 = sin(_905);
	var _831: f32;
	_831 = _903;
	var _906: f32 = 0.000000;
	var _832: f32;
	_832 = _906;
	var _907: f32 = 0.000001;
	var _908: bool = _831 > _907;
	if (_908)
	{
		var _911: f32 = _28 * _831;
		var _912: f32 = _829 / _911;
		_832 = _912;
	}
	var _914: vec3<f32> = env_dir(_830);
	var _913: vec4<f32> = vec4<f32>(_914, _832);
	return _913;
}

fn env_dir(_802_in: vec2<f32>) -> vec3<f32> {
	var _802: vec2<f32> = _802_in;
	var _806: f32 = _802.y;
	var _807: f32 = _806 * _26;
	var _803: f32;
	_803 = _807;
	var _808: f32 = _set0_1.params.y;
	var _809: f32 = _802.x;
	var _810: f32 = _809 * _27;
	var _811: f32 = _810 - _26;
	var _812: f32 = _811 - _808;
	var _804: f32;
	_804 = _812;
	var _813: f32 = sin(_803);
	var _805: f32;
	_805 = _813;
	var _815: f32 = cos(_804);
	var _816: f32 = _805 * _815;
	var _817: f32 = sin(_804);
	var _818: f32 = -_805;
	var _819: f32 = _818 * _817;
	var _820: f32 = cos(_803);
	var _814: vec3<f32> = vec3<f32>(_816, _819, _820);
	return _814;
}

fn bsdf_eval(_1026_in: vec3<f32>, _1027_in: vec3<f32>, _1028_in: vec3<f32>, _1029_in: f32, _1030_in: vec3<f32>, _1031_in: vec3<f32>, _1032_in: f32, _1033_in: f32) -> vec4<f32> {
	var _1026: vec3<f32> = _1026_in;
	var _1027: vec3<f32> = _1027_in;
	var _1028: vec3<f32> = _1028_in;
	var _1029: f32 = _1029_in;
	var _1030: vec3<f32> = _1030_in;
	var _1031: vec3<f32> = _1031_in;
	var _1032: f32 = _1032_in;
	var _1033: f32 = _1033_in;
	var _1048: f32 = dot(_1027, _1026);
	var _1034: f32;
	_1034 = _1048;
	var _1049: f32 = 0.000000;
	var _1050: bool = _1034 <= _1049;
	if (_1050)
	{
		var _1054: f32 = 0.000000;
		var _1055: f32 = 0.000000;
		var _1056: f32 = 0.000000;
		var _1057: f32 = 0.000000;
		var _1053: vec4<f32> = vec4<f32>(_1054, _1055, _1056, _1057);
		return _1053;
	}
	var _1059: vec3<f32> = _1028 + _1026;
	var _1058: vec3<f32> = normalize(_1059);
	var _1035: vec3<f32>;
	_1035 = _1058;
	var _1061: f32 = dot(_1027, _1035);
	var _1062: f32 = 0.000000;
	var _1060: f32 = max(_1061, _1062);
	var _1036: f32;
	_1036 = _1060;
	var _1064: f32 = dot(_1028, _1035);
	var _1065: f32 = 0.000000;
	var _1063: f32 = max(_1064, _1065);
	var _1037: f32;
	_1037 = _1063;
	var _1066: f32 = _1032 * _1032;
	var _1038: f32;
	_1038 = _1066;
	var _1067: f32 = 1.000000;
	var _1068: f32 = 1.000000;
	var _1069: f32 = _1038 - _1068;
	var _1070: f32 = _1036 * _1036;
	var _1071: f32 = _1070 * _1069;
	var _1072: f32 = _1071 + _1067;
	var _1039: f32;
	_1039 = _1072;
	var _1074: f32 = _26 * _1039;
	var _1075: f32 = _1074 * _1039;
	var _1076: f32 = 0.000000;
	var _1073: f32 = max(_1075, _1076);
	var _1077: f32 = _1038 / _1073;
	var _1040: f32;
	_1040 = _1077;
	var _1078: f32 = smith_lambda(_1029, _1038);
	var _1041: f32;
	_1041 = _1078;
	var _1079: f32 = smith_lambda(_1034, _1038);
	var _1042: f32;
	_1042 = _1079;
	var _1080: vec3<f32> = f_schlick(_1031, _1037);
	var _1043: vec3<f32>;
	_1043 = _1080;
	var _1082: f32 = 4.000000;
	var _1083: f32 = _1082 * _1029;
	var _1084: f32 = _1083 * _1034;
	var _1085: f32 = 0.000000;
	var _1081: f32 = max(_1084, _1085);
	var _1086: f32 = 1.000000;
	var _1087: f32 = _1086 + _1041;
	var _1088: f32 = _1087 + _1042;
	var _1089: f32 = _1088 * _1081;
	var _1090: f32 = _1040 / _1089;
	var _1091: vec3<f32> = _1043 * _1090;
	var _1044: vec3<f32>;
	_1044 = _1091;
	var _1092: vec3<f32> = _1030 / _26;
	var _1045: vec3<f32>;
	_1045 = _1092;
	var _1094: f32 = 4.000000;
	var _1095: f32 = _1094 * _1029;
	var _1096: f32 = 0.000000;
	var _1093: f32 = max(_1095, _1096);
	var _1097: f32 = 1.000000;
	var _1098: f32 = _1097 + _1041;
	var _1099: f32 = _1098 * _1093;
	var _1100: f32 = _1040 / _1099;
	var _1046: f32;
	_1046 = _1100;
	var _1101: f32 = _1034 / _26;
	var _1047: f32;
	_1047 = _1101;
	var _1103: vec3<f32> = _1045 + _1044;
	var _1104: vec3<f32> = _1103 * _1034;
	var _1105: f32 = 1.000000;
	var _1106: f32 = _1105 - _1033;
	var _1107: f32 = _1106 * _1047;
	var _1108: f32 = _1033 * _1046;
	var _1109: f32 = _1108 + _1107;
	var _1102: vec4<f32> = vec4<f32>(_1104, _1109);
	return _1102;
}

fn smith_lambda(_362_in: f32, _363_in: f32) -> f32 {
	var _362: f32 = _362_in;
	var _363: f32 = _363_in;
	var _365: f32 = _362 * _362;
	var _364: f32;
	_364 = _365;
	var _366: f32 = 1.000000;
	var _369: f32 = 0.000000;
	var _368: f32 = max(_364, _369);
	var _370: f32 = 1.000000;
	var _371: f32 = _370 - _364;
	var _372: f32 = _363 * _371;
	var _373: f32 = _372 / _368;
	var _374: f32 = 1.000000;
	var _375: f32 = _374 + _373;
	var _367: f32 = sqrt(_375);
	var _376: f32 = _367 - _366;
	var _377: f32 = 0.500000;
	var _378: f32 = _377 * _376;
	return _378;
}

fn f_schlick(_306_in: vec3<f32>, _307_in: f32) -> vec3<f32> {
	var _306: vec3<f32> = _306_in;
	var _307: f32 = _307_in;
	var _311: f32 = 1.000000;
	var _312: f32 = _311 - _307;
	var _310: f32 = saturate(_312);
	var _308: f32;
	_308 = _310;
	var _313: f32 = _308 * _308;
	var _309: f32;
	_309 = _313;
	var _314: f32 = _309 * _309;
	var _315: f32 = _314 * _308;
	var _316: f32 = 1.000000;
	var _317: vec3<f32> = _316 - _306;
	var _318: vec3<f32> = _317 * _315;
	var _319: vec3<f32> = _306 + _318;
	return _319;
}

fn occluded(_1110_in: vec3<f32>, _1111_in: vec3<f32>) -> bool {
	var _1110: vec3<f32> = _1110_in;
	var _1111: vec3<f32> = _1111_in;
	var _1112: _kong_ray;
	_1112.origin = _1110;
	_1112.direction = _1111;
	var _1114: f32 = 0.000100;
	_1112.min = _1114;
	var _1115: f32 = 100.000000;
	_1112.max = _1115;
	var _1113: _kong_ray_query;
	_1113 = _kong_trace(_1112, true);
	var _1117: bool = _1113.hit;
	return _1117;
}

