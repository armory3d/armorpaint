
#pragma once
#include "iron_physics.h"

#include "enums.h"
#include "minic.h"

typedef struct slot_brush {
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
	struct gpu_texture    *image;      // 200px
	struct gpu_texture    *image_icon; // 50px
	bool                   preview_ready;
	i32                    id;
} slot_brush_t;

typedef struct slot_layer {
	i32                   id;
	char                 *name;
	char                 *ext;
	bool                  visible;
	struct slot_layer    *parent;   // Group (for layers) or layer (for masks and filters)
	struct gpu_texture   *texpaint; // Base or mask
	struct gpu_texture   *texpaint_nor;
	struct gpu_texture   *texpaint_pack;
	struct gpu_texture   *texpaint_preview; // Layer preview
	f32                   mask_opacity;     // Opacity mask
	struct slot_material *fill_material;
	bool                  show_panel;
	blend_type_t          blending;
	i32                   object_mask;
	f32                   scale;
	f32                   angle;
	uv_type_t             uv_type;
	i32                   uv_map;
	bool                  paint_base;
	bool                  paint_opac;
	bool                  paint_occ;
	bool                  paint_rough;
	bool                  paint_met;
	bool                  paint_nor;
	bool                  paint_nor_blend;
	bool                  paint_height;
	bool                  paint_height_blend;
	bool                  paint_emis;
	bool                  paint_subs;
	mat4_t                decal_mat; // Decal layer
	struct gpu_texture   *texpaint_sculpt;
	f32_array_t          *path_points;        // uv-space [x, y, ..]
	f32_array_t          *path_points_world;  // world-space [x, y, z, ..]
	f32_array_t          *path_points_camera; // [loc.xyzw, rot.xyzw, aspect, ..] at time of point placement
	i32_array_t          *path_points_parent;
	i32                   path_tool;
	bool                  path_curved;
	struct slot_material *path_material;
} slot_layer_t;

typedef struct slot_font {
	struct gpu_texture *image; // 200px
	bool                preview_ready;
	i32                 id;
	struct draw_font   *font;
	char               *name;
	char               *file;
} slot_font_t;

typedef struct slot_sound {
	struct gpu_texture *image; // 200px
	i32                 id;
	sound_t            *sound;
	char               *name;
	char               *file;
} slot_sound_t;

typedef struct config {
	char *version;
	char *sha; // Commit id
	// The locale should be specified in ISO 639-1 format:
	// https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes
	// "system" is a special case that will use the system locale
	char *locale;
	// Window
	i32  window_mode; // window, fullscreen
	i32  window_w;
	i32  window_h;
	i32  window_x;
	i32  window_y;
	bool window_resizable;
	bool window_maximizable;
	bool window_minimizable;
	bool window_vsync;
	i32  window_frequency;
	f32  window_scale;
	// Render path
	f32   rp_supersample;
	bool  rp_ssao;
	f32   rp_bloom;
	f32   rp_vignette;
	f32   rp_grain;
	f32   rp_contrast;
	f32   rp_gamma;
	char *lut_path; // .cube
	bool  texture_filter;
	// Application
	struct string_array *recent_projects; // Recently opened projects
	struct string_array *bookmarks;       // Bookmarked folders in browser
	struct string_array *plugins;         // List of enabled plugins
	char                *keymap;          // Link to keymap file
	char                *theme;           // Link to theme file
	i32                  undo_steps;      // Number of undo steps to preserve
	f32                  camera_fov;
	f32                  camera_pan_speed;
	f32                  camera_zoom_speed;
	f32                  camera_rotation_speed;
	bool                 camera_upside_down;
	i32                  zoom_direction;
	bool                 wrap_mouse;
	bool                 show_asset_names;
	bool                 touch_ui;
	bool                 splash_screen;
	struct i32_array    *layout;          // Sizes
	struct i32_array    *layout_tabs;     // Active tabs
	i32                  camera_pivot;    // Cursor, camera center
	i32                  camera_controls; // Orbit, rotate, fly
	char                *server;
	i32                  viewport_mode;
	i32                  pathtrace_mode;
	bool                 pressure_radius; // Pen pressure controls
	f32                  pressure_sensitivity;
	i32                  layer_res;
	bool                 brush_live;
	bool                 node_previews;
	bool                 pressure_hardness;
	bool                 pressure_angle;
	bool                 pressure_opacity;
	bool                 material_live;
	bool                 brush_depth_reject;
	bool                 brush_angle_reject;
	f32                  brush_alpha_discard;
	i32                  dilate_radius;
	char                *blender;
	i32                  scene_atlas_res;
	bool                 grid_snap;
	bool                 experimental;
	i32                  neural_res;
	i32                  console_model;
	render_mode_t        render_mode;
	workspace_t          workspace;
	workflow_t           workflow;
	bool                 view2d_grid_show;
	i32                  view2d_grid_cell;
	bool                 view2d_grid_snap;
} config_t;

typedef struct slot_material {
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
	struct gpu_texture    *image;
	struct gpu_texture    *image_icon;
	bool                   preview_ready;
	struct material_data  *data;
	i32                    id;
	bool                   paint_base;
	bool                   paint_opac;
	i32                    paint_opac_mode;
	bool                   paint_occ;
	bool                   paint_rough;
	bool                   paint_met;
	bool                   paint_nor;
	bool                   paint_height;
	bool                   paint_emis;
	bool                   paint_subs;
} slot_material_t;

typedef struct tab_draw {
	void (*f)(struct ui_handle *);
} tab_draw_t;

typedef struct tab_draw_t_array *tab_draw_array_t;

typedef struct context {
	struct asset        *texture;
	struct mesh_object  *paint_object;
	struct mesh_object  *merged_object;
	bool                 merged_object_is_atlas;
	i32                  ddirty;  // depth
	i32                  pdirty;  // paint
	i32                  rdirty;  // render
	i32                  rtdirty; // raytrace
	bool                 brush_blend_dirty;
	bool                 split_view;
	i32                  view_index;
	i32                  view_index_last;
	struct swatch_color *swatch;
	struct swatch_color *picked_color;
	void (*color_picker_callback)(struct swatch_color *);
	struct f32_array           *default_irradiance;
	struct gpu_texture         *default_radiance;
	struct gpu_texture_t_array *default_radiance_mipmaps;
	struct gpu_texture         *saved_envmap;
	struct gpu_texture         *empty_envmap;
	struct gpu_texture         *preview_envmap;
	bool                        envmap_loaded;
	bool                        show_envmap;
	bool                        show_envmap_blur;
	bool                        show_envmap_spheres;
	bool                        capturing_screenshot;
	bool                        capture_background;
	f32                         envmap_angle;
	f32                         light_angle;
	bool                        cull_backfaces;
	texture_ldr_format_t        format_type;
	f32                         format_quality;
	export_destination_t        layers_destination;
	bool                        export_padding;
	split_type_t                split_by;
	f32                         select_time;
	viewport_mode_t             viewport_mode;
	void                       *viewport_shader;
	bool                        hscale_was_changed;
	mesh_format_t               export_mesh_format;
	i32                         export_mesh_index;
	bool                        pack_assets_on_export;
	bool                        pack_assets_on_save;
	vec4_t                      paint_vec;
	f32                         last_paint_x;
	f32                         last_paint_y;
	bool                        foreground_event;
	i32                         painted;
	f32                         brush_time;
	f32                         clone_start_x;
	f32                         clone_start_y;
	f32                         clone_delta_x;
	f32                         clone_delta_y;
	bool                        clone_set_source;
	f32                         grab_start_x;
	f32                         grab_start_y;
	bool                        show_compass;
	i32                         project_type;
	f32                         last_paint_vec_x;
	f32                         last_paint_vec_y;
	f32                         prev_paint_vec_x;
	f32                         prev_paint_vec_y;
	i32                         frame;
	bool                        paint2d_view;
	bool                        brush_locked;
	camera_type_t               camera_type;
	char                       *texture_export_path;
	i32                         last_status_position;
	camera_pivot_t              camera_pivot;
	camera_controls_t           camera_controls;
	bool                        pen_painting_only;
	struct slot_material       *material;
	struct slot_layer          *layer;
	struct slot_brush          *brush;
	struct slot_font           *font;
	struct slot_sound          *sound;
	tool_type_t                 tool;
	bool                        layer_preview_dirty;
	bool                        layers_preview_dirty;
	struct i32_imap            *node_preview_socket_map;
	struct any_imap            *node_preview_map;
	char                       *node_preview_name;
	struct any_map             *node_previews;
	struct string_array        *node_previews_used;
	bool                        selected_node_preview;
	struct gpu_texture         *mask_preview_rgba32;
	struct slot_layer          *mask_preview_last;
	bool                        colorid_picked;
	bool                        colorid_viewport_mask;
	bool                        material_preview;
	mat4_t                      saved_camera;
	tool_type_t                 color_picker_previous_tool;
	i32                         materialid_picked;
	f32                         uvx_picked;
	f32                         uvy_picked;
	bool                        picker_select_material;
	bool                        picker_paint_mask;
	bool                        picker_viewport_mask;
	bool                        pick_pos_nor_tex;
	bool                        pick_object_id;
	f32                         posx_picked;
	f32                         posy_picked;
	f32                         posz_picked;
	f32                         norx_picked;
	f32                         nory_picked;
	f32                         norz_picked;
	bool                        draw_wireframe;
	bool                        draw_texels;
	i32                         colorid;
	export_mode_t               layers_export;
	struct gpu_texture         *decal_image;
	bool                        decal_preview;
	f32                         decal_x;
	f32                         decal_y;
	bool                        brush_camera_align;
	bool                        select_active; // Select tool
	bool                        select_dragging;
	f32                         select_start_x;
	f32                         select_start_y;
	f32                         select_x1;
	f32                         select_y1;
	f32                         select_x2;
	f32                         select_y2;
	bool                        write_icon_on_export;
	struct gpu_texture         *text_tool_image;
	char                       *text_tool_text;
	i32                         layer_filter;
	struct object              *gizmo;
	struct object              *gizmo_translate_x;
	struct object              *gizmo_translate_y;
	struct object              *gizmo_translate_z;
	struct object              *gizmo_scale_x;
	struct object              *gizmo_scale_y;
	struct object              *gizmo_scale_z;
	struct object              *gizmo_rotate_x;
	struct object              *gizmo_rotate_y;
	struct object              *gizmo_rotate_z;
	bool                        gizmo_started;
	f32                         gizmo_offset;
	f32                         gizmo_drag;
	f32                         gizmo_drag_last;
	bool                        translate_x;
	bool                        translate_y;
	bool                        translate_z;
	bool                        scale_x;
	bool                        scale_y;
	bool                        scale_z;
	bool                        rotate_x;
	bool                        rotate_y;
	bool                        rotate_z;
	f32                         brush_nodes_radius;
	f32                         brush_nodes_opacity;
	bool                        brush_nodes_uses_random;
	struct gpu_texture         *brush_mask_image;
	bool                        brush_mask_image_is_alpha;
	struct gpu_texture         *brush_stencil_image;
	bool                        brush_stencil_image_is_alpha;
	f32                         brush_stencil_x;
	f32                         brush_stencil_y;
	f32                         brush_stencil_scale;
	bool                        brush_stencil_scaling;
	f32                         brush_stencil_angle;
	bool                        brush_stencil_rotating;
	f32                         brush_nodes_scale;
	f32                         brush_nodes_angle;
	f32                         brush_nodes_hardness;
	bool                        brush_directional;
	f32                         brush_radius;
	f32                         brush_scale_x;
	f32                         brush_decal_mask_radius;
	blend_type_t                brush_blending;
	f32                         brush_opacity;
	f32                         brush_scale;
	f32                         brush_angle;
	f32                         brush_hardness;
	f32                         brush_lazy_radius;
	f32                         brush_lazy_step;
	f32                         brush_lazy_x;
	f32                         brush_lazy_y;
	uv_type_t                   brush_paint;
	sculpt_type_t               brush_sculpt;
	f32                         brush_angle_reject_dot;
	bake_type_t                 bake_type;
	bake_axis_t                 bake_axis;
	bake_up_axis_t              bake_up_axis;
	i32                         bake_samples;
	f32                         bake_ao_strength;
	f32                         bake_ao_radius;
	f32                         bake_ao_offset;
	f32                         bake_curv_strength;
	f32                         bake_curv_radius;
	f32                         bake_curv_offset;
	i32                         bake_curv_smooth;
	i32                         bake_high_poly;
	bool                        xray;
	bool                        sym_x;
	bool                        sym_y;
	bool                        sym_z;
	i32                         fill_type;
	i32                         blur_type;
	bool                        paint2d;
	i32                         maximized_sidebar_width;
	i32                         drag_dest;
	vec4_t                      coords;
	f32                         start_x;
	f32                         start_y;
	bool                        lock_begin;
	bool                        lock_x;
	bool                        lock_y;
	f32                         lock_start_x;
	f32                         lock_start_y;
	bool                        registered;
	f32                         particle_hit_x;
	f32                         particle_hit_y;
	f32                         particle_hit_z;
	f32                         last_particle_hit_x;
	f32                         last_particle_hit_y;
	f32                         last_particle_hit_z;
	struct asim_body           *paint_body;
	struct {
		f32                hit_x;
		f32                hit_y;
		f32                hit_z;
		f32                hit_last_x;
		f32                hit_last_y;
		f32                hit_last_z;
		f32                hit_nor_x;
		f32                hit_nor_y;
		f32                hit_nor_z;
		f32                contact_time;
		struct tween_anim *timer;
		struct asim_body  *body;
		struct object     *bullet;
	} particles[32];
	i32             particle_index;
	f32             particle_friction;
	f32             particle_bounciness;
	f32             particle_gravity_x;
	f32             particle_gravity_y;
	f32             particle_gravity_z;
	f32             particle_lifetime;
	f32             particle_mass;
	f32             particle_random;
	f32             particle_spawn_distance;
	struct any_map *keymap;
} context_t;

typedef struct node_shader {
	struct node_shader_context *context;
	struct string_array        *ins;
	struct string_array        *outs;
	char                       *frag_out;
	struct string_array        *consts;
	struct string_array        *textures;
	struct any_map             *functions;

	buffer_t vert;
	buffer_t vert_end;
	buffer_t vert_normal;
	buffer_t vert_attribs;
	i32      vert_write_normal;

	buffer_t frag;
	buffer_t frag_end;
	buffer_t frag_normal;
	buffer_t frag_attribs;
	i32      frag_write_normal;

	// References
	bool vert_n;
	bool frag_bposition;
	bool frag_wposition;
	bool frag_mposition;
	bool frag_vposition;
	bool frag_wvpposition;
	bool frag_ndcpos;
	bool frag_wtangent;
	bool frag_vvec;
	bool frag_vvec_cam;
	bool frag_n;
	bool frag_nattr;
	bool frag_dotnv;
} node_shader_t;

typedef struct material {
	char                  *name;
	struct ui_node_canvas *canvas;
} material_t;

typedef struct node_shader_context {
	struct node_shader    *kong;
	struct shader_context *data;
	bool                   allow_vcols;
	struct material       *material;
} node_shader_context_t;

typedef struct history_step {
	char                  *name;
	history_action_t       action;
	struct ui_node_canvas *canvas; // Node history
	i32                    canvas_group;
	i32                    layer;
	layer_slot_type_t      layer_type;
	i32                    layer_parent;
	i32                    object;
	i32                    material;
	i32                    brush;
	f32                    layer_opacity;
	i32                    layer_object;
	i32                    layer_blending;
	i32                    prev_order; // Previous layer position
	i32                    canvas_type;
} history_step_t;

typedef struct logic_node {
	struct logic_node_input_t_array *inputs;
	struct logic_node_t_array       *outputs;
	struct logic_node_value *(*get)(void *, i32);
	struct gpu_texture *(*get_as_image)(void *, i32);
	struct gpu_texture *(*get_cached_image)(void *);
	void (*set)(void *, struct f32_array *);
	struct logic_node_ext *ext;
} logic_node_t;

typedef struct logic_node_ext {
	struct logic_node *base;
} logic_node_ext_t;

typedef struct logic_node_value {
	f32    _f32;
	vec4_t _vec4;
	char  *_str;
} logic_node_value_t;

typedef struct logic_node_input {
	struct logic_node_ext *node;
	i32                    from; // Socket index
} logic_node_input_t;

typedef struct node_group {
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
} node_group_t;

typedef struct {
	char           *name;
	string_array_t *objects;
	string_array_t *layers;
} stage_t;

typedef struct {
	struct asset_t_array         *assets;
	struct slot_material_t_array *materials;
	struct slot_brush_t_array    *brushes;
	struct slot_layer_t_array    *layers;
	struct slot_font_t_array     *fonts;
	struct slot_sound_t_array    *sounds;
	struct node_group_t_array    *material_groups;
	char                         *filepath;
	i32                           next_asset_id;
	struct mesh_object_t_array   *paint_objects;
} project_runtime_t;

typedef struct project {
	char                                        *version;
	struct string_array                         *assets;  // texture_assets
	bool                                         is_bgra; // Swapped red and blue channels for layer textures
	struct packed_asset_t_array                 *packed_assets;
	char                                        *envmap; // Asset name
	f32                                          envmap_strength;
	f32                                          envmap_angle;
	bool                                         envmap_blur;
	struct f32_array                            *camera_world;
	struct f32_array                            *camera_origin;
	f32                                          camera_fov;
	struct swatch_color_t_array                 *swatches;
	struct ui_node_canvas_t_array               *brush_nodes;
	struct buffer_t_array                       *brush_icons;
	struct ui_node_canvas_t_array               *material_nodes;
	struct ui_node_canvas_t_array               *material_groups;
	struct buffer_t_array                       *material_icons;
	struct material_data2_t_array               *material_datas;
	struct string_array                         *font_assets;
	struct string_array                         *sound_assets;
	struct layer_data_t_array                   *layer_datas;
	struct mesh_data_t_array                    *mesh_datas;
	struct string_array                         *mesh_assets;
	struct buffer_t_array                       *mesh_icons;
	struct f32_array_t_array                    *mesh_transforms;
	struct i32_array                            *mesh_materials;
	struct i32_array                            *mesh_parents;
	struct i32_array                            *mesh_physics_shapes; // -1 = no physics
	struct f32_array                            *mesh_physics_masses;
	struct buffer_t_array                       *mesh_skins; // NULL = no skin
	struct i32_array                            *atlas_objects;
	struct string_array                         *atlas_names;
	struct string_array                         *script_datas;
	struct string_array                         *script_names;
	i32                                          timeline_frame_rate;
	i32                                          timeline_max_frames;
	struct timeline_layer_keyframe_data_t_array *timeline_layers;
	struct timeline_mesh_keyframe_data_t_array  *timeline_meshes;
	struct stage_t_array                        *stages;
	project_runtime_t                           *_;
} project_t;

typedef struct asset {
	i32            id;
	char          *name;
	char          *file;
	gpu_texture_t *image;
} asset_t;

typedef struct packed_asset {
	char          *name;
	struct buffer *bytes;
} packed_asset_t;

typedef struct swatch_color {
	i32 base;
	f32 opacity;
	f32 occlusion;
	f32 roughness;
	f32 metallic;
	i32 normal;
	f32 emission;
	f32 height;
	f32 subsurface;
} swatch_color_t;

typedef struct layer_data {
	char             *name;
	i32               res; // Width pixels
	i32               bpp; // Bits per pixel
	struct buffer    *texpaint;
	f32               uv_scale;
	f32               uv_rot;
	i32               uv_type;
	struct f32_array *decal_mat;
	f32               opacity_mask;
	i32               fill_material;
	i32               object_mask;
	i32               blending;
	i32               parent;
	bool              visible;
	struct buffer    *texpaint_nor;
	struct buffer    *texpaint_pack;
	struct buffer    *texpaint_sculpt;
	bool              paint_base;
	bool              paint_opac;
	bool              paint_occ;
	bool              paint_rough;
	bool              paint_met;
	bool              paint_nor;
	bool              paint_nor_blend;
	bool              paint_height;
	bool              paint_height_blend;
	bool              paint_emis;
	bool              paint_subs;
	i32               uv_map;
	f32_array_t      *path_points;
	f32_array_t      *path_points_world;
	f32_array_t      *path_points_camera;
	i32_array_t      *path_points_parent;
	i32               path_tool;
	bool              path_curved;
	i32               path_material;
} layer_data_t;

typedef struct timeline_layer_keyframe_data {
	i32               frame;
	i32               layer_index;
	struct buffer    *texpaint;
	struct buffer    *texpaint_nor;
	struct buffer    *texpaint_pack;
	struct f32_array *path_points;
	struct f32_array *path_points_world;
	struct f32_array *path_points_camera;
	struct i32_array *path_points_parent;
	bool              tween;
} timeline_layer_keyframe_data_t;

typedef struct timeline_mesh_keyframe_data {
	i32               frame;
	i32               mesh_index;
	struct f32_array *transform;
	bool              tween;
} timeline_mesh_keyframe_data_t;

typedef struct shader_out {
	char *out_basecol;
	char *out_roughness;
	char *out_metallic;
	char *out_occlusion;
	char *out_opacity;
	char *out_height;
	char *out_emission;
	char *out_subsurface;
} shader_out_t;

typedef struct plugin {
	void        *on_ui;
	void        *on_draw;
	void        *on_update;
	void        *on_delete;
	char        *version;
	char        *name;
	minic_ctx_t *ctx;
} plugin_t;

typedef struct rect {
	i32 x;
	i32 y;
	i32 w;
	i32 h;
} rect_t;

typedef struct parse_node_preview_result {
	struct shader_context   *scon;
	struct material_context *mcon;
} parse_node_preview_result_t;

typedef struct ui_node_t_array *node_list_t;

typedef struct export_preset {
	struct export_preset_texture_t_array *textures;
} export_preset_t;

typedef struct export_preset_texture {
	char                *name;
	struct string_array *channels;
	char                *color_space;
} export_preset_texture_t;

typedef struct neural_node_model {
	char                *name;
	char                *memory;
	char                *size;
	char                *nodes;
	struct string_array *urls;
	char                *web;
	char                *license;
} neural_node_model_t;

typedef struct float_node {
	struct logic_node  *base;
	f32                 value;
	struct gpu_texture *image;
} float_node_t;

typedef struct node_group_t_array {
	node_group_t **buffer;
	int            length;
	int            capacity;
} node_group_t_array_t;

typedef struct neural_node_model_t_array {
	neural_node_model_t **buffer;
	int                   length;
	int                   capacity;
} neural_node_model_t_array_t;

typedef struct buffer_t_array {
	buffer_t **buffer;
	int        length;
	int        capacity;
} buffer_t_array_t;

typedef struct f32_array_t_array {
	f32_array_t **buffer;
	int           length;
	int           capacity;
} f32_array_t_array_t;

typedef struct slot_material_t_array {
	slot_material_t **buffer;
	int               length;
	int               capacity;
} slot_material_t_array_t;

typedef struct mesh_object_t_array {
	mesh_object_t **buffer;
	int             length;
	int             capacity;
} mesh_object_t_array_t;

typedef struct ui_handle_t_array {
	ui_handle_t **buffer;
	int           length;
	int           capacity;
} ui_handle_t_array_t;

typedef struct ui_node_canvas_t_array {
	ui_node_canvas_t **buffer;
	int                length;
	int                capacity;
} ui_node_canvas_t_array_t;

typedef struct node_list_t_array {
	node_list_t **buffer;
	int           length;
	int           capacity;
} node_list_t_array_t;

typedef struct slot_brush_t_array {
	slot_brush_t **buffer;
	int            length;
	int            capacity;
} slot_brush_t_array_t;

typedef struct asset_t_array {
	asset_t **buffer;
	int       length;
	int       capacity;
} asset_t_array_t;

typedef struct slot_font_t_array {
	slot_font_t **buffer;
	int           length;
	int           capacity;
} slot_font_t_array_t;

typedef struct slot_sound_t_array {
	slot_sound_t **buffer;
	int            length;
	int            capacity;
} slot_sound_t_array_t;

typedef struct slot_layer_t_array {
	slot_layer_t **buffer;
	int            length;
	int            capacity;
} slot_layer_t_array_t;

typedef struct gpu_texture_t_array {
	gpu_texture_t **buffer;
	int             length;
	int             capacity;
} gpu_texture_t_array_t;

typedef struct ui_node_socket_t_array {
	ui_node_socket_t **buffer;
	int                length;
	int                capacity;
} ui_node_socket_t_array_t;

typedef struct logic_node_t_array {
	logic_node_t **buffer;
	int            length;
	int            capacity;
} logic_node_t_array_t;

typedef struct transform_t_array {
	transform_t **buffer;
	int           length;
	int           capacity;
} transform_t_array_t;

typedef struct ui_node_button_t_array {
	ui_node_button_t **buffer;
	int                length;
	int                capacity;
} ui_node_button_t_array_t;

typedef struct logic_node_input_t_array {
	logic_node_input_t **buffer;
	int                  length;
	int                  capacity;
} logic_node_input_t_array_t;

typedef struct history_step_t_array {
	history_step_t **buffer;
	int              length;
	int              capacity;
} history_step_t_array_t;

typedef struct tab_draw_array_t_array {
	tab_draw_array_t **buffer;
	int                length;
	int                capacity;
} tab_draw_array_t_array_t;

typedef struct tab_draw_t_array {
	tab_draw_t **buffer;
	int          length;
	int          capacity;
} tab_draw_t_array_t;

typedef struct object_t_array {
	object_t **buffer;
	int        length;
	int        capacity;
} object_t_array_t;

typedef struct render_target_t_array {
	render_target_t **buffer;
	int               length;
	int               capacity;
} render_target_t_array_t;

typedef struct ui_node_link_t_array {
	ui_node_link_t **buffer;
	int              length;
	int              capacity;
} ui_node_link_t_array_t;

typedef struct mesh_data_t_array {
	mesh_data_t **buffer;
	int           length;
	int           capacity;
} mesh_data_t_array_t;

typedef struct shader_context_t_array {
	shader_context_t **buffer;
	int                length;
	int                capacity;
} shader_context_t_array_t;

typedef struct raw_mesh_t_array {
	raw_mesh_t **buffer;
	int          length;
	int          capacity;
} raw_mesh_t_array_t;

typedef struct export_preset_texture_t_array {
	export_preset_texture_t **buffer;
	int                       length;
	int                       capacity;
} export_preset_texture_t_array_t;

typedef struct layer_data_t_array {
	layer_data_t **buffer;
	int            length;
	int            capacity;
} layer_data_t_array_t;

typedef struct timeline_layer_keyframe_data_t_array {
	timeline_layer_keyframe_data_t **buffer;
	int                              length;
	int                              capacity;
} timeline_layer_keyframe_data_t_array_t;

typedef struct timeline_mesh_keyframe_data_t_array {
	timeline_mesh_keyframe_data_t **buffer;
	int                             length;
	int                             capacity;
} timeline_mesh_keyframe_data_t_array_t;

typedef struct material_data2 {
	bool paint_base;
	bool paint_opac;
	bool paint_occ;
	bool paint_rough;
	bool paint_met;
	bool paint_nor;
	bool paint_height;
	bool paint_emis;
	bool paint_subs;
	int  opac_mode;
} material_data2_t;

typedef struct material_data2_t_array {
	material_data2_t **buffer;
	int                length;
	int                capacity;
} material_data2_t_array_t;

typedef struct ui_node_t_array {
	ui_node_t **buffer;
	int         length;
	int         capacity;
} ui_node_t_array_t;

typedef struct packed_asset_t_array {
	packed_asset_t **buffer;
	int              length;
	int              capacity;
} packed_asset_t_array_t;

typedef struct swatch_color_t_array {
	swatch_color_t **buffer;
	int              length;
	int              capacity;
} swatch_color_t_array_t;

typedef struct stage_t_array {
	stage_t **buffer;
	int       length;
	int       capacity;
} stage_t_array_t;
