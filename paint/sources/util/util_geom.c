
#include "../global.h"

raw_mesh_t *geom_make_plane(f32 size_x, f32 size_y, i32 verts_x, i32 verts_y, f32 uv_scale) {

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {0});
	mesh->scale_pos  = 1.0;
	mesh->scale_tex  = uv_scale;
	mesh->name       = "Plane";
	mesh->has_next   = false;

	// Pack positions to (-1, 1) range
	f32 half_x      = size_x / 2.0;
	f32 half_y      = size_y / 2.0;
	mesh->scale_pos = math_max(size_x, size_y);
	f32 inv         = (1 / (float)mesh->scale_pos) * 32767;

	mesh->posa = i16_array_create(verts_x * verts_y * 4);
	mesh->nora = i16_array_create(verts_x * verts_y * 2);
	mesh->texa = i16_array_create(verts_x * verts_y * 2);
	mesh->inda = u32_array_create((verts_x - 1) * (verts_y - 1) * 6);
	f32 step_x = size_x / (float)(verts_x - 1);
	f32 step_y = size_y / (float)(verts_y - 1);
	for (i32 i = 0; i < verts_x * verts_y; ++i) {
		f32 x                         = (i % verts_x) * step_x - half_x;
		f32 y                         = math_floor(i / (float)verts_x) * step_y - half_y;
		mesh->posa->buffer[i * 4]     = math_floor(x * inv);
		mesh->posa->buffer[i * 4 + 1] = math_floor(y * inv);
		mesh->posa->buffer[i * 4 + 2] = 0;
		mesh->nora->buffer[i * 2]     = 0;
		mesh->nora->buffer[i * 2 + 1] = 0;
		mesh->posa->buffer[i * 4 + 3] = 32767;
		x                             = (i % verts_x) / (float)(verts_x - 1);
		y                             = 1.0 - math_floor(i / (float)verts_x) / (float)(verts_y - 1);
		mesh->texa->buffer[i * 2]     = math_floor(x * 32767);
		mesh->texa->buffer[i * 2 + 1] = math_floor(y * 32767);
	}
	for (i32 i = 0; i < (verts_x - 1) * (verts_y - 1); ++i) {
		f32 x                         = i % (verts_x - 1);
		f32 y                         = math_floor(i / (float)(verts_y - 1));
		mesh->inda->buffer[i * 6]     = y * verts_x + x;
		mesh->inda->buffer[i * 6 + 1] = y * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 2] = (y + 1) * verts_x + x;
		mesh->inda->buffer[i * 6 + 3] = y * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 4] = (y + 1) * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 5] = (y + 1) * verts_x + x;
	}

	return mesh;
}

raw_mesh_t *geom_make_uv_sphere(f32 radius, i32 width_segments, i32 height_segments, bool stretch_uv, f32 uv_scale) {

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {0});
	mesh->scale_pos  = 1.0;
	mesh->scale_tex  = 1.0;
	mesh->name       = "Sphere";
	mesh->has_next   = false;

	// Pack positions to (-1, 1) range
	mesh->scale_pos = radius;
	mesh->scale_tex = uv_scale;
	f32 inv         = (1 / (float)mesh->scale_pos) * 32767;
	f32 pi2         = math_pi() * 2;

	i32 width_verts  = width_segments + 1;
	i32 height_verts = height_segments + 1;
	mesh->posa       = i16_array_create(width_verts * height_verts * 4);
	mesh->nora       = i16_array_create(width_verts * height_verts * 2);
	mesh->texa       = i16_array_create(width_verts * height_verts * 2);
	mesh->inda       = u32_array_create(width_segments * height_segments * 6 - width_segments * 6);

	vec4_t nor = (vec4_t){0.0, 0.0, 0.0, 1.0};
	i32    pos = 0;
	for (i32 y = 0; y < height_verts; ++y) {
		f32 v      = y / (float)height_segments;
		f32 v_flip = 1.0 - v;
		if (!stretch_uv) {
			v_flip /= 2;
		}
		f32 u_off = y == 0 ? 0.5 / (float)width_segments : y == height_segments ? -0.5 / (float)width_segments : 0.0;
		for (i32 x = 0; x < width_verts; ++x) {
			f32 u                      = x / (float)width_segments;
			f32 u_pi2                  = u * pi2;
			f32 v_pi                   = v * math_pi();
			f32 v_pi_sin               = math_sin(v_pi);
			f32 vx                     = -radius * math_cos(u_pi2) * v_pi_sin;
			f32 vy                     = radius * math_sin(u_pi2) * v_pi_sin;
			f32 vz                     = -radius * math_cos(v_pi);
			i32 i4                     = pos * 4;
			i32 i2                     = pos * 2;
			mesh->posa->buffer[i4]     = math_floor(vx * inv);
			mesh->posa->buffer[i4 + 1] = math_floor(vy * inv);
			mesh->posa->buffer[i4 + 2] = math_floor(vz * inv);
			nor                        = (vec4_t){vx, vy, vz, 1.0};
			nor                        = vec4_norm(nor);
			mesh->posa->buffer[i4 + 3] = math_floor(nor.z * 32767);
			mesh->nora->buffer[i2]     = math_floor(nor.x * 32767);
			mesh->nora->buffer[i2 + 1] = math_floor(nor.y * 32767);
			i32 tx                     = (math_floor((u + u_off) * 32767) - 1);
			i32 ty                     = (math_floor(v_flip * 32767) - 1);
			tx                         = tx < 0 ? 0 : (tx > 32767 ? 32767 : tx);
			ty                         = ty < 0 ? 0 : (ty > 32767 ? 32767 : ty);
			mesh->texa->buffer[i2]     = tx;
			mesh->texa->buffer[i2 + 1] = ty;
			pos++;
		}
	}

	pos                  = 0;
	i32 height_segments1 = height_segments - 1;
	for (i32 y = 0; y < height_segments; ++y) {
		for (i32 x = 0; x < width_segments; ++x) {
			i32 x1 = x + 1;
			i32 y1 = y + 1;
			f32 a  = y * width_verts + x1;
			f32 b  = y * width_verts + x;
			f32 c  = y1 * width_verts + x;
			f32 d  = y1 * width_verts + x1;
			if (y > 0) {
				mesh->inda->buffer[pos++] = a;
				mesh->inda->buffer[pos++] = b;
				mesh->inda->buffer[pos++] = d;
			}
			if (y < height_segments1) {
				mesh->inda->buffer[pos++] = b;
				mesh->inda->buffer[pos++] = c;
				mesh->inda->buffer[pos++] = d;
			}
		}
	}

	return mesh;
}
