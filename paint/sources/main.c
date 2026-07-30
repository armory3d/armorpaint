
#include "global.h"

#include "io/export_arm.c"
#include "io/export_exr.c"
#include "io/export_mesh.c"
#include "io/export_obj.c"
#include "io/export_player.c"
#include "io/export_texture.c"
#include "io/import_arm.c"
#include "io/import_asset.c"
#include "io/import_blend_mesh.c"
#include "io/import_envmap.c"
#include "io/import_folder.c"
#include "io/import_font.c"
#include "io/import_json_material.c"
#include "io/import_keymap.c"
#include "io/import_legacy.c"
#include "io/import_lut.c"
#include "io/import_mesh.c"
#include "io/import_obj.c"
#include "io/import_plugin.c"
#include "io/import_sound.c"
#include "io/import_text.c"
#include "io/import_texture.c"
#include "io/import_theme.c"

#include "nodes_brush/boolean_node.c"
#include "nodes_brush/brush_output_node.c"
#include "nodes_brush/color_node.c"
#include "nodes_brush/float_node.c"
#include "nodes_brush/input_node.c"
#include "nodes_brush/integer_node.c"
#include "nodes_brush/math_node.c"
#include "nodes_brush/null_node.c"
#include "nodes_brush/random_node.c"
#include "nodes_brush/separate_vector_node.c"
#include "nodes_brush/string_node.c"
#include "nodes_brush/tex_image_node.c"
#include "nodes_brush/time_node.c"
#include "nodes_brush/vector_math_node.c"
#include "nodes_brush/vector_node.c"

#include "nodes_material/attribute_node.c"
#include "nodes_material/bake_texture_node.c"
#include "nodes_material/blur_node.c"
#include "nodes_material/brick_texture_node.c"
#include "nodes_material/brightness_contrast_node.c"
#include "nodes_material/bump_node.c"
#include "nodes_material/camera_texture_node.c"
#include "nodes_material/checker_texture_node.c"
#include "nodes_material/clamp_node.c"
#include "nodes_material/color_mask_node.c"
#include "nodes_material/color_ramp_node.c"
#include "nodes_material/combine_color_node.c"
#include "nodes_material/combine_xyz_node.c"
#include "nodes_material/curvature_bake_node.c"
#include "nodes_material/float_curve_node.c"
#include "nodes_material/gabor_texture_node.c"
#include "nodes_material/gamma_node.c"
#include "nodes_material/geometry_node.c"
#include "nodes_material/gradient_texture_node.c"
#include "nodes_material/group_node.c"
#include "nodes_material/hue_saturation_value_node.c"
#include "nodes_material/image_texture_node.c"
#include "nodes_material/invert_color_node.c"
#include "nodes_material/layer_mask_node.c"
#include "nodes_material/layer_node.c"
#include "nodes_material/magic_texture_node.c"
#include "nodes_material/map_range_node.c"
#include "nodes_material/mapping_node.c"
#include "nodes_material/material_node.c"
#include "nodes_material/material_output_node.c"
#include "nodes_material/math2_node.c"
#include "nodes_material/mix_color_node.c"
#include "nodes_material/mix_normal_map_node.c"
#include "nodes_material/noise_texture_node.c"
#include "nodes_material/normal_map_node.c"
#include "nodes_material/normal_node.c"
#include "nodes_material/object_info_node.c"
#include "nodes_material/picker_node.c"
#include "nodes_material/quantize_node.c"
#include "nodes_material/replace_color_node.c"
#include "nodes_material/rgb_curves_node.c"
#include "nodes_material/rgb_node.c"
#include "nodes_material/rgb_to_bw_node.c"
#include "nodes_material/script_node.c"
#include "nodes_material/separate_color_node.c"
#include "nodes_material/separate_xyz_node.c"
#include "nodes_material/shader_node.c"
#include "nodes_material/text_texture_node.c"
#include "nodes_material/texture_coordinate_node.c"
#include "nodes_material/tilesheet_animation_node.c"
#include "nodes_material/tilesheet_node.c"
#include "nodes_material/uv_map_node.c"
#include "nodes_material/value_node.c"
#include "nodes_material/vector_curves_node.c"
#include "nodes_material/vector_math2_node.c"
#include "nodes_material/vector_rotate_node.c"
#include "nodes_material/vector_transform_node.c"
#include "nodes_material/voronoi_texture_node.c"
#include "nodes_material/warp_node.c"
#include "nodes_material/wave_texture_node.c"
#include "nodes_material/wireframe_node.c"

#include "nodes_neural/edit_image_node.c"
#include "nodes_neural/image_to_3d_mesh_node.c"
#include "nodes_neural/image_to_pbr_node.c"
#include "nodes_neural/neural_node.c"
#include "nodes_neural/neural_node_models.c"
#include "nodes_neural/repeat_node.c"
#include "nodes_neural/save_image_node.c"
#include "nodes_neural/text_to_image_node.c"
#include "nodes_neural/text_to_text_node.c"
#include "nodes_neural/texture_mesh_node.c"
#include "nodes_neural/upscale_image_node.c"

#include "render/make_bake.c"
#include "render/make_blur.c"
#include "render/make_brush.c"
#include "render/make_clone.c"
#include "render/make_depth.c"
#include "render/make_discard.c"
#include "render/make_material.c"
#include "render/make_mesh.c"
#include "render/make_mesh_preview.c"
#include "render/make_node_preview.c"
#include "render/make_paint.c"
#include "render/make_particle.c"
#include "render/make_picking.c"
#include "render/make_sculpt.c"
#include "render/make_texcoord.c"
#include "render/render_compass.c"
#include "render/render_envsphere.c"
#include "render/render_gizmo.c"
#include "render/render_path_base.c"
#include "render/render_path_deferred.c"
#include "render/render_path_forward.c"
#include "render/render_path_paint.c"
#include "render/render_path_preview.c"
#include "render/render_path_raytrace.c"
#include "render/render_path_raytrace_bake.c"
#include "render/render_pathsphere.c"

#include "ui/box_export.c"
#include "ui/box_import_mesh.c"
#include "ui/box_new_project.c"
#include "ui/box_preferences.c"
#include "ui/box_projects.c"
#include "ui/tab_browser.c"
#include "ui/tab_brushes.c"
#include "ui/tab_console.c"
#include "ui/tab_debug.c"
#include "ui/tab_fonts.c"
#include "ui/tab_layers.c"
#include "ui/tab_materials.c"
#include "ui/tab_meshes.c"
#include "ui/tab_plugins.c"
#include "ui/tab_scripts.c"
#include "ui/tab_sounds.c"
#include "ui/tab_swatches.c"
#include "ui/tab_textures.c"
#include "ui/tab_timeline.c"
#include "ui/ui_base.c"
#include "ui/ui_box.c"
#include "ui/ui_files.c"
#include "ui/ui_header.c"
#include "ui/ui_menu.c"
#include "ui/ui_menubar.c"
#include "ui/ui_nodes.c"
#include "ui/ui_search.c"
#include "ui/ui_sidebar.c"
#include "ui/ui_statusbar.c"
#include "ui/ui_toolbar.c"
#include "ui/ui_view2d.c"

#include "util/edit_uvmap.c"
#include "util/util_brush.c"
#include "util/util_clone.c"
#include "util/util_cursor.c"
#include "util/util_encode.c"
#include "util/util_geom.c"
#include "util/util_layer.c"
#include "util/util_mesh.c"
#include "util/util_nodes.c"
#include "util/util_particle.c"
#include "util/util_path.c"
#include "util/util_raycast.c"
#include "util/util_render.c"
#include "util/util_resize.c"
#include "util/util_select.c"
#include "util/util_shortcut.c"
#include "util/util_stage.c"
#include "util/util_stencil.c"
#include "util/util_texture.c"
#include "util/util_touch.c"
#include "util/util_ui.c"
#include "util/util_uv.c"

#include "args.c"
#include "base.c"
#include "camera.c"
#include "config.c"
#include "console.c"
#include "context.c"
#include "history.c"
#include "keymap.c"
#include "logic_node.c"
#include "minic_impl.c"
#include "node_shader.c"
#include "nodes_brush.c"
#include "nodes_material.c"
#include "operator.c"
#include "parser_logic.c"
#include "parser_material.c"
#include "pipes.c"
#include "player.c"
#include "plugin.c"
#include "project.c"
#include "resource.c"
#include "sim.c"
#include "slot_brush.c"
#include "slot_font.c"
#include "slot_layer.c"
#include "slot_material.c"
#include "slot_sound.c"
#include "strings.c"
#include "translator.c"
#include "uniforms.c"
#include "viewport.c"

#ifdef WITH_PLUGINS
void plugins_init();
#endif

void _kickstart() {
	_render_path_cached_shader_contexts = any_map_create();
	gc_root(_render_path_cached_shader_contexts);

	ui_children = any_map_create();
	gc_root(ui_children);

	ui_nodes_custom_buttons = any_map_create();
	gc_root(ui_nodes_custom_buttons);

	g_operators = any_map_create();
	gc_root(g_operators);

	box_export_htab = ui_handle_create();
	gc_root(box_export_htab);

	box_export_mesh_handle = ui_handle_create();
	gc_root(box_export_mesh_handle);

	box_export_hpreset = ui_handle_create();
	gc_root(box_export_hpreset);

	box_export_channels = any_array_create_from_raw(
	    (void *[]){
	        "base_r", "base_g", "base_b", "height", "metal",  "nor_r",  "nor_g",  "nor_g_directx", "nor_b",  "occ", "opac", "rough",
	        "smooth", "emis",   "subs",   "diff_r", "diff_g", "diff_b", "spec_r", "spec_g",        "spec_b", "0.0", "1.0",
	    },
	    23);
	gc_root(box_export_channels);

	box_export_color_spaces = any_array_create_from_raw(
	    (void *[]){
	        "linear",
	        "srgb",
	    },
	    2);
	gc_root(box_export_color_spaces);

	box_export_h_export_player_target = ui_handle_create();
	gc_root(box_export_h_export_player_target);

	import_texture_importers = any_map_create();
	gc_root(import_texture_importers);

	import_text_importers = any_map_create();
	gc_root(import_text_importers);

	ui_files_path = ui_files_default_path;
	gc_root(ui_files_path);

	base_res_handle = ui_handle_create();
	gc_root(base_res_handle);

	base_res_x_handle = ui_handle_create();
	gc_root(base_res_x_handle);

	base_res_y_handle = ui_handle_create();
	gc_root(base_res_y_handle);

	base_bits_handle = ui_handle_create();
	gc_root(base_bits_handle);

	base_drop_paths = any_array_create_from_raw((void *[]){}, 0);
	gc_root(base_drop_paths);

	ui_base_hwnds = ui_base_init_hwnds();
	gc_root(ui_base_hwnds);

	ui_base_htabs = ui_base_init_htabs();
	gc_root(ui_base_htabs);

	ui_base_hwnd_tabs = ui_base_init_hwnd_tabs();
	gc_root(ui_base_hwnd_tabs);

	ui_toolbar_handle = ui_handle_create();
	gc_root(ui_toolbar_handle);

	ui_toolbar_tool_names = any_array_create_from_raw(
	    (void *[]){
	        _tr("Brush"),
	        _tr("Eraser"),
	        _tr("Fill"),
	        _tr("Decal"),
	        _tr("Text"),
	        _tr("Clone"),
	        _tr("Blur"),
	        _tr("Particle"),
	        _tr("ColorID"),
	        _tr("Picker"),
	        _tr("Material"),
	        _tr("Cursor"),
	        _tr("Select"),
	        _tr("Bake"), // Hidden, used by Bake Texture node
	    },
	    14);
	gc_root(ui_toolbar_tool_names);
	ui_toolbar_tool_names->length--; // Hide Bake tool

	ui_toolbar_tooltip_extras = any_array_create_from_raw(
	    (void *[]){
	        _tr("Hold {action_paint} to paint\nHold {brush_ruler} and press {action_paint} to paint a straight line (ruler mode)"),
	        _tr("Hold {action_paint} to erase\nHold {brush_ruler} and press {action_paint} to erase a straight line (ruler mode)"),
	        "",
	        _tr("Hold {decal_mask} to paint on a decal mask"),
	        _tr("Hold {decal_mask} to use the text as a mask"),
	        _tr("Hold {set_clone_source} to set source"),
	        "",
	        "",
	        "",
	        "",
	        "",
	        "",
	        "",
	    },
	    13);
	gc_root(ui_toolbar_tooltip_extras);
	uniforms_ext_ortho_p = mat4_ortho(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5);

	box_projects_htab = ui_handle_create();
	gc_root(box_projects_htab);

	box_projects_hsearch = ui_handle_create();
	gc_root(box_projects_hsearch);

	tab_scripts_hscript = ui_handle_create();
	gc_root(tab_scripts_hscript);

	import_mesh_importers = any_map_create();
	gc_root(import_mesh_importers);

	ui_menubar_hwnd = ui_handle_create();
	gc_root(ui_menubar_hwnd);

	ui_menubar_menu_handle = ui_handle_create();
	gc_root(ui_menubar_menu_handle);

	ui_menubar_tab = ui_handle_create();
	gc_root(ui_menubar_tab);
	ui_menubar_w = ui_menubar_default_w;

	translator_translations = any_map_create();
	gc_root(translator_translations);

	g_project = GC_ALLOC_INIT(project_t, {0});
	gc_root(g_project);

	g_project->_           = gc_alloc(sizeof(project_runtime_t));
	g_project->_->filepath = "";
	g_project->_->assets   = any_array_create_from_raw((void *[]){}, 0);

	g_project->mesh_assets = any_array_create_from_raw((void *[]){}, 0);

	g_project->_->material_groups = any_array_create_from_raw((void *[]){}, 0);

	g_project->_->materials = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->brushes   = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->layers    = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->fonts     = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->sounds    = any_array_create_from_raw((void *[]){}, 0);

	ui_view2d_hwnd = ui_handle_create();
	gc_root(ui_view2d_hwnd);

	ui_view2d_htab = ui_handle_create();
	gc_root(ui_view2d_htab);

	parser_material_node_values = any_map_create();
	gc_root(parser_material_node_values);

	parser_material_node_vectors = any_map_create();
	gc_root(parser_material_node_vectors);

	parser_material_custom_nodes = any_map_create();
	gc_root(parser_material_custom_nodes);

	parser_material_parsed_map = any_map_create();
	gc_root(parser_material_parsed_map);

	parser_material_texture_map = any_map_create();
	gc_root(parser_material_texture_map);

	tab_browser_hpath = ui_handle_create();
	gc_root(tab_browser_hpath);

	tab_browser_hsearch = ui_handle_create();
	gc_root(tab_browser_hsearch);

	util_mesh_unwrappers = any_map_create();
	gc_root(util_mesh_unwrappers);

	ui_header_h = ui_header_default_h;

	ui_header_handle = ui_handle_create();
	gc_root(ui_header_handle);

	g_plugins = any_map_create();
	gc_root(g_plugins);

	parser_logic_custom_nodes = any_map_create();
	gc_root(parser_logic_custom_nodes);

	resource_bundled = any_map_create();
	gc_root(resource_bundled);

	ui_nodes_hwnd = ui_handle_create();
	gc_root(ui_nodes_hwnd);

	ui_nodes_group_stack = any_array_create_from_raw((void *[]){}, 0);
	gc_root(ui_nodes_group_stack);

	ui_nodes_htab = ui_handle_create();
	gc_root(ui_nodes_htab);

	_ui_nodes_htype = ui_handle_create();
	gc_root(_ui_nodes_htype);

	_ui_nodes_hname = ui_handle_create();
	gc_root(_ui_nodes_hname);

	_ui_nodes_hmin = ui_handle_create();
	gc_root(_ui_nodes_hmin);

	_ui_nodes_hmax = ui_handle_create();
	gc_root(_ui_nodes_hmax);

	_ui_nodes_hval0 = ui_handle_create();
	gc_root(_ui_nodes_hval0);

	_ui_nodes_hval1 = ui_handle_create();
	gc_root(_ui_nodes_hval1);

	_ui_nodes_hval2 = ui_handle_create();
	gc_root(_ui_nodes_hval2);

	_ui_nodes_hval3 = ui_handle_create();
	gc_root(_ui_nodes_hval3);

	nodes_brush_categories = any_array_create_from_raw(
	    (void *[]){
	        _tr("Nodes"),
	    },
	    1);
	gc_root(nodes_brush_categories);

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	nodes_material_categories = any_array_create_from_raw(
	    (void *[]){
	        _tr("Input"),
	        _tr("Texture"),
	        _tr("Color"),
	        _tr("Utilities"),
	        _tr("Neural"),
	        _tr("Group"),
	    },
	    6);
#else
	nodes_material_categories = any_array_create_from_raw(
	    (void *[]){
	        _tr("Input"),
	        _tr("Texture"),
	        _tr("Color"),
	        _tr("Utilities"),
	        _tr("Group"),
	    },
	    5);
#endif
	gc_root(nodes_material_categories);

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	ui_sidebar_default_w = ui_sidebar_default_w_mini;
#else
	ui_sidebar_default_w = ui_sidebar_default_w_full;
#endif

	ui_sidebar_hminimized = ui_handle_create();
	gc_root(ui_sidebar_hminimized);
	ui_sidebar_w_mini = ui_sidebar_default_w_mini;

	console_last_traces = any_array_create(0);
	gc_root(console_last_traces);

	box_preferences_htab = ui_handle_create();
	gc_root(box_preferences_htab);

	ui_box_hwnd = ui_handle_create();
	gc_root(ui_box_hwnd);

	tab_layers_layer_name_handle = ui_handle_create();
	gc_root(tab_layers_layer_name_handle);

	tab_meshes_mesh_name_handle = ui_handle_create();
	gc_root(tab_meshes_mesh_name_handle);

	render_path_raytrace_f32a = f32_array_create(24);
	gc_root(render_path_raytrace_f32a);
	render_path_raytrace_help_mat = mat4_identity();

	neural_node_results = any_imap_create();
	gc_root(neural_node_results);

	sys_on_resize = base_on_resize;
	sys_on_w      = base_w;
	sys_on_h      = base_h;
	sys_on_x      = base_x;
	sys_on_y      = base_y;

	iron_set_app_name(manifest_title); // Used to locate external application data folder
	config_load();
	config_init();
	context_init();
	sys_start(config_get_options());

#ifdef is_debug
	double t_start = iron_time();
#endif

	if (g_config->layout == NULL) {
		config_init_layout();
	}
	iron_set_app_name(manifest_title);

	data_cached_scene_raws = any_map_create();
	gc_root(data_cached_scene_raws);
	any_map_set(data_cached_scene_raws, "Scene", startup_get_scene());
	scene_set_active("Scene");

	uniforms_ext_init();
	render_path_base_init();
	render_path_deferred_init(); // Allocate gbuffer
	if (g_config->render_mode == RENDER_MODE_FORWARD) {
		render_path_forward_init();
		render_path_commands = render_path_forward_commands;
		gc_root(render_path_commands);
	}
	else {

		render_path_commands = render_path_deferred_commands;
		gc_root(render_path_commands);
	}

#ifdef WITH_PLUGINS
	plugins_init();
#endif

	if (!string_equals(g_config->lut_path, "") && iron_file_exists(g_config->lut_path)) {
		import_lut_run(g_config->lut_path);
	}

	base_init();

#ifdef is_debug
	iron_log("Started in %fs\n", iron_time() - t_start);
#endif

	iron_start();
}
