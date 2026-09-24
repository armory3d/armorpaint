#include "../../tools/amake/amake.h"

project_t *main() {
	project_t *project = project_create("test");
	add_project(project, "../../");
	add_cfiles(project, "sources/main.c");
	add_shaders(project, "shaders/*.shader");
	add_assets(project, "assets/*", "data/{name}", 0);
	return project;
}
