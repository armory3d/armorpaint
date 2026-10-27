
#include "../global.h"

#ifdef WITH_PLUGINS
void export_glb_run(char *path, mesh_object_t_array_t *paint_objects);
#endif

void export_mesh_run(char *path, mesh_object_t_array_t *paint_objects) {
	if (paint_objects == NULL) {
		paint_objects = g_project->_->paint_objects;
	}
	if (g_context->export_mesh_format == MESH_FORMAT_OBJ) {
		if (g_config->tools == TOOLS_SCULPT) {
			export_obj_run_sculpt(path, paint_objects);
		}
		else {
			export_obj_run(path, paint_objects);
		}
	}
	else if (g_context->export_mesh_format == MESH_FORMAT_GLB) {
#ifdef WITH_PLUGINS
		export_glb_run(path, paint_objects);
#endif
	}
	else {
		export_arm_run_mesh(path, paint_objects);
	}
}
