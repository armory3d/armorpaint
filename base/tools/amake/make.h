#pragma once

// Internal amake interface, see amake.h for the project.c api

#include "iron_array.h"
#include "iron_string.h"
#include <stdbool.h>

// Options

typedef struct {
	char *target;   // windows, linux, macos, ios, android, wasm
	char *graphics; // direct3d12, vulkan, metal, webgpu
	char *ccompiler;
	char *arch;
	char *build_path; // Debug or Release
	bool  compile;
	bool  run;
	bool  debug;
} options_t;

extern options_t goptions;
extern int       gargc;
extern char    **gargv;
extern char     *path_sep;
extern char     *other_path_sep;
extern char     *makedir; // base/tools
extern char     *irondir; // base

// Project

typedef struct {
	char *version;
	char *build;
} ios_options_t;

typedef struct {
	char *package;
	int   version_code;
	char *version_name;
	char *permissions; // Comma separated
} android_options_t;

typedef struct {
	char *file;
	char *project_dir;
	char *project_name;
} project_file_t;

typedef struct {
	char *match;
	char *destination;
	int   options; // ASSET_NOEMBED | ASSET_NOPROCESSING
} matcher_t;

#define ASSET_NOEMBED      1
#define ASSET_NOPROCESSING 2

typedef struct project {
	// Visible to project.c
	char             *name;
	char             *icon;
	ios_options_t     ios;
	android_options_t android;

	char        *safe_name;
	char        *basedir;
	char        *uuid;
	char        *debugdir;
	char        *executable_name;
	char        *c_std;
	bool         lto;
	bool         no_flatten;
	any_array_t *files;           // project_file_t *
	any_array_t *includes;        // add_cfiles patterns
	any_array_t *includedirs;     // char *
	any_array_t *defines;         // char *
	any_array_t *libs;            // char *
	any_array_t *cflags;          // char *
	any_array_t *cmd_args;        // char *
	any_array_t *javadirs;        // char *
	any_array_t *subprojects;     // project_t *
	any_array_t *asset_matchers;  // matcher_t *
	any_array_t *shader_matchers; // matcher_t *
} project_t;

typedef struct {
	char *name;
	char *package;
	char *dirname; // Root project directory, always with forward slashes
	bool  release;
	bool  embed;
	bool  with_physics;
	bool  with_d3dcompiler;
	bool  with_nfd;
	bool  with_compress;
	bool  with_image_write;
	bool  with_video_write;
	bool  with_eval;
	bool  with_plugins;
	bool  with_kong;
	bool  with_raytrace;
	bool  with_bc7;
	bool  with_audio;
	bool  with_gamepad;
	bool  idle_sleep;
	bool  export_version_info;
	bool  export_data_list;
} flags_t;

extern flags_t flags;

project_t *project_create(char *name, char *basedir);
project_t *load_project(char *directory, bool is_root);
void       project_search_files(project_t *project);
void       project_internal_flatten(project_t *project);
void       export_iron_project(project_t *project);

// Exporters

void export_solution(project_t *project);

// Assets

void export_icon_ico(char *icon, char *to, char *from);
void export_png_icon(char *icon, char *to, int width, int height, char *from);

// Scripts

void  script_register_natives(void);
void  script_push_dir(char *dir);
void  script_pop_dir(void);
char *script_path(char *p);
int   run_script(char *file, int argc, char **argv);

// aimage.c, ashader.c
void export_k(const char *from, const char *to);
void export_ico(const char *from, const char *to);
void export_png(const char *from, const char *to, int width, int height);
int  ashader(char *shader_lang, char *from, char *to);

// Strings, all results are heap allocated and leaked

any_array_t *list_create(void);
bool         list_contains(any_array_t *list, char *s);
char        *str_lower(char *s);
char        *str_upper(char *s);
int          str_index_of(char *s, char *search);
int          str_last_index_of(char *s, char *search);
char        *js_substring(char *s, int start, int end); // String.prototype.substring semantics
char        *js_substr(char *s, int start, int length); // String.prototype.substr semantics
char        *str_replace(char *s, char *search, char *replace);
char        *str_trim(char *s);
char        *str_join(any_array_t *parts, char *sep);
char        *json_escape(char *s);
bool         glob_matches(char *text, char *pattern);

// Paths

#define path_join(...)    path_join_n((char *[]){__VA_ARGS__, NULL})
#define path_resolve(...) path_resolve_n((char *[]){__VA_ARGS__, NULL})
char *path_join_n(char **parts);
char *path_resolve_n(char **parts);
bool  path_isabs(char *p);
char *path_relative(char *from, char *to);
char *path_normalize(char *p);
char *path_extname(char *p);
char *path_basename(char *p);
char *path_basename_noext(char *p);
char *path_dirname(char *p);
char *path_slashes(char *p); // Backslashes to forward slashes

// File system

bool         fs_exists(char *p);
bool         fs_isdir(char *p);
double       fs_mtime(char *p); // Milliseconds
any_array_t *fs_readdir(char *p);
char        *fs_readfile(char *p, int *size);
void         fs_writefile(char *p, char *data);
void         fs_writefile_bytes(char *p, char *data, int size);
void         fs_copyfile(char *from, char *to);
void         fs_ensuredir(char *dir);
void         fs_copydir(char *from, char *to);

// OS

char *os_platform(void); // linux, win32, darwin
char *os_cwd(void);
char *os_env(char *name);
int   os_exec(any_array_t *args, char *cwd, char **out_stdout);
char *os_popen(char *cmd);
int   os_cpus_length(void);
void  os_chmod_exec(char *p);
char *sys_dir(void);
char *random_uuid(void);
char *new_path_id(char *path);
