
let flags                 = globalThis.flags;
flags.name                = "ArmorPaint";
flags.package             = "org.armorpaint";
flags.embed               = os_argv().indexOf("--embed") >= 0; // os_argv().indexOf("--debug") == -1; // clang 19
flags.with_physics        = true;
flags.with_d3dcompiler    = true;
flags.with_nfd            = true;
flags.with_compress       = platform != "android";
flags.with_image_write    = true;
flags.with_video_write    = os_argv().indexOf("tcc") == -1;
flags.with_eval           = true;
flags.with_plugins        = true;
flags.with_kong           = true;
flags.with_raytrace       = true;
flags.with_bc7            = true;
flags.with_audio          = platform == "linux" || platform == "windows";
flags.idle_sleep          = true;
flags.export_version_info = true;
flags.export_data_list    = platform == "android"; // .apk contents

if (platform == "wasm") {
	flags.with_nfd         = false;
	flags.with_compress    = false;
	flags.with_plugins     = false;
	flags.with_raytrace    = false;
	flags.export_data_list = true;
}

let project = new Project(flags.name);
project.add_project("../base");
project.add_cfiles("sources/startup.c");
project.add_cfiles("sources/minic_api.c");
project.add_cfiles("sources/main.c");
project.add_shaders("shaders/*.kong");
project.add_assets("assets/*", {destination : "data/{name}"});
project.add_assets("assets/export_presets/*", {destination : "data/export_presets/{name}"});
project.add_assets("assets/keymap_presets/*", {destination : "data/keymap_presets/{name}"});
project.add_assets("assets/licenses/**", {destination : "data/licenses/{name}"});
project.add_assets("assets/plugins/*", {destination : "data/plugins/{name}"});
project.add_assets("assets/meshes/*", {destination : "data/meshes/{name}", noembed : true});
project.add_assets("assets/meshes/default/*", {destination : "data/meshes/{name}"}); // embed default mesh
project.add_assets("assets/locale/*", {destination : "data/locale/{name}"});
project.add_assets("assets/readme/readme.txt", {destination : "{name}"});
return project;
