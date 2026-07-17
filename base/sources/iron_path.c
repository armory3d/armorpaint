#include "iron_path.h"

#include <string.h>

#include "iron_array.h"
#include "iron_file.h"
#include "iron_gc.h"
#include "iron_map.h"
#include "iron_string.h"

#ifdef IRON_LINUX
bool path_is_protected_linux = false;
#endif

string_array_t        *_path_mesh_formats     = NULL;
string_array_t        *_path_texture_formats  = NULL;
string_array_t        *_path_sound_formats    = NULL;
string_array_t        *_path_text_formats     = NULL;
static string_array_t *_path_base_color_ext   = NULL;
static string_array_t *_path_opacity_ext      = NULL;
static string_array_t *_path_normal_map_ext   = NULL;
static string_array_t *_path_occlusion_ext    = NULL;
static string_array_t *_path_roughness_ext    = NULL;
static string_array_t *_path_metallic_ext     = NULL;
static string_array_t *_path_displacement_ext = NULL;

string_array_t *path_mesh_formats(void) {
	if (_path_mesh_formats == NULL) {
		_path_mesh_formats = string_array_create(0);
		gc_root(_path_mesh_formats);
		string_array_push(_path_mesh_formats, "obj");
		string_array_push(_path_mesh_formats, "blend");
	}
	return _path_mesh_formats;
}

string_array_t *path_texture_formats(void) {
	if (_path_texture_formats == NULL) {
		_path_texture_formats = string_array_create(0);
		gc_root(_path_texture_formats);
		string_array_push(_path_texture_formats, "jpg");
		string_array_push(_path_texture_formats, "jpeg");
		string_array_push(_path_texture_formats, "png");
		string_array_push(_path_texture_formats, "tga");
		string_array_push(_path_texture_formats, "bmp");
		string_array_push(_path_texture_formats, "psd");
		string_array_push(_path_texture_formats, "gif");
		string_array_push(_path_texture_formats, "hdr");
		string_array_push(_path_texture_formats, "k");
	}
	return _path_texture_formats;
}

string_array_t *path_sound_formats(void) {
	if (_path_sound_formats == NULL) {
		_path_sound_formats = string_array_create(0);
		gc_root(_path_sound_formats);
		string_array_push(_path_sound_formats, "wav");
		string_array_push(_path_sound_formats, "ogg");
	}
	return _path_sound_formats;
}

string_array_t *path_text_formats(void) {
	if (_path_text_formats == NULL) {
		_path_text_formats = string_array_create(0);
		gc_root(_path_text_formats);
		string_array_push(_path_text_formats, "txt");
		string_array_push(_path_text_formats, "json");
	}
	return _path_text_formats;
}

string_array_t *path_base_color_ext(void) {
	if (_path_base_color_ext == NULL) {
		_path_base_color_ext = string_array_create(0);
		gc_root(_path_base_color_ext);
		string_array_push(_path_base_color_ext, "albedo");
		string_array_push(_path_base_color_ext, "alb");
		string_array_push(_path_base_color_ext, "basecol");
		string_array_push(_path_base_color_ext, "basecolor");
		string_array_push(_path_base_color_ext, "diffuse");
		string_array_push(_path_base_color_ext, "diff");
		string_array_push(_path_base_color_ext, "base");
		string_array_push(_path_base_color_ext, "bc");
		string_array_push(_path_base_color_ext, "d");
		string_array_push(_path_base_color_ext, "color");
		string_array_push(_path_base_color_ext, "col");
	}
	return _path_base_color_ext;
}

string_array_t *path_opacity_ext(void) {
	if (_path_opacity_ext == NULL) {
		_path_opacity_ext = string_array_create(0);
		gc_root(_path_opacity_ext);
		string_array_push(_path_opacity_ext, "opac");
		string_array_push(_path_opacity_ext, "opacity");
		string_array_push(_path_opacity_ext, "alpha");
	}
	return _path_opacity_ext;
}

string_array_t *path_normal_map_ext(void) {
	if (_path_normal_map_ext == NULL) {
		_path_normal_map_ext = string_array_create(0);
		gc_root(_path_normal_map_ext);
		string_array_push(_path_normal_map_ext, "normal");
		string_array_push(_path_normal_map_ext, "normals");
		string_array_push(_path_normal_map_ext, "nor");
		string_array_push(_path_normal_map_ext, "n");
		string_array_push(_path_normal_map_ext, "nrm");
		string_array_push(_path_normal_map_ext, "normalgl");
	}
	return _path_normal_map_ext;
}

string_array_t *path_occlusion_ext(void) {
	if (_path_occlusion_ext == NULL) {
		_path_occlusion_ext = string_array_create(0);
		gc_root(_path_occlusion_ext);
		string_array_push(_path_occlusion_ext, "ao");
		string_array_push(_path_occlusion_ext, "occlusion");
		string_array_push(_path_occlusion_ext, "ambientOcclusion");
		string_array_push(_path_occlusion_ext, "o");
		string_array_push(_path_occlusion_ext, "occ");
	}
	return _path_occlusion_ext;
}

string_array_t *path_roughness_ext(void) {
	if (_path_roughness_ext == NULL) {
		_path_roughness_ext = string_array_create(0);
		gc_root(_path_roughness_ext);
		string_array_push(_path_roughness_ext, "roughness");
		string_array_push(_path_roughness_ext, "rough");
		string_array_push(_path_roughness_ext, "r");
		string_array_push(_path_roughness_ext, "rgh");
	}
	return _path_roughness_ext;
}

string_array_t *path_metallic_ext(void) {
	if (_path_metallic_ext == NULL) {
		_path_metallic_ext = string_array_create(0);
		gc_root(_path_metallic_ext);
		string_array_push(_path_metallic_ext, "metallic");
		string_array_push(_path_metallic_ext, "metal");
		string_array_push(_path_metallic_ext, "metalness");
		string_array_push(_path_metallic_ext, "m");
		string_array_push(_path_metallic_ext, "met");
	}
	return _path_metallic_ext;
}

string_array_t *path_displacement_ext(void) {
	if (_path_displacement_ext == NULL) {
		_path_displacement_ext = string_array_create(0);
		gc_root(_path_displacement_ext);
		string_array_push(_path_displacement_ext, "displacement");
		string_array_push(_path_displacement_ext, "height");
		string_array_push(_path_displacement_ext, "h");
		string_array_push(_path_displacement_ext, "disp");
	}
	return _path_displacement_ext;
}

static char *data_path(void) {
#ifdef IRON_ANDROID
	return "data" PATH_SEP;
#else
	return "." PATH_SEP "data" PATH_SEP;
#endif
}

char *path_data(void) {
	return string("%s%s%s", iron_internal_files_location(), PATH_SEP, data_path());
}

char *path_to_relative(char *from, char *to) {
	any_array_t *a = string_split(from, PATH_SEP);
	any_array_t *b = string_split(to, PATH_SEP);
	if (a->length > 0 && !path_is_folder(from)) {
		array_pop(a);
	}
	while (a->length > 0 && b->length > 0) {
		char *a0 = a->buffer[0];
		char *b0 = b->buffer[0];
		if (string_equals(a0, b0)) {
			array_shift(a);
			array_shift(b);
		}
		else {
			break;
		}
	}
	char *p = "";
	for (uint32_t i = 0; i < a->length; ++i) {
		p = string("%s.." PATH_SEP, p);
	}
	p = string("%s%s", p, string_array_join(b, PATH_SEP));
	return p;
}

char *path_normalize(char *path) {
	size_t path_len = strlen(path);
	if (path_len > 0 && ends_with(path, PATH_SEP)) {
		path = substring(path, 0, path_len - 1);
	}
	any_array_t *ar = string_split(path, PATH_SEP);
	uint32_t     i  = 0;
	while (i < ar->length) {
		if (i > 0) {
			char *ar_i   = ar->buffer[i];
			char *ar_i_1 = ar->buffer[i - 1];
			if (string_equals(ar_i, "..") && !string_equals(ar_i_1, "..")) {
				array_splice(ar, i - 1, 2);
				i--;
			}
			else {
				i++;
			}
		}
		else {
			i++;
		}
	}
	return string_array_join(ar, PATH_SEP);
}

char *path_base_dir(char *path) {
	int32_t last = string_last_index_of(path, PATH_SEP);
	return substring(path, 0, last + 1);
}

char *path_base_name(char *path) {
	int32_t last_sep = string_last_index_of(path, PATH_SEP);
	int32_t last_dot = string_last_index_of(path, ".");
	return substring(path, last_sep + 1, last_dot);
}

bool path_is_mesh(char *path) {
	char           *p       = to_lower_case(path);
	string_array_t *formats = path_mesh_formats();
	for (uint32_t i = 0; i < formats->length; ++i) {
		char *s   = formats->buffer[i];
		char *ext = string(".%s", s);
		if (ends_with(p, ext)) {
			return true;
		}
	}
	return false;
}

bool path_is_texture(char *path) {
	char           *p       = to_lower_case(path);
	string_array_t *formats = path_texture_formats();
	for (uint32_t i = 0; i < formats->length; ++i) {
		char *s   = formats->buffer[i];
		char *ext = string(".%s", s);
		if (ends_with(p, ext)) {
			return true;
		}
	}
	return false;
}

bool path_is_sound(char *path) {
	char           *p       = to_lower_case(path);
	string_array_t *formats = path_sound_formats();
	for (uint32_t i = 0; i < formats->length; ++i) {
		char *s   = formats->buffer[i];
		char *ext = string(".%s", s);
		if (ends_with(p, ext)) {
			return true;
		}
	}
	return false;
}

bool path_is_font(char *path) {
	char *p = to_lower_case(path);
	return ends_with(p, ".ttf") || ends_with(p, ".ttc") || ends_with(p, ".otf");
}

bool path_is_project(char *path) {
	char *p = to_lower_case(path);
	return ends_with(p, ".arm");
}

bool path_is_plugin(char *path) {
	char *p = to_lower_case(path);
	return ends_with(p, ".c");
}

bool path_is_json(char *path) {
	char *p = to_lower_case(path);
	return ends_with(p, ".json");
}

bool path_is_text(char *path) {
	char           *p       = to_lower_case(path);
	string_array_t *formats = path_text_formats();
	for (uint32_t i = 0; i < formats->length; ++i) {
		char *s   = formats->buffer[i];
		char *ext = string(".%s", s);
		if (ends_with(p, ext)) {
			return true;
		}
	}
	return false;
}

bool path_is_ext_format(char *path) {
	char *p = to_lower_case(path);
	return ends_with(p, ".stl") || ends_with(p, ".svg");
}

bool path_is_lut(char *path) {
	char *p = to_lower_case(path);
	return ends_with(p, ".cube");
}

bool path_is_known(char *path) {
	return path_is_mesh(path) || path_is_texture(path) || path_is_sound(path) || path_is_font(path) || path_is_project(path) || path_is_plugin(path) ||
	       path_is_text(path) || path_is_ext_format(path) || path_is_lut(path);
}

bool path_check_ext(char *p, string_array_t *exts) {
	p = string_replace_all(p, "-", "_");
	for (uint32_t i = 0; i < exts->length; ++i) {
		char *ext            = exts->buffer[i];
		char *ext_underscore = string("_%s", ext);
		if (ends_with(p, ext_underscore)) {
			return true;
		}
		char *ext_underscore_mid = string("_%s_", ext);
		if (string_index_of(p, ext_underscore_mid) >= 0 && !ends_with(p, "_preview") && !ends_with(p, "_icon")) {
			return true;
		}
	}
	return false;
}

bool path_is_base_color_tex(char *p) {
	return path_check_ext(p, path_base_color_ext());
}

bool path_is_opacity_tex(char *p) {
	return path_check_ext(p, path_opacity_ext());
}

bool path_is_normal_map_tex(char *p) {
	return path_check_ext(p, path_normal_map_ext());
}

bool path_is_occlusion_tex(char *p) {
	return path_check_ext(p, path_occlusion_ext());
}

bool path_is_roughness_tex(char *p) {
	return path_check_ext(p, path_roughness_ext());
}

bool path_is_metallic_tex(char *p) {
	return path_check_ext(p, path_metallic_ext());
}

bool path_is_displacement_tex(char *p) {
	return path_check_ext(p, path_displacement_ext());
}

bool path_is_folder(char *p) {
	char        *p_slash = string_replace_all(p, "\\", "/");
	any_array_t *split   = string_split(p_slash, "/");
	char        *last    = array_pop(split);
	return string_index_of(last, ".") < 0;
}

bool path_is_protected(void) {
#ifdef IRON_WINDOWS
	return string_index_of(iron_internal_files_location(), "Program Files") >= 0;
#elif defined(IRON_LINUX)
	return path_is_protected_linux;
#elif defined(IRON_ANDROID)
	return true;
#elif defined(IRON_IOS)
	return true;
#else
	return false;
#endif
}

char *path_join(char *a, char *b) {
	char *path = a;
	if (!ends_with(path, PATH_SEP)) {
		path = string("%s" PATH_SEP, path);
	}
	path = string("%s%s", path, b);
	return path;
}
