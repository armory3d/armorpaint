// Based on https://github.com/Kode/kmake by RobDangerous

// amake: evaluates project.c files, exports assets and shaders and writes the project files for the target

#include "make.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

options_t goptions;
int       gargc;
char    **gargv;
char     *path_sep       = "/";
char     *other_path_sep = "\\";
char     *makedir;
char     *irondir;

static double now_ms(void) {
#ifdef _WIN32
	return (double)GetTickCount64();
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
#endif
}

static char *default_target(void) {
	if (strcmp(os_platform(), "linux") == 0) {
		return "linux";
	}
	else if (strcmp(os_platform(), "win32") == 0) {
		return "windows";
	}
	return "macos";
}

static project_t *export_amake_project(void) {
	printf("Creating %s project files.\n", goptions.target);
	project_t *project = load_project(".", true);
	if (strcmp(goptions.graphics, "metal") == 0) {
		any_array_push(project->includes, path_join("build", "sources", "*"));
	}
	project_search_files(project);
	project_internal_flatten(project);
	fs_ensuredir("build");
	export_solution(project);
	return project;
}

static any_array_t *args_list(char **args, int count) {
	any_array_t *list = list_create();
	for (int i = 0; i < count; ++i) {
		any_array_push(list, args[i]);
	}
	return list;
}

static void compile_project(int status, project_t *project) {
	if (status != 0) {
		exit(1);
	}
	char *exe = project->executable_name != NULL ? project->executable_name : project->safe_name;
	if (strcmp(goptions.target, "linux") == 0) {
		char *from = path_resolve(path_join("build", goptions.build_path), exe);
		char *to   = path_resolve(".", project->debugdir, exe);
		fs_copyfile(from, to);
		os_chmod_exec(to);
	}
	else if (strcmp(goptions.target, "windows") == 0) {
		// os_exec changed the working directory to build
		char *from = path_join("x64", goptions.debug ? "Debug" : "Release", string("%s.exe", exe));
		char *to   = path_resolve("..", project->debugdir, string("%s.exe", exe));
		fs_copyfile(from, to);
	}
	else if (strcmp(goptions.target, "wasm") == 0) {
		char *from = path_resolve(path_join("build", goptions.build_path), string("%s.wasm", exe));
		char *to   = path_resolve(".", project->debugdir, "start.wasm");
		fs_copyfile(from, to);
	}
	if (goptions.run) {
		if (strcmp(goptions.target, "macos") == 0) {
			char *app = string("build/%s/%s.app/Contents/MacOS/%s", goptions.debug ? "Debug" : "Release", project->name, project->name);
			os_exec(args_list((char *[]){app}, 1), "build", NULL);
		}
		else if (strcmp(goptions.target, "linux") == 0) {
			char *dir = path_resolve(".", project->debugdir);
			os_exec(args_list((char *[]){path_resolve(dir, exe)}, 1), dir, NULL);
		}
		else if (strcmp(goptions.target, "windows") == 0) {
			os_exec(args_list((char *[]){path_resolve("..", project->debugdir, exe)}, 1), path_resolve(".", project->debugdir), NULL);
		}
	}
}

static void run(void) {
	printf("Using Iron from %s\n", irondir);
	goptions.build_path = goptions.debug ? "Debug" : "Release";
	project_t *project  = export_amake_project();
	char      *name     = project->safe_name;
	if (!goptions.compile || name[0] == '\0') {
		return;
	}

	printf("Compiling...\n");
	char *target = goptions.target;
	int   status;
	if (strcmp(target, "linux") == 0 || strcmp(target, "wasm") == 0 || strcmp(goptions.ccompiler, "tcc") == 0) {
		char *cores = string("%d", os_cpus_length());
		status      = os_exec(args_list((char *[]){"make", "-j", cores}, 3), path_join("build", goptions.build_path), NULL);
	}
	else if (strcmp(target, "macos") == 0 || strcmp(target, "ios") == 0) {
		char *config = goptions.debug ? "Debug" : "Release";
		status       = os_exec(args_list((char *[]){"xcodebuild", "-configuration", config, "-project", string("%s.xcodeproj", name)}, 5), "build", NULL);
	}
	else if (strcmp(target, "windows") == 0) {
		char *program_files = os_env("ProgramFiles(x86)");
		char *vswhere       = path_join(program_files != NULL ? program_files : "", "Microsoft Visual Studio", "Installer", "vswhere.exe");
		char *vsvars        = "";
		os_exec(args_list((char *[]){vswhere, "-products", "*", "-latest", "-find", "VC\\Auxiliary\\Build\\vcvars64.bat"}, 6), NULL, &vsvars);
		fs_writefile(path_join("build", "build.bat"),
		             string("@call \"%s\"\n@MSBuild.exe \"%s\" /m /clp:ErrorsOnly /p:Configuration=%s,Platform=x64", str_trim(vsvars),
		                    path_resolve("build", string("%s.vcxproj", name)), goptions.debug ? "Debug" : "Release"));
		status = os_exec(args_list((char *[]){"build.bat"}, 1), "build", NULL);
	}
	else if (strcmp(target, "android") == 0) {
		bool         win      = strcmp(os_platform(), "win32") == 0;
		char        *assemble = string("assemble%s", goptions.debug ? "Debug" : "Release");
		any_array_t *args     = win ? args_list((char *[]){"gradlew.bat", assemble}, 2) : args_list((char *[]){"bash", "gradlew", assemble}, 3);
		status                = os_exec(args, path_join("build", name), NULL);
	}
	else {
		return;
	}
	compile_project(status, project);
}

static void parse_options(int argc, char **argv) {
	goptions.target    = default_target();
	goptions.graphics  = "default";
	goptions.ccompiler = "clang";
	goptions.arch      = "default";
	for (int i = 1; i < argc; ++i) {
		if (!starts_with(argv[i], "--")) {
			continue;
		}
		char *name  = argv[i] + 2;
		char *value = NULL;
		if (i < argc - 1 && !starts_with(argv[i + 1], "--")) {
			value = argv[++i];
		}
		if (strcmp(name, "target") == 0 && value != NULL) {
			goptions.target = value;
		}
		else if (strcmp(name, "graphics") == 0 && value != NULL) {
			goptions.graphics = value;
		}
		else if (strcmp(name, "ccompiler") == 0 && value != NULL) {
			goptions.ccompiler = value;
		}
		else if (strcmp(name, "arch") == 0 && value != NULL) {
			goptions.arch = value;
		}
		else if (strcmp(name, "compile") == 0) {
			goptions.compile = true;
		}
		else if (strcmp(name, "run") == 0) {
			goptions.run = true;
		}
		else if (strcmp(name, "debug") == 0) {
			goptions.debug = true;
		}
	}
	if (goptions.run) {
		goptions.compile = true;
	}
	if (strcmp(goptions.graphics, "default") == 0) {
		if (strcmp(goptions.target, "wasm") == 0) {
			goptions.graphics = "webgpu";
		}
		else if (strcmp(os_platform(), "win32") == 0) {
			goptions.graphics = "direct3d12";
		}
		else if (strcmp(os_platform(), "darwin") == 0) {
			goptions.graphics = "metal";
		}
		else {
			goptions.graphics = "vulkan";
		}
	}
}

int main(int argc, char **argv) {
	gargc = argc;
	gargv = argv;
	if (strcmp(os_platform(), "win32") == 0) {
		path_sep       = "\\";
		other_path_sep = "/";
	}

	// The binary lives in base/tools/bin/<platform>
	char *binpath  = path_resolve(argv[0]);
	char *toolsdir = js_substring(binpath, 0, str_last_index_of(binpath, path_sep));
	makedir        = path_join(toolsdir, "..", "..");
	irondir        = path_join(makedir, "..");

	// amake --ashader <spirv|metal|hlsl|wgsl> <from> <to>
	if (argc > 1 && strcmp(argv[1], "--ashader") == 0) {
		if (argc < 5) {
			printf("Usage: amake --ashader <spirv|metal|hlsl|wgsl> <from> <to>\n");
			return 1;
		}
		return ashader(argv[2], argv[3], argv[4]);
	}

	// amake --c <file.c> [args], runs a C file with minic
	if (argc > 1 && strcmp(argv[1], "--c") == 0) {
		if (argc < 3) {
			printf("Usage: amake --c <file.c> [args]\n");
			return 1;
		}
		return run_script(argv[2], argc - 3, argv + 3);
	}

	parse_options(argc, argv);
	double start = now_ms();
	run();
	printf("Done in %dms.\n", (int)(now_ms() - start));
	return 0;
}
