#include "../global.h"

#define MESH_IMAGE_MAX_SIZE 2048

// Pixels read back during one evaluation, by texture and channel
typedef struct mesh_image_cache {
	gpu_texture_t    *texture;
	i32               channel;
	mesh_geo_image_t *image;
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

mesh_geo_image_t *mesh_image_create(i32 width, i32 height) {
	mesh_geo_image_t *img = calloc(1, sizeof(mesh_geo_image_t));
	img->width            = width;
	img->height           = height;
	img->pixels           = calloc(width * height * 4 + 1, sizeof(f32));
	return img;
}

void mesh_image_free(mesh_geo_image_t *img) {
	free(img->pixels);
	free(img);
}

static mesh_geo_image_t *mesh_image_cache_add(gpu_texture_t *texture, i32 channel, mesh_geo_image_t *img) {
	if (mesh_image_cache_count == mesh_image_cache_cap) {
		mesh_image_cache_cap = mesh_image_cache_cap == 0 ? 4 : mesh_image_cache_cap * 2;
		mesh_image_cache     = realloc(mesh_image_cache, mesh_image_cache_cap * sizeof(mesh_image_cache_t));
	}
	mesh_image_cache[mesh_image_cache_count++] = (mesh_image_cache_t){.texture = texture, .channel = channel, .image = img};
	return img;
}

// Copied into a float target first, which decodes compressed and bgra textures and caps the size.
// A channel of 0 - 3 is copied into all four, -1 keeps rgba.
static mesh_geo_image_t *mesh_image_read(gpu_texture_t *texture, i32 channel) {
	i32 w = texture->width;
	i32 h = texture->height;
	if (w > MESH_IMAGE_MAX_SIZE || h > MESH_IMAGE_MAX_SIZE) {
		f32 s = (f32)MESH_IMAGE_MAX_SIZE / (w > h ? w : h);
		w     = math_max(1, math_floor(w * s));
		h     = math_max(1, math_floor(h * s));
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	gpu_texture_t *target = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_RGBA128);
	draw_begin(target, true, 0x00000000);
	draw_set_pipeline(pipes_copy128);
	draw_scaled_image(texture, 0, 0, w, h);
	draw_set_pipeline(NULL);
	draw_end();
	buffer_t *buf = gpu_get_texture_pixels(target);

	mesh_geo_image_t *img = mesh_image_create(w, h);
	memcpy(img->pixels, buf->buffer, w * h * 4 * sizeof(f32));
	gpu_delete_texture(target);
	if (in_use) {
		draw_begin(current, false, 0);
	}

	if (channel >= 0) {
		for (i32 i = 0; i < w * h; ++i) {
			f32 *p = img->pixels + i * 4;
			p[0] = p[1] = p[2] = p[3] = p[channel];
		}
	}
	return img;
}

// Image of an image or layer mask node, NULL when the texture is missing.
// The alpha output of an image gives alpha in all channels, a layer mask its red channel.
mesh_geo_image_t *mesh_image_node_get(ui_node_t *node, i32 socket) {
	gpu_texture_t *texture = mesh_image_node_texture(node);
	if (texture == NULL || texture->width <= 0 || texture->height <= 0) {
		return NULL;
	}
	i32 channel = string_equals(node->type, "LAYER_MASK") ? 0 : socket == 1 ? 3 : -1;
	for (i32 i = 0; i < mesh_image_cache_count; ++i) {
		if (mesh_image_cache[i].texture == texture && mesh_image_cache[i].channel == channel) {
			return mesh_image_cache[i].image;
		}
	}
	return mesh_image_cache_add(texture, channel, mesh_image_read(texture, channel));
}

// Freed with the read back images once the evaluation ends
void mesh_image_node_track(mesh_geo_image_t *img) {
	mesh_image_cache_add(NULL, -1, img);
}

void mesh_image_node_clear() {
	for (i32 i = 0; i < mesh_image_cache_count; ++i) {
		mesh_image_free(mesh_image_cache[i].image);
	}
	mesh_image_cache_count = 0;
}

// Nearest pixel, repeating, u and v as in mesh uvs: 0, 0 is the top left pixel corner
vec4_t mesh_image_sample(mesh_geo_image_t *img, f32 u, f32 v) {
	if (img == NULL || img->pixels == NULL || img->width <= 0 || img->height <= 0 || !isfinite(u) || !isfinite(v)) {
		return (vec4_t){0.0, 0.0, 0.0, 0.0};
	}
	i32 x  = (i32)((u - math_floor(u)) * img->width) % img->width;
	i32 y  = (i32)((v - math_floor(v)) * img->height) % img->height;
	f32 *p = img->pixels + (y * img->width + x) * 4;
	return (vec4_t){p[0], p[1], p[2], p[3]};
}

// Linked into a value or vector socket, the first pixel of the image
vec4_t mesh_image_node_vector(ui_node_t *node) {
	i32               socket = nodes_mesh_vector_socket;
	mesh_geo_image_t *img    = mesh_image_node_get(node, socket);
	if (img == NULL) {
		bool color = socket == 0 && string_equals(node->type, "TEX_IMAGE");
		return color ? (vec4_t){1.0, 0.0, 1.0, 1.0} : (vec4_t){0.0, 0.0, 0.0, 0.0}; // Pink for a missing texture
	}
	f32 *p = img->pixels;
	return (vec4_t){p[0], p[1], p[2], p[3]};
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
