#include "../global.h"

// Pixels read back during one evaluation, by texture and channel.
// The images of one texture share its pixels, the first one owns them.
typedef struct mesh_image_cache {
	gpu_texture_t    *texture;
	i32               channel;
	mesh_geo_image_t *image;
	bool              owns;
} mesh_image_cache_t;

static mesh_image_cache_t *mesh_image_cache       = NULL;
static i32                 mesh_image_cache_count = 0;
static i32                 mesh_image_cache_cap   = 0;

// Texture of an image or layer mask node
gpu_texture_t *mesh_image_node_texture(ui_node_t *node) {
	i32 i = nodes_mesh_button(node, 0);
	if (string_equals(node->type, "LAYER_MASK")) {
		return i >= 0 && i < g_project->_->layers->length ? g_project->_->layers->buffer[i]->texpaint : NULL; // 9999 - Layer deleted
	}
	if (i < 0 || i >= g_project->_->assets->length) { // 9999 - Texture deleted
		return NULL;
	}
	return project_get_image(g_project->_->assets->buffer[i]);
}

// Float rgba pixels, owned by the evaluation once tracked
mesh_geo_image_t *mesh_image_create(i32 width, i32 height) {
	mesh_geo_image_t *img = calloc(1, sizeof(mesh_geo_image_t));
	img->width            = width;
	img->height           = height;
	img->pixels           = calloc(width * height * 4 + 1, sizeof(f32));
	img->format           = GPU_TEXTURE_FORMAT_RGBA128;
	img->channel          = -1;
	return img;
}

static mesh_geo_image_t *mesh_image_cache_add(gpu_texture_t *texture, i32 channel, mesh_geo_image_t *img, bool owns) {
	if (mesh_image_cache_count == mesh_image_cache_cap) {
		mesh_image_cache_cap = mesh_image_cache_cap == 0 ? 4 : mesh_image_cache_cap * 2;
		mesh_image_cache     = realloc(mesh_image_cache, mesh_image_cache_cap * sizeof(mesh_image_cache_t));
	}
	mesh_image_cache[mesh_image_cache_count++] = (mesh_image_cache_t){.texture = texture, .channel = channel, .image = img, .owns = owns};
	return img;
}

// The pixels as stored, 8, 16 or 32 bits, decoded when sampled. Render targets read back bgra on vulkan and metal,
// textures created from images rgba. A pixel buffer the texture did not keep before is taken over and freed with the image.
static u8 *mesh_image_read(gpu_texture_t *texture, bool *owns) {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	bool      cached = texture->buffer != NULL;
	buffer_t *buf    = gpu_get_texture_pixels(texture);
	u8       *pixels = buf->buffer;
	if (!cached) {
		free(texture->buffer);
		texture->buffer = NULL;
	}
	if (in_use) {
		draw_begin(current, false, 0);
	}
	*owns = !cached;
	return pixels;
}

// Image of a texture, a channel of 0 - 3 reads in all four, -1 keeps rgba. NULL for depth and compressed formats.
mesh_geo_image_t *mesh_image_texture_get(gpu_texture_t *texture, bool render_target, i32 channel) {
	gpu_texture_format_t format = texture->format;
	if (format == GPU_TEXTURE_FORMAT_D32) {
		return NULL;
	}
#ifndef WITH_BC7
	if (format == GPU_TEXTURE_FORMAT_RGBA32_BC7) {
		return NULL;
	}
#endif
	u8 *pixels = NULL;
	for (i32 i = 0; i < mesh_image_cache_count; ++i) {
		mesh_image_cache_t *c = &mesh_image_cache[i];
		if (c->texture == texture && c->channel == channel) {
			return c->image;
		}
		if (c->texture == texture) {
			pixels = c->image->pixels;
		}
	}
	bool owns = false;
	if (pixels == NULL) {
		pixels = mesh_image_read(texture, &owns);
	}
	mesh_geo_image_t *img = calloc(1, sizeof(mesh_geo_image_t));
	img->width            = texture->width;
	img->height           = texture->height;
	img->pixels           = pixels;
	img->format           = format == GPU_TEXTURE_FORMAT_RGBA32_BC7 ? GPU_TEXTURE_FORMAT_RGBA32 : format; // Decompressed on read
	img->channel          = channel;
#ifdef IRON_BGRA
	img->bgra = render_target && format == GPU_TEXTURE_FORMAT_RGBA32;
#endif
	return mesh_image_cache_add(texture, channel, img, owns);
}

// Image of an image or layer mask node, NULL when the texture is missing.
// The alpha output of an image gives alpha in all channels, a layer mask its red channel.
mesh_geo_image_t *mesh_image_node_get(ui_node_t *node, i32 socket) {
	gpu_texture_t *texture = mesh_image_node_texture(node);
	if (texture == NULL || texture->width <= 0 || texture->height <= 0) {
		return NULL;
	}
	bool layer   = string_equals(node->type, "LAYER_MASK");
	i32  channel = layer ? 0 : socket == 1 ? 3 : -1;
	return mesh_image_texture_get(texture, layer, channel);
}

// Freed with the read back images once the evaluation ends
void mesh_image_node_track(mesh_geo_image_t *img) {
	mesh_image_cache_add(NULL, -1, img, true);
}

void mesh_image_node_clear() {
	for (i32 i = 0; i < mesh_image_cache_count; ++i) {
		if (mesh_image_cache[i].owns) {
			free(mesh_image_cache[i].image->pixels);
		}
		free(mesh_image_cache[i].image);
	}
	mesh_image_cache_count = 0;
}

// Pixel x, y decoded to float rgba
vec4_t mesh_image_texel(mesh_geo_image_t *img, i32 x, i32 y) {
	i32    i = y * img->width + x;
	vec4_t c;
	switch (img->format) {
	case GPU_TEXTURE_FORMAT_RGBA128: {
		f32 *p = (f32 *)img->pixels + i * 4;
		c      = (vec4_t){p[0], p[1], p[2], p[3]};
		break;
	}
	case GPU_TEXTURE_FORMAT_RGBA64: {
		u16 *p = (u16 *)img->pixels + i * 4;
		c      = (vec4_t){render_path_raytrace_half_to_float(p[0]), render_path_raytrace_half_to_float(p[1]), render_path_raytrace_half_to_float(p[2]),
		                  render_path_raytrace_half_to_float(p[3])};
		break;
	}
	case GPU_TEXTURE_FORMAT_R32: {
		f32 r = ((f32 *)img->pixels)[i];
		c     = (vec4_t){r, r, r, 1.0};
		break;
	}
	case GPU_TEXTURE_FORMAT_R16: {
		f32 r = render_path_raytrace_half_to_float(((u16 *)img->pixels)[i]);
		c     = (vec4_t){r, r, r, 1.0};
		break;
	}
	case GPU_TEXTURE_FORMAT_R8: {
		f32 r = img->pixels[i] / 255.0;
		c     = (vec4_t){r, r, r, 1.0};
		break;
	}
	default: {
		u8 *p = img->pixels + i * 4;
		c     = img->bgra ? (vec4_t){p[2] / 255.0, p[1] / 255.0, p[0] / 255.0, p[3] / 255.0}
		                  : (vec4_t){p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, p[3] / 255.0};
		break;
	}
	}
	if (img->channel >= 0) {
		f32 v = img->channel == 0 ? c.x : img->channel == 1 ? c.y : img->channel == 2 ? c.z : c.w;
		c     = (vec4_t){v, v, v, v};
	}
	return c;
}

static bool mesh_image_valid(mesh_geo_image_t *img, f32 u, f32 v) {
	return img != NULL && img->pixels != NULL && img->width > 0 && img->height > 0 && isfinite(u) && isfinite(v);
}

// Nearest pixel, repeating, u and v as in mesh uvs: 0, 0 is the top left pixel corner
vec4_t mesh_image_sample(mesh_geo_image_t *img, f32 u, f32 v) {
	if (!mesh_image_valid(img, u, v)) {
		return (vec4_t){0.0, 0.0, 0.0, 0.0};
	}
	i32 x = (i32)((u - math_floor(u)) * img->width) % img->width;
	i32 y = (i32)((v - math_floor(v)) * img->height) % img->height;
	return mesh_image_texel(img, x, y);
}

static vec4_t mesh_image_lerp(vec4_t a, vec4_t b, f32 t) {
	return (vec4_t){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

// Bilinear between the four nearest pixel centers, repeating
vec4_t mesh_image_sample_bilinear(mesh_geo_image_t *img, f32 u, f32 v) {
	if (!mesh_image_valid(img, u, v)) {
		return (vec4_t){0.0, 0.0, 0.0, 0.0};
	}
	f32    x  = (u - math_floor(u)) * img->width - 0.5;
	f32    y  = (v - math_floor(v)) * img->height - 0.5;
	i32    x0 = (i32)math_floor(x);
	i32    y0 = (i32)math_floor(y);
	f32    fx = x - x0;
	f32    fy = y - y0;
	i32    x1 = (x0 + 1) % img->width;
	i32    y1 = (y0 + 1) % img->height;
	x0        = (x0 + img->width) % img->width;
	y0        = (y0 + img->height) % img->height;
	vec4_t a  = mesh_image_lerp(mesh_image_texel(img, x0, y0), mesh_image_texel(img, x1, y0), fx);
	vec4_t b  = mesh_image_lerp(mesh_image_texel(img, x0, y1), mesh_image_texel(img, x1, y1), fx);
	return mesh_image_lerp(a, b, fy);
}

// Linked into a value or vector socket, the first pixel of the image
vec4_t mesh_image_node_vector(ui_node_t *node) {
	i32               socket = nodes_mesh_vector_socket;
	mesh_geo_image_t *img    = mesh_image_node_get(node, socket);
	if (img == NULL) {
		bool color = socket == 0 && string_equals(node->type, "TEX_IMAGE");
		return color ? (vec4_t){1.0, 0.0, 1.0, 1.0} : (vec4_t){0.0, 0.0, 0.0, 0.0}; // Pink for a missing texture
	}
	return mesh_image_texel(img, 0, 0);
}

void mesh_image_node_init() {
	// Texture of the project, button 0 is the asset index like the material image node
	ui_node_t *n = nodes_mesh_def(nodes_mesh_input, _tr("Image Texture"), "TEX_IMAGE", 0xff4982a0, NULL);
	any_array_push(n->outputs, ALLOC_INIT(ui_node_socket_t, {.name          = _tr("Color"),
	                                                         .type          = "RGBA",
	                                                         .color         = 0xffc7c729,
	                                                         .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                         .min           = 0.0,
	                                                         .max           = 1.0,
	                                                         .precision     = 100}));
	any_array_push(
	    n->outputs,
	    ALLOC_INIT(
	        ui_node_socket_t,
	        {.name = _tr("Alpha"), .type = "VALUE", .color = 0xffa1a1a1, .default_value = f32_array_create_x(1.0), .min = 0.0, .max = 1.0, .precision = 100}));
	nodes_mesh_def_button(n, _tr("File"), "ENUM", 0, "");
	n->buttons->buffer[0]->output = -1;
	any_map_set(nodes_mesh_node_vectors, "TEX_IMAGE", mesh_image_node_vector);
}
