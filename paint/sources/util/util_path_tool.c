
#include "../global.h"

#define PATH_TOOL_SIDES       12
#define PATH_TOOL_MIN_DIST    0.0001
#define PATH_TOOL_MIN_RADIUS  0.001
#define PATH_TOOL_CURVE_STEPS 16
#define PATH_TOOL_SMOOTH_DOT  0.8 // Joints of a curve bending less than this are shaded smooth

static vec4_t        *_path_tool_points   = NULL; // World space, placed on the ground plane
static i32           *_path_tool_parents  = NULL; // Point the segment or curve ending at each point starts from, -1 for none
static i32            _path_tool_count    = 0;
static i32            _path_tool_capacity = 0;
static vec4_t        *_path_tool_samples  = NULL; // Points along the chains, curves sampled
static i32           *_path_tool_chains   = NULL; // End of each chain in the samples
static i32            _path_tool_tool     = -1;   // Tool the points were placed with
static mesh_object_t *_path_tool_object   = NULL; // Created with the first segment, or the selected mesh being edited
static mesh_object_t *_path_tool_synced   = NULL; // Paint object the points were last picked up from
static i32            _path_tool_active   = -1;   // Selected point, new points connect to it
static i32            _path_tool_dragging = -1;
static any_imap_t    *_path_tool_datas    = NULL; // Object uid -> f32_array_t, curve flag, radius and the points in the object space
static f32            _path_tool_radius;          // Radius the mesh was built with
static f32            _path_tool_right_x;
static f32            _path_tool_right_y;

static void _path_tool_vertex(mesh_geo_t *g, i32 i, vec4_t pos, vec4_t nor, f32 u, f32 v) {
	g->pos[i * 3]     = pos.x;
	g->pos[i * 3 + 1] = pos.y;
	g->pos[i * 3 + 2] = pos.z;
	g->nor[i * 3]     = nor.x;
	g->nor[i * 3 + 1] = nor.y;
	g->nor[i * 3 + 2] = nor.z;
	g->tex[i * 2]     = u;
	g->tex[i * 2 + 1] = v;
}

// Segments stay separate where the path turns back
static bool _path_tool_mitered(vec4_t dir, vec4_t neighbor_dir) {
	return vec4_len(vec4_add(dir, neighbor_dir)) >= 0.5;
}

// Shifts a ring point along the segment onto the plane shared with the neighbor segment
static vec4_t _path_tool_miter(vec4_t offset, vec4_t dir, vec4_t neighbor_dir) {
	if (!_path_tool_mitered(dir, neighbor_dir)) {
		return offset;
	}
	vec4_t m = vec4_norm(vec4_add(dir, neighbor_dir));
	return vec4_sub(offset, vec4_mult(dir, vec4_dot(offset, m) / vec4_dot(dir, m)));
}

static void _path_tool_cap(mesh_geo_t *g, i32 *vi, i32 *ii, vec4_t center, vec4_t n, vec4_t b, vec4_t nor, f32 radius, bool flip) {
	i32 sides = PATH_TOOL_SIDES;
	i32 base  = *vi;
	_path_tool_vertex(g, base, center, nor, 0.5, 0.5);
	for (i32 k = 0; k < sides; ++k) {
		f32    a = k / (f32)sides * math_pi() * 2.0;
		vec4_t o = vec4_add(vec4_mult(n, math_cos(a) * radius), vec4_mult(b, math_sin(a) * radius));
		_path_tool_vertex(g, base + 1 + k, vec4_add(center, o), nor, 0.5 + math_cos(a) * 0.5, 0.5 + math_sin(a) * 0.5);
	}
	for (i32 k = 0; k < sides; ++k) {
		i32 k1          = (k + 1) % sides;
		g->ind[(*ii)++] = base;
		g->ind[(*ii)++] = base + 1 + (flip ? k1 : k);
		g->ind[(*ii)++] = base + 1 + (flip ? k : k1);
	}
	*vi += sides + 1;
}

// Normal shared by the ring points of both segments meeting at a joint
static vec4_t _path_tool_joint_normal(vec4_t nor, vec4_t dir, vec4_t neighbor_dir) {
	if (vec4_dot(dir, neighbor_dir) < PATH_TOOL_SMOOTH_DOT) {
		return nor;
	}
	vec4_t m = vec4_norm(vec4_add(dir, neighbor_dir));
	return vec4_norm(vec4_sub(nor, vec4_mult(m, vec4_dot(nor, m))));
}

static bool _path_tool_curved() {
	return _path_tool_tool == TOOL_TYPE_CURVE;
}

// Curves keep anchors at even indices, each anchor with a parent is reached through the control point before it
static bool _path_tool_is_node(i32 i) {
	return !_path_tool_curved() || i % 2 == 0;
}

static i32 _path_tool_parent(i32 i) {
	i32 p = _path_tool_parents[i];
	return p >= 0 && p < i && _path_tool_is_node(p) ? p : -1; // Points connect to earlier ones
}

static bool _path_tool_has_mesh() {
	for (i32 i = 0; i < _path_tool_count; ++i) {
		if (_path_tool_is_node(i) && _path_tool_parent(i) >= 0) {
			return true;
		}
	}
	return false;
}

// Nodes without a parent start the path not drawn yet
static bool _path_tool_pending() {
	return _path_tool_object == NULL && _path_tool_count > 0;
}

static vec4_t _path_tool_bezier(vec4_t p0, vec4_t pc, vec4_t p1, f32 t) {
	f32 u = 1.0 - t;
	return vec4_add(vec4_add(vec4_mult(p0, u * u), vec4_mult(pc, 2.0 * u * t)), vec4_mult(p1, t * t));
}

static void _path_tool_sample(i32 *count, vec4_t p) {
	if (vec4_dist(p, _path_tool_samples[*count - 1]) >= PATH_TOOL_MIN_DIST) {
		_path_tool_samples[(*count)++] = p;
	}
}

// Appends the chain from node `from` through `next`, following the first child of each node
static void _path_tool_chain(i32 *count, i32 *chains, i32 *first_child, i32 from, i32 next) {
	i32 start                    = *count;
	_path_tool_samples[(*count)++] = _path_tool_points[from];
	for (i32 j = next; j >= 0; j = first_child[j]) {
		i32 p = _path_tool_parent(j);
		if (_path_tool_curved()) {
			for (i32 k = 1; k <= PATH_TOOL_CURVE_STEPS; ++k) {
				_path_tool_sample(count, _path_tool_bezier(_path_tool_points[p], _path_tool_points[j - 1], _path_tool_points[j], k / (f32)PATH_TOOL_CURVE_STEPS));
			}
		}
		else {
			_path_tool_sample(count, _path_tool_points[j]);
		}
	}
	if (*count - start < 2) {
		*count = start;
		return;
	}
	_path_tool_chains[(*chains)++] = *count;
}

// Splits the point tree into chains of samples, a branch starts a new chain at its parent
static i32 _path_tool_sample_chains() {
	i32  n             = _path_tool_count;
	_path_tool_samples = realloc(_path_tool_samples, (n * (PATH_TOOL_CURVE_STEPS + 1) + 1) * sizeof(vec4_t));
	_path_tool_chains  = realloc(_path_tool_chains, (n + 1) * sizeof(i32));
	i32 *first_child   = malloc((n + 1) * sizeof(i32));
	for (i32 i = 0; i < n; ++i) {
		first_child[i] = -1;
	}
	for (i32 i = n - 1; i >= 0; --i) {
		i32 p = _path_tool_is_node(i) ? _path_tool_parent(i) : -1;
		if (p >= 0) {
			first_child[p] = i;
		}
	}
	i32 count  = 0;
	i32 chains = 0;
	for (i32 i = 0; i < n; ++i) {
		if (!_path_tool_is_node(i)) {
			continue;
		}
		i32 p = _path_tool_parent(i);
		if (p < 0) {
			_path_tool_chain(&count, &chains, first_child, i, first_child[i]);
		}
		else if (first_child[p] != i) {
			_path_tool_chain(&count, &chains, first_child, p, i);
		}
	}
	free(first_child);
	return chains;
}

// Round tube with mitered joints and capped ends
static void _path_tool_tube(mesh_geo_t *g, i32 *_vi, i32 *_ii, vec4_t *points, i32 count, f32 radius, bool smooth) {
	i32 sides    = PATH_TOOL_SIDES;
	i32 segments = count - 1;
	i32 vi       = *_vi;
	i32 ii       = *_ii;

	f32 total = 0.0;
	for (i32 s = 0; s < segments; ++s) {
		total += vec4_dist(points[s], points[s + 1]);
	}

	vec4_t n;
	vec4_t b;
	vec4_t prev_dir;
	f32    dist = 0.0;
	for (i32 s = 0; s < segments; ++s) {
		vec4_t p0  = points[s];
		vec4_t p1  = points[s + 1];
		vec4_t dir = vec4_norm(vec4_sub(p1, p0));
		if (s == 0) {
			vec4_t up = math_abs(dir.z) > 0.99 ? vec4_x_axis() : vec4_z_axis();
			n         = vec4_norm(vec4_cross(up, dir));
		}
		else {
			// Mirror the frame over the joint, ring points of both segments then meet
			if (_path_tool_mitered(prev_dir, dir)) {
				vec4_t m = vec4_norm(vec4_add(prev_dir, dir));
				n        = vec4_sub(n, vec4_mult(m, 2.0 * vec4_dot(n, m)));
			}
			n = vec4_norm(vec4_sub(n, vec4_mult(dir, vec4_dot(n, dir))));
		}
		b               = vec4_cross(dir, n);
		vec4_t next_dir = s < segments - 1 ? vec4_norm(vec4_sub(points[s + 2], points[s + 1])) : dir;

		if (s == 0 || !_path_tool_mitered(dir, prev_dir)) {
			_path_tool_cap(g, &vi, &ii, p0, n, b, vec4_mult(dir, -1.0), radius, true);
		}

		f32 len  = vec4_dist(p0, p1);
		i32 base = vi;
		for (i32 k = 0; k <= sides; ++k) {
			f32    a   = k / (f32)sides * math_pi() * 2.0;
			vec4_t nor = vec4_add(vec4_mult(n, math_cos(a)), vec4_mult(b, math_sin(a)));
			vec4_t o0  = vec4_mult(nor, radius);
			vec4_t o1  = o0;
			if (s > 0) {
				o0 = _path_tool_miter(o0, dir, prev_dir);
			}
			if (s < segments - 1) {
				o1 = _path_tool_miter(o1, dir, next_dir);
			}
			vec4_t nor0 = smooth && s > 0 ? _path_tool_joint_normal(nor, dir, prev_dir) : nor;
			vec4_t nor1 = smooth && s < segments - 1 ? _path_tool_joint_normal(nor, dir, next_dir) : nor;
			_path_tool_vertex(g, vi++, vec4_add(p0, o0), nor0, k / (f32)sides, dist / total);
			_path_tool_vertex(g, vi++, vec4_add(p1, o1), nor1, k / (f32)sides, (dist + len) / total);
		}
		for (i32 k = 0; k < sides; ++k) {
			i32 v00      = base + k * 2;
			i32 v01      = v00 + 1;
			i32 v10      = v00 + 2;
			i32 v11      = v00 + 3;
			g->ind[ii++] = v00;
			g->ind[ii++] = v10;
			g->ind[ii++] = v11;
			g->ind[ii++] = v00;
			g->ind[ii++] = v11;
			g->ind[ii++] = v01;
		}

		if (s == segments - 1 || !_path_tool_mitered(dir, next_dir)) {
			_path_tool_cap(g, &vi, &ii, p1, n, b, dir, radius, false);
		}
		dist += len;
		prev_dir = dir;
	}
	*_vi = vi;
	*_ii = ii;
}

// Tubes of all the chains in _path_tool_samples
static mesh_geo_t *_path_tool_make_geo(vec4_t *points, i32 chains, f32 radius, bool smooth) {
	i32 segments = 0;
	for (i32 c = 0, start = 0; c < chains; start = _path_tool_chains[c++]) {
		segments += _path_tool_chains[c] - start - 1;
	}
	mesh_geo_t *g  = util_geo_create(segments * (PATH_TOOL_SIDES + 1) * 4, segments * PATH_TOOL_SIDES * 12);
	i32         vi = 0;
	i32         ii = 0;
	for (i32 c = 0, start = 0; c < chains; start = _path_tool_chains[c++]) {
		_path_tool_tube(g, &vi, &ii, points + start, _path_tool_chains[c] - start, radius, smooth);
	}
	g->vertex_count = vi;
	g->index_count  = ii;
	return g;
}

static f32_array_t *_path_tool_data(mesh_object_t *o) {
	return _path_tool_datas != NULL ? any_imap_get(_path_tool_datas, o->base->uid) : NULL;
}

static void _path_tool_set_data(mesh_object_t *o, f32_array_t *data) {
	if (_path_tool_datas == NULL) {
		_path_tool_datas = any_imap_create();
	}
	any_imap_set(_path_tool_datas, o->base->uid, data);
}

static void _path_tool_reserve(i32 count) {
	if (count > _path_tool_capacity) {
		_path_tool_capacity = math_max(count, math_max(_path_tool_capacity * 2, 16));
		_path_tool_points   = realloc(_path_tool_points, _path_tool_capacity * sizeof(vec4_t));
		_path_tool_parents  = realloc(_path_tool_parents, _path_tool_capacity * sizeof(i32));
	}
}

// Data flags, points stored before the parents were have none and form a single chain
#define PATH_TOOL_DATA_CURVE   1
#define PATH_TOOL_DATA_PARENTS 2

// Keeps the points with the mesh, [flags, radius, points xyz, parents]
static void _path_tool_store() {
	i32          n    = _path_tool_count;
	f32_array_t *data = _path_tool_data(_path_tool_object);
	if (data == NULL || data->length != 2 + n * 4) {
		data = f32_array_create(2 + n * 4);
		_path_tool_set_data(_path_tool_object, data);
	}
	mat4_t inv      = mat4_inv(_path_tool_object->base->transform->world);
	data->buffer[0] = PATH_TOOL_DATA_PARENTS | (_path_tool_curved() ? PATH_TOOL_DATA_CURVE : 0);
	data->buffer[1] = _path_tool_radius;
	for (i32 i = 0; i < n; ++i) {
		vec4_t p                    = _path_tool_points[i];
		p.w                         = 1.0;
		p                           = vec4_apply_mat4(p, inv);
		data->buffer[2 + i * 3]     = p.x;
		data->buffer[3 + i * 3]     = p.y;
		data->buffer[4 + i * 3]     = p.z;
		data->buffer[2 + n * 3 + i] = _path_tool_parents[i];
	}
}

static void _path_tool_rebuild() {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	// The object may have been deleted in the meantime
	if (_path_tool_object != NULL && array_index_of(g_project->_->paint_objects, _path_tool_object) < 0) {
		_path_tool_object = NULL;
	}

	_path_tool_radius = util_path_tool_radius;
	f32     radius    = math_max(_path_tool_radius, PATH_TOOL_MIN_RADIUS); // Typed values are not clamped
	bool    curved    = _path_tool_curved();
	i32     chains    = _path_tool_sample_chains(); // Reallocates the samples
	i32     count     = chains > 0 ? _path_tool_chains[chains - 1] : 0;
	vec4_t *world     = _path_tool_samples;
	// The mesh is built in the object space, a new object sits at the first point
	vec4_t *local = malloc(count * sizeof(vec4_t));
	mat4_t  inv   = _path_tool_object != NULL ? mat4_inv(_path_tool_object->base->transform->world) : mat4_identity();
	for (i32 i = 0; i < count; ++i) {
		vec4_t p = world[i];
		p.w      = 1.0;
		local[i] = _path_tool_object != NULL ? vec4_apply_mat4(p, inv) : vec4_sub(p, _path_tool_points[0]);
	}
	mesh_geo_t  *g  = _path_tool_make_geo(local, chains, radius, curved);
	mesh_data_t *md = nodes_mesh_geo_to_data(g, curved ? "Curve" : "Path");
	util_geo_free(g);
	free(local);

	if (_path_tool_object == NULL) {
		_path_tool_object                       = tab_meshes_append_data(md);
		_path_tool_object->base->transform->loc = _path_tool_points[0];
		transform_build_matrix(_path_tool_object->base->transform);
		util_mesh_transform_changed();
	}
	else if (nodes_mesh_set_input(_path_tool_object, md)) {
		// The mesh nodes of the object build on the path
	}
	else {
		mesh_data_t *old = _path_tool_object->data;
		if (!util_mesh_data_is_shared(old) && !nodes_mesh_holds(old)) {
			sys_notify_on_next_frame(&util_mesh_delete_data_uncache, old);
		}
		mesh_object_set_data(_path_tool_object, md);
		transform_build_matrix(_path_tool_object->base->transform);
		util_mesh_merge(NULL);
		nodes_mesh_request_refresh();
		render_path_raytrace_ready = false;
	}
	_path_tool_store();
	tab_meshes_reset_preview_map();

	if (in_use) {
		draw_begin(current, false, 0);
	}

	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	ui_header_handle->redraws = 2;
	g_context->ddirty         = 2;
}

static void _path_tool_end() {
	_path_tool_count    = 0;
	_path_tool_object   = NULL;
	_path_tool_synced   = NULL;
	_path_tool_active   = -1;
	_path_tool_dragging = -1;
}

// Size of the point spheres, the selected one is drawn 1.5x bigger
f32 util_path_tool_point_scale(vec4_t p) {
	f32 dist = vec4_dist(scene_camera->base->transform->loc, p);
	return dist / 8.0 * scene_camera->data->fov * 0.2;
}

// Points of the path being drawn or edited, drawn as spheres by render_pathsphere_tool
vec4_t *util_path_tool_get_points(i32 *count, i32 *active) {
	bool tool = (g_context->tool == TOOL_TYPE_PATH || g_context->tool == TOOL_TYPE_CURVE) && !base_player;
	*count    = tool && g_context->tool == _path_tool_tool ? _path_tool_count : 0;
	*active   = _path_tool_active;
	return _path_tool_points;
}

// Picks up the points of the selected mesh when no new path is being started
static void _path_tool_sync() {
	mesh_object_t *po = g_context->paint_object;
	if (_path_tool_pending()) {
		if (po == _path_tool_synced) {
			return;
		}
		// Another mesh was selected
		_path_tool_count = 0;
	}
	if (_path_tool_dragging >= 0) {
		return;
	}
	bool         selected = po != NULL && !context_object_deselected() && array_index_of(g_project->_->paint_objects, po) >= 0;
	f32_array_t *data     = selected ? _path_tool_data(po) : NULL;
	i32          flags    = data != NULL ? (i32)data->buffer[0] : 0;
	if (data == NULL || ((flags & PATH_TOOL_DATA_CURVE) != 0) != _path_tool_curved()) {
		_path_tool_count  = 0;
		_path_tool_object = NULL;
		_path_tool_synced = po;
		_path_tool_active = -1;
		return;
	}
	bool parents     = (flags & PATH_TOOL_DATA_PARENTS) != 0;
	_path_tool_count = (data->length - 2) / (parents ? 4 : 3);
	_path_tool_reserve(_path_tool_count);
	for (i32 i = 0; i < _path_tool_count; ++i) {
		vec4_t p             = (vec4_t){data->buffer[2 + i * 3], data->buffer[3 + i * 3], data->buffer[4 + i * 3], 1.0};
		_path_tool_points[i] = vec4_apply_mat4(p, po->base->transform->world);
		if (parents) {
			_path_tool_parents[i] = (i32)data->buffer[2 + _path_tool_count * 3 + i];
		}
		else {
			// A single chain, controls point back to the anchor before them
			_path_tool_parents[i] = _path_tool_curved() && i % 2 == 0 ? i - 2 : i - 1;
		}
	}
	if (po != _path_tool_synced) {
		// A mesh selected elsewhere shows its points with none selected, a path just drawn keeps its point
		if (po != _path_tool_object) {
			_path_tool_active = -1;
		}
		_path_tool_radius         = data->buffer[1];
		util_path_tool_radius     = _path_tool_radius;
		ui_header_handle->redraws = 2;
	}
	if (_path_tool_active >= _path_tool_count) {
		_path_tool_active = _path_tool_count - 1;
	}
	_path_tool_object = po;
	_path_tool_synced = po;
}

// Point under the mouse
static i32 _path_tool_pick() {
	ray_t *ray = raycast_get_ray(mouse_view_x(), mouse_view_y(), scene_camera);
	if (ray == NULL) {
		return -1;
	}
	i32 closest  = -1;
	f32 min_dist = 0.0;
	for (i32 i = 0; i < _path_tool_count; ++i) {
		vec4_t p      = _path_tool_points[i];
		f32    dist   = vec4_dist(scene_camera->base->transform->loc, p);
		f32    radius = util_path_tool_point_scale(p) * 1.5;
		if (ray_intersects_sphere(ray, p, radius) && (closest < 0 || dist < min_dist)) {
			min_dist = dist;
			closest  = i;
		}
	}
	free(ray);
	return closest;
}

static void _path_tool_drag() {
	if (!mouse_down("left")) {
		_path_tool_dragging = -1;
		return;
	}
	vec4_t from = _path_tool_points[_path_tool_dragging];
	vec4_t p;
	if (!util_tool_plane_point_at(from, &p) || vec4_dist(p, from) < PATH_TOOL_MIN_DIST) {
		return;
	}
	if (keyboard_down("control")) {
		// Move all points
		vec4_t d = vec4_sub(p, from);
		for (i32 i = 0; i < _path_tool_count; ++i) {
			_path_tool_points[i] = vec4_add(_path_tool_points[i], d);
		}
	}
	_path_tool_points[_path_tool_dragging] = p;
	if (_path_tool_has_mesh()) {
		_path_tool_rebuild();
	}
	g_context->ddirty = 2;
}

// Parent of a new point, curves grow from anchors
static i32 _path_tool_next_parent() {
	i32 p = _path_tool_active;
	if (_path_tool_curved() && p >= 0 && p % 2 == 1) {
		p = _path_tool_parents[p];
	}
	return p;
}

static void _path_tool_add(vec4_t p) {
	if (_path_tool_active >= 0 && vec4_dist(p, _path_tool_points[_path_tool_active]) < PATH_TOOL_MIN_DIST) {
		return;
	}
	i32 i = _path_tool_count;
	_path_tool_reserve(i + 1);
	_path_tool_points[i]  = p;
	_path_tool_parents[i] = _path_tool_next_parent();
	_path_tool_active     = i;
	_path_tool_count++;
	// A control point adds no curve until its anchor is placed
	if (_path_tool_is_node(i) && _path_tool_parent(i) >= 0) {
		_path_tool_rebuild();
	}
	else if (_path_tool_object != NULL) {
		// The points are picked up from the mesh again on the next frame
		_path_tool_store();
	}
}

// Removes the selected point and connects its children to its parent, curves remove the anchor with its control point
static void _path_tool_remove() {
	i32 n   = _path_tool_count;
	i32 idx = _path_tool_active;
	if (idx < 0 || idx >= n) {
		return;
	}
	i32 start = idx;
	i32 count = 1;
	if (_path_tool_curved()) {
		if (idx % 2 == 1) {
			count = idx + 1 < n ? 2 : 1;
		}
		else if (idx > 0) {
			start = idx - 1;
			count = 2;
		}
		else {
			count = n > 1 ? 2 : 1;
		}
	}
	i32 last       = start + count - 1;
	i32 new_parent = _path_tool_parents[last];
	if (new_parent >= start && new_parent <= last) {
		new_parent = -1;
	}
	else if (new_parent > last) {
		new_parent -= count;
	}
	for (i32 i = 0; i < n; ++i) {
		i32 p = _path_tool_parents[i];
		if (p >= start && p <= last) {
			p = new_parent;
		}
		else if (p > last) {
			p -= count;
		}
		_path_tool_parents[i] = p;
	}
	memmove(&_path_tool_points[start], &_path_tool_points[start + count], (n - start - count) * sizeof(vec4_t));
	memmove(&_path_tool_parents[start], &_path_tool_parents[start + count], (n - start - count) * sizeof(i32));
	_path_tool_count    = n - count;
	_path_tool_active   = _path_tool_count > 0 ? (new_parent >= 0 ? new_parent : _path_tool_count - 1) : -1;
	_path_tool_dragging = -1;

	if (_path_tool_has_mesh()) {
		if (_path_tool_object != NULL) {
			_path_tool_rebuild();
		}
	}
	else if (_path_tool_object != NULL) {
		// Nothing left to build, the path goes with its mesh
		if (_path_tool_object == g_context->paint_object && g_project->_->paint_objects->length > 1) {
			imap_delete(_path_tool_datas, _path_tool_object->base->uid);
			util_mesh_delete();
			_path_tool_count  = 0;
			_path_tool_object = NULL;
			_path_tool_active = -1;
		}
		// Otherwise the stored points are picked up again
	}
	g_context->ddirty = 2;
}

static void _path_tool_deselect() {
	_path_tool_active = -1;
	if (_path_tool_pending()) {
		_path_tool_count = 0;
	}
	g_context->ddirty = 2;
}

static void _path_tool_input() {
	// Right click deselects the point so the next click starts a new path, right drag rotates the camera
	if (mouse_started("right")) {
		_path_tool_right_x = mouse_view_x();
		_path_tool_right_y = mouse_view_y();
	}
	if (mouse_released("right") && math_abs(mouse_view_x() - _path_tool_right_x) < 4 && math_abs(mouse_view_y() - _path_tool_right_y) < 4) {
		_path_tool_deselect();
		return;
	}
	if (keyboard_started("escape")) {
		_path_tool_deselect();
		return;
	}

	_path_tool_sync();

	if (_path_tool_dragging >= 0) {
		_path_tool_drag();
		return;
	}

	if (keyboard_started("delete") && !g_ui->is_typing && context_in_3d_view()) {
		_path_tool_remove();
		return;
	}

	if (_path_tool_has_mesh() && _path_tool_object != NULL && _path_tool_radius != util_path_tool_radius && !mouse_down("left")) {
		_path_tool_rebuild();
	}

	if (!util_tool_input_enabled() || !mouse_started("left")) {
		return;
	}
	i32 picked = _path_tool_pick();
	if (picked >= 0) {
		_path_tool_dragging = picked;
		_path_tool_active   = picked;
		g_context->ddirty   = 2;
		return;
	}
	vec4_t p;
	if (!util_tool_plane_point(&p)) {
		return;
	}
	if (_path_tool_active < 0) {
		// Start a new path, the edited one is done
		_path_tool_count  = 0;
		_path_tool_object = NULL;
	}
	_path_tool_add(p);
}

void util_path_tool_update() {
	bool active = (g_context->tool == TOOL_TYPE_PATH || g_context->tool == TOOL_TYPE_CURVE) && !base_player;
	if (!active || g_context->tool != _path_tool_tool) {
		_path_tool_end();
		_path_tool_tool = g_context->tool;
		if (!active) {
			return;
		}
	}
	_path_tool_input();
}

void util_path_tool_reset() {
	_path_tool_datas = NULL;
	_path_tool_end();
}

void util_path_tool_copy(mesh_object_t *from, mesh_object_t *to) {
	f32_array_t *data = _path_tool_data(from);
	if (data != NULL) {
		_path_tool_set_data(to, f32_array_create_from_array(data));
	}
}

// Fills project mesh_paths, one array for each mesh, empty when it is not a path
void util_path_tool_export() {
	g_project->mesh_paths      = NULL;
	f32_array_t_array_t *paths = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		f32_array_t *data = _path_tool_data(g_project->_->paint_objects->buffer[i]);
		if (data != NULL) {
			g_project->mesh_paths = paths;
		}
		any_array_push(paths, data != NULL ? data : f32_array_create(0));
	}
}

void util_path_tool_import() {
	util_path_tool_reset();
	f32_array_t_array_t *paths = g_project->mesh_paths;
	if (paths == NULL) {
		return;
	}
	for (i32 i = 0; i < paths->length && i < g_project->_->paint_objects->length; ++i) {
		if (paths->buffer[i] != NULL && paths->buffer[i]->length >= 2) {
			// Decoded arrays live in the project blob
			_path_tool_set_data(g_project->_->paint_objects->buffer[i], f32_array_create_from_array(paths->buffer[i]));
		}
	}
}

// Line or curve from the last point to the mouse and the control lines, called while drawing the cursor
void util_path_tool_render() {
	if (g_context->tool != _path_tool_tool || _path_tool_count == 0) {
		return;
	}

	draw_scissor(base_x(), base_y(), base_w(), base_h());
	draw_set_color(0xffffffff);
	bool curved = _path_tool_curved();
	i32  next   = _path_tool_count; // Index of the next point, an even one is an anchor ending a curve
	i32  parent = _path_tool_next_parent();
	vec4_t p;
	if (_path_tool_active >= 0 && _path_tool_dragging < 0 && util_tool_input_enabled() && util_tool_plane_point(&p)) {
		if (curved && next % 2 == 0 && parent >= 0) {
			vec4_t from = _path_tool_points[parent];
			vec4_t a    = from;
			for (i32 k = 1; k <= PATH_TOOL_CURVE_STEPS; ++k) {
				vec4_t b = _path_tool_bezier(from, _path_tool_points[next - 1], p, k / (f32)PATH_TOOL_CURVE_STEPS);
				util_tool_draw_line(a, b);
				a = b;
			}
		}
		else if (parent >= 0) {
			util_tool_draw_line(_path_tool_points[parent], p);
		}
	}
	// The path stays visible no matter how thin the tube is
	i32 chains = _path_tool_sample_chains();
	for (i32 c = 0, start = 0; c < chains; start = _path_tool_chains[c++]) {
		for (i32 i = start + 1; i < _path_tool_chains[c]; ++i) {
			util_tool_draw_line(_path_tool_samples[i - 1], _path_tool_samples[i]);
		}
	}
	if (curved) {
		// Control points are off the curve
		for (i32 i = 1; i < _path_tool_count; i += 2) {
			i32 anchor = i + 1 < _path_tool_count ? _path_tool_parent(i + 1) : -1;
			i32 from   = anchor >= 0 ? anchor : _path_tool_parent(i);
			if (from >= 0) {
				util_tool_draw_line(_path_tool_points[from], _path_tool_points[i]);
			}
			if (anchor >= 0) {
				util_tool_draw_line(_path_tool_points[i], _path_tool_points[i + 1]);
			}
		}
	}
	draw_flush();
	gpu_disable_scissor();
}
