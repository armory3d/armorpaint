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
	add_cfiles(project, "bake_maps/**");
	
	add_define(project, "UFBX_MINIMAL");
	add_define(project, "UFBX_ENABLE_TRIANGULATION");
	add_define(project, "UFBX_ENABLE_SCENE_EVALUATION");

	return project;
}
