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

const _14: i32 = 16;

const _15: i32 = 2;

const _16: i32 = 0;

const _17: i32 = 1;

const _18: i32 = 2;

const _19: i32 = 3;

const _20: i32 = 5;

const _21: i32 = 2;

const _22: f32 = 0.500000;

const _23: f32 = 0.050000;

const _24: f32 = 3.14159274;

const _25: f32 = 6.28318548;

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
	var _712: vec3<u32> = _kong_dispatch_thread_id;
	var _704: vec3<u32>;
	_704 = _712;
	var _713: vec2<u32> = textureDimensions(_set0_3);
	var _705: vec2<u32>;
	_705 = _713;
	var _714: u32 = _705.y;
	var _715: u32 = _704.y;
	var _716: bool = _715 >= _714;
	var _717: u32 = _705.x;
	var _718: u32 = _704.x;
	var _719: bool = _718 >= _717;
	var _720: bool = _719 || _716;
	if (_720)
	{
		return;
	}
	var _724: f32 = _set0_1.eye.w;
	var _723: i32 = i32(_724);
	var _706: i32;
	_706 = _723;
	var _726: vec2<u32> = _704.xy;
	var _725: sampler_cache = init_sampler(_726, _706);
	var _707: sampler_cache;
	_707 = _725;
	var _728: f32 = 0.000000;
	var _729: f32 = 0.000000;
	var _730: f32 = 0.000000;
	var _727: vec3<f32> = vec3<f32>(_728, _729, _730);
	var _708: vec3<f32>;
	_708 = _727;
	{
		var _734: i32 = 0;
		var _731: i32;
		_731 = _734;
		while (true)
{
		var _738: bool = _731 < _13;
		if (!_738) { break; }
		{
			var _745: i32 = _706 * _13;
			var _746: i32 = _745 + _731;
			var _739: i32;
			_739 = _746;
			var _748: vec2<u32> = _704.xy;
			var _747: vec2<f32> = vec2<f32>(_748);
			var _740: vec2<f32>;
			_740 = _747;
			var _750: vec2<u32> = _704.xy;
			var _751: i32 = 0;
			var _749: f32 = rnd(_750, _739, _751, _707, _706);
			_740.x += _749;
			var _753: vec2<u32> = _704.xy;
			var _754: i32 = 1;
			var _752: f32 = rnd(_753, _739, _754, _707, _706);
			_740.y += _752;
			var _741: _kong_ray;
			var _755: f32 = 0.000100;
			_741.min = _755;
			var _756: f32 = 100.000000;
			_741.max = _756;
			var _757: vec3<f32> = _set0_1.eye.xyz;
			_741.origin = _757;
			var _759: f32 = 1.000000;
			var _760: f32 = 2.000000;
			var _761: vec2<f32> = vec2<f32>(_705);
			var _762: vec2<f32> = _740 / _761;
			var _763: vec2<f32> = _762 * _760;
			var _764: vec2<f32> = _763 - _759;
			var _765: vec3<f32> = _set0_1.eye.xyz;
			var _758: vec3<f32> = camera_ray_direction(_764, _765);
			_741.direction = _758;
			var _767: f32 = 1.000000;
			var _768: f32 = 1.000000;
			var _769: f32 = 1.000000;
			var _766: vec3<f32> = vec3<f32>(_767, _768, _769);
			var _742: vec3<f32>;
			_742 = _766;
			{
				var _773: i32 = 0;
				var _770: i32;
				_770 = _773;
				while (true)
{
				var _777: bool = _770 < _14;
				if (!_777) { break; }
				{
					var _831: i32 = _770 * _20;
					var _832: i32 = _15 + _831;
					var _778: i32;
					_778 = _832;
					var _833: bool = _770 >= _21;
					if (_833)
					{
						var _840: f32 = _742.x;
						var _841: f32 = _742.y;
						var _839: f32 = max(_840, _841);
						var _842: f32 = _742.z;
						var _838: f32 = max(_839, _842);
						var _837: f32 = clamp(_838, _23, _22);
						var _834: f32;
						_834 = _837;
						var _844: vec2<u32> = _704.xy;
						var _845: i32 = _778 + _16;
						var _843: f32 = rnd(_844, _739, _845, _707, _706);
						var _846: bool = _843 > _834;
						if (_846)
						{
							break;
						}
						var _849: vec3<f32> = _742 / _834;
						_742 = _849;
					}
					var _779: _kong_ray_query;
					_779 = _kong_trace(_741, false);
					var _851: bool = _779.hit;
					var _852: bool = !_851;
					if (_852)
					{
						var _853: vec3<f32>;
						var _857: f32 = 1.000000;
						var _854: f32;
						_854 = _857;
						var _858: f32 = 0.000000;
						var _859: f32 = _set0_1.params.x;
						var _860: bool = _859 < _858;
						var _861: i32 = 0;
						var _862: bool = _770 == _861;
						var _863: bool = _862 && _860;
						if (_863)
						{
							var _867: f32 = 0.027500;
							var _868: f32 = 0.027500;
							var _869: f32 = 0.027500;
							var _866: vec3<f32> = vec3<f32>(_867, _868, _869);
							_853 = _866;
						}
						var _870: bool = !_863;
						if (_870)
						{
							var _874: vec3<f32> = _741.direction;
							var _873: vec3<f32> = env_radiance(_874);
							_853 = _873;
						}
						var _876: vec3<f32> = _742 * _853;
						var _877: vec3<f32> = _876 * _854;
						var _878: f32 = 0.000000;
						var _879: vec3<f32> = vec3<f32>(_878, _878, _878);
						var _880: f32 = 8.000000;
						var _881: vec3<f32> = vec3<f32>(_880, _880, _880);
						var _875: vec3<f32> = clamp(_877, _879, _881);
						_708 += _875;
						break;
					}
					var _882: u32 = _kong_instances[_779.instance].geometry;
					var _780: u32;
					_780 = _882;
					var _883: vec2<f32> = _779.barycentrics;
					var _781: vec2<f32>;
					_781 = _883;
					var _885: i32 = 0;
					var _884: vec4<u32> = _kong_vertices[_kong_indices[_779.primitive * 3u + u32(_885)]];
					var _782: vec4<u32>;
					_782 = _884;
					var _887: i32 = 1;
					var _886: vec4<u32> = _kong_vertices[_kong_indices[_779.primitive * 3u + u32(_887)]];
					var _783: vec4<u32>;
					_783 = _886;
					var _889: i32 = 2;
					var _888: vec4<u32> = _kong_vertices[_kong_indices[_779.primitive * 3u + u32(_889)]];
					var _784: vec4<u32>;
					_784 = _888;
					var _891: u32 = _782.w;
					var _890: vec2<f32> = s16_to_f32(_891);
					var _785: vec2<f32>;
					_785 = _890;
					var _893: u32 = _783.w;
					var _892: vec2<f32> = s16_to_f32(_893);
					var _786: vec2<f32>;
					_786 = _892;
					var _895: u32 = _784.w;
					var _894: vec2<f32> = s16_to_f32(_895);
					var _787: vec2<f32>;
					_787 = _894;
					var _896: f32 = _set0_1.params.z;
					var _897: vec2<f32> = hit_attribute2d(_785, _786, _787, _781);
					var _898: vec2<f32> = _897 * _896;
					var _788: vec2<f32>;
					_788 = _898;
					var _899: vec2<u32> = textureDimensions(_kong_geometry_texture0);
					var _789: vec2<u32>;
					_789 = _899;
					var _901: vec2<f32> = vec2<f32>(_789);
var _902: vec2<f32> = fract(_788);
					var _903: vec2<f32> = _902 * _901;
					var _900: vec2<u32> = vec2<u32>(_903);
					var _790: vec2<u32>;
					_790 = _900;
					var _904: vec4<f32> = textureLoad(_kong_geometry_texture0, _790, 0);
					var _791: vec4<f32>;
					_791 = _904;
					var _905: f32 = _779.t;
					var _792: f32;
					_792 = _905;
					var _906: vec3<f32> = _741.direction;
					var _907: vec3<f32> = _906 * _792;
					var _908: vec3<f32> = _741.origin;
					var _909: vec3<f32> = _908 + _907;
					var _793: vec3<f32>;
					_793 = _909;
					var _911: u32 = _782.y;
					var _910: vec2<f32> = s16_to_f32(_911);
					var _794: vec2<f32>;
					_794 = _910;
					var _913: u32 = _783.y;
					var _912: vec2<f32> = s16_to_f32(_913);
					var _795: vec2<f32>;
					_795 = _912;
					var _915: u32 = _784.y;
					var _914: vec2<f32> = s16_to_f32(_915);
					var _796: vec2<f32>;
					_796 = _914;
					var _918: u32 = _782.x;
					var _917: vec2<f32> = s16_to_f32(_918);
					var _919: f32 = _794.x;
					var _916: vec3<f32> = vec3<f32>(_917, _919);
					var _797: vec3<f32>;
					_797 = _916;
					var _922: u32 = _783.x;
					var _921: vec2<f32> = s16_to_f32(_922);
					var _923: f32 = _795.x;
					var _920: vec3<f32> = vec3<f32>(_921, _923);
					var _798: vec3<f32>;
					_798 = _920;
					var _926: u32 = _784.x;
					var _925: vec2<f32> = s16_to_f32(_926);
					var _927: f32 = _796.x;
					var _924: vec3<f32> = vec3<f32>(_925, _927);
					var _799: vec3<f32>;
					_799 = _924;
					var _930: u32 = _782.z;
					var _929: vec2<f32> = s16_to_f32(_930);
					var _931: f32 = _794.y;
					var _928: vec3<f32> = vec3<f32>(_929, _931);
					var _800: vec3<f32>;
					_800 = _928;
					var _934: u32 = _783.z;
					var _933: vec2<f32> = s16_to_f32(_934);
					var _935: f32 = _795.y;
					var _932: vec3<f32> = vec3<f32>(_933, _935);
					var _801: vec3<f32>;
					_801 = _932;
					var _938: u32 = _784.z;
					var _937: vec2<f32> = s16_to_f32(_938);
					var _939: f32 = _796.y;
					var _936: vec3<f32> = vec3<f32>(_937, _939);
					var _802: vec3<f32>;
					_802 = _936;
					var _941: vec3<f32> = hit_attribute(_800, _801, _802, _781);
					var _940: vec3<f32> = normalize(_941);
					var _803: vec3<f32>;
					_803 = _940;
					var _943: vec3<f32> = _798 - _797;
					var _944: vec3<f32> = _799 - _797;
					var _942: vec3<f32> = cross(_943, _944);
					var _804: vec3<f32>;
					_804 = _942;
					var _945: f32 = 0.000000;
					var _946: f32 = dot(_804, _804);
					var _947: bool = _946 > _945;
					if (_947)
					{
						var _950: vec3<f32> = normalize(_804);
						_804 = _950;
					}
					var _951: bool = !_947;
					if (_951)
					{
						_804 = _803;
					}
					var _954: f32 = 0.000000;
					var _955: f32 = dot(_804, _803);
					var _956: bool = _955 < _954;
					if (_956)
					{
						var _959: vec3<f32> = -_804;
						_804 = _959;
					}
					var _805: vec3<f32>;
					_805 = _803;
					var _960: mat3x3<f32> = _kong_instances[_779.instance].object_to_world;
					var _806: mat3x3<f32>;
					_806 = _960;
					var _962: vec3<f32> = _806 * _803;
					var _961: vec3<f32> = normalize(_962);
					_803 = _961;
					var _964: vec3<f32> = _806 * _804;
					var _963: vec3<f32> = normalize(_964);
					_804 = _963;
					var _965: f32 = 0.000000;
					var _967: vec3<f32> = _741.direction;
					var _966: f32 = dot(_804, _967);
					var _968: bool = _966 > _965;
					var _807: bool;
					_807 = _968;
					if (_807)
					{
						var _971: vec3<f32> = -_804;
						_804 = _971;
						var _972: vec3<f32> = -_803;
						_803 = _972;
					}
					var _973: bool = true;
					var _808: bool;
					_808 = _973;
					var _975: f32 = 0.000000;
					var _976: f32 = 0.000000;
					var _977: f32 = 0.000000;
					var _978: f32 = 0.000000;
					var _974: vec4<f32> = vec4<f32>(_975, _976, _977, _978);
					var _809: vec4<f32>;
					_809 = _974;
					if (_808)
					{
						var _981: vec4<f32> = textureLoad(_kong_geometry_texture1, _790, 0);
						_809 = _981;
					}
					var _983: vec3<f32> = _791.xyz;
					var _982: vec3<f32> = srgb_to_linear(_983);
					var _810: vec3<f32>;
					_810 = _982;
					var _984: bool = _779.front_face;
					var _985: bool = !_984;
					if (_985)
					{
						var _990: f32 = 0.001000;
						var _991: vec3<f32> = vec3<f32>(_990, _990, _990);
						var _989: vec3<f32> = max(_810, _991);
						var _992: f32 = _791.w;
						var _993: f32 = _792 * _992;
						var _994: vec3<f32> = vec3<f32>(_993, _993, _993);
						var _988: vec3<f32> = pow(_989, _994);
						_742 *= _988;
					}
					var _995: i32 = 1;
					var _996: i32 = 3;
					var _998: f32 = 255.000000;
					var _999: f32 = _809.w;
					var _1000: f32 = _999 * _998;
					var _997: i32 = i32(_1000);
					var _1001: i32 = _997 % _996;
					var _1002: bool = _1001 == _995;
					if (_1002)
					{
						var _1005: f32 = 100.000000;
						var _1006: vec3<f32> = _742 * _810;
						var _1007: vec3<f32> = _1006 * _1005;
						_708 += _1007;
						break;
					}
					var _1008: vec4<f32> = textureLoad(_kong_geometry_texture2, _790, 0);
					var _811: vec4<f32>;
					_811 = _1008;
					var _1010: vec2<u32> = _704.xy;
					var _1011: i32 = _778 + _17;
					var _1009: f32 = rnd(_1010, _739, _1011, _707, _706);
					var _812: f32;
					_812 = _1009;
					var _1012: f32 = _791.w;
					var _1013: bool = _812 > _1012;
					if (_1013)
					{
						var _1019: vec3<f32> = _741.direction;
						var _1018: tangent_basis = create_basis(_1019);
						var _1014: tangent_basis;
						_1014 = _1018;
						var _1021: vec3<f32> = _1014.tangent;
						var _1022: vec3<f32> = _1014.binormal;
						var _1023: vec3<f32> = _741.direction;
						var _1025: vec2<u32> = _704.xy;
						var _1026: i32 = _778 + _19;
						var _1024: f32 = rnd(_1025, _739, _1026, _707, _706);
						var _1028: vec2<u32> = _704.xy;
						var _1029: i32 = 1;
						var _1030: i32 = _778 + _19;
						var _1031: i32 = _1030 + _1029;
						var _1027: f32 = rnd(_1028, _739, _1031, _707, _706);
						var _1020: vec3<f32> = cos_weighted_direction(_1021, _1022, _1023, _1024, _1027);
						var _1015: vec3<f32>;
						_1015 = _1020;
						var _1034: vec3<f32> = _741.direction;
						var _1035: f32 = 0.500000;
						var _1036: f32 = _811.y;
						var _1037: f32 = _811.y;
						var _1038: f32 = _1037 * _1036;
						var _1039: f32 = _1038 * _1035;
						var _1040: vec3<f32> = vec3<f32>(_1039, _1039, _1039);
var _1033: vec3<f32> = mix(_1034, _1015, _1040);
						var _1032: vec3<f32> = normalize(_1033);
						_741.direction = _1032;
						var _1042: vec3<f32> = _741.direction;
						var _1041: vec3<f32> = offset_ray(_793, _804, _1042);
						_741.origin = _1041;
						var _1043: i32 = 1;
						_770 += _1043;
						continue;
					}
					var _1045: vec2<u32> = _704.xy;
					var _1046: i32 = _778 + _18;
					var _1044: f32 = rnd(_1045, _739, _1046, _707, _706);
					_812 = _1044;
					var _813: vec3<f32>;
					var _814: vec3<f32>;
					if (_808)
					{
						var _1051: tangent_basis = create_uv_basis(_797, _798, _799, _785, _786, _787, _805);
						var _1047: tangent_basis;
						_1047 = _1051;
						var _1052: vec3<f32> = _1047.tangent;
						var _1053: vec3<f32> = _806 * _1052;
						_813 = _1053;
						var _1054: vec3<f32> = _1047.binormal;
						var _1055: vec3<f32> = _806 * _1054;
						_814 = _1055;
						var _1057: f32 = dot(_803, _813);
						var _1058: vec3<f32> = _803 * _1057;
						var _1059: vec3<f32> = _813 - _1058;
						var _1056: vec3<f32> = normalize(_1059);
						_813 = _1056;
						var _1061: f32 = dot(_813, _814);
						var _1062: vec3<f32> = _813 * _1061;
						var _1063: f32 = dot(_803, _814);
						var _1064: vec3<f32> = _803 * _1063;
						var _1065: vec3<f32> = _814 - _1064;
						var _1066: vec3<f32> = _1065 - _1062;
						var _1060: vec3<f32> = normalize(_1066);
						_814 = _1060;
						if (_807)
						{
							var _1069: vec3<f32> = -_814;
							_814 = _1069;
						}
						var _1071: f32 = 1.000000;
						var _1072: f32 = 2.000000;
						var _1073: vec3<f32> = _809.xyz;
						var _1074: vec3<f32> = _1073 * _1072;
						var _1075: vec3<f32> = _1074 - _1071;
						var _1070: vec3<f32> = normalize(_1075);
						var _1048: vec3<f32>;
						_1048 = _1070;
						var _1077: f32 = _1048.z;
						var _1078: vec3<f32> = _803 * _1077;
						var _1079: f32 = _1048.y;
						var _1080: vec3<f32> = _814 * _1079;
						var _1081: f32 = _1048.x;
						var _1082: vec3<f32> = _813 * _1081;
						var _1083: vec3<f32> = _1082 - _1080;
						var _1084: vec3<f32> = _1083 + _1078;
						var _1076: vec3<f32> = normalize(_1084);
						_803 = _1076;
						var _1085: f32 = 0.000100;
						var _1086: f32 = dot(_803, _804);
						var _1087: bool = _1086 < _1085;
						if (_1087)
						{
							var _1091: f32 = dot(_803, _804);
							var _1092: f32 = 0.000100;
							var _1093: f32 = _1092 - _1091;
							var _1094: vec3<f32> = _804 * _1093;
							var _1095: vec3<f32> = _803 + _1094;
							var _1090: vec3<f32> = normalize(_1095);
							_803 = _1090;
						}
					}
					var _1096: vec3<f32> = _741.direction;
					var _1097: vec3<f32> = -_1096;
					var _815: vec3<f32>;
					_815 = _1097;
					var _1098: f32 = dot(_803, _815);
					var _816: f32;
					_816 = _1098;
					var _1099: f32 = 0.001000;
					var _1100: bool = _816 < _1099;
					if (_1100)
					{
						var _1104: i32 = 0;
						var _1105: bool = _770 > _1104;
						if (_1105)
						{
							break;
						}
						var _1109: f32 = 0.001000;
						var _1110: f32 = _1109 - _816;
						var _1111: vec3<f32> = _815 * _1110;
						var _1112: vec3<f32> = _803 + _1111;
						var _1108: vec3<f32> = normalize(_1112);
						_803 = _1108;
						var _1113: f32 = dot(_803, _804);
						var _1101: f32;
						_1101 = _1113;
						var _1114: f32 = 0.001000;
						var _1115: bool = _1101 < _1114;
						if (_1115)
						{
							var _1119: f32 = 0.001000;
							var _1120: f32 = _1119 - _1101;
							var _1121: vec3<f32> = _804 * _1120;
							var _1122: vec3<f32> = _803 + _1121;
							var _1118: vec3<f32> = normalize(_1122);
							_803 = _1118;
						}
						var _1124: f32 = dot(_803, _815);
						var _1125: f32 = 0.001000;
						var _1123: f32 = max(_1124, _1125);
						_816 = _1123;
					}
					var _1126: tangent_basis = create_basis(_803);
					var _817: tangent_basis;
					_817 = _1126;
					var _1127: vec3<f32> = _817.tangent;
					_813 = _1127;
					var _1128: vec3<f32> = _817.binormal;
					_814 = _1128;
					var _1130: f32 = _811.z;
					var _1129: vec3<f32> = surface_albedo(_810, _1130);
					var _818: vec3<f32>;
					_818 = _1129;
					var _1132: f32 = _811.z;
					var _1131: vec3<f32> = surface_specular(_810, _1132);
					var _819: vec3<f32>;
					_819 = _1131;
					var _1133: f32 = _811.y;
					var _820: f32;
					_820 = _1133;
					var _1134: vec3<f32> = spec_directional_albedo(_819, _816, _820);
					var _821: vec3<f32>;
					_821 = _1134;
					var _1135: f32 = 1.000000;
					var _1136: vec3<f32> = _1135 - _821;
					var _1137: vec3<f32> = _818 * _1136;
					var _822: vec3<f32>;
					_822 = _1137;
					var _1138: f32 = luma(_821);
					var _823: f32;
					_823 = _1138;
					var _1139: f32 = luma(_822);
					var _824: f32;
					_824 = _1139;
					var _1142: f32 = _823 + _824;
					var _1143: f32 = 0.000010;
					var _1141: f32 = max(_1142, _1143);
					var _1144: f32 = _823 / _1141;
					var _1145: f32 = 0.050000;
					var _1146: f32 = 0.995000;
					var _1140: f32 = clamp(_1144, _1145, _1146);
					var _825: f32;
					_825 = _1140;
					var _1148: f32 = _820 * _820;
					var _1149: f32 = 0.001000;
					var _1147: f32 = max(_1148, _1149);
					var _826: f32;
					_826 = _1147;
					var _1151: vec2<u32> = _704.xy;
					var _1152: i32 = _778 + _19;
					var _1150: f32 = rnd(_1151, _739, _1152, _707, _706);
					var _827: f32;
					_827 = _1150;
					var _1154: vec2<u32> = _704.xy;
					var _1155: i32 = 1;
					var _1156: i32 = _778 + _19;
					var _1157: i32 = _1156 + _1155;
					var _1153: f32 = rnd(_1154, _739, _1157, _707, _706);
					var _828: f32;
					_828 = _1153;
					var _1158: bool = _812 < _825;
					if (_1158)
					{
						var _1168: f32 = _826 * _826;
						var _1159: f32;
						_1159 = _1168;
						var _1170: f32 = dot(_815, _813);
						var _1171: f32 = dot(_815, _814);
						var _1169: vec3<f32> = vec3<f32>(_1170, _1171, _816);
						var _1160: vec3<f32>;
						_1160 = _1169;
						var _1172: vec3<f32> = sample_ggx_vndf(_1160, _826, _827, _828);
						var _1161: vec3<f32>;
						_1161 = _1172;
						var _1174: vec3<f32> = -_1160;
						var _1173: vec3<f32> = reflect(_1174, _1161);
						var _1162: vec3<f32>;
						_1162 = _1173;
						var _1175: f32 = 0.000000;
						var _1176: f32 = _1162.z;
						var _1177: bool = _1176 <= _1175;
						if (_1177)
						{
							break;
						}
						var _1180: f32 = _1162.z;
						var _1181: vec3<f32> = _803 * _1180;
						var _1182: f32 = _1162.y;
						var _1183: vec3<f32> = _814 * _1182;
						var _1184: f32 = _1162.x;
						var _1185: vec3<f32> = _813 * _1184;
						var _1186: vec3<f32> = _1185 + _1183;
						var _1187: vec3<f32> = _1186 + _1181;
						_741.direction = _1187;
						var _1189: f32 = _1160.z;
						var _1188: f32 = smith_lambda(_1189, _1159);
						var _1163: f32;
						_1163 = _1188;
						var _1191: f32 = _1162.z;
						var _1190: f32 = smith_lambda(_1191, _1159);
						var _1164: f32;
						_1164 = _1190;
						var _1192: f32 = 1.000000;
						var _1193: f32 = _1192 + _1163;
						var _1194: f32 = _1193 + _1164;
						var _1195: f32 = 1.000000;
						var _1196: f32 = _1195 + _1163;
						var _1197: f32 = _1196 / _1194;
						var _1165: f32;
						_1165 = _1197;
						var _1198: f32 = _1165 / _825;
						var _1201: f32 = dot(_1160, _1161);
						var _1202: f32 = 0.000000;
						var _1200: f32 = max(_1201, _1202);
						var _1199: vec3<f32> = f_schlick(_819, _1200);
						var _1203: vec3<f32> = _1199 * _1198;
						_742 *= _1203;
					}
					var _1204: bool = !_1158;
					if (_1204)
					{
						var _1207: vec3<f32> = cos_weighted_direction(_813, _814, _803, _827, _828);
						_741.direction = _1207;
						var _1208: f32 = 1.000000;
						var _1209: f32 = _1208 - _825;
						var _1210: vec3<f32> = _822 / _1209;
						_742 *= _1210;
					}
					var _1211: f32 = 0.000000;
					var _1213: vec3<f32> = _741.direction;
					var _1212: f32 = dot(_1213, _804);
					var _1214: bool = _1212 <= _1211;
					if (_1214)
					{
						break;
					}
					var _1217: f32 = 0.000000;
					var _1220: f32 = _742.x;
					var _1221: f32 = _742.y;
					var _1219: f32 = max(_1220, _1221);
					var _1222: f32 = _742.z;
					var _1218: f32 = max(_1219, _1222);
					var _1223: bool = _1218 <= _1217;
					if (_1223)
					{
						break;
					}
					var _1227: vec3<f32> = _741.direction;
					var _1226: vec3<f32> = offset_ray(_793, _804, _1227);
					_741.origin = _1226;
					var _1228: i32 = 2;
					var _1229: i32 = 3;
					var _1231: f32 = 255.000000;
					var _1232: f32 = _809.w;
					var _1233: f32 = _1232 * _1231;
					var _1230: i32 = i32(_1233);
					var _1234: i32 = _1230 % _1229;
					var _1235: bool = _1234 == _1228;
					if (_1235)
					{
						var _1240: f32 = 10.000000;
						var _1242: f32 = 2.000000;
						var _1243: f32 = _792 * _1242;
						var _1244: f32 = 1.000000;
						var _1241: f32 = min(_1243, _1244);
						var _1245: f32 = 1.000000;
						var _1246: f32 = _1245 / _1241;
						var _1247: f32 = _1246 / _1240;
						var _1248: f32 = 0.500000;
						var _1239: f32 = min(_1247, _1248);
						var _1236: f32;
						_1236 = _1239;
						var _1249: vec3<f32> = _742 * _1236;
						_742 += _1249;
						var _1250: f32 = 0.500000;
						var _1251: bool = _812 < _1250;
						if (_1251)
						{
							var _1254: f32 = 0.001000;
							var _1255: vec3<f32> = _741.direction;
							var _1256: vec3<f32> = _1255 * _812;
							var _1257: vec3<f32> = _1256 * _1254;
							_741.origin += _1257;
						}
					}
					var _1258: i32 = 1;
					_770 += _1258;
				}
				}
			}
			var _1259: i32 = 1;
			_731 += _1259;
		}
		}
	}
	var _1261: vec2<u32> = _704.xy;
	var _1260: vec4<f32> = textureLoad(_kong_prev, vec2<u32>(u32(_1261.x), u32(_1261.y)), 0);
	var _709: vec4<f32>;
	_709 = _1260;
	var _1262: vec3<f32> = _709.xyz;
	var _710: vec3<f32>;
	_710 = _1262;
	var _1264: f32 = f32(_13);
	var _1263: f32 = f32(_1264);
	var _1265: vec3<f32> = _708 / _1263;
	_708 = _1265;
	var _1266: f32 = 1.000000;
	var _1267: f32 = _set0_1.eye.w;
	var _1268: f32 = _1267 + _1266;
	var _1269: f32 = 1.000000;
	var _1270: f32 = _1269 / _1268;
	var _711: f32;
	_711 = _1270;
	var _1272: vec3<f32> = vec3<f32>(_711, _711, _711);
var _1271: vec3<f32> = mix(_710, _708, _1272);
	_710 = _1271;
	var _1274: f32 = 1.000000;
	var _1273: vec4<f32> = vec4<f32>(_710, _1274);
	var _1275: vec2<u32> = _704.xy;
	textureStore(_set0_3, vec2<u32>(u32(_1275.x), u32(_1275.y)), _1273);
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
	var _207: f32 = _203 + _24;
	var _208: f32 = _207 + _195;
	var _197: f32;
	_197 = _208;
	var _210: f32 = _197 / _25;
	var _211: f32 = _196 / _24;
	var _209: vec2<f32> = vec2<f32>(_210, _211);
	return _209;
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

fn cos_weighted_direction(_447_in: vec3<f32>, _448_in: vec3<f32>, _449_in: vec3<f32>, _450_in: f32, _451_in: f32) -> vec3<f32> {
	var _447: vec3<f32> = _447_in;
	var _448: vec3<f32> = _448_in;
	var _449: vec3<f32> = _449_in;
	var _450: f32 = _450_in;
	var _451: f32 = _451_in;
	var _454: f32 = sqrt(_450);
	var _452: f32;
	_452 = _454;
	var _455: f32 = _25 * _451;
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
	var _515: f32 = _25 * _473;
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

