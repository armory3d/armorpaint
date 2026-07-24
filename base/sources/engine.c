#include "engine.h"
#include <limits.h>
#include <stdlib.h>

i32 _object_uid_counter = 0;

i32 sys_w(void);
i32 sys_h(void);
i32 sys_x(void);
i32 sys_y(void);
f32 sys_time(void);

gpu_pipeline_t *_mesh_object_last_pipeline   = NULL;
vec4_t          _camera_object_sphere_center = {0};

gpu_buffer_t   *gpu_create_vertex_buffer(i32 count, gpu_vertex_structure_t *structure);
gpu_buffer_t   *gpu_create_index_buffer(i32 count);
void            gpu_delete_buffer(gpu_buffer_t *buffer);
f32             math_floor(f32 x);
gpu_shader_t   *gpu_create_shader(buffer_t *data, i32 shader_type);
gpu_shader_t   *gpu_create_shader_from_source(char *source, int source_size, gpu_shader_type_t shader_type);
void            gpu_delete_shader(gpu_shader_t *shader);
gpu_pipeline_t *gpu_create_pipeline();

typedef struct {
	char             *source;
	int               source_size;
	gpu_shader_type_t shader_type;
	gpu_shader_t     *result;
} shader_compile_job_t;

static void shader_compile_worker(void *param) {
	shader_compile_job_t *job = (shader_compile_job_t *)param;
	job->result               = gpu_create_shader_from_source(job->source, job->source_size, job->shader_type);
}
void gpu_delete_pipeline(gpu_pipeline_t *pipeline);
#ifdef arm_embed
gpu_shader_t *sys_get_shader(char *name);
#endif

//  ██████╗ ██████╗      ██╗███████╗ ██████╗████████╗
// ██╔═══██╗██╔══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██║   ██║██████╔╝     ██║█████╗  ██║        ██║
// ██║   ██║██╔══██╗██   ██║██╔══╝  ██║        ██║
// ╚██████╔╝██████╔╝╚█████╔╝███████╗╚██████╗   ██║
//  ╚═════╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

object_t *object_create(bool is_empty) {
	object_t *raw  = gc_alloc(sizeof(object_t));
	raw->name      = "";
	raw->children  = any_array_create(0);
	raw->visible   = true;
	raw->culled    = false;
	raw->uid       = _object_uid_counter++;
	raw->transform = transform_create(raw);
	raw->is_empty  = is_empty;
	if (raw->is_empty) {
		any_array_push(scene_empties, raw);
	}
	return raw;
}

void object_set_parent(object_t *raw, object_t *parent_object) {
	if (parent_object == raw || parent_object == raw->parent) {
		return;
	}

	if (raw->parent != NULL) {
		array_remove(raw->parent->children, raw);
		raw->parent = NULL; // Rebuild matrix without a parent
		transform_build_matrix(raw->transform);
	}

	if (parent_object == NULL) {
		parent_object = _scene_scene_parent;
	}
	raw->parent = parent_object;
	any_array_push(raw->parent->children, raw);
}

void object_remove_super(object_t *raw) {
	if (raw->is_empty) {
		array_remove(scene_empties, raw);
	}
	while (raw->children->length > 0) {
		object_remove((object_t *)raw->children->buffer[0]);
	}
	if (raw->parent != NULL) {
		array_remove(raw->parent->children, raw);
		raw->parent = NULL;
	}
}

void object_remove(object_t *raw) {
	if (string_equals(raw->ext_type, "mesh_object_t")) {
		mesh_object_remove(raw->ext);
	}
	else if (string_equals(raw->ext_type, "camera_object_t")) {
		camera_object_remove(raw->ext);
	}
	else {
		object_remove_super(raw);
	}
}

object_t *object_get_child(object_t *raw, char *name) {
	if (string_equals(raw->name, name)) {
		return raw;
	}
	for (i32 i = 0; i < raw->children->length; ++i) {
		object_t *c = (object_t *)raw->children->buffer[i];
		object_t *r = object_get_child(c, name);
		if (r != NULL) {
			return r;
		}
	}
	return NULL;
}

// ████████╗██████╗  █████╗ ███╗   ██╗███████╗███████╗ ██████╗ ██████╗ ███╗   ███╗
// ╚══██╔══╝██╔══██╗██╔══██╗████╗  ██║██╔════╝██╔════╝██╔═══██╗██╔══██╗████╗ ████║
//    ██║   ██████╔╝███████║██╔██╗ ██║███████╗█████╗  ██║   ██║██████╔╝██╔████╔██║
//    ██║   ██╔══██╗██╔══██║██║╚██╗██║╚════██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║
//    ██║   ██║  ██║██║  ██║██║ ╚████║███████║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║
//    ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝

transform_t *transform_create(object_t *object) {
	transform_t *raw = gc_alloc(sizeof(transform_t));
	raw->scale_world = 1.0;
	raw->object      = object;
	transform_reset(raw);
	return raw;
}

void transform_reset(transform_t *raw) {
	raw->world        = mat4_identity();
	raw->world_unpack = mat4_identity();
	raw->local        = mat4_identity();
	raw->loc          = (vec4_t){0.0, 0.0, 0.0, 0.0};
	raw->rot          = (quat_t){0.0, 0.0, 0.0, 1.0};
	raw->scale        = (vec4_t){1.0, 1.0, 1.0, 0.0};
	raw->dim          = (vec4_t){2.0, 2.0, 2.0, 0.0};
	raw->radius       = 1.0;
	raw->dirty        = true;
}

void transform_update(transform_t *raw) {
	if (raw->dirty) {
		transform_build_matrix(raw);
	}
}

void transform_build_matrix(transform_t *raw) {
	raw->local = mat4_compose(raw->loc, raw->rot, raw->scale);

	if (raw->object->parent != NULL) {
		raw->world = mat4_mult_mat3x4(raw->local, raw->object->parent->transform->world);
	}
	else {
		raw->world = raw->local;
	}

	raw->world_unpack = raw->world;
	if (raw->scale_world != 1.0) {
		raw->world_unpack.m00 *= raw->scale_world;
		raw->world_unpack.m01 *= raw->scale_world;
		raw->world_unpack.m02 *= raw->scale_world;
		raw->world_unpack.m03 *= raw->scale_world;
		raw->world_unpack.m10 *= raw->scale_world;
		raw->world_unpack.m11 *= raw->scale_world;
		raw->world_unpack.m12 *= raw->scale_world;
		raw->world_unpack.m13 *= raw->scale_world;
		raw->world_unpack.m20 *= raw->scale_world;
		raw->world_unpack.m21 *= raw->scale_world;
		raw->world_unpack.m22 *= raw->scale_world;
		raw->world_unpack.m23 *= raw->scale_world;
	}

	transform_compute_dim(raw);

	// Update children
	for (i32 i = 0; i < raw->object->children->length; ++i) {
		object_t *n = (object_t *)raw->object->children->buffer[i];
		transform_build_matrix(n->transform);
	}

	raw->dirty = false;
}

void transform_set_matrix(transform_t *raw, mat4_t mat) {
	raw->local = mat;
	transform_decompose(raw);
	transform_build_matrix(raw);
}

void transform_decompose(transform_t *raw) {
	mat4_decomposed_t *dec = mat4_decompose(raw->local);
	raw->loc               = dec->loc;
	raw->rot               = dec->rot;
	raw->scale             = dec->scl;
}

void transform_rotate(transform_t *raw, vec4_t axis, float f) {
	quat_t q = quat_from_axis_angle(axis, f);
	raw->rot = quat_mult(q, raw->rot);
	transform_build_matrix(raw);
}

void transform_move(transform_t *raw, vec4_t axis, float f) {
	raw->loc = vec4_fadd(raw->loc, axis.x * f, axis.y * f, axis.z * f, 0.0);
	transform_build_matrix(raw);
}

void transform_compute_radius(transform_t *raw) {
	raw->radius = sqrtf(raw->dim.x * raw->dim.x + raw->dim.y * raw->dim.y + raw->dim.z * raw->dim.z);
}

void transform_compute_dim(transform_t *raw) {
	if (raw->object->raw == NULL && string_equals(raw->object->ext_type, "mesh_object_t")) {
		mesh_object_t *mo    = (mesh_object_t *)raw->object->ext;
		vec4_t         aabb  = mesh_data_calculate_aabb(mo->data);
		obj_t         *o_raw = gc_alloc(sizeof(obj_t));
		o_raw->dimensions    = f32_array_create_xyz(aabb.x, aabb.y, aabb.z);
		raw->object->raw     = o_raw;
	}

	if (raw->object->raw == NULL || raw->object->raw->dimensions == NULL) {
		raw->dim = (vec4_t){2.0 * raw->scale.x, 2.0 * raw->scale.y, 2.0 * raw->scale.z, 0.0};
	}
	else {
		f32_array_t *d = raw->object->raw->dimensions;
		raw->dim       = (vec4_t){d->buffer[0] * raw->scale.x, d->buffer[1] * raw->scale.y, d->buffer[2] * raw->scale.z, 0.0};
	}
	transform_compute_radius(raw);
}

vec4_t transform_look(transform_t *raw) {
	return mat4_look(raw->world);
}

vec4_t transform_right(transform_t *raw) {
	return mat4_right(raw->world);
}

vec4_t transform_up(transform_t *raw) {
	return mat4_up(raw->world);
}

float transform_world_x(transform_t *raw) {
	return raw->world.m30;
}

float transform_world_y(transform_t *raw) {
	return raw->world.m31;
}

float transform_world_z(transform_t *raw) {
	return raw->world.m32;
}

//  ██████╗ █████╗ ███╗   ███╗███████╗██████╗  █████╗     ██████╗  █████╗ ████████╗ █████╗
// ██╔════╝██╔══██╗████╗ ████║██╔════╝██╔══██╗██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██║     ███████║██╔████╔██║█████╗  ██████╔╝███████║    ██║  ██║███████║   ██║   ███████║
// ██║     ██╔══██║██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║    ██║  ██║██╔══██║   ██║   ██╔══██║
// ╚██████╗██║  ██║██║ ╚═╝ ██║███████╗██║  ██║██║  ██║    ██████╔╝██║  ██║   ██║   ██║  ██║
//  ╚═════╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

camera_data_t *camera_data_parse(char *name, char *id) {
	scene_t       *format = data_get_scene_raw(name);
	camera_data_t *raw    = camera_data_get_raw_by_name(format->camera_datas, id);
	if (raw == NULL) {
		iron_log("Camera data '%s' not found!", id);
	}
	return raw;
}

camera_data_t *camera_data_get_raw_by_name(any_array_t *datas, char *name) {
	if (strcmp(name, "") == 0) {
		return (camera_data_t *)datas->buffer[0];
	}
	for (i32 i = 0; i < datas->length; ++i) {
		camera_data_t *d = (camera_data_t *)datas->buffer[i];
		if (strcmp(d->name, name) == 0) {
			return d;
		}
	}
	return NULL;
}

// ██╗    ██╗ ██████╗ ██████╗ ██╗      ██████╗     ██████╗  █████╗ ████████╗ █████╗
// ██║    ██║██╔═══██╗██╔══██╗██║      ██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██║ █╗ ██║██║   ██║██████╔╝██║      ██║  ██║    ██║  ██║███████║   ██║   ███████║
// ██║███╗██║██║   ██║██╔══██╗██║      ██║  ██║    ██║  ██║██╔══██║   ██║   ██╔══██║
// ╚███╔███╔╝╚██████╔╝██║  ██║███████╗ ██████╔╝    ██████╔╝██║  ██║   ██║   ██║  ██║
//  ╚══╝╚══╝  ╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝     ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

f32_array_t *_world_data_empty_irr = NULL;

world_data_t *world_data_parse(char *name, char *id) {
	scene_t      *format = data_get_scene_raw(name);
	world_data_t *raw    = world_data_get_raw_by_name(format->world_datas, id);
	if (raw == NULL) {
		iron_log("World data '%s' not found!", id);
		return NULL;
	}

	raw->_                   = gc_alloc(sizeof(world_data_runtime_t));
	raw->_->radiance_mipmaps = any_array_create(0);

	raw->_->irradiance = world_data_set_irradiance(raw);
	if (raw->radiance != NULL) {
		raw->_->radiance = data_get_texture(raw->radiance);
		while (raw->_->radiance_mipmaps->length < raw->radiance_mipmaps) {
			any_array_push(raw->_->radiance_mipmaps, NULL);
		}
		i32   dot  = string_last_index_of(raw->radiance, ".");
		char *ext  = substring(raw->radiance, dot, string_length(raw->radiance));
		char *base = substring(raw->radiance, 0, dot);

		for (i32 i = 0; i < raw->radiance_mipmaps; ++i) {
			char          *mip_name             = string("%s_%s%s", base, i32_to_string(i), ext);
			gpu_texture_t *mipimg               = data_get_texture(mip_name);
			raw->_->radiance_mipmaps->buffer[i] = mipimg;
		}
	}

	return raw;
}

world_data_t *world_data_get_raw_by_name(any_array_t *datas, char *name) {
	if (strcmp(name, "") == 0) {
		return (world_data_t *)datas->buffer[0];
	}
	for (i32 i = 0; i < datas->length; ++i) {
		world_data_t *d = (world_data_t *)datas->buffer[i];
		if (strcmp(d->name, name) == 0) {
			return d;
		}
	}
	return NULL;
}

f32_array_t *world_data_get_empty_irradiance() {
	if (_world_data_empty_irr == NULL) {
		_world_data_empty_irr = f32_array_create(28);
		gc_root(_world_data_empty_irr);
		for (i32 i = 0; i < _world_data_empty_irr->length; ++i) {
			_world_data_empty_irr->buffer[i] = 0.0;
		}
	}
	return _world_data_empty_irr;
}

f32_array_t *world_data_set_irradiance(world_data_t *raw) {
	if (raw->irradiance == NULL) {
		return world_data_get_empty_irradiance();
	}
	buffer_t     *b                 = data_get_blob(string("%s.arm", raw->irradiance));
	irradiance_t *irradiance_parsed = (irradiance_t *)armpack_decode(b);
	f32_array_t  *irr               = f32_array_create(28); // Align to mult of 4 - 27->28
	for (i32 i = 0; i < 27; ++i) {
		irr->buffer[i] = irradiance_parsed->irradiance->buffer[i];
	}
	return irr;
}

void world_data_load_envmap(world_data_t *raw) {
	if (raw->envmap != NULL) {
		raw->_->envmap = data_get_texture(raw->envmap);
	}
}

// ███╗   ███╗ █████╗ ████████╗███████╗██████╗ ██╗ █████╗ ██╗         ██████╗  █████╗ ████████╗ █████╗
// ████╗ ████║██╔══██╗╚══██╔══╝██╔════╝██╔══██╗██║██╔══██╗██║        ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██╔████╔██║███████║   ██║   █████╗  ██████╔╝██║███████║██║        ██║  ██║███████║   ██║   ███████║
// ██║╚██╔╝██║██╔══██║   ██║   ██╔══╝  ██╔══██╗██║██╔══██║██║        ██║  ██║██╔══██║   ██║   ██╔══██║
// ██║ ╚═╝ ██║██║  ██║   ██║   ███████╗██║  ██║██║██║  ██║███████╗   ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝╚═╝  ╚═╝╚══════╝   ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

i32 _material_data_uid_counter = 0;

material_data_t *material_data_create(material_data_t *raw, char *file) {
	raw->_      = gc_alloc(sizeof(material_data_runtime_t));
	raw->_->uid = (float)(++_material_data_uid_counter); // Start from 1

	any_array_t *ref         = string_split(raw->shader, "/");
	char        *object_file = "";
	char        *data_ref    = "";
	if (ref->length == 2) { // File reference
		object_file = (char *)ref->buffer[0];
		data_ref    = (char *)ref->buffer[1];
	}
	else { // Local data
		object_file = file;
		data_ref    = raw->shader;
	}

	raw->_->shader = data_get_shader(object_file, data_ref);
	for (i32 i = 0; i < (i32)raw->contexts->length; ++i) {
		material_context_t *c = (material_context_t *)raw->contexts->buffer[i];
		material_context_load(c);
	}
	return raw;
}

material_data_t *material_data_parse(char *file, char *name) {
	scene_t         *format = data_get_scene_raw(file);
	material_data_t *raw    = material_data_get_raw_by_name(format->material_datas, name);
	if (raw == NULL) {
		iron_log("Material data '%s' not found!", name);
		return NULL;
	}
	return material_data_create(raw, file);
}

material_data_t *material_data_get_raw_by_name(any_array_t *datas, char *name) {
	if (strcmp(name, "") == 0) {
		return (material_data_t *)datas->buffer[0];
	}
	for (i32 i = 0; i < (i32)datas->length; ++i) {
		material_data_t *d = (material_data_t *)datas->buffer[i];
		if (strcmp(d->name, name) == 0) {
			return d;
		}
	}
	return NULL;
}

material_context_t *material_data_get_context(material_data_t *raw, char *name) {
	for (i32 i = 0; i < (i32)raw->contexts->length; ++i) {
		material_context_t *c = (material_context_t *)raw->contexts->buffer[i];
		if (strcmp(c->name, name) == 0) {
			return c;
		}
	}
	return NULL;
}

void material_context_load(material_context_t *raw) {
	raw->_ = gc_alloc(sizeof(material_context_runtime_t));
	if (raw->bind_textures != NULL && raw->bind_textures->length > 0) {
		raw->_->textures = any_array_create(0);
		for (i32 i = 0; i < (i32)raw->bind_textures->length; ++i) {
			bind_tex_t *tex = (bind_tex_t *)raw->bind_textures->buffer[i];
			if (strcmp(tex->file, "") == 0) { // Empty texture
				continue;
			}
			gpu_texture_t *image = data_get_texture(tex->file);
			any_array_push(raw->_->textures, image);
		}
	}
}

// ███████╗██╗  ██╗ █████╗ ██████╗ ███████╗██████╗     ██████╗  █████╗ ████████╗ █████╗
// ██╔════╝██║  ██║██╔══██╗██╔══██╗██╔════╝██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ███████╗███████║███████║██║  ██║█████╗  ██████╔╝    ██║  ██║███████║   ██║   ███████║
// ╚════██║██╔══██║██╔══██║██║  ██║██╔══╝  ██╔══██╗    ██║  ██║██╔══██║   ██║   ██╔══██║
// ███████║██║  ██║██║  ██║██████╔╝███████╗██║  ██║    ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝ ╚══════╝╚═╝  ╚═╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

shader_data_t *shader_data_create(shader_data_t *raw) {
	for (i32 i = 0; i < raw->contexts->length; ++i) {
		shader_context_t *c = (shader_context_t *)raw->contexts->buffer[i];
		shader_context_load(c);
	}
	return raw;
}

char *shader_data_ext() {
#ifdef IRON_VULKAN
	return ".spirv";
#elif defined(IRON_METAL)
	return ".metal";
#elif defined(IRON_WASM)
	return ".wgsl";
#else
	return ".d3d11";
#endif
}

shader_data_t *shader_data_parse(char *file, char *name) {
	scene_t       *format = data_get_scene_raw(file);
	shader_data_t *raw    = shader_data_get_raw_by_name(format->shader_datas, name);
	if (raw == NULL) {
		iron_log("Shader data '%s' not found!", name);
		return NULL;
	}
	return shader_data_create(raw);
}

shader_data_t *shader_data_get_raw_by_name(shader_data_t_array_t *datas, char *name) {
	if (strcmp(name, "") == 0) {
		return (shader_data_t *)datas->buffer[0];
	}
	for (i32 i = 0; i < datas->length; ++i) {
		shader_data_t *d = (shader_data_t *)datas->buffer[i];
		if (strcmp(d->name, name) == 0) {
			return d;
		}
	}
	return NULL;
}

void shader_data_delete(shader_data_t *raw) {
	for (i32 i = 0; i < raw->contexts->length; ++i) {
		shader_context_t *c = (shader_context_t *)raw->contexts->buffer[i];
		shader_context_delete(c);
	}
}

shader_context_t *shader_data_get_context(shader_data_t *raw, char *name) {
	for (i32 i = 0; i < raw->contexts->length; ++i) {
		shader_context_t *c = (shader_context_t *)raw->contexts->buffer[i];
		if (strcmp(c->name, name) == 0) {
			return c;
		}
	}
	return NULL;
}

void shader_context_load(shader_context_t *raw) {
	if (raw->_ == NULL) {
		raw->_ = gc_alloc(sizeof(shader_context_runtime_t));
	}
	shader_context_parse_vertex_struct(raw);
	shader_context_compile(raw);
}

static void shader_context_delete_shaders(shader_context_t *raw) {
#ifdef arm_embed
	if (!raw->shader_from_source) {
		return; // Owned by sys_get_shader
	}
#endif
	if (raw->_->pipe->fragment_shader != NULL) {
		gpu_delete_shader(raw->_->pipe->fragment_shader);
		raw->_->pipe->fragment_shader = NULL;
	}
	if (raw->_->pipe->vertex_shader != NULL) {
		gpu_delete_shader(raw->_->pipe->vertex_shader);
		raw->_->pipe->vertex_shader = NULL;
	}
}

void shader_context_compile(shader_context_t *raw) {
	raw->_->pipe               = gpu_create_pipeline();
	raw->_->constants          = i32_array_create(0);
	raw->_->tex_units          = i32_array_create(0);
	raw->_->pipe->input_layout = &raw->_->structure;
	raw->_->pipe->depth_write  = raw->depth_write;
	raw->_->pipe->depth_mode   = shader_context_get_compare_mode(raw->compare_mode);
	raw->_->pipe->cull_mode    = shader_context_get_cull_mode(raw->cull_mode);
	if (raw->blend_source != NULL) {
		raw->_->pipe->blend_source = shader_context_get_blend_fac(raw->blend_source);
	}
	if (raw->blend_destination != NULL) {
		raw->_->pipe->blend_destination = shader_context_get_blend_fac(raw->blend_destination);
	}
	if (raw->alpha_blend_source != NULL) {
		raw->_->pipe->alpha_blend_source = shader_context_get_blend_fac(raw->alpha_blend_source);
	}
	if (raw->alpha_blend_destination != NULL) {
		raw->_->pipe->alpha_blend_destination = shader_context_get_blend_fac(raw->alpha_blend_destination);
	}
	if (raw->color_writes_red != NULL) {
		for (i32 i = 0; i < raw->color_writes_red->length; ++i) {
			raw->_->pipe->color_write_mask_red[i] = (bool)(intptr_t)raw->color_writes_red->buffer[i];
		}
	}
	if (raw->color_writes_green != NULL) {
		for (i32 i = 0; i < raw->color_writes_green->length; ++i) {
			raw->_->pipe->color_write_mask_green[i] = (bool)(intptr_t)raw->color_writes_green->buffer[i];
		}
	}
	if (raw->color_writes_blue != NULL) {
		for (i32 i = 0; i < raw->color_writes_blue->length; ++i) {
			raw->_->pipe->color_write_mask_blue[i] = (bool)(intptr_t)raw->color_writes_blue->buffer[i];
		}
	}
	if (raw->color_writes_alpha != NULL) {
		for (i32 i = 0; i < raw->color_writes_alpha->length; ++i) {
			raw->_->pipe->color_write_mask_alpha[i] = (bool)(intptr_t)raw->color_writes_alpha->buffer[i];
		}
	}
	if (raw->color_attachments != NULL) {
		raw->_->pipe->color_attachment_count = raw->color_attachments->length;
		for (i32 i = 0; i < raw->color_attachments->length; ++i) {
			raw->_->pipe->color_attachment[i] = shader_context_get_tex_format((char *)raw->color_attachments->buffer[i]);
		}
	}
	if (raw->depth_attachment != NULL) {
		raw->_->pipe->depth_attachment_bits = strcmp(raw->depth_attachment, "NONE") == 0 ? 0 : 32;
	}

	if (raw->shader_from_source) {

#ifdef IRON_WASM
		raw->_->pipe->vertex_shader   = gpu_create_shader_from_source(raw->vertex_shader, raw->_->vertex_shader_size, GPU_SHADER_TYPE_VERTEX);
		raw->_->pipe->fragment_shader = gpu_create_shader_from_source(raw->fragment_shader, raw->_->fragment_shader_size, GPU_SHADER_TYPE_FRAGMENT);
#else
		shader_compile_job_t vs_job = {raw->vertex_shader, raw->_->vertex_shader_size, GPU_SHADER_TYPE_VERTEX, NULL};
		shader_compile_job_t fs_job = {raw->fragment_shader, raw->_->fragment_shader_size, GPU_SHADER_TYPE_FRAGMENT, NULL};
		iron_thread_t        vs_thread;
		iron_thread_init(&vs_thread, shader_compile_worker, &vs_job);
		shader_compile_worker(&fs_job);
		iron_thread_wait_and_destroy(&vs_thread);
		raw->_->pipe->vertex_shader   = vs_job.result;
		raw->_->pipe->fragment_shader = fs_job.result;
#endif

#ifdef IRON_WASM
		free(raw->vertex_shader);
		raw->vertex_shader = NULL;
		free(raw->fragment_shader);
		raw->fragment_shader = NULL;
#endif

		if (raw->_->pipe->vertex_shader == NULL || raw->_->pipe->fragment_shader == NULL) {
			return;
		}
	}
	else {
#ifdef arm_embed
		raw->_->pipe->fragment_shader = sys_get_shader(raw->fragment_shader);
		raw->_->pipe->vertex_shader   = sys_get_shader(raw->vertex_shader);
#else
		buffer_t *vs_buffer           = data_get_blob(string("%s%s", raw->vertex_shader, shader_data_ext()));
		raw->_->pipe->vertex_shader   = gpu_create_shader(vs_buffer, GPU_SHADER_TYPE_VERTEX);
		buffer_t *fs_buffer           = data_get_blob(string("%s%s", raw->fragment_shader, shader_data_ext()));
		raw->_->pipe->fragment_shader = gpu_create_shader(fs_buffer, GPU_SHADER_TYPE_FRAGMENT);
#endif
	}

	shader_context_finish_compile(raw);
}

i32 shader_context_type_size(char *t) {
	if (strcmp(t, "int") == 0)
		return 4;
	if (strcmp(t, "float") == 0)
		return 4;
	if (strcmp(t, "float2") == 0)
		return 8;
	if (strcmp(t, "float4") == 0)
		return 16;
	if (strcmp(t, "float4x4") == 0)
		return 64;
#ifdef IRON_DIRECT3D12
	if (strcmp(t, "float3") == 0)
		return 12;
	if (strcmp(t, "float3x3") == 0)
		return 44; // 16 + 16 + 12
#else
	if (strcmp(t, "float3") == 0)
		return 16;
	if (strcmp(t, "float3x3") == 0)
		return 48; // std140 array elements are padded to 16
#endif
	return 0;
}

i32 shader_context_type_pad(i32 offset, i32 size) {
#ifdef IRON_DIRECT3D12
	i32 r = offset % 16;
	if (r == 0) {
		return 0;
	}
	if (size >= 16 || r + size > 16) {
		return 16 - r;
	}
	return 0;
#else
	if (size > 16) {
		size = 16;
	}
	return (size - (offset % size)) % size;
#endif
}

void shader_context_finish_compile(shader_context_t *raw) {
	gpu_pipeline_compile(raw->_->pipe);
	if (raw->constants != NULL) {
		i32 offset = 0;
		for (i32 i = 0; i < raw->constants->length; ++i) {
			shader_const_t *c    = (shader_const_t *)raw->constants->buffer[i];
			i32             size = shader_context_type_size(c->type);
			offset += shader_context_type_pad(offset, size);
			shader_context_add_const(raw, offset);
			offset += size;
		}
	}
	if (raw->texture_units != NULL) {
		for (i32 i = 0; i < raw->texture_units->length; ++i) {
			shader_context_add_tex(raw, i);
		}
	}
}

gpu_vertex_data_t shader_context_parse_data(char *data) {
	if (strcmp(data, "float1") == 0)
		return GPU_VERTEX_DATA_F32_1X;
	if (strcmp(data, "float2") == 0)
		return GPU_VERTEX_DATA_F32_2X;
	if (strcmp(data, "float3") == 0)
		return GPU_VERTEX_DATA_F32_3X;
	if (strcmp(data, "float4") == 0)
		return GPU_VERTEX_DATA_F32_4X;
	if (strcmp(data, "short2norm") == 0)
		return GPU_VERTEX_DATA_I16_2X_NORM;
	if (strcmp(data, "short4norm") == 0)
		return GPU_VERTEX_DATA_I16_4X_NORM;
	return GPU_VERTEX_DATA_F32_1X;
}

void shader_context_parse_vertex_struct(shader_context_t *raw) {
	raw->_->structure = (gpu_vertex_structure_t){0};
	for (i32 i = 0; i < raw->vertex_elements->length; ++i) {
		vertex_element_t *elem = (vertex_element_t *)raw->vertex_elements->buffer[i];
		gpu_vertex_structure_add(&raw->_->structure, elem->name, shader_context_parse_data(elem->data));
	}
}

void shader_context_delete(shader_context_t *raw) {
	shader_context_delete_shaders(raw);
	gpu_delete_pipeline(raw->_->pipe);
}

gpu_compare_mode_t shader_context_get_compare_mode(char *s) {
	if (strcmp(s, "always") == 0)
		return GPU_COMPARE_MODE_ALWAYS;
	if (strcmp(s, "never") == 0)
		return GPU_COMPARE_MODE_NEVER;
	if (strcmp(s, "equal") == 0)
		return GPU_COMPARE_MODE_EQUAL;
	return GPU_COMPARE_MODE_LESS;
}

gpu_cull_mode_t shader_context_get_cull_mode(char *s) {
	if (strcmp(s, "none") == 0)
		return GPU_CULL_MODE_NONE;
	if (strcmp(s, "clockwise") == 0)
		return GPU_CULL_MODE_CLOCKWISE;
	return GPU_CULL_MODE_COUNTER_CLOCKWISE;
}

gpu_blend_t shader_context_get_blend_fac(char *s) {
	if (strcmp(s, "blend_one") == 0)
		return GPU_BLEND_ONE;
	if (strcmp(s, "blend_zero") == 0)
		return GPU_BLEND_ZERO;
	if (strcmp(s, "source_alpha") == 0)
		return GPU_BLEND_SOURCE_ALPHA;
	if (strcmp(s, "destination_alpha") == 0)
		return GPU_BLEND_DEST_ALPHA;
	if (strcmp(s, "inverse_source_alpha") == 0)
		return GPU_BLEND_INV_SOURCE_ALPHA;
	if (strcmp(s, "inverse_destination_alpha") == 0)
		return GPU_BLEND_INV_DEST_ALPHA;
	return GPU_BLEND_ONE;
}

gpu_texture_format_t shader_context_get_tex_format(char *s) {
	if (strcmp(s, "RGBA32") == 0)
		return GPU_TEXTURE_FORMAT_RGBA32;
	if (strcmp(s, "RGBA64") == 0)
		return GPU_TEXTURE_FORMAT_RGBA64;
	if (strcmp(s, "RGBA128") == 0)
		return GPU_TEXTURE_FORMAT_RGBA128;
	if (strcmp(s, "R32") == 0)
		return GPU_TEXTURE_FORMAT_R32;
	if (strcmp(s, "R16") == 0)
		return GPU_TEXTURE_FORMAT_R16;
	if (strcmp(s, "R8") == 0)
		return GPU_TEXTURE_FORMAT_R8;
	return GPU_TEXTURE_FORMAT_RGBA32;
}

void shader_context_add_const(shader_context_t *raw, i32 offset) {
	i32_array_push(raw->_->constants, offset);
}

void shader_context_add_tex(shader_context_t *raw, i32 i) {
	i32_array_push(raw->_->tex_units, i);
}

// ███╗   ███╗███████╗███████╗██╗  ██╗    ██████╗  █████╗ ████████╗ █████╗
// ████╗ ████║██╔════╝██╔════╝██║  ██║    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██╔████╔██║█████╗  ███████╗███████║    ██║  ██║███████║   ██║   ███████║
// ██║╚██╔╝██║██╔══╝  ╚════██║██╔══██║    ██║  ██║██╔══██║   ██║   ██╔══██║
// ██║ ╚═╝ ██║███████╗███████║██║  ██║    ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚═╝     ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

mesh_data_t *mesh_data_parse(char *name, char *id) {
	scene_t     *format = data_get_scene_raw(name);
	mesh_data_t *raw    = mesh_data_get_raw_by_name(format->mesh_datas, id);
	if (raw == NULL) {
		iron_log("Mesh data '%s' not found!", id);
		return NULL;
	}
	return mesh_data_create(raw);
}

mesh_data_t *mesh_data_get_raw_by_name(any_array_t *datas, char *name) {
	if (string_equals(name, "")) {
		return (mesh_data_t *)datas->buffer[0];
	}
	for (i32 i = 0; i < datas->length; ++i) {
		mesh_data_t *d = (mesh_data_t *)datas->buffer[i];
		if (string_equals(d->name, name)) {
			return d;
		}
	}
	return NULL;
}

mesh_data_t *mesh_data_create(mesh_data_t *raw) {
	raw->_ = gc_alloc(sizeof(mesh_data_runtime_t));
	if (raw->scale_pos == 0.0) {
		raw->scale_pos = 1.0;
	}
	if (raw->scale_tex == 0.0) {
		raw->scale_tex = 1.0;
	}
	raw->_->structure = mesh_data_get_vertex_struct(raw->vertex_arrays);
	mesh_data_build(raw);
	return raw;
}

gpu_vertex_structure_t mesh_data_get_vertex_struct(any_array_t *vertex_arrays) {
	gpu_vertex_structure_t structure = {0};
	for (i32 i = 0; i < vertex_arrays->length; ++i) {
		vertex_array_t *va = (vertex_array_t *)vertex_arrays->buffer[i];
		gpu_vertex_structure_add(&structure, va->attrib, mesh_data_get_vertex_data(va->data));
	}
	return structure;
}

gpu_vertex_data_t mesh_data_get_vertex_data(char *data) {
	if (string_equals(data, "short4norm")) {
		return GPU_VERTEX_DATA_I16_4X_NORM;
	}
	return GPU_VERTEX_DATA_I16_2X_NORM; // short2norm
}

i32 mesh_data_get_vertex_size(char *vertex_data) {
	if (string_equals(vertex_data, "short4norm")) {
		return 4;
	}
	return 2; // short2norm
}

void mesh_data_build_vertices(gpu_buffer_t *vertex_buffer, any_array_t *vertex_arrays) {
	vertex_array_t *va0       = (vertex_array_t *)vertex_arrays->buffer[0];
	int16_t        *vertices  = gpu_vertex_buffer_lock(vertex_buffer);
	i32             size      = mesh_data_get_vertex_size(va0->data);
	i32             num_verts = va0->values->length / size;
	i32             di        = -1;
	for (i32 i = 0; i < num_verts; ++i) {
		for (i32 va = 0; va < vertex_arrays->length; ++va) {
			vertex_array_t *v = (vertex_array_t *)vertex_arrays->buffer[va];
			i32             l = mesh_data_get_vertex_size(v->data);
			for (i32 o = 0; o < l; ++o) {
				vertices[++di] = v->values->buffer[i * l + o];
			}
		}
	}
	gpu_vertex_buffer_unlock(vertex_buffer);
}

void mesh_data_build_indices(gpu_buffer_t *index_buffer, u32_array_t *index_array) {
	uint32_t *ia = gpu_index_buffer_lock(index_buffer);
	memcpy(ia, index_array->buffer, index_buffer->count * 4);
	gpu_index_buffer_unlock(index_buffer);
}

vertex_array_t *mesh_data_get_vertex_array(mesh_data_t *raw, char *name) {
	for (i32 i = 0; i < raw->vertex_arrays->length; ++i) {
		vertex_array_t *va = (vertex_array_t *)raw->vertex_arrays->buffer[i];
		if (string_equals(va->attrib, name)) {
			return va;
		}
	}
	return NULL;
}

void mesh_data_build(mesh_data_t *raw) {
	vertex_array_t *positions = mesh_data_get_vertex_array(raw, "pos");
	i32             size      = mesh_data_get_vertex_size(positions->data);
	raw->_->vertex_buffer     = gpu_create_vertex_buffer(positions->values->length / size, &raw->_->structure);
	mesh_data_build_vertices(raw->_->vertex_buffer, raw->vertex_arrays);
	raw->_->index_buffer = gpu_create_index_buffer(raw->index_array->length);
	mesh_data_build_indices(raw->_->index_buffer, raw->index_array);
}

vec4_t mesh_data_calculate_aabb(mesh_data_t *raw) {
	vec4_t          aabb_min  = (vec4_t){-0.01, -0.01, -0.01, 0.0};
	vec4_t          aabb_max  = (vec4_t){0.01, 0.01, 0.01, 0.0};
	vec4_t          aabb      = (vec4_t){0.0, 0.0, 0.0, 0.0};
	i32             i         = 0;
	vertex_array_t *positions = mesh_data_get_vertex_array(raw, "pos");
	while (i < positions->values->length) {
		if (positions->values->buffer[i] > aabb_max.x) {
			aabb_max.x = positions->values->buffer[i];
		}
		if (positions->values->buffer[i + 1] > aabb_max.y) {
			aabb_max.y = positions->values->buffer[i + 1];
		}
		if (positions->values->buffer[i + 2] > aabb_max.z) {
			aabb_max.z = positions->values->buffer[i + 2];
		}
		if (positions->values->buffer[i] < aabb_min.x) {
			aabb_min.x = positions->values->buffer[i];
		}
		if (positions->values->buffer[i + 1] < aabb_min.y) {
			aabb_min.y = positions->values->buffer[i + 1];
		}
		if (positions->values->buffer[i + 2] < aabb_min.z) {
			aabb_min.z = positions->values->buffer[i + 2];
		}
		i += 4;
	}
	aabb.x = (fabsf(aabb_min.x) + fabsf(aabb_max.x)) / 32767 * raw->scale_pos;
	aabb.y = (fabsf(aabb_min.y) + fabsf(aabb_max.y)) / 32767 * raw->scale_pos;
	aabb.z = (fabsf(aabb_min.z) + fabsf(aabb_max.z)) / 32767 * raw->scale_pos;
	return aabb;
}

void mesh_data_delete(mesh_data_t *raw) {
	gpu_delete_buffer(raw->_->vertex_buffer);
	gpu_delete_buffer(raw->_->index_buffer);
}

// ███╗   ███╗███████╗███████╗██╗  ██╗     ██████╗ ██████╗      ██╗███████╗ ██████╗████████╗
// ████╗ ████║██╔════╝██╔════╝██║  ██║    ██╔═══██╗██╔══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██╔████╔██║█████╗  ███████╗███████║    ██║   ██║██████╔╝     ██║█████╗  ██║        ██║
// ██║╚██╔╝██║██╔══╝  ╚════██║██╔══██║    ██║   ██║██╔══██╗██   ██║██╔══╝  ██║        ██║
// ██║ ╚═╝ ██║███████╗███████║██║  ██║    ╚██████╔╝██████╔╝╚█████╔╝███████╗╚██████╗   ██║
// ╚═╝     ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝     ╚═════╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

mesh_object_t *mesh_object_create(mesh_data_t *data, material_data_t *material) {
	mesh_object_t *raw   = gc_alloc(sizeof(mesh_object_t));
	raw->frustum_culling = true;
	raw->base            = object_create(false);
	raw->base->ext       = raw;
	raw->base->ext_type  = "mesh_object_t";
	raw->material        = material;
	mesh_object_set_data(raw, data);
	any_array_push(scene_meshes, raw);
	return raw;
}

void mesh_object_set_data(mesh_object_t *raw, mesh_data_t *data) {
	raw->data = data;
	// Scale-up packed (-1,1) mesh coords
	raw->base->transform->scale_world = data->scale_pos;
}

void mesh_object_remove(mesh_object_t *raw) {
	array_remove(scene_meshes, raw);
	object_remove_super(raw->base);
}

bool mesh_object_cull_material(mesh_object_t *raw, char *context) {
	// Skip render if material does not contain current context
	if (!mesh_object_valid_context(raw, raw->material, context)) {
		raw->base->culled = true;
		return true;
	}
	if (raw->skip_context != NULL && string_equals(raw->skip_context, context)) {
		raw->base->culled = true;
		return true;
	}
	if (raw->force_context != NULL && !string_equals(raw->force_context, context)) {
		raw->base->culled = true;
		return true;
	}
	raw->base->culled = false;
	return false;
}

bool mesh_object_cull_mesh(mesh_object_t *raw, char *context, camera_object_t *camera) {
	if (camera->data->frustum_culling && raw->frustum_culling) {
		f32 radius_scale = 1.0;
		if (!camera_object_sphere_in_frustum(&camera->frustum_planes, raw->base->transform, radius_scale, 0.0, 0.0, 0.0)) {
			raw->base->culled = true;
			return true;
		}
	}
	raw->base->culled = false;
	return false;
}

void mesh_object_render(mesh_object_t *raw, char *context, string_array_t *bind_params) {
	if (!raw->base->visible) {
		return; // Skip render if object is hidden
	}
	if (mesh_object_cull_mesh(raw, context, scene_camera)) {
		return;
	}
	if (mesh_object_cull_material(raw, context)) {
		return;
	}

	uniforms_pos_unpack = raw->data->scale_pos;
	uniforms_tex_unpack = raw->data->scale_tex;
	transform_update(raw->base->transform);

	shader_context_t   *scontext = NULL;
	material_context_t *mcontext = NULL;
	material_data_t    *mat      = raw->material;
	for (i32 j = 0; j < (i32)mat->contexts->length; ++j) {
		material_context_t *c = (material_context_t *)mat->contexts->buffer[j];
		if (string_equals(c->name, context)) {
			scontext = shader_data_get_context(mat->_->shader, context);
			mcontext = c;
			break;
		}
	}

	if (scontext->_->pipe != _mesh_object_last_pipeline) {
		gpu_set_pipeline(scontext->_->pipe);
		gc_unroot(_mesh_object_last_pipeline);
		_mesh_object_last_pipeline = scontext->_->pipe;
		gc_root(_mesh_object_last_pipeline);
	}
	uniforms_set_context_consts(scontext, bind_params);
	uniforms_set_obj_consts(scontext, raw->base);
	uniforms_set_material_consts(scontext, mcontext);
	gpu_set_vertex_buffer(raw->data->_->vertex_buffer);
	gpu_set_index_buffer(raw->data->_->index_buffer);
	gpu_draw();
}

bool mesh_object_valid_context(mesh_object_t *raw, material_data_t *mat, char *context) {
	return material_data_get_context(mat, context) != NULL;
}

//  ██████╗ █████╗ ███╗   ███╗███████╗██████╗  █████╗      ██████╗ ██████╗      ██╗███████╗ ██████╗████████╗
// ██╔════╝██╔══██╗████╗ ████║██╔════╝██╔══██╗██╔══██╗    ██╔═══██╗██╔══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██║     ███████║██╔████╔██║█████╗  ██████╔╝███████║    ██║   ██║██████╔╝     ██║█████╗  ██║        ██║
// ██║     ██╔══██║██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║    ██║   ██║██╔══██╗██   ██║██╔══╝  ██║        ██║
// ╚██████╗██║  ██║██║ ╚═╝ ██║███████╗██║  ██║██║  ██║    ╚██████╔╝██████╔╝╚█████╔╝███████╗╚██████╗   ██║
//  ╚═════╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝     ╚═════╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

camera_object_t *camera_object_create(camera_data_t *data) {
	camera_object_t *raw = gc_alloc(sizeof(camera_object_t));
	raw->no_jitter_p     = mat4_identity();
	raw->frame           = 0;
	raw->base            = object_create(false);
	raw->base->ext       = raw;
	raw->base->ext_type  = "camera_object_t";
	raw->data            = data;

	camera_object_build_proj(raw, -1.0);

	raw->v  = mat4_identity();
	raw->vp = mat4_identity();

	if (data->frustum_culling) {
		raw->frustum_planes = any_array_create(0);
		for (i32 i = 0; i < 6; ++i) {
			any_array_push(raw->frustum_planes, frustum_plane_create());
		}
	}

	any_array_push(scene_cameras, raw);
	return raw;
}

void camera_object_build_proj(camera_object_t *raw, f32 screen_aspect) {
	if (raw->data->ortho != NULL) {
		raw->p = mat4_ortho(raw->data->ortho->buffer[0], raw->data->ortho->buffer[1], raw->data->ortho->buffer[2], raw->data->ortho->buffer[3],
		                    -raw->data->far_plane, raw->data->far_plane);
	}
	else {
		if (screen_aspect < 0) {
			screen_aspect = (f32)sys_w() / (f32)sys_h();
		}
		f32 aspect = raw->data->aspect != 0.0 ? raw->data->aspect : screen_aspect;
		raw->p     = mat4_persp(raw->data->fov, aspect, raw->data->near_plane, raw->data->far_plane);
	}
	raw->no_jitter_p = raw->p;
}

void camera_object_remove(camera_object_t *raw) {
	array_remove(scene_cameras, raw);
	object_remove_super(raw->base);
}

void camera_object_proj_jitter(camera_object_t *raw) {
	i32 w  = render_path_current_w;
	i32 h  = render_path_current_h;
	raw->p = raw->no_jitter_p;
	i32 i  = raw->frame % 2;
	f32 x  = i == 0 ? -0.5 : 0.5;
	f32 y  = i == 0 ? 0.5 : -0.5;
	raw->p.m20 += x / w;
	raw->p.m21 += y / h;
}

void camera_object_build_mat(camera_object_t *raw) {
	transform_build_matrix(raw->base->transform);

	raw->v  = mat4_inv(raw->base->transform->world);
	raw->vp = mat4_mult_mat(raw->v, raw->p);

	if (raw->data->frustum_culling) {
		camera_object_build_view_frustum(raw->vp, raw->frustum_planes);
	}
}

vec4_t camera_object_right(camera_object_t *raw) {
	return vec4_norm((vec4_t){raw->base->transform->local.m00, raw->base->transform->local.m01, raw->base->transform->local.m02, 0.0});
}

vec4_t camera_object_up(camera_object_t *raw) {
	return vec4_norm((vec4_t){raw->base->transform->local.m10, raw->base->transform->local.m11, raw->base->transform->local.m12, 0.0});
}

vec4_t camera_object_look(camera_object_t *raw) {
	return vec4_norm((vec4_t){-raw->base->transform->local.m20, -raw->base->transform->local.m21, -raw->base->transform->local.m22, 0.0});
}

vec4_t camera_object_right_world(camera_object_t *raw) {
	return vec4_norm((vec4_t){raw->base->transform->world.m00, raw->base->transform->world.m01, raw->base->transform->world.m02, 0.0});
}

vec4_t camera_object_up_world(camera_object_t *raw) {
	return vec4_norm((vec4_t){raw->base->transform->world.m10, raw->base->transform->world.m11, raw->base->transform->world.m12, 0.0});
}

vec4_t camera_object_look_world(camera_object_t *raw) {
	return vec4_norm((vec4_t){-raw->base->transform->world.m20, -raw->base->transform->world.m21, -raw->base->transform->world.m22, 0.0});
}

void camera_object_build_view_frustum(mat4_t vp, frustum_plane_array_t *frustum_planes) {
	// Left plane
	frustum_plane_set_components(frustum_planes->buffer[0], vp.m03 + vp.m00, vp.m13 + vp.m10, vp.m23 + vp.m20, vp.m33 + vp.m30);
	// Right plane
	frustum_plane_set_components(frustum_planes->buffer[1], vp.m03 - vp.m00, vp.m13 - vp.m10, vp.m23 - vp.m20, vp.m33 - vp.m30);
	// Top plane
	frustum_plane_set_components(frustum_planes->buffer[2], vp.m03 - vp.m01, vp.m13 - vp.m11, vp.m23 - vp.m21, vp.m33 - vp.m31);
	// Bottom plane
	frustum_plane_set_components(frustum_planes->buffer[3], vp.m03 + vp.m01, vp.m13 + vp.m11, vp.m23 + vp.m21, vp.m33 + vp.m31);
	// Near plane
	frustum_plane_set_components(frustum_planes->buffer[4], vp.m02, vp.m12, vp.m22, vp.m32);
	// Far plane
	frustum_plane_set_components(frustum_planes->buffer[5], vp.m03 - vp.m02, vp.m13 - vp.m12, vp.m23 - vp.m22, vp.m33 - vp.m32);
	// Normalize planes
	for (i32 i = 0; i < frustum_planes->length; ++i) {
		frustum_plane_normalize(frustum_planes->buffer[i]);
	}
}

bool camera_object_sphere_in_frustum(frustum_plane_array_t *frustum_planes, transform_t *t, f32 radius_scale, f32 offset_x, f32 offset_y, f32 offset_z) {
	// Use scale when radius is changing
	f32 radius = t->radius * radius_scale;
	for (i32 i = 0; i < frustum_planes->length; ++i) {
		frustum_plane_t *plane       = frustum_planes->buffer[i];
		_camera_object_sphere_center = (vec4_t){transform_world_x(t) + offset_x, transform_world_y(t) + offset_y, transform_world_z(t) + offset_z, 0.0};
		// Outside the frustum
		if (frustum_plane_dist_to_sphere(plane, _camera_object_sphere_center, radius) + radius * 2 < 0) {
			return false;
		}
	}
	return true;
}

// ███████╗██████╗ ██╗   ██╗███████╗████████╗██╗   ██╗███╗   ███╗    ██████╗ ██╗      █████╗ ███╗   ██╗███████╗
// ██╔════╝██╔══██╗██║   ██║██╔════╝╚══██╔══╝██║   ██║████╗ ████║    ██╔══██╗██║     ██╔══██╗████╗  ██║██╔════╝
// █████╗  ██████╔╝██║   ██║███████╗   ██║   ██║   ██║██╔████╔██║    ██████╔╝██║     ███████║██╔██╗ ██║█████╗
// ██╔══╝  ██╔══██╗██║   ██║╚════██║   ██║   ██║   ██║██║╚██╔╝██║    ██╔═══╝ ██║     ██╔══██║██║╚██╗██║██╔══╝
// ██║     ██║  ██║╚██████╔╝███████║   ██║   ╚██████╔╝██║ ╚═╝ ██║    ██║     ███████╗██║  ██║██║ ╚████║███████╗
// ╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚══════╝   ╚═╝    ╚═════╝ ╚═╝     ╚═╝    ╚═╝     ╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝

frustum_plane_t *frustum_plane_create() {
	frustum_plane_t *raw = gc_alloc(sizeof(frustum_plane_t));
	raw->normal          = (vec4_t){1.0, 0.0, 0.0, 0.0};
	raw->constant        = 0.0;
	return raw;
}

void frustum_plane_normalize(frustum_plane_t *raw) {
	f32 inv_normal_length = 1.0 / vec4_len(raw->normal);
	raw->normal           = vec4_mult(raw->normal, inv_normal_length);
	raw->constant *= inv_normal_length;
}

f32 frustum_plane_dist_to_sphere(frustum_plane_t *raw, vec4_t sphere_center, f32 sphere_radius) {
	return (vec4_dot(raw->normal, sphere_center) + raw->constant) - sphere_radius;
}

void frustum_plane_set_components(frustum_plane_t *raw, f32 x, f32 y, f32 z, f32 w) {
	raw->normal   = (vec4_t){x, y, z, 0.0};
	raw->constant = w;
}

// ██╗   ██╗███╗   ██╗██╗███████╗ ██████╗ ██████╗ ███╗   ███╗███████╗
// ██║   ██║████╗  ██║██║██╔════╝██╔═══██╗██╔══██╗████╗ ████║██╔════╝
// ██║   ██║██╔██╗ ██║██║█████╗  ██║   ██║██████╔╝██╔████╔██║███████╗
// ██║   ██║██║╚██╗██║██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║╚════██║
// ╚██████╔╝██║ ╚████║██║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║███████║
//  ╚═════╝ ╚═╝  ╚═══╝╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝

f32 uniforms_pos_unpack = 1.0;
f32 uniforms_tex_unpack = 1.0;

gpu_texture_t *(*uniforms_tex_links)(object_t *o, material_data_t *md, char *s)     = NULL;
mat4_t (*uniforms_mat4_links)(object_t *o, material_data_t *md, char *s)            = NULL;
vec4_t (*uniforms_vec4_links)(object_t *o, material_data_t *md, char *s)            = NULL;
vec4_t (*uniforms_vec3_links)(object_t *o, material_data_t *md, char *s)            = NULL;
vec2_t (*uniforms_vec2_links)(object_t *o, material_data_t *md, char *s)            = NULL;
f32 (*uniforms_f32_links)(object_t *o, material_data_t *md, char *s)                = NULL;
f32_array_t *(*uniforms_f32_array_links)(object_t *o, material_data_t *md, char *s) = NULL;
i32 (*uniforms_i32_links)(object_t *o, material_data_t *md, char *s)                = NULL;

void uniforms_set_context_consts(shader_context_t *context, string_array_t *bind_params) {
	// On shader compile error, _.constants.length will be 0
	if (context->constants != NULL && context->constants->length == context->_->constants->length) {
		for (i32 i = 0; i < context->constants->length; ++i) {
			shader_const_t *c = context->constants->buffer[i];
			uniforms_set_context_const(context->_->constants->buffer[i], c);
		}
	}

	// Texture context constants
	if (bind_params != NULL) { // Bind targets
		for (i32 i = 0; i < (i32)math_floor(bind_params->length / 2); ++i) {
			i32              pos        = i * 2; // bind params = [texture, sampler_id]
			char            *rt_id      = bind_params->buffer[pos];
			char            *sampler_id = bind_params->buffer[pos + 1];
			render_target_t *rt         = any_map_get(render_path_render_targets, rt_id);
			uniforms_bind_render_target(rt, context, sampler_id);
		}
	}

	// Texture links
	if (context->texture_units != NULL && context->texture_units->length == context->_->tex_units->length) {
		for (i32 j = 0; j < context->texture_units->length; ++j) {
			char *tulink = context->texture_units->buffer[j]->link;
			if (tulink == NULL) {
				continue;
			}

			if (tulink[0] == '$') { // Link to embedded data
				gpu_set_texture(context->_->tex_units->buffer[j], any_map_get(scene_embedded, substring(tulink, 1, string_length(tulink))));
			}
			else if (string_equals(tulink, "_envmap_radiance")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->radiance);
				}
			}
			else if (string_equals(tulink, "_envmap_radiance0")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->radiance_mipmaps->buffer[0]);
				}
			}
			else if (string_equals(tulink, "_envmap_radiance1")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->radiance_mipmaps->buffer[1]);
				}
			}
			else if (string_equals(tulink, "_envmap_radiance2")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->radiance_mipmaps->buffer[2]);
				}
			}
			else if (string_equals(tulink, "_envmap_radiance3")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->radiance_mipmaps->buffer[3]);
				}
			}
			else if (string_equals(tulink, "_envmap_radiance4")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->radiance_mipmaps->buffer[4]);
				}
			}
			else if (string_equals(tulink, "_envmap")) {
				world_data_t *w = scene_world;
				if (w != NULL) {
					gpu_set_texture(context->_->tex_units->buffer[j], w->_->envmap);
				}
			}
		}
	}
}

void uniforms_set_obj_consts(shader_context_t *context, object_t *object) {
	if (context->constants != NULL && context->constants->length == context->_->constants->length) {
		for (i32 i = 0; i < context->constants->length; ++i) {
			shader_const_t *c = context->constants->buffer[i];
			uniforms_set_obj_const(object, context->_->constants->buffer[i], c);
		}
	}

	// Texture object constants
	// External
	if (uniforms_tex_links != NULL && context->texture_units != NULL && context->texture_units->length == context->_->tex_units->length) {
		for (i32 j = 0; j < context->texture_units->length; ++j) {
			tex_unit_t *tu = context->texture_units->buffer[j];
			if (tu->link == NULL) {
				continue;
			}

			gpu_texture_t *image = uniforms_tex_links(object, current_material(object), tu->link);
			if (image != NULL) {
				gpu_set_texture(context->_->tex_units->buffer[j], image);
			}
		}
	}
}

void uniforms_bind_render_target(render_target_t *rt, shader_context_t *context, char *sampler_id) {
	if (rt == NULL) {
		return;
	}

	if (context->texture_units != NULL && context->texture_units->length == context->_->tex_units->length) {
		for (i32 j = 0; j < context->texture_units->length; ++j) { // Set texture
			if (string_equals(sampler_id, context->texture_units->buffer[j]->name)) {
				gpu_set_texture(context->_->tex_units->buffer[j], rt->_image);
			}
		}
	}
}

bool uniforms_set_context_const(i32 location, shader_const_t *c) {
	if (c->link == NULL) {
		return true;
	}

	camera_object_t *camera = scene_camera;

	if (string_equals(c->type, "float4x4")) {
		mat4_t m = mat4_nan();
		if (string_equals(c->link, "_view_matrix")) {
			m = camera->v;
		}
		else if (string_equals(c->link, "_proj_matrix")) {
			m = camera->p;
		}
		else if (string_equals(c->link, "_inv_proj_matrix")) {
			m = mat4_inv(camera->p);
		}
		else if (string_equals(c->link, "_view_proj_matrix")) {
			m = camera->vp;
		}
		else if (string_equals(c->link, "_inv_view_proj_matrix")) {
			m = mat4_mult_mat(camera->v, camera->p);
			m = mat4_inv(m);
		}
		else if (string_equals(c->link, "_skydome_matrix")) {
			transform_t *tr     = camera->base->transform;
			vec4_t       v      = (vec4_t){transform_world_x(tr), transform_world_y(tr), transform_world_z(tr), 0.0};
			f32          bounds = camera->data->far_plane * 0.9f;
			vec4_t       v2     = (vec4_t){bounds, bounds, bounds, 0.0};
			m                   = mat4_compose(v, (quat_t){0.0, 0.0, 0.0, 1.0}, v2);
			m                   = mat4_mult_mat(m, camera->v);
			m                   = mat4_mult_mat(m, camera->p);
		}
		else { // Unknown uniform
			return false;
		}

		gpu_set_mat4(location, m);
		return true;
	}
	else if (string_equals(c->type, "float4")) {
		vec4_t v = vec4_nan();
		if (string_equals(c->link, "_envmap_irradiance0")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[0];
			v.y             = fa->buffer[1];
			v.z             = fa->buffer[2];
			v.w             = fa->buffer[3];
		}
		else if (string_equals(c->link, "_envmap_irradiance1")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[4];
			v.y             = fa->buffer[5];
			v.z             = fa->buffer[6];
			v.w             = fa->buffer[7];
		}
		else if (string_equals(c->link, "_envmap_irradiance2")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[8];
			v.y             = fa->buffer[9];
			v.z             = fa->buffer[10];
			v.w             = fa->buffer[11];
		}
		else if (string_equals(c->link, "_envmap_irradiance3")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[12];
			v.y             = fa->buffer[13];
			v.z             = fa->buffer[14];
			v.w             = fa->buffer[15];
		}
		else if (string_equals(c->link, "_envmap_irradiance4")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[16];
			v.y             = fa->buffer[17];
			v.z             = fa->buffer[18];
			v.w             = fa->buffer[19];
		}
		else if (string_equals(c->link, "_envmap_irradiance5")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[20];
			v.y             = fa->buffer[21];
			v.z             = fa->buffer[22];
			v.w             = fa->buffer[23];
		}
		else if (string_equals(c->link, "_envmap_irradiance6")) {
			f32_array_t *fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
			v.x             = fa->buffer[24];
			v.y             = fa->buffer[25];
			v.z             = fa->buffer[26];
			v.w             = fa->buffer[27];
		}
		else {
			return false;
		}

		if (!vec4_isnan(v)) {
			gpu_set_float4(location, v.x, v.y, v.z, v.w);
		}
		else {
			gpu_set_float4(location, 0, 0, 0, 0);
		}
		return true;
	}
	else if (string_equals(c->type, "float3")) {
		vec4_t v = vec4_nan();

		if (string_equals(c->link, "_camera_pos")) {
			v = (vec4_t){transform_world_x(camera->base->transform), transform_world_y(camera->base->transform), transform_world_z(camera->base->transform),
			             0.0};
		}
		else if (string_equals(c->link, "_camera_look")) {
			v = vec4_norm(camera_object_look_world(camera));
		}
		else {
			return false;
		}

		if (!vec4_isnan(v)) {
			gpu_set_float3(location, v.x, v.y, v.z);
		}
		else {
			gpu_set_float3(location, 0.0, 0.0, 0.0);
		}
		return true;
	}
	else if (string_equals(c->type, "float2")) {
		vec4_t v = vec4_nan();

		if (string_equals(c->link, "_vec2x")) {
			v.x = 1.0;
			v.y = 0.0;
		}
		else if (string_equals(c->link, "_vec2x_inv")) {
			v.x = 1.0f / render_path_current_w;
			v.y = 0.0;
		}
		else if (string_equals(c->link, "_vec2x2")) {
			v.x = 2.0;
			v.y = 0.0;
		}
		else if (string_equals(c->link, "_vec2x2_inv")) {
			v.x = 2.0f / render_path_current_w;
			v.y = 0.0;
		}
		else if (string_equals(c->link, "_vec2y")) {
			v.x = 0.0;
			v.y = 1.0;
		}
		else if (string_equals(c->link, "_vec2y_inv")) {
			v.x = 0.0;
			v.y = 1.0f / render_path_current_h;
		}
		else if (string_equals(c->link, "_vec2y2")) {
			v.x = 0.0;
			v.y = 2.0;
		}
		else if (string_equals(c->link, "_vec2y2_inv")) {
			v.x = 0.0;
			v.y = 2.0f / render_path_current_h;
		}
		else if (string_equals(c->link, "_vec2y3")) {
			v.x = 0.0;
			v.y = 3.0;
		}
		else if (string_equals(c->link, "_vec2y3_inv")) {
			v.x = 0.0;
			v.y = 3.0f / render_path_current_h;
		}
		else if (string_equals(c->link, "_screen_size")) {
			v.x = (f32)render_path_current_w;
			v.y = (f32)render_path_current_h;
		}
		else if (string_equals(c->link, "_screen_size_inv")) {
			v.x = 1.0f / render_path_current_w;
			v.y = 1.0f / render_path_current_h;
		}
		else if (string_equals(c->link, "_camera_plane_proj")) {
			f32 znear = camera->data->near_plane;
			f32 zfar  = camera->data->far_plane;
			v.x       = zfar / (zfar - znear);
			v.y       = (-zfar * znear) / (zfar - znear);
		}
		else if (starts_with(c->link, "_size(")) {
			char          *tex   = substring(c->link, 6, string_length(c->link) - 1);
			gpu_texture_t *image = uniforms_tex_links != NULL ? uniforms_tex_links(NULL, NULL, tex) : NULL;
			if (image != NULL) {
				v.x = (f32)image->width;
				v.y = (f32)image->height;
			}
			else if (_render_path_bind_params != NULL) {
				for (i32 i = 0; i < (i32)math_floor(_render_path_bind_params->length / 2); ++i) {
					i32   pos        = i * 2; // bind params = [texture, sampler_id]
					char *sampler_id = _render_path_bind_params->buffer[pos + 1];
					if (string_equals(sampler_id, tex)) {
						char            *rt_id = _render_path_bind_params->buffer[pos];
						render_target_t *rt    = any_map_get(render_path_render_targets, rt_id);
						v.x                    = (f32)rt->width;
						v.y                    = (f32)rt->height;
					}
				}
			}
		}
		else {
			return false;
		}

		if (!vec4_isnan(v)) {
			gpu_set_float2(location, v.x, v.y);
		}
		else {
			gpu_set_float2(location, 0.0, 0.0);
		}
		return true;
	}
	else if (string_equals(c->type, "float")) {
		f32 f = 0.0;

		if (string_equals(c->link, "_time")) {
			f = sys_time();
		}
		else if (string_equals(c->link, "_aspect_ratio_window")) {
			f = (f32)sys_w() / (f32)sys_h();
		}
		else {
			return false;
		}

		gpu_set_float(location, f);
		return true;
	}
	else if (string_equals(c->type, "floats")) {
		f32_array_t *fa = NULL;

		if (string_equals(c->link, "_envmap_irradiance")) {
			fa = scene_world == NULL ? world_data_get_empty_irradiance() : scene_world->_->irradiance;
		}

		if (fa != NULL) {
			gpu_set_floats(location, fa);
			return true;
		}
	}
	else if (string_equals(c->type, "int")) {
		i32 i = 0;

		if (string_equals(c->link, "_envmap_num_mipmaps")) {
			world_data_t *w = scene_world;
			i               = w != NULL ? w->radiance_mipmaps + 1 - 2 : 1; // Include basecolor and exclude 2 scaled mips
		}
		else {
			return false;
		}

		gpu_set_int(location, i);
		return true;
	}
	return false;
}

vec4_t uniforms_vec4_zero_to_one(vec4_t v) {
	v.x = v.x == 0.0 ? 1.0 : v.x;
	v.y = v.y == 0.0 ? 1.0 : v.y;
	v.z = v.z == 0.0 ? 1.0 : v.z;
	return v;
}

void uniforms_set_obj_const(object_t *obj, i32 loc, shader_const_t *c) {
	if (c->link == NULL) {
		return;
	}

	camera_object_t *camera = scene_camera;
	if (string_equals(c->type, "float4x4")) {
		mat4_t m = mat4_nan();

		if (string_equals(c->link, "_world_matrix")) {
			m = obj->transform->world_unpack;
		}
		else if (string_equals(c->link, "_inv_world_matrix")) {
			m = mat4_inv(obj->transform->world_unpack);
		}
		else if (string_equals(c->link, "_world_view_proj_matrix")) {
			m = mat4_mult_mat(obj->transform->world_unpack, camera->v);
			m = mat4_mult_mat(m, camera->p);
		}
		else if (string_equals(c->link, "_world_wiew_matrix")) {
			m = mat4_mult_mat(obj->transform->world_unpack, camera->v);
		}
		else if (uniforms_mat4_links != NULL) {
			m = uniforms_mat4_links(obj, current_material(obj), c->link);
		}

		if (mat4_isnan(m)) {
			return;
		}
		gpu_set_mat4(loc, m);
	}
	else if (string_equals(c->type, "float3x3")) {
		mat3_t m = mat3_nan();

		if (string_equals(c->link, "_normal_matrix")) {
			mat4_t m4 = mat4_inv(obj->transform->world);
			m4        = mat4_transpose3(m4);
			m         = mat3_set_from4(m4);
		}
		else if (string_equals(c->link, "_view_matrix3")) {
			m = mat3_set_from4(camera->v);
		}

		if (mat3_isnan(m)) {
			return;
		}
		gpu_set_mat3(loc, m);
	}
	else if (string_equals(c->type, "float4")) {
		vec4_t v = vec4_nan();

		if (uniforms_vec4_links != NULL) {
			v = uniforms_vec4_links(obj, current_material(obj), c->link);
		}

		if (vec4_isnan(v)) {
			return;
		}
		gpu_set_float4(loc, v.x, v.y, v.z, v.w);
	}
	else if (string_equals(c->type, "float3")) {
		vec4_t v = vec4_nan();

		if (string_equals(c->link, "_dim")) { // Model space
			vec4_t d = obj->transform->dim;
			vec4_t s = obj->transform->scale;
			d        = uniforms_vec4_zero_to_one(d);
			s        = uniforms_vec4_zero_to_one(s);
			v        = (vec4_t){(d.x / s.x), (d.y / s.y), (d.z / s.z), 0.0};
		}
		else if (string_equals(c->link, "_half_dim")) { // Model space
			vec4_t d = obj->transform->dim;
			vec4_t s = obj->transform->scale;
			d        = uniforms_vec4_zero_to_one(d);
			s        = uniforms_vec4_zero_to_one(s);
			v        = (vec4_t){(d.x / s.x) / 2, (d.y / s.y) / 2, (d.z / s.z) / 2, 0.0};
		}
		else if (uniforms_vec3_links != NULL) {
			v = uniforms_vec3_links(obj, current_material(obj), c->link);
		}

		if (vec4_isnan(v)) {
			return;
		}
		gpu_set_float3(loc, v.x, v.y, v.z);
	}
	else if (string_equals(c->type, "float2")) {
		vec2_t v = vec2_nan();

		if (uniforms_vec2_links != NULL) {
			v = uniforms_vec2_links(obj, current_material(obj), c->link);
		}

		if (vec2_isnan(v)) {
			return;
		}
		gpu_set_float2(loc, v.x, v.y);
	}
	else if (string_equals(c->type, "float")) {
		f32 f = NAN;

		if (string_equals(c->link, "_object_info_index")) {
			f = (f32)obj->uid;
		}
		else if (string_equals(c->link, "_object_info_material_index")) {
			f = current_material(obj)->_->uid;
		}
		else if (string_equals(c->link, "_object_info_random")) {
			f = obj->urandom;
		}
		else if (string_equals(c->link, "_pos_unpack")) {
			f = uniforms_pos_unpack;
		}
		else if (string_equals(c->link, "_tex_unpack")) {
			f = uniforms_tex_unpack;
		}
		else if (uniforms_f32_links != NULL) {
			f = uniforms_f32_links(obj, current_material(obj), c->link);
		}

		if (isnan(f)) {
			return;
		}
		gpu_set_float(loc, f);
	}
	else if (string_equals(c->type, "floats")) {
		f32_array_t *fa = NULL;

		if (uniforms_f32_array_links != NULL) {
			fa = uniforms_f32_array_links(obj, current_material(obj), c->link);
		}

		if (fa == NULL) {
			return;
		}
		gpu_set_floats(loc, fa);
	}
	else if (string_equals(c->type, "int")) {
		i32 i = INT_MAX;

		if (string_equals(c->link, "_uid")) {
			i = obj->uid;
		}
		else if (uniforms_i32_links != NULL) {
			i = uniforms_i32_links(obj, current_material(obj), c->link);
		}

		if (i == INT_MAX) {
			return;
		}

		gpu_set_int(loc, i);
	}
}

void uniforms_set_material_consts(shader_context_t *context, material_context_t *material_context) {
	if (material_context->bind_constants != NULL) {
		for (i32 i = 0; i < material_context->bind_constants->length; ++i) {
			bind_const_t *matc = material_context->bind_constants->buffer[i];
			i32           pos  = -1;
			for (i32 j = 0; j < context->constants->length; ++j) {
				char *name = context->constants->buffer[j]->name;
				if (string_equals(name, matc->name)) {
					pos = j;
					break;
				}
			}
			if (pos == -1) {
				continue;
			}
			shader_const_t *c = context->constants->buffer[pos];

			uniforms_set_material_const(context->_->constants->buffer[pos], c, matc);
		}
	}

	if (material_context->_->textures != NULL) {
		for (i32 i = 0; i < material_context->_->textures->length; ++i) {
			char *mname = material_context->bind_textures->buffer[i]->name;

			for (i32 j = 0; j < context->_->tex_units->length; ++j) {
				char *sname = context->texture_units->buffer[j]->name;
				if (string_equals(mname, sname)) {
					gpu_set_texture(context->_->tex_units->buffer[j], material_context->_->textures->buffer[i]);
					break;
				}
			}
		}
	}
}

material_data_t *current_material(object_t *object) {
	if (object != NULL && object->ext != NULL) {
		mesh_object_t *mo = object->ext;
		return mo->material;
	}
	return NULL;
}

void uniforms_set_material_const(i32 location, shader_const_t *shader_const, bind_const_t *material_const) {
	if (string_equals(shader_const->type, "float4")) {
		gpu_set_float4(location, material_const->vec->buffer[0], material_const->vec->buffer[1], material_const->vec->buffer[2],
		               material_const->vec->buffer[3]);
	}
	else if (string_equals(shader_const->type, "float3")) {
		gpu_set_float3(location, material_const->vec->buffer[0], material_const->vec->buffer[1], material_const->vec->buffer[2]);
	}
	else if (string_equals(shader_const->type, "float2")) {
		gpu_set_float2(location, material_const->vec->buffer[0], material_const->vec->buffer[1]);
	}
	else if (string_equals(shader_const->type, "float")) {
		gpu_set_float(location, material_const->vec->buffer[0]);
	}
	else if (string_equals(shader_const->type, "bool")) {
		gpu_set_bool(location, material_const->vec->buffer[0] > 0.0f);
	}
	else if (string_equals(shader_const->type, "int")) {
		gpu_set_int(location, (i32)math_floor(material_const->vec->buffer[0]));
	}
}

// ██████╗  █████╗ ████████╗ █████╗
// ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██║  ██║███████║   ██║   ███████║
// ██║  ██║██╔══██║   ██║   ██╔══██║
// ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

any_map_t *data_cached_scene_raws = NULL;
any_map_t *data_cached_meshes     = NULL;
any_map_t *data_cached_cameras    = NULL;
any_map_t *data_cached_materials  = NULL;
any_map_t *data_cached_worlds     = NULL;
any_map_t *data_cached_shaders    = NULL;
any_map_t *data_cached_blobs      = NULL;
any_map_t *data_cached_textures   = NULL;
any_map_t *data_cached_videos     = NULL;
any_map_t *data_cached_fonts      = NULL;
any_map_t *data_cached_sounds     = NULL;
i32        data_assets_loaded     = 0;

buffer_t      *iron_load_blob(char *file);
gpu_texture_t *iron_load_texture(char *file);
void           gpu_delete_texture(gpu_texture_t *texture);
void          *iron_load_sound(char *file);

char *data_path(void) {
#ifdef IRON_ANDROID
	return "data" PATH_SEP;
#else
	return "." PATH_SEP "data" PATH_SEP;
#endif
}

bool data_is_abs(char *file) {
	return char_at(file, 0)[0] == '/' || char_at(file, 1)[0] == ':' || (char_at(file, 0)[0] == '\\' && char_at(file, 1)[0] == '\\');
}

bool data_is_up(char *file) {
	return char_at(file, 0)[0] == '.' && char_at(file, 1)[0] == '.';
}

char *data_resolve_path(char *file) {
	if (data_is_abs(file) || data_is_up(file)) {
		return file;
	}
	return string("%s%s", data_path(), file);
}

mesh_data_t *data_get_mesh(char *file, char *name) {
	if (data_cached_meshes == NULL) {
		data_cached_meshes = any_map_create();
		gc_root(data_cached_meshes);
	}
	char        *handle = string("%s%s", file, name);
	mesh_data_t *cached = (mesh_data_t *)any_map_get(data_cached_meshes, handle);
	if (cached != NULL) {
		return cached;
	}
	mesh_data_t *b = mesh_data_parse(file, name);
	any_map_set(data_cached_meshes, handle, b);
	b->_->handle = handle;
	return b;
}

camera_data_t *data_get_camera(char *file, char *name) {
	if (data_cached_cameras == NULL) {
		data_cached_cameras = any_map_create();
		gc_root(data_cached_cameras);
	}
	char          *handle = string("%s%s", file, name);
	camera_data_t *cached = (camera_data_t *)any_map_get(data_cached_cameras, handle);
	if (cached != NULL) {
		return cached;
	}
	camera_data_t *b = camera_data_parse(file, name);
	any_map_set(data_cached_cameras, handle, b);
	return b;
}

material_data_t *data_get_material(char *file, char *name) {
	if (data_cached_materials == NULL) {
		data_cached_materials = any_map_create();
		gc_root(data_cached_materials);
	}
	char            *handle = string("%s%s", file, name);
	material_data_t *cached = (material_data_t *)any_map_get(data_cached_materials, handle);
	if (cached != NULL) {
		return cached;
	}
	material_data_t *b = material_data_parse(file, name);
	any_map_set(data_cached_materials, handle, b);
	return b;
}

world_data_t *data_get_world(char *file, char *name) {
	if (data_cached_worlds == NULL) {
		data_cached_worlds = any_map_create();
		gc_root(data_cached_worlds);
	}
	char         *handle = string("%s%s", file, name);
	world_data_t *cached = (world_data_t *)any_map_get(data_cached_worlds, handle);
	if (cached != NULL) {
		return cached;
	}
	world_data_t *b = world_data_parse(file, name);
	any_map_set(data_cached_worlds, handle, b);
	return b;
}

shader_data_t *data_get_shader(char *file, char *name) {
	if (data_cached_shaders == NULL) {
		data_cached_shaders = any_map_create();
		gc_root(data_cached_shaders);
	}
	// Only one context override per shader data for now
	char          *handle = name; // Shader must have unique name
	shader_data_t *cached = (shader_data_t *)any_map_get(data_cached_shaders, handle);
	if (cached != NULL) {
		return cached;
	}
	shader_data_t *b = shader_data_parse(file, name);
	any_map_set(data_cached_shaders, handle, b);
	return b;
}

scene_t *data_get_scene_raw(char *file) {
	if (data_cached_scene_raws == NULL) {
		data_cached_scene_raws = any_map_create();
		gc_root(data_cached_scene_raws);
	}
	scene_t *cached = (scene_t *)any_map_get(data_cached_scene_raws, file);
	if (cached != NULL) {
		return cached;
	}
	// If no extension specified, set to .arm
	char     *ext    = ends_with(file, ".arm") ? "" : ".arm";
	buffer_t *b      = data_get_blob(string("%s%s", file, ext));
	scene_t  *parsed = (scene_t *)armpack_decode(b);
	any_map_set(data_cached_scene_raws, file, parsed);
	return parsed;
}

buffer_t *data_get_blob(char *file) {
	if (data_cached_blobs == NULL) {
		data_cached_blobs = any_map_create();
		gc_root(data_cached_blobs);
	}
	buffer_t *cached = (buffer_t *)any_map_get(data_cached_blobs, file);
	if (cached != NULL) {
		return cached;
	}
	buffer_t *b = iron_load_blob(data_resolve_path(file));
	any_map_set(data_cached_blobs, file, b);
	data_assets_loaded++;
	return b;
}

gpu_texture_t *data_get_texture(char *file) {
	if (data_cached_textures == NULL) {
		data_cached_textures = any_map_create();
		gc_root(data_cached_textures);
	}
	gpu_texture_t *cached = (gpu_texture_t *)any_map_get(data_cached_textures, file);
	if (cached != NULL) {
		return cached;
	}
	gpu_texture_t *b = iron_load_texture(data_resolve_path(file));
	if (b == NULL) {
		return NULL;
	}
	any_map_set(data_cached_textures, file, b);
	data_assets_loaded++;
	return b;
}

video_t *data_get_video(char *file) {
	if (data_cached_videos == NULL) {
		data_cached_videos = any_map_create();
		gc_root(data_cached_videos);
	}
	// Strip extension and use .webm
	char    *base   = substring(file, 0, string_length(file) - 4);
	char    *webm   = string("%s.webm", base);
	video_t *cached = (video_t *)any_map_get(data_cached_videos, webm);
	if (cached != NULL) {
		return cached;
	}
	// let b: video_t = iron_load_video(data_resolve_path(file));
	// map_set(data_cached_videos, file, b);
	// data_assets_loaded++;
	// return b;
	return NULL;
}

draw_font_t *data_get_font(char *file) {
	if (data_cached_fonts == NULL) {
		data_cached_fonts = any_map_create();
		gc_root(data_cached_fonts);
	}
	draw_font_t *cached = (draw_font_t *)any_map_get(data_cached_fonts, file);
	if (cached != NULL) {
		return cached;
	}
	buffer_t    *blob = iron_load_blob(data_resolve_path(file));
	draw_font_t *b    = gc_alloc(sizeof(draw_font_t));
	b->buf            = blob;
	b->index          = 0;
	any_map_set(data_cached_fonts, file, b);
	data_assets_loaded++;
	return b;
}

sound_t *data_get_sound(char *file) {
	if (data_cached_sounds == NULL) {
		data_cached_sounds = any_map_create();
		gc_root(data_cached_sounds);
	}
	sound_t *cached = (sound_t *)any_map_get(data_cached_sounds, file);
	if (cached != NULL) {
		return cached;
	}
	sound_t *b = iron_load_sound(data_resolve_path(file));
	any_map_set(data_cached_sounds, file, b);
	data_assets_loaded++;
	return b;
}

void data_delete_sound(char *handle) {
	if (data_cached_sounds == NULL) {
		return;
	}
	sound_t *sound = (sound_t *)any_map_get(data_cached_sounds, handle);
	if (sound == NULL) {
		return;
	}
#ifdef IRON_AUDIO
	iron_a1_sound_destroy(sound);
#endif
	map_delete(data_cached_sounds, handle);
}

void data_delete_mesh(char *handle) {
	if (data_cached_meshes == NULL) {
		return;
	}
	mesh_data_t *mesh = (mesh_data_t *)any_map_get(data_cached_meshes, handle);
	if (mesh == NULL) {
		return;
	}
	mesh_data_delete(mesh);
	map_delete(data_cached_meshes, handle);
}

void data_delete_blob(char *handle) {
	if (data_cached_blobs == NULL) {
		return;
	}
	buffer_t *blob = (buffer_t *)any_map_get(data_cached_blobs, handle);
	if (blob == NULL) {
		return;
	}
	map_delete(data_cached_blobs, handle);
}

void data_delete_texture(char *handle) {
	if (data_cached_textures == NULL) {
		return;
	}
	gpu_texture_t *image = (gpu_texture_t *)any_map_get(data_cached_textures, handle);
	if (image == NULL) {
		return;
	}
	gpu_delete_texture(image);
	map_delete(data_cached_textures, handle);
}

void data_delete_video(char *handle) {
	if (data_cached_videos == NULL) {
		return;
	}
	video_t *video = (video_t *)any_map_get(data_cached_videos, handle);
	if (video == NULL) {
		return;
	}
	video_unload(video);
	map_delete(data_cached_videos, handle);
}

void data_delete_font(char *handle) {
	if (data_cached_fonts == NULL) {
		return;
	}
	draw_font_t *font = (draw_font_t *)any_map_get(data_cached_fonts, handle);
	if (font == NULL) {
		return;
	}
	draw_font_destroy(font);
	map_delete(data_cached_fonts, handle);
}

// ███████╗ ██████╗███████╗███╗   ██╗███████╗
// ██╔════╝██╔════╝██╔════╝████╗  ██║██╔════╝
// ███████╗██║     █████╗  ██╔██╗ ██║█████╗
// ╚════██║██║     ██╔══╝  ██║╚██╗██║██╔══╝
// ███████║╚██████╗███████╗██║ ╚████║███████╗
// ╚══════╝ ╚═════╝╚══════╝╚═╝  ╚═══╝╚══════╝

camera_object_t *scene_camera             = NULL;
world_data_t    *scene_world              = NULL;
any_array_t     *scene_meshes             = NULL;
any_array_t     *scene_cameras            = NULL;
any_array_t     *scene_empties            = NULL;
any_map_t       *scene_embedded           = NULL;
i32              _scene_uid_counter       = 0;
i32              _scene_uid               = 0;
scene_t         *_scene_raw               = NULL;
object_t        *_scene_root              = NULL;
object_t        *_scene_scene_parent      = NULL;
i32              _scene_objects_traversed = 0;
i32              _scene_objects_count     = 0;

object_t *scene_create(scene_t *format) {
	_scene_uid = _scene_uid_counter++;

	gc_unroot(scene_meshes);
	scene_meshes = any_array_create(0);
	gc_root(scene_meshes);

	gc_unroot(scene_cameras);
	scene_cameras = any_array_create(0);
	gc_root(scene_cameras);

	gc_unroot(scene_empties);
	scene_empties = any_array_create(0);
	gc_root(scene_empties);

	gc_unroot(scene_embedded);
	scene_embedded = any_map_create();
	gc_root(scene_embedded);

	gc_unroot(_scene_root);
	_scene_root = object_create(true);
	gc_root(_scene_root);
	_scene_root->name = "Root";

	gc_unroot(_scene_raw);
	_scene_raw = format;
	gc_root(_scene_raw);

	gc_unroot(scene_world);
	scene_world = data_get_world(format->name, format->world_ref);
	gc_root(scene_world);

	// Startup scene
	object_t *scene_object = scene_add_scene(format->name, NULL);

	gc_unroot(scene_camera);
	scene_camera = (camera_object_t *)scene_cameras->buffer[0]; // format->camera_ref
	gc_root(scene_camera);

	gc_unroot(_scene_scene_parent);
	_scene_scene_parent = scene_object;
	gc_root(_scene_scene_parent);

	return scene_object;
}

void scene_remove(void) {
	for (i32 i = 0; i < scene_meshes->length; ++i) {
		mesh_object_t *o = (mesh_object_t *)scene_meshes->buffer[i];
		mesh_object_remove(o);
	}
	for (i32 i = 0; i < scene_cameras->length; ++i) {
		camera_object_t *o = (camera_object_t *)scene_cameras->buffer[i];
		camera_object_remove(o);
	}
	for (i32 i = 0; i < scene_empties->length; ++i) {
		object_t *o = (object_t *)scene_empties->buffer[i];
		object_remove(o);
	}
	object_remove(_scene_root);
}

object_t *scene_set_active(char *scene_name) {
	if (_scene_root != NULL) {
		scene_remove();
	}
	scene_t  *format = data_get_scene_raw(scene_name);
	object_t *o      = scene_create(format);
	return o;
}

void scene_render_frame(void) {
	if (render_path_commands == NULL) {
		return;
	}
	for (i32 i = 0; i < scene_empties->length; ++i) {
		object_t *e = (object_t *)scene_empties->buffer[i];
		if (e != NULL && e->parent != NULL) {
			transform_update(e->transform);
		}
	}

	camera_object_proj_jitter(scene_camera);
	camera_object_build_mat(scene_camera);
	render_path_render_frame();
	scene_camera->frame++;
}

object_t *scene_add_object(object_t *parent) {
	object_t *object = object_create(true);
	if (parent != NULL) {
		object_set_parent(object, parent);
	}
	else {
		object_set_parent(object, _scene_root);
	}
	return object;
}

object_t *scene_get_child(char *name) {
	return object_get_child(_scene_root, name);
}

mesh_object_t *scene_add_mesh_object(mesh_data_t *data, material_data_t *material, object_t *parent) {
	mesh_object_t *object = mesh_object_create(data, material);
	if (parent != NULL) {
		object_set_parent(object->base, parent);
	}
	else {
		object_set_parent(object->base, _scene_root);
	}
	return object;
}

camera_object_t *scene_add_camera_object(camera_data_t *data, object_t *parent) {
	camera_object_t *object = camera_object_create(data);
	if (parent != NULL) {
		object_set_parent(object->base, parent);
	}
	else {
		object_set_parent(object->base, _scene_root);
	}
	return object;
}

void scene_traverse_objects(scene_t *format, object_t *parent, any_array_t *objects) {
	if (objects == NULL) {
		return;
	}
	for (i32 i = 0; i < objects->length; ++i) {
		obj_t *o = (obj_t *)objects->buffer[i];
		if (!o->spawn) {
			continue; // Do not auto-create Scene object
		}
		object_t *object = scene_create_object(o, format, parent);
		scene_traverse_objects(format, object, (any_array_t *)(void *)o->children);
	}
}

object_t *scene_add_scene(char *scene_name, object_t *parent) {
	if (parent == NULL) {
		parent       = scene_add_object(NULL);
		parent->name = scene_name;
	}
	scene_t *format = data_get_scene_raw(scene_name);
	scene_load_embedded_data(format->embedded_datas); // Additional scene assets
	_scene_objects_traversed = 0;
	_scene_objects_count     = scene_get_objects_count(format->objects);

	if (format->objects != NULL && format->objects->length > 0) {
		scene_traverse_objects(format, parent, format->objects); // Scene objects
	}
	return parent;
}

i32 scene_get_objects_count(any_array_t *objects) {
	if (objects == NULL) {
		return 0;
	}
	i32 result = objects->length;
	for (i32 i = 0; i < objects->length; ++i) {
		obj_t *o = (obj_t *)objects->buffer[i];
		if (!o->spawn) {
			continue; // Do not count children of non-spawned objects
		}
		if (o->children != NULL) {
			result += scene_get_objects_count((any_array_t *)(void *)o->children);
		}
	}
	return result;
}

object_t *_scene_spawn_object_tree(obj_t *obj, object_t *parent, bool spawn_children) {
	object_t      *object       = scene_create_object(obj, _scene_raw, parent);
	obj_t_array_t *obj_children = obj->children;
	if (spawn_children && obj_children != NULL) {
		for (i32 i = 0; i < obj_children->length; ++i) {
			obj_t *child = (obj_t *)obj_children->buffer[i];
			_scene_spawn_object_tree(child, object, spawn_children);
		}
	}
	return object;
}

object_t *scene_spawn_object(char *name, object_t *parent, bool spawn_children) {
	obj_t *obj = scene_get_raw_object_by_name(_scene_raw, name);
	return _scene_spawn_object_tree(obj, parent, spawn_children);
}

obj_t *scene_get_raw_object_by_name(scene_t *format, char *name) {
	return scene_traverse_objs(format->objects, name);
}

obj_t *scene_traverse_objs(any_array_t *children, char *name) {
	for (i32 i = 0; i < children->length; ++i) {
		obj_t *o = (obj_t *)children->buffer[i];
		if (string_equals(o->name, name)) {
			return o;
		}
		if (o->children != NULL) {
			obj_t *res = scene_traverse_objs((any_array_t *)(void *)o->children, name);
			if (res != NULL) {
				return res;
			}
		}
	}
	return NULL;
}

object_t *scene_create_object(obj_t *o, scene_t *format, object_t *parent) {
	char *scene_name = format->name;

	if (string_equals(o->type, "camera_object")) {
		camera_data_t   *b      = data_get_camera(scene_name, o->data_ref);
		camera_object_t *object = scene_add_camera_object(b, parent);
		return scene_return_object(object->base, o);
	}
	else if (string_equals(o->type, "mesh_object")) {
		if (o->material_ref == NULL) {
			return scene_create_mesh_object(o, format, parent, NULL);
		}
		else {
			char            *ref = o->material_ref;
			material_data_t *mat = data_get_material(scene_name, ref);
			return scene_create_mesh_object(o, format, parent, mat);
		}
	}
	else if (string_equals(o->type, "object")) {
		object_t *object = scene_add_object(parent);
		return scene_return_object(object, o);
	}
	return NULL;
}

object_t *scene_create_mesh_object(obj_t *o, scene_t *format, object_t *parent, material_data_t *material) {
	// Mesh reference
	any_array_t *ref         = string_split(o->data_ref, "/");
	char        *object_file = "";
	char        *data_ref    = "";
	char        *scene_name  = format->name;
	if (ref->length == 2) { // File reference
		object_file = (char *)ref->buffer[0];
		data_ref    = (char *)ref->buffer[1];
	}
	else { // Local mesh data
		object_file = scene_name;
		data_ref    = o->data_ref;
	}
	return scene_return_mesh_object(object_file, data_ref, material, parent, o);
}

object_t *scene_return_mesh_object(char *object_file, char *data_ref, material_data_t *material, object_t *parent, obj_t *o) {
	mesh_data_t   *mesh   = data_get_mesh(object_file, data_ref);
	mesh_object_t *object = scene_add_mesh_object(mesh, material, parent);
	return scene_return_object(object->base, o);
}

object_t *scene_return_object(object_t *object, obj_t *o) {
	if (object != NULL) {
		object->raw     = o;
		object->name    = o->name;
		object->visible = o->visible;
		scene_gen_transform(o, object->transform);
	}
	return object;
}

void scene_gen_transform(obj_t *object, transform_t *transform) {
	transform->world       = object->transform != NULL ? mat4_from_f32_array(object->transform, 0) : mat4_identity();
	mat4_decomposed_t *dec = mat4_decompose(transform->world);
	transform->loc         = dec->loc;
	transform->rot         = dec->rot;
	transform->scale       = dec->scl;
	if (transform->object->parent != NULL) {
		transform_update(transform);
	}
}

void scene_load_embedded_data(string_array_t *datas) {
	if (datas == NULL) {
		return;
	}
	for (i32 i = 0; i < datas->length; ++i) {
		char *file = datas->buffer[i];
		scene_embed_data(file);
	}
}

void scene_embed_data(char *file) {
	gpu_texture_t *image = data_get_texture(file);
	any_map_set(scene_embedded, file, image);
}

// ██████╗ ███████╗███╗   ██╗██████╗ ███████╗██████╗     ██████╗  █████╗ ████████╗██╗  ██╗
// ██╔══██╗██╔════╝████╗  ██║██╔══██╗██╔════╝██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║
// ██████╔╝█████╗  ██╔██╗ ██║██║  ██║█████╗  ██████╔╝    ██████╔╝███████║   ██║   ███████║
// ██╔══██╗██╔══╝  ██║╚██╗██║██║  ██║██╔══╝  ██╔══██╗    ██╔═══╝ ██╔══██║   ██║   ██╔══██║
// ██║  ██║███████╗██║ ╚████║██████╔╝███████╗██║  ██║    ██║     ██║  ██║   ██║   ██║  ██║
// ╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝╚═════╝ ╚══════╝╚═╝  ╚═╝    ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

#include "const_data.h"

void           _gpu_begin(gpu_texture_t *render_target, any_array_t *additional, gpu_texture_t *depth_buffer, gpu_clear_t flags, unsigned color, float depth);
gpu_texture_t *gpu_create_render_target(i32 width, i32 height, i32 format);
int            iron_window_width(void);
int            iron_window_height(void);

void (*render_path_commands)(void)                   = NULL;
any_map_t       *render_path_render_targets          = NULL;
i32              render_path_current_w               = 0;
i32              render_path_current_h               = 0;
f32              _render_path_frame_time             = 0.0;
i32              _render_path_frame                  = 0;
render_target_t *_render_path_current_target         = NULL;
gpu_texture_t   *_render_path_current_image          = NULL;
bool             _render_path_paused                 = false;
i32              _render_path_last_w                 = 0;
i32              _render_path_last_h                 = 0;
string_array_t  *_render_path_bind_params            = NULL;
f32              _render_path_last_frame_time        = 0.0;
i32              _render_path_loading                = 0;
any_map_t       *_render_path_cached_shader_contexts = NULL;

bool render_path_ready(void) {
	return _render_path_loading == 0;
}

void render_path_render_frame(void) {
	if (!render_path_ready() || _render_path_paused) {
		return;
	}
	if (_render_path_last_w > 0 && (_render_path_last_w != sys_w() || _render_path_last_h != sys_h())) {
		render_path_resize();
	}
	_render_path_last_w          = sys_w();
	_render_path_last_h          = sys_h();
	_render_path_frame_time      = sys_time() - _render_path_last_frame_time;
	_render_path_last_frame_time = sys_time();
	render_path_current_w        = sys_w();
	render_path_current_h        = sys_h();
	render_path_commands();
	_render_path_frame++;
}

void render_path_set_target(char *target, string_array_t *additional, char *depth_buffer, gpu_clear_t flags, i32 color, f32 depth) {
	if (_render_path_current_image != NULL) {
		render_path_end();
	}

	gc_unroot(_mesh_object_last_pipeline);
	_mesh_object_last_pipeline = NULL;

	if (string_equals(target, "")) { // Framebuffer
		gc_unroot(_render_path_current_target);
		_render_path_current_target = NULL;
		render_path_current_w       = sys_w();
		render_path_current_h       = sys_h();
		_gpu_begin(NULL, NULL, NULL, flags, color, depth);
		gpu_viewport(sys_x(), render_path_current_h - (sys_h() - sys_y()), sys_w(), sys_h());
	}
	else { // Render target
		render_target_t *rt = (render_target_t *)any_map_get(render_path_render_targets, target);
		gc_unroot(_render_path_current_target);
		_render_path_current_target = rt;
		gc_root(_render_path_current_target);
		any_array_t *additional_images = NULL;
		if (additional != NULL) {
			additional_images = any_array_create(0);
			for (i32 i = 0; i < additional->length; ++i) {
				char            *s = (char *)additional->buffer[i];
				render_target_t *t = (render_target_t *)any_map_get(render_path_render_targets, s);
				any_array_push(additional_images, t->_image);
			}
		}
		render_path_current_w = rt->_image->width;
		render_path_current_h = rt->_image->height;
		render_target_t *db   = depth_buffer != NULL ? (render_target_t *)any_map_get(render_path_render_targets, depth_buffer) : NULL;
		gc_unroot(_render_path_current_image);
		_render_path_current_image = rt->_image;
		gc_root(_render_path_current_image);
		_gpu_begin(rt->_image, additional_images, db != NULL ? db->_image : NULL, flags, color, depth);
	}
	gc_unroot(_render_path_bind_params);
	_render_path_bind_params = NULL;
}

void render_path_end(void) {
	gpu_end();
	gc_unroot(_render_path_current_image);
	_render_path_current_image = NULL;
	gc_unroot(_render_path_bind_params);
	_render_path_bind_params = NULL;
}

void render_path_draw_meshes(char *context) {
	gc_unroot(_mesh_object_last_pipeline);
	_mesh_object_last_pipeline = NULL;
	any_array_t *meshes        = scene_meshes;
	for (i32 i = 0; i < meshes->length; ++i) {
		mesh_object_t *mesh = (mesh_object_t *)meshes->buffer[i];
		mesh_object_render(mesh, context, _render_path_bind_params);
	}
	render_path_end();
}

void render_path_draw_skydome(char *handle) {
	if (const_data_skydome_vb == NULL) {
		const_data_create_skydome_data();
	}
	cached_shader_context_t *cc = (cached_shader_context_t *)any_map_get(_render_path_cached_shader_contexts, handle);
	gpu_set_pipeline(cc->context->_->pipe);
	uniforms_set_context_consts(cc->context, _render_path_bind_params);
	uniforms_set_obj_consts(cc->context, NULL);
	gpu_set_vertex_buffer(const_data_skydome_vb);
	gpu_set_index_buffer(const_data_skydome_ib);
	gpu_draw();
	render_path_end();
}

void render_path_bind_target(char *target, char *uniform) {
	if (_render_path_bind_params != NULL) {
		string_array_push(_render_path_bind_params, target);
		string_array_push(_render_path_bind_params, uniform);
	}
	else {
		_render_path_bind_params = string_array_create(2);
		gc_root(_render_path_bind_params);
		_render_path_bind_params->buffer[0] = target;
		_render_path_bind_params->buffer[1] = uniform;
		_render_path_bind_params->length    = 2;
	}
}

void render_path_draw_shader(char *handle) {
	// Full-screen triangle
	// file/data_name/context
	cached_shader_context_t *cc = (cached_shader_context_t *)any_map_get(_render_path_cached_shader_contexts, handle);
	if (const_data_screen_aligned_vb == NULL) {
		const_data_create_screen_aligned_data();
	}
	gpu_set_pipeline(cc->context->_->pipe);
	uniforms_set_context_consts(cc->context, _render_path_bind_params);
	uniforms_set_obj_consts(cc->context, NULL);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	render_path_end();
}

void render_path_load_shader(char *handle) {
	_render_path_loading++;
	if (_render_path_cached_shader_contexts == NULL) {
		_render_path_cached_shader_contexts = any_map_create();
		gc_root(_render_path_cached_shader_contexts);
	}
	cached_shader_context_t *cc = (cached_shader_context_t *)any_map_get(_render_path_cached_shader_contexts, handle);
	if (cc != NULL) {
		_render_path_loading--;
		return;
	}

	cc = gc_alloc(sizeof(cached_shader_context_t));
	any_map_set(_render_path_cached_shader_contexts, handle, cc);

	// file/data_name/context
	any_array_t *shader_path = string_split(handle, "/");

	shader_data_t *res = data_get_shader((char *)shader_path->buffer[0], (char *)shader_path->buffer[1]);
	cc->context        = shader_data_get_context(res, (char *)shader_path->buffer[2]);
	_render_path_loading--;
}

void render_path_resize(void) {
	if (iron_window_width() == 0 || iron_window_height() == 0) {
		return;
	}
	any_array_t *render_targets_keys = map_keys(render_path_render_targets);
	for (i32 i = 0; i < render_targets_keys->length; ++i) {
		char            *key = (char *)render_targets_keys->buffer[i];
		render_target_t *rt  = (render_target_t *)any_map_get(render_path_render_targets, key);
		if (rt != NULL && rt->width == 0) {
			gpu_delete_texture(rt->_image);
			rt->_image = render_path_create_image(rt);
		}
	}
}

render_target_t *render_path_create_render_target(render_target_t *t) {
	if (render_path_render_targets == NULL) {
		render_path_render_targets = any_map_create();
		gc_root(render_path_render_targets);
	}
	t->_image = render_path_create_image(t);
	any_map_set(render_path_render_targets, t->name, t);
	return t;
}

gpu_texture_t *render_path_create_image(render_target_t *t) {
	i32 width  = t->width == 0 ? sys_w() : t->width;
	i32 height = t->height == 0 ? sys_h() : t->height;
	width      = (i32)math_floor(width * t->scale);
	height     = (i32)math_floor(height * t->scale);
	if (width < 1) {
		width = 1;
	}
	if (height < 1) {
		height = 1;
	}
	return gpu_create_render_target(width, height, t->format != NULL ? render_path_get_tex_format(t->format) : GPU_TEXTURE_FORMAT_RGBA32);
}

gpu_texture_format_t render_path_get_tex_format(char *s) {
	if (string_equals(s, "RGBA32")) {
		return GPU_TEXTURE_FORMAT_RGBA32;
	}
	if (string_equals(s, "RGBA64")) {
		return GPU_TEXTURE_FORMAT_RGBA64;
	}
	if (string_equals(s, "RGBA128")) {
		return GPU_TEXTURE_FORMAT_RGBA128;
	}
	if (string_equals(s, "R32")) {
		return GPU_TEXTURE_FORMAT_R32;
	}
	if (string_equals(s, "R16")) {
		return GPU_TEXTURE_FORMAT_R16;
	}
	if (string_equals(s, "R8")) {
		return GPU_TEXTURE_FORMAT_R8;
	}
	if (string_equals(s, "D32")) {
		return GPU_TEXTURE_FORMAT_D32;
	}
	return GPU_TEXTURE_FORMAT_RGBA32;
}

render_target_t *render_target_create(void) {
	render_target_t *raw = gc_alloc(sizeof(render_target_t));
	raw->scale           = 1.0;
	return raw;
}
