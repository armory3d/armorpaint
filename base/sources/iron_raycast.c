#include "iron_raycast.h"

#include "iron_alloc.h"
#include "iron_math.h"
#include <math.h>

i32 sys_w(void);
i32 sys_h(void);

mat4_t _raycast_vp_inv = {0};
mat4_t _raycast_p_inv  = {0};
mat4_t _raycast_v_inv  = {0};

ray_t *raycast_get_ray(f32 input_x, f32 input_y, camera_object_t *camera) {
	vec4_t start = (vec4_t){0.0, 0.0, 0.0, 0.0};
	vec4_t end   = (vec4_t){0.0, 0.0, 0.0, 0.0};

	// Get 3D point form screen coords
	// Set two vectors with opposing z values
	start.x = (input_x / sys_w()) * 2.0 - 1.0;
	start.y = -((input_y / sys_h()) * 2.0 - 1.0);
	start.z = -1.0;
	end.x   = start.x;
	end.y   = start.y;
	end.z   = 1.0;

	_raycast_p_inv  = mat4_inv(camera->p);
	_raycast_v_inv  = mat4_inv(camera->v);
	_raycast_vp_inv = mat4_mult_mat(_raycast_p_inv, _raycast_v_inv);
	start           = vec4_apply_proj(start, _raycast_vp_inv);
	end             = vec4_apply_proj(end, _raycast_vp_inv);

	// Find direction from start to end
	end = vec4_sub(end, start);
	end = vec4_norm(end);
	end.x *= camera->data->far_plane;
	end.y *= camera->data->far_plane;
	end.z *= camera->data->far_plane;

	return ray_create(start, end);
}

vec4_t raycast_box_intersect(transform_t *transform, f32 input_x, f32 input_y, camera_object_t *camera) {
	ray_t *ray = raycast_get_ray(input_x, input_y, camera);
	vec4_t c   = (vec4_t){transform_world_x(transform), transform_world_y(transform), transform_world_z(transform), 0.0};
	vec4_t s   = (vec4_t){transform->dim.x, transform->dim.y, transform->dim.z, 0.0};
	return ray_intersect_box(ray, c, s);
}

transform_t *raycast_closest_box_intersect(any_array_t *transforms, f32 input_x, f32 input_y, camera_object_t *camera) {
	any_array_t *intersects = any_array_create(0);

	// Get intersects
	for (i32 i = 0; i < (i32)transforms->length; ++i) {
		transform_t *t         = (transform_t *)transforms->buffer[i];
		vec4_t       intersect = raycast_box_intersect(t, input_x, input_y, camera);
		if (!vec4_isnan(intersect)) {
			any_array_push(intersects, t);
		}
	}

	// No intersects
	if (intersects->length == 0) {
		return NULL;
	}

	// Get closest intersect
	transform_t *closest  = NULL;
	f32          min_dist = 100000.0;
	for (i32 i = 0; i < (i32)intersects->length; ++i) {
		transform_t *t    = (transform_t *)intersects->buffer[i];
		f32          dist = vec4_dist(t->loc, camera->base->transform->loc);
		if (dist < min_dist) {
			min_dist = dist;
			closest  = t;
		}
	}

	return closest;
}

vec4_t raycast_plane_intersect(vec4_t normal, vec4_t a, f32 input_x, f32 input_y, camera_object_t *camera) {
	ray_t  *ray   = raycast_get_ray(input_x, input_y, camera);
	plane_t plane = plane_create();
	plane_set(&plane, normal, a);
	return ray_intersect_plane(ray, &plane);
}

ray_t *ray_create(vec4_t origin, vec4_t dir) {
	ray_t *raw  = calloc(1, sizeof(ray_t));
	raw->origin = origin;
	raw->dir    = dir;
	return raw;
}

vec4_t ray_at(ray_t *raw, f32 t) {
	return vec4_add(vec4_mult(raw->dir, t), raw->origin);
}

f32 ray_dist_to_point(ray_t *raw, vec4_t point) {
	vec4_t dir = vec4_norm(raw->dir);
	f32    t   = vec4_dot(vec4_sub(point, raw->origin), dir);
	if (t < 0.0f)
		t = 0.0f;
	vec4_t closest = vec4_add(raw->origin, vec4_mult(dir, t));
	return vec4_dist(closest, point);
}

bool ray_intersects_sphere(ray_t *raw, vec4_t sphere_center, f32 sphere_radius) {
	return ray_dist_to_point(raw, sphere_center) <= sphere_radius;
}

bool ray_intersects_plane(ray_t *raw, plane_t *plane) {
	// Check if the ray lies on the plane first
	f32 dist_to_point = plane_dist_to_point(plane, raw->origin);
	if (dist_to_point == 0) {
		return true;
	}

	f32 denominator = vec4_dot(plane->normal, raw->dir);
	if (denominator * dist_to_point < 0) {
		return true;
	}

	// Ray origin is behind the plane (and is pointing behind it)
	return false;
}

f32 ray_dist_to_plane(ray_t *raw, plane_t *plane) {
	f32 denominator = vec4_dot(plane->normal, raw->dir);
	if (denominator == 0) {
		// Line is coplanar, return origin
		if (plane_dist_to_point(plane, raw->origin) == 0) {
			return 0;
		}

		return -1;
	}

	f32 t = -(vec4_dot(raw->origin, plane->normal) + plane->constant) / denominator;

	// Return if the ray never intersects the plane
	return t >= 0 ? t : -1;
}

vec4_t ray_intersect_plane(ray_t *raw, plane_t *plane) {
	f32 t = ray_dist_to_plane(raw, plane);
	if (t == -1) {
		return vec4_nan();
	}
	return ray_at(raw, t);
}

vec4_t ray_intersect_box(ray_t *raw, vec4_t center, vec4_t dim) {
	// http://www.scratchapixel.com/lessons/3d-basic-lessons/lesson-7-intersecting-simple-shapes/ray-box-intersection/
	f32 tmin;
	f32 tmax;
	f32 tymin;
	f32 tymax;
	f32 tzmin;
	f32 tzmax;

	f32 half_x    = dim.x / 2;
	f32 half_y    = dim.y / 2;
	f32 half_z    = dim.z / 2;
	f32 box_min_x = center.x - half_x;
	f32 box_min_y = center.y - half_y;
	f32 box_min_z = center.z - half_z;
	f32 box_max_x = center.x + half_x;
	f32 box_max_y = center.y + half_y;
	f32 box_max_z = center.z + half_z;

	f32    invdirx = 1.0 / raw->dir.x;
	f32    invdiry = 1.0 / raw->dir.y;
	f32    invdirz = 1.0 / raw->dir.z;
	vec4_t origin  = raw->origin;

	if (invdirx >= 0) {
		tmin = (box_min_x - origin.x) * invdirx;
		tmax = (box_max_x - origin.x) * invdirx;
	}
	else {
		tmin = (box_max_x - origin.x) * invdirx;
		tmax = (box_min_x - origin.x) * invdirx;
	}

	if (invdiry >= 0) {
		tymin = (box_min_y - origin.y) * invdiry;
		tymax = (box_max_y - origin.y) * invdiry;
	}
	else {
		tymin = (box_max_y - origin.y) * invdiry;
		tymax = (box_min_y - origin.y) * invdiry;
	}

	if ((tmin > tymax) || (tymin > tmax)) {
		return vec4_nan();
	}

	// These lines also handle the case where tmin or tmax is nan
	// (result of 0 * inf). x != x returns true if x is nan
	if (tymin > tmin || tmin != tmin) {
		tmin = tymin;
	}
	if (tymax < tmax || tmax != tmax) {
		tmax = tymax;
	}

	if (invdirz >= 0) {
		tzmin = (box_min_z - origin.z) * invdirz;
		tzmax = (box_max_z - origin.z) * invdirz;
	}
	else {
		tzmin = (box_max_z - origin.z) * invdirz;
		tzmax = (box_min_z - origin.z) * invdirz;
	}

	if ((tmin > tzmax) || (tzmin > tmax)) {
		return vec4_nan();
	}
	if (tzmin > tmin || tmin != tmin) {
		tmin = tzmin;
	}
	if (tzmax < tmax || tmax != tmax) {
		tmax = tzmax;
	}

	// Return point closest to the ray (positive side)
	if (tmax < 0) {
		return vec4_nan();
	}

	return ray_at(raw, tmin >= 0 ? tmin : tmax);
}

vec4_t ray_intersect_triangle(ray_t *raw, vec4_t a, vec4_t b, vec4_t c, bool cull_backface) {
	// Compute the offset origin, edges, and normal
	// from http://www.geometrictools.com/LibMathematics/Intersection/Wm5IntrRay3Triangle3.cpp
	vec4_t edge1  = vec4_sub(b, a);
	vec4_t edge2  = vec4_sub(c, a);
	vec4_t normal = vec4_cross(edge1, edge2);

	f32 ddn = vec4_dot(raw->dir, normal);
	i32 sign;

	if (ddn > 0) {
		if (cull_backface) {
			return vec4_nan();
		}
		sign = 1;
	}
	else if (ddn < 0) {
		sign = -1;
		ddn  = -ddn;
	}
	else {
		return vec4_nan();
	}

	vec4_t diff   = vec4_sub(raw->origin, a);
	f32    ddqxe2 = sign * vec4_dot(raw->dir, vec4_cross(diff, edge2));

	// b1 < 0, no intersection
	if (ddqxe2 < 0) {
		return vec4_nan();
	}

	f32 dde1xq = sign * vec4_dot(raw->dir, vec4_cross(edge1, diff));

	// b2 < 0, no intersection
	if (dde1xq < 0) {
		return vec4_nan();
	}

	// b1+b2 > 1, no intersection
	if (ddqxe2 + dde1xq > ddn) {
		return vec4_nan();
	}

	// Line intersects triangle, check if ray does.
	f32 qdn = -sign * vec4_dot(diff, normal);

	// t < 0, no intersection
	if (qdn < 0) {
		return vec4_nan();
	}

	// Ray intersects triangle.
	return ray_at(raw, qdn / ddn);
}

plane_t plane_create(void) {
	plane_t raw;
	raw.normal   = (vec4_t){1.0, 0.0, 0.0, 0.0};
	raw.constant = 0.0;
	return raw;
}

f32 plane_dist_to_point(plane_t *raw, vec4_t point) {
	return vec4_dot(raw->normal, point) + raw->constant;
}

plane_t plane_set(plane_t *raw, vec4_t normal, vec4_t point) {
	raw->normal   = normal;
	raw->constant = -vec4_dot(point, raw->normal);
	return *raw;
}
