#include "../base/tools/amake/amake.h"

project_t *main() {
	flags->name                = "ArmorPaint";
	flags->package             = "org.armorpaint";
	flags->embed               = has_arg("--embed"); // !has_arg("--debug"); // clang 19
	flags->with_physics        = true;
	flags->with_d3dcompiler    = true;
	flags->with_nfd            = true;
	flags->with_compress       = platform != PLATFORM_ANDROID;
	flags->with_image_write    = true;
	flags->with_video_write    = !has_arg("tcc");
	flags->with_eval           = true;
	flags->with_plugins        = true;
	flags->with_kong           = true;
	flags->with_raytrace       = true;
	flags->with_bc7            = true;
	flags->with_audio          = platform == PLATFORM_LINUX || platform == PLATFORM_WINDOWS;
	flags->idle_sleep          = true;
	flags->export_version_info = true;
	flags->export_data_list    = platform == PLATFORM_ANDROID; // .apk contents

	if (platform == PLATFORM_WASM) {
		flags->with_nfd         = false;
		flags->with_compress    = false;
		flags->with_plugins     = false;
		flags->with_raytrace    = false;
		flags->export_data_list = true;
	}

	project_t *project = project_create(flags->name);
	add_project(project, "../base");
	add_cfiles(project, "sources/startup.c");
	add_cfiles(project, "sources/minic_api.c");
	add_cfiles(project, "sources/main.c");
	add_shaders(project, "shaders/*.shader");
	add_assets(project, "assets/*", "data/{name}", 0);
	add_assets(project, "assets/export_presets/*", "data/export_presets/{name}", 0);
	add_assets(project, "assets/keymap_presets/*", "data/keymap_presets/{name}", 0);
	add_assets(project, "assets/licenses/**", "data/licenses/{name}", 0);
	add_assets(project, "assets/plugins/*", "data/plugins/{name}", 0);
	add_assets(project, "assets/meshes/*", "data/meshes/{name}", ASSET_NOEMBED);
	add_assets(project, "assets/meshes/default/*", "data/meshes/{name}", 0); // Embed default mesh
	add_assets(project, "assets/locale/*", "data/locale/{name}", 0);
	add_assets(project, "assets/readme/readme.txt", "{name}", 0);
	return project;
}
