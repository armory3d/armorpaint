
#pragma once

#include "enums.h"
#include "types.h"
#include <iron.h>

char *manifest_title           = "ArmorPaint";
char *manifest_version         = "1.0 alpha";
char *manifest_version_project = "11";
char *manifest_version_config  = "1";
char *manifest_url             = "https://armorpaint.org";
char *manifest_url_android     = "https://play.google.com/store/apps/details?id=org.armorpaint";
char *manifest_url_ios         = "https://apps.apple.com/app/armorpaint/id1533967534";

context_t   *g_context;
project_t   *g_project;
config_t    *g_config;
any_map_t   *g_keymap;
ui_t        *g_ui;
draw_font_t *g_font;
ui_theme_t  *g_theme;
any_map_t   *g_plugins;
any_map_t   *g_operators;

any_map_t       *ui_children;
any_map_t       *ui_nodes_custom_buttons;
gpu_texture_t   *lut_image             = NULL;
i32              lut_size              = 0;
gpu_texture_t   *layers_temp_image     = NULL;
gpu_texture_t   *layers_expa           = NULL;
gpu_texture_t   *layers_expb           = NULL;
gpu_texture_t   *layers_expc           = NULL;
f32              layers_default_base   = 0.5;
f32              layers_default_rough  = 0.4;
ui_align_t       config_button_align   = UI_ALIGN_LEFT;
char            *config_button_spacing = "       ";
any_imap_t      *physics_body_object_map;
ui_handle_t     *box_export_htab;
string_array_t  *box_export_files = NULL;
ui_handle_t     *box_export_mesh_handle;
ui_handle_t     *box_export_hpreset;
export_preset_t *box_export_preset = NULL;
string_array_t  *box_export_channels;
string_array_t  *box_export_color_spaces;
ui_handle_t     *box_export_h_export_player_target;
gpu_pipeline_t  *pipes_copy;
gpu_pipeline_t  *pipes_copy8;
gpu_pipeline_t  *pipes_copy64;
gpu_pipeline_t  *pipes_copy128;
gpu_pipeline_t  *pipes_copy_bgra;
gpu_pipeline_t  *pipes_copy_rgb = NULL;
gpu_pipeline_t  *pipes_merge    = NULL;
gpu_pipeline_t  *pipes_merge_r  = NULL;
gpu_pipeline_t  *pipes_merge_g  = NULL;
gpu_pipeline_t  *pipes_merge_b  = NULL;
gpu_pipeline_t  *pipes_merge_mask;
gpu_pipeline_t  *pipes_invert_mask;
gpu_pipeline_t  *pipes_apply_mask;
gpu_pipeline_t  *pipes_colorid_to_mask;
i32              pipes_tex0;
i32              pipes_tex1;
i32              pipes_texmask;
i32              pipes_texa;
i32              pipes_opac;
i32              pipes_blending;
i32              pipes_tex1w;
i32              pipes_tex0_mask;
i32              pipes_texa_mask;
i32              pipes_opac_apply_mask;
i32              pipes_tex0_merge_mask;
i32              pipes_texa_merge_mask;
i32              pipes_tex_colorid;
i32              pipes_texpaint_colorid;
i32              pipes_opac_merge_mask;
i32              pipes_blending_merge_mask;
gpu_texture_t   *pipes_temp_mask_image = NULL;
gpu_pipeline_t  *pipes_cursor;
i32              pipes_cursor_vp;
i32              pipes_cursor_inv_vp;
i32              pipes_cursor_mouse;
i32              pipes_cursor_tex_step;
i32              pipes_cursor_radius;
i32              pipes_cursor_camera_right;
i32              pipes_cursor_tint;
i32              pipes_cursor_camera_up;
i32              pipes_cursor_camera_align;
i32              pipes_cursor_gbufferd;
gpu_pipeline_t  *pipes_cursor_decal;
i32              pipes_cursor_decal_vp;
i32              pipes_cursor_decal_inv_vp;
i32              pipes_cursor_decal_mouse;
i32              pipes_cursor_decal_tex_step;
i32              pipes_cursor_decal_radius;
i32              pipes_cursor_decal_camera_right;
i32              pipes_cursor_decal_opacity;
i32              pipes_cursor_decal_angle;
i32              pipes_cursor_decal_scale_x;
i32              pipes_cursor_decal_camera_up;
i32              pipes_cursor_decal_camera_align;
i32              pipes_cursor_decal_gbufferd;
i32              pipes_cursor_decal_texdecal;
i32              pipes_cursor_decal_gbuffer0;
i32              pipes_offset;
any_map_t       *import_texture_importers;
any_map_t       *import_text_importers;

#ifdef IRON_WINDOWS
char *ui_files_default_path = "C:\\Users";
#elif defined(IRON_MACOS)
char *ui_files_default_path = "/Users";
#else
char *ui_files_default_path = "/";
#endif

char            *ui_files_filename;
char            *ui_files_path;
char            *ui_files_last_path     = "";
bool             base_ui_enabled        = true;
bool             base_view3d_show       = true;
bool             base_is_dragging       = false;
bool             base_is_resizing       = false;
asset_t         *base_drag_asset        = NULL;
swatch_color_t  *base_drag_swatch       = NULL;
char            *base_drag_file         = NULL;
gpu_texture_t   *base_drag_file_icon    = NULL;
f32              base_drag_off_x        = 0.0;
f32              base_drag_off_y        = 0.0;
i32              base_default_element_w = 100;
i32              base_default_element_h = 28;
i32              base_default_font_size = 13;
ui_handle_t     *base_res_handle;
ui_handle_t     *base_res_x_handle;
ui_handle_t     *base_res_y_handle;
ui_handle_t     *base_bits_handle;
string_array_t  *base_drop_paths;
slot_material_t *base_drag_material = NULL;
slot_layer_t    *base_drag_layer    = NULL;
slot_brush_t    *base_drag_brush    = NULL;
slot_font_t     *base_drag_font     = NULL;
slot_sound_t    *base_drag_sound    = NULL;
mesh_object_t   *base_drag_mesh     = NULL;
bool             base_player_lock   = false;

bool                      ui_base_show = true;
i32                       ui_base_viewport_col;
ui_handle_t_array_t      *ui_base_hwnds;
ui_handle_t_array_t      *ui_base_htabs;
i32                       ui_base_border_started;
ui_handle_t              *ui_base_border_handle;
tab_draw_array_t_array_t *ui_base_hwnd_tabs;
i32                       ui_toolbar_default_w = 36;
ui_handle_t              *ui_toolbar_handle;
string_array_t           *ui_toolbar_tool_names;
string_array_t           *ui_toolbar_tooltip_extras;
mat4_t                    uniforms_ext_ortho_p;
ui_handle_t              *box_projects_htab;
ui_handle_t              *box_projects_hsearch;
history_step_t_array_t   *history_steps;
i32                       history_undo_i      = 0;     // Undo layer
i32                       history_undos       = 0;     // Undos available
i32                       history_redos       = 0;     // Redos available
bool                      history_push_undo   = false; // Store undo on next paint
slot_layer_t_array_t     *history_undo_layers = NULL;
ui_handle_t              *tab_scripts_hscript;
bool                      tab_scripts_minimap_dirty = true;
extern int                tab_stages_selected;
any_map_t                *import_mesh_importers;
i32                       ui_menubar_default_w = 406;
ui_handle_t              *ui_menubar_hwnd;
ui_handle_t              *ui_menubar_menu_handle;
ui_handle_t              *ui_menubar_tab;
i32                       ui_menubar_w;
any_map_t                *translator_translations;
physics_world_t          *physics_world_active;
bool                      ui_menu_show                       = false;
bool                      ui_menu_nested                     = false;
i32                       ui_menu_x                          = 0;
i32                       ui_menu_y                          = 0;
i32                       ui_menu_h                          = 0;
bool                      ui_menu_keep_open                  = false;
void (*ui_menu_commands)(void)                               = NULL;
bool                      ui_menu_show_first                 = true;
i32                       render_path_base_bloom_current_mip = 0;
f32                       render_path_base_bloom_sample_scale;
bool                      render_path_base_buf_swapped = false;
string_array_t           *project_default_mesh_list    = NULL;
gpu_pipeline_t           *ui_view2d_pipe;
i32                       ui_view2d_channel_loc;
view_2d_type_t            ui_view2d_type = VIEW_2D_TYPE_LAYER;
bool                      ui_view2d_show = false;
i32                       ui_view2d_wx;
i32                       ui_view2d_wy;
i32                       ui_view2d_ww;
i32                       ui_view2d_wh;
ui_handle_t              *ui_view2d_hwnd;
f32                       ui_view2d_pan_x       = 0.0;
f32                       ui_view2d_pan_y       = 0.0;
f32                       ui_view2d_pan_scale   = 1.0;
bool                      ui_view2d_tiled_show  = false;
bool                      ui_view2d_grid_redraw = true;
ui_handle_t              *ui_view2d_htab;
bool                      sim_running                = false;
bool                      sim_record                 = false;
bool                      viewport_recording         = false;
bool                      node_shader_dump_to_script = false;
node_shader_context_t    *parser_material_con;
node_shader_t            *parser_material_kong;
material_context_t       *parser_material_matcon;
string_array_t           *parser_material_parsed;
ui_node_t_array_t        *parser_material_parents;
ui_node_canvas_t_array_t *parser_material_canvases;
ui_node_t_array_t        *parser_material_nodes;
ui_node_link_t_array_t   *parser_material_links;
f32                       parser_material_eps = 0.000001;
any_map_t                *parser_material_node_values;
any_map_t                *parser_material_node_vectors;
any_map_t                *parser_material_custom_nodes;
bool                      parser_material_parse_height            = false;
bool                      parser_material_parse_emission          = false;
bool                      parser_material_parse_subsurface        = false;
bool                      parser_material_parsing_basecolor       = false;
bool                      parser_material_triplanar               = false; // Sample using tex_coord/1/2 & tex_coord_blend
bool                      parser_material_sample_keep_aspect      = false; // Adjust uvs to preserve texture aspect ratio
char                     *parser_material_sample_uv_scale         = "1.0";
bool                      parser_material_transform_color_space   = true;
bool                      parser_material_blur_passthrough        = false;
bool                      parser_material_warp_passthrough        = false;
bool                      parser_material_bake_passthrough        = false;
ui_node_canvas_t         *parser_material_start_group             = NULL;
ui_node_t_array_t        *parser_material_start_parents           = NULL;
ui_node_t                *parser_material_start_node              = NULL;
char                     *parser_material_out_normaltan; // Raw tangent space normal parsed from normal map
any_map_t                *parser_material_script_links = NULL;
any_map_t                *parser_material_parsed_map;
any_map_t                *parser_material_texture_map;
bool                      parser_material_is_frag           = true;
bool                      args_player                       = false;
i32                       util_render_material_preview_size = 256;
i32                       util_render_node_preview_size     = 512;
i32                       util_render_decal_preview_size    = 512;
i32                       util_render_layer_preview_size    = 200;
i32                       util_render_font_preview_size     = 200;
ui_handle_t              *tab_browser_hpath;
ui_handle_t              *tab_browser_hsearch;
bool                      tab_browser_refresh = false;
extern i32                ui_files_selected;
extern i32                path_point_dragging;
extern i32                path_layer_last_active;
any_map_t                *util_mesh_unwrappers;
i32                       ui_header_default_h = 30;
i32                       ui_header_h;
ui_handle_t              *ui_header_handle;
any_map_t                *parser_logic_custom_nodes;
any_map_t                *resource_bundled;
i32                       render_path_raytrace_bake_rays_pix       = 0;
i32                       render_path_raytrace_bake_rays_sec       = 0;
i32                       render_path_raytrace_bake_current_sample = 0;
bool                      bake_texture_node_baking                 = false;
gpu_texture_t            *bake_texture_node_texpaint               = NULL;
bool                      make_material_transluc_used              = false;
bool                      make_material_height_used                = false;
bool                      make_material_emis_used                  = false;
bool                      make_material_subs_used                  = false;
bool                      ui_nodes_show                            = false;
i32                       ui_nodes_wx;
i32                       ui_nodes_wy;
i32                       ui_nodes_ww;
i32                       ui_nodes_wh;
canvas_type_t             ui_nodes_canvas_type       = CANVAS_TYPE_MATERIAL;
bool                      ui_nodes_grid_redraw       = true;
i32                       ui_nodes_grid_cell_w       = 200;
i32                       ui_nodes_grid_small_cell_w = 40;
ui_handle_t              *ui_nodes_hwnd;
node_group_t_array_t     *ui_nodes_group_stack;
ui_handle_t              *ui_nodes_htab;
f32                       ui_nodes_last_zoom = 1.0;
ui_handle_t              *_ui_nodes_htype;
ui_handle_t              *_ui_nodes_hname;
ui_handle_t              *_ui_nodes_hmin;
ui_handle_t              *_ui_nodes_hmax;
ui_handle_t              *_ui_nodes_hval0;
ui_handle_t              *_ui_nodes_hval1;
ui_handle_t              *_ui_nodes_hval2;
ui_handle_t              *_ui_nodes_hval3;
ui_node_t_array_t        *nodes_brush_category0;
string_array_t           *nodes_brush_categories;
node_list_t_array_t      *nodes_brush_list;
any_map_t                *nodes_brush_creates;
string_array_t           *nodes_material_categories;
ui_node_t_array_t        *nodes_material_input;
ui_node_t_array_t        *nodes_material_texture;
ui_node_t_array_t        *nodes_material_color;
ui_node_t_array_t        *nodes_material_utilities;
ui_node_t_array_t        *nodes_material_neural;
ui_node_t_array_t        *nodes_material_group;
node_list_t_array_t      *nodes_material_list        = NULL;
i32                       make_mesh_layer_pass_count = 1;

i32             ui_sidebar_default_w_mini = 56;
i32             ui_sidebar_default_w_full = 280;
i32             ui_sidebar_default_w;
ui_handle_t    *ui_sidebar_hminimized;
i32             ui_sidebar_w_mini;
char           *console_message       = "";
f32             console_message_timer = 0.0;
i32             console_message_color = 0x00000000;
string_array_t *console_last_traces;
vec4_t          camera_origins[2];
mat4_t          camera_views[2];
ui_handle_t    *box_preferences_htab;
string_array_t *box_preferences_files_plugin = NULL;
ui_handle_t    *box_preferences_h_theme;
ui_handle_t    *box_preferences_h_preset;
slot_layer_t   *render_path_paint_live_layer        = NULL;
i32             render_path_paint_live_layer_drawn  = 0;
bool            render_path_paint_live_layer_locked = false;
bool            render_path_paint_push_undo_last;
bool            render_path_paint_baking = false;
bool            ui_box_show              = false;
ui_handle_t    *ui_box_hwnd;
bool            ui_box_click_to_hide = true;
i32             ui_box_modalw        = 400;
i32             ui_box_modalh        = 170;
ui_handle_t    *tab_layers_layer_name_handle;
ui_handle_t    *tab_meshes_mesh_name_handle;
slot_layer_t   *tab_layers_l;
gpu_texture_t  *util_uv_uvmap                    = NULL;
bool            util_uv_uvmap_cached             = false;
gpu_texture_t  *util_uv_trianglemap              = NULL;
bool            util_uv_trianglemap_cached       = false;
gpu_texture_t  *util_uv_dilatemap                = NULL;
bool            util_uv_dilatemap_cached         = false;
gpu_texture_t  *util_uv_uvislandmap              = NULL;
bool            util_uv_uvislandmap_cached       = false;
i32             render_path_raytrace_frame       = 0;
bool            render_path_raytrace_ready       = false;
bool            render_path_raytrace_init_shader = true;
f32_array_t    *render_path_raytrace_f32a;
mat4_t          render_path_raytrace_help_mat;
gpu_texture_t  *render_path_raytrace_last_envmap = NULL;
bool            render_path_raytrace_is_bake     = false;

bool  sculpt_push_undo                          = false;
i32   ui_statusbar_default_h                    = 33;
char *parser_material_bake_passthrough_strength = "0.0";
char *parser_material_bake_passthrough_radius   = "0.0";
char *parser_material_bake_passthrough_offset   = "0.0";

any_imap_t                  *neural_node_results;
ui_node_t                   *neural_node_current;
i32                          neural_node_downloading = 0;
neural_node_model_t_array_t *neural_node_models      = NULL;

#ifdef IRON_DIRECT3D12
char *render_path_raytrace_ext = ".cso";
#elif defined(IRON_METAL)
char *render_path_raytrace_ext = ".metal";
#else
char *render_path_raytrace_ext = ".spirv";
#endif

char *str_hue_sat = "\
fun hsv_to_rgb(c: float3): float3 { \
	var K: float4 = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0); \
	var p: float3 = abs3(frac3(c.xxx + K.xyz) * 6.0 - K.www); \
	return lerp3(K.xxx, clamp3(p - K.xxx, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0)), c.y) * c.z; \
} \
fun rgb_to_hsv(c: float3): float3 { \
	var K: float4 = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0); \
	var p: float4 = lerp4(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g)); \
	var q: float4 = lerp4(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r)); \
	var d: float = q.x - min(q.w, q.y); \
	var e: float = 0.0000000001; \
	return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x); \
} \
fun hue_sat(col: float3, shift: float4): float3 { \
	var hsv: float3 = rgb_to_hsv(col); \
	hsv.x += shift.x; \
	hsv.y *= shift.y; \
	hsv.z *= shift.z; \
	return lerp3(hsv_to_rgb(hsv), col, shift.w); \
} \
";

char *str_brightcontrast = "\
fun brightcontrast(col: float3, bright: float, contr: float): float3 { \
	var a: float = 1.0 + contr; \
	var b: float = bright - contr * 0.5; \
	return max3(a * col + b, float3(0.0, 0.0, 0.0)); \
} \
";

char *str_cotangent_frame = "\
fun cotangent_frame(n: float3, p: float3, tex_coord: float2): float3x3 { \
	var duv1: float2 = ddx2(tex_coord); \
	var duv2: float2 = ddy2(tex_coord); \
	var dp1: float3 = ddx3(p); \
	var dp2: float3 = ddy3(p); \
	var dp2perp: float3 = cross(dp2, n); \
	var dp1perp: float3 = cross(n, dp1); \
	var t: float3 = dp2perp * duv1.x + dp1perp * duv2.x; \
	var b: float3 = dp2perp * duv1.y + dp1perp * duv2.y; \
	var invmax: float = rsqrt(max(dot(t, t), dot(b, b))); \
	return float3x3(t * invmax, b * invmax, n); \
} \
";

// let str_octahedron_wrap: string = "\
// fun octahedron_wrap(v: float2): float2 { \
// 	return (1.0 - abs(v.yx)) * (float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0)); \
// } \
// ";

char *str_octahedron_wrap = "\
fun octahedron_wrap(v: float2): float2 { \
	var a: float2; \
	if (v.x >= 0.0) { a.x = 1.0; } else { a.x = -1.0; } \
	if (v.y >= 0.0) { a.y = 1.0; } else { a.y = -1.0; } \
	var r: float2; \
	r.x = abs(v.y); \
	r.y = abs(v.x); \
	r.x = 1.0 - r.x; \
	r.y = 1.0 - r.y; \
	return r * a; \
} \
";

// let str_pack_float_int16: string = "\
// fun pack_f32_i16(f: float, i: uint): float { \
// 	var prec: float = float(1 << 16); \
// 	var maxi: float = float(1 << 4); \
// 	var prec_minus_one: float = prec - 1.0; \
// 	var t1: float = ((prec / maxi) - 1.0) / prec_minus_one; \
// 	var t2: float = (prec / maxi) / prec_minus_one; \
// 	return t1 * f + t2 * float(i); \
// } \
// ";

char *str_pack_float_int16 = "\
fun pack_f32_i16(f: float, i: uint): float { \
	return 0.062504762 * min(f, 0.9999) + 0.062519999 * float(i); \
} \
";

char *str_dither_bayer = "\
fun dither_bayer(uv: float2): float { \
	var x: int = int(uv.x % 4.0); \
	var y: int = int(uv.y % 4.0); \
	if (y == 0) { \
		if (x == 0) { \
			return 0.0 / 16.0; \
		} \
		if (x == 1) { \
			return 8.0 / 16.0; \
		} \
		if (x == 2) { \
			return 2.0 / 16.0; \
		} \
		return 10.0 / 16.0; \
	} \
	if (y == 1) { \
		if (x == 0) { \
			return 12.0 / 16.0; \
		} \
		if (x == 1) { \
			return 4.0 / 16.0; \
		} \
		if (x == 2) { \
			return 14.0 / 16.0; \
		} \
		return 6.0 / 16.0; \
	} \
	if (y == 2) { \
		if (x == 0) { \
			return 3.0 / 16.0; \
		} \
		if (x == 1) { \
			return 11.0 / 16.0; \
		} \
		if (x == 2) { \
			return 1.0 / 16.0; \
		} \
		return 9.0 / 16.0; \
	} \
	if (x == 0) { \
		return 15.0 / 16.0; \
	} \
	if (x == 1) { \
		return 7.0 / 16.0; \
	} \
	if (x == 2) { \
		return 13.0 / 16.0; \
	} \
	return 5.0 / 16.0; \
} \
";
