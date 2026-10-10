#include "../../base/tools/amake/amake.h"

project_t *main() {
	project_t *project = project_create("plugins");
	add_cfiles(project, "plugins.c");
	add_cfiles(project, "io_svg/**");
	add_cfiles(project, "io_exr/**");
	add_cfiles(project, "io_psd/**");
	add_cfiles(project, "io_gltf/**");
	add_cfiles(project, "io_fbx/**");
	add_cfiles(project, "io_tiff/**");

	if (fs_exists("external")) {
		add_cfiles(project, "external/**");
		add_shaders(project, "external/*.shader");
		add_assets(project, "external/assets/*", "data/{name}", 0);
		add_define(project, "WITH_EXTERNAL");
	}

	return project;
}
