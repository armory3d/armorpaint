// Project file exporters

#include "make.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char    *out_path = NULL;
static buffer_t out      = {0};

static void write_file(char *path) {
	out_path = path;
	if (out.buffer == NULL) {
		string_buffer_init(&out);
	}
	string_buffer_reset(&out);
}

static void close_file(void) {
	fs_writefile(out_path, string_buffer_get(&out));
	string_buffer_reset(&out);
}

// Writes an indented line, data has to go through the format arguments
static void p(int indent, const char *fmt, ...) {
	for (int i = 0; i < indent; ++i) {
		string_buffer_append(&out, "\t");
	}
	va_list args;
	va_start(args, fmt);
	va_list args_copy;
	va_copy(args_copy, args);
	int len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);
	char *line = malloc(len + 1);
	vsnprintf(line, len + 1, fmt, args);
	va_end(args);
	string_buffer_append(&out, line);
	string_buffer_append(&out, "\n");
	free(line);
}

static bool is_source(char *file) {
	return ends_with(file, ".c");
}

// Unique object file names for sources, parallel to project->files, NULL for non-sources
static any_array_t *object_names(project_t *project) {
	any_array_t *names = list_create();
	any_array_t *used  = list_create();
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (!is_source(file)) {
			any_array_push(names, NULL);
			continue;
		}
		char *name = str_lower(file);
		if (str_index_of(name, "/") >= 0) {
			name = js_substr(name, str_last_index_of(name, "/") + 1, (int)strlen(name));
		}
		name = js_substr(name, 0, str_last_index_of(name, "."));
		while (list_contains(used, name)) {
			name = string("%s_", name);
		}
		any_array_push(used, name);
		any_array_push(names, name);
	}
	return names;
}

static char *executable_name(project_t *project) {
	return project->executable_name != NULL ? project->executable_name : project->safe_name;
}

// ███╗   ███╗ █████╗ ██╗  ██╗███████╗
// ████╗ ████║██╔══██╗██║ ██╔╝██╔════╝
// ██╔████╔██║███████║█████╔╝ █████╗
// ██║╚██╔╝██║██╔══██║██╔═██╗ ██╔══╝
// ██║ ╚═╝ ██║██║  ██║██║  ██╗███████╗
// ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝

static void make_export(project_t *project, char *ccompiler, char *cflags, char *linker_flags, char *output_ext) {
	if (strcmp(ccompiler, "tcc") == 0) {
		char *tccdir = string("%s/tcc", makedir);
		linker_flags = "-lc -lm -pthread";
		ccompiler =
		    string("%s/bin/%s/tcc -DSTBI_NO_SIMD -DSINFL_NO_SIMD -DIRON_NOSIMD -w -I%s/include -B%s/bin/%s", tccdir, sys_dir(), tccdir, tccdir, sys_dir());
	}
	cflags = string("%s -Wno-incompatible-pointer-types", cflags);

	char *from        = path_resolve(".");
	char *to          = path_resolve("build");
	char *output_path = path_resolve(to, goptions.build_path);
	fs_ensuredir(output_path);
	any_array_t *names     = object_names(project);
	buffer_t     ofilelist = {0};
	string_buffer_init(&ofilelist);
	for (int i = 0; i < names->length; ++i) {
		if (names->buffer[i] != NULL) {
			string_buffer_append(&ofilelist, string("%s.o ", names->buffer[i]));
		}
	}
	char *ofiles = string_buffer_get(&ofilelist);

	write_file(path_resolve(output_path, "makefile"));
	buffer_t line = {0};
	string_buffer_init(&line);
	string_buffer_append(&line, "-I./ "); // Local directory to pick up the precompiled headers
	for (int i = 0; i < project->includedirs->length; ++i) {
		string_buffer_append(&line, string("-I%s ", path_relative(output_path, path_resolve(from, project->includedirs->buffer[i]))));
	}
	p(0, "INC=%s", string_buffer_get(&line));

	string_buffer_reset(&line);
	string_buffer_append(&line, linker_flags);
	for (int i = 0; i < project->libs->length; ++i) {
		string_buffer_append(&line, string(" -l%s", project->libs->buffer[i]));
	}
	p(0, "LIB=%s", string_buffer_get(&line));

	string_buffer_reset(&line);
	for (int i = 0; i < project->defines->length; ++i) {
		string_buffer_append(&line, string("-D%s ", str_replace(project->defines->buffer[i], "\"", "\\\"")));
	}
	if (!goptions.debug) {
		string_buffer_append(&line, "-DNDEBUG ");
	}
	p(0, "DEF=%s", string_buffer_get(&line));
	p(0, "");

	string_buffer_reset(&line);
	string_buffer_append(&line, string("%s -std=%s ", cflags, project->c_std));
	for (int i = 0; i < project->cflags->length; ++i) {
		string_buffer_append(&line, string("%s ", project->cflags->buffer[i]));
	}
	p(0, "CFLAGS=%s", string_buffer_get(&line));

	char *optimization = goptions.debug ? "-g" : "-O2";
	char *exe          = executable_name(project);
	p(0, "%s%s: %s", exe, output_ext, ofiles);
	p(0, "\t%s -o \"%s%s\" %s %s $(LIB)", ccompiler, exe, output_ext, optimization, ofiles);
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (!is_source(file)) {
			continue;
		}
		p(0, "");
		char *name     = names->buffer[i];
		char *realfile = path_relative(output_path, path_resolve(from, file));
		p(0, "-include %s.d", name);
		p(0, "%s.o: %s", name, realfile);
		p(0, "\t%s %s $(INC) $(DEF) -MD $(CFLAGS) -c %s -o %s.o", ccompiler, optimization, realfile, name);
	}
	close_file();
}

//  ██████╗ ██████╗ ███╗   ███╗██████╗ ██╗██╗     ███████╗     ██████╗ ██████╗ ███╗   ███╗███╗   ███╗ █████╗ ███╗   ██╗██████╗ ███████╗
// ██╔════╝██╔═══██╗████╗ ████║██╔══██╗██║██║     ██╔════╝    ██╔════╝██╔═══██╗████╗ ████║████╗ ████║██╔══██╗████╗  ██║██╔══██╗██╔════╝
// ██║     ██║   ██║██╔████╔██║██████╔╝██║██║     █████╗      ██║     ██║   ██║██╔████╔██║██╔████╔██║███████║██╔██╗ ██║██║  ██║███████╗
// ██║     ██║   ██║██║╚██╔╝██║██╔═══╝ ██║██║     ██╔══╝      ██║     ██║   ██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║██║╚██╗██║██║  ██║╚════██║
// ╚██████╗╚██████╔╝██║ ╚═╝ ██║██║     ██║███████╗███████╗    ╚██████╗╚██████╔╝██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║██║ ╚████║██████╔╝███████║
//  ╚═════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝     ╚═╝╚══════╝╚══════╝     ╚═════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚══════╝

static char *ndk_from_sdk_root(void) {
	char *sdk = os_env("ANDROID_HOME");
	if (sdk == NULL) {
		sdk = os_env("ANDROID_SDK_ROOT");
	}
	if (sdk == NULL || sdk[0] == '\0') {
		return NULL;
	}
	char *ndk_dir = path_join(sdk, "ndk");
	if (!fs_exists(ndk_dir)) {
		return NULL;
	}
	any_array_t *ndks = fs_readdir(ndk_dir);
	for (int i = 0; i < ndks->length; ++i) {
		if (((char *)ndks->buffer[i])[0] != '.') {
			return path_join(ndk_dir, ndks->buffer[i]);
		}
	}
	return NULL;
}

static void json_string_list(buffer_t *sb, any_array_t *list) {
	string_buffer_append(sb, "[");
	for (int i = 0; i < list->length; ++i) {
		string_buffer_append(sb, string("%s\"%s\"", i > 0 ? "," : "", json_escape(list->buffer[i])));
	}
	string_buffer_append(sb, "]");
}

static void compile_commands_export(project_t *project) {
	char *from = path_resolve(os_cwd(), path_resolve("."));
	char *to   = path_resolve("build");
	write_file(path_resolve(to, "compile_commands.json"));

	any_array_t *includes = list_create();
	for (int i = 0; i < project->includedirs->length; ++i) {
		any_array_push(includes, "-I");
		any_array_push(includes, path_resolve(from, project->includedirs->buffer[i]));
	}
	any_array_t *defines = list_create();
	for (int i = 0; i < project->defines->length; ++i) {
		any_array_push(defines, "-D");
		any_array_push(defines, str_replace(project->defines->buffer[i], "\"", "\\\""));
	}
	any_array_t *names = object_names(project);

	any_array_t *default_args = list_create();
	if (strcmp(goptions.target, "android") == 0) {
		any_array_push(default_args, "--target=aarch64-none-linux-android21");
		any_array_push(default_args, "-DANDROID");
		char *android_ndk = os_env("ANDROID_NDK");
		if (android_ndk == NULL) {
			android_ndk = ndk_from_sdk_root();
		}
		if (android_ndk != NULL && android_ndk[0] != '\0') {
			char *host_tag = "";
			if (strcmp(os_platform(), "linux") == 0) {
				host_tag = "linux-x86_64";
			}
			else if (strcmp(os_platform(), "darwin") == 0) {
				host_tag = "darwin-x86_64";
			}
			else if (strcmp(os_platform(), "win32") == 0) {
				host_tag = "windows-x86_64";
			}
			char *ndk_toolchain = path_join(android_ndk, string("toolchains/llvm/prebuilt/%s", host_tag));
			if (host_tag[0] != '\0' && fs_exists(ndk_toolchain)) {
				any_array_push(default_args, string("--gcc-toolchain=%s", ndk_toolchain));
				any_array_push(default_args, string("--sysroot=%s/sysroot", ndk_toolchain));
			}
			else {
				// Fallback to the first found toolchain
				any_array_t *toolchains = fs_readdir(path_join(android_ndk, "toolchains/llvm/prebuilt/"));
				if (toolchains->length > 0) {
					ndk_toolchain = path_join(android_ndk, string("toolchains/llvm/prebuilt/%s", toolchains->buffer[0]));
					any_array_push(default_args, string("--gcc-toolchain=%s", ndk_toolchain));
					any_array_push(default_args, string("--sysroot=%s/sysroot", ndk_toolchain));
					printf("Found android ndk toolchain in %s.\n", ndk_toolchain);
				}
			}
		}
	}

	buffer_t sb = {0};
	string_buffer_init(&sb);
	string_buffer_append(&sb, "[");
	bool first = true;
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (!is_source(file)) {
			continue;
		}
		any_array_t *args = list_create();
		any_array_push(args, "/usr/bin/clang");
		any_array_push(args, "-c");
		any_array_push(args, "-o");
		any_array_push(args, string("%s%s.o", goptions.debug ? "Debug" : "Release", names->buffer[i]));
		if (ends_with(file, ".c")) {
			any_array_push(args, "-std=c99");
		}
		for (int j = 0; j < default_args->length; ++j) {
			any_array_push(args, default_args->buffer[j]);
		}
		any_array_push(args, path_resolve(from, file));
		for (int j = 0; j < includes->length; ++j) {
			any_array_push(args, includes->buffer[j]);
		}
		for (int j = 0; j < defines->length; ++j) {
			any_array_push(args, defines->buffer[j]);
		}
		string_buffer_append(&sb, first ? "{" : ",{");
		first = false;
		string_buffer_append(&sb, string("\"directory\":\"%s\",", json_escape(from)));
		string_buffer_append(&sb, string("\"file\":\"%s\",", json_escape(path_resolve(from, file))));
		string_buffer_append(&sb, string("\"output\":\"%s\",", json_escape(path_resolve(to, string("%s.o", names->buffer[i])))));
		string_buffer_append(&sb, "\"arguments\":");
		json_string_list(&sb, args);
		string_buffer_append(&sb, "}");
	}
	string_buffer_append(&sb, "]");
	p(0, "%s", string_buffer_get(&sb));
	close_file();
}

// ██╗   ██╗██╗███████╗██╗   ██╗ █████╗ ██╗         ███████╗████████╗██╗   ██╗██████╗ ██╗ ██████╗
// ██║   ██║██║██╔════╝██║   ██║██╔══██╗██║         ██╔════╝╚══██╔══╝██║   ██║██╔══██╗██║██╔═══██╗
// ██║   ██║██║███████╗██║   ██║███████║██║         ███████╗   ██║   ██║   ██║██║  ██║██║██║   ██║
// ╚██╗ ██╔╝██║╚════██║██║   ██║██╔══██║██║         ╚════██║   ██║   ██║   ██║██║  ██║██║██║   ██║
//  ╚████╔╝ ██║███████║╚██████╔╝██║  ██║███████╗    ███████║   ██║   ╚██████╔╝██████╔╝██║╚██████╔╝
//   ╚═══╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝    ╚══════╝   ╚═╝    ╚═════╝ ╚═════╝ ╚═╝ ╚═════╝

static char *vs_configs[] = {"Debug", "Develop", "Release"};

static char *get_dir_from_string(char *file, char *base) {
	file = path_slashes(file);
	if (str_index_of(file, "/") >= 0) {
		char *dir = js_substr(file, 0, str_last_index_of(file, "/"));
		return path_slashes(path_join(base, path_relative(base, dir)));
	}
	return base;
}

static char *vs_get_dir(project_file_t *file) {
	return get_dir_from_string(file->file, file->project_name);
}

static char *vs_debug_dir(char *from, project_t *project) {
	char *debugdir = project->debugdir;
	if (!path_isabs(debugdir)) {
		debugdir = path_resolve(from, debugdir);
	}
	return str_replace(debugdir, "/", "\\");
}

static void vs_export_user_file(char *from, char *to, project_t *project) {
	if (project->debugdir[0] == '\0') {
		return;
	}
	write_file(path_resolve(to, string("%s.vcxproj.user", project->safe_name)));
	p(0, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	p(0, "<Project ToolsVersion=\"Current\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
	p(1, "<PropertyGroup>");
	if (strcmp(goptions.target, "windows") == 0) {
		p(2, "<LocalDebuggerWorkingDirectory>%s</LocalDebuggerWorkingDirectory>", vs_debug_dir(from, project));
		p(2, "<DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>");
		any_array_push(project->cmd_args, vs_debug_dir(from, project));
		p(2, "<LocalDebuggerCommandArguments>%s</LocalDebuggerCommandArguments>", str_join(project->cmd_args, " "));
	}
	p(1, "</PropertyGroup>");
	p(0, "</Project>");
	close_file();
}

static char *pretty_dir(char *dir) {
	while (starts_with(dir, "../")) {
		dir = js_substring(dir, 3, (int)strlen(dir));
	}
	return str_replace(dir, "/", "\\");
}

static char *vs_file_path(char *from, char *to, project_t *project, char *file) {
	if (project->no_flatten && !path_isabs(file)) {
		return path_resolve(path_join(project->basedir, file));
	}
	return file;
}

typedef bool (*file_filter_t)(char *file);

static bool filter_h(char *file) {
	return ends_with(file, ".h");
}

static bool filter_c(char *file) {
	return ends_with(file, ".c");
}

static bool filter_hlsl(char *file) {
	return ends_with(file, ".hlsl");
}

static bool filter_rc(char *file) {
	return ends_with(file, ".rc");
}

static void vs_item_group(char *from, char *to, project_t *project, char *type, file_filter_t filter) {
	p(1, "<ItemGroup>");
	for (int i = 0; i < project->files->length; ++i) {
		project_file_t *file = project->files->buffer[i];
		char           *dir  = vs_get_dir(file);
		if (filter(file->file)) {
			p(2, "<%s Include=\"%s\">", type, vs_file_path(from, to, project, file->file));
			p(3, "<Filter>%s</Filter>", pretty_dir(dir));
			p(2, "</%s>", type);
		}
	}
	p(1, "</ItemGroup>");
}

static void vs_export_filters(char *from, char *to, project_t *project) {
	for (int i = 0; i < project->subprojects->length; ++i) {
		vs_export_filters(from, to, project->subprojects->buffer[i]);
	}
	write_file(path_resolve(to, string("%s.vcxproj.filters", project->safe_name)));
	p(0, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	p(0, "<Project ToolsVersion=\"Current\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
	char        *lastdir = "";
	any_array_t *dirs    = list_create();
	for (int i = 0; i < project->files->length; ++i) {
		char *dir = vs_get_dir(project->files->buffer[i]);
		if (strcmp(dir, lastdir) != 0) {
			char *subdir = dir;
			while (str_index_of(subdir, "/") >= 0) {
				subdir = js_substr(subdir, 0, str_last_index_of(subdir, "/"));
				if (!list_contains(dirs, subdir)) {
					any_array_push(dirs, subdir);
				}
			}
			any_array_push(dirs, dir);
			lastdir = dir;
		}
	}
	p(1, "<ItemGroup>");
	for (int i = 0; i < dirs->length; ++i) {
		char *pretty = pretty_dir(dirs->buffer[i]);
		if (strcmp(pretty, "..") != 0) {
			p(2, "<Filter Include=\"%s\">", pretty);
			p(3, "<UniqueIdentifier>{%s}</UniqueIdentifier>", str_upper(random_uuid()));
			p(2, "</Filter>");
		}
	}
	p(1, "</ItemGroup>");
	vs_item_group(from, to, project, "ClInclude", filter_h);
	vs_item_group(from, to, project, "ClCompile", filter_c);
	vs_item_group(from, to, project, "CustomBuild", filter_hlsl);
	if (strcmp(goptions.target, "windows") == 0) {
		vs_item_group(from, to, project, "ResourceCompile", filter_rc);
	}
	p(0, "</Project>");
	close_file();
}

static void vs_configuration(char *config, int indent, project_t *project) {
	p(indent, "<PropertyGroup Condition=\"'$(Configuration)'=='%s'\" Label=\"Configuration\">", config);
	p(indent + 1, "<ConfigurationType>Application</ConfigurationType>");
	p(indent + 1, "<UseDebugLibraries>%s</UseDebugLibraries>", strcmp(config, "Release") == 0 ? "false" : "true");
	p(indent + 1, "<PlatformToolset>ClangCL</PlatformToolset>");
	p(indent + 1, "<PreferredToolArchitecture>x64</PreferredToolArchitecture>");
	if (strcmp(config, "Release") == 0 && project->lto) {
		p(indent + 1, "<WholeProgramOptimization>true</WholeProgramOptimization>");
	}
	p(indent + 1, "<CharacterSet>Unicode</CharacterSet>");
	p(indent, "</PropertyGroup>");
}

static char *vs_optimization(char *config) {
	if (strcmp(config, "Develop") == 0) {
		return "Full";
	}
	if (strcmp(config, "Release") == 0) {
		return "MaxSpeed";
	}
	return "Disabled";
}

static char *vs_c_std(project_t *project) {
	char *std = str_lower(project->c_std);
	if (strcmp(std, "c11") == 0) {
		return "stdc11";
	}
	if (strcmp(std, "c17") == 0 || strcmp(std, "c2x") == 0) {
		return "stdc17";
	}
	return "";
}

static void vs_item_definition(char *config, char *system, char *includes, char *debug_defines, char *release_defines, int indent, char *debug_libs,
                               char *release_libs, project_t *project) {
	bool release = strcmp(config, "Release") == 0;
	p(indent, "<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='%s|%s'\">", config, system);
	p(indent + 1, "<ClCompile>");
	p(indent + 2, "<AdditionalIncludeDirectories>%s</AdditionalIncludeDirectories>", includes);
	p(indent + 2,
	  "<AdditionalOptions>-Wno-deprecated-declarations -Wno-c23-extensions -Wno-incompatible-pointer-types -Wno-incompatible-function-pointer-types "
	  "-Wno-microsoft-enum-forward-reference /bigobj%s %%(AdditionalOptions)</AdditionalOptions>",
	  release ? " /Gw" : "");
	p(indent + 2, "<WarningLevel>Level3</WarningLevel>");
	p(indent + 2, "<Optimization>%s</Optimization>", vs_optimization(config));
	if (release) {
		p(indent + 2, "<FunctionLevelLinking>true</FunctionLevelLinking>");
		p(indent + 2, "<IntrinsicFunctions>true</IntrinsicFunctions>");
		p(indent + 2, "<DebugInformationFormat>None</DebugInformationFormat>");
	}
	p(indent + 2, "<PreprocessorDefinitions>%s%sWIN32;_WINDOWS;%%(PreprocessorDefinitions)</PreprocessorDefinitions>",
	  release ? release_defines : debug_defines, strcmp(system, "x64") == 0 ? "SYS_64;" : "");
	p(indent + 2, "<RuntimeLibrary>%s</RuntimeLibrary>", release ? "MultiThreaded" : "MultiThreadedDebug");
	p(indent + 2, "<MultiProcessorCompilation>true</MultiProcessorCompilation>");
	p(indent + 2, "<MinimalRebuild>false</MinimalRebuild>");
	if (strcmp(config, "Develop") == 0) {
		p(indent + 2, "<BasicRuntimeChecks>Default</BasicRuntimeChecks>");
	}
	p(indent + 2, "<LanguageStandard_C>%s</LanguageStandard_C>", vs_c_std(project));
	p(indent + 1, "</ClCompile>");
	p(indent + 1, "<Link>");
	p(indent + 2, "<SubSystem>%s</SubSystem>", strcmp(project->name, "amake") == 0 ? "Console" : "Windows");
	p(indent + 2, "<GenerateDebugInformation>%s</GenerateDebugInformation>", release ? "false" : "true");
	if (release) {
		p(indent + 2, "<EnableCOMDATFolding>true</EnableCOMDATFolding>");
		p(indent + 2, "<OptimizeReferences>true</OptimizeReferences>");
	}
	p(indent + 2,
	  "<AdditionalDependencies>%skernel32.lib;user32.lib;gdi32.lib;winspool.lib;comdlg32.lib;advapi32.lib;shell32.lib;ole32.lib;oleaut32.lib;uuid.lib;"
	  "odbc32.lib;odbccp32.lib;%%(AdditionalDependencies)</AdditionalDependencies>",
	  release ? release_libs : debug_libs);
	p(indent + 1, "</Link>");
	p(indent + 1, "<Manifest>");
	p(indent + 2, "<EnableDpiAwareness>PerMonitorHighDPIAware</EnableDpiAwareness>");
	p(indent + 1, "</Manifest>");
	p(indent, "</ItemDefinitionGroup>");
}

static char *vs_libs(char *from, char *to, project_t *project, char *config) {
	buffer_t sb = {0};
	string_buffer_init(&sb);
	for (int i = 0; i < project->subprojects->length; ++i) {
		project_t *sub = project->subprojects->buffer[i];
		if (sub->no_flatten) {
			string_buffer_append(&sb, string("%s\\build\\x64\\%s\\%s.lib;", project->basedir, config, sub->safe_name));
		}
		else {
			string_buffer_append(&sb, string("%s\\%s.lib;", config, sub->safe_name));
		}
	}
	// Release libs list the subprojects twice, kept from the original tool
	if (strcmp(config, "Release") == 0) {
		for (int i = 0; i < project->subprojects->length; ++i) {
			project_t *sub = project->subprojects->buffer[i];
			string_buffer_append(&sb, string("Release\\%s.lib;", sub->safe_name));
		}
	}
	for (int i = 0; i < project->libs->length; ++i) {
		char *lib = project->libs->buffer[i];
		if (fs_exists(path_resolve(from, string("%s.lib", lib)))) {
			string_buffer_append(&sb, string("%s.lib;", path_relative(to, path_resolve(from, lib))));
		}
		else {
			string_buffer_append(&sb, string("%s.lib;", lib));
		}
	}
	return string_buffer_get(&sb);
}

static void vs_export_project(char *from, char *to, project_t *project) {
	for (int i = 0; i < project->subprojects->length; ++i) {
		vs_export_project(from, to, project->subprojects->buffer[i]);
	}
	bool windows = strcmp(goptions.target, "windows") == 0;
	write_file(path_resolve(to, string("%s.vcxproj", project->safe_name)));
	p(0, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	p(0, "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
	p(1, "<ItemGroup Label=\"ProjectConfigurations\">");
	for (int i = 0; i < 3; ++i) {
		p(2, "<ProjectConfiguration Include=\"%s|x64\">", vs_configs[i]);
		p(3, "<Configuration>%s</Configuration>", vs_configs[i]);
		p(3, "<Platform>x64</Platform>");
		p(2, "</ProjectConfiguration>");
	}
	p(1, "</ItemGroup>");
	p(1, "<PropertyGroup Label=\"Globals\">");
	p(2, "<ProjectGuid>{%s}</ProjectGuid>", str_lower(project->uuid));
	p(2, "<VCProjectVersion>18.0</VCProjectVersion>");
	p(2, "<WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
	p(1, "</PropertyGroup>");
	p(1, "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />");
	for (int i = 0; i < 3; ++i) {
		vs_configuration(vs_configs[i], 1, project);
	}
	p(1, "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />");
	p(1, "<ImportGroup Label=\"ExtensionSettings\">");
	p(2, "<Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\masm.props\" />");
	p(1, "</ImportGroup>");
	p(1, "<PropertyGroup Label=\"UserMacros\" />");
	if (project->executable_name != NULL) {
		p(1, "<PropertyGroup>");
		p(2, "<TargetName>%s</TargetName>", project->executable_name);
		p(1, "</PropertyGroup>");
	}
	if (windows) {
		p(1, "<ImportGroup Label=\"PropertySheets\" Condition=\"'$(Platform)'=='x64'\">");
		p(2, "<Import Project=\"$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\" "
		     "Condition=\"exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')\" Label=\"LocalAppDataPlatform\" />");
		p(1, "</ImportGroup>");
	}
	buffer_t debug_defines = {0};
	string_buffer_init(&debug_defines);
	string_buffer_append(&debug_defines, "_DEBUG;");
	buffer_t release_defines = {0};
	string_buffer_init(&release_defines);
	string_buffer_append(&release_defines, "NDEBUG;");
	for (int i = 0; i < project->defines->length; ++i) {
		string_buffer_append(&debug_defines, string("%s;", project->defines->buffer[i]));
		string_buffer_append(&release_defines, string("%s;", project->defines->buffer[i]));
	}
	any_array_t *includes = list_create();
	for (int i = 0; i < project->includedirs->length; ++i) {
		char *relativized = path_relative(to, path_resolve(from, project->includedirs->buffer[i]));
		any_array_push(includes, relativized[0] == '\0' ? "." : relativized);
	}
	char *incstring    = str_join(includes, ";");
	char *debug_libs   = vs_libs(from, to, project, "Debug");
	char *release_libs = vs_libs(from, to, project, "Release");
	for (int i = 0; i < 3; ++i) {
		vs_item_definition(vs_configs[i], "x64", incstring, string_buffer_get(&debug_defines), string_buffer_get(&release_defines), 2, debug_libs, release_libs,
		                   project);
	}
	p(1, "<ItemGroup>");
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (ends_with(file, ".h")) {
			p(2, "<ClInclude Include=\"%s\" />", vs_file_path(from, to, project, file));
		}
	}
	p(1, "</ItemGroup>");
	p(1, "<ItemGroup>");
	any_array_t *used = list_create();
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (!is_source(file)) {
			continue;
		}
		char *name = str_lower(file);
		if (str_index_of(name, "/") >= 0) {
			name = js_substr(name, str_last_index_of(name, "/") + 1, (int)strlen(name));
		}
		name           = js_substr(name, 0, str_last_index_of(name, "."));
		char *filepath = vs_file_path(from, to, project, file);
		if (!list_contains(used, name)) {
			p(2, "<ClCompile Include=\"%s\" />", filepath);
		}
		else {
			while (list_contains(used, name)) {
				name = string("%s_", name);
			}
			p(2, "<ClCompile Include=\"%s\">", filepath);
			p(3, "<ObjectFileName>$(IntDir)\\%s.obj</ObjectFileName>", name);
			p(2, "</ClCompile>");
		}
		any_array_push(used, name);
	}
	p(1, "</ItemGroup>");
	p(1, "<ItemGroup>");
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (ends_with(file, ".natvis")) {
			p(2, "<Natvis Include=\"%s\"/>", file);
		}
	}
	p(1, "</ItemGroup>");
	if (windows) {
		p(1, "<ItemGroup>");
		p(0, "</ItemGroup>");
		p(1, "<ItemGroup>");
		p(2, "<None Include=\"icon.ico\" />");
		p(1, "</ItemGroup>");
		p(1, "<ItemGroup>");
		p(2, "<ResourceCompile Include=\"resources.rc\" />");
		for (int i = 0; i < project->files->length; ++i) {
			char *file = ((project_file_t *)project->files->buffer[i])->file;
			if (ends_with(file, ".rc")) {
				p(2, "<ResourceCompile Include=\"%s\" />", file);
			}
		}
		p(1, "</ItemGroup>");
	}
	p(1, "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />");
	p(1, "<ImportGroup Label=\"ExtensionTargets\">");
	p(2, "<Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\masm.targets\"/>");
	p(1, "</ImportGroup>");
	p(0, "</Project>");
	close_file();
}

static void vs_export(project_t *project) {
	char *from = path_resolve(".");
	char *to   = path_resolve("build");
	write_file(path_resolve(to, string("%s.slnx", project->safe_name)));
	p(0, "<Solution>");
	p(1, "<Configurations>");
	p(2, "<Platform Name=\"x64\" />");
	p(1, "</Configurations>");
	p(1, "<Project Path=\"%s.vcxproj\" Id=\"%s\" />", project->safe_name, str_lower(project->uuid));
	p(0, "</Solution>");
	close_file();
	vs_export_project(from, to, project);
	vs_export_filters(from, to, project);
	vs_export_user_file(from, to, project);
	write_file(path_resolve(to, "resources.rc"));
	p(0, "107       ICON         \"icon.ico\"");
	close_file();
	export_icon_ico(project->icon, path_resolve(to, "icon.ico"), from);
}

// ██╗  ██╗ ██████╗ ██████╗ ██████╗ ███████╗
// ╚██╗██╔╝██╔════╝██╔═══██╗██╔══██╗██╔════╝
//  ╚███╔╝ ██║     ██║   ██║██║  ██║█████╗
//  ██╔██╗ ██║     ██║   ██║██║  ██║██╔══╝
// ██╔╝ ██╗╚██████╗╚██████╔╝██████╔╝███████╗
// ╚═╝  ╚═╝ ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝

// The JavaScript version hashed the directory object, which stringified to this constant
// Kept so that regenerated xcode projects keep their object ids
#define XC_FILE_ID_PREFIX "[object Object]"

typedef struct {
	char *name;
	char *id;
} xc_dir_t;

typedef struct {
	char     *name;
	xc_dir_t *dir;
	char     *build_id;
	char     *file_id;
} xc_file_t;

typedef struct {
	char *name; // With the .framework extension when it has none
	char *build_id;
	char *file_id;
	char *local_path;
} xc_framework_t;

typedef struct {
	char  *idiom;
	double size;
	int    scale;
} xc_icon_t;

static char *last_name(char *name) {
	if (str_index_of(name, "/") < 0) {
		return name;
	}
	return js_substr(name, str_last_index_of(name, "/") + 1, (int)strlen(name));
}

static xc_dir_t *xc_find_dir(char *name, any_array_t *dirs) {
	for (int i = 0; i < dirs->length; ++i) {
		xc_dir_t *dir = dirs->buffer[i];
		if (strcmp(dir->name, name) == 0) {
			return dir;
		}
	}
	return NULL;
}

static xc_dir_t *xc_add_dir(char *name, any_array_t *dirs) {
	xc_dir_t *dir = xc_find_dir(name, dirs);
	if (dir == NULL) {
		dir       = calloc(1, sizeof(xc_dir_t));
		dir->name = name;
		dir->id   = new_path_id(name);
		any_array_push(dirs, dir);
		while (str_index_of(name, "/") >= 0) {
			name = js_substr(name, 0, str_last_index_of(name, "/"));
			xc_add_dir(name, dirs);
		}
	}
	return dir;
}

static char *xc_get_dir(project_file_t *file) {
	if (str_index_of(file->file, "/") >= 0) {
		char *dir = js_substr(file->file, 0, str_last_index_of(file->file, "/"));
		return path_slashes(path_join(file->project_name, path_relative(file->project_dir, dir)));
	}
	return file->project_name;
}

static bool xc_is_build_file(char *name) {
	return ends_with(name, ".c") || ends_with(name, ".m") || ends_with(name, ".metal");
}

static void add_icons(any_array_t *icons, char *idiom, double *sizes, int *scales, int count) {
	for (int i = 0; i < count; ++i) {
		xc_icon_t *icon = calloc(1, sizeof(xc_icon_t));
		icon->idiom     = idiom;
		icon->size      = sizes[i];
		icon->scale     = scales[i];
		any_array_push(icons, icon);
	}
}

static char *dir_of(char *path) {
	return js_substr(path, 0, str_last_index_of(path, "/"));
}

static void xc_workspace(char *to, project_t *project) {
	fs_ensuredir(path_resolve(to, string("%s.xcodeproj", project->safe_name), "project.xcworkspace"));
	write_file(path_resolve(to, string("%s.xcodeproj", project->safe_name), "project.xcworkspace", "contents.xcworkspacedata"));
	p(0, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	p(0, "<Workspace");
	p(0, "version = \"1.0\">");
	p(0, "<FileRef");
	p(0, "location = \"self:%s.xcodeproj\">", project->safe_name);
	p(0, "</FileRef>");
	p(0, "</Workspace>");
	close_file();
}

static void xc_code_sign_identity(bool team, bool ios) {
	p(4, "CODE_SIGN_IDENTITY = \"-\";");
	if (team) {
		p(4, ios ? "\"CODE_SIGN_IDENTITY[sdk=iphoneos*]\" = \"Apple Development\";" : "\"CODE_SIGN_IDENTITY[sdk=macosx*]\" = \"Apple Development\";");
	}
}

static void xc_clang_warnings(project_t *project) {
	p(4, "ALWAYS_SEARCH_USER_PATHS = NO;");
	p(4, "CLANG_ENABLE_MODULES = YES;");
	p(4, "CLANG_ENABLE_OBJC_ARC = YES;");
	p(4, "CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;");
	p(4, "CLANG_WARN_BOOL_CONVERSION = YES;");
	p(4, "CLANG_WARN_COMMA = YES;");
	p(4, "CLANG_WARN_CONSTANT_CONVERSION = YES;");
	p(4, "CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;");
	p(4, "CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;");
	p(4, "CLANG_WARN_EMPTY_BODY = YES;");
	p(4, "CLANG_WARN_ENUM_CONVERSION = YES;");
	p(4, "CLANG_WARN_INFINITE_RECURSION = YES;");
	p(4, "CLANG_WARN_INT_CONVERSION = YES;");
	p(4, "CLANG_WARN_NON_LITERAL_NULL_CONVERSION = YES;");
	p(4, "CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;");
	p(4, "CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;");
	p(4, "CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;");
	p(4, "CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;");
	p(4, "CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;");
	p(4, "CLANG_WARN_STRICT_PROTOTYPES = YES;");
	p(4, "CLANG_WARN_SUSPICIOUS_MOVE = YES;");
	p(4, "CLANG_WARN_UNREACHABLE_CODE = YES;");
	p(4, "CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;");
}

static void xc_defines(project_t *project) {
	for (int i = 0; i < project->defines->length; ++i) {
		char *define = project->defines->buffer[i];
		if (str_index_of(define, "=") >= 0) {
			p(5, "\"%s\",", str_replace(define, "\"", "\\\\\\\""));
		}
		else {
			p(5, "%s,", define);
		}
	}
}

static void xc_target_settings(project_t *project, char *from, char *plistname, any_array_t *frameworks, char *team, char *bundle, char *version, char *build,
                               bool ios) {
	p(4, "ARCHS = arm64;");
	p(4, "ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;");
	p(4, "CODE_SIGN_STYLE = Automatic;");
	if (team[0] != '\0') {
		p(4, "DEVELOPMENT_TEAM = %s;", team);
	}
	if (strcmp(goptions.target, "macos") == 0) {
		p(4, "COMBINE_HIDPI_IMAGES = YES;");
	}
	p(4, "ENABLE_HARDENED_RUNTIME = YES;");
	p(4, "FRAMEWORK_SEARCH_PATHS = (");
	p(5, "\"$(inherited)\",");
	// Search paths to local frameworks
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *framework = frameworks->buffer[i];
		if (framework->local_path != NULL) {
			p(5, "%s,", dir_of(framework->local_path));
		}
	}
	p(4, ");");
	p(4, "HEADER_SEARCH_PATHS = (");
	p(5, "\"$(inherited)\",");
	p(5, "\"/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include\",");
	for (int i = 0; i < project->includedirs->length; ++i) {
		p(5, "\"%s\",", str_replace(path_resolve(from, project->includedirs->buffer[i]), " ", "\\\\ "));
	}
	p(4, ");");
	p(4, "LIBRARY_SEARCH_PATHS = (");
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *framework = frameworks->buffer[i];
		if ((ends_with(framework->name, ".dylib") || ends_with(framework->name, ".a")) && framework->local_path != NULL) {
			p(5, "%s,", dir_of(framework->local_path));
		}
	}
	p(4, ");");
	p(4, "INFOPLIST_EXPAND_BUILD_SETTINGS = \"YES\";");
	p(4, "INFOPLIST_FILE = \"%s\";", path_resolve(from, plistname));
	p(0, "INFOPLIST_KEY_LSApplicationCategoryType = \"public.app-category.graphics-design\";");
	p(4, "LD_RUNPATH_SEARCH_PATHS = (");
	p(5, "\"$(inherited)\",");
	if (ios) {
		p(5, "\"@executable_path/Frameworks\",");
	}
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *framework = frameworks->buffer[i];
		if (ends_with(framework->name, ".dylib") && framework->local_path != NULL) {
			p(5, "%s,", dir_of(framework->local_path));
		}
	}
	p(4, ");");
	if (project->cflags->length > 0) {
		p(4, "OTHER_CFLAGS = (");
		for (int i = 0; i < project->cflags->length; ++i) {
			p(5, "\"%s\",", project->cflags->buffer[i]);
		}
		p(4, ");");
	}
	p(4, "PRODUCT_BUNDLE_IDENTIFIER = \"%s\";", bundle);
	p(4, "BUNDLE_VERSION = \"%s\";", version);
	p(4, "BUILD_VERSION = \"%s\";", build);
	xc_code_sign_identity(team[0] != '\0', ios);
	p(4, "PRODUCT_NAME = \"$(TARGET_NAME)\";");
}

static void xc_export(project_t *project) {
	char *from = path_resolve(".");
	char *to   = path_resolve("build");
	bool  ios  = strcmp(goptions.target, "ios") == 0;
	fs_ensuredir(path_resolve(to, string("%s.xcodeproj", project->safe_name)));
	xc_workspace(to, project);

	any_array_t *icons = list_create();
	if (ios) {
		add_icons(icons, "iphone", (double[]){20, 20, 29, 29, 40, 40, 60, 60}, (int[]){2, 3, 2, 3, 2, 3, 2, 3}, 8);
		add_icons(icons, "ipad", (double[]){20, 20, 29, 29, 40, 40, 76, 76, 83.5}, (int[]){1, 2, 1, 2, 1, 2, 1, 2, 2}, 9);
		add_icons(icons, "ios-marketing", (double[]){1024}, (int[]){1}, 1);
	}
	else {
		add_icons(icons, "mac", (double[]){16, 16, 32, 32, 128, 128, 256, 256, 512, 512}, (int[]){1, 2, 1, 2, 1, 2, 1, 2, 1, 2}, 10);
	}
	fs_ensuredir(path_resolve(to, "Images.xcassets", "AppIcon.appiconset"));
	write_file(path_resolve(to, "Images.xcassets", "AppIcon.appiconset", "Contents.json"));
	p(0, "{");
	p(1, "\"images\" : [");
	for (int i = 0; i < icons->length; ++i) {
		xc_icon_t *icon = icons->buffer[i];
		p(2, "{");
		p(3, "\"idiom\" : \"%s\",", icon->idiom);
		p(3, "\"size\" : \"%gx%g\",", icon->size, icon->size);
		p(3, "\"filename\" : \"%s%dx%g.png\",", icon->idiom, icon->scale, icon->size);
		p(3, "\"scale\" : \"%dx\"", icon->scale);
		p(2, i == icons->length - 1 ? "}" : "},");
	}
	p(1, "],");
	p(1, "\"info\" : {");
	p(2, "\"version\" : 1,");
	p(2, "\"author\" : \"xcode\"");
	p(1, "}");
	p(0, "}");
	close_file();
	for (int i = 0; i < icons->length; ++i) {
		xc_icon_t *icon = icons->buffer[i];
		int        size = (int)(icon->size * icon->scale);
		export_png_icon(project->icon, path_resolve(to, "Images.xcassets", "AppIcon.appiconset", string("%s%dx%g.png", icon->idiom, icon->scale, icon->size)),
		                size, size, from);
	}

	char        *plistname = "";
	any_array_t *files     = list_create();
	any_array_t *dirs      = list_create();
	for (int i = 0; i < project->files->length; ++i) {
		project_file_t *fileobject = project->files->buffer[i];
		char           *filename   = fileobject->file;
		if (ends_with(filename, ".plist")) {
			plistname = filename;
		}
		xc_file_t *file = calloc(1, sizeof(xc_file_t));
		file->name      = filename;
		file->dir       = xc_add_dir(xc_get_dir(fileobject), dirs);
		file->build_id  = new_path_id(string("%s%s_buildid", XC_FILE_ID_PREFIX, filename));
		file->file_id   = new_path_id(string("%s%s_fileid", XC_FILE_ID_PREFIX, filename));
		any_array_push(files, file);
	}
	if (plistname[0] == '\0') {
		printf("Error: no plist found\n");
	}
	any_array_t *frameworks = list_create();
	for (int i = 0; i < project->libs->length; ++i) {
		char           *lib       = project->libs->buffer[i];
		xc_framework_t *framework = calloc(1, sizeof(xc_framework_t));
		framework->name           = str_index_of(lib, ".") < 0 ? string("%s.framework", lib) : lib;
		framework->build_id       = new_path_id(string("%s_buildid", lib));
		framework->file_id        = new_path_id(string("%s_fileid", lib));
		any_array_push(frameworks, framework);
	}

	char *bundle  = string("org.armory3d.%s", str_lower(project->name));
	char *version = project->ios.version != NULL ? project->ios.version : "1.0";
	char *build   = project->ios.build != NULL ? project->ios.build : "1";
	char *team    = starts_with(from, "/Users/lubos/") ? "AUW6AHHL4Q" : "";

	char *project_id                   = new_path_id("_projectId");
	char *app_file_id                  = new_path_id("_appFileId");
	char *framework_build_id           = new_path_id("_frameworkBuildId");
	char *source_build_id              = new_path_id("_sourceBuildId");
	char *frameworks_group_id          = new_path_id("_frameworksGroupId");
	char *products_group_id            = new_path_id("_productsGroupId");
	char *main_group_id                = new_path_id("_mainGroupId");
	char *target_id                    = new_path_id("_targetId");
	char *native_build_config_list_id  = new_path_id("_nativeBuildConfigListId");
	char *project_build_config_list_id = new_path_id("_projectBuildConfigListId");
	char *debug_id                     = new_path_id("_debugId");
	char *release_id                   = new_path_id("_releaseId");
	char *native_debug_id              = new_path_id("_nativeDebugId");
	char *native_release_id            = new_path_id("_nativeReleaseId");
	char *debug_dir_file_id            = new_path_id("_debugDirFileId");
	char *debug_dir_build_id           = new_path_id("_debugDirBuildId");
	char *resources_build_id           = new_path_id("_resourcesBuildId");
	char *icon_file_id                 = new_path_id("_iconFileId");
	char *icon_build_id                = new_path_id("_iconBuildId");
	char *safe_name                    = project->safe_name;

	write_file(path_resolve(to, string("%s.xcodeproj", safe_name), "project.pbxproj"));
	p(0, "// !$*UTF8*$!");
	p(0, "{");
	p(1, "archiveVersion = 1;");
	p(1, "classes = {");
	p(1, "};");
	p(1, "objectVersion = 46;");
	p(1, "objects = {");
	p(0, "");
	p(0, "/* Begin PBXBuildFile section */");
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *f = frameworks->buffer[i];
		p(2, "%s /* %s in Frameworks */ = {isa = PBXBuildFile; fileRef = %s /* %s */; };", f->build_id, f->name, f->file_id, f->name);
	}
	p(2, "%s /* Deployment in Resources */ = {isa = PBXBuildFile; fileRef = %s /* Deployment */; };", debug_dir_build_id, debug_dir_file_id);
	for (int i = 0; i < files->length; ++i) {
		xc_file_t *file = files->buffer[i];
		if (xc_is_build_file(file->name)) {
			p(2, "%s /* %s in Sources */ = {isa = PBXBuildFile; fileRef = %s /* %s */; };", file->build_id, file->name, file->file_id, file->name);
		}
	}
	p(2, "%s /* Images.xcassets in Resources */ = {isa = PBXBuildFile; fileRef = %s /* Images.xcassets */; };", icon_build_id, icon_file_id);
	p(0, "/* End PBXBuildFile section */");
	p(0, "");
	p(0, "/* Begin PBXFileReference section */");
	p(2,
	  "%s /* %s.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = \"%s.app\"; sourceTree = "
	  "BUILT_PRODUCTS_DIR; };",
	  app_file_id, safe_name, executable_name(project));
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *f = frameworks->buffer[i];
		if (ends_with(f->name, ".framework")) {
			if (str_index_of(f->name, "/") >= 0) {
				// Local framework, a directory is specified
				f->local_path = path_resolve(from, f->name);
				p(2, "%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = %s; path = %s; sourceTree = \"<absolute>\"; };",
				  f->file_id, f->name, f->name, f->local_path);
			}
			else {
				// Xcode framework
				p(2,
				  "%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = %s; path = System/Library/Frameworks/%s; sourceTree = "
				  "SDKROOT; };",
				  f->file_id, f->name, f->name, f->name);
			}
		}
		else if (ends_with(f->name, ".dylib")) {
			if (str_index_of(f->name, "/") >= 0) {
				f->local_path = path_resolve(from, f->name);
				p(2, "%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = compiled.mach-o.dylib; name = %s; path = %s; sourceTree = \"<absolute>\"; };",
				  f->file_id, f->name, f->name, f->local_path);
			}
			else {
				p(2, "%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = compiled.mach-o.dylib; name = %s; path = usr/lib/%s; sourceTree = SDKROOT; };",
				  f->file_id, f->name, f->name, f->name);
			}
		}
		else {
			f->local_path = path_resolve(from, f->name);
			p(2, "%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = %s; path = %s; sourceTree = \"<group>\"; };", f->file_id,
			  f->name, f->name, f->local_path);
		}
	}
	p(2, "%s /* Deployment */ = {isa = PBXFileReference; lastKnownFileType = folder; name = Deployment; path = \"%s\"; sourceTree = \"<group>\"; };",
	  debug_dir_file_id, path_resolve(from, project->debugdir));
	for (int i = 0; i < files->length; ++i) {
		xc_file_t *file         = files->buffer[i];
		char      *filetype     = "unknown";
		char      *fileencoding = "";
		if (ends_with(file->name, ".plist")) {
			filetype = "text.plist.xml";
		}
		if (ends_with(file->name, ".h")) {
			filetype = "sourcecode.c.h";
		}
		if (ends_with(file->name, ".m")) {
			filetype = "sourcecode.c.objc";
		}
		if (ends_with(file->name, ".c")) {
			filetype = "sourcecode.c.c";
		}
		if (ends_with(file->name, ".metal")) {
			filetype     = "sourcecode.metal";
			fileencoding = "fileEncoding = 4; ";
		}
		if (!ends_with(file->name, ".DS_Store")) {
			p(2, "%s /* %s */ = {isa = PBXFileReference; %slastKnownFileType = %s; name = \"%s\"; path = \"%s\"; sourceTree = \"<group>\"; };", file->file_id,
			  file->name, fileencoding, filetype, last_name(file->name), path_resolve(from, file->name));
		}
	}
	p(2, "%s /* Images.xcassets */ = {isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = Images.xcassets; sourceTree = \"<group>\"; };",
	  icon_file_id);
	p(0, "/* End PBXFileReference section */");
	p(0, "");
	p(0, "/* Begin PBXFrameworksBuildPhase section */");
	p(2, "%s /* Frameworks */ = {", framework_build_id);
	p(3, "isa = PBXFrameworksBuildPhase;");
	p(3, "buildActionMask = 2147483647;");
	p(3, "files = (");
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *f = frameworks->buffer[i];
		p(4, "%s /* %s in Frameworks */,", f->build_id, f->name);
	}
	p(3, ");");
	p(3, "runOnlyForDeploymentPostprocessing = 0;");
	p(2, "};");
	p(0, "/* End PBXFrameworksBuildPhase section */");
	p(0, "");
	p(0, "/* Begin PBXGroup section */");
	p(2, "%s = {", main_group_id);
	p(3, "isa = PBXGroup;");
	p(3, "children = (");
	p(4, "%s /* Images.xcassets */,", icon_file_id);
	p(4, "%s /* Deployment */,", debug_dir_file_id);
	for (int i = 0; i < dirs->length; ++i) {
		xc_dir_t *dir = dirs->buffer[i];
		if (str_index_of(dir->name, "/") < 0) {
			p(4, "%s /* %s */,", dir->id, dir->name);
		}
	}
	p(4, "%s /* Frameworks */,", frameworks_group_id);
	p(4, "%s /* Products */,", products_group_id);
	p(3, ");");
	p(3, "sourceTree = \"<group>\";");
	p(2, "};");
	p(2, "%s /* Products */ = {", products_group_id);
	p(3, "isa = PBXGroup;");
	p(3, "children = (");
	p(4, "%s /* %s.app */,", app_file_id, safe_name);
	p(3, ");");
	p(3, "name = Products;");
	p(3, "sourceTree = \"<group>\";");
	p(2, "};");
	p(2, "%s /* Frameworks */ = {", frameworks_group_id);
	p(3, "isa = PBXGroup;");
	p(3, "children = (");
	for (int i = 0; i < frameworks->length; ++i) {
		xc_framework_t *f = frameworks->buffer[i];
		p(4, "%s /* %s */,", f->file_id, f->name);
	}
	p(3, ");");
	p(3, "name = Frameworks;");
	p(3, "sourceTree = \"<group>\";");
	p(2, "};");
	for (int i = 0; i < dirs->length; ++i) {
		xc_dir_t *dir = dirs->buffer[i];
		p(2, "%s /* %s */ = {", dir->id, dir->name);
		p(3, "isa = PBXGroup;");
		p(3, "children = (");
		for (int j = 0; j < dirs->length; ++j) {
			xc_dir_t *dir2 = dirs->buffer[j];
			if (dir2 == dir) {
				continue;
			}
			if (starts_with(dir2->name, dir->name)) {
				char *rest = js_substr(dir2->name, (int)strlen(dir->name) + 1, (int)strlen(dir2->name));
				if (str_index_of(rest, "/") < 0) {
					p(4, "%s /* %s */,", dir2->id, dir2->name);
				}
			}
		}
		for (int j = 0; j < files->length; ++j) {
			xc_file_t *file = files->buffer[j];
			if (file->dir == dir && !ends_with(file->name, ".DS_Store")) {
				p(4, "%s /* %s */,", file->file_id, file->name);
			}
		}
		p(3, ");");
		if (str_index_of(dir->name, "/") < 0) {
			p(3, "path = ../;");
		}
		p(3, "name = \"%s\";", last_name(dir->name));
		p(3, "sourceTree = \"<group>\";");
		p(2, "};");
	}
	p(0, "/* End PBXGroup section */");
	p(0, "");
	p(0, "/* Begin PBXNativeTarget section */");
	p(2, "%s /* %s */ = {", target_id, safe_name);
	p(3, "isa = PBXNativeTarget;");
	p(3, "buildConfigurationList = %s /* Build configuration list for PBXNativeTarget \"%s\" */;", native_build_config_list_id, safe_name);
	p(3, "buildPhases = (");
	p(4, "%s /* Sources */,", source_build_id);
	p(4, "%s /* Frameworks */,", framework_build_id);
	p(4, "%s /* Resources */,", resources_build_id);
	p(3, ");");
	p(3, "buildRules = (");
	p(3, ");");
	p(3, "dependencies = (");
	p(3, ");");
	p(3, "name = \"%s\";", project->name);
	p(3, "productName = \"%s\";", project->name);
	p(3, "productReference = %s /* %s.app */;", app_file_id, safe_name);
	p(3, strcmp(project->name, "amake") == 0 ? "productType = \"com.apple.product-type.tool\";" : "productType = \"com.apple.product-type.application\";");
	p(2, "};");
	p(0, "/* End PBXNativeTarget section */");
	p(0, "");
	p(0, "/* Begin PBXProject section */");
	p(2, "%s /* Project object */ = {", project_id);
	p(3, "isa = PBXProject;");
	p(3, "attributes = {");
	p(4, "LastUpgradeCheck = 1230;");
	p(4, "ORGANIZATIONNAME = \"Armory3D\";");
	p(4, "TargetAttributes = {");
	p(5, "%s = {", target_id);
	p(6, "CreatedOnToolsVersion = 6.1.1;");
	if (team[0] != '\0') {
		p(6, "DevelopmentTeam = %s;", team);
		p(6, "ProvisioningStyle = Automatic;");
	}
	p(5, "};");
	p(4, "};");
	p(3, "};");
	p(3, "buildConfigurationList = %s /* Build configuration list for PBXProject \"%s\" */;", project_build_config_list_id, safe_name);
	p(3, "compatibilityVersion = \"Xcode 3.2\";");
	p(3, "developmentRegion = en;");
	p(3, "hasScannedForEncodings = 0;");
	p(3, "knownRegions = (");
	p(4, "en,");
	p(4, "Base,");
	p(3, ");");
	p(3, "mainGroup = %s;", main_group_id);
	p(3, "productRefGroup = %s /* Products */;", products_group_id);
	p(3, "projectDirPath = \"\";");
	p(3, "projectRoot = \"\";");
	p(3, "targets = (");
	p(4, "%s /* %s */,", target_id, safe_name);
	p(3, ");");
	p(2, "};");
	p(0, "/* End PBXProject section */");
	p(0, "");
	p(0, "/* Begin PBXResourcesBuildPhase section */");
	p(2, "%s /* Resources */ = {", resources_build_id);
	p(3, "isa = PBXResourcesBuildPhase;");
	p(3, "buildActionMask = 2147483647;");
	p(3, "files = (");
	p(4, "%s /* Deployment in Resources */,", debug_dir_build_id);
	p(4, "%s /* Images.xcassets in Resources */,", icon_build_id);
	p(3, ");");
	p(3, "runOnlyForDeploymentPostprocessing = 0;");
	p(2, "};");
	p(0, "/* End PBXResourcesBuildPhase section */");
	p(0, "");
	p(0, "/* Begin PBXSourcesBuildPhase section */");
	p(2, "%s /* Sources */ = {", source_build_id);
	p(3, "isa = PBXSourcesBuildPhase;");
	p(3, "buildActionMask = 2147483647;");
	p(3, "files = (");
	for (int i = 0; i < files->length; ++i) {
		xc_file_t *file = files->buffer[i];
		if (xc_is_build_file(file->name)) {
			p(4, "%s /* %s in Sources */,", file->build_id, file->name);
		}
	}
	p(3, ");");
	p(0, "runOnlyForDeploymentPostprocessing = 0;");
	p(0, "};");
	p(0, "/* End PBXSourcesBuildPhase section */");
	p(0, "");
	p(0, "/* Begin XCBuildConfiguration section */");
	p(2, "%s /* Debug */ = {", debug_id);
	p(3, "isa = XCBuildConfiguration;");
	p(3, "buildSettings = {");
	xc_clang_warnings(project);
	xc_code_sign_identity(team[0] != '\0', ios);
	p(4, "COPY_PHASE_STRIP = NO;");
	p(4, "ENABLE_STRICT_OBJC_MSGSEND = YES;");
	p(4, "ENABLE_TESTABILITY = YES;");
	p(4, "GCC_C_LANGUAGE_STANDARD = \"%s\";", project->c_std);
	p(4, "GCC_DYNAMIC_NO_PIC = NO;");
	p(4, "GCC_NO_COMMON_BLOCKS = YES;");
	p(4, "GCC_OPTIMIZATION_LEVEL = 0;");
	p(4, "GCC_PREPROCESSOR_DEFINITIONS = (");
	p(5, "\"DEBUG=1\",");
	xc_defines(project);
	p(5, "\"$(inherited)\",");
	p(4, ");");
	p(4, "GCC_SYMBOLS_PRIVATE_EXTERN = NO;");
	p(4, "GCC_WARN_64_TO_32_BIT_CONVERSION = YES;");
	p(4, "GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;");
	p(4, "GCC_WARN_UNDECLARED_SELECTOR = YES;");
	p(4, "GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;");
	p(4, "GCC_WARN_UNUSED_FUNCTION = YES;");
	p(4, "GCC_WARN_UNUSED_VARIABLE = YES;");
	p(4, ios ? "IPHONEOS_DEPLOYMENT_TARGET = 18.0;" : "MACOSX_DEPLOYMENT_TARGET = 14.0;");
	p(4, "MTL_ENABLE_DEBUG_INFO = YES;");
	p(4, "ONLY_ACTIVE_ARCH = YES;");
	if (ios) {
		p(4, "SDKROOT = iphoneos;");
		p(4, "TARGETED_DEVICE_FAMILY = \"1,2\";");
	}
	else {
		p(4, "SDKROOT = macosx;");
	}
	p(3, "};");
	p(3, "name = Debug;");
	p(2, "};");
	p(2, "%s /* Release */ = {", release_id);
	p(3, "isa = XCBuildConfiguration;");
	p(3, "buildSettings = {");
	xc_clang_warnings(project);
	xc_code_sign_identity(team[0] != '\0', ios);
	p(4, "COPY_PHASE_STRIP = YES;");
	if (strcmp(goptions.target, "macos") == 0) {
		p(4, "DEBUG_INFORMATION_FORMAT = \"dwarf-with-dsym\";");
	}
	p(4, "ENABLE_NS_ASSERTIONS = NO;");
	p(4, "ENABLE_STRICT_OBJC_MSGSEND = YES;");
	p(4, "GCC_C_LANGUAGE_STANDARD = \"%s\";", project->c_std);
	p(4, "GCC_NO_COMMON_BLOCKS = YES;");
	p(4, "GCC_PREPROCESSOR_DEFINITIONS = (");
	p(5, "NDEBUG,");
	xc_defines(project);
	p(5, "\"$(inherited)\",");
	p(4, ");");
	p(4, "GCC_WARN_64_TO_32_BIT_CONVERSION = YES;");
	p(4, "GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;");
	p(4, "GCC_WARN_UNDECLARED_SELECTOR = YES;");
	p(4, "GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;");
	p(4, "GCC_WARN_UNUSED_FUNCTION = YES;");
	p(4, "GCC_WARN_UNUSED_VARIABLE = YES;");
	p(4, ios ? "IPHONEOS_DEPLOYMENT_TARGET = 18.0;" : "MACOSX_DEPLOYMENT_TARGET = 14.0;");
	p(4, "MTL_ENABLE_DEBUG_INFO = NO;");
	p(4, "ONLY_ACTIVE_ARCH = YES;");
	if (ios) {
		p(4, "SDKROOT = iphoneos;");
		p(4, "TARGETED_DEVICE_FAMILY = \"1,2\";");
		p(4, "VALIDATE_PRODUCT = YES;");
	}
	else {
		p(4, "SDKROOT = macosx;");
	}
	p(3, "};");
	p(3, "name = Release;");
	p(2, "};");
	p(2, "%s /* Debug */ = {", native_debug_id);
	p(3, "isa = XCBuildConfiguration;");
	p(3, "buildSettings = {");
	xc_target_settings(project, from, plistname, frameworks, team, bundle, version, build, ios);
	p(3, "};");
	p(3, "name = Debug;");
	p(2, "};");
	p(2, "%s /* Release */ = {", native_release_id);
	p(3, "isa = XCBuildConfiguration;");
	p(3, "buildSettings = {");
	xc_target_settings(project, from, plistname, frameworks, team, bundle, version, build, ios);
	p(3, "};");
	p(3, "name = Release;");
	p(2, "};");
	p(0, "/* End XCBuildConfiguration section */");
	p(0, "");
	p(0, "/* Begin XCConfigurationList section */");
	p(2, "%s /* Build configuration list for PBXProject \"%s\" */ = {", project_build_config_list_id, safe_name);
	p(3, "isa = XCConfigurationList;");
	p(3, "buildConfigurations = (");
	p(4, "%s /* Debug */,", debug_id);
	p(4, "%s /* Release */,", release_id);
	p(3, ");");
	p(3, "defaultConfigurationIsVisible = 0;");
	p(3, "defaultConfigurationName = Release;");
	p(2, "};");
	p(2, "%s /* Build configuration list for PBXNativeTarget \"%s\" */ = {", native_build_config_list_id, safe_name);
	p(3, "isa = XCConfigurationList;");
	p(3, "buildConfigurations = (");
	p(4, "%s /* Debug */,", native_debug_id);
	p(4, "%s /* Release */,", native_release_id);
	p(3, ");");
	p(3, "defaultConfigurationIsVisible = 0;");
	p(3, "defaultConfigurationName = Release;");
	p(2, "};");
	p(0, "/* End XCConfigurationList section */");
	p(1, "};");
	p(1, "rootObject = %s /* Project object */;", project_id);
	p(0, "}");
	close_file();
}

//  █████╗ ███╗   ██╗██████╗ ██████╗  ██████╗ ██╗██████╗
// ██╔══██╗████╗  ██║██╔══██╗██╔══██╗██╔═══██╗██║██╔══██╗
// ███████║██╔██╗ ██║██║  ██║██████╔╝██║   ██║██║██║  ██║
// ██╔══██║██║╚██╗██║██║  ██║██╔══██╗██║   ██║██║██║  ██║
// ██║  ██║██║ ╚████║██████╔╝██║  ██║╚██████╔╝██║██████╔╝
// ╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚═╝╚═════╝

static char *get_text_data(char *p) {
	char *data = fs_readfile(string("%s/sources/backends/data/%s", irondir, p), NULL);
	return data != NULL ? data : "";
}

static void copy_data(char *p, char *to) {
	fs_copyfile(string("%s/sources/backends/data/%s", irondir, p), to);
}

static void android_write_app_gradle(project_t *project, char *outdir, char *from, char *package, int version_code, char *version_name) {
	char *gradle = get_text_data("android/app/build.gradle.kts");
	gradle       = str_replace(gradle, "{package}", package);
	gradle       = str_replace(gradle, "{versionCode}", string("%d", version_code));
	gradle       = str_replace(gradle, "{versionName}", version_name);
	gradle       = str_replace(gradle, "{compileSdkVersion}", "36");
	gradle       = str_replace(gradle, "{minSdkVersion}", "35"); // Android 15
	gradle       = str_replace(gradle, "{targetSdkVersion}", "36");
	char *arch   = "";
	if (strcmp(goptions.arch, "arm8") == 0) {
		arch = "ndk {abiFilters += listOf(\"arm64-v8a\")}";
	}
	else if (strcmp(goptions.arch, "default") != 0) {
		arch = "ndk {abiFilters += listOf(\"\")}";
	}
	gradle               = str_replace(gradle, "{architecture}", arch);
	buffer_t javasources = {0};
	string_buffer_init(&javasources);
	for (int i = 0; i < project->javadirs->length; ++i) {
		char *dir = path_relative(path_join(outdir, "app"), path_resolve(from, project->javadirs->buffer[i]));
		string_buffer_append(&javasources, string("\"%s\", ", path_slashes(dir)));
	}
	string_buffer_append(&javasources, string("\"%s\"", path_slashes(path_join(irondir, "sources", "backends", "data", "android_java"))));
	gradle = str_replace(gradle, "{javasources}", string_buffer_get(&javasources));
	fs_writefile(path_join(outdir, "app", "build.gradle.kts"), gradle);
}

static void android_write_cmake_lists(project_t *project, char *outdir, char *from) {
	char    *cmake   = get_text_data("android/app/CMakeLists.txt");
	buffer_t defines = {0};
	string_buffer_init(&defines);
	for (int i = 0; i < project->defines->length; ++i) {
		string_buffer_append(&defines, string(" -D%s", str_replace(project->defines->buffer[i], "\"", "\\\\\\\"")));
	}
	cmake = str_replace(cmake, "{debug_defines}", string_buffer_get(&defines));
	cmake = str_replace(cmake, "{release_defines}", string_buffer_get(&defines));

	buffer_t includes = {0};
	string_buffer_init(&includes);
	for (int i = 0; i < project->includedirs->length; ++i) {
		string_buffer_append(&includes, string("  \"%s\"\n", path_slashes(path_resolve(project->includedirs->buffer[i]))));
	}
	cmake = str_replace(cmake, "{includes}", string_buffer_get(&includes));

	buffer_t files = {0};
	string_buffer_init(&files);
	for (int i = 0; i < project->files->length; ++i) {
		char *file = ((project_file_t *)project->files->buffer[i])->file;
		if (ends_with(file, ".c") || ends_with(file, ".h")) {
			char *full = path_isabs(file) ? path_resolve(file) : path_resolve(path_join(from, file));
			string_buffer_append(&files, string("  \"%s\"\n", path_slashes(full)));
		}
	}
	cmake = str_replace(cmake, "{files}", string_buffer_get(&files));

	buffer_t libraries1 = {0};
	string_buffer_init(&libraries1);
	buffer_t libraries2 = {0};
	string_buffer_init(&libraries2);
	for (int i = 0; i < project->libs->length; ++i) {
		char *lib = project->libs->buffer[i];
		string_buffer_append(&libraries1, string("find_library(%s-lib %s)\n", lib, lib));
		string_buffer_append(&libraries2, string("  ${%s-lib}\n", lib));
	}
	cmake = str_replace(cmake, "{libraries1}", string_buffer_get(&libraries1));
	cmake = str_replace(cmake, "{libraries2}", string_buffer_get(&libraries2));
	fs_writefile(path_join(outdir, "app", "CMakeLists.txt"), cmake);
}

static void android_write_manifest(char *outdir, char *package, int version_code, char *version_name, char *permissions) {
	char *manifest = get_text_data("android/main/AndroidManifest.xml");
	manifest       = str_replace(manifest, "{package}", package);
	manifest       = str_replace(manifest, "{installLocation}", "internalOnly");
	manifest       = str_replace(manifest, "{versionCode}", string("%d", version_code));
	manifest       = str_replace(manifest, "{versionName}", version_name);
	manifest       = str_replace(manifest, "{screenOrientation}", "sensor");
	manifest       = str_replace(manifest, "{targetSdkVersion}", "36");
	buffer_t perms = {0};
	string_buffer_init(&perms);
	if (permissions != NULL && permissions[0] != '\0') {
		any_array_t *list = string_split(permissions, ",");
		for (int i = 0; i < list->length; ++i) {
			string_buffer_append(&perms, string("\n\t<uses-permission android:name=\"%s\"/>", list->buffer[i]));
		}
	}
	manifest = str_replace(manifest, "{permissions}", string_buffer_get(&perms));
	manifest = str_replace(manifest, "{metadata}", "");
	fs_ensuredir(path_join(outdir, "app", "src", "main"));
	fs_writefile(path_join(outdir, "app", "src", "main", "AndroidManifest.xml"), manifest);
}

static void android_export(project_t *project) {
	char *from      = path_resolve(".");
	char *to        = path_resolve("build");
	char *safe_name = project->safe_name;
	char *outdir    = path_join(to, safe_name);
	fs_ensuredir(outdir);

	char *package      = project->android.package != NULL ? project->android.package : "org.armory3d";
	int   version_code = project->android.version_code != 0 ? project->android.version_code : 1;
	char *version_name = project->android.version_name != NULL ? project->android.version_name : "1.0";

	fs_writefile(path_join(outdir, "build.gradle.kts"), get_text_data("android/build.gradle.kts"));
	fs_writefile(path_join(outdir, "gradle.properties"), get_text_data("android/gradle.properties"));
	fs_writefile(path_join(outdir, "gradlew"), get_text_data("android/gradlew"));
	os_chmod_exec(path_join(outdir, "gradlew"));
	fs_writefile(path_join(outdir, "gradlew.bat"), get_text_data("android/gradlew.bat"));
	fs_writefile(path_join(outdir, "settings.gradle.kts"), str_replace(get_text_data("android/settings.gradle.kts"), "{name}", project->name));
	fs_ensuredir(path_join(outdir, "app"));
	fs_writefile(path_join(outdir, "app", "proguard-rules.pro"), get_text_data("android/app/proguard-rules.pro"));
	android_write_app_gradle(project, outdir, from, package, version_code, version_name);
	android_write_cmake_lists(project, outdir, from);
	fs_ensuredir(path_join(outdir, "app", "src"));
	fs_ensuredir(path_join(outdir, "app", "src", "main"));
	android_write_manifest(outdir, package, version_code, version_name, project->android.permissions);
	char *strings = str_replace(get_text_data("android/main/res/values/strings.xml"), "{name}", project->name);
	fs_ensuredir(path_join(outdir, "app", "src", "main", "res", "values"));
	fs_writefile(path_join(outdir, "app", "src", "main", "res", "values", "strings.xml"), strings);

	char *folders[] = {"mipmap-mdpi", "mipmap-hdpi", "mipmap-xhdpi", "mipmap-xxhdpi", "mipmap-xxxhdpi"};
	int   dpis[]    = {48, 72, 96, 144, 192};
	for (int i = 0; i < 5; ++i) {
		fs_ensuredir(path_join(outdir, "app", "src", "main", "res", folders[i]));
		export_png_icon(project->icon, path_resolve(to, safe_name, "app", "src", "main", "res", folders[i], "ic_launcher.png"), dpis[i], dpis[i], from);
		export_png_icon(project->icon, path_resolve(to, safe_name, "app", "src", "main", "res", folders[i], "ic_launcher_round.png"), dpis[i], dpis[i], from);
	}
	fs_ensuredir(path_join(outdir, "gradle", "wrapper"));
	copy_data("android/gradle/wrapper/gradle-wrapper.jar", path_join(outdir, "gradle", "wrapper", "gradle-wrapper.jar"));
	fs_writefile(path_join(outdir, "gradle", "wrapper", "gradle-wrapper.properties"), get_text_data("android/gradle/wrapper/gradle-wrapper.properties"));
	fs_copydir(path_resolve(from, project->debugdir), path_resolve(to, safe_name, "app", "src", "main", "assets"));
	compile_commands_export(project);
}

// ███████╗██╗  ██╗██████╗  ██████╗ ██████╗ ████████╗
// ██╔════╝╚██╗██╔╝██╔══██╗██╔═══██╗██╔══██╗╚══██╔══╝
// █████╗   ╚███╔╝ ██████╔╝██║   ██║██████╔╝   ██║
// ██╔══╝   ██╔██╗ ██╔═══╝ ██║   ██║██╔══██╗   ██║
// ███████╗██╔╝ ██╗██║     ╚██████╔╝██║  ██║   ██║
// ╚══════╝╚═╝  ╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝   ╚═╝

void export_solution(project_t *project) {
	if (strcmp(goptions.ccompiler, "tcc") == 0) {
		char *flags = goptions.debug ? "" : "-fno-asynchronous-unwind-tables";
		make_export(project, goptions.ccompiler, flags, "", "");
	}
	else if (strcmp(goptions.target, "ios") == 0 || strcmp(goptions.target, "macos") == 0) {
		xc_export(project);
	}
	else if (strcmp(goptions.target, "android") == 0) {
		android_export(project);
	}
	else if (strcmp(goptions.target, "wasm") == 0) {
		char *compiler = "clang --target=wasm32 -nostdlib -matomics -mbulk-memory";
		char *linker =
		    "--target=wasm32 -nostdlib -matomics -mbulk-memory "
		    "\"-Wl,--import-memory,--shared-memory,--allow-undefined,--no-entry,--initial-memory=671088640,--max-memory=671088640,-z,stack-size=256000\"";
		if (!goptions.debug) {
			linker = string("%s -Wl,--strip-all", linker);
		}
		make_export(project, compiler, "", linker, ".wasm");
		compile_commands_export(project);
	}
	else if (strcmp(goptions.target, "linux") == 0) {
		char *compiler_flags = "";
		char *linker_flags   = "-static-libgcc -pthread -lm";
		if (strcmp(goptions.ccompiler, "gcc") == 0) {
			compiler_flags = "-flto";
			linker_flags   = string("%s -flto", linker_flags);
		}
		if (!goptions.debug) {
			compiler_flags = string("%s -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables", compiler_flags);
			linker_flags   = string("%s -Wl,--gc-sections -Wl,-s", linker_flags);
		}
		make_export(project, goptions.ccompiler, compiler_flags, linker_flags, "");
		compile_commands_export(project);
	}
	else {
		vs_export(project);
	}
}
