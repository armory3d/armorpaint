#include "tools/amake/amake.h"

project_t *main() {
	project_t *project = project_create("Base");
	add_include_dir(project, "sources");
	add_include_dir(project, "sources/libs");
	add_shaders(project, "shaders/*.shader");
	add_assets(project, "assets/*", "data/{name}", 0);
	add_assets(project, "assets/licenses/**", "data/licenses/{name}", 0);
	add_assets(project, "assets/themes/*.json", "data/themes/{name}", 0);
	add_cfiles(project, "sources/*.c");
	add_cfiles(project, "sources/kong/dir.c");
	add_define(project, string("EMBED_H_PATH=\"%s/build/embed.h\"", flags->dirname));

	if (platform == PLATFORM_WINDOWS) {
		add_cfiles(project, "sources/backends/windows_system.*");
		add_cfiles(project, "sources/backends/windows_net.*");
		add_cfiles(project, "sources/backends/windows_thread.*");
		add_cfiles(project, "sources/backends/vulkan_gpu.*");
		add_define(project, "_CRT_SECURE_NO_WARNINGS");
		add_define(project, "_WINSOCK_DEPRECATED_NO_WARNINGS");
		add_define(project, "IRON_VULKAN");
		add_define(project, "VK_USE_PLATFORM_WIN32_KHR");
		add_include_dir(project, "$(VULKAN_SDK)/Include"); // https://vulkan.lunarg.com/sdk/home
		add_lib(project, "$(VULKAN_SDK)/Lib/vulkan-1");
		add_lib(project, "Winhttp");
		add_lib(project, "Dwmapi"); // DWMWA_USE_IMMERSIVE_DARK_MODE
		if (flags->with_audio) {
			add_lib(project, "dsound");
			add_cfiles(project, "sources/backends/windows_audio.*");
		}
		if (flags->with_gamepad) {
			add_lib(project, "dinput8");
		}
	}
	else if (platform == PLATFORM_LINUX) {
		bool with_wayland = has_arg("--wayland");
		if (with_wayland) {
			add_define(project, "WITH_WAYLAND");
			add_cfiles(project, "sources/backends/wayland_system.c");
			char *dir        = string("%s/build/wayland", flags->dirname);
			char *xdg_shell  = "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml";
			char *viewporter = "/usr/share/wayland-protocols/stable/viewporter/viewporter.xml";
			char *fractional = "/usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml";
			char *decoration = "/usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml";
			char *tablet     = "/usr/share/wayland-protocols/stable/tablet/tablet-v2.xml";
			char *output     = "/usr/share/wayland-protocols/unstable/xdg-output/xdg-output-unstable-v1.xml";
			fs_ensuredir(dir);
			fs_writefile(string("%s/xdg-shell.h", dir), os_popen(string("wayland-scanner client-header %s /dev/stdout", xdg_shell)));
			fs_writefile(string("%s/xdg-shell.c", dir), os_popen(string("wayland-scanner private-code %s /dev/stdout", xdg_shell)));
			fs_writefile(string("%s/viewporter.h", dir), os_popen(string("wayland-scanner client-header %s /dev/stdout", viewporter)));
			fs_writefile(string("%s/viewporter.c", dir), os_popen(string("wayland-scanner private-code %s /dev/stdout", viewporter)));
			fs_writefile(string("%s/fractional-scale.h", dir), os_popen(string("wayland-scanner client-header %s /dev/stdout", fractional)));
			fs_writefile(string("%s/fractional-scale.c", dir), os_popen(string("wayland-scanner private-code %s /dev/stdout", fractional)));
			fs_writefile(string("%s/xdg-decoration.h", dir), os_popen(string("wayland-scanner client-header %s /dev/stdout", decoration)));
			fs_writefile(string("%s/xdg-decoration.c", dir), os_popen(string("wayland-scanner private-code %s /dev/stdout", decoration)));
			fs_writefile(string("%s/tablet.h", dir), os_popen(string("wayland-scanner client-header %s /dev/stdout", tablet)));
			fs_writefile(string("%s/tablet.c", dir), os_popen(string("wayland-scanner private-code %s /dev/stdout", tablet)));
			fs_writefile(string("%s/xdg-output.h", dir), os_popen(string("wayland-scanner client-header %s /dev/stdout", output)));
			fs_writefile(string("%s/xdg-output.c", dir), os_popen(string("wayland-scanner private-code %s /dev/stdout", output)));
			add_include_dir(project, dir);
			add_cfiles(project, string("%s/**", dir));
			add_lib(project, "wayland-client");
			add_lib(project, "wayland-cursor");
			add_lib(project, "xkbcommon");
		}
		else {
			add_cfiles(project, "sources/backends/linux_system.*");
			add_lib(project, "X11");
			add_lib(project, "Xi");
			add_lib(project, "Xcursor");
			add_lib(project, "Xrandr");
		}
		add_cfiles(project, "sources/backends/posix_net.*");
		add_cfiles(project, "sources/backends/posix_thread.*");
		add_cfiles(project, "sources/backends/vulkan_gpu.*");
		add_define(project, "IRON_VULKAN");
		add_define(project, "_POSIX_C_SOURCE=200809L");
		add_lib(project, "ssl");
		add_lib(project, "crypto");
		add_lib(project, "vulkan");
		if (flags->with_audio) {
			add_lib(project, "asound");
			add_cfiles(project, "sources/backends/linux_audio.*");
		}
		if (flags->with_gamepad) {
			add_lib(project, "udev");
		}
	}
	else if (platform == PLATFORM_MACOS) {
		add_cfiles(project, "sources/backends/macos_system.*");
		add_cfiles(project, "sources/backends/apple_net.*");
		if (flags->with_audio) {
			add_cfiles(project, "sources/backends/macos_audio.c");
		}
		add_cfiles(project, "sources/backends/apple_thread.*");
		add_cfiles(project, "sources/backends/metal_gpu.*");
		add_cfiles(project, "sources/backends/data/mac.plist");
		add_define(project, "IRON_METAL");
	}
	else if (platform == PLATFORM_IOS) {
		add_cfiles(project, "sources/backends/ios_system.*");
		add_cfiles(project, "sources/backends/apple_net.*");
		add_cfiles(project, "sources/backends/apple_thread.*");
		add_cfiles(project, "sources/backends/metal_gpu.*");
		add_cfiles(project, "sources/backends/data/ios.plist");
		add_cfiles(project, "sources/backends/ios_file_dialog.m");
		if (flags->with_audio) {
			add_cfiles(project, "sources/backends/ios_audio.m");
			add_lib(project, "AudioToolbox");
			add_lib(project, "AVFoundation");
		}
		add_define(project, "IRON_METAL");
		project->ios.build   = string("%d", version_code());
		project->ios.version = string("%d", version_code());
	}
	else if (platform == PLATFORM_ANDROID) {
		add_cfiles(project, "sources/backends/android_system.*");
		add_cfiles(project, "sources/backends/posix_thread.*");
		add_cfiles(project, "sources/backends/vulkan_gpu.*");
		add_cfiles(project, "sources/backends/android_file_dialog.c");
		add_cfiles(project, "sources/backends/android_net.c");
		add_cfiles(project, "sources/backends/android_native_app_glue.c");
		add_define(project, "IRON_ANDROID");
		add_define(project, "IRON_VULKAN");
		add_define(project, "VK_USE_PLATFORM_ANDROID_KHR");
		add_lib(project, "vulkan");
		add_lib(project, "log");
		add_lib(project, "android");
		if (flags->with_audio) {
			add_lib(project, "OpenSLES");
			add_cfiles(project, "sources/backends/android_audio.c");
		}
		project->android.package      = flags->package;
		project->android.permissions  = "android.permission.INTERNET";
		project->android.version_code = version_code();
		project->android.version_name = string("%d", version_code());
	}
	else if (platform == PLATFORM_WASM) {
		add_cfiles(project, "sources/backends/wasm_system.*");
		add_cfiles(project, "sources/backends/wasm_thread.*");
		if (flags->with_audio) {
			add_cfiles(project, "sources/backends/wasm_audio.*");
		}
		add_cfiles(project, "sources/backends/webgpu_gpu.*");
		add_define(project, "IRON_WASM");
		add_define(project, "IRON_WEBGPU");
		add_cfiles(project, "sources/miniclib/**");
		add_include_dir(project, "sources/miniclib");
		add_assets(project, "sources/backends/data/wasm/*", NULL, 0);
	}

	if (flags->with_kong) {
		add_define(project, "WITH_KONG");
		add_cfiles(project, "sources/kong/dir.c");
		add_cfiles(project, "sources/kong/kong.c");
		add_cfiles(project, "sources/kong/kong_cstyle.c");
		add_cfiles(project, "sources/kong/stb_ds.c");
		add_cfiles(project, "sources/kong/kong_spirv.c");
		add_cfiles(project, "sources/kong/kong_spirv_opt.c");
		add_cfiles(project, "sources/kong/kong_wgsl.c");
		if (platform == PLATFORM_MACOS || platform == PLATFORM_IOS) {
			add_cfiles(project, "sources/kong/kong_metal.c");
		}
	}

	if (flags->with_plugins) {
		add_define(project, "WITH_PLUGINS");
		add_project(project, string("%s/plugins", flags->dirname));
		add_cfiles(project, "sources/plugins/plugin_api.c");
	}

	if (flags->embed) {
		add_define(project, "WITH_EMBED");
		add_define(project, "arm_embed");
	}

	if (flags->with_physics) {
		add_define(project, "WITH_PHYSICS");
		add_shader(project, "shaders/compute/gpu_sim.shader", "gpu_sim", graphics == GRAPHICS_WEBGPU ? "_QUEUE_AHEAD" : "");
	}

	if (flags->with_raytrace) {
		add_define(project, "WITH_RAYTRACE");
		add_assets(project, "assets/raytrace/*", "data/{name}", 0);
		char *low = graphics == GRAPHICS_VULKAN ? "" : "_LOW_SAMPLES";
		add_shader(project, "shaders/raytrace/raytrace_brute.shader", "raytrace_brute_core", low);
		add_shader(project, "shaders/raytrace/raytrace_brute.shader", "raytrace_brute_full", string("%s _FULL", low));
		add_shader(project, "shaders/raytrace/raytrace_bake_ao.shader", "raytrace_bake_ao", low);
		add_shader(project, "shaders/raytrace/raytrace_bake_light.shader", "raytrace_bake_light", low);
		add_shader(project, "shaders/raytrace/raytrace_bake_bent.shader", "raytrace_bake_bent", low);
		add_shader(project, "shaders/raytrace/raytrace_bake_thick.shader", "raytrace_bake_thick", low);
	}

	if (flags->export_version_info) {
		char *dir  = string("%s/build", flags->dirname);
		char *sha  = os_popen("git log --pretty=format:\"%h\" -n 1");
		char *data = string("{ \"sha\": \"%.7s\", \"date\": \"%s\" }", sha, date_string());
		fs_ensuredir(dir);
		fs_writefile(string("%s/version.json", dir), data);
		// Adds version.json to embed.txt list
		add_assets(project, string("%s/version.json", dir), "data/{name}", 0);
	}

	if (flags->export_data_list) {
		char *root   = flags->dirname;
		char *dir    = string("%s/build", root);
		char *format = "{\"/data/plugins\":\"%s\",\"/data/export_presets\":\"%s\",\"/data/keymap_presets\":\"%s\",\"/data/locale\":\"%s\",\"/data/"
		               "meshes\":\"%s\",\"/data/themes\":\"%s\"}";
		char *data   = string(format, fs_list_files(string("%s/assets/plugins", root)), fs_list_files(string("%s/assets/export_presets", root)),
		                      fs_list_files(string("%s/assets/keymap_presets", root)), fs_list_files(string("%s/assets/locale", root)),
		                      fs_list_files(string("%s/assets/meshes", root)), fs_list_files("assets/themes"));
		fs_ensuredir(dir);
		fs_writefile(string("%s/data_list.json", dir), data);
		add_assets(project, string("%s/data_list.json", dir), "data/{name}", 0);
	}

	if (flags->with_audio) {
		add_define(project, "IRON_AUDIO");
		add_cfiles(project, "sources/libs/stb_vorbis.c");
	}

	if (flags->with_eval) {
		add_define(project, "WITH_EVAL");
		add_cfiles(project, "sources/libs/minic.c");
		add_cfiles(project, "sources/libs/minic_tests.c");
	}

	if (flags->with_bc7) {
		add_define(project, "WITH_BC7");
		add_cfiles(project, "sources/libs/bc7enc.c");
		if (graphics != GRAPHICS_WEBGPU) {
			add_shader(project, "shaders/compute/gpu_bc7.shader", "gpu_bc7", "");
		}
	}

	if (flags->with_gamepad) {
		add_define(project, "WITH_GAMEPAD");
	}

	bool with_websocket = platform == PLATFORM_LINUX || platform == PLATFORM_WINDOWS || platform == PLATFORM_MACOS;
	if (with_websocket) {
		add_define(project, "WITH_WEBSOCKET");
		if (platform == PLATFORM_WINDOWS) {
			add_cfiles(project, "sources/backends/windows_websocket.c");
		}
		else if (platform == PLATFORM_LINUX) {
			add_cfiles(project, "sources/backends/posix_websocket.c");
		}
		else if (platform == PLATFORM_MACOS) {
			add_cfiles(project, "sources/backends/macos_websocket.m");
		}
	}

	if (flags->idle_sleep) {
		add_define(project, "IDLE_SLEEP");
	}

	char *root = flags->dirname;
	if (fs_exists(string("%s/icon.png", root))) {
		project->icon = "icon.png";
		if (platform == PLATFORM_MACOS && fs_exists(string("%s/icon_macos.png", root))) {
			project->icon = "icon_macos.png";
		}
		else if (platform == PLATFORM_IOS && fs_exists(string("%s/icon_ios.png", root))) {
			project->icon = "icon_ios.png";
		}
		else if (platform == PLATFORM_LINUX) {
			add_assets(project, string("%s/icon.png", root), "{name}", ASSET_NOPROCESSING | ASSET_NOEMBED);
		}
	}

	if (flags->with_nfd && (platform == PLATFORM_WINDOWS || platform == PLATFORM_LINUX || platform == PLATFORM_MACOS)) {
		add_define(project, "WITH_NFD");
		add_cfiles(project, "sources/libs/nfd.c");
		if (platform == PLATFORM_LINUX) {
			add_include_dir(project, "/usr/include/gtk-3.0");
			add_include_dir(project, "/usr/include/glib-2.0");
			add_include_dir(project, "/usr/lib/x86_64-linux-gnu/glib-2.0/include");
			add_include_dir(project, "/usr/include/pango-1.0");
			add_include_dir(project, "/usr/include/cairo");
			add_include_dir(project, "/usr/include/gdk-pixbuf-2.0");
			add_include_dir(project, "/usr/include/atk-1.0");
			add_include_dir(project, "/usr/lib64/glib-2.0/include");
			add_include_dir(project, "/usr/lib/glib-2.0/include");
			add_include_dir(project, "/usr/include/harfbuzz");
			add_lib(project, "dl"); // gtk-3 is loaded at runtime, see nfd_gtk.h
		}
		else if (platform == PLATFORM_MACOS) {
			add_cfiles(project, "sources/libs/nfd.m");
		}
	}

	if (flags->with_compress) {
		add_define(project, "WITH_COMPRESS");
		add_cfiles(project, "sources/libs/untar.c");
	}

	if (flags->with_image_write) {
		add_define(project, "WITH_IMAGE_WRITE");
	}

	if (flags->with_video_write) {
		add_define(project, "WITH_VIDEO_WRITE");
	}

	project_flatten(project);
	return project;
}
