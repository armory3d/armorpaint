
// Procedural envmaps

#include "../global.h"

static f32 *env_rgb;
static i32  env_w;
static i32  env_h;

static void env_dir(i32 x, i32 y, f32 *d, f32 *phi) {
	*phi      = (y + 0.5f) / env_h * 3.14159265f;
	f32 theta = (x + 0.5f) / env_w * 2.0f * 3.14159265f - 3.14159265f;
	d[0]      = sinf(*phi) * cosf(theta);
	d[1]      = -sinf(*phi) * sinf(theta);
	d[2]      = cosf(*phi);
}

static void env_normalize(f32 *x, f32 *y, f32 *z) {
	f32 l = sqrtf(*x * *x + *y * *y + *z * *z);
	*x /= l;
	*y /= l;
	*z /= l;
}

void env_begin(i32 w, i32 h, f32 r, f32 g, f32 b) {
	free(env_rgb);
	env_w   = w;
	env_h   = h;
	env_rgb = malloc(sizeof(f32) * 3 * w * h);
	for (i32 i = 0; i < w * h; ++i) {
		env_rgb[i * 3]     = r;
		env_rgb[i * 3 + 1] = g;
		env_rgb[i * 3 + 2] = b;
	}
}

void env_add_lobe(f32 dx, f32 dy, f32 dz, f32 power, f32 r, f32 g, f32 b) {
	env_normalize(&dx, &dy, &dz);
	for (i32 y = 0; y < env_h; ++y) {
		for (i32 x = 0; x < env_w; ++x) {
			f32 d[3], phi;
			env_dir(x, y, d, &phi);
			f32  t = powf(fmaxf(d[0] * dx + d[1] * dy + d[2] * dz, 0.0f), power);
			f32 *c = &env_rgb[(y * env_w + x) * 3];
			c[0] += r * t;
			c[1] += g * t;
			c[2] += b * t;
		}
	}
}

void env_add_disc(f32 dx, f32 dy, f32 dz, f32 deg, f32 irradiance, f32 r, f32 g, f32 b) {
	env_normalize(&dx, &dy, &dz);
	f32 cos_max = cosf(deg * 0.5f * 3.14159265f / 180.0f);
	f64 omega   = 0.0; // Solid angle of the disc texels
	for (i32 pass = 0; pass < 2; ++pass) {
		f32 radiance = pass == 1 ? irradiance / (f32)omega : 0.0f;
		for (i32 y = 0; y < env_h; ++y) {
			for (i32 x = 0; x < env_w; ++x) {
				f32 d[3], phi;
				env_dir(x, y, d, &phi);
				if (d[0] * dx + d[1] * dy + d[2] * dz < cos_max) {
					continue;
				}
				if (pass == 0) {
					omega += (2.0 * 3.14159265 / env_w) * (3.14159265 / env_h) * sin(phi);
					continue;
				}
				f32 *c = &env_rgb[(y * env_w + x) * 3];
				c[0] += r * radiance;
				c[1] += g * radiance;
				c[2] += b * radiance;
			}
		}
	}
}

void env_write(char *path) {
	char      head[128];
	i32       len = snprintf(head, sizeof(head), "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n", env_h, env_w);
	buffer_t *out = buffer_create(len + env_w * env_h * 4);
	memcpy(out->buffer, head, len);
	u8 *p = out->buffer + len;
	for (i32 i = 0; i < env_w * env_h; ++i) {
		f32 *c = &env_rgb[i * 3];
		f32  m = fmaxf(c[0], fmaxf(c[1], c[2]));
		if (m <= 1e-32f) {
			memset(p + i * 4, 0, 4);
			continue;
		}
		f32 e     = ceilf(log2f(m));
		f32 scale = 256.0f / exp2f(e);
		for (i32 k = 0; k < 3; ++k) {
			p[i * 4 + k] = (u8)fminf(c[k] * scale, 255.0f);
		}
		p[i * 4 + 3] = (u8)(e + 128);
	}
	iron_file_save_bytes(path, out, out->length);
	array_delete(out);
}
