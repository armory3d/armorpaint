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

struct tangent_basis {
	tangent: vec3<f32>,
	binormal: vec3<f32>,
};

struct _1_type {
	v0: vec4<f32>,
	v1: vec4<f32>,
	v2: vec4<f32>,
	v3: vec4<f32>,
	v4: vec4<f32>,
};

const _13: i32 = 2;

const _14: i32 = 5;

const _15: i32 = 4;

const _16: f32 = 0.000500;

const _17: f32 = 3.14159274;

const _18: f32 = 6.28318548;

const _19: f32 = 19.739208;

const _20: i32 = 256;

const _21: i32 = 128;

const _22: i32 = 32768;

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
	var _475: vec3<u32> = _kong_dispatch_thread_id;
	var _463: vec3<u32>;
	_463 = _475;
	var _476: vec2<u32> = textureDimensions(_set0_3);
	var _464: vec2<u32>;
	_464 = _476;
	var _477: u32 = _464.y;
	var _478: u32 = _463.y;
	var _479: bool = _478 >= _477;
	var _480: u32 = _464.x;
	var _481: u32 = _463.x;
	var _482: bool = _481 >= _480;
	var _483: bool = _482 || _479;
	if (_483)
	{
		return;
	}
	var _487: vec2<u32> = _463.xy;
	var _486: vec4<f32> = textureLoad(_set0_4, vec2<u32>(u32(_487.x), u32(_487.y)), 0);
	var _465: vec4<f32>;
	_465 = _486;
	var _488: f32 = 0.000000;
	var _489: f32 = _465.w;
	var _490: bool = _489 == _488;
	if (_490)
	{
		var _494: f32 = 0.000000;
		var _495: f32 = 0.000000;
		var _496: f32 = 0.000000;
		var _497: f32 = 0.000000;
		var _493: vec4<f32> = vec4<f32>(_494, _495, _496, _497);
		var _498: vec2<u32> = _463.xy;
		textureStore(_set0_3, vec2<u32>(u32(_498.x), u32(_498.y)), _493);
		return;
	}
	var _499: vec3<f32> = _465.xyz;
	var _466: vec3<f32>;
	_466 = _499;
	var _501: vec2<u32> = _463.xy;
	var _500: vec4<f32> = textureLoad(_set0_5, vec2<u32>(u32(_501.x), u32(_501.y)), 0);
	var _467: vec4<f32>;
	_467 = _500;
	var _503: vec3<f32> = _467.xyz;
	var _502: vec3<f32> = normalize(_503);
	var _468: vec3<f32>;
	_468 = _502;
	var _505: f32 = _set0_1.v0.x;
	var _504: i32 = i32(_505);
	var _469: i32;
	_469 = _504;
	var _506: f32 = 0.000000;
	var _507: f32 = _set0_1.v1.w;
	var _508: bool = _507 != _506;
	var _470: bool;
	_470 = _508;
	var _510: u32 = _463.x;
	var _509: i32 = i32(_510);
	var _471: i32;
	_471 = _509;
	var _512: u32 = _463.y;
	var _511: i32 = i32(_512);
	var _472: i32;
	_472 = _511;
	var _514: f32 = 0.000000;
	var _515: f32 = 0.000000;
	var _516: f32 = 0.000000;
	var _513: vec3<f32> = vec3<f32>(_514, _515, _516);
	var _473: vec3<f32>;
	_473 = _513;
	{
		var _520: i32 = 0;
		var _517: i32;
		_517 = _520;
		while (true)
{
		var _524: bool = _517 < _13;
		if (!_524) { break; }
		{
			var _533: i32 = _469 * _13;
			var _534: i32 = _533 + _517;
			var _525: i32;
			_525 = _534;
			var _526: vec3<f32>;
			_526 = _466;
			var _527: vec3<f32>;
			_527 = _468;
			var _528: vec3<f32>;
			_528 = _468;
			var _536: f32 = 1.000000;
			var _537: f32 = 1.000000;
			var _538: f32 = 1.000000;
			var _535: vec3<f32> = vec3<f32>(_536, _537, _538);
			var _529: vec3<f32>;
			_529 = _535;
			var _540: f32 = 0.000000;
			var _541: f32 = 0.000000;
			var _542: f32 = 0.000000;
			var _539: vec3<f32> = vec3<f32>(_540, _541, _542);
			var _530: vec3<f32>;
			_530 = _539;
			{
				var _546: i32 = 0;
				var _543: i32;
				_543 = _546;
				while (true)
{
				var _550: bool = _543 < _14;
				if (!_550) { break; }
				{
					var _576: i32 = _543 * _15;
					var _551: i32;
					_551 = _576;
					if (_470)
					{
						var _584: f32 = rand(_471, _472, _525, _551, _469);
						var _586: i32 = 1;
						var _587: i32 = _551 + _586;
						var _585: f32 = rand(_471, _472, _525, _587, _469);
						var _583: vec4<f32> = sample_env(_584, _585);
						var _577: vec4<f32>;
						_577 = _583;
						var _588: vec3<f32> = _577.xyz;
						var _578: vec3<f32>;
						_578 = _588;
						var _589: f32 = _577.w;
						var _579: f32;
						_579 = _589;
						var _590: f32 = dot(_527, _578);
						var _580: f32;
						_580 = _590;
						var _591: f32 = 0.000000;
						var _592: f32 = dot(_528, _578);
						var _593: bool = _592 > _591;
						var _594: f32 = 0.000000;
						var _595: bool = _580 > _594;
						var _596: f32 = 0.000000;
						var _597: bool = _579 > _596;
						var _598: bool = _597 && _595;
						var _599: bool = _598 && _593;
						if (_599)
						{
							var _603: vec3<f32> = _528 * _16;
							var _604: vec3<f32> = _526 + _603;
							var _602: bool = occluded(_604, _578);
							var _605: bool = !_602;
							if (_605)
							{
								var _609: f32 = _580 / _17;
								var _606: f32;
								_606 = _609;
								var _610: f32 = mis_weight(_579, _606);
								var _611: f32 = _606 / _579;
								var _612: vec3<f32> = env_radiance(_578);
								var _613: vec3<f32> = _529 * _612;
								var _614: vec3<f32> = _613 * _611;
								var _615: vec3<f32> = _614 * _610;
								_530 += _615;
							}
						}
					}
					var _618: i32 = 2;
					var _619: i32 = _551 + _618;
					var _617: f32 = rand(_471, _472, _525, _619, _469);
					var _621: i32 = 3;
					var _622: i32 = _551 + _621;
					var _620: f32 = rand(_471, _472, _525, _622, _469);
					var _616: vec3<f32> = cos_weighted_direction(_527, _617, _620);
					var _552: vec3<f32>;
					_552 = _616;
					var _623: f32 = 0.000000;
					var _624: f32 = dot(_552, _528);
					var _625: bool = _624 <= _623;
					if (_625)
					{
						break;
					}
					var _629: f32 = dot(_527, _552);
					var _630: f32 = 0.000000;
					var _628: f32 = max(_629, _630);
					var _631: f32 = _628 / _17;
					var _553: f32;
					_553 = _631;
					var _554: _kong_ray;
					var _632: vec3<f32> = _528 * _16;
					var _633: vec3<f32> = _526 + _632;
					_554.origin = _633;
					_554.direction = _552;
					var _634: f32 = 0.000100;
					_554.min = _634;
					var _635: f32 = 100.000000;
					_554.max = _635;
					var _555: _kong_ray_query;
					_555 = _kong_trace(_554, false);
					var _637: bool = _555.hit;
					var _638: bool = !_637;
					if (_638)
					{
						var _642: f32 = 1.000000;
						var _639: f32;
						_639 = _642;
						if (_470)
						{
							var _646: f32 = env_pdf(_552);
							var _645: f32 = mis_weight(_553, _646);
							_639 = _645;
						}
						var _647: vec3<f32> = env_radiance(_552);
						var _648: vec3<f32> = _529 * _647;
						var _649: vec3<f32> = _648 * _639;
						_530 += _649;
						break;
					}
					var _650: u32 = _kong_instances[_555.instance].geometry;
					var _556: u32;
					_556 = _650;
					var _651: vec2<f32> = _555.barycentrics;
					var _557: vec2<f32>;
					_557 = _651;
					var _653: i32 = 0;
					var _652: vec4<u32> = _kong_vertices[_kong_indices[_555.primitive * 3u + u32(_653)]];
					var _558: vec4<u32>;
					_558 = _652;
					var _655: i32 = 1;
					var _654: vec4<u32> = _kong_vertices[_kong_indices[_555.primitive * 3u + u32(_655)]];
					var _559: vec4<u32>;
					_559 = _654;
					var _657: i32 = 2;
					var _656: vec4<u32> = _kong_vertices[_kong_indices[_555.primitive * 3u + u32(_657)]];
					var _560: vec4<u32>;
					_560 = _656;
					var _660: u32 = _558.w;
					var _659: vec2<f32> = s16_to_f32(_660);
					var _662: u32 = _559.w;
					var _661: vec2<f32> = s16_to_f32(_662);
					var _664: u32 = _560.w;
					var _663: vec2<f32> = s16_to_f32(_664);
					var _658: vec2<f32> = hit_attribute2d(_659, _661, _663, _557);
					var _561: vec2<f32>;
					_561 = _658;
					var _665: vec2<u32> = textureDimensions(_kong_geometry_texture2);
					var _562: vec2<u32>;
					_562 = _665;
					var _668: vec2<f32> = vec2<f32>(_562);
var _669: vec2<f32> = fract(_561);
					var _670: vec2<f32> = _669 * _668;
					var _667: vec2<u32> = vec2<u32>(_670);
					var _666: vec4<f32> = textureLoad(_kong_geometry_texture2, _667, 0);
					var _563: vec4<f32>;
					_563 = _666;
					var _672: vec3<f32> = _563.xyz;
					var _674: f32 = 2.200000;
					var _675: f32 = 2.200000;
					var _676: f32 = 2.200000;
					var _673: vec3<f32> = vec3<f32>(_674, _675, _676);
					var _671: vec3<f32> = pow(_672, _673);
					_529 *= _671;
					var _677: f32 = 0.000000;
					var _680: f32 = _529.x;
					var _681: f32 = _529.y;
					var _679: f32 = max(_680, _681);
					var _682: f32 = _529.z;
					var _678: f32 = max(_679, _682);
					var _683: bool = _678 <= _677;
					if (_683)
					{
						break;
					}
					var _687: u32 = _558.y;
					var _686: vec2<f32> = s16_to_f32(_687);
					var _564: vec2<f32>;
					_564 = _686;
					var _689: u32 = _559.y;
					var _688: vec2<f32> = s16_to_f32(_689);
					var _565: vec2<f32>;
					_565 = _688;
					var _691: u32 = _560.y;
					var _690: vec2<f32> = s16_to_f32(_691);
					var _566: vec2<f32>;
					_566 = _690;
					var _694: u32 = _558.x;
					var _693: vec2<f32> = s16_to_f32(_694);
					var _695: f32 = _564.x;
					var _692: vec3<f32> = vec3<f32>(_693, _695);
					var _567: vec3<f32>;
					_567 = _692;
					var _698: u32 = _559.x;
					var _697: vec2<f32> = s16_to_f32(_698);
					var _699: f32 = _565.x;
					var _696: vec3<f32> = vec3<f32>(_697, _699);
					var _568: vec3<f32>;
					_568 = _696;
					var _702: u32 = _560.x;
					var _701: vec2<f32> = s16_to_f32(_702);
					var _703: f32 = _566.x;
					var _700: vec3<f32> = vec3<f32>(_701, _703);
					var _569: vec3<f32>;
					_569 = _700;
					var _706: u32 = _558.z;
					var _705: vec2<f32> = s16_to_f32(_706);
					var _707: f32 = _564.y;
					var _704: vec3<f32> = vec3<f32>(_705, _707);
					var _570: vec3<f32>;
					_570 = _704;
					var _710: u32 = _559.z;
					var _709: vec2<f32> = s16_to_f32(_710);
					var _711: f32 = _565.y;
					var _708: vec3<f32> = vec3<f32>(_709, _711);
					var _571: vec3<f32>;
					_571 = _708;
					var _714: u32 = _560.z;
					var _713: vec2<f32> = s16_to_f32(_714);
					var _715: f32 = _566.y;
					var _712: vec3<f32> = vec3<f32>(_713, _715);
					var _572: vec3<f32>;
					_572 = _712;
					var _716: mat3x3<f32> = _kong_instances[_555.instance].object_to_world;
					var _573: mat3x3<f32>;
					_573 = _716;
					var _718: vec3<f32> = hit_attribute(_570, _571, _572, _557);
					var _719: vec3<f32> = _573 * _718;
					var _717: vec3<f32> = normalize(_719);
					_527 = _717;
					var _721: vec3<f32> = _568 - _567;
					var _722: vec3<f32> = _569 - _567;
					var _720: vec3<f32> = cross(_721, _722);
					_528 = _720;
					var _723: f32 = 0.000000;
					var _724: f32 = dot(_528, _528);
					var _725: bool = _724 > _723;
					if (_725)
					{
						var _729: vec3<f32> = _573 * _528;
						var _728: vec3<f32> = normalize(_729);
						_528 = _728;
					}
					var _730: bool = !_725;
					if (_730)
					{
						_528 = _527;
					}
					var _733: f32 = 0.000000;
					var _734: f32 = dot(_528, _552);
					var _735: bool = _734 > _733;
					if (_735)
					{
						var _738: vec3<f32> = -_528;
						_528 = _738;
					}
					var _739: f32 = 0.000000;
					var _740: f32 = dot(_527, _528);
					var _741: bool = _740 < _739;
					if (_741)
					{
						var _744: vec3<f32> = -_527;
						_527 = _744;
					}
					var _745: f32 = _555.t;
					var _746: vec3<f32> = _552 * _745;
					var _747: vec3<f32> = _554.origin;
					var _748: vec3<f32> = _747 + _746;
					_526 = _748;
					var _749: i32 = 1;
					_543 += _749;
				}
				}
			}
			var _752: f32 = 32.000000;
			var _753: f32 = 32.000000;
			var _754: f32 = 32.000000;
			var _751: vec3<f32> = vec3<f32>(_752, _753, _754);
			var _750: vec3<f32> = min(_530, _751);
			_473 += _750;
			var _755: i32 = 1;
			_517 += _755;
		}
		}
	}
	var _757: f32 = f32(_13);
	var _756: f32 = f32(_757);
	var _758: vec3<f32> = _473 / _756;
	_473 = _758;
	var _759: f32 = 0.000000;
	var _760: f32 = _set0_1.v2.x;
	var _761: bool = _760 == _759;
	if (_761)
	{
		var _766: vec2<u32> = _463.xy;
		var _765: vec4<f32> = textureLoad(_set0_6, vec2<u32>(u32(_766.x), u32(_766.y)), 0);
		var _762: vec4<f32>;
		_762 = _765;
		var _767: vec3<f32> = _762.xyz;
		_473 *= _767;
	}
	var _474: vec3<f32>;
	_474 = _473;
	var _768: i32 = 0;
	var _769: bool = _469 > _768;
	if (_769)
	{
		var _774: vec2<u32> = _463.xy;
		var _773: vec4<f32> = textureLoad(_kong_prev, vec2<u32>(u32(_774.x), u32(_774.y)), 0);
		var _770: vec4<f32>;
		_770 = _773;
		var _776: vec3<f32> = _770.xyz;
		var _779: i32 = 1;
		var _780: i32 = _469 + _779;
		var _778: f32 = f32(_780);
		var _777: f32 = f32(_778);
		var _781: f32 = 1.000000;
		var _782: f32 = _781 / _777;
		var _783: vec3<f32> = vec3<f32>(_782, _782, _782);
var _775: vec3<f32> = mix(_776, _473, _783);
		_474 = _775;
	}
	var _785: f32 = 1.000000;
	var _784: vec4<f32> = vec4<f32>(_474, _785);
	var _786: vec2<u32> = _463.xy;
	textureStore(_set0_3, vec2<u32>(u32(_786.x), u32(_786.y)), _784);
}

fn rand(_108_in: i32, _109_in: i32, _110_in: i32, _111_in: i32, _112_in: i32) -> f32 {
	var _108: i32 = _108_in;
	var _109: i32 = _109_in;
	var _110: i32 = _110_in;
	var _111: i32 = _111_in;
	var _112: i32 = _112_in;
	var _122: i32 = 8;
	var _123: i32 = 128;
	var _124: i32 = 127;
	var _125: i32 = 11;
	var _126: i32 = _112 * _125;
	var _127: i32 = _109 + _126;
	var _128: i32 = _127 & _124;
	var _129: i32 = _128 * _123;
	var _130: i32 = 127;
	var _131: i32 = 9;
	var _132: i32 = _112 * _131;
	var _133: i32 = _108 + _132;
	var _134: i32 = _133 & _130;
	var _135: i32 = _134 + _129;
	var _136: i32 = _135 * _122;
	var _137: i32 = 8;
	var _138: i32 = 255;
	var _139: i32 = _111 & _138;
	var _140: i32 = _139 % _137;
	var _141: i32 = _140 + _136;
	var _113: i32;
	_113 = _141;
	var _143: vec2<u32> = table_texel(_113);
	var _142: vec4<f32> = textureLoad(_set0_9, vec2<u32>(u32(_143.x), u32(_143.y)), 0);
	var _114: vec4<f32>;
	_114 = _142;
	var _144: u32 = table_channel(_114, _113);
	var _115: u32;
	_115 = _144;
	var _145: i32 = 8;
	var _146: i32 = 128;
	var _147: i32 = 127;
	var _148: i32 = 11;
	var _149: i32 = _112 * _148;
	var _150: i32 = _109 + _149;
	var _151: i32 = _150 & _147;
	var _152: i32 = _151 * _146;
	var _153: i32 = 127;
	var _154: i32 = 9;
	var _155: i32 = _112 * _154;
	var _156: i32 = _108 + _155;
	var _157: i32 = _156 & _153;
	var _158: i32 = _157 + _152;
	var _159: i32 = _158 * _145;
	var _160: i32 = 255;
	var _161: i32 = _111 & _160;
	var _162: i32 = _161 + _159;
	var _116: i32;
	_116 = _162;
	var _164: vec2<u32> = table_texel(_116);
	var _163: vec4<f32> = textureLoad(_set0_10, vec2<u32>(u32(_164.x), u32(_164.y)), 0);
	var _117: vec4<f32>;
	_117 = _163;
	var _165: u32 = table_channel(_117, _116);
	var _118: u32;
	_118 = _165;
	var _166: i32 = 255;
	var _167: i32 = _110 & _166;
	_110 = _167;
	var _168: i32 = 255;
	var _169: i32 = _111 & _168;
	_111 = _169;
	var _170: i32 = i32(_118);
	var _171: i32 = _110 ^ _170;
	var _119: i32;
	_119 = _171;
	var _174: u32 = u32(_119);
	var _175: u32 = u32(_111);
	var _173: vec2<u32> = vec2<u32>(_174, _175);
	var _172: vec4<f32> = textureLoad(_set0_8, vec2<u32>(u32(_173.x), u32(_173.y)), 0);
	var _120: vec4<f32>;
	_120 = _172;
	var _177: f32 = 255.000000;
	var _178: f32 = _120.x;
	var _179: f32 = _178 * _177;
	var _176: i32 = i32(_179);
	var _121: i32;
	_121 = _176;
	var _180: i32 = i32(_115);
	var _181: i32 = _121 ^ _180;
	_121 = _181;
	var _182: f32 = 256.000000;
	var _184: f32 = f32(_121);
	var _183: f32 = f32(_184);
	var _185: f32 = 0.500000;
	var _186: f32 = _185 + _183;
	var _187: f32 = _186 / _182;
	return _187;
}

fn table_texel(_65_in: i32) -> vec2<u32> {
	var _65: i32 = _65_in;
	var _67: i32 = 2;
	var _68: i32 = 131071;
	var _69: i32 = _65 & _68;
	var _70: i32 = _69 >> u32(_67);
	var _66: i32;
	_66 = _70;
	var _73: i32 = 127;
	var _74: i32 = _66 & _73;
	var _72: u32 = u32(_74);
	var _76: i32 = 7;
	var _77: i32 = _66 >> u32(_76);
	var _75: u32 = u32(_77);
	var _71: vec2<u32> = vec2<u32>(_72, _75);
	return _71;
}

fn table_channel(_78_in: vec4<f32>, _79_in: i32) -> u32 {
	var _78: vec4<f32> = _78_in;
	var _79: i32 = _79_in;
	var _82: i32 = 3;
	var _83: i32 = _79 & _82;
	var _80: i32;
	_80 = _83;
	var _84: f32 = _78.w;
	var _81: f32;
	_81 = _84;
	var _85: i32 = 0;
	var _86: bool = _80 == _85;
	if (_86)
	{
		var _89: f32 = _78.x;
		_81 = _89;
	}
	var _90: bool = !_86;
	var _91: i32 = 1;
	var _92: bool = _80 == _91;
	var _93: bool = _90 && _92;
	if (_93)
	{
		var _96: f32 = _78.y;
		_81 = _96;
	}
	var _97: bool = !_92;
	var _98: bool = _90 && _97;
	var _99: i32 = 2;
	var _100: bool = _80 == _99;
	var _101: bool = _98 && _100;
	if (_101)
	{
		var _104: f32 = _78.z;
		_81 = _104;
	}
	var _106: f32 = 255.000000;
	var _107: f32 = _81 * _106;
	var _105: u32 = u32(_107);
	return _105;
}

fn sample_env(_309_in: f32, _310_in: f32) -> vec4<f32> {
	var _309: f32 = _309_in;
	var _310: f32 = _310_in;
	var _322: f32 = f32(_22);
	var _321: f32 = f32(_322);
	var _323: f32 = _309 * _321;
	var _311: f32;
	_311 = _323;
	var _325: i32 = i32(_311);
	var _326: i32 = 0;
	var _327: i32 = 1;
	var _328: i32 = _22 - _327;
	var _324: i32 = clamp_int(_325, _326, _328);
	var _312: i32;
	_312 = _324;
	var _330: f32 = f32(_312);
	var _329: f32 = f32(_330);
	var _331: f32 = _311 - _329;
	var _313: f32;
	_313 = _331;
	var _335: i32 = 256;
	var _336: i32 = _312 % _335;
	var _334: u32 = u32(_336);
	var _338: i32 = 256;
	var _339: i32 = _312 / _338;
	var _337: u32 = u32(_339);
	var _333: vec2<u32> = vec2<u32>(_334, _337);
	var _332: vec4<f32> = textureLoad(_set0_11, vec2<u32>(u32(_333.x), u32(_333.y)), 0);
	var _314: vec4<f32>;
	_314 = _332;
	var _315: i32;
	var _340: f32 = 0.000000;
	var _316: f32;
	_316 = _340;
	var _317: f32;
	var _341: f32 = _314.x;
	var _342: bool = _310 < _341;
	if (_342)
	{
		_315 = _312;
		var _345: f32 = 0.000000;
		var _346: f32 = _314.x;
		var _347: bool = _346 > _345;
		if (_347)
		{
			var _350: f32 = _314.x;
			var _351: f32 = _310 / _350;
			_316 = _351;
		}
		var _352: f32 = _314.z;
		_317 = _352;
	}
	var _353: bool = !_342;
	if (_353)
	{
		var _358: f32 = _314.y;
		var _357: i32 = i32(_358);
		var _359: i32 = 0;
		var _360: i32 = 1;
		var _361: i32 = _22 - _360;
		var _356: i32 = clamp_int(_357, _359, _361);
		_315 = _356;
		var _362: f32 = 1.000000;
		var _363: f32 = _314.x;
		var _364: bool = _363 < _362;
		if (_364)
		{
			var _367: f32 = _314.x;
			var _368: f32 = 1.000000;
			var _369: f32 = _368 - _367;
			var _370: f32 = _314.x;
			var _371: f32 = _310 - _370;
			var _372: f32 = _371 / _369;
			_316 = _372;
		}
		var _373: f32 = _314.w;
		_317 = _373;
	}
	var _376: f32 = f32(_20);
	var _375: f32 = f32(_376);
	var _379: i32 = 256;
	var _380: i32 = _315 % _379;
	var _378: f32 = f32(_380);
	var _377: f32 = f32(_378);
	var _381: f32 = _377 + _313;
	var _382: f32 = _381 / _375;
	var _384: f32 = f32(_21);
	var _383: f32 = f32(_384);
	var _387: i32 = 256;
	var _388: i32 = _315 / _387;
	var _386: f32 = f32(_388);
	var _385: f32 = f32(_386);
	var _389: f32 = _385 + _316;
	var _390: f32 = _389 / _383;
	var _374: vec2<f32> = vec2<f32>(_382, _390);
	var _318: vec2<f32>;
	_318 = _374;
	var _392: f32 = _318.y;
	var _393: f32 = _392 * _17;
	var _391: f32 = sin(_393);
	var _319: f32;
	_319 = _391;
	var _394: f32 = 0.000000;
	var _320: f32;
	_320 = _394;
	var _395: f32 = 0.000001;
	var _396: bool = _319 > _395;
	if (_396)
	{
		var _399: f32 = _19 * _319;
		var _400: f32 = _317 / _399;
		_320 = _400;
	}
	var _402: vec3<f32> = env_dir(_318);
	var _401: vec4<f32> = vec4<f32>(_402, _320);
	return _401;
}

fn clamp_int(_281_in: i32, _282_in: i32, _283_in: i32) -> i32 {
	var _281: i32 = _281_in;
	var _282: i32 = _282_in;
	var _283: i32 = _283_in;
	var _284: bool = _281 < _282;
	if (_284)
	{
		return _282;
	}
	var _287: bool = _281 > _283;
	if (_287)
	{
		return _283;
	}
	return _281;
}

fn env_dir(_290_in: vec2<f32>) -> vec3<f32> {
	var _290: vec2<f32> = _290_in;
	var _294: f32 = _290.y;
	var _295: f32 = _294 * _17;
	var _291: f32;
	_291 = _295;
	var _296: f32 = _set0_1.v1.z;
	var _297: f32 = _290.x;
	var _298: f32 = _297 * _18;
	var _299: f32 = _298 - _17;
	var _300: f32 = _299 - _296;
	var _292: f32;
	_292 = _300;
	var _301: f32 = sin(_291);
	var _293: f32;
	_293 = _301;
	var _303: f32 = cos(_292);
	var _304: f32 = _293 * _303;
	var _305: f32 = sin(_292);
	var _306: f32 = -_293;
	var _307: f32 = _306 * _305;
	var _308: f32 = cos(_291);
	var _302: vec3<f32> = vec3<f32>(_304, _307, _308);
	return _302;
}

fn occluded(_455_in: vec3<f32>, _456_in: vec3<f32>) -> bool {
	var _455: vec3<f32> = _455_in;
	var _456: vec3<f32> = _456_in;
	var _457: _kong_ray;
	_457.origin = _455;
	_457.direction = _456;
	var _459: f32 = 0.000100;
	_457.min = _459;
	var _460: f32 = 100.000000;
	_457.max = _460;
	var _458: _kong_ray_query;
	_458 = _kong_trace(_457, true);
	var _462: bool = _458.hit;
	return _462;
}

fn mis_weight(_445_in: f32, _446_in: f32) -> f32 {
	var _445: f32 = _445_in;
	var _446: f32 = _446_in;
	var _449: f32 = _445 * _445;
	var _447: f32;
	_447 = _449;
	var _450: f32 = _446 * _446;
	var _448: f32;
	_448 = _450;
	var _452: f32 = _447 + _448;
	var _453: f32 = 0.000000;
	var _451: f32 = max(_452, _453);
	var _454: f32 = _447 / _451;
	return _454;
}

fn env_radiance(_271_in: vec3<f32>) -> vec3<f32> {
	var _271: vec3<f32> = _271_in;
	var _275: f32 = _set0_1.v1.z;
	var _274: vec2<f32> = equirect(_271, _275);
	var _272: vec2<f32>;
	_272 = _274;
	var _277: f32 = 0.000000;
	var _276: vec4<f32> = textureSampleLevel(_set0_7, _set0_12, _272, _277);
	var _273: vec4<f32>;
	_273 = _276;
	var _278: f32 = _set0_1.v1.x;
	var _279: vec3<f32> = _273.xyz;
	var _280: vec3<f32> = _279 * _278;
	return _280;
}

fn equirect(_253_in: vec3<f32>, _254_in: f32) -> vec2<f32> {
	var _253: vec3<f32> = _253_in;
	var _254: f32 = _254_in;
	var _259: f32 = _253.z;
	var _260: f32 = -1.000000;
	var _261: f32 = 1.000000;
	var _258: f32 = clamp(_259, _260, _261);
	var _257: f32 = acos(_258);
	var _255: f32;
	_255 = _257;
	var _263: f32 = _253.y;
	var _264: f32 = -_263;
	var _265: f32 = _253.x;
	var _262: f32 = atan2(_264, _265);
	var _266: f32 = _262 + _17;
	var _267: f32 = _266 + _254;
	var _256: f32;
	_256 = _267;
	var _269: f32 = _256 / _18;
	var _270: f32 = _255 / _17;
	var _268: vec2<f32> = vec2<f32>(_269, _270);
	return _268;
}

fn cos_weighted_direction(_228_in: vec3<f32>, _229_in: f32, _230_in: f32) -> vec3<f32> {
	var _228: vec3<f32> = _228_in;
	var _229: f32 = _229_in;
	var _230: f32 = _230_in;
	var _234: tangent_basis = create_basis(_228);
	var _231: tangent_basis;
	_231 = _234;
	var _235: f32 = sqrt(_229);
	var _232: f32;
	_232 = _235;
	var _236: f32 = _18 * _230;
	var _233: f32;
	_233 = _236;
	var _239: f32 = 0.000000;
	var _240: f32 = 1.000000;
	var _241: f32 = _240 - _229;
	var _238: f32 = max(_239, _241);
	var _237: f32 = sqrt(_238);
	var _242: vec3<f32> = _228 * _237;
	var _243: f32 = sin(_233);
	var _244: f32 = _232 * _243;
	var _245: vec3<f32> = _231.binormal;
	var _246: vec3<f32> = _245 * _244;
	var _247: f32 = cos(_233);
	var _248: f32 = _232 * _247;
	var _249: vec3<f32> = _231.tangent;
	var _250: vec3<f32> = _249 * _248;
	var _251: vec3<f32> = _250 + _246;
	var _252: vec3<f32> = _251 + _242;
	return _252;
}

fn create_basis(_188_in: vec3<f32>) -> tangent_basis {
	var _188: vec3<f32> = _188_in;
	var _193: f32 = 1.000000;
	var _189: f32;
	_189 = _193;
	var _194: f32 = 0.000000;
	var _195: f32 = _188.z;
	var _196: bool = _195 < _194;
	if (_196)
	{
		var _199: f32 = -1.000000;
		_189 = _199;
	}
	var _200: f32 = _188.z;
	var _201: f32 = _189 + _200;
	var _202: f32 = -1.000000;
	var _203: f32 = _202 / _201;
	var _190: f32;
	_190 = _203;
	var _204: f32 = _188.y;
	var _205: f32 = _188.x;
	var _206: f32 = _205 * _204;
	var _207: f32 = _206 * _190;
	var _191: f32;
	_191 = _207;
	var _192: tangent_basis;
	var _209: f32 = _188.x;
	var _210: f32 = _188.x;
	var _211: f32 = _189 * _210;
	var _212: f32 = _211 * _209;
	var _213: f32 = _212 * _190;
	var _214: f32 = 1.000000;
	var _215: f32 = _214 + _213;
	var _216: f32 = _189 * _191;
	var _217: f32 = _188.x;
	var _218: f32 = -_189;
	var _219: f32 = _218 * _217;
	var _208: vec3<f32> = vec3<f32>(_215, _216, _219);
	_192.tangent = _208;
	var _221: f32 = _188.y;
	var _222: f32 = _188.y;
	var _223: f32 = _222 * _221;
	var _224: f32 = _223 * _190;
	var _225: f32 = _189 + _224;
	var _226: f32 = _188.y;
	var _227: f32 = -_226;
	var _220: vec3<f32> = vec3<f32>(_191, _225, _227);
	_192.binormal = _220;
	return _192;
}

fn env_pdf(_403_in: vec3<f32>) -> f32 {
	var _403: vec3<f32> = _403_in;
	var _410: f32 = _set0_1.v1.z;
	var _409: vec2<f32> = equirect(_403, _410);
	var _404: vec2<f32>;
	_404 = _409;
	var _414: f32 = f32(_20);
	var _413: f32 = f32(_414);
	var _416: f32 = _404.x;
var _415: f32 = fract(_416);
	var _417: f32 = _415 * _413;
	var _412: i32 = i32(_417);
	var _418: i32 = 0;
	var _419: i32 = 1;
	var _420: i32 = _20 - _419;
	var _411: i32 = clamp_int(_412, _418, _420);
	var _405: i32;
	_405 = _411;
	var _424: f32 = f32(_21);
	var _423: f32 = f32(_424);
	var _425: f32 = _404.y;
	var _426: f32 = _425 * _423;
	var _422: i32 = i32(_426);
	var _427: i32 = 0;
	var _428: i32 = 1;
	var _429: i32 = _21 - _428;
	var _421: i32 = clamp_int(_422, _427, _429);
	var _406: i32;
	_406 = _421;
	var _432: u32 = u32(_405);
	var _433: u32 = u32(_406);
	var _431: vec2<u32> = vec2<u32>(_432, _433);
	var _430: vec4<f32> = textureLoad(_set0_11, vec2<u32>(u32(_431.x), u32(_431.y)), 0);
	var _407: vec4<f32>;
	_407 = _430;
	var _435: f32 = _404.y;
	var _436: f32 = _435 * _17;
	var _434: f32 = sin(_436);
	var _408: f32;
	_408 = _434;
	var _437: f32 = 0.000001;
	var _438: bool = _408 > _437;
	if (_438)
	{
		var _441: f32 = _19 * _408;
		var _442: f32 = _407.z;
		var _443: f32 = _442 / _441;
		return _443;
	}
	var _444: f32 = 0.000000;
	return _444;
}

fn s16_to_f32(_23_in: u32) -> vec2<f32> {
	var _23: u32 = _23_in;
	var _26: i32 = 16;
	var _28: i32 = 16;
	var _29: u32 = _23 << u32(_28);
	var _27: i32 = i32(_29);
	var _30: i32 = _27 >> u32(_26);
	var _24: i32;
	_24 = _30;
	var _31: i32 = 16;
	var _32: i32 = i32(_23);
	var _33: i32 = _32 >> u32(_31);
	var _25: i32;
	_25 = _33;
	var _34: f32 = 32767.000000;
	var _37: f32 = f32(_24);
	var _36: f32 = f32(_37);
	var _39: f32 = f32(_25);
	var _38: f32 = f32(_39);
	var _35: vec2<f32> = vec2<f32>(_36, _38);
	var _40: vec2<f32> = _35 / _34;
	return _40;
}

fn hit_attribute2d(_53_in: vec2<f32>, _54_in: vec2<f32>, _55_in: vec2<f32>, _56_in: vec2<f32>) -> vec2<f32> {
	var _53: vec2<f32> = _53_in;
	var _54: vec2<f32> = _54_in;
	var _55: vec2<f32> = _55_in;
	var _56: vec2<f32> = _56_in;
	var _57: vec2<f32> = _55 - _53;
	var _58: f32 = _56.y;
	var _59: vec2<f32> = _58 * _57;
	var _60: vec2<f32> = _54 - _53;
	var _61: f32 = _56.x;
	var _62: vec2<f32> = _61 * _60;
	var _63: vec2<f32> = _53 + _62;
	var _64: vec2<f32> = _63 + _59;
	return _64;
}

fn hit_attribute(_41_in: vec3<f32>, _42_in: vec3<f32>, _43_in: vec3<f32>, _44_in: vec2<f32>) -> vec3<f32> {
	var _41: vec3<f32> = _41_in;
	var _42: vec3<f32> = _42_in;
	var _43: vec3<f32> = _43_in;
	var _44: vec2<f32> = _44_in;
	var _45: vec3<f32> = _43 - _41;
	var _46: f32 = _44.y;
	var _47: vec3<f32> = _46 * _45;
	var _48: vec3<f32> = _42 - _41;
	var _49: f32 = _44.x;
	var _50: vec3<f32> = _49 * _48;
	var _51: vec3<f32> = _41 + _50;
	var _52: vec3<f32> = _51 + _47;
	return _52;
}

