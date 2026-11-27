#include "../global.h"
#include <float.h>

#define MESH_SCATTER_MAX 8192

static mesh_scatter_t **mesh_scatters      = NULL;
static i32              mesh_scatter_count = 0;

static u32 mesh_scatter_rng(u32 *state) {
	u32 x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static f32 mesh_scatter_rand01(u32 *state) {
	return (mesh_scatter_rng(state) & 0x00ffffffu) / 16777216.0f;
}

static void mesh_scatter_free_entry(mesh_scatter_t *s) {
	free(s->pos_local);
	free(s->nor_local);
	free(s->yaw);
	free(s->scale_mult);
	free(s->urandom);
	free(s->world);
	free(s->world_unpack);
	free(s);
}

// Frees scatter entries for host uid, or all entries when uid is -1
void mesh_scatter_clear(i32 uid) {
	for (i32 i = 0; i < mesh_scatter_count;) {
		if (uid == -1 || mesh_scatters[i]->host_uid == uid) {
			mesh_scatter_free_entry(mesh_scatters[i]);
			mesh_scatters[i] = mesh_scatters[--mesh_scatter_count];
		}
		else {
			++i;
		}
	}
}

bool mesh_scatter_has(i32 uid) {
	for (i32 i = 0; i < mesh_scatter_count; ++i) {
		if (mesh_scatters[i]->host_uid == uid && mesh_scatters[i]->count > 0) {
			return true;
		}
	}
	return false;
}

bool mesh_scatter_has_visible() {
	for (i32 i = 0; i < mesh_scatter_count; ++i) {
		mesh_scatter_t *s = mesh_scatters[i];
		if (s->count <= 0) {
			continue;
		}
		mesh_object_t *host   = nodes_mesh_object(s->host_uid);
		mesh_object_t *target = nodes_mesh_object(s->target_uid);
		if (host != NULL && host->base->visible && target != NULL && target->data != NULL && target->data->_->index_buffer->count > 0) {
			return true;
		}
	}
	return false;
}

// Whether any visible host object scatters target
bool mesh_scatter_uses(mesh_object_t *target) {
	if (target == NULL) {
		return false;
	}
	for (i32 i = 0; i < mesh_scatter_count; ++i) {
		mesh_scatter_t *s = mesh_scatters[i];
		if (s->count <= 0 || s->target_uid != target->base->uid) {
			continue;
		}
		mesh_object_t *host = nodes_mesh_object(s->host_uid);
		if (host != NULL && host->base->visible) {
			return true;
		}
	}
	return false;
}

// Recomputes world and world_unpack instance matrices whenever host/target transforms or target mesh bounds change
static void mesh_scatter_sync(mesh_scatter_t *s, mesh_object_t *host, mesh_object_t *target) {
	transform_update(host->base->transform);
	transform_update(target->base->transform);
	mat4_t hw = host->base->transform->world;
	mat4_t tw = target->base->transform->world;
	f32    sp = target->data->scale_pos;
	u32    iv = target->data->_->index_buffer->version;

	if (s->matrices_valid && memcmp(s->host_world.m, hw.m, sizeof(f32) * 16) == 0 && memcmp(s->target_world.m, tw.m, sizeof(f32) * 16) == 0 &&
	    s->target_data == target->data && s->target_scale_pos == sp && s->target_topology == iv) {
		return;
	}

	s->host_world       = hw;
	s->target_world     = tw;
	s->target_data      = target->data;
	s->target_scale_pos = sp;
	s->target_topology  = iv;
	s->matrices_valid   = true;

	vec4_t lmin = {0.0f, 0.0f, 0.0f, 0.0f};
	vec4_t lmax = {0.0f, 0.0f, 0.0f, 0.0f};
	mesh_data_calculate_aabb_min_max(target->data, &lmin, &lmax);
	s->target_min_z = lmin.z;

	vec4_t ts = {
	    sqrtf(tw.m[0] * tw.m[0] + tw.m[1] * tw.m[1] + tw.m[2] * tw.m[2]),
	    sqrtf(tw.m[4] * tw.m[4] + tw.m[5] * tw.m[5] + tw.m[6] * tw.m[6]),
	    sqrtf(tw.m[8] * tw.m[8] + tw.m[9] * tw.m[9] + tw.m[10] * tw.m[10]),
	    1.0f,
	};
	mat4_t nm = mat4_transpose3(mat4_inv(hw));

	for (i32 i = 0; i < s->count; ++i) {
		f32   *pl     = s->pos_local + i * 3;
		f32   *nl     = s->nor_local + i * 3;
		vec4_t pw     = vec4_apply_mat4((vec4_t){pl[0], pl[1], pl[2], 1.0f}, hw);
		vec4_t nw     = vec4_apply_mat4((vec4_t){nl[0], nl[1], nl[2], 0.0f}, nm);
		f32    nl_len = sqrtf(nw.x * nw.x + nw.y * nw.y + nw.z * nw.z);
		if (nl_len > 1e-6f) {
			nw.x /= nl_len;
			nw.y /= nl_len;
			nw.z /= nl_len;
		}
		else {
			nw = (vec4_t){0.0f, 0.0f, 1.0f, 0.0f};
		}

		quat_t q = quat_from_axis_angle((vec4_t){0.0f, 0.0f, 1.0f, 0.0f}, s->yaw[i]);
		if (s->align > 0.0f) {
			f32 dot = nw.z < -1.0f ? -1.0f : (nw.z > 1.0f ? 1.0f : nw.z);
			f32 ang = acosf(dot) * s->align;
			if (ang > 1e-5f) {
				// cross((0, 0, 1), nw) = (-nw.y, nw.x, 0)
				f32    ax   = -nw.y;
				f32    ay   = nw.x;
				f32    alen = sqrtf(ax * ax + ay * ay);
				vec4_t axis = alen > 1e-5f ? (vec4_t){ax / alen, ay / alen, 0.0f, 0.0f} : (vec4_t){1.0f, 0.0f, 0.0f, 0.0f};
				quat_t qt   = quat_from_axis_angle(axis, ang);
				q           = quat_mult(qt, q);
			}
		}

		f32    sm  = s->scale_mult[i];
		vec4_t scl = {ts.x * sm, ts.y * sm, ts.z * sm, 1.0f};
		vec4_t up  = vec4_apply_quat((vec4_t){0.0f, 0.0f, 1.0f, 0.0f}, q);
		f32    dz  = (-s->target_min_z + s->offset_z) * scl.z;
		vec4_t loc = {pw.x + up.x * dz, pw.y + up.y * dz, pw.z + up.z * dz, 1.0f};

		mat4_t w  = mat4_compose(loc, q, scl);
		mat4_t wu = w;
		if (sp != 1.0f) {
			for (i32 k = 0; k < 12; ++k) {
				wu.m[k] *= sp;
			}
		}
		s->world[i]        = w;
		s->world_unpack[i] = wu;
	}
}

// Draws all scatter instances in the raster viewport pass
void mesh_scatter_draw(shader_context_t *scon, bool own) {
	if (mesh_scatter_count == 0 || g_context->material_preview) {
		return;
	}
	for (i32 i = 0; i < mesh_scatter_count; ++i) {
		mesh_scatter_t *s = mesh_scatters[i];
		if (s->count <= 0) {
			continue;
		}
		mesh_object_t *host   = nodes_mesh_object(s->host_uid);
		mesh_object_t *target = nodes_mesh_object(s->target_uid);
		if (host == NULL || !host->base->visible || target == NULL || target->data == NULL || target->data->_->index_buffer->count == 0) {
			continue;
		}
		shader_context_t *sh = make_material_is_default(target->material) ? scon : own ? render_path_base_own_scon(target) : NULL;
		if (sh == NULL) {
			continue;
		}

		mesh_scatter_sync(s, host, target);

		transform_t *t            = target->base->transform;
		mat4_t       saved_world  = t->world;
		mat4_t       saved_unpack = t->world_unpack;
		f32          saved_radius = t->radius;
		bool         saved_dirty  = t->dirty;
		bool         saved_vis    = target->base->visible;
		f32          saved_rand   = target->base->urandom;

		t->dirty              = false;
		target->base->visible = true;
		for (i32 k = 0; k < s->count; ++k) {
			t->world              = s->world[k];
			t->world_unpack       = s->world_unpack[k];
			t->radius             = saved_radius * s->scale_mult[k];
			target->base->urandom = s->urandom[k];
			mesh_object_render(target, sh, _render_path_bind_params);
		}

		t->world              = saved_world;
		t->world_unpack       = saved_unpack;
		t->radius             = saved_radius;
		t->dirty              = saved_dirty;
		target->base->visible = saved_vis;
		target->base->urandom = saved_rand;
	}
}

// Registers all scatter instances in the path tracer acceleration structure
void mesh_scatter_raytrace_add(i32 (*override_slot_fn)(mesh_data_t *), gpu_texture_t *(*override_tex_fn)(i32, char), bool is_bake) {
	if (mesh_scatter_count == 0) {
		return;
	}
	for (i32 i = 0; i < mesh_scatter_count; ++i) {
		mesh_scatter_t *s = mesh_scatters[i];
		if (s->count <= 0) {
			continue;
		}
		mesh_object_t *host   = nodes_mesh_object(s->host_uid);
		mesh_object_t *target = nodes_mesh_object(s->target_uid);
		if (host == NULL || !host->base->visible || target == NULL || target->data == NULL || target->data->_->index_buffer->count == 0) {
			continue;
		}

		mesh_scatter_sync(s, host, target);

		i32             slot = override_slot_fn(target->data);
		gpu_texture_t  *textures[3];
		gpu_texture_t **tex_ptr = NULL;
		if (slot >= 0) {
			gpu_texture_t *a = override_tex_fn(slot, 'a');
			if (is_bake) {
				textures[0] = NULL;
				textures[1] = NULL;
				textures[2] = a;
			}
			else {
				textures[0] = a;
				textures[1] = override_tex_fn(slot, 'b');
				textures[2] = override_tex_fn(slot, 'c');
			}
			tex_ptr = textures;
		}

		gpu_buffer_t *vb = target->data->_->vertex_buffer;
		gpu_buffer_t *ib = target->data->_->index_buffer;
		for (i32 k = 0; k < s->count; ++k) {
			_gpu_raytrace_as_add(vb, ib, s->world_unpack[k], tex_ptr);
		}
	}
}

static mesh_geo_image_t *mesh_scatter_density_image(ui_node_t *node, i32 socket) {
	ui_node_link_t *link = nodes_mesh_link(nodes_mesh_eval_canvas, node, socket);
	ui_node_t      *from = link != NULL ? ui_get_node(nodes_mesh_eval_canvas->nodes, link->from_id) : NULL;
	if (from != NULL && (string_equals(from->type, "TEX_IMAGE") || string_equals(from->type, "LAYER_MASK"))) {
		return mesh_image_node_get(from, link->from_socket);
	}
	return NULL;
}

mesh_geo_t *mesh_scatter_node_eval(ui_node_t *node) {
	mesh_geo_t    *g      = nodes_mesh_eval_input(node, 0);
	mesh_object_t *self   = nodes_mesh_eval_object;
	mesh_object_t *target = nodes_mesh_node_object(node);
	if (self == NULL || target == NULL || target == self || g->index_count < 3 || g->vertex_count < 3) {
		return g;
	}

	i32  count     = (i32)math_floor(nodes_mesh_value(node, 1));
	i32  seed      = (i32)math_floor(nodes_mesh_value(node, 2));
	f32  scale     = nodes_mesh_value(node, 3);
	f32  scale_var = nodes_mesh_value(node, 4);
	f32  align     = nodes_mesh_value(node, 5);
	f32  max_slope = nodes_mesh_value(node, 6);
	f32  min_dist  = nodes_mesh_value(node, 7);
	f32  offset_z  = nodes_mesh_value(node, 8);
	f32  density   = nodes_mesh_value(node, 9);
	bool inv_mask  = node->buttons->length > 1 && nodes_mesh_button(node, 1) > 0;

	count     = count < 0 ? 0 : (count > MESH_SCATTER_MAX ? MESH_SCATTER_MAX : count);
	scale     = scale < 0.001f ? 0.001f : scale;
	scale_var = scale_var < 0.0f ? 0.0f : (scale_var > 1.0f ? 1.0f : scale_var);
	align     = align < 0.0f ? 0.0f : (align > 1.0f ? 1.0f : align);
	max_slope = max_slope < 0.0f ? 0.0f : (max_slope > 180.0f ? 180.0f : max_slope);
	min_dist  = min_dist < 0.0f ? 0.0f : min_dist;
	density   = density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density);

	mesh_geo_image_t *mask = mesh_scatter_density_image(node, 9);
	mesh_geo_image_t *excl = node->inputs->length > 10 ? mesh_scatter_density_image(node, 10) : NULL;
	if (count == 0 || (mask == NULL && density <= 0.0f)) {
		return g;
	}
	if (mask == NULL) {
		count = (i32)math_floor(count * density + 0.5f);
		if (count <= 0) {
			return g;
		}
	}

	transform_update(self->base->transform);
	mat4_t hw       = self->base->transform->world;
	mat4_t nm       = mat4_transpose3(mat4_inv(hw));
	f32    min_nz   = cosf(max_slope * (f32)(3.14159265358979323846 / 180.0));
	i32    nt       = g->index_count / 3;
	f32   *cdf      = malloc(sizeof(f32) * nt);
	f64    acc_area = 0.0;

	for (i32 t = 0; t < nt; ++t) {
		u32  i0    = g->ind[t * 3];
		u32  i1    = g->ind[t * 3 + 1];
		u32  i2    = g->ind[t * 3 + 2];
		f32 *a     = g->pos + i0 * 3;
		f32 *b     = g->pos + i1 * 3;
		f32 *c     = g->pos + i2 * 3;
		f32  e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		f32  e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
		f32  cx    = e1[1] * e2[2] - e1[2] * e2[1];
		f32  cy    = e1[2] * e2[0] - e1[0] * e2[2];
		f32  cz    = e1[0] * e2[1] - e1[1] * e2[0];
		f32  area  = 0.5f * sqrtf(cx * cx + cy * cy + cz * cz);
		if (area > 1e-10f && max_slope < 179.9f) {
			vec4_t nw  = vec4_apply_mat4((vec4_t){cx, cy, cz, 0.0f}, nm);
			f32    len = sqrtf(nw.x * nw.x + nw.y * nw.y + nw.z * nw.z);
			if (len <= 1e-8f || nw.z / len < min_nz) {
				area = 0.0f;
			}
		}
		acc_area += (f64)area;
		cdf[t] = (f32)acc_area;
	}

	if (acc_area <= 1e-12) {
		free(cdf);
		return g;
	}

	f32 *pos_local  = malloc(sizeof(f32) * count * 3);
	f32 *nor_local  = malloc(sizeof(f32) * count * 3);
	f32 *yaw        = malloc(sizeof(f32) * count);
	f32 *scale_mult = malloc(sizeof(f32) * count);
	f32 *urandom    = malloc(sizeof(f32) * count);
	f32 *placed_w   = min_dist > 0.0f ? malloc(sizeof(f32) * count * 3) : NULL;
	f32  min_dist2  = min_dist * min_dist;

	u32 rng = (u32)(seed * 747796405u + node->id * 2891336453u + 0x9e3779b9u);
	if (rng == 0) {
		rng = 1;
	}

	i32 placed       = 0;
	i32 max_attempts = count * (mask != NULL || excl != NULL || min_dist > 0.0f ? 32 : 2);
	f32 total_area   = cdf[nt - 1];

	for (i32 attempt = 0; attempt < max_attempts && placed < count; ++attempt) {
		f32 r  = mesh_scatter_rand01(&rng) * total_area;
		i32 lo = 0;
		i32 hi = nt - 1;
		while (lo < hi) {
			i32 mid = (lo + hi) >> 1;
			if (cdf[mid] < r) {
				lo = mid + 1;
			}
			else {
				hi = mid;
			}
		}
		i32 t  = lo;
		u32 i0 = g->ind[t * 3];
		u32 i1 = g->ind[t * 3 + 1];
		u32 i2 = g->ind[t * 3 + 2];

		f32 r1 = sqrtf(mesh_scatter_rand01(&rng));
		f32 r2 = mesh_scatter_rand01(&rng);
		f32 w0 = 1.0f - r1;
		f32 w1 = r1 * (1.0f - r2);
		f32 w2 = r1 * r2;

		if (mask != NULL || excl != NULL) {
			f32 tu = w0 * g->tex[i0 * 2] + w1 * g->tex[i1 * 2] + w2 * g->tex[i2 * 2];
			f32 tv = w0 * g->tex[i0 * 2 + 1] + w1 * g->tex[i1 * 2 + 1] + w2 * g->tex[i2 * 2 + 1];
			f32 d  = 1.0f;
			if (mask != NULL) {
				f32 m = mesh_image_sample(mask, tu, tv).x;
				d *= inv_mask ? (1.0f - m) : m;
			}
			if (excl != NULL) {
				d *= 1.0f - mesh_image_sample(excl, tu, tv).x;
			}
			if (d <= 0.001f || mesh_scatter_rand01(&rng) > d) {
				continue;
			}
		}

		f32 px = w0 * g->pos[i0 * 3] + w1 * g->pos[i1 * 3] + w2 * g->pos[i2 * 3];
		f32 py = w0 * g->pos[i0 * 3 + 1] + w1 * g->pos[i1 * 3 + 1] + w2 * g->pos[i2 * 3 + 1];
		f32 pz = w0 * g->pos[i0 * 3 + 2] + w1 * g->pos[i1 * 3 + 2] + w2 * g->pos[i2 * 3 + 2];

		if (placed_w != NULL) {
			vec4_t pw      = vec4_apply_mat4((vec4_t){px, py, pz, 1.0f}, hw);
			bool   too_far = true;
			for (i32 j = 0; j < placed; ++j) {
				f32 dx = pw.x - placed_w[j * 3];
				f32 dy = pw.y - placed_w[j * 3 + 1];
				f32 dz = pw.z - placed_w[j * 3 + 2];
				if (dx * dx + dy * dy + dz * dz < min_dist2) {
					too_far = false;
					break;
				}
			}
			if (!too_far) {
				continue;
			}
			placed_w[placed * 3]     = pw.x;
			placed_w[placed * 3 + 1] = pw.y;
			placed_w[placed * 3 + 2] = pw.z;
		}

		f32 nx   = w0 * g->nor[i0 * 3] + w1 * g->nor[i1 * 3] + w2 * g->nor[i2 * 3];
		f32 ny   = w0 * g->nor[i0 * 3 + 1] + w1 * g->nor[i1 * 3 + 1] + w2 * g->nor[i2 * 3 + 1];
		f32 nz   = w0 * g->nor[i0 * 3 + 2] + w1 * g->nor[i1 * 3 + 2] + w2 * g->nor[i2 * 3 + 2];
		f32 nlen = sqrtf(nx * nx + ny * ny + nz * nz);
		if (nlen > 1e-6f) {
			nx /= nlen;
			ny /= nlen;
			nz /= nlen;
		}
		else {
			nx = 0.0f;
			ny = 0.0f;
			nz = 1.0f;
		}

		pos_local[placed * 3]     = px;
		pos_local[placed * 3 + 1] = py;
		pos_local[placed * 3 + 2] = pz;
		nor_local[placed * 3]     = nx;
		nor_local[placed * 3 + 1] = ny;
		nor_local[placed * 3 + 2] = nz;
		yaw[placed]               = mesh_scatter_rand01(&rng) * 6.28318530718f;
		f32 sv                    = 1.0f + (mesh_scatter_rand01(&rng) * 2.0f - 1.0f) * scale_var;
		if (sv < 0.05f) {
			sv = 0.05f;
		}
		scale_mult[placed] = scale * sv;
		urandom[placed]    = mesh_scatter_rand01(&rng);
		placed++;
	}

	free(cdf);
	free(placed_w);

	if (placed == 0) {
		free(pos_local);
		free(nor_local);
		free(yaw);
		free(scale_mult);
		free(urandom);
		return g;
	}

	mesh_scatter_t *s = calloc(1, sizeof(mesh_scatter_t));
	s->host_uid       = self->base->uid;
	s->target_uid     = target->base->uid;
	s->count          = placed;
	s->align          = align;
	s->offset_z       = offset_z;
	s->pos_local      = pos_local;
	s->nor_local      = nor_local;
	s->yaw            = yaw;
	s->scale_mult     = scale_mult;
	s->urandom        = urandom;
	s->world          = malloc(sizeof(mat4_t) * placed);
	s->world_unpack   = malloc(sizeof(mat4_t) * placed);
	mesh_scatter_sync(s, self, target);

	mesh_scatters                       = realloc(mesh_scatters, sizeof(mesh_scatter_t *) * (mesh_scatter_count + 1));
	mesh_scatters[mesh_scatter_count++] = s;

	return g;
}

void mesh_scatter_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Scatter"), "MESH_SCATTER", 0xff4982a0, "MESH");
	nodes_mesh_def_button(n, _tr("Mesh"), "ENUM", 0, NULL);
	n->buttons->buffer[0]->default_value = f32_array_create_xyz(0, -1, 0);
	nodes_mesh_def_button(n, _tr("Invert Mask"), "BOOL", 0, NULL);
	nodes_mesh_def_mesh(n);
	nodes_mesh_def_value(n, _tr("Count"), 100, 0, 5000, 1);
	nodes_mesh_def_value(n, _tr("Seed"), 0, 0, 1000, 1);
	nodes_mesh_def_value(n, _tr("Scale"), 1.0, 0.01, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Scale Variation"), 0.2, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Align Normal"), 0.0, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Max Slope"), 45.0, 0.0, 180.0, 1);
	nodes_mesh_def_value(n, _tr("Min Distance"), 0.0, 0.0, 2.0, 100);
	nodes_mesh_def_value(n, _tr("Offset Z"), 0.0, -1.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Density"), 1.0, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Exclude"), 0.0, 0.0, 1.0, 100);
	any_map_set(nodes_mesh_node_evals, "MESH_SCATTER", mesh_scatter_node_eval);
}
