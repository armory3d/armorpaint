// ArmorPad code editor
// ../../make --run

#include "../amake/amake.h"

project_t *main() {
	flags->name       = "ArmorPad";
	flags->package    = "org.armorpad";
	flags->idle_sleep = !has_arg("--debug");

	project_t *project = project_create(flags->name);
	add_project(project, "../../");
	add_cfiles(project, "sources/main.c");
	return project;
}
