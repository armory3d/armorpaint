
#include "../global.h"
#include <float.h>

#define CSG_EPS        1e-5
#define CSG_STRIDE     6 // pos, nor
#define CSG_CHUNK_SIZE (4 * 1024 * 1024)

typedef struct csg_poly {
	i32              n;
	f64             *v;
	f64              plane[4];
	i32              origin; // Source triangle
	bool             flipped;
	struct csg_poly *next;
} csg_poly_t;

typedef struct csg_list {
	csg_poly_t *head;
	csg_poly_t *tail;
	i32         count;
} csg_list_t;

typedef struct csg_node {
	f64              plane[4];
	bool             has_plane;
	struct csg_node *front;
	struct csg_node *back;
	csg_list_t       polys;
} csg_node_t;

typedef struct csg_chunk {
	struct csg_chunk *next;
	i32               used;
	i32               size;
} csg_chunk_t;

static csg_chunk_t *csg_chunks     = NULL;
static i32          csg_node_count = 0;

static void *csg_alloc(i32 size) {
	size = (size + 15) & ~15;
	if (csg_chunks == NULL || csg_chunks->used + size > csg_chunks->size) {
		i32          chunk_size = size > CSG_CHUNK_SIZE ? size : CSG_CHUNK_SIZE;
		csg_chunk_t *c          = malloc(sizeof(csg_chunk_t) + chunk_size);
		c->next                 = csg_chunks;
		c->used                 = 0;
		c->size                 = chunk_size;
		csg_chunks              = c;
	}
	void *p = (char *)(csg_chunks + 1) + csg_chunks->used;
	csg_chunks->used += size;
	memset(p, 0, size);
	return p;
}

static void csg_free_all() {
	while (csg_chunks != NULL) {
		csg_chunk_t *next = csg_chunks->next;
		free(csg_chunks);
		csg_chunks = next;
	}
	csg_node_count = 0;
}

static void csg_list_push(csg_list_t *l, csg_poly_t *p) {
	p->next = NULL;
	if (l->tail == NULL) {
		l->head = p;
	}
	else {
		l->tail->next = p;
	}
	l->tail = p;
	l->count++;
}

static void csg_list_append(csg_list_t *l, csg_list_t src) {
	if (src.head == NULL) {
		return;
	}
	if (l->tail == NULL) {
		l->head = src.head;
	}
	else {
		l->tail->next = src.head;
	}
	l->tail = src.tail;
	l->count += src.count;
}

static csg_poly_t *csg_poly_create(i32 n) {
	csg_poly_t *p = csg_alloc(sizeof(csg_poly_t));
	p->n          = n;
	p->v          = csg_alloc(n * CSG_STRIDE * sizeof(f64));
	return p;
}

static csg_node_t *csg_node_create() {
	csg_node_count++;
	return csg_alloc(sizeof(csg_node_t));
}

static void csg_poly_flip(csg_poly_t *p) {
	for (i32 i = 0; i < p->n / 2; ++i) {
		f64 *a = p->v + i * CSG_STRIDE;
		f64 *b = p->v + (p->n - 1 - i) * CSG_STRIDE;
		for (i32 k = 0; k < CSG_STRIDE; ++k) {
			f64 t = a[k];
			a[k]  = b[k];
			b[k]  = t;
		}
	}
	for (i32 i = 0; i < p->n; ++i) {
		f64 *v = p->v + i * CSG_STRIDE;
		v[3]   = -v[3];
		v[4]   = -v[4];
		v[5]   = -v[5];
	}
	for (i32 k = 0; k < 4; ++k) {
		p->plane[k] = -p->plane[k];
	}
	p->flipped = !p->flipped;
}

static void csg_split(f64 *plane, csg_poly_t *p, csg_list_t *coplanar_front, csg_list_t *coplanar_back, csg_list_t *front, csg_list_t *back) {
	i32 nfront = 0;
	i32 nback  = 0;
	for (i32 i = 0; i < p->n; ++i) {
		f64 *v = p->v + i * CSG_STRIDE;
		f64  t = plane[0] * v[0] + plane[1] * v[1] + plane[2] * v[2] - plane[3];
		if (t < -CSG_EPS) {
			nback++;
		}
		else if (t > CSG_EPS) {
			nfront++;
		}
	}

	if (nfront == 0 && nback == 0) {
		f64 d = plane[0] * p->plane[0] + plane[1] * p->plane[1] + plane[2] * p->plane[2];
		csg_list_push(d > 0 ? coplanar_front : coplanar_back, p);
		return;
	}
	if (nback == 0) {
		csg_list_push(front, p);
		return;
	}
	if (nfront == 0) {
		csg_list_push(back, p);
		return;
	}

	// Spanning, each side gets at most two more vertices
	csg_poly_t *f = csg_poly_create(p->n + 2);
	csg_poly_t *b = csg_poly_create(p->n + 2);
	f->n          = 0;
	b->n          = 0;
	for (i32 i = 0; i < p->n; ++i) {
		f64 *vi = p->v + i * CSG_STRIDE;
		f64 *vj = p->v + ((i + 1) % p->n) * CSG_STRIDE;
		f64  ti = plane[0] * vi[0] + plane[1] * vi[1] + plane[2] * vi[2] - plane[3];
		f64  tj = plane[0] * vj[0] + plane[1] * vj[1] + plane[2] * vj[2] - plane[3];
		i32  si = ti < -CSG_EPS ? -1 : (ti > CSG_EPS ? 1 : 0);
		i32  sj = tj < -CSG_EPS ? -1 : (tj > CSG_EPS ? 1 : 0);
		if (si != -1) {
			memcpy(f->v + f->n++ * CSG_STRIDE, vi, CSG_STRIDE * sizeof(f64));
		}
		if (si != 1) {
			memcpy(b->v + b->n++ * CSG_STRIDE, vi, CSG_STRIDE * sizeof(f64));
		}
		if (si * sj == -1) {
			f64 t = ti / (ti - tj);
			f64 v[CSG_STRIDE];
			for (i32 k = 0; k < CSG_STRIDE; ++k) {
				v[k] = vi[k] + (vj[k] - vi[k]) * t;
			}
			memcpy(f->v + f->n++ * CSG_STRIDE, v, CSG_STRIDE * sizeof(f64));
			memcpy(b->v + b->n++ * CSG_STRIDE, v, CSG_STRIDE * sizeof(f64));
		}
	}
	memcpy(f->plane, p->plane, sizeof(p->plane));
	memcpy(b->plane, p->plane, sizeof(p->plane));
	f->origin = b->origin = p->origin;
	f->flipped = b->flipped = p->flipped;
	if (f->n >= 3) {
		csg_list_push(front, f);
	}
	if (b->n >= 3) {
		csg_list_push(back, b);
	}
}

static void csg_build(csg_node_t *node, csg_list_t polys) {
	while (polys.head != NULL) {
		if (!node->has_plane) {
			memcpy(node->plane, polys.head->plane, sizeof(node->plane));
			node->has_plane = true;
		}
		csg_list_t  front = {0};
		csg_list_t  back  = {0};
		csg_poly_t *p     = polys.head;
		while (p != NULL) {
			csg_poly_t *next = p->next;
			csg_split(node->plane, p, &node->polys, &node->polys, &front, &back);
			p = next;
		}
		if (front.head != NULL && node->front == NULL) {
			node->front = csg_node_create();
		}
		if (back.head != NULL && node->back == NULL) {
			node->back = csg_node_create();
		}
		if (front.head != NULL && back.head != NULL) {
			if (front.count <= back.count) {
				csg_build(node->front, front);
				node  = node->back;
				polys = back;
			}
			else {
				csg_build(node->back, back);
				node  = node->front;
				polys = front;
			}
		}
		else if (front.head != NULL) {
			node  = node->front;
			polys = front;
		}
		else {
			node  = node->back;
			polys = back;
		}
	}
}

// Removes the parts of polys which are inside of the tree
static csg_list_t csg_clip(csg_node_t *node, csg_list_t polys) {
	csg_list_t out = {0};
	while (polys.head != NULL) {
		if (!node->has_plane) {
			csg_list_append(&out, polys);
			break;
		}
		csg_list_t  front = {0};
		csg_list_t  back  = {0};
		csg_poly_t *p     = polys.head;
		while (p != NULL) {
			csg_poly_t *next = p->next;
			csg_split(node->plane, p, &front, &back, &front, &back);
			p = next;
		}
		if (node->front == NULL) {
			csg_list_append(&out, front);
			front = (csg_list_t){0};
		}
		if (node->back == NULL) {
			back = (csg_list_t){0};
		}
		if (front.head != NULL && back.head != NULL) {
			if (front.count <= back.count) {
				csg_list_append(&out, csg_clip(node->front, front));
				node  = node->back;
				polys = back;
			}
			else {
				csg_list_append(&out, csg_clip(node->back, back));
				node  = node->front;
				polys = front;
			}
		}
		else if (front.head != NULL) {
			node  = node->front;
			polys = front;
		}
		else {
			node  = node->back;
			polys = back;
		}
	}
	return out;
}

static csg_node_t **csg_nodes(csg_node_t *root, i32 *count) {
	csg_node_t **nodes = malloc((csg_node_count + 1) * sizeof(csg_node_t *));
	i32          n     = 0;
	nodes[n++]         = root;
	for (i32 i = 0; i < n; ++i) {
		if (nodes[i]->front != NULL) {
			nodes[n++] = nodes[i]->front;
		}
		if (nodes[i]->back != NULL) {
			nodes[n++] = nodes[i]->back;
		}
	}
	*count = n;
	return nodes;
}

static void csg_invert(csg_node_t *root) {
	i32          count;
	csg_node_t **nodes = csg_nodes(root, &count);
	for (i32 i = 0; i < count; ++i) {
		csg_node_t *node = nodes[i];
		for (csg_poly_t *p = node->polys.head; p != NULL; p = p->next) {
			csg_poly_flip(p);
		}
		for (i32 k = 0; k < 4; ++k) {
			node->plane[k] = -node->plane[k];
		}
		csg_node_t *front = node->front;
		node->front       = node->back;
		node->back        = front;
	}
	free(nodes);
}

// Removes the parts of a which are inside of b
static void csg_clip_to(csg_node_t *a, csg_node_t *b) {
	i32          count;
	csg_node_t **nodes = csg_nodes(a, &count);
	for (i32 i = 0; i < count; ++i) {
		nodes[i]->polys = csg_clip(b, nodes[i]->polys);
	}
	free(nodes);
}

static csg_list_t csg_all_polys(csg_node_t *root) {
	i32          count;
	csg_node_t **nodes = csg_nodes(root, &count);
	csg_list_t   out   = {0};
	for (i32 i = 0; i < count; ++i) {
		csg_list_append(&out, nodes[i]->polys);
		nodes[i]->polys = (csg_list_t){0};
	}
	free(nodes);
	return out;
}

static f64 csg_poly_area(csg_poly_t *p) {
	f64  area = 0.0;
	f64 *a    = p->v;
	for (i32 i = 1; i + 1 < p->n; ++i) {
		f64 *b     = p->v + i * CSG_STRIDE;
		f64 *c     = p->v + (i + 1) * CSG_STRIDE;
		f64  e0[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		f64  e1[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
		f64  n[3]  = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
		area += sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) * 0.5;
	}
	return area;
}

static csg_list_t csg_from_geo(mesh_geo_t *g, f64 *center, f64 scale, i32 origin, csg_poly_t **origins) {
	csg_list_t out = {0};
	for (i32 i = 0; i + 2 < g->index_count; i += 3) {
		csg_poly_t *p = csg_poly_create(3);
		p->origin     = origin + i / 3;
		for (i32 j = 0; j < 3; ++j) {
			u32  index = g->ind[i + j];
			f64 *v     = p->v + j * CSG_STRIDE;
			for (i32 k = 0; k < 3; ++k) {
				v[k]     = (g->pos[index * 3 + k] - center[k]) * scale;
				v[3 + k] = g->nor[index * 3 + k];
			}
		}
		f64 *a     = p->v;
		f64 *b     = p->v + CSG_STRIDE;
		f64 *c     = p->v + CSG_STRIDE * 2;
		f64  e0[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		f64  e1[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
		f64  n[3]  = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
		f64  len   = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len < 1e-12) {
			continue; // Degenerate
		}
		p->plane[0] = n[0] / len;
		p->plane[1] = n[1] / len;
		p->plane[2] = n[2] / len;
		p->plane[3] = p->plane[0] * a[0] + p->plane[1] * a[1] + p->plane[2] * a[2];

		// The tree splits polygons, keep the triangle to restore it when all of its pieces survive
		csg_poly_t *copy = csg_poly_create(3);
		memcpy(copy->v, p->v, 3 * CSG_STRIDE * sizeof(f64));
		memcpy(copy->plane, p->plane, sizeof(p->plane));
		origins[p->origin] = copy;
		csg_list_push(&out, p);
	}
	return out;
}

static void csg_bounds(mesh_geo_t *g, f32 *mn, f32 *mx) {
	for (i32 i = 0; i < g->vertex_count * 3; ++i) {
		i32 k = i % 3;
		if (g->pos[i] < mn[k]) {
			mn[k] = g->pos[i];
		}
		if (g->pos[i] > mx[k]) {
			mx[k] = g->pos[i];
		}
	}
}

mesh_geo_t *util_csg_boolean(mesh_geo_t *ga, mesh_geo_t *gb, i32 op) {
	// Fit both meshes into a unit box to keep the epsilon meaningful
	f32 mn[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
	f32 mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
	csg_bounds(ga, mn, mx);
	csg_bounds(gb, mn, mx);
	f64 center[3] = {(mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5};
	f64 extent    = math_max(mx[0] - mn[0], math_max(mx[1] - mn[1], mx[2] - mn[2]));
	f64 scale     = extent > 0.0 ? 1.0 / extent : 1.0;

	i32          count_a      = ga->index_count / 3;
	i32          origin_count = count_a + gb->index_count / 3;
	csg_poly_t **origins      = calloc(origin_count + 1, sizeof(csg_poly_t *));
	f64         *areas        = calloc(origin_count + 1, sizeof(f64));
	bool        *flipped      = calloc(origin_count + 1, sizeof(bool));

	csg_node_t *a = csg_node_create();
	csg_node_t *b = csg_node_create();
	csg_build(a, csg_from_geo(ga, center, scale, 0, origins));
	csg_build(b, csg_from_geo(gb, center, scale, count_a, origins));

	if (op == MESH_BOOLEAN_UNION) {
		csg_clip_to(a, b);
		csg_clip_to(b, a);
		csg_invert(b);
		csg_clip_to(b, a);
		csg_invert(b);
	}
	else if (op == MESH_BOOLEAN_DIFFERENCE) {
		csg_invert(a);
		csg_clip_to(a, b);
		csg_clip_to(b, a);
		csg_invert(b);
		csg_clip_to(b, a);
		csg_invert(b);
	}
	else {
		csg_invert(a);
		csg_clip_to(b, a);
		csg_invert(b);
		csg_clip_to(a, b);
		csg_clip_to(b, a);
	}

	csg_list_t polys = csg_all_polys(a);
	csg_list_append(&polys, csg_all_polys(b));
	if (op != MESH_BOOLEAN_UNION) {
		for (csg_poly_t *p = polys.head; p != NULL; p = p->next) {
			csg_poly_flip(p);
		}
	}

	for (csg_poly_t *p = polys.head; p != NULL; p = p->next) {
		areas[p->origin] += csg_poly_area(p);
		flipped[p->origin] = p->flipped;
	}

	// Uncut triangles are restored from their pieces
	csg_list_t  result = {0};
	csg_poly_t *p      = polys.head;
	while (p != NULL) {
		csg_poly_t *next   = p->next;
		csg_poly_t *origin = origins[p->origin];
		f64         area   = csg_poly_area(origin);
		if (areas[p->origin] < 0.0) {
			// Restored already
		}
		else if (math_abs(areas[p->origin] - area) > area * 1e-6) {
			csg_list_push(&result, p);
		}
		else {
			if (flipped[p->origin]) {
				csg_poly_flip(origin);
			}
			csg_list_push(&result, origin);
			areas[p->origin] = -1.0;
		}
		p = next;
	}
	polys = result;
	free(origins);
	free(areas);
	free(flipped);

	i32 tri_count = 0;
	for (csg_poly_t *p = polys.head; p != NULL; p = p->next) {
		tri_count += p->n - 2;
	}

	mesh_geo_t *out = util_geo_create(tri_count * 3, tri_count * 3);
	i32         vi  = 0;
	for (csg_poly_t *p = polys.head; p != NULL; p = p->next) {
		for (i32 i = 1; i + 1 < p->n; ++i) {
			i32 corners[3] = {0, i, i + 1};
			for (i32 j = 0; j < 3; ++j) {
				f64 *v   = p->v + corners[j] * CSG_STRIDE;
				f64  len = sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
				for (i32 k = 0; k < 3; ++k) {
					out->pos[vi * 3 + k] = v[k] / scale + center[k];
					out->nor[vi * 3 + k] = len > 1e-12 ? v[3 + k] / len : p->plane[k];
				}
				out->ind[vi] = vi;
				vi++;
			}
		}
	}

	csg_free_all();
	return out;
}
