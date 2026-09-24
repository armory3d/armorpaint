#pragma once

// project.c api
//
// project.c files are interpreted by amake with minic, this header is for documentation
// Each project.c defines main(), which creates the project and returns it:
//
//   #include "../base/tools/amake/amake.h"
//
//   project_t *main() {
//       project_t *project = project_create("MyProject");
//       add_project(project, "../base");
//       add_cfiles(project, "sources/*.c");
//       return project;
//   }
//
// Relative paths resolve against the directory of the project.c file
// Patterns: "*" matches within a directory, "**" matches across directories

#include <stdbool.h>

typedef enum {
	PLATFORM_WINDOWS,
	PLATFORM_LINUX,
	PLATFORM_MACOS,
	PLATFORM_IOS,
	PLATFORM_ANDROID,
	PLATFORM_WASM,
} platform_t;

typedef enum {
	GRAPHICS_DIRECT3D12,
	GRAPHICS_VULKAN,
	GRAPHICS_METAL,
	GRAPHICS_WEBGPU,
} graphics_t;

// add_assets options
enum {
	ASSET_NOEMBED      = 1, // Keep the file on disk when embedding
	ASSET_NOPROCESSING = 2, // Copy images as they are instead of converting them to .k
};

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
	char             *name;
	char             *icon; // Relative to the root project, defaults to "icon.png"
	ios_options_t     ios;
	android_options_t android;
} project_t;

// Build flags, set by the root project and read by the projects it adds
typedef struct {
	char *name;
	char *package;
	char *dirname; // Root project directory
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

extern platform_t platform;
extern graphics_t graphics;
extern flags_t   *flags;

project_t *project_create(char *name);
project_t *add_project(project_t *project, char *directory); // Evaluates directory/project.c, NULL when the directory does not exist
void       add_cfiles(project_t *project, char *pattern);
void       add_define(project_t *project, char *define);
void       add_include_dir(project_t *project, char *dir);
void       add_lib(project_t *project, char *lib);
void       add_shaders(project_t *project, char *pattern);
void       add_assets(project_t *project, char *pattern, char *destination, int options); // destination like "data/{name}"
void       project_flatten(project_t *project);                                           // Merge the added projects into this one

bool  has_arg(char *arg); // Command line contains arg
bool  fs_exists(char *path);
void  fs_ensuredir(char *path);
void  fs_writefile(char *path, char *data);
char *fs_list_files(char *dir); // Sorted, comma separated file names
char *os_popen(char *command);  // Returns stdout
int   version_code(void);       // Date as yymmdd
char *date_string(void);        // Date as yyyy-mm-dd
char *string(char *format, ...);

// General purpose, also used by scripts run with `amake --c <file.c> [args]`
// A script defines int main(), relative paths resolve against the working directory

typedef struct {
	char **buffer;
	int    length;
} string_array_t;

typedef struct {
	int count;
} map_t; // String to string map

char           *script_arg(int i); // Arguments after the script path, NULL when out of range
void            print(char *s);
string_array_t *fs_readdir(char *dir);
char           *fs_readfile(char *path); // NULL when missing
int             string_index_of(char *s, char *search, int from);
int             string_length(char *s);
bool            string_equals(char *a, char *b);
bool            starts_with(char *s, char *start);
bool            ends_with(char *s, char *end);
char           *substring(char *s, int start, int end);
map_t          *map_create(void);
void            map_set(map_t *map, char *key, char *value);
char           *map_get(map_t *map, char *key);             // NULL when missing
map_t          *json_parse_map(char *json);                 // Flat {"key": "value"} objects
char           *json_stringify_map(map_t *map, int indent); // Keys sorted, like JSON.stringify(map, sorted_keys, indent)
