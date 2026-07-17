#pragma once

#include "iron_array.h"
#include "iron_global.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef IRON_WINDOWS
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

#ifdef IRON_LINUX
extern bool path_is_protected_linux;
#endif

string_array_t *path_mesh_formats(void);
string_array_t *path_texture_formats(void);
string_array_t *path_text_formats(void);

char *path_data(void);
char *path_to_relative(char *from, char *to);
char *path_normalize(char *path);
char *path_base_dir(char *path);
char *path_base_name(char *path);
bool  path_is_mesh(char *path);
bool  path_is_texture(char *path);
bool  path_is_sound(char *path);
bool  path_is_font(char *path);
bool  path_is_project(char *path);
bool  path_is_plugin(char *path);
bool  path_is_json(char *path);
bool  path_is_text(char *path);
bool  path_is_ext_format(char *path);
bool  path_is_lut(char *path);
bool  path_is_known(char *path);
bool  path_is_base_color_tex(char *p);
bool  path_is_opacity_tex(char *p);
bool  path_is_normal_map_tex(char *p);
bool  path_is_occlusion_tex(char *p);
bool  path_is_roughness_tex(char *p);
bool  path_is_metallic_tex(char *p);
bool  path_is_displacement_tex(char *p);
bool  path_is_folder(char *p);
bool  path_is_protected(void);
char *path_join(char *a, char *b);
