#include "amake.h"

project_t *main() {
	project_t *project = project_create("amake");
	add_define(project, "AMAKE");

	add_include_dir(project, "../../sources");
	add_cfiles(project, "../../sources/iron_string.c");
	add_cfiles(project, "../../sources/iron_array.c");
	add_cfiles(project, "../../sources/libs/minic.c");

	add_cfiles(project, "main.c");
	add_cfiles(project, "make.c");
	add_cfiles(project, "exporters.c");
	add_cfiles(project, "script.c");
	add_cfiles(project, "util.c");
	add_cfiles(project, "aimage.c");
	add_cfiles(project, "ashader.c");

	add_cfiles(project, "../../sources/kong/dir.c");
	add_cfiles(project, "../../sources/kong/kong.c");
	add_cfiles(project, "../../sources/kong/kong_cstyle.c");
	add_cfiles(project, "../../sources/kong/stb_ds.c");
	add_cfiles(project, "../../sources/kong/kong_spirv.c");
	add_cfiles(project, "../../sources/kong/kong_wgsl.c");
	if (platform == PLATFORM_WINDOWS) {
		add_cfiles(project, "../../sources/kong/kong_hlsl.c");
	}
	if (platform == PLATFORM_MACOS || platform == PLATFORM_IOS) {
		add_cfiles(project, "../../sources/kong/kong_metal.c");
	}

	if (platform == PLATFORM_WINDOWS) {
		add_define(project, "_CRT_SECURE_NO_WARNINGS");
		add_lib(project, "d3dcompiler");
		add_lib(project, "dxguid");
	}
	else if (platform == PLATFORM_MACOS) {
		add_cfiles(project, "../../sources/backends/data/mac.plist");
	}
	else if (platform == PLATFORM_LINUX) {
		add_lib(project, "dl -static");
	}

	return project;
}
