#include "../../tools/amake/amake.h"

project_t *main() {
	project_t *project = project_create("test");
	add_project(project, "../../");
	add_cfiles(project, "main.c");
	add_shaders(project, "./*.shader");
	return project;
}
