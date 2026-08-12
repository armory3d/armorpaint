#pragma once

#include "iron_armpack.h"
#include "iron_array.h"
#include "iron_audio.h"
#include "iron_draw.h"
#include "iron_gc.h"
#include "iron_gpu.h"
#include "iron_map.h"
#include "iron_math.h"
#include "iron_path.h"
#include "iron_string.h"
#include "iron_sys.h"
#include "iron_system.h"
#include "iron_thread.h"
#include <math.h>
#include <string.h>
#ifdef WITH_PHYSICS
#include "iron_physics.h"
#endif

typedef struct object object_t;

typedef struct obj_t_array {
	struct obj **buffer;
	int          length;
	int          capacity;
} obj_t_array_t;

typedef struct vertex_array_t_array {
	struct vertex_array **buffer;
	int                   length;
	int                   capacity;
} vertex_array_t_array_t;

typedef struct shader_data_t_array {
	struct shader_data **buffer;
	int                  length;
	int                  capacity;
} shader_data_t_array_t;

typedef struct tex_unit_t_array {
	struct tex_unit **buffer;
	int               length;
	int               capacity;
} tex_unit_t_array_t;

typedef struct shader_const_t_array {
	struct shader_const **buffer;
	int                   length;
	int                   capacity;
} shader_const_t_array_t;

typedef struct vertex_element_t_array {
	struct vertex_element **buffer;
	int                     length;
	int                     capacity;
} vertex_element_t_array_t;

typedef struct bind_const_t_array {
	struct bind_const **buffer;
	int                 length;
	int                 capacity;
} bind_const_t_array_t;

typedef struct bind_tex_t_array {
	struct bind_tex **buffer;
	int               length;
	int               capacity;
} bind_tex_t_array_t;

typedef struct material_context_t_array {
	struct material_context **buffer;
	int                       length;
	int                       capacity;
} material_context_t_array_t;

typedef struct obj_runtime {
	void *_gc; // Link to armpack_decode result
} obj_runtime_t;

typedef struct obj {
	char          *name;
	char          *type; // object, mesh_object, camera_object
	char          *data_ref;
	f32_array_t   *transform;
	f32_array_t   *dimensions;
	bool           visible;
	bool           spawn; // Auto add object when creating scene
	void          *anim;  // TODO: deprecated
	char          *material_ref;
	obj_t_array_t *children;
	obj_runtime_t *_;
} obj_t;

typedef struct vertex_array {
	char        *attrib;
	char        *data; // short4norm, short2norm
	i16_array_t *values;
} vertex_array_t;

typedef struct mesh_data_runtime {
	char                  *handle;
	gpu_buffer_t          *vertex_buffer;
	gpu_buffer_t          *index_buffer;
	gpu_vertex_structure_t structure;
	buffer_t              *skin_blob; // Source file bytes
	i32                    skin_frames;
} mesh_data_runtime_t;

typedef struct mesh_data {
	char                   *name;
	float                   scale_pos;
	float                   scale_tex;
	vertex_array_t_array_t *vertex_arrays;
	u32_array_t            *index_array;
	mesh_data_runtime_t    *_;
} mesh_data_t;

typedef struct camera_data {
	char        *name;
	float        near_plane;
	float        far_plane;
	float        fov;
	float        aspect;
	bool         frustum_culling;
	f32_array_t *ortho; // Indicates ortho camera, left, right, bottom, top
} camera_data_t;

typedef struct frustum_plane {
	vec4_t normal;
	f32    constant;
} frustum_plane_t;

typedef struct frustum_plane_array {
	frustum_plane_t **buffer;
	int               length;
	int               capacity;
} frustum_plane_array_t;

typedef struct camera_object {
	object_t              *base;
	camera_data_t         *data;
	mat4_t                 p;
	mat4_t                 no_jitter_p;
	i32                    frame;
	mat4_t                 v;
	mat4_t                 vp;
	frustum_plane_array_t *frustum_planes;
} camera_object_t;

typedef struct world_data_runtime {
	gpu_texture_t *envmap;
	gpu_texture_t *radiance;
	any_array_t   *radiance_mipmaps; // gpu_texture_t[]
	f32_array_t   *irradiance;
} world_data_runtime_t;

typedef struct world_data {
	char                 *name;
	i32                   color;
	float                 strength;
	char                 *irradiance; // Reference to irradiance_t blob
	char                 *radiance;
	i32                   radiance_mipmaps;
	char                 *envmap;
	world_data_runtime_t *_;
} world_data_t;

typedef struct irradiance {
	f32_array_t *irradiance; // Spherical harmonics, bands 0,1,2
} irradiance_t;

typedef struct scene {
	char           *name;
	any_array_t    *objects;      // obj_t[]
	any_array_t    *mesh_datas;   // mesh_data_t[]
	any_array_t    *camera_datas; // camera_data_t[]
	char           *camera_ref;
	any_array_t    *material_datas; // material_data_t[]
	any_array_t    *shader_datas;   // shader_data_t[]
	any_array_t    *world_datas;    // world_data_t[]
	char           *world_ref;
	any_array_t    *speaker_datas;  // TODO: deprecated
	string_array_t *embedded_datas; // Preload for this scene, images only for now
} scene_t;

typedef struct shader_context_runtime {
	gpu_pipeline_t        *pipe;
	i32_array_t           *constants;
	i32_array_t           *tex_units;
	gpu_vertex_structure_t structure;
	i32                    vertex_shader_size;
	i32                    fragment_shader_size;
} shader_context_runtime_t;

typedef struct vertex_element {
	char *name;
	char *data; // "short4norm", "short2norm"
} vertex_element_t;

typedef struct shader_const {
	char *name;
	char *type;
	char *link;
} shader_const_t;

typedef struct tex_unit {
	char *name;
	char *link;
} tex_unit_t;

typedef struct shader_context {
	char                     *name;
	bool                      depth_write;
	char                     *compare_mode;
	char                     *cull_mode;
	char                     *vertex_shader;
	char                     *fragment_shader;
	bool                      shader_from_source;
	char                     *blend_source;
	char                     *blend_destination;
	char                     *alpha_blend_source;
	char                     *alpha_blend_destination;
	u8_array_t               *color_writes_red; // Per target masks
	u8_array_t               *color_writes_green;
	u8_array_t               *color_writes_blue;
	u8_array_t               *color_writes_alpha;
	string_array_t           *color_attachments; // RGBA32, RGBA64, R8
	char                     *depth_attachment;  // D32
	vertex_element_t_array_t *vertex_elements;   // vertex_element_t[]
	shader_const_t_array_t   *constants;         // shader_const_t[]
	tex_unit_t_array_t       *texture_units;     // tex_unit_t[]
	shader_context_runtime_t *_;
} shader_context_t;

typedef struct shader_data {
	char        *name;
	any_array_t *contexts; // shader_context_t[]
} shader_data_t;

typedef struct bind_const {
	char        *name;
	f32_array_t *vec; // bool (vec[0] > 0) | i32 | f32 | vec2 | vec3 | vec4
} bind_const_t;

typedef struct bind_tex {
	char *name;
	char *file;
} bind_tex_t;

typedef struct material_context_runtime {
	any_array_t *textures; // gpu_texture_t[]
} material_context_runtime_t;

typedef struct material_context {
	char                       *name;
	bind_const_t_array_t       *bind_constants; // bind_const_t[]
	bind_tex_t_array_t         *bind_textures;  // bind_tex_t[]
	material_context_runtime_t *_;
} material_context_t;

typedef struct material_data_runtime {
	float          uid;
	shader_data_t *shader;
} material_data_runtime_t;

typedef struct material_data {
	char                       *name;
	char                       *shader;
	material_context_t_array_t *contexts; // material_context_t[]
	material_data_runtime_t    *_;
} material_data_t;

typedef struct render_target {
	char *name;
	i32   width;
	i32   height;
	// Optional
	char *format;
	f32   scale;
	// Runtime
	gpu_texture_t *_image;
} render_target_t;

typedef struct cached_shader_context {
	shader_context_t *context;
} cached_shader_context_t;

typedef struct mesh_object {
	object_t        *base;
	mesh_data_t     *data;
	material_data_t *material;
	f32              camera_dist;
	bool             frustum_culling;
	char            *skip_context;  // Do not draw this context
	char            *force_context; // Draw only this context
} mesh_object_t;

typedef struct transform {
	mat4_t    world;
	mat4_t    local;
	vec4_t    loc;
	quat_t    rot;
	vec4_t    scale;
	float     scale_world;
	mat4_t    world_unpack;
	bool      dirty;
	object_t *object;
	vec4_t    dim;
	float     radius;
} transform_t;

typedef struct {
	int empty;
#ifdef WITH_PHYSICS
	asim_body_t *body;
#endif
} object_runtime_t;

typedef struct object {
	i32               uid;
	float             urandom;
	obj_t            *raw;
	char             *name;
	transform_t      *transform;
	object_t         *parent;
	any_array_t      *children;
	bool              visible;
	bool              culled;
	bool              is_empty;
	void             *ext;
	char             *ext_type;
	object_runtime_t *_;
} object_t;

//  ██████╗ ██████╗      ██╗███████╗ ██████╗████████╗
// ██╔═══██╗██╔══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██║   ██║██████╔╝     ██║█████╗  ██║        ██║
// ██║   ██║██╔══██╗██   ██║██╔══╝  ██║        ██║
// ╚██████╔╝██████╔╝╚█████╔╝███████╗╚██████╗   ██║
//  ╚═════╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

extern i32 _object_uid_counter;

object_t *object_create(bool is_empty);
void      object_set_parent(object_t *raw, object_t *parent_object);
void      object_remove_super(object_t *raw);
void      object_remove(object_t *raw);
object_t *object_get_child(object_t *raw, char *name);

// ████████╗██████╗  █████╗ ███╗   ██╗███████╗███████╗ ██████╗ ██████╗ ███╗   ███╗
// ╚══██╔══╝██╔══██╗██╔══██╗████╗  ██║██╔════╝██╔════╝██╔═══██╗██╔══██╗████╗ ████║
//    ██║   ██████╔╝███████║██╔██╗ ██║███████╗█████╗  ██║   ██║██████╔╝██╔████╔██║
//    ██║   ██╔══██╗██╔══██║██║╚██╗██║╚════██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║
//    ██║   ██║  ██║██║  ██║██║ ╚████║███████║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║
//    ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝

transform_t *transform_create(object_t *object);
void         transform_reset(transform_t *raw);
void         transform_update(transform_t *raw);
void         transform_build_matrix(transform_t *raw);
void         transform_set_matrix(transform_t *raw, mat4_t mat);
void         transform_decompose(transform_t *raw);
void         transform_rotate(transform_t *raw, vec4_t axis, float f);
void         transform_move(transform_t *raw, vec4_t axis, float f);
void         transform_compute_radius(transform_t *raw);
void         transform_compute_dim(transform_t *raw);
vec4_t       transform_look(transform_t *raw);
vec4_t       transform_right(transform_t *raw);
vec4_t       transform_up(transform_t *raw);
float        transform_world_x(transform_t *raw);
float        transform_world_y(transform_t *raw);
float        transform_world_z(transform_t *raw);

//  ██████╗ █████╗ ███╗   ███╗███████╗██████╗  █████╗     ██████╗  █████╗ ████████╗ █████╗
// ██╔════╝██╔══██╗████╗ ████║██╔════╝██╔══██╗██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██║     ███████║██╔████╔██║█████╗  ██████╔╝███████║    ██║  ██║███████║   ██║   ███████║
// ██║     ██╔══██║██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║    ██║  ██║██╔══██║   ██║   ██╔══██║
// ╚██████╗██║  ██║██║ ╚═╝ ██║███████╗██║  ██║██║  ██║    ██████╔╝██║  ██║   ██║   ██║  ██║
//  ╚═════╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

camera_data_t *camera_data_parse(char *name, char *id);
camera_data_t *camera_data_get_raw_by_name(any_array_t *datas, char *name);

//  ██████╗ █████╗ ███╗   ███╗███████╗██████╗  █████╗      ██████╗ ██████╗      ██╗███████╗ ██████╗████████╗
// ██╔════╝██╔══██╗████╗ ████║██╔════╝██╔══██╗██╔══██╗    ██╔═══██╗██╔══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██║     ███████║██╔████╔██║█████╗  ██████╔╝███████║    ██║   ██║██████╔╝     ██║█████╗  ██║        ██║
// ██║     ██╔══██║██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║    ██║   ██║██╔══██╗██   ██║██╔══╝  ██║        ██║
// ╚██████╗██║  ██║██║ ╚═╝ ██║███████╗██║  ██║██║  ██║    ╚██████╔╝██████╔╝╚█████╔╝███████╗╚██████╗   ██║
//  ╚═════╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝     ╚═════╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

extern vec4_t    _camera_object_sphere_center;
camera_object_t *camera_object_create(camera_data_t *data);
void             camera_object_build_proj(camera_object_t *raw, f32 screen_aspect);
void             camera_object_remove(camera_object_t *raw);
void             camera_object_proj_jitter(camera_object_t *raw);
void             camera_object_build_mat(camera_object_t *raw);
vec4_t           camera_object_right(camera_object_t *raw);
vec4_t           camera_object_up(camera_object_t *raw);
vec4_t           camera_object_look(camera_object_t *raw);
vec4_t           camera_object_right_world(camera_object_t *raw);
vec4_t           camera_object_up_world(camera_object_t *raw);
vec4_t           camera_object_look_world(camera_object_t *raw);
void             camera_object_build_view_frustum(mat4_t vp, frustum_plane_array_t *frustum_planes);
bool camera_object_sphere_in_frustum(frustum_plane_array_t *frustum_planes, transform_t *t, f32 radius_scale, f32 offset_x, f32 offset_y, f32 offset_z);

// ███████╗██████╗ ██╗   ██╗███████╗████████╗██╗   ██╗███╗   ███╗    ██████╗ ██╗      █████╗ ███╗   ██╗███████╗
// ██╔════╝██╔══██╗██║   ██║██╔════╝╚══██╔══╝██║   ██║████╗ ████║    ██╔══██╗██║     ██╔══██╗████╗  ██║██╔════╝
// █████╗  ██████╔╝██║   ██║███████╗   ██║   ██║   ██║██╔████╔██║    ██████╔╝██║     ███████║██╔██╗ ██║█████╗
// ██╔══╝  ██╔══██╗██║   ██║╚════██║   ██║   ██║   ██║██║╚██╔╝██║    ██╔═══╝ ██║     ██╔══██║██║╚██╗██║██╔══╝
// ██║     ██║  ██║╚██████╔╝███████║   ██║   ╚██████╔╝██║ ╚═╝ ██║    ██║     ███████╗██║  ██║██║ ╚████║███████╗
// ╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚══════╝   ╚═╝    ╚═════╝ ╚═╝     ╚═╝    ╚═╝     ╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝

frustum_plane_t *frustum_plane_create();
void             frustum_plane_normalize(frustum_plane_t *raw);
f32              frustum_plane_dist_to_sphere(frustum_plane_t *raw, vec4_t sphere_center, f32 sphere_radius);
void             frustum_plane_set_components(frustum_plane_t *raw, f32 x, f32 y, f32 z, f32 w);

// ██╗    ██╗ ██████╗ ██████╗ ██╗      ██████╗     ██████╗  █████╗ ████████╗ █████╗
// ██║    ██║██╔═══██╗██╔══██╗██║      ██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██║ █╗ ██║██║   ██║██████╔╝██║      ██║  ██║    ██║  ██║███████║   ██║   ███████║
// ██║███╗██║██║   ██║██╔══██╗██║      ██║  ██║    ██║  ██║██╔══██║   ██║   ██╔══██║
// ╚███╔███╔╝╚██████╔╝██║  ██║███████╗ ██████╔╝    ██████╔╝██║  ██║   ██║   ██║  ██║
//  ╚══╝╚══╝  ╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝     ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

extern f32_array_t *_world_data_empty_irr;
world_data_t       *world_data_parse(char *name, char *id);
world_data_t       *world_data_get_raw_by_name(any_array_t *datas, char *name);
f32_array_t        *world_data_get_empty_irradiance();
f32_array_t        *world_data_set_irradiance(world_data_t *raw);
void                world_data_load_envmap(world_data_t *raw);

// ███╗   ███╗ █████╗ ████████╗███████╗██████╗ ██╗ █████╗ ██╗         ██████╗  █████╗ ████████╗ █████╗
// ████╗ ████║██╔══██╗╚══██╔══╝██╔════╝██╔══██╗██║██╔══██╗██║        ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██╔████╔██║███████║   ██║   █████╗  ██████╔╝██║███████║██║        ██║  ██║███████║   ██║   ███████║
// ██║╚██╔╝██║██╔══██║   ██║   ██╔══╝  ██╔══██╗██║██╔══██║██║        ██║  ██║██╔══██║   ██║   ██╔══██║
// ██║ ╚═╝ ██║██║  ██║   ██║   ███████╗██║  ██║██║██║  ██║███████╗   ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝╚═╝  ╚═╝╚══════╝   ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

extern i32          _material_data_uid_counter;
material_data_t    *material_data_create(material_data_t *raw, char *file);
material_data_t    *material_data_parse(char *file, char *name);
material_data_t    *material_data_get_raw_by_name(any_array_t *datas, char *name);
material_context_t *material_data_get_context(material_data_t *raw, char *name);
void                material_context_load(material_context_t *raw);

// ███████╗██╗  ██╗ █████╗ ██████╗ ███████╗██████╗     ██████╗  █████╗ ████████╗ █████╗
// ██╔════╝██║  ██║██╔══██╗██╔══██╗██╔════╝██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ███████╗███████║███████║██║  ██║█████╗  ██████╔╝    ██║  ██║███████║   ██║   ███████║
// ╚════██║██╔══██║██╔══██║██║  ██║██╔══╝  ██╔══██╗    ██║  ██║██╔══██║   ██║   ██╔══██║
// ███████║██║  ██║██║  ██║██████╔╝███████╗██║  ██║    ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝ ╚══════╝╚═╝  ╚═╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

shader_data_t       *shader_data_create(shader_data_t *raw);
char                *shader_data_ext();
shader_data_t       *shader_data_parse(char *file, char *name);
shader_data_t       *shader_data_get_raw_by_name(shader_data_t_array_t *datas, char *name);
void                 shader_data_delete(shader_data_t *raw);
shader_context_t    *shader_data_get_context(shader_data_t *raw, char *name);
void                 shader_context_load(shader_context_t *raw);
void                 shader_context_compile(shader_context_t *raw);
i32                  shader_context_type_size(char *t);
i32                  shader_context_type_pad(i32 offset, i32 size);
void                 shader_context_finish_compile(shader_context_t *raw);
gpu_vertex_data_t    shader_context_parse_data(char *data);
void                 shader_context_parse_vertex_struct(shader_context_t *raw);
void                 shader_context_delete(shader_context_t *raw);
gpu_compare_mode_t   shader_context_get_compare_mode(char *s);
gpu_cull_mode_t      shader_context_get_cull_mode(char *s);
gpu_blend_t          shader_context_get_blend_fac(char *s);
gpu_texture_format_t shader_context_get_tex_format(char *s);
void                 shader_context_add_const(shader_context_t *raw, i32 offset);
void                 shader_context_add_tex(shader_context_t *raw, i32 i);

// ███╗   ███╗███████╗███████╗██╗  ██╗    ██████╗  █████╗ ████████╗ █████╗
// ████╗ ████║██╔════╝██╔════╝██║  ██║    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██╔████╔██║█████╗  ███████╗███████║    ██║  ██║███████║   ██║   ███████║
// ██║╚██╔╝██║██╔══╝  ╚════██║██╔══██║    ██║  ██║██╔══██║   ██║   ██╔══██║
// ██║ ╚═╝ ██║███████╗███████║██║  ██║    ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚═╝     ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

mesh_data_t           *mesh_data_parse(char *name, char *id);
mesh_data_t           *mesh_data_get_raw_by_name(any_array_t *datas, char *name);
mesh_data_t           *mesh_data_create(mesh_data_t *raw);
gpu_vertex_structure_t mesh_data_get_vertex_struct(any_array_t *vertex_arrays);
gpu_vertex_data_t      mesh_data_get_vertex_data(char *data);
i32                    mesh_data_get_vertex_size(char *vertex_data);
void                   mesh_data_build_vertices(gpu_buffer_t *vertex_buffer, any_array_t *vertex_arrays);
void                   mesh_data_build_indices(gpu_buffer_t *index_buffer, u32_array_t *index_array);
vertex_array_t        *mesh_data_get_vertex_array(mesh_data_t *raw, char *name);
void                   mesh_data_build(mesh_data_t *raw);
vec4_t                 mesh_data_calculate_aabb(mesh_data_t *raw);
void                   mesh_data_delete(mesh_data_t *raw);

// ███╗   ███╗███████╗███████╗██╗  ██╗     ██████╗ ██████╗      ██╗███████╗ ██████╗████████╗
// ████╗ ████║██╔════╝██╔════╝██║  ██║    ██╔═══██╗██╔══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██╔████╔██║█████╗  ███████╗███████║    ██║   ██║██████╔╝     ██║█████╗  ██║        ██║
// ██║╚██╔╝██║██╔══╝  ╚════██║██╔══██║    ██║   ██║██╔══██╗██   ██║██╔══╝  ██║        ██║
// ██║ ╚═╝ ██║███████╗███████║██║  ██║    ╚██████╔╝██████╔╝╚█████╔╝███████╗╚██████╗   ██║
// ╚═╝     ╚═╝╚══════╝╚══════╝╚═╝  ╚═╝     ╚═════╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

extern gpu_pipeline_t *_mesh_object_last_pipeline;
mesh_object_t         *mesh_object_create(mesh_data_t *data, material_data_t *material);
void                   mesh_object_set_data(mesh_object_t *raw, mesh_data_t *data);
void                   mesh_object_remove(mesh_object_t *raw);
bool                   mesh_object_cull_material(mesh_object_t *raw, char *context);
bool                   mesh_object_cull_mesh(mesh_object_t *raw, char *context, camera_object_t *camera);
void                   mesh_object_render(mesh_object_t *raw, char *context, string_array_t *bind_params);
bool                   mesh_object_valid_context(mesh_object_t *raw, material_data_t *mat, char *context);

extern mat4_t _uniforms_mat;
extern mat4_t _uniforms_mat2;
extern mat3_t _uniforms_mat3;
extern vec4_t _uniforms_vec;
extern vec4_t _uniforms_vec2;
extern quat_t _uniforms_quat;
extern f32    uniforms_pos_unpack;
extern f32    uniforms_tex_unpack;
extern gpu_texture_t *(*uniforms_tex_links)(object_t *o, material_data_t *md, char *s);
extern mat4_t (*uniforms_mat4_links)(object_t *o, material_data_t *md, char *s);
extern vec4_t (*uniforms_vec4_links)(object_t *o, material_data_t *md, char *s);
extern vec4_t (*uniforms_vec3_links)(object_t *o, material_data_t *md, char *s);
extern vec2_t (*uniforms_vec2_links)(object_t *o, material_data_t *md, char *s);
extern f32 (*uniforms_f32_links)(object_t *o, material_data_t *md, char *s);
extern f32_array_t *(*uniforms_f32_array_links)(object_t *o, material_data_t *md, char *s);
extern i32 (*uniforms_i32_links)(object_t *o, material_data_t *md, char *s);

// ██╗   ██╗███╗   ██╗██╗███████╗ ██████╗ ██████╗ ███╗   ███╗███████╗
// ██║   ██║████╗  ██║██║██╔════╝██╔═══██╗██╔══██╗████╗ ████║██╔════╝
// ██║   ██║██╔██╗ ██║██║█████╗  ██║   ██║██████╔╝██╔████╔██║███████╗
// ██║   ██║██║╚██╗██║██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║╚════██║
// ╚██████╔╝██║ ╚████║██║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║███████║
//  ╚═════╝ ╚═╝  ╚═══╝╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝

void             uniforms_set_context_consts(shader_context_t *context, string_array_t *bind_params);
void             uniforms_set_obj_consts(shader_context_t *context, object_t *object);
void             uniforms_bind_render_target(render_target_t *rt, shader_context_t *context, char *sampler_id);
bool             uniforms_set_context_const(i32 location, shader_const_t *c);
void             uniforms_set_obj_const(object_t *obj, i32 loc, shader_const_t *c);
void             uniforms_set_material_consts(shader_context_t *context, material_context_t *material_context);
material_data_t *current_material(object_t *object);
void             uniforms_set_material_const(i32 location, shader_const_t *shader_const, bind_const_t *material_const);

// ██████╗  █████╗ ████████╗ █████╗
// ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
// ██║  ██║███████║   ██║   ███████║
// ██║  ██║██╔══██║   ██║   ██╔══██║
// ██████╔╝██║  ██║   ██║   ██║  ██║
// ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

typedef void sound_t;

extern any_map_t *data_cached_scene_raws;
extern any_map_t *data_cached_meshes;
extern any_map_t *data_cached_cameras;
extern any_map_t *data_cached_materials;
extern any_map_t *data_cached_worlds;
extern any_map_t *data_cached_shaders;
extern any_map_t *data_cached_blobs;
extern any_map_t *data_cached_textures;
extern any_map_t *data_cached_videos;
extern any_map_t *data_cached_fonts;
extern any_map_t *data_cached_sounds;
extern i32        data_assets_loaded;

mesh_data_t     *data_get_mesh(char *file, char *name);
camera_data_t   *data_get_camera(char *file, char *name);
material_data_t *data_get_material(char *file, char *name);
world_data_t    *data_get_world(char *file, char *name);
shader_data_t   *data_get_shader(char *file, char *name);
scene_t         *data_get_scene_raw(char *file);
gpu_texture_t   *data_get_texture(char *file);
buffer_t        *data_get_blob(char *file);
video_t         *data_get_video(char *file);
draw_font_t     *data_get_font(char *file);
sound_t         *data_get_sound(char *file);
void             data_delete_mesh(char *handle);
void             data_delete_blob(char *handle);
void             data_delete_texture(char *handle);
void             data_delete_video(char *handle);
void             data_delete_font(char *handle);
void             data_delete_sound(char *handle);
bool             data_is_abs(char *file);
bool             data_is_up(char *file);
char            *data_resolve_path(char *file);
char            *data_path(void);

// ███████╗ ██████╗███████╗███╗   ██╗███████╗
// ██╔════╝██╔════╝██╔════╝████╗  ██║██╔════╝
// ███████╗██║     █████╗  ██╔██╗ ██║█████╗
// ╚════██║██║     ██╔══╝  ██║╚██╗██║██╔══╝
// ███████║╚██████╗███████╗██║ ╚████║███████╗
// ╚══════╝ ╚═════╝╚══════╝╚═╝  ╚═══╝╚══════╝

extern camera_object_t *scene_camera;
extern world_data_t    *scene_world;
extern any_array_t     *scene_meshes;
extern any_array_t     *scene_cameras;
extern any_array_t     *scene_empties;
extern any_map_t       *scene_embedded;
extern i32              _scene_uid_counter;
extern i32              _scene_uid;
extern scene_t         *_scene_raw;
extern object_t        *_scene_root;
extern object_t        *_scene_scene_parent;
extern i32              _scene_objects_traversed;
extern i32              _scene_objects_count;

object_t        *scene_create(scene_t *format);
void             scene_remove(void);
object_t        *scene_set_active(char *scene_name);
void             scene_render_frame(void);
object_t        *scene_add_object(object_t *parent);
object_t        *scene_get_child(char *name);
mesh_object_t   *scene_add_mesh_object(mesh_data_t *data, material_data_t *material, object_t *parent);
camera_object_t *scene_add_camera_object(camera_data_t *data, object_t *parent);
void             scene_traverse_objects(scene_t *format, object_t *parent, any_array_t *objects);
object_t        *scene_add_scene(char *scene_name, object_t *parent);
i32              scene_get_objects_count(any_array_t *objects);
object_t        *_scene_spawn_object_tree(obj_t *obj, object_t *parent, bool spawn_children);
object_t        *scene_spawn_object(char *name, object_t *parent, bool spawn_children);
obj_t           *scene_get_raw_object_by_name(scene_t *format, char *name);
obj_t           *scene_traverse_objs(any_array_t *children, char *name);
object_t        *scene_create_object(obj_t *o, scene_t *format, object_t *parent);
object_t        *scene_create_mesh_object(obj_t *o, scene_t *format, object_t *parent, material_data_t *material);
object_t        *scene_return_mesh_object(char *object_file, char *data_ref, material_data_t *material, object_t *parent, obj_t *o);
object_t        *scene_return_object(object_t *object, obj_t *o);
void             scene_gen_transform(obj_t *object, transform_t *transform);
void             scene_load_embedded_data(string_array_t *datas);
void             scene_embed_data(char *file);

// ██████╗ ███████╗███╗   ██╗██████╗ ███████╗██████╗     ██████╗  █████╗ ████████╗██╗  ██╗
// ██╔══██╗██╔════╝████╗  ██║██╔══██╗██╔════╝██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║
// ██████╔╝█████╗  ██╔██╗ ██║██║  ██║█████╗  ██████╔╝    ██████╔╝███████║   ██║   ███████║
// ██╔══██╗██╔══╝  ██║╚██╗██║██║  ██║██╔══╝  ██╔══██╗    ██╔═══╝ ██╔══██║   ██║   ██╔══██║
// ██║  ██║███████╗██║ ╚████║██████╔╝███████╗██║  ██║    ██║     ██║  ██║   ██║   ██║  ██║
// ╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝╚═════╝ ╚══════╝╚═╝  ╚═╝    ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

extern void (*render_path_commands)(void);
extern any_map_t       *render_path_render_targets;
extern i32              render_path_current_w;
extern i32              render_path_current_h;
extern f32              _render_path_frame_time;
extern i32              _render_path_frame;
extern render_target_t *_render_path_current_target;
extern gpu_texture_t   *_render_path_current_image;
extern bool             _render_path_paused;
extern i32              _render_path_last_w;
extern i32              _render_path_last_h;
extern string_array_t  *_render_path_bind_params;
extern f32              _render_path_last_frame_time;
extern i32              _render_path_loading;
extern any_map_t       *_render_path_cached_shader_contexts;

bool                 render_path_ready(void);
void                 render_path_render_frame(void);
void                 render_path_set_target(char *target, string_array_t *additional, char *depth_buffer, gpu_clear_t flags, i32 color, f32 depth);
void                 render_path_end(void);
void                 render_path_draw_meshes(char *context);
void                 render_path_draw_skydome(char *handle);
void                 render_path_bind_target(char *target, char *uniform);
void                 render_path_draw_shader(char *handle);
void                 render_path_load_shader(char *handle);
void                 render_path_resize(void);
render_target_t     *render_path_create_render_target(render_target_t *t);
gpu_texture_t       *render_path_create_image(render_target_t *t);
gpu_texture_format_t render_path_get_tex_format(char *s);
render_target_t     *render_target_create(void);
