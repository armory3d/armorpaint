
cbuffer constants {
	float4 params0; // x: erosion, y: deposition, z: lake deposition, w: transport capacity
	float4 params1; // x: talus height difference per cell, y: thermal rate, z: drop to the outside per cell, w: routing slope per cell
	float4 params2; // x: relief, height range over the width, y: flow exponent, z: area exponent, w: cells
};

#ifdef _PACK
tex2d<rgba64> target;
#else
tex2d<rgba128> target;
#endif
tex2d state; // State, the input height for _INIT
#ifdef _PACK
tex2d source; // Input height
#endif

float2 get_offset(int i) {
	if (i == 0) {
		return float2(-1.0, 0.0);
	}
	if (i == 1) {
		return float2(1.0, 0.0);
	}
	if (i == 2) {
		return float2(0.0, 1.0);
	}
	if (i == 3) {
		return float2(0.0, -1.0);
	}
	if (i == 4) {
		return float2(-1.0, 1.0);
	}
	if (i == 5) {
		return float2(1.0, 1.0);
	}
	if (i == 6) {
		return float2(1.0, -1.0);
	}
	return float2(-1.0, -1.0);
}

// Random per cell, so the water routed over flats meanders instead of running straight
float jitter(int x, int y) {
	return 0.25 + frac(sin(float(x) * 12.9898 + float(y) * 78.233) * 43758.5453) * 1.5;
}

float4 load_state(int x, int y) {
	return state[uint2(uint(x), uint(y))];
}

// Height across the edge cell x, y from the outside cell at offset ox, oy, the edge erodes as the cells inside
float mirror_height(int x, int y, int ox, int oy, int w, int h) {
	int ix = x - ox;
	int iy = y - oy;
	if (ix < 0 || ix >= w) {
		ix = x;
	}
	if (iy < 0 || iy >= h) {
		iy = y;
	}
	return load_state(ix, iy).r;
}

// Routing surface of the neighbor, cells outside of the map are a bit lower so the water leaves
float routing(int x, int y, int w, int h, float own, float d) {
	if (x < 0 || y < 0 || x >= w || y >= h) {
		return own - constants.params1.z * d;
	}
	return load_state(x, y).a;
}

// Sum of the outflow weights of a cell to its lower neighbors
float outflow_total(int x, int y, int w, int h, float height, float surface) {
	float total = 0.0;
	for (int i = 0; i < 8; i += 1) {
		float2 o = get_offset(i);
		float  d = length(o);
		float  n = routing(x + int(o.x), y + int(o.y), w, h, height, d);
		total += pow(max((surface - n) / d, 0.0), constants.params2.y);
	}
	return total;
}

#ifdef _INIT
#[compute, threads(8, 8, 1)]
void terrain_init() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(state);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	float4 h      = state[id.xy];
	float  v      = max(h.r, 0.0) + min(h.r, 0.0); // Nan as 0
	target[id.xy] = float4(v, 1.0, 0.0, 16.0);     // Routing surface comes down from above
}
#endif

#ifdef _FILL
// Routing surface towards the height filled up to the outlet of each depression
#[compute, threads(8, 8, 1)]
void terrain_fill() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(state);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	int    w    = int(dim.x);
	int    hh   = int(dim.y);
	int    x    = int(id.x);
	int    y    = int(id.y);
	float4 c    = state[id.xy];
	float  fill = c.a;
	float  j    = jitter(x, y);
	for (int i = 0; i < 8; i += 1) {
		float2 o = get_offset(i);
		float  d = length(o);
		fill     = min(fill, routing(x + int(o.x), y + int(o.y), w, hh, c.r, d) + constants.params1.w * d * j);
	}
	target[id.xy] = float4(c.r, c.g, c.b, max(c.r, fill));
}
#endif

#ifdef _FLOW
// Water and sediment gathered from the higher neighbors, stream power erosion and deposition
#[compute, threads(8, 8, 1)]
void terrain_flow() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(state);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	int    w        = int(dim.x);
	int    hh       = int(dim.y);
	int    x        = int(id.x);
	int    y        = int(id.y);
	float4 c        = state[id.xy];
	float  h        = c.r;
	float  surface  = c.a;
	float  area_in  = 0.0;
	float  sed_in   = 0.0;
	float  fill     = surface;
	float  slope    = 0.0;
	float  down     = h;
	float  down_d   = 1.0;
	float  j        = jitter(x, y);
	for (int i = 0; i < 8; i += 1) {
		float2 o  = get_offset(i);
		float  d  = length(o);
		int    nx = x + int(o.x);
		int    ny = y + int(o.y);
		float  n  = h - constants.params1.z * d;
		float  to = min(mirror_height(x, y, int(o.x), int(o.y), w, hh), h); // Water leaves the map, the edge goes down with the inside
		if (nx >= 0 && ny >= 0 && nx < w && ny < hh) {
			float4 s = load_state(nx, ny);
			n        = s.a;
			to       = s.a;
			if (s.a > surface) {
				float part = pow((s.a - surface) / d, constants.params2.y) / outflow_total(nx, ny, w, hh, s.r, s.a);
				area_in += s.g * part;
				sed_in += s.b * part;
			}
		}
		fill    = min(fill, n + constants.params1.w * d * j);
		float s = (surface - n) / d;
		if (s > slope) {
			slope  = s;
			down   = to;
			down_d = d;
		}
	}
	float area  = 1.0 + area_in;
	fill        = max(h, fill);
	float lake  = fill - h;
	float erode = 0.0;
	if (lake <= 0.0 && h > down) {
		// Implicit in the new height, stable for any rate
		float f = constants.params0.x * pow(area, constants.params2.z) / down_d;
		erode   = (h - down) * f / (1.0 + f);
	}
	float capacity = constants.params0.w * sqrt(area) * slope * constants.params2.x;
	erode          = erode * clamp(1.0 - sed_in / max(capacity, 0.000000000001), 0.0, 1.0);
	float deposit  = constants.params0.y * max(sed_in - capacity, 0.0);
	if (lake > 0.0) {
		deposit = min(constants.params0.z * sed_in, lake);
	}
	target[id.xy] = float4(h + deposit - erode, area, max(sed_in + erode - deposit, 0.0), fill);
}
#endif

#ifdef _THERMAL
// Slopes steeper than the talus angle slide down, pairwise so no material is lost
#[compute, threads(8, 8, 1)]
void terrain_thermal() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(state);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	int    w  = int(dim.x);
	int    hh = int(dim.y);
	int    x  = int(id.x);
	int    y  = int(id.y);
	float4 c  = state[id.xy];
	float  t  = 0.0;
	for (int i = 0; i < 8; i += 1) {
		float2 o  = get_offset(i);
		float  d  = length(o);
		int    nx = x + int(o.x);
		int    ny = y + int(o.y);
		float n  = mirror_height(x, y, int(o.x), int(o.y), w, hh);
		if (nx >= 0 && ny >= 0 && nx < w && ny < hh) {
			n = load_state(nx, ny).r;
		}
		float dh = c.r - n;
		float tl = constants.params1.x * d;
		t += max(0.0 - dh - tl, 0.0) - max(dh - tl, 0.0);
	}
	target[id.xy] = float4(c.r + t * constants.params1.y, c.g, c.b, c.a);
}
#endif

#ifdef _PACK
// Height change, flow, slope of the surface and lake depth for the material
float surface_height(int x, int y) {
	float4 s = load_state(x, y);
	return max(s.r, s.a);
}

#[compute, threads(8, 8, 1)]
void terrain_pack() {
	uint3 id  = dispatch_thread_id();
	uint2 dim = texture_size(state);
	if (id.x >= dim.x || id.y >= dim.y) {
		return;
	}
	int    w  = int(dim.x);
	int    hh = int(dim.y);
	int    x  = int(id.x);
	int    y  = int(id.y);
	float4 c  = state[id.xy];
	int    x0 = x - 1;
	int    x1 = x + 1;
	int    y0 = y - 1;
	int    y1 = y + 1;
	if (x0 < 0) {
		x0 = 0;
	}
	if (x1 >= w) {
		x1 = w - 1;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (y1 >= hh) {
		y1 = hh - 1;
	}
	float  gx     = (surface_height(x1, y) - surface_height(x0, y)) / float(x1 - x0);
	float  gy     = (surface_height(x, y1) - surface_height(x, y0)) / float(y1 - y0);
	float  g      = length(float2(gx, gy)) * constants.params2.x * constants.params2.w;
	float  slope  = g / sqrt(1.0 + g * g); // Sine of the slope angle
	float  flow   = clamp(log(c.g) / log(constants.params2.w * constants.params2.w) * 1.5, 0.0, 1.0);
	float4 h0     = source[id.xy];
	target[id.xy] = float4(c.r - h0.r, flow, slope, max(c.a - c.r, 0.0));
}
#endif
