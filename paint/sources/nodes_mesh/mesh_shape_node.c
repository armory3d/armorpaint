
#include "../global.h"

// Shapes, each one fits into a unit box

static void nodes_mesh_shape_sphere(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	f32 phi   = (1.0 - v) * math_pi();
	nor[0]    = math_sin(phi) * math_cos(theta);
	nor[1]    = math_sin(phi) * math_sin(theta);
	nor[2]    = math_cos(phi);
	pos[0]    = nor[0] * 0.5;
	pos[1]    = nor[1] * 0.5;
	pos[2]    = nor[2] * 0.5;
}

static void nodes_mesh_shape_cylinder(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	nor[0]    = math_cos(theta);
	nor[1]    = math_sin(theta);
	nor[2]    = 0.0;
	pos[0]    = nor[0] * 0.5;
	pos[1]    = nor[1] * 0.5;
	pos[2]    = v - 0.5;
}

static void nodes_mesh_shape_cone(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	f32 r     = 0.5 * (1.0 - v);
	f32 len   = math_sqrt(1.25);
	nor[0]    = math_cos(theta) / len;
	nor[1]    = math_sin(theta) / len;
	nor[2]    = 0.5 / len;
	pos[0]    = math_cos(theta) * r;
	pos[1]    = math_sin(theta) * r;
	pos[2]    = v - 0.5;
}

static void nodes_mesh_shape_torus(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	f32 phi   = v * 2.0 * math_pi();
	f32 major = 0.375;
	f32 minor = 0.125;
	nor[0]    = math_cos(phi) * math_cos(theta);
	nor[1]    = math_cos(phi) * math_sin(theta);
	nor[2]    = math_sin(phi);
	pos[0]    = (major + minor * math_cos(phi)) * math_cos(theta);
	pos[1]    = (major + minor * math_cos(phi)) * math_sin(theta);
	pos[2]    = minor * math_sin(phi);
}

static void nodes_mesh_shape_plane(f32 u, f32 v, f32 *pos, f32 *nor) {
	pos[0] = u - 0.5;
	pos[1] = v - 0.5;
	pos[2] = 0.0;
	nor[0] = 0.0;
	nor[1] = 0.0;
	nor[2] = 1.0;
}

static mesh_geo_t *nodes_mesh_shape_grid(i32 nu, i32 nv, void (*f)(f32, f32, f32 *, f32 *)) {
	mesh_geo_t *g = util_geo_create((nu + 1) * (nv + 1), nu * nv * 6);
	for (i32 j = 0; j <= nv; ++j) {
		for (i32 i = 0; i <= nu; ++i) {
			i32 vi = j * (nu + 1) + i;
			f32 u  = i / (f32)nu;
			f32 v  = j / (f32)nv;
			f(u, v, g->pos + vi * 3, g->nor + vi * 3);
			g->tex[vi * 2]     = u;
			g->tex[vi * 2 + 1] = 1.0 - v;
		}
	}
	i32 ii = 0;
	for (i32 j = 0; j < nv; ++j) {
		for (i32 i = 0; i < nu; ++i) {
			u32 a        = j * (nu + 1) + i;
			u32 b        = a + 1;
			u32 c        = b + nu + 1;
			u32 d        = a + nu + 1;
			g->ind[ii++] = a;
			g->ind[ii++] = b;
			g->ind[ii++] = c;
			g->ind[ii++] = a;
			g->ind[ii++] = c;
			g->ind[ii++] = d;
		}
	}
	return g;
}

static mesh_geo_t *nodes_mesh_shape_cap(i32 segments, f32 z) {
	mesh_geo_t *g = util_geo_create(segments + 2, segments * 3);
	for (i32 i = 0; i <= segments + 1; ++i) {
		// Last vertex is the center
		f32 theta         = i / (f32)segments * 2.0 * math_pi();
		f32 r             = i > segments ? 0.0 : 0.5;
		g->pos[i * 3]     = math_cos(theta) * r;
		g->pos[i * 3 + 1] = math_sin(theta) * r;
		g->pos[i * 3 + 2] = z;
		g->nor[i * 3 + 2] = z > 0.0 ? 1.0 : -1.0;
		g->tex[i * 2]     = (z > 0.0 ? 0.25 : 0.75) + g->pos[i * 3] * 0.5;
		g->tex[i * 2 + 1] = 0.25 - g->pos[i * 3 + 1] * 0.5;
	}
	for (i32 i = 0; i < segments; ++i) {
		g->ind[i * 3]     = segments + 1;
		g->ind[i * 3 + 1] = z > 0.0 ? i : i + 1;
		g->ind[i * 3 + 2] = z > 0.0 ? i + 1 : i;
	}
	return g;
}

static mesh_geo_t *nodes_mesh_shape_box() {
	mesh_geo_t *g = util_geo_create(24, 36);
	for (i32 face = 0; face < 6; ++face) {
		i32 axis  = face / 2;
		f32 sign  = face % 2 == 0 ? 1.0 : -1.0;
		i32 u     = (axis + 1) % 3;
		i32 v     = (axis + 2) % 3;
		f32 cu[4] = {-0.5, 0.5, 0.5, -0.5};
		f32 cv[4] = {-0.5, -0.5, 0.5, 0.5};
		for (i32 i = 0; i < 4; ++i) {
			i32 vi                = face * 4 + i;
			i32 corner            = sign > 0.0 ? i : 3 - i;
			g->pos[vi * 3 + axis] = 0.5 * sign;
			g->pos[vi * 3 + u]    = cu[corner];
			g->pos[vi * 3 + v]    = cv[corner];
			g->nor[vi * 3 + axis] = sign;
			// Faces are laid out in a 3x2 grid
			g->tex[vi * 2]     = (face % 3 + cu[corner] + 0.5) / 3.0;
			g->tex[vi * 2 + 1] = (face / 3 + 0.5 - cv[corner]) / 3.0;
		}
		u32 quad[6] = {0, 1, 2, 0, 2, 3};
		for (i32 i = 0; i < 6; ++i) {
			g->ind[face * 6 + i] = face * 4 + quad[i];
		}
	}
	return g;
}

// Moves the side of a capped shape into the lower half of the texture
static void nodes_mesh_shape_side_uvs(mesh_geo_t *g) {
	for (i32 i = 0; i < g->vertex_count; ++i) {
		g->tex[i * 2 + 1] = 0.5 + g->tex[i * 2 + 1] * 0.5;
	}
}

mesh_geo_t *nodes_mesh_shape(i32 shape, f32 *size, i32 segments) {
	segments      = segments < 3 ? 3 : (segments > 512 ? 512 : segments);
	mesh_geo_t *g = NULL;
	if (shape == 1) {
		g = nodes_mesh_shape_grid(segments, math_max(segments / 2, 2), nodes_mesh_shape_sphere);
	}
	else if (shape == 2) {
		g = nodes_mesh_shape_grid(segments, 1, nodes_mesh_shape_cylinder);
		nodes_mesh_shape_side_uvs(g);
		g = nodes_mesh_geo_join(g, nodes_mesh_shape_cap(segments, 0.5));
		g = nodes_mesh_geo_join(g, nodes_mesh_shape_cap(segments, -0.5));
	}
	else if (shape == 3) {
		g = nodes_mesh_shape_grid(segments, 1, nodes_mesh_shape_cone);
		nodes_mesh_shape_side_uvs(g);
		g = nodes_mesh_geo_join(g, nodes_mesh_shape_cap(segments, -0.5));
	}
	else if (shape == 4) {
		g = nodes_mesh_shape_grid(segments, math_max(segments / 2, 3), nodes_mesh_shape_torus);
	}
	else if (shape == 5) {
		g = nodes_mesh_shape_grid(1, 1, nodes_mesh_shape_plane);
	}
	else {
		g = nodes_mesh_shape_box();
	}

	// Drop the collapsed triangles at poles and tips
	i32 index_count = 0;
	for (i32 i = 0; i + 2 < g->index_count; i += 3) {
		f32 *a  = g->pos + g->ind[i] * 3;
		f32 *b  = g->pos + g->ind[i + 1] * 3;
		f32 *c  = g->pos + g->ind[i + 2] * 3;
		f32  cx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
		f32  cy = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
		f32  cz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
		if (cx * cx + cy * cy + cz * cz < 1e-16) {
			continue;
		}
		g->ind[index_count++] = g->ind[i];
		g->ind[index_count++] = g->ind[i + 1];
		g->ind[index_count++] = g->ind[i + 2];
	}
	g->index_count = index_count;

	for (i32 i = 0; i < g->vertex_count; ++i) {
		f32 *p   = g->pos + i * 3;
		f32 *n   = g->nor + i * 3;
		f32  len = 0.0;
		for (i32 k = 0; k < 3; ++k) {
			p[k] *= size[k];
			n[k] /= math_max(math_abs(size[k]), 0.0001);
			len += n[k] * n[k];
		}
		len = math_sqrt(len);
		for (i32 k = 0; k < 3; ++k) {
			n[k] /= len;
		}
	}
	return g;
}

mesh_geo_t *mesh_shape_node_eval(ui_node_t *node) {
	vec4_t size    = nodes_mesh_vector(node, 0);
	f32    sizes[] = {size.x, size.y, size.z};
	return nodes_mesh_shape(nodes_mesh_button(node, 0), sizes, math_floor(nodes_mesh_value(node, 1)));
}

void mesh_shape_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_input, _tr("Shape"), "MESH_SHAPE", 0xffb34f5a, "MESH");
	nodes_mesh_def_button(n, _tr("Shape"), "ENUM", 0, "Box\nSphere\nCylinder\nCone\nTorus\nPlane");
	nodes_mesh_def_vector(n, _tr("Size"), 1.0, 1.0, 1.0, 0.0, 10.0);
	nodes_mesh_def_value(n, _tr("Segments"), 32, 3, 128, 1);
	any_map_set(nodes_mesh_node_evals, "MESH_SHAPE", mesh_shape_node_eval);
}
