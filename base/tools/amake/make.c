// Project model, project.c evaluation, asset and shader export

#include "make.h"
#include "../../sources/libs/minic.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

flags_t flags;

static int          platform_value;
static int          graphics_value;
static any_array_t *script_dirs = NULL; // Directories of the project.c files being evaluated

// ██████╗ ██████╗  ██████╗      ██╗███████╗ ██████╗████████╗
// ██╔══██╗██╔══██╗██╔═══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
// ██████╔╝██████╔╝██║   ██║     ██║█████╗  ██║        ██║
// ██╔═══╝ ██╔══██╗██║   ██║██   ██║██╔══╝  ██║        ██║
// ██║     ██║  ██║╚██████╔╝╚█████╔╝███████╗╚██████╗   ██║
// ╚═╝     ╚═╝  ╚═╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝

project_t *project_create(char *name, char *basedir) {
	project_t *p = calloc(1, sizeof(project_t));
	p->name      = string_copy(name);
	p->safe_name = string_copy(name);
	for (char *c = p->safe_name; *c; ++c) {
		// Same as name.replace(/[^A-z0-9\-\_]/g, "-")
		bool keep = (*c >= 'A' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
		if (!keep) {
			*c = '-';
		}
	}
	p->icon            = "icon.png";
	p->basedir         = basedir;
	p->uuid            = random_uuid();
	p->debugdir        = "build/out";
	p->c_std           = "c11";
	p->lto             = true;
	p->no_flatten      = true;
	p->files           = list_create();
	p->includes        = list_create();
	p->includedirs     = list_create();
	p->defines         = list_create();
	p->libs            = list_create();
	p->cflags          = list_create();
	p->cmd_args        = list_create();
	p->javadirs        = list_create();
	p->subprojects     = list_create();
	p->asset_matchers  = list_create();
	p->shader_matchers = list_create();
	return p;
}

static bool contains_fancy_define(any_array_t *defines, char *value) {
	char *name = js_substring(value, 0, str_index_of(value, "="));
	for (int i = 0; i < defines->length; ++i) {
		char *element = defines->buffer[i];
		int   index   = str_index_of(element, "=");
		if (index >= 0 && strcmp(name, js_substring(element, 0, index)) == 0) {
			return true;
		}
	}
	return false;
}

static void add_file_for_real(project_t *project, char *file) {
	for (int i = 0; i < project->files->length; ++i) {
		project_file_t *f = project->files->buffer[i];
		if (strcmp(f->file, file) == 0) {
			return;
		}
	}
	project_file_t *f = calloc(1, sizeof(project_file_t));
	f->file           = file;
	f->project_dir    = project->basedir;
	f->project_name   = project->name;
	any_array_push(project->files, f);
}

static void search_files_in(project_t *project, char *current) {
	any_array_t *files = fs_readdir(current);
	for (int i = 0; i < files->length; ++i) {
		char *file = path_join(current, files->buffer[i]);
		if (!fs_exists(file) || fs_isdir(file)) {
			continue;
		}
		file = path_relative(project->basedir, file);
		for (int j = 0; j < project->includes->length; ++j) {
			char *include = project->includes->buffer[j];
			if (path_isabs(include)) {
				include = path_relative(project->basedir, include);
			}
			if (glob_matches(path_slashes(file), path_slashes(include))) {
				add_file_for_real(project, path_slashes(file));
			}
		}
	}
	for (int i = 0; i < files->length; ++i) {
		char *d = files->buffer[i];
		if (d[0] == '.') {
			continue;
		}
		char *dir = path_join(current, d);
		if (!fs_isdir(dir)) {
			continue;
		}
		search_files_in(project, dir);
	}
}

void project_search_files(project_t *project) {
	for (int i = 0; i < project->subprojects->length; ++i) {
		project_search_files(project->subprojects->buffer[i]);
	}
	search_files_in(project, project->basedir);
	for (int i = 0; i < project->includes->length; ++i) {
		char *include = project->includes->buffer[i];
		if (path_isabs(include) && strstr(include, "**") != NULL) {
			int star = str_index_of(include, "**");
			int end  = str_last_index_of(path_slashes(js_substring(include, 0, star)), "/");
			search_files_in(project, js_substring(include, 0, end));
		}
		if (starts_with(include, "../")) {
			char *start = "../";
			while (starts_with(include, start)) {
				start = string("%s../", start);
			}
			search_files_in(project, path_resolve(project->basedir, start));
		}
	}
}

static void merge_string(char **to, char *from) {
	if (*to == NULL) {
		*to = from;
	}
}

static void merge_list(any_array_t *to, any_array_t *from) {
	for (int i = 0; i < from->length; ++i) {
		if (!list_contains(to, from->buffer[i])) {
			any_array_push(to, from->buffer[i]);
		}
	}
}

// Merges android permission lists, both are comma separated
static void merge_permissions(char **to, char *from) {
	if (*to == NULL || from == NULL) {
		merge_string(to, from);
		return;
	}
	any_array_t *list = string_split(*to, ",");
	merge_list(list, string_split(from, ","));
	*to = str_join(list, ",");
}

void project_internal_flatten(project_t *project) {
	any_array_t *out = list_create();
	for (int i = 0; i < project->subprojects->length; ++i) {
		project_internal_flatten(project->subprojects->buffer[i]);
	}
	for (int i = 0; i < project->subprojects->length; ++i) {
		project_t *sub = project->subprojects->buffer[i];
		if (sub->no_flatten) {
			any_array_push(out, sub);
			continue;
		}
		if (!sub->lto) {
			project->lto = false;
		}
		if (strcmp(sub->icon, "icon.png") != 0) {
			project->icon = sub->icon;
		}
		merge_string(&project->ios.version, sub->ios.version);
		merge_string(&project->ios.build, sub->ios.build);
		merge_string(&project->android.package, sub->android.package);
		merge_string(&project->android.version_name, sub->android.version_name);
		merge_permissions(&project->android.permissions, sub->android.permissions);
		if (project->android.version_code == 0) {
			project->android.version_code = sub->android.version_code;
		}
		for (int j = 0; j < sub->defines->length; ++j) {
			char *d = sub->defines->buffer[j];
			if (str_index_of(d, "=") >= 0 ? !contains_fancy_define(project->defines, d) : !list_contains(project->defines, d)) {
				any_array_push(project->defines, d);
			}
		}
		for (int j = 0; j < sub->files->length; ++j) {
			project_file_t *file     = sub->files->buffer[j];
			char           *absolute = file->file;
			if (!path_isabs(absolute)) {
				absolute = path_join(sub->basedir, file->file);
			}
			project_file_t *f = calloc(1, sizeof(project_file_t));
			f->file           = path_slashes(absolute);
			f->project_dir    = sub->basedir;
			f->project_name   = sub->name;
			any_array_push(project->files, f);
		}
		for (int j = 0; j < sub->includedirs->length; ++j) {
			char *dir = path_resolve(sub->basedir, sub->includedirs->buffer[j]);
			if (!list_contains(project->includedirs, dir)) {
				any_array_push(project->includedirs, dir);
			}
		}
		for (int j = 0; j < sub->javadirs->length; ++j) {
			char *dir = path_resolve(sub->basedir, sub->javadirs->buffer[j]);
			if (!list_contains(project->javadirs, dir)) {
				any_array_push(project->javadirs, dir);
			}
		}
		merge_list(project->libs, sub->libs);
		merge_list(project->cflags, sub->cflags);
	}
	project->subprojects = out;
}

static void flatten_subprojects(project_t *project) {
	for (int i = 0; i < project->subprojects->length; ++i) {
		project_t *sub  = project->subprojects->buffer[i];
		sub->no_flatten = false;
		flatten_subprojects(sub);
	}
}

static matcher_t *matcher_create(project_t *project, char *match, char *destination, int options) {
	if (!path_isabs(match)) {
		char *base = path_slashes(path_resolve(project->basedir));
		if (!ends_with(base, "/")) {
			base = string("%s/", base);
		}
		match = string("%s%s", base, path_slashes(match));
	}
	matcher_t *m   = calloc(1, sizeof(matcher_t));
	m->match       = match;
	m->destination = destination;
	m->options     = options;
	return m;
}

static void concat(any_array_t *to, any_array_t *from) {
	for (int i = 0; i < from->length; ++i) {
		any_array_push(to, from->buffer[i]);
	}
}

static char *script_dir(void) {
	return script_dirs->buffer[script_dirs->length - 1];
}

void script_push_dir(char *dir) {
	if (script_dirs == NULL) {
		script_dirs = list_create();
	}
	any_array_push(script_dirs, dir);
}

void script_pop_dir(void) {
	script_dirs->length--;
}

// Relative paths passed to natives resolve against the directory of the running project.c
char *script_path(char *p) {
	return path_isabs(p) ? p : path_resolve(script_dir(), p);
}

// ███╗   ██╗ █████╗ ████████╗██╗██╗   ██╗███████╗███████╗
// ████╗  ██║██╔══██╗╚══██╔══╝██║██║   ██║██╔════╝██╔════╝
// ██╔██╗ ██║███████║   ██║   ██║██║   ██║█████╗  ███████╗
// ██║╚██╗██║██╔══██║   ██║   ██║╚██╗ ██╔╝██╔══╝  ╚════██║
// ██║ ╚████║██║  ██║   ██║   ██║ ╚████╔╝ ███████╗███████║
// ╚═╝  ╚═══╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═══╝  ╚══════╝╚══════╝

void console_log(char *s) {
	printf("%s\n", s);
}

static char *arg_str(minic_val_t *args, int argc, int i) {
	char *s = minic_arg_p(args, argc, i);
	return s != NULL ? string_copy(s) : "";
}

static minic_val_t native_project_create(minic_val_t *args, int argc) {
	return minic_val_ptr(project_create(arg_str(args, argc, 0), script_dir()));
}

static minic_val_t native_add_project(minic_val_t *args, int argc) {
	project_t *project   = minic_arg_p(args, argc, 0);
	char      *directory = arg_str(args, argc, 1);
	char      *from      = path_isabs(directory) ? directory : path_join(project->basedir, directory);
	if (!fs_exists(from)) {
		return minic_val_ptr(NULL);
	}
	project_t *sub = load_project(from, false);
	any_array_push(project->subprojects, sub);
	concat(project->asset_matchers, sub->asset_matchers);
	concat(project->shader_matchers, sub->shader_matchers);
	concat(project->defines, sub->defines);
	return minic_val_ptr(sub);
}

static minic_val_t native_add_cfiles(minic_val_t *args, int argc) {
	project_t *project = minic_arg_p(args, argc, 0);
	any_array_push(project->includes, arg_str(args, argc, 1));
	return minic_val_void();
}

static minic_val_t native_add_define(minic_val_t *args, int argc) {
	project_t *project = minic_arg_p(args, argc, 0);
	char      *define  = arg_str(args, argc, 1);
	if (!list_contains(project->defines, define)) {
		any_array_push(project->defines, define);
	}
	return minic_val_void();
}

static minic_val_t native_add_include_dir(minic_val_t *args, int argc) {
	project_t *project = minic_arg_p(args, argc, 0);
	char      *dir     = arg_str(args, argc, 1);
	if (!list_contains(project->includedirs, dir)) {
		any_array_push(project->includedirs, dir);
	}
	return minic_val_void();
}

static minic_val_t native_add_lib(minic_val_t *args, int argc) {
	project_t *project = minic_arg_p(args, argc, 0);
	any_array_push(project->libs, arg_str(args, argc, 1));
	return minic_val_void();
}

static minic_val_t native_add_shaders(minic_val_t *args, int argc) {
	project_t *project = minic_arg_p(args, argc, 0);
	any_array_push(project->shader_matchers, matcher_create(project, arg_str(args, argc, 1), NULL, 0));
	return minic_val_void();
}

static minic_val_t native_add_assets(minic_val_t *args, int argc) {
	project_t *project     = minic_arg_p(args, argc, 0);
	char      *destination = minic_arg_p(args, argc, 2);
	any_array_push(project->asset_matchers,
	               matcher_create(project, arg_str(args, argc, 1), destination != NULL ? string_copy(destination) : NULL, minic_arg_i(args, argc, 3)));
	return minic_val_void();
}

static minic_val_t native_project_flatten(minic_val_t *args, int argc) {
	project_t *project  = minic_arg_p(args, argc, 0);
	project->no_flatten = false;
	flatten_subprojects(project);
	return minic_val_void();
}

static minic_val_t native_flags_get(minic_val_t *args, int argc) {
	return minic_val_ptr(&flags);
}

static minic_val_t native_has_arg(minic_val_t *args, int argc) {
	char *arg = minic_arg_p(args, argc, 0);
	for (int i = 1; arg != NULL && i < gargc; ++i) {
		if (strcmp(gargv[i], arg) == 0) {
			return minic_val_int(1);
		}
	}
	return minic_val_int(0);
}

static minic_val_t native_fs_exists(minic_val_t *args, int argc) {
	return minic_val_int(fs_exists(script_path(arg_str(args, argc, 0))));
}

static minic_val_t native_fs_ensuredir(minic_val_t *args, int argc) {
	fs_ensuredir(path_normalize(script_path(arg_str(args, argc, 0))));
	return minic_val_void();
}

static minic_val_t native_fs_writefile(minic_val_t *args, int argc) {
	fs_writefile(script_path(arg_str(args, argc, 0)), arg_str(args, argc, 1));
	return minic_val_void();
}

static int compare_strings(const void *a, const void *b) {
	return strcmp(*(char **)a, *(char **)b);
}

static minic_val_t native_fs_list_files(minic_val_t *args, int argc) {
	char        *dir   = script_path(arg_str(args, argc, 0));
	any_array_t *all   = fs_readdir(dir);
	any_array_t *files = list_create();
	for (int i = 0; i < all->length; ++i) {
		if (!fs_isdir(path_join(dir, all->buffer[i]))) {
			any_array_push(files, all->buffer[i]);
		}
	}
	qsort(files->buffer, files->length, sizeof(char *), compare_strings);
	return minic_val_ptr(str_join(files, ","));
}

static minic_val_t native_os_popen(minic_val_t *args, int argc) {
	return minic_val_ptr(os_popen(arg_str(args, argc, 0)));
}

static minic_val_t native_version_code(minic_val_t *args, int argc) {
	time_t     now = time(NULL);
	struct tm *t   = localtime(&now);
	return minic_val_int((t->tm_year % 100) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday);
}

static minic_val_t native_date_string(minic_val_t *args, int argc) {
	time_t     now = time(NULL);
	struct tm *t   = gmtime(&now);
	return minic_val_ptr(string("%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday));
}

// string(fmt, ...) supports the printf conversions for ints, floats and strings
static minic_val_t native_string(minic_val_t *args, int argc) {
	char    *fmt = minic_arg_p(args, argc, 0);
	buffer_t sb  = {0};
	string_buffer_init(&sb);
	int arg = 1;
	while (fmt != NULL && *fmt != '\0') {
		if (*fmt != '%' || fmt[1] == '%') {
			char c[2] = {*fmt, '\0'};
			string_buffer_append(&sb, c);
			fmt += *fmt == '%' ? 2 : 1;
			continue;
		}
		char spec[32];
		int  sp    = 0;
		spec[sp++] = *fmt++;
		while (*fmt != '\0' && strchr("-+ #0123456789.", *fmt) && sp < 24) {
			spec[sp++] = *fmt++;
		}
		while (*fmt != '\0' && strchr("hlLzjt", *fmt)) {
			fmt++;
		}
		char c = *fmt;
		if (c == '\0') {
			break;
		}
		fmt++;
		spec[sp++]    = c;
		spec[sp]      = '\0';
		minic_val_t v = arg < argc ? args[arg++] : minic_val_int(0);
		char       *s;
		if (c == 's') {
			s = string(spec, v.type == MINIC_T_PTR && v.p != NULL ? (char *)v.p : "");
		}
		else if (strchr("eEfFgGaA", c)) {
			s = string(spec, minic_val_to_d(v));
		}
		else {
			s = string(spec, (int)minic_val_to_d(v));
		}
		string_buffer_append(&sb, s);
	}
	return minic_val_ptr(string_buffer_get(&sb));
}

void minic_register_builtins(void) {
	MINIC_STRUCT(ios_options_t);
	MINIC_S(version);
	MINIC_S(build);
	MINIC_END();

	MINIC_STRUCT(android_options_t);
	MINIC_S(package);
	MINIC_I(version_code);
	MINIC_S(version_name);
	MINIC_S(permissions);
	MINIC_END();

	MINIC_STRUCT(project_t);
	MINIC_S(name);
	MINIC_S(icon);
	MINIC_E(ios, ios_options_t);
	MINIC_E(android, android_options_t);
	MINIC_END();

	MINIC_STRUCT(flags_t);
	MINIC_S(name);
	MINIC_S(package);
	MINIC_S(dirname);
	MINIC_B(release);
	MINIC_B(embed);
	MINIC_B(with_physics);
	MINIC_B(with_d3dcompiler);
	MINIC_B(with_nfd);
	MINIC_B(with_compress);
	MINIC_B(with_image_write);
	MINIC_B(with_video_write);
	MINIC_B(with_eval);
	MINIC_B(with_plugins);
	MINIC_B(with_kong);
	MINIC_B(with_raytrace);
	MINIC_B(with_bc7);
	MINIC_B(with_audio);
	MINIC_B(with_gamepad);
	MINIC_B(idle_sleep);
	MINIC_B(export_version_info);
	MINIC_B(export_data_list);
	MINIC_END();

	static const char *platforms[] = {"PLATFORM_WINDOWS", "PLATFORM_LINUX", "PLATFORM_MACOS", "PLATFORM_IOS", "PLATFORM_ANDROID", "PLATFORM_WASM"};
	minic_register_enum("platform_t", platforms, NULL, 6);
	static const char *graphics[] = {"GRAPHICS_DIRECT3D12", "GRAPHICS_VULKAN", "GRAPHICS_METAL", "GRAPHICS_WEBGPU"};
	minic_register_enum("graphics_t", graphics, NULL, 4);
	minic_enum_const_add("ASSET_NOEMBED", ASSET_NOEMBED);
	minic_enum_const_add("ASSET_NOPROCESSING", ASSET_NOPROCESSING);
	minic_register_global("platform", &platform_value, MINIC_T_INT);
	minic_register_global("graphics", &graphics_value, MINIC_T_INT);

	minic_register("flags_get", "p:flags_t()", native_flags_get);
	minic_register("project_create", "p:project_t(p)", native_project_create);
	minic_register("add_project", "p:project_t(p,p)", native_add_project);
	minic_register("add_cfiles", "v(p,p)", native_add_cfiles);
	minic_register("add_define", "v(p,p)", native_add_define);
	minic_register("add_include_dir", "v(p,p)", native_add_include_dir);
	minic_register("add_lib", "v(p,p)", native_add_lib);
	minic_register("add_shaders", "v(p,p)", native_add_shaders);
	minic_register("add_assets", "v(p,p,p,i)", native_add_assets);
	minic_register("project_flatten", "v(p)", native_project_flatten);
	minic_register("has_arg", "b(p)", native_has_arg);
	minic_register("fs_exists", "b(p)", native_fs_exists);
	minic_register("fs_ensuredir", "v(p)", native_fs_ensuredir);
	minic_register("fs_writefile", "v(p,p)", native_fs_writefile);
	minic_register("fs_list_files", "p(p)", native_fs_list_files);
	minic_register("os_popen", "p(p)", native_os_popen);
	minic_register("version_code", "i()", native_version_code);
	minic_register("date_string", "p()", native_date_string);
	minic_register_native("string", native_string);
	script_register_natives();
}

static int enum_index(char *value, char **names, int count) {
	for (int i = 0; i < count; ++i) {
		if (strcmp(value, names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

project_t *load_project(char *directory, bool is_root) {
	char *dir = path_resolve(directory);

	if (is_root) {
		static char *platforms[] = {"windows", "linux", "macos", "ios", "android", "wasm"};
		static char *graphics[]  = {"direct3d12", "vulkan", "metal", "webgpu"};
		platform_value           = enum_index(goptions.target, platforms, 6);
		graphics_value           = enum_index(goptions.graphics, graphics, 4);
		memset(&flags, 0, sizeof(flags));
		flags.name    = "Armory";
		flags.package = "org.armory3d";
		flags.dirname = path_slashes(dir);
		flags.release = !goptions.debug;
	}

	char *file   = path_join(dir, "project.c");
	char *source = fs_readfile(file, NULL);
	if (source == NULL) {
		printf("Error: %s not found.\n", file);
		exit(1);
	}

	// Declared on the first line to keep the line numbers of errors intact
	source = string("flags_t *flags = flags_get(); %s", source);
	script_push_dir(dir);
	minic_ctx_t *ctx = minic_eval_named(source, file);
	script_pop_dir();

	if (minic_ctx_result(ctx) == -1.0f) {
		printf("Error: %s failed.\n", file);
		exit(1);
	}
	minic_val_t result  = minic_ctx_return_val(ctx);
	project_t  *project = result.type == MINIC_T_PTR ? result.p : NULL;
	if (project == NULL) {
		printf("Error: %s, main() has to return the project.\n", file);
		exit(1);
	}
	// The context stays alive, strings assigned by the script point into its memory

	if (is_root) {
		export_iron_project(project);
	}
	return project;
}

//  █████╗ ███████╗███████╗███████╗████████╗███████╗
// ██╔══██╗██╔════╝██╔════╝██╔════╝╚══██╔══╝██╔════╝
// ███████║███████╗███████╗█████╗     ██║   ███████╗
// ██╔══██║╚════██║╚════██║██╔══╝     ██║   ╚════██║
// ██║  ██║███████║███████║███████╗   ██║   ███████║
// ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝   ╚═╝   ╚══════╝

typedef struct {
	char *name;
	char *from;
	char *file;
	bool  noembed;
} asset_t;

typedef struct {
	char *name;
	char *files[2];
} compiled_shader_t;

void export_icon_ico(char *icon, char *to, char *from) {
	if (!fs_exists(path_join(from, icon))) {
		from = irondir;
		icon = "icon.png";
	}
	if (fs_exists(to) && fs_mtime(to) > fs_mtime(from)) {
		return;
	}
	export_ico(path_join(from, icon), to);
}

void export_png_icon(char *icon, char *to, int width, int height, char *from) {
	if (!fs_exists(path_join(from, icon))) {
		from = irondir;
		icon = "icon.png";
	}
	if (fs_exists(to) && fs_mtime(to) > fs_mtime(from)) {
		return;
	}
	export_png(path_join(from, icon), to, width, height);
}

static any_array_t *search_files2(char *current_dir, char *pattern) {
	any_array_t *result = list_create();
	if (!fs_exists(current_dir)) {
		return result;
	}
	current_dir        = path_join(current_dir);
	any_array_t *files = fs_readdir(current_dir);
	for (int i = 0; i < files->length; ++i) {
		char *file = path_join(current_dir, files->buffer[i]);
		if (fs_isdir(file)) {
			continue;
		}
		file = path_relative(current_dir, file);
		if (glob_matches(path_slashes(file), path_slashes(pattern))) {
			any_array_push(result, path_join(current_dir, path_slashes(file)));
		}
	}
	if (ends_with(pattern, "**")) {
		for (int i = 0; i < files->length; ++i) {
			char *d   = files->buffer[i];
			char *dir = path_join(current_dir, d);
			if (d[0] == '.' || !fs_isdir(dir)) {
				continue;
			}
			concat(result, search_files2(dir, pattern));
		}
	}
	return result;
}

static char *replace_pattern(char *pattern, char *value, char *filepath) {
	char *dir_value = path_relative(".", path_dirname(filepath));
	pattern         = str_replace(pattern, "{name}", value);
	return str_replace(pattern, dir_value[0] == '\0' ? "{dir}/" : "{dir}", dir_value);
}

// Returns the destination, sets the asset name
static char *create_export_info(char *filepath, bool keep_ext, matcher_t *m, char **name) {
	char *name_value  = path_basename_noext(filepath);
	char *destination = path_basename_noext(filepath);
	if (keep_ext || (m->options & ASSET_NOPROCESSING)) {
		destination = string("%s%s", destination, path_extname(filepath));
	}
	if (m->destination != NULL) {
		destination = replace_pattern(m->destination, destination, filepath);
	}
	if (keep_ext) {
		name_value = string("%s%s", name_value, path_extname(filepath));
	}
	if (name != NULL) {
		*name = name_value;
	}
	return path_normalize(destination);
}

static bool is_text_asset(char *file) {
	return ends_with(file, ".txt") || ends_with(file, ".md") || ends_with(file, ".json") || ends_with(file, ".c") || ends_with(file, ".html") ||
	       ends_with(file, ".js") || ends_with(file, ".ico");
}

static char *copy_image(char *from, char *to, bool embed) {
	char *to_full = path_join("build", embed ? "temp" : "out", to);
	to_full       = string("%s.k", to_full);
	if (!fs_exists(to_full) || fs_mtime(to_full) <= fs_mtime(from)) {
		fs_ensuredir(path_dirname(to_full));
		export_k(from, to_full);
	}
	return string("%s.k", to);
}

static char *copy_blob(char *from, char *to, bool embed) {
	fs_ensuredir(path_join("build", "out", path_dirname(to)));
	char *to_full = path_join("build", "out", to);
	if (embed && !is_text_asset(to)) {
		fs_ensuredir(path_join("build", "temp", path_dirname(to)));
		to_full = path_join("build", "temp", to);
	}
	if (!fs_exists(to_full) || fs_mtime(to_full) <= fs_mtime(from)) {
		fs_copyfile(from, to_full);
	}
	return to;
}

static void split_match(char *match, char **basedir, char **pattern) {
	match    = path_normalize(match);
	*basedir = js_substring(match, 0, str_last_index_of(match, path_sep));
	*pattern = match;
	if (path_isabs(*pattern)) {
		*pattern = path_relative(*basedir, *pattern);
	}
}

static any_array_t *export_assets(project_t *project) {
	any_array_t *assets = list_create();
	for (int i = 0; i < project->asset_matchers->length; ++i) {
		matcher_t *m = project->asset_matchers->buffer[i];
		char      *basedir;
		char      *pattern;
		split_match(m->match, &basedir, &pattern);
		any_array_t *files = search_files2(basedir, pattern);
		for (int j = 0; j < files->length; ++j) {
			char *file = files->buffer[j];
			printf("Exporting asset %d of %d (%s).\n", j + 1, files->length, path_basename(file));
			char    *ext   = str_lower(path_extname(file));
			bool     embed = flags.embed && !(m->options & ASSET_NOEMBED);
			asset_t *asset = calloc(1, sizeof(asset_t));
			asset->from    = file;
			asset->noembed = (m->options & ASSET_NOEMBED) != 0;
			if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".hdr") == 0) {
				char *destination = create_export_info(file, false, m, &asset->name);
				asset->file       = (m->options & ASSET_NOPROCESSING) ? copy_blob(file, destination, embed) : copy_image(file, destination, embed);
			}
			else {
				char *destination = create_export_info(file, true, m, &asset->name);
				asset->file       = copy_blob(file, destination, embed);
			}
			any_array_push(assets, asset);
		}
	}
	return assets;
}

static char *shader_type(void) {
	if (strcmp(goptions.graphics, "vulkan") == 0) {
		return "spirv";
	}
	else if (strcmp(goptions.graphics, "metal") == 0) {
		return "metal";
	}
	else if (strcmp(goptions.graphics, "direct3d12") == 0) {
		return "hlsl";
	}
	return "wgsl";
}

static void compile_shader(char *file, char *to_dir, char *temp) {
	char *type = shader_type();
	char *to   = path_join(to_dir, string("%s.%s", path_basename_noext(file), type));
	if (!fs_exists(file)) {
		return;
	}
	double from_time = fs_mtime(file);

	// Up to date when both outputs are newer than the source
	char  *ext      = strcmp(type, "hlsl") == 0 ? "d3d11" : type;
	char  *base     = js_substring(to, 0, str_last_index_of(to, ".") + 1);
	char  *outs[2]  = {string("%svert.%s", base, ext), string("%sfrag.%s", base, ext)};
	double out_time = 0;
	for (int i = 0; i < 2; ++i) {
		if (!fs_exists(outs[i])) {
			out_time = 0;
			break;
		}
		double t = fs_mtime(outs[i]);
		if (out_time == 0 || t < out_time) {
			out_time = t;
		}
	}
	if (from_time == 0 || (out_time != 0 && out_time > from_time)) {
		return;
	}
	fs_ensuredir(temp);
	ashader(type, file, to);
}

static any_array_t *export_shaders(project_t *project, char *to_dir, char *temp) {
	any_array_t *shaders = list_create();
	char        *type    = shader_type();
	if (strcmp(type, "hlsl") == 0) {
		type = "d3d11";
	}
	for (int i = 0; i < project->shader_matchers->length; ++i) {
		matcher_t *m = project->shader_matchers->buffer[i];
		char      *basedir;
		char      *pattern;
		split_match(m->match, &basedir, &pattern);
		any_array_t *files = search_files2(basedir, pattern);
		for (int j = 0; j < files->length; ++j) {
			char *file = files->buffer[j];
			printf("Compiling shader %d of %d (%s).\n", j + 1, files->length, path_basename(file));
			compile_shader(file, to_dir, temp);
			compiled_shader_t *shader = calloc(1, sizeof(compiled_shader_t));
			char              *base   = path_resolve("build", "temp", path_basename_noext(file));
			shader->files[0]          = string("%s.vert.%s", base, type);
			shader->files[1]          = string("%s.frag.%s", base, type);
			create_export_info(file, false, m, &shader->name);
			any_array_push(shaders, shader);
		}
	}
	return shaders;
}

static char *embed_symbol(char *file) {
	return str_replace(path_basename(file), ".", "_");
}

static void write_embed_header(any_array_t *assets, any_array_t *shaders) {
	any_array_t *files = list_create();
	for (int i = 0; i < assets->length; ++i) {
		asset_t *asset = assets->buffer[i];
		if (asset->noembed || is_text_asset(asset->from)) {
			continue;
		}
		any_array_push(files, path_resolve("build", "temp", asset->file));
	}
	for (int i = 0; i < shaders->length; ++i) {
		compiled_shader_t *shader = shaders->buffer[i];
		any_array_push(files, shader->files[0]);
		any_array_push(files, shader->files[1]);
	}
	if (files->length == 0) {
		return;
	}

	buffer_t sb = {0};
	string_buffer_init(&sb);
	string_buffer_append(&sb, "#pragma once\n");
	for (int i = 0; i < files->length; ++i) {
		char *file = files->buffer[i];
		string_buffer_append(&sb, string("const unsigned char %s[] = {\n", embed_symbol(file)));
		string_buffer_append(&sb, string("#embed \"%s\"\n", path_slashes(file)));
		string_buffer_append(&sb, "};\n");
	}
	string_buffer_append(&sb, "char *embed_keys[] = {\n");
	for (int i = 0; i < files->length; ++i) {
		char *f     = path_slashes(files->buffer[i]);
		int   index = str_last_index_of(f, "/temp/data/");
		f           = index > -1 ? js_substring(f, index + 11, (int)strlen(f)) : path_basename(files->buffer[i]);
		string_buffer_append(&sb, string("\"./data/%s\",\n", f));
	}
	string_buffer_append(&sb, "};\n");
	string_buffer_append(&sb, "const unsigned char *embed_values[] = {\n");
	for (int i = 0; i < files->length; ++i) {
		string_buffer_append(&sb, string("%s,\n", embed_symbol(files->buffer[i])));
	}
	string_buffer_append(&sb, "};\n");
	string_buffer_append(&sb, "const int embed_sizes[] = {\n");
	for (int i = 0; i < files->length; ++i) {
		string_buffer_append(&sb, string("sizeof(%s),\n", embed_symbol(files->buffer[i])));
	}
	string_buffer_append(&sb, "};\n");
	string_buffer_append(&sb, string("int embed_count = %d;\n", files->length));
	fs_writefile(path_join("build", "embed.h"), string_buffer_get(&sb));
}

void export_iron_project(project_t *project) {
	char *temp = path_join("build", "temp");
	fs_ensuredir(temp);
	fs_ensuredir(path_join("build", "out"));

	any_array_t *assets    = export_assets(project);
	char        *shaderdir = flags.embed ? path_join("build", "temp") : path_join("build", "out", "data");
	fs_ensuredir(shaderdir);
	any_array_t *shaders = export_shaders(project, shaderdir, temp);

	if (flags.embed) {
		write_embed_header(assets, shaders);
	}
	fs_ensuredir(path_join("build", "out"));
}
